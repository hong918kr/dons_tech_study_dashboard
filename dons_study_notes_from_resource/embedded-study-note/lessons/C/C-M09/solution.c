/* C-M09 strtol / atoi 구현: 오버플로와 에러 처리
 *
 * my_strtol(s, &end, base) 를 C11 7.22.1.4 대로 구현하고 libc strtol 과 대조한다.
 * 비교 항목은 세 가지: 반환값, end 오프셋(end - s), errno.
 *   - 표 기반 케이스 (경계·함정 하나하나)
 *   - 결정적(고정 시드 xorshift32) 생성 문자열 수천 개
 * 주의: "변환 없음" 일 때 errno = EINVAL 은 POSIX 의 'may' 항목이다. macOS(FreeBSD libc)는 설정하고
 *       glibc 는 설정하지 않는다. 이 하네스는 macOS libc 를 기준으로 한다.
 *   cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o sol_san && ./sol_san
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- Part A: 숫자 한 글자 → 값 ---------------- */
/* '0'-'9' → 0-9, 'a'-'z' / 'A'-'Z' → 10-35, 그 밖 → 36 (어떤 base 보다도 크거나 같다 = 숫자 아님) */
static int digit_val(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 36;
}

/* ---------------- Part B: base 결정과 0x 접두사 ---------------- */
/* p = 부호 다음 위치. *base 가 0 이면 16/8/10 으로 정하고, 건너뛸 접두사 뒤 위치를 돌려준다.
 * 함정: "0x" 뒤에 16진 숫자가 없으면 접두사가 아니다 → 숫자는 "0" 하나, end 는 'x' 를 가리킨다. */
static const char *resolve_base(const char *p, int *base) {
    bool hex_prefix = p[0] == '0' && (p[1] == 'x' || p[1] == 'X')
                      && digit_val((unsigned char)p[2]) < 16;
    if (*base == 0) *base = hex_prefix ? 16 : (p[0] == '0' ? 8 : 10);
    if (*base == 16 && hex_prefix) return p + 2;
    return p;
}

/* ---------------- Part A + B: my_strtol ---------------- */
/* 공백 → 부호 → 접두사 → 숫자들. 넘치면 LONG_MIN/LONG_MAX 로 포화하고 errno = ERANGE.
 * 숫자가 하나도 없으면 0, *end = s, errno = EINVAL. 성공하면 errno 를 건드리지 않는다.
 * long 이 64비트(맥 arm64)든 32비트(Cortex-M, ILP32)든 같은 코드 — 한계는 LONG_MAX 로만 표현. */
long my_strtol(const char *s, char **end, int base) {
    const char *p = s;
    if (base < 0 || base == 1 || base > 36) {         /* 지원 안 하는 base: POSIX EINVAL */
        if (end) *end = (char *)s;
        errno = EINVAL;
        return 0;
    }
    while (isspace((unsigned char)*p)) p++;
    bool neg = false;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }
    p = resolve_base(p, &base);

    /* 음수 쪽 한계 |LONG_MIN| = LONG_MAX + 1 은 long 으로는 표현 못 한다 → unsigned long 으로 */
    unsigned long limit  = neg ? (unsigned long)LONG_MAX + 1u : (unsigned long)LONG_MAX;
    unsigned long cutoff = limit / (unsigned)base;     /* acc 가 이보다 크면 acc*base 부터 넘친다 */
    unsigned      cutlim = (unsigned)(limit % (unsigned)base);  /* acc == cutoff 일 때 허용되는 마지막 숫자 */
    unsigned long acc = 0;
    bool any = false, overflow = false;
    int d;
    for (; (d = digit_val((unsigned char)*p)) < base; p++) {
        any = true;
        if (overflow) continue;                        /* 넘쳐도 숫자는 끝까지 소비한다 (end 위치) */
        if (acc > cutoff || (acc == cutoff && (unsigned)d > cutlim)) overflow = true;
        else acc = acc * (unsigned)base + (unsigned)d; /* 여기선 절대 limit 을 넘지 않는다 */
    }
    if (!any) {                                        /* 변환 없음: 공백·부호·"0x" 까지 전부 되돌림 */
        if (end) *end = (char *)s;
        errno = EINVAL;
        return 0;
    }
    if (end) *end = (char *)p;
    if (overflow) { errno = ERANGE; return neg ? LONG_MIN : LONG_MAX; }
    if (!neg) return (long)acc;                        /* acc <= LONG_MAX: 값 보존 변환 */
    return acc == limit ? LONG_MIN : -(long)acc;       /* -(LONG_MAX+1) 은 직접 만들 수 없다 */
}

/* ---------------- Part B: atoi 와 "제대로 된" 정수 파서 ---------------- */
/* libc atoi 는 결과가 int 로 표현 안 되면 UB (C11 7.22.1p1). 우리 것은 포화로 정의해 둔다. */
static int my_atoi(const char *s) {
    long v = my_strtol(s, NULL, 10);
    if (v > INT_MAX) return INT_MAX;
    if (v < INT_MIN) return INT_MIN;
    return (int)v;
}

typedef enum { PI_OK = 0, PI_NODIGITS, PI_JUNK, PI_RANGE } pi_err;
/* 문자열 전체가 10진 int 하나여야 한다 (앞뒤 공백만 허용). 실패하면 *out 을 건드리지 않는다. */
static pi_err parse_int_strict(const char *s, int *out) {
    char *end;
    errno = 0;                                         /* strtol 은 성공 시 errno 를 0 으로 만들지 않는다 */
    long v = my_strtol(s, &end, 10);
    if (end == s) return PI_NODIGITS;
    while (isspace((unsigned char)*end)) end++;
    if (*end != '\0') return PI_JUNK;
    if (errno == ERANGE || v < INT_MIN || v > INT_MAX) return PI_RANGE;
    *out = (int)v;
    return PI_OK;
}

/* ---------------- Part C: UART 명령 줄 "SET 0x1F -42" ---------------- */
#define NREGS 32
static int32_t g_regs[NREGS];                          /* 가짜 레지스터 파일 */

typedef enum { OP_SET, OP_GET, OP_RST } cmd_op;
typedef enum { CLI_OK = 0, CLI_EMPTY, CLI_UNKNOWN, CLI_ARGC, CLI_NOTNUM, CLI_RANGE } cli_err;
typedef struct { cmd_op op; uint8_t reg; int32_t val; } cmd_t;

/* (주어진 코드) 공백 ' ' '\t' '\r' '\n' 으로 구분된 다음 토큰. 없으면 NULL. 토큰은 NUL 종료가 아니다. */
static bool is_sep(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static const char *next_tok(const char **pp, size_t *len) {
    const char *p = *pp;
    while (is_sep(*p)) p++;
    if (*p == '\0') { *pp = p; return NULL; }
    const char *t = p;
    while (*p != '\0' && !is_sep(*p)) p++;
    *len = (size_t)(p - t);
    *pp = p;
    return t;
}

/* 토큰 [t, t+n) 전체가 base 0 정수 하나이고 [lo, hi] 안이어야 한다. */
static cli_err tok_num(const char *t, size_t n, long lo, long hi, long *out) {
    char *end;
    errno = 0;
    long v = my_strtol(t, &end, 0);
    if (end != t + n) return CLI_NOTNUM;               /* 숫자 없음("-"), 찌꺼기("12abc"), "0x", "08" */
    if (errno == ERANGE || v < lo || v > hi) return CLI_RANGE;
    *out = v;
    return CLI_OK;
}

/* "SET <reg> <val>" / "GET <reg>" / "RST". reg: 0..NREGS-1, val: int32 범위. 명령어는 대문자만. */
static cli_err cli_parse(const char *line, cmd_t *cmd) {
    const char *p = line, *tok[4];
    size_t len[4];
    int argc = 0;
    while (argc < 4 && (tok[argc] = next_tok(&p, &len[argc])) != NULL) argc++;
    if (argc == 0) return CLI_EMPTY;

    int need;
    if      (len[0] == 3 && memcmp(tok[0], "SET", 3) == 0) { cmd->op = OP_SET; need = 3; }
    else if (len[0] == 3 && memcmp(tok[0], "GET", 3) == 0) { cmd->op = OP_GET; need = 2; }
    else if (len[0] == 3 && memcmp(tok[0], "RST", 3) == 0) { cmd->op = OP_RST; need = 1; }
    else return CLI_UNKNOWN;
    if (argc != need) return CLI_ARGC;                 /* 4개째 토큰이 있으면 argc == 4 → 너무 많음 */

    long v;
    cli_err e;
    if (need >= 2) {
        if ((e = tok_num(tok[1], len[1], 0, NREGS - 1, &v)) != CLI_OK) return e;
        cmd->reg = (uint8_t)v;
    }
    if (need == 3) {
        if ((e = tok_num(tok[2], len[2], INT32_MIN, INT32_MAX, &v)) != CLI_OK) return e;
        cmd->val = (int32_t)v;
    }
    return CLI_OK;
}

/* (주어진 코드) 한 줄 실행 → 응답 문자열 ("OK", "R3=-42", "ERR NOTNUM") */
static cli_err cli_exec(const char *line, char *resp, size_t cap) {
    static const char *const names[] = { "OK", "EMPTY", "UNKNOWN", "ARGC", "NOTNUM", "RANGE" };
    cmd_t c = { OP_RST, 0, 0 };
    cli_err e = cli_parse(line, &c);
    if (e != CLI_OK)           snprintf(resp, cap, "ERR %s", names[e]);
    else if (c.op == OP_SET) { g_regs[c.reg] = c.val; snprintf(resp, cap, "OK"); }
    else if (c.op == OP_GET)   snprintf(resp, cap, "R%u=%ld", (unsigned)c.reg, (long)g_regs[c.reg]);
    else                     { memset(g_regs, 0, sizeof g_regs); snprintf(resp, cap, "OK"); }
    return e;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

typedef struct { long v; ptrdiff_t off; int err; } res_t;
static res_t run_lib(const char *s, int base) {
    char *e; errno = 0;
    long v = strtol(s, &e, base);
    return (res_t){ v, e - s, errno };
}
static res_t run_my(const char *s, int base) {
    char *e = NULL; errno = 0;
    long v = my_strtol(s, &e, base);
    return (res_t){ v, e ? e - s : -999, errno };
}
static int g_shown;                                    /* 불일치 상세는 처음 몇 개만 출력 */
static const char *esc(const char *s) {                /* 공백 문자를 \t \n 처럼 보이게 */
    static const char ws[] = "\t\n\v\f\r", nm[] = "tnvfr";
    static char buf[160]; size_t n = 0;
    for (; *s && n + 3 < sizeof buf; s++) {
        const char *m = strchr(ws, *s);
        if (m) { buf[n++] = '\\'; buf[n++] = nm[m - ws]; }
        else buf[n++] = *s;
    }
    buf[n] = '\0';
    return buf;
}
static bool same(const char *s, int base) {
    res_t a = run_lib(s, base), b = run_my(s, base);
    bool ok = a.v == b.v && a.off == b.off && a.err == b.err;
    if (!ok && g_shown++ < 8)
        printf("  mismatch base=%d \"%s\": libc v=%ld end=%td errno=%d | my v=%ld end=%td errno=%d\n",
               base, esc(s), a.v, a.off, a.err, b.v, b.off, b.err);
    return ok;
}

static uint32_t g_rng = 0xC0FFEE09u;                   /* 고정 시드 → 매번 같은 문자열 */
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}
static uint32_t rnd(uint32_t n) { return xorshift32() % n; }

/* 하네스용: 크기 mag 를 base 로 써서 buf 에 붙인다 (대소문자 섞기) */
static void put_mag(char *buf, size_t cap, unsigned long mag, int base, bool upper) {
    char tmp[80]; int n = 0;
    do { int d = (int)(mag % (unsigned)base); mag /= (unsigned)base;
         tmp[n++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10); } while (mag);
    size_t L = strlen(buf);
    while (n > 0 && L + 1 < cap) buf[L++] = tmp[--n];
    buf[L] = '\0';
}

/* 1) 무작위 문자 조합: 공백·부호·0·x·숫자·글자·기호 */
static int fuzz_alphabet(int count) {
    static const char A[] = " \t\n\v\f\r+-0000xXbB1789aAfFgGzZ_#.";
    static const int bases[] = { 0, 0, 2, 8, 10, 10, 16, 16, 36, 1, 37, -1 };
    int bad = 0;
    for (int i = 0; i < count; i++) {
        char s[16]; int n = (int)rnd(13);
        for (int k = 0; k < n; k++) s[k] = A[rnd(sizeof A - 1)];
        s[n] = '\0';
        int base = rnd(4) ? bases[rnd(12)] : 2 + (int)rnd(35);
        bad += !same(s, base);
    }
    return bad;
}
/* 2) LONG_MAX / LONG_MIN 바로 주변 (±3) 을 여러 base·접두사·앞자리 0 으로 */
static int fuzz_boundary(void) {
    static const int bases[] = { 2, 3, 7, 8, 10, 16, 36, 0 };
    int bad = 0;
    for (int bi = 0; bi < 8; bi++) for (int sign = 0; sign < 2; sign++) for (int dd = -3; dd <= 3; dd++) {
        int base = bases[bi], fb = base ? base : (int)(dd & 1 ? 16 : 10);
        unsigned long lim = sign ? (unsigned long)LONG_MAX + 1u : (unsigned long)LONG_MAX;
        unsigned long mag = lim + (unsigned long)(long)dd;          /* 경계 ± dd (unsigned 산술, 정의됨) */
        char s[96] = "";
        strcat(s, sign ? "-" : (dd & 2 ? "+" : ""));
        if (base == 0 && fb == 16) strcat(s, "0x");
        if (base == 16 && dd == 0) strcat(s, "0X");
        if (dd == 1 && base != 0) strcat(s, "000");                  /* 앞자리 0 은 크기를 안 바꾼다 */
        put_mag(s, sizeof s, mag, fb, dd < 0);
        if (dd == -2) strcat(s, "@tail");
        bad += !same(s, base);
    }
    return bad;
}
/* 3) 무작위 64비트 값 + 무작위 공백·부호·base·찌꺼기 */
static int fuzz_values(int count) {
    int bad = 0;
    for (int i = 0; i < count; i++) {
        unsigned long mag = ((unsigned long)xorshift32() << 32 | xorshift32()) >> rnd(64);
        int base = rnd(5) == 0 ? 0 : 2 + (int)rnd(35);
        int fb = base ? base : (int[]){ 8, 10, 16 }[rnd(3)];
        char s[96] = "";
        if (rnd(3) == 0) strcat(s, rnd(2) ? "  " : "\t");
        if (rnd(2)) strcat(s, rnd(2) ? "-" : "+");
        if (fb == 16 && (base == 0 || rnd(2))) strcat(s, rnd(2) ? "0x" : "0X");
        if (fb == 8 && base == 0) strcat(s, "0");
        put_mag(s, sizeof s, mag, fb, rnd(2));
        if (rnd(3) == 0) strcat(s, (const char *[]){ "z", " 12", "x", "9" }[rnd(4)]);
        bad += !same(s, base);
    }
    return bad;
}
/* 4) 아주 긴 숫자열 (20~60자리) — 오버플로 후에도 end 가 끝까지 가는지 */
static int fuzz_long(int count) {
    int bad = 0;
    for (int i = 0; i < count; i++) {
        int base = (int[]){ 2, 10, 16, 36, 0 }[rnd(5)];
        int fb = base ? base : 10;
        char s[80]; int n = 0;
        if (rnd(2)) s[n++] = '-';
        int len = 20 + (int)rnd(41);
        for (int k = 0; k < len; k++) { int d = (int)rnd((uint32_t)fb); s[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); }
        if (base == 0 && s[n - len] == '0') s[n - len] = '1';         /* base 0 이 8진으로 바뀌지 않게 */
        s[n] = '\0';
        bad += !same(s, base);
    }
    return bad;
}

int main(void) {
    /* Part A: digit_val */
    CHECK(digit_val('0') == 0 && digit_val('9') == 9);
    CHECK(digit_val('a') == 10 && digit_val('A') == 10 && digit_val('z') == 35 && digit_val('Z') == 35);
    CHECK(digit_val(' ') >= 36 && digit_val('/') >= 36 && digit_val(':') >= 36 && digit_val('@') >= 36);
    CHECK(digit_val('[') >= 36 && digit_val('`') >= 36 && digit_val('{') >= 36 && digit_val(0xE9) >= 36);

    /* Part A + B: 표 기반 대조 (값, end 오프셋, errno 모두 libc 와 같아야) */
    static const struct { const char *s; int base; } T[] = {
        { "0", 10 }, { "42", 10 }, { "-42", 10 }, { "+42", 10 }, { "  \t\n\v\f\r 7", 10 }, { "-0", 10 },
        { "123abc", 10 }, { "12 34", 10 }, { "1_000", 10 }, { "09", 10 },
        { "", 10 }, { "   ", 10 }, { "+", 10 }, { "-", 10 }, { " - 1", 10 }, { "+-1", 10 }, { "--1", 10 }, { "abc", 10 },
        { "1010", 2 }, { "-1111", 2 }, { "102", 2 }, { "777", 8 }, { "778", 8 }, { "zz", 36 }, { "ZZ", 36 },
        { "7fffffff", 16 }, { "ff", 16 }, { "FF", 16 }, { "0xff", 16 }, { "0XFF", 16 }, { "-0x1f", 16 },
        { "0x", 16 }, { "0X", 16 }, { "0xg", 16 }, { "  -0x", 16 }, { "0x", 10 }, { "0x10", 10 },
        { "0x1f", 0 }, { " -0x1F", 0 }, { "010", 0 }, { "-010", 0 }, { "08", 0 }, { "0", 0 }, { "12", 0 },
        { "0x", 0 }, { "0xz", 0 }, { "0x-1", 0 }, { "0b101", 0 }, { "0b101", 2 }, { "00x1", 0 },
        { "9223372036854775807", 10 }, { "9223372036854775808", 10 },
        { "-9223372036854775808", 10 }, { "-9223372036854775809", 10 },
        { "99999999999999999999999xyz", 10 }, { "-99999999999999999999999", 10 },
        { "0x7fffffffffffffff", 0 }, { "0x8000000000000000", 0 }, { "-0x8000000000000000", 0 },
        { "-0x8000000000000001", 0 }, { "0777777777777777777777", 0 }, { "01000000000000000000000", 0 },
        { "1y2p0ij32e8e7", 36 }, { "1y2p0ij32e8e8", 36 }, { "-1y2p0ij32e8e8", 36 }, { "-1y2p0ij32e8e9", 36 },
        { "12", 1 }, { "12", 37 }, { "12", -1 }, { "  12", 99 },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) CHECK(same(T[i].s, T[i].base));

    /* end 인자가 NULL 이어도 되고, 성공 시 errno 를 0 으로 "지우지" 않는다 (C11 7.5p3) */
    errno = EDOM;
    CHECK(my_strtol("123", NULL, 10) == 123 && errno == EDOM);

    /* 생성 문자열 대조 — 고정 시드라 매 실행 동일 */
    CHECK(fuzz_alphabet(3000) == 0);
    CHECK(fuzz_boundary() == 0);
    CHECK(fuzz_values(3000) == 0);
    CHECK(fuzz_long(500) == 0);

    /* Part B: atoi (범위 안에서는 libc atoi 와 같다. 범위 밖 libc atoi 는 UB 라 비교하지 않는다) */
    CHECK(my_atoi("  -123xyz") == atoi("  -123xyz"));
    CHECK(my_atoi("2147483647") == INT_MAX && my_atoi("-2147483648") == INT_MIN);
    CHECK(my_atoi("2147483648") == INT_MAX && my_atoi("-99999999999999999999") == INT_MIN);
    CHECK(my_atoi("abc") == 0 && my_atoi("0x1F") == 0);

    /* Part B: parse_int_strict — atoi 가 구분 못 하는 것들을 구분한다 */
    int out = 777;
    CHECK(parse_int_strict(" 42 ", &out) == PI_OK && out == 42);
    CHECK(parse_int_strict("-2147483648", &out) == PI_OK && out == INT_MIN);
    out = 777;
    CHECK(parse_int_strict("", &out) == PI_NODIGITS && out == 777);
    CHECK(parse_int_strict("  -", &out) == PI_NODIGITS);
    CHECK(parse_int_strict("0", &out) == PI_OK && out == 0);      /* atoi 로는 "0" 과 "abc" 가 같다 */
    CHECK(parse_int_strict("12abc", &out) == PI_JUNK);
    CHECK(parse_int_strict("2147483648", &out) == PI_RANGE);
    CHECK(parse_int_strict("99999999999999999999", &out) == PI_RANGE);

    /* Part C: UART 명령 */
    cmd_t c = { OP_RST, 0, 0 };
    char r[32];
    CHECK(cli_parse("SET 0x1F -42", &c) == CLI_OK && c.op == OP_SET && c.reg == 31 && c.val == -42);
    CHECK(cli_parse(" SET\t3  -0x10\r\n", &c) == CLI_OK && c.reg == 3 && c.val == -16);
    CHECK(cli_parse("SET 1 010", &c) == CLI_OK && c.val == 8);               /* base 0: 앞자리 0 = 8진수! */
    CHECK(cli_parse("SET 1 -2147483648", &c) == CLI_OK && c.val == INT32_MIN);
    CHECK(cli_parse("GET 7\r\n", &c) == CLI_OK && c.op == OP_GET && c.reg == 7);
    CHECK(cli_parse("RST", &c) == CLI_OK && c.op == OP_RST);
    CHECK(cli_parse("", &c) == CLI_EMPTY && cli_parse(" \r\n", &c) == CLI_EMPTY);
    CHECK(cli_parse("set 1 2", &c) == CLI_UNKNOWN && cli_parse("SETX 1 2", &c) == CLI_UNKNOWN);
    CHECK(cli_parse("SET 1", &c) == CLI_ARGC && cli_parse("SET 1 2 3", &c) == CLI_ARGC);
    CHECK(cli_parse("RST now", &c) == CLI_ARGC && cli_parse("GET", &c) == CLI_ARGC);
    CHECK(cli_parse("SET 1 12abc", &c) == CLI_NOTNUM);
    CHECK(cli_parse("SET 0x 5", &c) == CLI_NOTNUM);                          /* "0x" → 0 까지만 소비 */
    CHECK(cli_parse("SET 1 08", &c) == CLI_NOTNUM);                          /* 8진수에 8 은 없다 */
    CHECK(cli_parse("SET 1 -", &c) == CLI_NOTNUM && cli_parse("GET +", &c) == CLI_NOTNUM);
    CHECK(cli_parse("SET 0x20 1", &c) == CLI_RANGE && cli_parse("GET -1", &c) == CLI_RANGE);
    CHECK(cli_parse("SET 1 2147483648", &c) == CLI_RANGE);                   /* int32 밖 */
    CHECK(cli_parse("SET 1 99999999999999999999", &c) == CLI_RANGE);         /* long 밖 → ERANGE */
    cli_exec("RST", r, sizeof r);
    CHECK(cli_exec("SET 0x1F -42", r, sizeof r) == CLI_OK && strcmp(r, "OK") == 0);
    CHECK(cli_exec("GET 31", r, sizeof r) == CLI_OK && strcmp(r, "R31=-42") == 0);
    CHECK(cli_exec("SET 0x1F 0x", r, sizeof r) == CLI_NOTNUM && strcmp(r, "ERR NOTNUM") == 0);
    CHECK(cli_exec("GET 0x1f", r, sizeof r) == CLI_OK && strcmp(r, "R31=-42") == 0);  /* 실패한 SET 은 쓰지 않는다 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
