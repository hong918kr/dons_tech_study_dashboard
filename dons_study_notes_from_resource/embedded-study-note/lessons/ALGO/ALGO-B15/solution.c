/* ALGO-B15 명령 문자열 파싱 기초: 토큰화와 검증
 *
 * UART 콘솔 명령 파서 (힙 없음, 재귀 없음, 고정 크기 버퍼):
 *   Part A  cmd_tokenize()  제자리(in-place) 토큰화: 공백/탭/CR/LF, 최대 줄 길이, 최대 토큰 수
 *           kw_eq()         ASCII 대소문자 무시 키워드 비교 (toupper/로캘 안 씀)
 *   Part B  parse_u32()     엄격한 숫자 파서: 10진 / 0x 16진, '_' 자리 구분자, 넘침 검출
 *   Part C  cmd_parse()     문법 검사 → 태그 붙은 공용체(tagged union) cmd_t + 에러 코드
 *           lb_rx_byte()    UART RX ISR 이 1바이트씩 넣는 줄 버퍼 (백스페이스, 넘침 → 줄 끝까지 버림)
 *           lb_take()       태스크 문맥에서 완성된 줄을 꺼내고 버퍼를 ISR 에 돌려준다
 *
 * 명령 문법 (키워드는 대소문자 무시):
 *   HELP
 *   SET LED <n> ON|OFF        n = 0..7
 *   READ  <addr> <len>        addr: 10진 또는 0x16진, '_' 허용, 4바이트 정렬 / len: 10진 1..64
 *   WRITE <addr> <value>      addr: 위와 같음 / value: 0..0xFFFFFFFF
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CMD_MAX_LINE   80 /* 종단 NUL 을 뺀 최대 글자 수 */
#define CMD_MAX_TOKENS 6  /* 이보다 많으면 TOOMANY (EXTRA 와 구분) */
#define LED_COUNT      8
#define READ_MAX_LEN   64

typedef enum {
    CP_OK = 0,
    CP_ERR_EMPTY,   /* 토큰 0개 (공백뿐인 줄)            */
    CP_ERR_TOOLONG, /* 줄 길이 > CMD_MAX_LINE            */
    CP_ERR_TOOMANY, /* 토큰 수 > CMD_MAX_TOKENS          */
    CP_ERR_UNKNOWN, /* 모르는 명령어                     */
    CP_ERR_MISSING, /* 필요한 인자가 없음                */
    CP_ERR_EXTRA,   /* 인자가 남음                       */
    CP_ERR_BADNUM,  /* 숫자 모양이 아님                  */
    CP_ERR_RANGE,   /* 숫자 모양은 맞지만 범위 밖        */
    CP_ERR_ALIGN,   /* 주소가 4바이트 정렬이 아님        */
    CP_ERR_BADARG,  /* 키워드 자리에 다른 단어 (LED, ON) */
    CP_ERR__COUNT
} cp_err_t;

static const char *const cp_err_name[CP_ERR__COUNT] = {
    [CP_OK] = "OK",           [CP_ERR_EMPTY] = "EMPTY",     [CP_ERR_TOOLONG] = "TOOLONG",
    [CP_ERR_TOOMANY] = "TOOMANY", [CP_ERR_UNKNOWN] = "UNKNOWN", [CP_ERR_MISSING] = "MISSING",
    [CP_ERR_EXTRA] = "EXTRA", [CP_ERR_BADNUM] = "BADNUM",   [CP_ERR_RANGE] = "RANGE",
    [CP_ERR_ALIGN] = "ALIGN", [CP_ERR_BADARG] = "BADARG",
};
static const char *const cp_err_msg[CP_ERR__COUNT] = {
    [CP_OK] = "ok",
    [CP_ERR_EMPTY] = "empty line",
    [CP_ERR_TOOLONG] = "line too long",
    [CP_ERR_TOOMANY] = "too many tokens",
    [CP_ERR_UNKNOWN] = "unknown command",
    [CP_ERR_MISSING] = "missing argument",
    [CP_ERR_EXTRA] = "unexpected extra argument",
    [CP_ERR_BADNUM] = "malformed number",
    [CP_ERR_RANGE] = "number out of range",
    [CP_ERR_ALIGN] = "address not 4-byte aligned",
    [CP_ERR_BADARG] = "invalid keyword argument",
};
static const char *cp_strerror(cp_err_t e) {
    return (unsigned)e < CP_ERR__COUNT ? cp_err_msg[e] : "unknown error";
}

typedef enum { CMD_NONE = 0, CMD_HELP, CMD_SET_LED, CMD_READ, CMD_WRITE } cmd_kind_t;

typedef struct {
    cmd_kind_t kind; /* 태그: 아래 union 중 어느 쪽이 유효한가. 에러면 CMD_NONE */
    int err_tok;     /* 에러를 낸 토큰 번호 (0 = 명령어). 해당 없음 = -1        */
    union {
        struct { uint8_t idx; bool on; } led;
        struct { uint32_t addr, len; } read;
        struct { uint32_t addr, value; } write;
    } u;
} cmd_t;

/* ======================= Part A: 토큰화 ======================= */
static bool is_sep(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* line 안의 토큰 끝마다 '\0' 을 써 넣고 argv[k] 가 각 토큰 첫 글자를 가리키게 한다.
 * 반환: CP_OK (*argc = 토큰 수) / CP_ERR_TOOLONG (line 은 그대로) / CP_ERR_TOOMANY. */
static cp_err_t cmd_tokenize(char *line, char *argv[], int max_tok, int *argc) {
    size_t n = 0;
    while (line[n] != '\0')                  /* 최대 CMD_MAX_LINE+1 글자만 본다 */
        if (++n > CMD_MAX_LINE) return CP_ERR_TOOLONG;
    int k = 0;
    char *p = line;
    for (;;) {
        while (is_sep(*p)) p++;              /* 구분자 여러 개 = 하나 */
        if (*p == '\0') break;
        if (k == max_tok) return CP_ERR_TOOMANY;
        argv[k++] = p;                       /* 토큰 시작 */
        while (*p != '\0' && !is_sep(*p)) p++;
        if (*p != '\0') *p++ = '\0';         /* 토큰 끝: 구분자 자리를 NUL 로 */
    }
    *argc = k;
    return CP_OK;
}

static char ascii_upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }

/* tok 이 키워드 kw(대문자 리터럴)와 대소문자 무시로 같은가. 길이도 같아야 한다. */
static bool kw_eq(const char *tok, const char *kw) {
    while (*tok != '\0' && *kw != '\0') {
        if (ascii_upper(*tok) != *kw) return false;
        tok++;
        kw++;
    }
    return *tok == '\0' && *kw == '\0';
}

/* ======================= Part B: 숫자 ======================= */
static int digit_of(char c, unsigned base) {
    int d = (c >= '0' && c <= '9') ? c - '0'
          : (c >= 'a' && c <= 'f') ? c - 'a' + 10
          : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
    return (d >= 0 && (unsigned)d < base) ? d : -1;
}

/* s 전체가 0..max 범위의 부호 없는 정수인지. 10진, 또는 allow_hex 면 0x/0X 16진.
 * '_' 는 숫자와 숫자 사이에만 (0x4000_0010 OK, _1 1_ 1__0 0x_1 은 BADNUM).
 * 모양이 틀리면 BADNUM (넘쳤더라도), 모양은 맞는데 > max 이면 RANGE. 성공 시에만 *out 을 쓴다. */
static cp_err_t parse_u32(const char *s, uint32_t max, bool allow_hex, uint32_t *out) {
    unsigned base = 10;
    const char *p = s;
    if (allow_hex && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    }
    const uint32_t cutoff = UINT32_MAX / base, cutlim = UINT32_MAX % base;
    uint32_t acc = 0;
    bool over = false, prev_digit = false;
    int ndig = 0;
    for (; *p != '\0'; p++) {
        if (*p == '_') {
            if (!prev_digit || digit_of(p[1], base) < 0) return CP_ERR_BADNUM;
            prev_digit = false;
            continue;
        }
        int d = digit_of(*p, base);
        if (d < 0) return CP_ERR_BADNUM;
        if (acc > cutoff || (acc == cutoff && (uint32_t)d > cutlim)) over = true;
        else if (!over) acc = acc * base + (uint32_t)d;
        ndig++;
        prev_digit = true;
    }
    if (ndig == 0) return CP_ERR_BADNUM;
    if (over || acc > max) return CP_ERR_RANGE;
    *out = acc;
    return CP_OK;
}

/* ======================= Part C: 문법 → 구조체 ======================= */
static cp_err_t fail(cmd_t *out, cp_err_t e, int tok) {
    memset(out, 0, sizeof *out);
    out->kind = CMD_NONE;
    out->err_tok = tok;
    return e;
}

/* line 을 제자리에서 토큰화해 out 을 채운다. 검사 순서:
 *   ① 줄 길이/토큰 수  ② 빈 줄  ③ 명령어
 *   ④ 인자를 왼쪽부터: 없으면 MISSING, 있으면 모양(BADNUM/BADARG) → 범위(RANGE)
 *   ⑤ 남는 인자 EXTRA  ⑥ 명령 전체 의미: 정렬(ALIGN), addr+len 32비트 넘침(RANGE) */
static cp_err_t cmd_parse(char *line, cmd_t *out) {
    char *argv[CMD_MAX_TOKENS];
    int argc = 0, need = 0;
    cp_err_t e;
    memset(out, 0, sizeof *out);
    out->err_tok = -1;

    if ((e = cmd_tokenize(line, argv, CMD_MAX_TOKENS, &argc)) != CP_OK) return fail(out, e, -1);
    if (argc == 0) return fail(out, CP_ERR_EMPTY, -1);

    if (kw_eq(argv[0], "HELP")) {
        out->kind = CMD_HELP;
        need = 1;
    } else if (kw_eq(argv[0], "SET")) {
        uint32_t n = 0;
        if (argc < 2) return fail(out, CP_ERR_MISSING, 1);
        if (!kw_eq(argv[1], "LED")) return fail(out, CP_ERR_BADARG, 1);
        if (argc < 3) return fail(out, CP_ERR_MISSING, 2);
        if ((e = parse_u32(argv[2], LED_COUNT - 1, true, &n)) != CP_OK) return fail(out, e, 2);
        if (argc < 4) return fail(out, CP_ERR_MISSING, 3);
        if (kw_eq(argv[3], "ON")) out->u.led.on = true;
        else if (kw_eq(argv[3], "OFF")) out->u.led.on = false;
        else return fail(out, CP_ERR_BADARG, 3);
        out->u.led.idx = (uint8_t)n;
        out->kind = CMD_SET_LED;
        need = 4;
    } else if (kw_eq(argv[0], "READ") || kw_eq(argv[0], "WRITE")) {
        bool rd = (ascii_upper(argv[0][0]) == 'R');
        uint32_t addr = 0, v = 0;
        if (argc < 2) return fail(out, CP_ERR_MISSING, 1);
        if ((e = parse_u32(argv[1], UINT32_MAX, true, &addr)) != CP_OK) return fail(out, e, 1);
        if (argc < 3) return fail(out, CP_ERR_MISSING, 2);
        if (rd) e = parse_u32(argv[2], READ_MAX_LEN, false, &v); /* len: 10진만 */
        else e = parse_u32(argv[2], UINT32_MAX, true, &v);
        if (e == CP_OK && rd && v == 0) e = CP_ERR_RANGE;         /* len 1..64 */
        if (e != CP_OK) return fail(out, e, 2);
        if (rd) { out->kind = CMD_READ; out->u.read.addr = addr; out->u.read.len = v; }
        else { out->kind = CMD_WRITE; out->u.write.addr = addr; out->u.write.value = v; }
        need = 3;
    } else {
        return fail(out, CP_ERR_UNKNOWN, 0);
    }

    if (argc > need) return fail(out, CP_ERR_EXTRA, need);

    if (out->kind == CMD_READ || out->kind == CMD_WRITE) {
        uint32_t addr = (out->kind == CMD_READ) ? out->u.read.addr : out->u.write.addr;
        if (addr & 3u) return fail(out, CP_ERR_ALIGN, 1);
        /* addr + len - 1 <= 0xFFFFFFFF 를 64비트 없이: len - 1 <= UINT32_MAX - addr */
        if (out->kind == CMD_READ && out->u.read.len - 1u > UINT32_MAX - addr)
            return fail(out, CP_ERR_RANGE, 2);
    }
    return CP_OK;
}

/* ======================= Part C: ISR 줄 버퍼 ======================= */
typedef enum { LB_NONE = 0, LB_LINE, LB_TOOLONG } lb_status_t;

typedef struct {
    char buf[CMD_MAX_LINE + 1];
    uint16_t len;
    bool discarding;         /* 넘친 줄: 다음 CR/LF 까지 버리는 중 */
    bool too_long;           /* 완성된 "줄" 이 넘친 줄이었나 (ready 와 함께 전달) */
    atomic_bool ready;       /* ISR → 태스크 소유권 플래그 (release / acquire) */
    uint32_t dropped_bytes;  /* ready 동안 들어와 버린 바이트 (진단용) */
    uint32_t junk_bytes;     /* 제어 문자 등 무시한 바이트 (진단용) */
} linebuf_t;

/* UART RX ISR 에서 바이트마다 호출: O(1), 루프·라이브러리 호출 없음.
 * ready == true 인 동안 buf/len 은 태스크 것이므로 건드리지 않는다. */
static void lb_rx_byte(linebuf_t *lb, uint8_t c) {
    if (atomic_load_explicit(&lb->ready, memory_order_acquire)) {
        lb->dropped_bytes++;
        return;
    }
    if (c == '\r' || c == '\n') {
        if (lb->discarding) {                      /* 넘친 줄의 끝 → TOOLONG 으로 알린다 */
            lb->discarding = false;
            lb->too_long = true;
            lb->len = 0;
            lb->buf[0] = '\0';
            atomic_store_explicit(&lb->ready, true, memory_order_release);
            return;
        }
        if (lb->len == 0) return;                  /* 빈 줄, 또는 CRLF 의 LF */
        lb->buf[lb->len] = '\0';
        lb->too_long = false;
        atomic_store_explicit(&lb->ready, true, memory_order_release);
        return;
    }
    if (lb->discarding) return;
    if (c == 0x08 || c == 0x7F) {                  /* BS, DEL: 한 글자 지우기 */
        if (lb->len > 0) lb->len--;
        return;
    }
    if (c != '\t' && (c < 0x20 || c > 0x7E)) {     /* NUL·ESC·8비트 바이트는 줄에 넣지 않는다 */
        lb->junk_bytes++;
        return;
    }
    if (lb->len == CMD_MAX_LINE) {                 /* 81번째 글자: 줄 전체 폐기 모드 */
        lb->discarding = true;
        lb->len = 0;
        return;
    }
    lb->buf[lb->len++] = (char)c;
}

/* 태스크 문맥: 완성된 줄이 있으면 dst 로 복사하고 버퍼를 ISR 에 돌려준다. */
static lb_status_t lb_take(linebuf_t *lb, char *dst, size_t cap) {
    if (!atomic_load_explicit(&lb->ready, memory_order_acquire)) return LB_NONE;
    lb_status_t st = lb->too_long ? LB_TOOLONG : LB_LINE;
    size_t n = lb->len;
    if (n >= cap) n = cap - 1;
    memcpy(dst, lb->buf, n);
    dst[n] = '\0';
    lb->len = 0;
    lb->too_long = false;
    atomic_store_explicit(&lb->ready, false, memory_order_release); /* 소유권 반납은 마지막에 */
    return st;
}

/* ---- 주어진 코드: 태스크 루프의 콘솔 처리 (TODO 아님) ---- */
static void cmd_format(cp_err_t e, const cmd_t *c, char *reply, size_t cap) {
    if (e != CP_OK) {
        if (c->err_tok >= 0)
            snprintf(reply, cap, "ERR %s @%d: %s", cp_err_name[e], c->err_tok, cp_strerror(e));
        else
            snprintf(reply, cap, "ERR %s: %s", cp_err_name[e], cp_strerror(e));
        return;
    }
    switch (c->kind) {
    case CMD_HELP: snprintf(reply, cap, "OK HELP"); break;
    case CMD_SET_LED: snprintf(reply, cap, "OK LED %u %s", (unsigned)c->u.led.idx, c->u.led.on ? "ON" : "OFF"); break;
    case CMD_READ: snprintf(reply, cap, "OK READ 0x%08lX %lu", (unsigned long)c->u.read.addr, (unsigned long)c->u.read.len); break;
    case CMD_WRITE: snprintf(reply, cap, "OK WRITE 0x%08lX 0x%08lX", (unsigned long)c->u.write.addr, (unsigned long)c->u.write.value); break;
    default: snprintf(reply, cap, "ERR ?"); break;
    }
}

/* 줄이 있으면 파싱해서 응답을 쓰고 true. 줄이 없거나 공백뿐인 줄이면 false (프롬프트만 다시). */
static bool console_poll(linebuf_t *lb, char *reply, size_t cap) {
    char line[CMD_MAX_LINE + 1];
    cmd_t c;
    lb_status_t st = lb_take(lb, line, sizeof line);
    if (st == LB_NONE) return false;
    cp_err_t e;
    if (st == LB_TOOLONG) {
        memset(&c, 0, sizeof c);
        c.err_tok = -1;
        e = CP_ERR_TOOLONG;
    } else {
        e = cmd_parse(line, &c);
        if (e == CP_ERR_EMPTY) return false;
    }
    cmd_format(e, &c, reply, cap);
    return true;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng_state = 0x2026B15u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

/* 참조 토큰화 (테스트 쪽, 다른 방식): 토큰을 복사본 배열에 모은다 */
static int ref_tokenize(const char *s, char tok[][CMD_MAX_LINE + 1], int max) {
    int k = 0;
    size_t i = 0;
    while (s[i] != '\0') {
        if (is_sep(s[i])) { i++; continue; }
        if (k == max) return -1;
        size_t j = 0;
        while (s[i] != '\0' && !is_sep(s[i])) tok[k][j++] = s[i++];
        tok[k++][j] = '\0';
    }
    return k;
}

/* 참조 숫자 파서 (테스트 쪽): 모양 검사 후 '_' 를 지우고 libc strtoull 로 값 계산 */
static cp_err_t ref_parse_u32(const char *s, uint32_t max, bool allow_hex, uint32_t *out) {
    int base = 10;
    const char *b = s;
    if (allow_hex && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; b = s + 2; }
    size_t n = strlen(b);
    if (n == 0 || b[0] == '_' || b[n - 1] == '_' || strstr(b, "__") != NULL) return CP_ERR_BADNUM;
    char digits[256];
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (b[i] == '_') continue;
        if (digit_of(b[i], (unsigned)base) < 0) return CP_ERR_BADNUM;
        if (k < sizeof digits - 1) digits[k++] = b[i];
    }
    digits[k] = '\0';
    errno = 0;
    unsigned long long v = strtoull(digits, NULL, base);
    if (errno == ERANGE || v > max) return CP_ERR_RANGE;
    *out = (uint32_t)v;
    return CP_OK;
}

typedef struct {
    const char *line;
    cp_err_t err;
    int err_tok;
    cmd_kind_t kind;
    uint32_t a, b; /* LED: idx,on / READ: addr,len / WRITE: addr,value */
} parse_case_t;

static const parse_case_t k_cases[] = {
    /* ---- 정상 입력 ---- */
    {"SET LED 3 ON",                 CP_OK, -1, CMD_SET_LED, 3, 1},
    {"set led 7 off",                CP_OK, -1, CMD_SET_LED, 7, 0},
    {"  SeT\tLeD   0  oN  \r\n",     CP_OK, -1, CMD_SET_LED, 0, 1},
    {"SET LED 0x7 OFF",              CP_OK, -1, CMD_SET_LED, 7, 0},
    {"READ 0x4000_0010 4",           CP_OK, -1, CMD_READ, 0x40000010u, 4},
    {"read 0X4000001c 64\n",         CP_OK, -1, CMD_READ, 0x4000001Cu, 64},
    {"READ 1_073_741_840 1",         CP_OK, -1, CMD_READ, 0x40000010u, 1},
    {"READ 0 1",                     CP_OK, -1, CMD_READ, 0, 1},
    {"READ 0xFFFF_FFC0 64",          CP_OK, -1, CMD_READ, 0xFFFFFFC0u, 64},
    {"READ 0x40000010 010",          CP_OK, -1, CMD_READ, 0x40000010u, 10},
    {"WRITE 0x4000_0020 0xDEAD_BEEF", CP_OK, -1, CMD_WRITE, 0x40000020u, 0xDEADBEEFu},
    {"write 0x0 4294967295",         CP_OK, -1, CMD_WRITE, 0, 0xFFFFFFFFu},
    {"HELP",                         CP_OK, -1, CMD_HELP, 0, 0},
    {"help\r",                       CP_OK, -1, CMD_HELP, 0, 0},
    /* ---- 줄 / 토큰 수준 에러 ---- */
    {"",                             CP_ERR_EMPTY, -1, CMD_NONE, 0, 0},
    {" \t \r\n",                     CP_ERR_EMPTY, -1, CMD_NONE, 0, 0},
    {"a b c d e f g",                CP_ERR_TOOMANY, -1, CMD_NONE, 0, 0},
    {"FLASH 0 1",                    CP_ERR_UNKNOWN, 0, CMD_NONE, 0, 0},
    {"READX 0 4",                    CP_ERR_UNKNOWN, 0, CMD_NONE, 0, 0},
    {"REA 0 4",                      CP_ERR_UNKNOWN, 0, CMD_NONE, 0, 0},
    /* ---- 인자 개수 ---- */
    {"SET",                          CP_ERR_MISSING, 1, CMD_NONE, 0, 0},
    {"SET LED",                      CP_ERR_MISSING, 2, CMD_NONE, 0, 0},
    {"SET LED 3",                    CP_ERR_MISSING, 3, CMD_NONE, 0, 0},
    {"READ",                         CP_ERR_MISSING, 1, CMD_NONE, 0, 0},
    {"READ 0x4000_0010",             CP_ERR_MISSING, 2, CMD_NONE, 0, 0},
    {"SET LED 3 ON NOW",             CP_ERR_EXTRA, 4, CMD_NONE, 0, 0},
    {"READ 0x10 4 4",                CP_ERR_EXTRA, 3, CMD_NONE, 0, 0},
    {"HELP me",                      CP_ERR_EXTRA, 1, CMD_NONE, 0, 0},
    /* ---- 키워드 인자 ---- */
    {"SET FAN 3 ON",                 CP_ERR_BADARG, 1, CMD_NONE, 0, 0},
    {"SET LED 3 1",                  CP_ERR_BADARG, 3, CMD_NONE, 0, 0},
    {"SET LED 3 ONN",                CP_ERR_BADARG, 3, CMD_NONE, 0, 0},
    /* ---- 숫자 모양 ---- */
    {"SET LED three ON",             CP_ERR_BADNUM, 2, CMD_NONE, 0, 0},
    {"SET LED -1 ON",                CP_ERR_BADNUM, 2, CMD_NONE, 0, 0},
    {"SET LED +3 ON",                CP_ERR_BADNUM, 2, CMD_NONE, 0, 0},
    {"READ 0x 4",                    CP_ERR_BADNUM, 1, CMD_NONE, 0, 0},
    {"READ 0x_4000 4",               CP_ERR_BADNUM, 1, CMD_NONE, 0, 0},
    {"READ 0x4000__0010 4",          CP_ERR_BADNUM, 1, CMD_NONE, 0, 0},
    {"READ 0x4000_ 4",               CP_ERR_BADNUM, 1, CMD_NONE, 0, 0},
    {"READ 0x4000_0010g 4",          CP_ERR_BADNUM, 1, CMD_NONE, 0, 0},
    {"READ 0x10 0x4",                CP_ERR_BADNUM, 2, CMD_NONE, 0, 0},
    {"READ 0x10 4abc",               CP_ERR_BADNUM, 2, CMD_NONE, 0, 0},
    {"READ 99999999999z 4",          CP_ERR_BADNUM, 1, CMD_NONE, 0, 0},
    /* ---- 범위 ---- */
    {"SET LED 8 ON",                 CP_ERR_RANGE, 2, CMD_NONE, 0, 0},
    {"READ 0x1_0000_0000 4",         CP_ERR_RANGE, 1, CMD_NONE, 0, 0},
    {"READ 99999999999 4",           CP_ERR_RANGE, 1, CMD_NONE, 0, 0},
    {"READ 0x10 0",                  CP_ERR_RANGE, 2, CMD_NONE, 0, 0},
    {"READ 0x10 65",                 CP_ERR_RANGE, 2, CMD_NONE, 0, 0},
    {"WRITE 0x10 4294967296",        CP_ERR_RANGE, 2, CMD_NONE, 0, 0},
    {"READ 0xFFFF_FFFC 8",           CP_ERR_RANGE, 2, CMD_NONE, 0, 0},
    /* ---- 정렬 / 우선순위 ---- */
    {"READ 0x4000_0011 4",           CP_ERR_ALIGN, 1, CMD_NONE, 0, 0},
    {"WRITE 0x2 0",                  CP_ERR_ALIGN, 1, CMD_NONE, 0, 0},
    {"READ 0x11 4 junk",             CP_ERR_EXTRA, 3, CMD_NONE, 0, 0},
    {"READ 0x10 0 junk",             CP_ERR_RANGE, 2, CMD_NONE, 0, 0},
};

static bool run_case(const parse_case_t *t) {
    char buf[256];
    cmd_t c;
    snprintf(buf, sizeof buf, "%s", t->line);
    cp_err_t e = cmd_parse(buf, &c);
    bool ok = (e == t->err) && (c.err_tok == t->err_tok) && (c.kind == t->kind);
    if (ok && e == CP_OK) {
        switch (c.kind) {
        case CMD_SET_LED: ok = c.u.led.idx == t->a && (uint32_t)c.u.led.on == t->b; break;
        case CMD_READ: ok = c.u.read.addr == t->a && c.u.read.len == t->b; break;
        case CMD_WRITE: ok = c.u.write.addr == t->a && c.u.write.value == t->b; break;
        default: break;
        }
    }
    if (!ok)
        printf("  case \"%s\": got %s tok=%d kind=%d, want %s tok=%d kind=%d\n", t->line,
               (unsigned)e < CP_ERR__COUNT ? cp_err_name[e] : "?", c.err_tok, (int)c.kind,
               cp_err_name[t->err], t->err_tok, (int)t->kind);
    return ok;
}

static void feed(linebuf_t *lb, const char *bytes, size_t n) {
    for (size_t i = 0; i < n; i++) lb_rx_byte(lb, (uint8_t)bytes[i]);
}
static void feed_str(linebuf_t *lb, const char *s) { feed(lb, s, strlen(s)); }

#ifndef ALGO_B15_NO_MAIN
int main(void) {
    /* ---------- Part A: 토큰화 ---------- */
    {
        char l1[] = "  READ\t0x10   4 \r\n";
        char *av[CMD_MAX_TOKENS];
        int ac = -1;
        CHECK(cmd_tokenize(l1, av, CMD_MAX_TOKENS, &ac) == CP_OK && ac == 3);
        CHECK(ac == 3 && strcmp(av[0], "READ") == 0 && strcmp(av[1], "0x10") == 0 && strcmp(av[2], "4") == 0);
        CHECK(ac == 3 && av[0] == l1 + 2);                       /* 제자리: 복사가 아니라 원래 버퍼를 가리킨다 */
        char l2[] = "";
        ac = -1;
        CHECK(cmd_tokenize(l2, av, CMD_MAX_TOKENS, &ac) == CP_OK && ac == 0);
        char l3[] = "a b c d e f";                               /* 정확히 6개: OK */
        CHECK(cmd_tokenize(l3, av, CMD_MAX_TOKENS, &ac) == CP_OK && ac == 6);
        char l4[] = "a b c d e f g";                             /* 7개: TOOMANY */
        CHECK(cmd_tokenize(l4, av, CMD_MAX_TOKENS, &ac) == CP_ERR_TOOMANY);
        char l5[CMD_MAX_LINE + 2];
        memset(l5, 'x', CMD_MAX_LINE); l5[CMD_MAX_LINE] = '\0';  /* 80글자: OK */
        CHECK(cmd_tokenize(l5, av, CMD_MAX_TOKENS, &ac) == CP_OK && ac == 1);
        memset(l5, 'x', CMD_MAX_LINE + 1); l5[CMD_MAX_LINE + 1] = '\0'; /* 81글자: TOOLONG, 버퍼 그대로 */
        CHECK(cmd_tokenize(l5, av, CMD_MAX_TOKENS, &ac) == CP_ERR_TOOLONG && strlen(l5) == CMD_MAX_LINE + 1);

        CHECK(kw_eq("read", "READ") && kw_eq("ReAd", "READ") && kw_eq("READ", "READ"));
        CHECK(!kw_eq("REA", "READ") && !kw_eq("READX", "READ") && !kw_eq("", "READ"));
        CHECK(!kw_eq("RE@D", "READ") && !kw_eq("\xC2\xAA", "READ"));  /* '@'+1 == 'A' 함정, 8비트 바이트 */

        /* 전수 대조: 알파벳 {a, B, ' ', '\t', '\n'} 로 만든 길이 0~8 문자열 전부 (488,281개) */
        static const char alpha[] = {'a', 'B', ' ', '\t', '\n'};
        long mismatches = 0, total = 0;
        for (int len = 0; len <= 8; len++) {
            long count = 1;
            for (int i = 0; i < len; i++) count *= 5;
            for (long code = 0; code < count; code++) {
                char s[16], work[16], ref[CMD_MAX_TOKENS + 1][CMD_MAX_LINE + 1];
                long x = code;
                for (int i = 0; i < len; i++) { s[i] = alpha[x % 5]; x /= 5; }
                s[len] = '\0';
                memcpy(work, s, (size_t)len + 1);
                int rk = ref_tokenize(s, ref, CMD_MAX_TOKENS);
                int k = -1;
                cp_err_t e = cmd_tokenize(work, av, CMD_MAX_TOKENS, &k);
                bool same = (rk < 0) ? (e == CP_ERR_TOOMANY) : (e == CP_OK && k == rk);
                for (int i = 0; same && rk > 0 && i < rk; i++) same = strcmp(av[i], ref[i]) == 0;
                if (!same) mismatches++;
                total++;
            }
        }
        CHECK(total == 488281 && mismatches == 0);
        if (mismatches) printf("  tokenize: %ld/%ld mismatches vs reference\n", mismatches, total);
    }

    /* ---------- Part B: 숫자 ---------- */
    {
        uint32_t v = 12345;
        CHECK(parse_u32("0x4000_0010", UINT32_MAX, true, &v) == CP_OK && v == 0x40000010u);
        CHECK(parse_u32("0XdeadBEEF", UINT32_MAX, true, &v) == CP_OK && v == 0xDEADBEEFu);
        CHECK(parse_u32("4294967295", UINT32_MAX, true, &v) == CP_OK && v == UINT32_MAX);
        CHECK(parse_u32("1_000_000", UINT32_MAX, false, &v) == CP_OK && v == 1000000u);
        CHECK(parse_u32("010", UINT32_MAX, true, &v) == CP_OK && v == 10u);   /* 8진 아님 */
        v = 777;
        CHECK(parse_u32("4294967296", UINT32_MAX, true, &v) == CP_ERR_RANGE && v == 777);  /* 실패 시 *out 불변 */
        CHECK(parse_u32("0x1_0000_0000", UINT32_MAX, true, &v) == CP_ERR_RANGE);
        CHECK(parse_u32("99999999999999999999999", UINT32_MAX, true, &v) == CP_ERR_RANGE);
        CHECK(parse_u32("65", 64, false, &v) == CP_ERR_RANGE && parse_u32("64", 64, false, &v) == CP_OK);
        CHECK(parse_u32("", UINT32_MAX, true, &v) == CP_ERR_BADNUM);
        CHECK(parse_u32("0x", UINT32_MAX, true, &v) == CP_ERR_BADNUM);
        CHECK(parse_u32("-1", UINT32_MAX, true, &v) == CP_ERR_BADNUM && parse_u32("+1", UINT32_MAX, true, &v) == CP_ERR_BADNUM);
        CHECK(parse_u32(" 1", UINT32_MAX, true, &v) == CP_ERR_BADNUM);            /* strtoul 은 받아 준다 */
        CHECK(parse_u32("_1", UINT32_MAX, true, &v) == CP_ERR_BADNUM && parse_u32("1_", UINT32_MAX, true, &v) == CP_ERR_BADNUM);
        CHECK(parse_u32("1__0", UINT32_MAX, true, &v) == CP_ERR_BADNUM && parse_u32("0x_1", UINT32_MAX, true, &v) == CP_ERR_BADNUM);
        CHECK(parse_u32("0x10", UINT32_MAX, false, &v) == CP_ERR_BADNUM);         /* 10진 전용 자리 */
        CHECK(parse_u32("99999999999x", UINT32_MAX, true, &v) == CP_ERR_BADNUM);  /* 모양 에러가 넘침보다 우선 */

        /* 무작위 왕복: 값 → 10진/16진 문자열 + 무작위 '_' 삽입 → 파싱 → 같은 값 */
        long bad = 0;
        for (int i = 0; i < 200000; i++) {
            uint32_t x = xorshift32();
            if (i % 4 == 0) x >>= (xorshift32() % 32);                 /* 작은 값도 골고루 */
            bool hex = (i & 1) != 0;
            char raw[16], s[48];
            snprintf(raw, sizeof raw, hex ? "%lx" : "%lu", (unsigned long)x);
            size_t k = 0;
            if (hex) { s[k++] = '0'; s[k++] = (xorshift32() & 1) ? 'x' : 'X'; }
            for (size_t j = 0; raw[j]; j++) {
                s[k++] = raw[j];
                if (raw[j + 1] && xorshift32() % 4 == 0) s[k++] = '_';
            }
            s[k] = '\0';
            uint32_t got = 0;
            if (parse_u32(s, UINT32_MAX, true, &got) != CP_OK || got != x) bad++;
        }
        CHECK(bad == 0);
        if (bad) printf("  round-trip: %ld failures\n", bad);

        /* 무작위 쓰레기 대조: 작은 알파벳의 무작위 문자열을 참조 파서와 비교 (에러 코드 + 값) */
        static const char ga[] = "0123456789abcdefxXg_-+ ";
        long diff = 0;
        for (int i = 0; i < 300000; i++) {
            char s[24];
            int len = (int)(xorshift32() % 20);
            for (int j = 0; j < len; j++) s[j] = ga[xorshift32() % (sizeof ga - 1)];
            if (len >= 2 && xorshift32() % 3 == 0) { s[0] = '0'; s[1] = 'x'; }
            s[len] = '\0';
            uint32_t max = (xorshift32() & 1) ? UINT32_MAX : (xorshift32() % 100);
            bool ah = (xorshift32() & 1) != 0;
            uint32_t a = 1, b = 1;
            cp_err_t ea = parse_u32(s, max, ah, &a), eb = ref_parse_u32(s, max, ah, &b);
            if (ea != eb || (ea == CP_OK && a != b)) diff++;
        }
        CHECK(diff == 0);
        if (diff) printf("  garbage vs reference: %ld differences\n", diff);
    }

    /* ---------- Part C: 문법 → 구조체 (표 기반) ---------- */
    {
        size_t n = sizeof k_cases / sizeof k_cases[0];
        size_t okc = 0;
        for (size_t i = 0; i < n; i++) {
            bool ok = run_case(&k_cases[i]);
            CHECK(ok);
            okc += ok;
        }
        printf("parse table: %zu/%zu cases ok\n", okc, n);

        char longl[CMD_MAX_LINE + 8];
        cmd_t c;
        memset(longl, ' ', sizeof longl - 1); longl[sizeof longl - 1] = '\0';
        memcpy(longl, "HELP", 4);
        CHECK(cmd_parse(longl, &c) == CP_ERR_TOOLONG && c.err_tok == -1);   /* 87글자: 뒤가 공백이어도 길이 제한 */

        bool distinct = true;                                             /* 에러 메시지가 모두 다르다 */
        for (int i = 0; i < CP_ERR__COUNT; i++)
            for (int j = i + 1; j < CP_ERR__COUNT; j++)
                if (strcmp(cp_strerror((cp_err_t)i), cp_strerror((cp_err_t)j)) == 0) distinct = false;
        CHECK(distinct);
    }

    /* ---------- Part C: ISR 줄 버퍼 + 태스크 ---------- */
    {
        static linebuf_t lb;     /* 0 초기화 = 빈 버퍼 */
        char reply[128];

        CHECK(!console_poll(&lb, reply, sizeof reply));                  /* 아직 줄 없음 */
        feed_str(&lb, "SET LED 3 ON\r\n");
        CHECK(console_poll(&lb, reply, sizeof reply) && strcmp(reply, "OK LED 3 ON") == 0);
        CHECK(!console_poll(&lb, reply, sizeof reply));                  /* CRLF 의 LF 가 빈 줄을 만들지 않는다 */

        feed_str(&lb, "REAX\bD 0x4000_0010 4\r");                         /* 백스페이스로 X 를 지움 */
        CHECK(console_poll(&lb, reply, sizeof reply) && strcmp(reply, "OK READ 0x40000010 4") == 0);

        feed_str(&lb, "\b\b\x7FHELP\n");                                  /* 빈 줄에서 BS 는 무시 */
        CHECK(console_poll(&lb, reply, sizeof reply) && strcmp(reply, "OK HELP") == 0);

        feed_str(&lb, "WRI\x1BTE 0x10 0x1\n");                            /* ESC(0x1B) 는 줄에 안 들어간다 */
        CHECK(console_poll(&lb, reply, sizeof reply) && strcmp(reply, "OK WRITE 0x00000010 0x00000001") == 0);
        feed(&lb, "WRI\x1BTE 0x10 0x1\x00\n", 17);                        /* 진짜 NUL 바이트도 줄에 안 들어간다 */
        CHECK(console_poll(&lb, reply, sizeof reply) && strcmp(reply, "OK WRITE 0x00000010 0x00000001") == 0);
        CHECK(lb.junk_bytes == 3);

        for (int i = 0; i < 100; i++) lb_rx_byte(&lb, 'A');               /* 넘침 → 줄 끝까지 버림 */
        CHECK(lb.len <= CMD_MAX_LINE);
        feed_str(&lb, "\r\n");
        CHECK(console_poll(&lb, reply, sizeof reply) && strcmp(reply, "ERR TOOLONG: line too long") == 0);
        feed_str(&lb, "HELP\n");                                          /* 다음 줄은 정상 */
        CHECK(console_poll(&lb, reply, sizeof reply) && strcmp(reply, "OK HELP") == 0);

        char tmp[CMD_MAX_LINE + 1];
        for (int i = 0; i < CMD_MAX_LINE + 1; i++) lb_rx_byte(&lb, 'B');  /* 81글자: 경계 바로 위 */
        lb_rx_byte(&lb, '\n');
        CHECK(lb_take(&lb, tmp, sizeof tmp) == LB_TOOLONG);
        feed_str(&lb, "HELP");                                            /* 정확히 80글자는 넘침 아님 */
        for (int i = 4; i < CMD_MAX_LINE; i++) lb_rx_byte(&lb, ' ');
        lb_rx_byte(&lb, '\n');
        CHECK(console_poll(&lb, reply, sizeof reply) && strcmp(reply, "OK HELP") == 0);
        for (int i = 0; i < CMD_MAX_LINE; i++) lb_rx_byte(&lb, '\t');
        lb_rx_byte(&lb, '\n');
        CHECK(!console_poll(&lb, reply, sizeof reply) && !atomic_load(&lb.ready)); /* 공백뿐 → 응답 없음 */

        uint32_t d0 = lb.dropped_bytes;
        feed_str(&lb, "READ 0x11 4\n");                                   /* 태스크가 늦으면 */
        feed_str(&lb, "HELP\n");                                          /* 이 줄은 버려진다 */
        CHECK(lb.dropped_bytes - d0 == 5);
        CHECK(console_poll(&lb, reply, sizeof reply) && strcmp(reply, "ERR ALIGN @1: address not 4-byte aligned") == 0);
        feed_str(&lb, "set led 9 on\n");
        CHECK(console_poll(&lb, reply, sizeof reply) && strcmp(reply, "ERR RANGE @2: number out of range") == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
