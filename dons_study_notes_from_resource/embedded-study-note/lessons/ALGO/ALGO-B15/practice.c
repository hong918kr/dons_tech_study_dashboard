/* ALGO-B15 추가 연습 — 한 번에 검증
 *   연습 1  qtok()          따옴표 토큰: "hello world", \" \\ 이스케이프를 제자리에서 풀기 (쓰기 ≤ 읽기)
 *   연습 2  cmd_lookup()    고유 접두사 축약: "wr" → WRITE, "re" → 모호함, 정확히 일치가 우선
 *   연습 3  lb_rx_echo()    에코와 줄 편집: BS → "\b \b", Ctrl-U → 줄 지우기, 넘침/빈 BS → BEL
 *   연습 4  parse_u32_libc() strtoul 로 같은 계약 만들기 + 직접 만든 파서와 차분 퍼징 (이중 접두사 함정)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 *        cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice && ./practice
 */
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng_state = 0xB15B15u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

/* ======================= 연습 1: 따옴표 토큰 ======================= */
typedef enum { QT_OK = 0, QT_TOOMANY, QT_UNTERMINATED, QT_BADQUOTE } qt_err_t;

static bool qsep(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* 따옴표 밖: 구분자로 자른다. 따옴표 안: 구분자도 글자, \" → ", \\ → \, 그 밖의 \x 는 그대로 둔다.
 * 따옴표는 토큰 전체를 감싸야 한다: ab"c" 나 "ab"c 는 BADQUOTE. 결과는 제자리에 쓴다 (w ≤ r 불변식). */
static qt_err_t qtok(char *line, char *argv[], int max, int *argc) {
    int k = 0;
    char *r = line;
    for (;;) {
        while (qsep(*r)) r++;
        if (*r == '\0') break;
        if (k == max) return QT_TOOMANY;
        if (*r == '"') {
            char *w = ++r;                 /* 따옴표 다음부터 같은 버퍼에 다시 쓴다 */
            argv[k++] = w;
            for (;;) {
                if (*r == '\0') return QT_UNTERMINATED;
                if (*r == '"') break;
                if (*r == '\\' && (r[1] == '"' || r[1] == '\\')) r++;  /* 이스케이프: 다음 글자만 남김 */
                *w++ = *r++;
            }
            r++;                           /* 닫는 따옴표 */
            if (*r != '\0' && !qsep(*r)) return QT_BADQUOTE;
            *w = '\0';                     /* w < r 이므로 닫는 따옴표 자리 이전에 NUL */
            if (*r != '\0') r++;
        } else {
            argv[k++] = r;
            while (*r != '\0' && !qsep(*r)) {
                if (*r == '"') return QT_BADQUOTE;
                r++;
            }
            if (*r != '\0') *r++ = '\0';
        }
    }
    *argc = k;
    return QT_OK;
}

/* 테스트용 인코더: 임의 토큰 → 따옴표 + 이스케이프 */
static size_t qencode(const char *tok, char *out) {
    size_t n = 0;
    out[n++] = '"';
    for (; *tok; tok++) {
        if (*tok == '"' || *tok == '\\') out[n++] = '\\';
        out[n++] = *tok;
    }
    out[n++] = '"';
    return n;
}

/* ======================= 연습 2: 고유 접두사 ======================= */
#define LOOKUP_UNKNOWN   (-1)
#define LOOKUP_AMBIGUOUS (-2)

static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

/* table 은 대문자. 정확히 일치하면 그것, 아니면 접두사로 유일하게 맞는 것, 둘 이상이면 모호함. */
static int cmd_lookup(const char *tok, const char *const table[], int n) {
    if (tok[0] == '\0') return LOOKUP_UNKNOWN;
    int found = LOOKUP_UNKNOWN;
    for (int i = 0; i < n; i++) {
        const char *a = tok, *b = table[i];
        while (*a != '\0' && *b != '\0' && up(*a) == *b) { a++; b++; }
        if (*a != '\0') continue;          /* tok 이 더 길거나 다른 글자 */
        if (*b == '\0') return i;          /* 정확히 일치: 바로 승리 */
        found = (found == LOOKUP_UNKNOWN) ? i : LOOKUP_AMBIGUOUS;
    }
    return found;
}

/* ======================= 연습 3: 에코와 줄 편집 ======================= */
#define ECHO_MAX_LINE 16                  /* 테스트하기 쉽게 작게 */

typedef struct {
    char buf[ECHO_MAX_LINE + 1];
    unsigned len;
    bool ready;
} echobuf_t;

/* 바이트 c 를 처리하고, 단말로 돌려보낼 에코 바이트를 echo 에 쓴다 (반환 = 에코 길이).
 * 실제 펌웨어에선 echo 는 TX 링 버퍼로 들어간다 — ISR 이 UART TX 를 기다리며 막히면 안 된다. */
static size_t lb_rx_echo(echobuf_t *lb, uint8_t c, char *echo) {
    size_t n = 0;
    if (lb->ready) return 0;
    if (c == '\r' || c == '\n') {
        if (c == '\n' && lb->len == 0) return 0;   /* CRLF 의 LF */
        lb->buf[lb->len] = '\0';
        lb->ready = lb->len > 0;
        echo[n++] = '\r'; echo[n++] = '\n';
        return n;
    }
    if (c == 0x08 || c == 0x7F) {
        if (lb->len == 0) { echo[n++] = '\a'; return n; }
        lb->len--;
        memcpy(echo, "\b \b", 3);                  /* 왼쪽으로, 공백으로 덮고, 다시 왼쪽 */
        return 3;
    }
    if (c == 0x15) {                               /* Ctrl-U: 줄 전체 지우기 */
        for (; lb->len > 0; lb->len--) { memcpy(echo + n, "\b \b", 3); n += 3; }
        return n;
    }
    if (c < 0x20 || c > 0x7E) return 0;
    if (lb->len == ECHO_MAX_LINE) { echo[n++] = '\a'; return n; }   /* 꽉 참: 글자 대신 벨 */
    lb->buf[lb->len++] = (char)c;
    echo[n++] = (char)c;
    return n;
}

static size_t echo_feed(echobuf_t *lb, const char *s, char *out) {
    size_t n = 0;
    for (; *s; s++) n += lb_rx_echo(lb, (uint8_t)*s, out + n);
    out[n] = '\0';
    return n;
}

/* ======================= 연습 4: strtoul 로 같은 계약 ======================= */
typedef enum { PN_OK = 0, PN_BADNUM, PN_RANGE } pn_err_t;

static int digit_of(char c, unsigned base) {
    int d = (c >= '0' && c <= '9') ? c - '0'
          : (c >= 'a' && c <= 'f') ? c - 'a' + 10
          : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
    return (d >= 0 && (unsigned)d < base) ? d : -1;
}

/* 본문과 같은 직접 구현 ('_' 지원 포함) */
static pn_err_t parse_u32(const char *s, uint32_t max, bool allow_hex, uint32_t *out) {
    unsigned base = 10;
    const char *p = s;
    if (allow_hex && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
    const uint32_t cutoff = UINT32_MAX / base, cutlim = UINT32_MAX % base;
    uint32_t acc = 0;
    bool over = false, prev_digit = false;
    int ndig = 0;
    for (; *p != '\0'; p++) {
        if (*p == '_') {
            if (!prev_digit || digit_of(p[1], base) < 0) return PN_BADNUM;
            prev_digit = false;
            continue;
        }
        int d = digit_of(*p, base);
        if (d < 0) return PN_BADNUM;
        if (acc > cutoff || (acc == cutoff && (uint32_t)d > cutlim)) over = true;
        else if (!over) acc = acc * base + (uint32_t)d;
        ndig++;
        prev_digit = true;
    }
    if (ndig == 0) return PN_BADNUM;
    if (over || acc > max) return PN_RANGE;
    *out = acc;
    return PN_OK;
}

/* strtoul 판 ('_' 없음). naive = true 면 "본문 첫 글자가 숫자인가" 검사를 빼먹은 흔한 버전. */
static pn_err_t parse_u32_libc(const char *s, uint32_t max, bool allow_hex, uint32_t *out, bool naive) {
    int base = 10;
    const char *body = s;
    if (allow_hex && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; body = s + 2; }
    /* strtoul 이 스스로 받아 주는 것들(앞 공백, +/-, base 16 에서 또 한 번의 0x)을 먼저 막는다:
     * 본문 전체가 그 base 의 숫자인지 직접 확인하면 strtoul 은 "계산 + 넘침 검출" 만 맡는다 */
    if (!naive) {
        if (body[0] == '\0') return PN_BADNUM;
        for (const char *q = body; *q; q++)
            if (digit_of(*q, (unsigned)base) < 0) return PN_BADNUM;
    }
    if (naive && (s[0] == '-' || s[0] == '+' || s[0] == ' ' || s[0] == '\0')) return PN_BADNUM;
    char *end;
    errno = 0;
    unsigned long v = strtoul(body, &end, base);
    if (end == body || *end != '\0') return PN_BADNUM;           /* 모양 먼저 */
    if (errno == ERANGE || v > max) return PN_RANGE;             /* LP64 에선 v > UINT32_MAX 도 여기서 */
    *out = (uint32_t)v;
    return PN_OK;
}

int main(void) {
    /* ---- 연습 1 ---- */
    {
        char *av[6];
        int ac = -1;
        char l1[] = "ECHO \"hello world\"  x";
        CHECK(qtok(l1, av, 6, &ac) == QT_OK && ac == 3 && strcmp(av[1], "hello world") == 0 && strcmp(av[2], "x") == 0);
        char l2[] = "ECHO \"a \\\"q\\\" b\"";                     /* ECHO "a \"q\" b" */
        CHECK(qtok(l2, av, 6, &ac) == QT_OK && ac == 2 && strcmp(av[1], "a \"q\" b") == 0);
        char l3[] = "ECHO \"\"";                                   /* 빈 토큰도 토큰이다 */
        CHECK(qtok(l3, av, 6, &ac) == QT_OK && ac == 2 && av[1][0] == '\0');
        char l4[] = "ECHO \"abc";
        CHECK(qtok(l4, av, 6, &ac) == QT_UNTERMINATED);
        char l5[] = "ECHO ab\"c\"";
        CHECK(qtok(l5, av, 6, &ac) == QT_BADQUOTE);
        char l6[] = "ECHO \"ab\"c";
        CHECK(qtok(l6, av, 6, &ac) == QT_BADQUOTE);
        char l7[] = "\"a\\\\\" \"c:\\path\"";                      /* "a\\" "c:\path" */
        CHECK(qtok(l7, av, 6, &ac) == QT_OK && ac == 2 && strcmp(av[0], "a\\") == 0 && strcmp(av[1], "c:\\path") == 0);
        char l8[] = "\"x\\\"";                                     /* "x\"  → 닫는 따옴표가 이스케이프됨 */
        CHECK(qtok(l8, av, 6, &ac) == QT_UNTERMINATED);

        /* 무작위 왕복: 토큰들 → 인코딩해 한 줄로 → qtok → 원래 토큰 */
        static const char al[] = "ab \t\"\\x";
        long bad = 0;
        for (int it = 0; it < 50000; it++) {
            char toks[4][8], line[160];
            int nt = 1 + (int)(xorshift32() % 4);
            size_t n = 0;
            for (int i = 0; i < nt; i++) {
                int tl = (int)(xorshift32() % 7);
                for (int j = 0; j < tl; j++) toks[i][j] = al[xorshift32() % (sizeof al - 1)];
                toks[i][tl] = '\0';
                n += qencode(toks[i], line + n);
                line[n++] = (xorshift32() & 1) ? ' ' : '\t';
            }
            line[n] = '\0';
            bool same = qtok(line, av, 6, &ac) == QT_OK && ac == nt;
            for (int i = 0; same && i < nt; i++) same = strcmp(av[i], toks[i]) == 0;
            if (!same) bad++;
        }
        CHECK(bad == 0);
    }

    /* ---- 연습 2 ---- */
    {
        static const char *const tbl[] = {"READ", "RESET", "REBOOT", "WRITE", "WHO", "SET", "SETUP", "HELP"};
        const int n = (int)(sizeof tbl / sizeof tbl[0]);
        CHECK(cmd_lookup("rea", tbl, n) == 0);
        CHECK(cmd_lookup("re", tbl, n) == LOOKUP_AMBIGUOUS);
        CHECK(cmd_lookup("res", tbl, n) == 1 && cmd_lookup("reb", tbl, n) == 2);
        CHECK(cmd_lookup("w", tbl, n) == LOOKUP_AMBIGUOUS && cmd_lookup("wr", tbl, n) == 3);
        CHECK(cmd_lookup("set", tbl, n) == 5);                      /* SETUP 의 접두사지만 정확히 일치가 우선 */
        CHECK(cmd_lookup("SETU", tbl, n) == 6);
        CHECK(cmd_lookup("readx", tbl, n) == LOOKUP_UNKNOWN && cmd_lookup("x", tbl, n) == LOOKUP_UNKNOWN);
        CHECK(cmd_lookup("", tbl, n) == LOOKUP_UNKNOWN);
        /* 전수: 모든 이름의 모든 접두사를 느린 기준(접두사 일치 개수 세기)과 대조 */
        long bad = 0;
        for (int i = 0; i < n; i++) {
            for (size_t L = 1; L <= strlen(tbl[i]); L++) {
                char p[16];
                memcpy(p, tbl[i], L); p[L] = '\0';
                int exact = -1, cnt = 0, last = -1;
                for (int j = 0; j < n; j++) {
                    if (strncmp(tbl[j], p, L) == 0) { cnt++; last = j; }
                    if (strcmp(tbl[j], p) == 0) exact = j;
                }
                int want = exact >= 0 ? exact : cnt == 1 ? last : cnt == 0 ? LOOKUP_UNKNOWN : LOOKUP_AMBIGUOUS;
                if (cmd_lookup(p, tbl, n) != want) bad++;
            }
        }
        CHECK(bad == 0);
    }

    /* ---- 연습 3 ---- */
    {
        echobuf_t lb = {0};
        char out[256];
        echo_feed(&lb, "HEX\bLP", out);
        CHECK(strcmp(out, "HEX\b \bLP") == 0 && lb.len == 4 && memcmp(lb.buf, "HELP", 4) == 0);
        echo_feed(&lb, "\r\n", out);
        CHECK(strcmp(out, "\r\n") == 0 && lb.ready && strcmp(lb.buf, "HELP") == 0);
        lb = (echobuf_t){0};
        echo_feed(&lb, "\b", out);
        CHECK(strcmp(out, "\a") == 0 && lb.len == 0);                  /* 빈 줄에서 BS → 벨 */
        echo_feed(&lb, "abc\x15", out);
        CHECK(strcmp(out, "abc\b \b\b \b\b \b") == 0 && lb.len == 0);  /* Ctrl-U */
        echo_feed(&lb, "0123456789ABCDEFG", out);                      /* 17번째 글자: 벨 */
        CHECK(lb.len == ECHO_MAX_LINE && strcmp(out, "0123456789ABCDEF\a") == 0);
        echo_feed(&lb, "\r", out);
        CHECK(lb.ready && strlen(lb.buf) == ECHO_MAX_LINE);
        lb = (echobuf_t){0};
        echo_feed(&lb, "\r\n\n", out);                                  /* 빈 줄: 줄바꿈만 에코, ready 아님 */
        CHECK(!lb.ready && strcmp(out, "\r\n") == 0);
    }

    /* ---- 연습 4 ---- */
    {
        uint32_t v = 0;
        CHECK(parse_u32_libc("0x40000010", UINT32_MAX, true, &v, false) == PN_OK && v == 0x40000010u);
        CHECK(parse_u32_libc("4294967296", UINT32_MAX, true, &v, false) == PN_RANGE);   /* LP64: ERANGE 아님, v > max 로 */
        CHECK(parse_u32_libc("-1", UINT32_MAX, true, &v, false) == PN_BADNUM);
        CHECK(parse_u32_libc("0x-1", UINT32_MAX, true, &v, false) == PN_BADNUM);
        CHECK(parse_u32_libc("0x0x5", UINT32_MAX, true, &v, false) == PN_BADNUM);
        CHECK(parse_u32_libc("0x0x5", UINT32_MAX, true, &v, true) == PN_OK && v == 5);  /* naive: 이중 접두사 통과! */
        CHECK(parse_u32_libc("0x 5", UINT32_MAX, true, &v, true) == PN_OK);             /* naive: 공백도 통과 */

        /* 차분 퍼징: '_' 없는 무작위 문자열에서 직접 구현 == strtoul 판 */
        static const char ga[] = "0123456789abcdefxX-+ ";
        long diff = 0, naive_diff = 0;
        char first_naive[32] = "";
        for (int it = 0; it < 300000; it++) {
            char s[24];
            int len = (int)(xorshift32() % 14);
            for (int j = 0; j < len; j++) s[j] = ga[xorshift32() % (sizeof ga - 1)];
            if (len >= 2 && xorshift32() % 2 == 0) { s[0] = '0'; s[1] = 'x'; }
            s[len] = '\0';
            uint32_t max = (xorshift32() & 1) ? UINT32_MAX : xorshift32() % 1000;
            bool ah = (xorshift32() % 4) != 0;
            uint32_t a = 0, b = 0, c = 0;
            pn_err_t ea = parse_u32(s, max, ah, &a);
            pn_err_t eb = parse_u32_libc(s, max, ah, &b, false);
            pn_err_t ec = parse_u32_libc(s, max, ah, &c, true);
            if (ea != eb || (ea == PN_OK && a != b)) diff++;
            if (ea != ec || (ea == PN_OK && a != c)) {
                if (naive_diff++ == 0) snprintf(first_naive, sizeof first_naive, "%s", s);
            }
        }
        CHECK(diff == 0);
        CHECK(naive_diff > 0);                       /* 퍼저가 naive 판의 구멍을 찾아낸다 */
        printf("differential fuzz: careful=%ld diffs, naive=%ld diffs (first: \"%s\")\n", diff, naive_diff, first_naive);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
