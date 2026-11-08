/* C-B07 문자열 기본 구현: strlen / strcpy / strcmp / strncpy / strlcpy
 * libc 결과와 대조. 버퍼는 malloc으로 "딱 맞게" 잡아서 ASan이 1바이트 넘침도 잡게 한다:
 *   cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o sol_asan && ./sol_asan
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>   /* 비교용 libc: strlen strcpy strcmp strncpy strlcpy(macOS/BSD) */

/* ---------------- Part A: strlen / strcpy / strcmp ---------------- */
static size_t my_strlen(const char *s) {
    const char *p = s;
    while (*p != '\0') p++;
    return (size_t)(p - s);                  /* NUL 은 길이에 안 들어간다 */
}
static char *my_strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++) != '\0') { }      /* NUL 까지 복사한 뒤 멈춘다 */
    return dst;
}
static int my_strcmp(const char *a, const char *b) {
    const unsigned char *p = (const unsigned char *)a;   /* C11 7.24.4p1: unsigned char 로 비교 */
    const unsigned char *q = (const unsigned char *)b;
    while (*p != '\0' && *p == *q) { p++; q++; }
    return (int)*p - (int)*q;
}

/* ---------------- Part B: strncpy / strlcpy ---------------- */
/* 정확히 n 바이트를 쓴다: src 가 짧으면 나머지를 0으로 채우고, 길이 >= n 이면 NUL 없음 */
static char *my_strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}
/* size > 0 이면 항상 NUL 종료. 반환값 = strlen(src) → 반환값 >= size 이면 잘린 것 */
static size_t my_strlcpy(char *dst, const char *src, size_t size) {
    size_t len = my_strlen(src);
    if (size != 0) {
        size_t k = (len < size - 1) ? len : size - 1;
        for (size_t i = 0; i < k; i++) dst[i] = src[i];
        dst[k] = '\0';
    }
    return len;
}

/* ---------------- Part C: 장치 이름 필드 + UART 명령 줄 버퍼 ---------------- */
#define NAME_FIELD 16    /* 플래시 레코드의 고정 폭 이름 필드: NUL 패딩, 정확히 16자면 NUL 없음 */
#define LINE_MAX   32    /* UART 한 줄 버퍼: 내용 최대 31자 + NUL */

typedef struct {
    uint32_t magic;
    char     name[NAME_FIELD];
    uint32_t crc;
} dev_record_t;

typedef struct {
    char   buf[LINE_MAX];
    size_t len;
    bool   overflow;
} line_buf_t;

/* 이름이 16자를 넘으면 -1 (레코드 변경 없음). 아니면 필드 전체를 덮어쓰고(남는 칸 0) 0 */
static int dev_set_name(dev_record_t *r, const char *name) {
    if (my_strlen(name) > NAME_FIELD) return -1;
    my_strncpy(r->name, name, NAME_FIELD);
    return 0;
}
/* 필드 → C 문자열. 필드 밖을 절대 읽지 않는다. out 은 항상 NUL 종료(outsz > 0).
 * 반환 = 필드 안 이름 길이 (strlcpy 처럼 반환값 >= outsz 이면 잘린 것) */
static size_t dev_get_name(const dev_record_t *r, char *out, size_t outsz) {
    size_t n = 0;
    while (n < NAME_FIELD && r->name[n] != '\0') n++;
    if (outsz != 0) {
        size_t k = (n < outsz - 1) ? n : outsz - 1;
        for (size_t i = 0; i < k; i++) out[i] = r->name[i];
        out[k] = '\0';
    }
    return n;
}
/* UART RX 바이트 하나 (ISR 에서 불린다고 가정).
 * '\r' 무시. '\n' 이면 buf 를 NUL 종료하고 1 (넘쳤던 줄이면 -1) 반환 후 다음 줄 준비.
 * 31자를 넘는 바이트는 버리고 overflow 표시. 그 외 0. */
static int line_feed(line_buf_t *lb, char c) {
    if (c == '\r') return 0;
    if (c == '\n') {
        int ret = lb->overflow ? -1 : 1;
        lb->buf[lb->len] = '\0';             /* len <= LINE_MAX-1 이므로 안전 */
        lb->len = 0;
        lb->overflow = false;
        return ret;
    }
    if (lb->len < LINE_MAX - 1) lb->buf[lb->len++] = c;   /* NUL 자리 1칸은 남긴다 */
    else lb->overflow = true;
    return 0;
}

/* (주어진 코드) "NAME <이름>" / "CLEAR" 명령 실행. 0=성공, -1=인자 오류, -2=모르는 명령 */
static int cmd_exec(dev_record_t *r, char *line) {
    char *arg = line;
    while (*arg != '\0' && *arg != ' ') arg++;
    if (*arg == ' ') *arg++ = '\0';          /* 제자리 토큰 분리: 첫 공백을 NUL 로 */
    if (my_strcmp(line, "NAME") == 0)  return dev_set_name(r, arg);
    if (my_strcmp(line, "CLEAR") == 0) { memset(r->name, 0, NAME_FIELD); return 0; }
    return -2;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static int sgn(int v) { return (v > 0) - (v < 0); }
/* 딱 len+1 바이트짜리 힙 사본 — 한 바이트라도 더 읽으면 ASan 이 잡는다 */
static char *heap_dup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
static int feed_str(line_buf_t *lb, const char *s) {
    int last = 0;
    while (*s) last = line_feed(lb, *s++);
    return last;
}

int main(void) {
    static const char *const strs[] = { "", "a", "hello", "exactly16chars!!", "\x80\xff" };
    const size_t NSTR = sizeof strs / sizeof strs[0];

    /* Part A — strlen, strcpy (정확한 크기의 힙 버퍼) */
    for (size_t i = 0; i < NSTR; i++) {
        char *src = heap_dup(strs[i]);
        size_t len = strlen(strs[i]);
        char *dst = malloc(len + 1);
        if (!src || !dst) return 2;
        memset(dst, 0xAA, len + 1);
        CHECK(my_strlen(src) == len);
        CHECK(my_strcpy(dst, src) == dst && memcmp(dst, strs[i], len + 1) == 0);
        free(src); free(dst);
    }
    /* Part A — strcmp: 부호만 libc 와 같으면 된다 */
    static const char *const pairs[][2] = {
        { "", "" }, { "", "a" }, { "abc", "abd" }, { "abc", "ab" },
        { "A", "a" }, { "\x80", "a" }, { "a\xff", "a\x01" },
    };
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++)
        CHECK(sgn(my_strcmp(pairs[i][0], pairs[i][1])) == sgn(strcmp(pairs[i][0], pairs[i][1])));
    CHECK(my_strcmp("\x80", "a") > 0);       /* 0x80 > 0x61 — signed char 로 비교하면 음수가 나온다 */

    /* Part B — strncpy vs libc: 정확히 n 바이트 버퍼 두 개를 비교 */
    static const struct { const char *src; size_t n; } nc[] = {
        { "", 4 }, { "abc", 8 }, { "abcd", 4 }, { "abcdef", 4 }, { "x", 1 },
    };
    for (size_t i = 0; i < sizeof nc / sizeof nc[0]; i++) {
        char *a = malloc(nc[i].n), *b = malloc(nc[i].n);
        if (!a || !b) return 2;
        memset(a, 0xAA, nc[i].n); memset(b, 0xAA, nc[i].n);
        CHECK(my_strncpy(a, nc[i].src, nc[i].n) == a);
        strncpy(b, nc[i].src, nc[i].n);
        CHECK(memcmp(a, b, nc[i].n) == 0);
        free(a); free(b);
    }
    {
        char g[12];
        memset(g, 0xAA, sizeof g);
        my_strncpy(g, "ab", 8);              /* 8바이트만 써야 한다: "ab" + 0 6개 */
        CHECK(memcmp(g, "ab\0\0\0\0\0\0", 8) == 0 && (unsigned char)g[8] == 0xAA);
        memset(g, 0xAA, sizeof g);
        my_strncpy(g, "abcd", 4);            /* 정확히 n 길이 → NUL 없음 */
        CHECK(memcmp(g, "abcd", 4) == 0 && (unsigned char)g[4] == 0xAA);
        my_strncpy(g, "zzz", 0);             /* n == 0 → 아무것도 안 쓴다 */
        CHECK(g[0] == 'a');
    }

    /* Part B — strlcpy vs libc: 반환값과 버퍼 내용 */
    static const struct { const char *src; size_t size; } lc[] = {
        { "", 4 }, { "abc", 4 }, { "abcd", 4 }, { "hello world", 4 }, { "abc", 1 },
    };
    for (size_t i = 0; i < sizeof lc / sizeof lc[0]; i++) {
        char *a = malloc(lc[i].size), *b = malloc(lc[i].size);
        if (!a || !b) return 2;
        memset(a, 0xAA, lc[i].size); memset(b, 0xAA, lc[i].size);
        size_t ra = my_strlcpy(a, lc[i].src, lc[i].size);
        size_t rb = strlcpy(b, lc[i].src, lc[i].size);
        CHECK(ra == rb && memcmp(a, b, lc[i].size) == 0);
        free(a); free(b);
    }
    {
        char one = 'Q';
        CHECK(my_strlcpy(&one, "abc", 0) == 3 && one == 'Q');   /* size 0: 쓰지 않고 길이만 */
        char buf[8] = { 0 };
        CHECK(my_strlcpy(buf, "1234567", sizeof buf) <  sizeof buf);  /* 7자 = 딱 맞음 */
        CHECK(my_strlcpy(buf, "12345678", sizeof buf) >= sizeof buf); /* 8자 = 잘림 감지 */
        CHECK(strcmp(buf, "1234567") == 0);
    }

    /* Part C — 고정 폭 이름 필드 */
    dev_record_t rec;
    memset(&rec, 0xEE, sizeof rec);          /* 쓰레기 값으로 시작 */
    char out[32];
    CHECK(dev_set_name(&rec, "sensor-01") == 0);
    CHECK(memcmp(rec.name, "sensor-01\0\0\0\0\0\0\0", NAME_FIELD) == 0);   /* 남는 칸 0 패딩 */
    CHECK(dev_set_name(&rec, "ABCDEFGHIJKLMNOP") == 0);                  /* 16자: NUL 없이 꽉 참 */
    CHECK(dev_get_name(&rec, out, sizeof out) == 16 && strcmp(out, "ABCDEFGHIJKLMNOP") == 0);
    CHECK(dev_set_name(&rec, "ABCDEFGHIJKLMNOPQ") == -1);                /* 17자: 거부 */
    CHECK(memcmp(rec.name, "ABCDEFGHIJKLMNOP", NAME_FIELD) == 0);        /* 레코드 그대로 */
    CHECK(dev_get_name(&rec, out, 8) == 16 && strcmp(out, "ABCDEFG") == 0);  /* 작은 out: 잘림 */

    /* Part C — UART 줄 버퍼 + 명령 */
    line_buf_t lb = { .len = 0, .overflow = false };
    CHECK(feed_str(&lb, "NAME sensor-01\r") == 0);                      /* 아직 줄 미완성 */
    CHECK(line_feed(&lb, '\n') == 1 && strcmp(lb.buf, "NAME sensor-01") == 0);  /* \r 제거 */
    CHECK(cmd_exec(&rec, lb.buf) == 0 && dev_get_name(&rec, out, sizeof out) == 9);
    CHECK(strcmp(out, "sensor-01") == 0);
    CHECK(feed_str(&lb, "NAME 0123456789abcdef0123456789\n") == 1);     /* 31자 = 경계, OK */
    CHECK(my_strlen(lb.buf) == 31 && cmd_exec(&rec, lb.buf) == -1);    /* 이름 26자 > 16 → 거부 */
    CHECK(feed_str(&lb, "HELLO\n") == 1 && cmd_exec(&rec, lb.buf) == -2); /* 모르는 명령 */
    CHECK(feed_str(&lb, "NAME 0123456789abcdef0123456789X\n") == -1);   /* 32자 = 넘침 */
    CHECK(feed_str(&lb, "CLEAR\n") == 1 && cmd_exec(&rec, lb.buf) == 0); /* 다음 줄은 정상 */
    CHECK(dev_get_name(&rec, out, sizeof out) == 0 && out[0] == '\0');

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
