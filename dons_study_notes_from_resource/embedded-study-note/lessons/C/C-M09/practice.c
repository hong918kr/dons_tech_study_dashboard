/* C-M09 추가 연습 — 한 번에 검증
 * 연습 1: my_strtoul ("-1" 이 ULONG_MAX 가 되는 이유)
 * 연습 2: unsigned 없이 음수로 누적하는 strtol (BSD 옛 구현 방식)
 * 연습 3: 한계를 매개변수로 — ILP32(long = 32비트) 흉내 strto_i32 / strto_i16
 * 연습 4: "0x10, 7,-3" 같은 쉼표 목록 파서 (에러 위치 보고)
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

static int digit_val(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 36;
}
static const char *resolve_base(const char *p, int *base) {
    bool hex_prefix = p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && digit_val((unsigned char)p[2]) < 16;
    if (*base == 0) *base = hex_prefix ? 16 : (p[0] == '0' ? 8 : 10);
    return (*base == 16 && hex_prefix) ? p + 2 : p;
}
/* 공통 앞부분: base 검사 → 공백 → 부호 → 접두사. base 가 잘못이면 NULL */
static const char *front(const char *s, int *base, bool *neg) {
    if (*base < 0 || *base == 1 || *base > 36) return NULL;
    while (isspace((unsigned char)*s)) s++;
    *neg = false;
    if (*s == '+' || *s == '-') { *neg = (*s == '-'); s++; }
    return resolve_base(s, base);
}

/* ---------- 연습 1: strtoul — 부호는 "결과 타입에서" 부정한다 (C11 7.22.1.4p5) ---------- */
static unsigned long my_strtoul(const char *s, char **end, int base) {
    bool neg;
    const char *p = front(s, &base, &neg);
    if (!p) { if (end) *end = (char *)s; errno = EINVAL; return 0; }
    unsigned long cutoff = ULONG_MAX / (unsigned)base, acc = 0;
    unsigned cutlim = (unsigned)(ULONG_MAX % (unsigned)base);
    bool any = false, ovf = false;
    for (int d; (d = digit_val((unsigned char)*p)) < base; p++) {
        any = true;
        if (ovf) continue;
        if (acc > cutoff || (acc == cutoff && (unsigned)d > cutlim)) ovf = true;
        else acc = acc * (unsigned)base + (unsigned)d;
    }
    if (!any) { if (end) *end = (char *)s; errno = EINVAL; return 0; }
    if (end) *end = (char *)p;
    if (ovf) { errno = ERANGE; return ULONG_MAX; }      /* 부호가 '-' 여도 ULONG_MAX */
    return neg ? 0ul - acc : acc;                         /* unsigned 부정: 2^64 - acc (정의된 동작) */
}

/* ---------- 연습 2: 음수 쪽으로 누적 — signed 만 쓰고도 overflow 없음 ---------- */
/* |LONG_MIN| >= |LONG_MAX| 이므로 음수 쪽에 담으면 LONG_MIN 까지 그대로 표현된다.
 * C99 부터 / 는 0 쪽으로 버림 → LONG_MIN / base 는 음수, LONG_MIN % base 는 (-base, 0]. */
static long strtol_negacc(const char *s, char **end, int base) {
    bool neg;
    const char *p = front(s, &base, &neg);
    if (!p) { if (end) *end = (char *)s; errno = EINVAL; return 0; }
    long cutoff = LONG_MIN / base;
    int  cutlim = -(int)(LONG_MIN % base);
    long acc = 0;                                         /* 항상 <= 0 */
    bool any = false, ovf = false;
    for (int d; (d = digit_val((unsigned char)*p)) < base; p++) {
        any = true;
        if (ovf) continue;
        if (acc < cutoff || (acc == cutoff && d > cutlim)) ovf = true;
        else acc = acc * base - d;                        /* >= LONG_MIN 이 보장된 뒤에만 계산 */
    }
    if (!any) { if (end) *end = (char *)s; errno = EINVAL; return 0; }
    if (end) *end = (char *)p;
    if (ovf) { errno = ERANGE; return neg ? LONG_MIN : LONG_MAX; }
    if (neg) return acc;
    if (acc < -LONG_MAX) { errno = ERANGE; return LONG_MAX; }  /* "+9223372036854775808" */
    return -acc;
}

/* ---------- 연습 3: 한계를 매개변수로 — 같은 코드가 32비트 long 에서도 맞다 ---------- */
#define DEFINE_STRTO(NAME, T, TMIN, TMAX)                                              \
    static T NAME(const char *s, char **end, int base) {                               \
        bool neg;                                                                      \
        const char *p = front(s, &base, &neg);                                         \
        if (!p) { if (end) *end = (char *)s; errno = EINVAL; return 0; }               \
        uintmax_t limit = neg ? (uintmax_t)(TMAX) + 1u : (uintmax_t)(TMAX);            \
        uintmax_t cutoff = limit / (unsigned)base, acc = 0;                            \
        unsigned cutlim = (unsigned)(limit % (unsigned)base);                          \
        bool any = false, ovf = false;                                                 \
        for (int d; (d = digit_val((unsigned char)*p)) < base; p++) {                  \
            any = true;                                                                \
            if (ovf) continue;                                                         \
            if (acc > cutoff || (acc == cutoff && (unsigned)d > cutlim)) ovf = true;   \
            else acc = acc * (unsigned)base + (unsigned)d;                             \
        }                                                                              \
        if (!any) { if (end) *end = (char *)s; errno = EINVAL; return 0; }             \
        if (end) *end = (char *)p;                                                     \
        if (ovf) { errno = ERANGE; return neg ? (TMIN) : (TMAX); }                     \
        if (!neg) return (T)acc;                                                       \
        return acc == limit ? (TMIN) : (T)-(T)acc;                                     \
    }
DEFINE_STRTO(strto_i32, int32_t, INT32_MIN, INT32_MAX)   /* Cortex-M 의 long 과 같은 폭 */
DEFINE_STRTO(strto_i16, int16_t, INT16_MIN, INT16_MAX)

/* ---------- 연습 4: 쉼표 목록 "0x10, 7,-3" ---------- */
/* 성공: 항목 수. 실패: -1, *err_pos = 문제 항목(또는 문자)의 위치. 빈 문자열은 0개. */
static int parse_list(const char *s, long *out, int cap, size_t *err_pos) {
    const char *p = s;
    int n = 0;
    while (*p == ' ') p++;
    if (*p == '\0') return 0;
    for (;;) {
        while (*p == ' ') p++;
        char *e;
        errno = 0;
        long v = strtol(p, &e, 0);
        if (e == p || errno == ERANGE || n == cap) { *err_pos = (size_t)(p - s); return -1; }
        out[n++] = v;
        p = e;
        while (*p == ' ') p++;
        if (*p == '\0') return n;
        if (*p != ',') { *err_pos = (size_t)(p - s); return -1; }
        p++;                                              /* ',' 다음엔 반드시 항목이 와야 한다 */
    }
}

/* ---------------- 테스트 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x5EED0009u;
static uint32_t xorshift32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static uint32_t rnd(uint32_t n) { return xorshift32() % n; }

/* 무작위 문자열 하나: 공백·부호·접두사·숫자(때로 아주 큼)·찌꺼기 */
static int gen(char *s, int *base_out) {
    static const char A[] = " \t+-0xX19afAFgz";
    int base = (int[]){ 0, 2, 8, 10, 10, 16, 36, 1 + (int)rnd(36) }[rnd(8)];
    int n = 0;
    if (rnd(4) == 0) { int k = (int)rnd(10); while (k--) s[n++] = A[rnd(sizeof A - 1)]; }
    else {
        if (rnd(3) == 0) s[n++] = ' ';
        if (rnd(2)) s[n++] = rnd(2) ? '-' : '+';
        if (rnd(3) == 0) { s[n++] = '0'; s[n++] = 'x'; }
        int len = 1 + (int)rnd(rnd(4) ? 12 : 30);
        for (int k = 0; k < len; k++) s[n++] = "0123456789abcdefABCDEFz"[rnd(23)];
    }
    s[n] = '\0';
    *base_out = base;
    return n;
}

int main(void) {
    char *e1, *e2;
    char s[64];
    int base;

    /* 연습 1 */
    errno = 0;
    CHECK(my_strtoul("-1", &e1, 10) == ULONG_MAX && errno == 0 && *e1 == '\0');   /* 에러 아님! */
    errno = 0;
    CHECK(my_strtoul("18446744073709551616", NULL, 10) == ULONG_MAX && errno == ERANGE);
    errno = 0;
    CHECK(my_strtoul("-18446744073709551616", NULL, 10) == ULONG_MAX && errno == ERANGE);
    CHECK(my_strtoul("0xFFFFFFFF", NULL, 0) == 0xFFFFFFFFul);
    int bad1 = 0;
    for (int i = 0; i < 2000; i++) {
        gen(s, &base);
        errno = 0; unsigned long a = strtoul(s, &e1, base); int ea = errno;
        errno = 0; unsigned long b = my_strtoul(s, &e2, base); int eb = errno;
        bad1 += !(a == b && e1 == e2 && ea == eb);
    }
    CHECK(bad1 == 0);

    /* 연습 2 */
    errno = 0;
    CHECK(strtol_negacc("-9223372036854775808", NULL, 10) == LONG_MIN && errno == 0);
    errno = 0;
    CHECK(strtol_negacc("9223372036854775808", NULL, 10) == LONG_MAX && errno == ERANGE);
    int bad2 = 0;
    for (int i = 0; i < 3000; i++) {
        gen(s, &base);
        errno = 0; long a = strtol(s, &e1, base); int ea = errno;
        errno = 0; long b = strtol_negacc(s, &e2, base); int eb = errno;
        bad2 += !(a == b && e1 == e2 && ea == eb);
    }
    CHECK(bad2 == 0);

    /* 연습 3: 기대값 = libc strtol(64비트) 결과를 32/16비트로 포화한 것 */
    errno = 0;
    CHECK(strto_i32("2147483647", NULL, 10) == INT32_MAX && errno == 0);
    errno = 0;
    CHECK(strto_i32("2147483648", NULL, 10) == INT32_MAX && errno == ERANGE);
    errno = 0;
    CHECK(strto_i32("-0x80000000", NULL, 0) == INT32_MIN && errno == 0);
    errno = 0;
    CHECK(strto_i32("0xFFFFFFFF", NULL, 0) == INT32_MAX && errno == ERANGE);     /* ILP32 strtol 의 함정 */
    errno = 0;
    CHECK(strto_i16("-32769", NULL, 10) == INT16_MIN && errno == ERANGE);
    int bad3 = 0;
    for (int i = 0; i < 3000; i++) {
        gen(s, &base);
        errno = 0; long a = strtol(s, &e1, base); int ea = errno;
        if (a > INT32_MAX) { a = INT32_MAX; ea = ERANGE; }
        if (a < INT32_MIN) { a = INT32_MIN; ea = ERANGE; }
        errno = 0; int32_t b = strto_i32(s, &e2, base); int eb = errno;
        bad3 += !(a == b && e1 == e2 && ea == eb);
    }
    CHECK(bad3 == 0);

    /* 연습 4 */
    long v[4];
    size_t pos = 0;
    CHECK(parse_list("0x10, 7,-3", v, 4, &pos) == 3 && v[0] == 16 && v[1] == 7 && v[2] == -3);
    CHECK(parse_list("   ", v, 4, &pos) == 0);
    CHECK(parse_list("1,,2", v, 4, &pos) == -1 && pos == 2);
    CHECK(parse_list("1, 2,", v, 4, &pos) == -1 && pos == 5);
    CHECK(parse_list("5 6", v, 4, &pos) == -1 && pos == 2);
    CHECK(parse_list("0x,1", v, 4, &pos) == -1 && pos == 1);
    CHECK(parse_list("1,99999999999999999999", v, 4, &pos) == -1 && pos == 2);
    CHECK(parse_list("1,2,3,4,5", v, 4, &pos) == -1 && pos == 8);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
