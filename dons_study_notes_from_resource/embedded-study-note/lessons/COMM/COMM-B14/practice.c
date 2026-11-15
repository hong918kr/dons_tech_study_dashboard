/* COMM-B14 추가 연습 — 한 번에 검증
 *
 * 연습 1  터미널 키: ANSI 이스케이프(화살표 ESC [ A, Delete ESC [ 3 ~, F1 ESC O P) 삼키기 + Ctrl-U / Ctrl-W / Ctrl-C
 * 연습 2  parse_u32: 레지스터 값용 (부호 없음, 0x / 0b, 0xFFFFFFFF 까지)
 * 연습 3  따옴표 인자 토크나이저: name "hello world", \" 와 \\ 이스케이프, 닫히지 않은 따옴표 = 에러
 * 연습 4  AT 명령 이어 쓰기: AT+LED=1;+LED? — 응답은 OK 하나, 첫 에러에서 멈춤
 * 연습 5  고정소수점 인자: "23.5" → 23500 (milli), 소수 셋째 자리까지, int32 범위
 * 모두 self-contained (본문 solution.c 의 필요한 부분만 축약해서 넣었다). malloc 없음.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t g_rng = 0xB14C0DEu;
static uint32_t xorshift32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

typedef enum { NUM_OK = 0, NUM_E_SYNTAX, NUM_E_RANGE } num_status_t;

/* ======================= 연습 1: 이스케이프 시퀀스와 편집 키 ======================= */
#define L1_MAX 40
typedef enum { L_NONE = 0, L_LINE, L_CANCEL } l_event_t;
typedef enum { ESC_NONE = 0, ESC_START, ESC_CSI, ESC_SS3 } esc_state_t;
typedef struct { char buf[L1_MAX + 1]; uint8_t len; esc_state_t esc; bool last_cr, ready; uint32_t n_seq; } le1_t;

static l_event_t le1_feed(le1_t *le, uint8_t c) {
    if (le->ready) { le->len = 0; le->buf[0] = '\0'; le->ready = false; }
    bool after_cr = le->last_cr;
    le->last_cr = (c == '\r');
    if (c == '\n' && after_cr) return L_NONE;
    if (c == '\r' || c == '\n') { le->esc = ESC_NONE; le->ready = true; return L_LINE; }   /* 줄 끝은 시퀀스도 끊는다 */
    switch (le->esc) {
    case ESC_START: le->esc = (c == '[') ? ESC_CSI : (c == 'O') ? ESC_SS3 : ESC_NONE; return L_NONE;
    case ESC_CSI:   if (c >= 0x40 && c <= 0x7E) le->esc = ESC_NONE; return L_NONE;  /* 파라미터 0x30-3F, 중간 0x20-2F */
    case ESC_SS3:   le->esc = ESC_NONE; return L_NONE;                              /* ESC O P = F1: 1바이트 더 */
    case ESC_NONE:  break;
    }
    if (c == 0x1B) { le->esc = ESC_START; le->n_seq++; return L_NONE; }
    if (c == 0x03) { le->len = 0; le->buf[0] = '\0'; le->ready = true; return L_CANCEL; }   /* Ctrl-C */
    if (c == 0x15) { le->len = 0; le->buf[0] = '\0'; return L_NONE; }                    /* Ctrl-U: 줄 지우기 */
    if (c == 0x17) {                                                                       /* Ctrl-W: 단어 지우기 */
        while (le->len && le->buf[le->len - 1] == ' ') le->len--;
        while (le->len && le->buf[le->len - 1] != ' ') le->len--;
        le->buf[le->len] = '\0';
        return L_NONE;
    }
    if (c == 0x08 || c == 0x7F) { if (le->len) le->buf[--le->len] = '\0'; return L_NONE; }
    if (c < 0x20 || c > 0x7E) return L_NONE;
    if (le->len < L1_MAX) { le->buf[le->len++] = (char)c; le->buf[le->len] = '\0'; }
    return L_NONE;
}
/* 비교용: 제어 문자(< 0x20)만 버리는 순진한 필터 — ESC 는 버리지만 뒤따르는 "[A" 는 인쇄 가능 문자라 남는다 */
static void naive_filter(const char *in, size_t n, char *out) {
    size_t k = 0;
    for (size_t i = 0; i < n; i++) { uint8_t c = (uint8_t)in[i]; if (c >= 0x20 && c <= 0x7E) out[k++] = (char)c; }
    out[k] = '\0';
}
static char g_got[256];
static const char *le1_trace(le1_t *le, const char *s, size_t n) {
    size_t g = 0;
    for (size_t i = 0; i < n; i++) {
        l_event_t ev = le1_feed(le, (uint8_t)s[i]);
        const char *add = ev == L_LINE ? le->buf : ev == L_CANCEL ? "^C" : NULL;
        if (add) { size_t k = strlen(add); memcpy(g_got + g, add, k); g += k; g_got[g++] = '|'; }
    }
    g_got[g] = '\0';
    return g_got;
}
#define T1(lit) le1_trace(&l1, lit, sizeof(lit) - 1)

/* ======================= 연습 2: parse_u32 ======================= */
static int digit_val(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return 99;
}
static num_status_t parse_u32(const char *s, uint32_t *out) {
    uint32_t base = 10, acc = 0;
    bool big = false;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
    else if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) { base = 2; s += 2; }
    if (*s == '\0') return NUM_E_SYNTAX;
    for (; *s; s++) {
        uint32_t d = (uint32_t)digit_val(*s);
        if (d >= base) return NUM_E_SYNTAX;                    /* 부호 '-' 도 여기서 SYNTAX */
        if (!big && acc > (UINT32_MAX - d) / base) big = true;
        if (!big) acc = acc * base + d;
    }
    if (big) return NUM_E_RANGE;
    *out = acc;
    return NUM_OK;
}

/* ======================= 연습 3: 따옴표 토크나이저 ======================= */
/* 반환: 토큰 수, -1 = 토큰 너무 많음, -2 = 닫히지 않은 따옴표. 읽기 r / 쓰기 w 두 포인터로 제자리 언이스케이프 (w <= r) */
static int tokenize_q(char *s, char *argv[], int max_args) {
    int argc = 0;
    char *r = s, *w = s;
    for (;;) {
        while (*r == ' ') r++;
        if (*r == '\0') return argc;
        if (argc == max_args) return -1;
        argv[argc++] = w;
        bool q = false;
        while (*r != '\0' && (q || *r != ' ')) {
            if (*r == '"') { q = !q; r++; continue; }
            if (q && *r == '\\' && (r[1] == '"' || r[1] == '\\')) { *w++ = r[1]; r += 2; continue; }
            *w++ = *r++;
        }
        if (q) return -2;
        if (*r != '\0') r++;
        *w++ = '\0';
    }
}

/* ======================= 연습 4: AT 이어 쓰기 ======================= */
static struct { uint8_t led; uint32_t baud; } dev4;
static void o_puts(char *o, size_t cap, const char *s) { size_t n = strlen(o); while (*s && n + 1 < cap) o[n++] = *s++; o[n] = '\0'; }
/* 확장 명령 하나 ("+LED=1", "+LED?", "+BAUD?") — 0 = OK, -1 = ERROR */
static int at4_one(const char *c, char *o, size_t cap) {
    if (strcmp(c, "+LED=0") == 0 || strcmp(c, "+LED=1") == 0) { dev4.led = (uint8_t)(c[5] - '0'); return 0; }
    if (strcmp(c, "+LED?") == 0) { o_puts(o, cap, dev4.led ? "\r\n+LED: 1\r\n" : "\r\n+LED: 0\r\n"); return 0; }
    if (strcmp(c, "+BAUD?") == 0) { char t[32]; snprintf(t, sizeof t, "\r\n+BAUD: %u\r\n", (unsigned)dev4.baud); o_puts(o, cap, t); return 0; }
    return -1;
}
/* "AT" 뒤를 ';' 로 나눠 차례로 실행. 첫 에러에서 멈추고 ERROR. 앞서 실행된 명령은 되돌리지 않는다. */
static int at_multi(const char *line, char *o, size_t cap) {
    char work[64], *seg;
    o[0] = '\0';
    size_t n = strlen(line);
    if (n < 2 || n >= sizeof work || !((line[0] == 'A' || line[0] == 'a') && (line[1] == 'T' || line[1] == 't'))) {
        o_puts(o, cap, "\r\nERROR\r\n"); return -1;
    }
    memcpy(work, line + 2, n - 1);                             /* NUL 포함 */
    seg = work;
    if (*seg != '\0') {
        for (;;) {
            char *semi = strchr(seg, ';');
            if (semi) *semi = '\0';
            if (at4_one(seg, o, cap) != 0) { o_puts(o, cap, "\r\nERROR\r\n"); return -1; }
            if (!semi) break;
            seg = semi + 1;
        }
    }
    o_puts(o, cap, "\r\nOK\r\n");
    return 0;
}

/* ======================= 연습 5: 고정소수점 milli ======================= */
static num_status_t parse_milli(const char *s, int32_t *out) {
    bool neg = false;
    if (*s == '+' || *s == '-') { neg = (*s == '-'); s++; }
    uint64_t ip = 0; int nd = 0;
    for (; *s >= '0' && *s <= '9'; s++, nd++) if (ip < 10000000u) ip = ip * 10u + (uint64_t)(*s - '0');   /* 포화: 넘침 없음 */
    if (nd == 0) return NUM_E_SYNTAX;                          /* ".5", "-", "" */
    uint32_t frac = 0; int nf = 0;
    if (*s == '.') {
        for (s++; *s >= '0' && *s <= '9'; s++) { if (nf == 3) return NUM_E_SYNTAX; frac = frac * 10u + (uint32_t)(*s - '0'); nf++; }
        if (nf == 0) return NUM_E_SYNTAX;                      /* "5." */
    }
    if (*s != '\0') return NUM_E_SYNTAX;                       /* "1.2.3", "1e3" */
    for (; nf < 3; nf++) frac *= 10u;
    uint64_t m = ip * 1000u + frac;
    if (m > (neg ? 2147483648u : 2147483647u)) return NUM_E_RANGE;
    *out = neg ? (m == 2147483648u ? INT32_MIN : -(int32_t)m) : (int32_t)m;
    return NUM_OK;
}

int main(void) {
    /* ---- 연습 1 ---- */
    le1_t l1; memset(&l1, 0, sizeof l1);
    {
        char nv[64]; static const char arrow[] = "led\x1b[A 1";
        naive_filter(arrow, sizeof arrow - 1, nv);
        CHECK(strcmp(nv, "led[A 1") == 0);                                 /* 순진한 필터의 결과 */
    }
    CHECK(strcmp(T1("led\x1b[A 1\r"), "led 1|") == 0 && l1.n_seq == 1);    /* 화살표 위 */
    CHECK(strcmp(T1("se\x1b[3~t 2\r"), "set 2|") == 0);                     /* Delete 키 = ESC [ 3 ~ */
    CHECK(strcmp(T1("a\x1bOPb\r"), "ab|") == 0);                            /* F1 = ESC O P (SS3) */
    CHECK(strcmp(T1("x\x1b[1;5C" "y\r"), "xy|") == 0);                      /* Ctrl+→ = ESC [ 1 ; 5 C */
    CHECK(strcmp(T1("q\x1b" "z\r"), "q|") == 0);                            /* ESC + 문자 2바이트 (Alt-z) */
    CHECK(strcmp(T1("led\x1b[\rok\r"), "led|ok|") == 0);                    /* 끊긴 시퀀스: CR 이 끊는다 */
    CHECK(strcmp(T1("garbage\x15led 1\r"), "led 1|") == 0);                 /* Ctrl-U */
    CHECK(strcmp(T1("set 3 99\x17" "42\r"), "set 3 42|") == 0);             /* Ctrl-W */
    CHECK(strcmp(T1("set 3   \x17\x17" "get\r"), "get|") == 0);
    CHECK(strcmp(T1("reboot\x03\r\n"), "^C||") == 0);                       /* Ctrl-C: 취소, 이어지는 CRLF 는 빈 줄 */

    /* ---- 연습 2 ---- */
    {
        static const struct { const char *s; num_status_t st; uint32_t v; } T[] = {
            { "0", NUM_OK, 0 }, { "4294967295", NUM_OK, 0xFFFFFFFFu }, { "0xFFFFFFFF", NUM_OK, 0xFFFFFFFFu },
            { "0xdeadBEEF", NUM_OK, 0xDEADBEEFu }, { "0b1010", NUM_OK, 10 }, { "0B11111111", NUM_OK, 255 },
            { "4294967296", NUM_E_RANGE, 0 }, { "0x100000000", NUM_E_RANGE, 0 },
            { "0b100000000000000000000000000000000", NUM_E_RANGE, 0 },
            { "-1", NUM_E_SYNTAX, 0 }, { "+1", NUM_E_SYNTAX, 0 }, { "0b102", NUM_E_SYNTAX, 0 }, { "0b", NUM_E_SYNTAX, 0 },
            { "0x", NUM_E_SYNTAX, 0 }, { "", NUM_E_SYNTAX, 0 }, { "12abc", NUM_E_SYNTAX, 0 },
        };
        int bad = 0;
        for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
            uint32_t v = 777; num_status_t st = parse_u32(T[i].s, &v);
            if (st != T[i].st || v != (st == NUM_OK ? T[i].v : 777u)) { bad++; printf("  parse_u32(\"%s\") st=%d v=%u\n", T[i].s, (int)st, (unsigned)v); }
        }
        CHECK(bad == 0);
        char s[48]; bad = 0;
        for (int i = 0; i < 20000; i++) {
            uint32_t x = xorshift32(), got = 0;
            snprintf(s, sizeof s, (i & 1) ? "%u" : "0x%X", (unsigned)x);
            if (parse_u32(s, &got) != NUM_OK || got != x) bad++;
            int k = 0; s[k++] = '0'; s[k++] = 'b';                   /* 2진 표기도 */
            for (int b = 31; b >= 0; b--) s[k++] = (char)('0' + ((x >> b) & 1u));
            s[k] = '\0';
            if (parse_u32(s, &got) != NUM_OK || got != x) bad++;
        }
        CHECK(bad == 0);
    }

    /* ---- 연습 3 ---- */
    {
        char *av[8];
        char a[] = "name \"hello world\"", b[] = "say \"a \\\"b\\\" c\" end", c[] = "x \"\" y", d[] = "bad \"open";
        char e[] = "path \"C:\\\\tmp\"", f[] = "ab\"c d\"e", g[] = "1 2 3 4 5 6 7 8 9";
        CHECK(tokenize_q(a, av, 8) == 2 && strcmp(av[1], "hello world") == 0);
        CHECK(tokenize_q(b, av, 8) == 3 && strcmp(av[1], "a \"b\" c") == 0 && strcmp(av[2], "end") == 0);
        CHECK(tokenize_q(c, av, 8) == 3 && av[1][0] == '\0' && strcmp(av[2], "y") == 0);   /* 빈 인자 */
        CHECK(tokenize_q(d, av, 8) == -2);
        CHECK(tokenize_q(e, av, 8) == 2 && strcmp(av[1], "C:\\tmp") == 0);
        CHECK(tokenize_q(f, av, 8) == 1 && strcmp(av[0], "abc de") == 0);
        CHECK(tokenize_q(g, av, 8) == -1);
        /* 따옴표 없는 랜덤 줄은 본문 tokenize 와 결과가 같아야 한다 (w == r 이라 그대로) */
        int bad = 0;
        for (int i = 0; i < 5000; i++) {
            char s1[48], s2[48]; size_t n = xorshift32() % 40;
            for (size_t j = 0; j < n; j++) s1[j] = (xorshift32() % 3 == 0) ? ' ' : (char)('a' + xorshift32() % 26);
            s1[n] = '\0'; memcpy(s2, s1, n + 1);
            char *x[8], *y[8]; int nx = tokenize_q(s1, x, 8), ny = 0;
            { char *p = s2; for (;;) { while (*p == ' ') p++; if (!*p) break; if (ny == 8) { ny = -1; break; }
                                       y[ny++] = p; while (*p && *p != ' ') p++; if (*p) *p++ = '\0'; } }
            if (nx != ny) bad++;
            else for (int k = 0; k < nx; k++) if (strcmp(x[k], y[k]) != 0) bad++;
        }
        CHECK(bad == 0);
    }

    /* ---- 연습 4 ---- */
    {
        char o[128];
        dev4.led = 0; dev4.baud = 115200;
        CHECK(at_multi("AT+LED=1;+LED?", o, sizeof o) == 0 && strcmp(o, "\r\n+LED: 1\r\n\r\nOK\r\n") == 0);
        CHECK(at_multi("AT+LED?;+BAUD?", o, sizeof o) == 0 && strcmp(o, "\r\n+LED: 1\r\n\r\n+BAUD: 115200\r\n\r\nOK\r\n") == 0);
        CHECK(at_multi("AT+LED=2;+LED=0", o, sizeof o) == -1 && dev4.led == 1 && strcmp(o, "\r\nERROR\r\n") == 0);  /* 멈춤 */
        CHECK(at_multi("AT+LED=0;+FOO", o, sizeof o) == -1 && dev4.led == 0);   /* 앞의 것은 이미 실행됨 */
        CHECK(at_multi("AT", o, sizeof o) == 0 && strcmp(o, "\r\nOK\r\n") == 0);
        CHECK(at_multi("AT+LED=1;", o, sizeof o) == -1);                       /* 빈 명령 */
        CHECK(at_multi("AT;;", o, sizeof o) == -1 && at_multi("A", o, sizeof o) == -1);
    }

    /* ---- 연습 5 ---- */
    {
        static const struct { const char *s; num_status_t st; int32_t v; } T[] = {
            { "23.5", NUM_OK, 23500 }, { "-0.001", NUM_OK, -1 }, { "0.1", NUM_OK, 100 }, { "7", NUM_OK, 7000 },
            { "+1.25", NUM_OK, 1250 }, { "-0.5", NUM_OK, -500 }, { "2147483.647", NUM_OK, INT32_MAX },
            { "-2147483.648", NUM_OK, INT32_MIN }, { "000012.340", NUM_OK, 12340 },
            { "2147483.648", NUM_E_RANGE, 0 }, { "99999999999.0", NUM_E_RANGE, 0 }, { "-2147483.649", NUM_E_RANGE, 0 },
            { "1.2345", NUM_E_SYNTAX, 0 }, { ".", NUM_E_SYNTAX, 0 }, { "5.", NUM_E_SYNTAX, 0 }, { ".5", NUM_E_SYNTAX, 0 },
            { "1.2.3", NUM_E_SYNTAX, 0 }, { "1e3", NUM_E_SYNTAX, 0 }, { "", NUM_E_SYNTAX, 0 }, { "-", NUM_E_SYNTAX, 0 },
            { "12abc", NUM_E_SYNTAX, 0 }, { "1,5", NUM_E_SYNTAX, 0 },
        };
        int bad = 0;
        for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
            int32_t v = 4242; num_status_t st = parse_milli(T[i].s, &v);
            if (st != T[i].st || v != (st == NUM_OK ? T[i].v : 4242)) { bad++; printf("  parse_milli(\"%s\") st=%d v=%d\n", T[i].s, (int)st, (int)v); }
        }
        CHECK(bad == 0);
        char s[48]; bad = 0;
        for (int i = 0; i < 20000; i++) {
            int32_t m = (int32_t)xorshift32(), got = 0;
            int64_t a = m < 0 ? -(int64_t)m : m;
            snprintf(s, sizeof s, "%s%lld.%03lld", m < 0 ? "-" : "", (long long)(a / 1000), (long long)(a % 1000));
            if (parse_milli(s, &got) != NUM_OK || got != m) bad++;
        }
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
