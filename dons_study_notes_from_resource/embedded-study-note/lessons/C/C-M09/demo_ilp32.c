/* C-M09 demo — 같은 cutoff 코드를 Cortex-M(ILP32: long = 32비트)으로 크로스 컴파일해 본다.
 * 프리스탠딩 헤더만 쓴다 (<limits.h> <stdbool.h>) — errno 대신 *err 로 돌려준다.
 *   clang -std=c11 --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_ilp32.c -o -
 *   clang -std=c11 --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_ilp32.c -o -
 *   clang -std=c11 -O2 -S demo_ilp32.c -o -          # 호스트(arm64, LP64) 비교
 */
#include <limits.h>
#include <stdbool.h>

#if defined(__arm__)
_Static_assert(sizeof(long) == 4 && LONG_MAX == 2147483647L, "AAPCS: long is 32-bit");
#else
_Static_assert(sizeof(long) == 8, "LP64 host");
#endif

static inline int dval(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 36;
}

/* 숫자 부분만 (공백·부호·접두사는 처리했다고 가정). err: 0 ok, 1 no digits, 2 range */
static inline long digits(const char *p, const char **end, int base, bool neg, int *err) {
    unsigned long limit  = neg ? (unsigned long)LONG_MAX + 1u : (unsigned long)LONG_MAX;
    unsigned long cutoff = limit / (unsigned)base;
    unsigned      cutlim = (unsigned)(limit % (unsigned)base);
    unsigned long acc = 0;
    bool any = false, ovf = false;
    for (int d; (d = dval((unsigned char)*p)) < base; p++) {
        any = true;
        if (ovf) continue;
        if (acc > cutoff || (acc == cutoff && (unsigned)d > cutlim)) ovf = true;
        else acc = acc * (unsigned)base + (unsigned)d;
    }
    *end = p;
    *err = !any ? 1 : ovf ? 2 : 0;
    if (!any) return 0;
    if (ovf) return neg ? LONG_MIN : LONG_MAX;
    if (!neg) return (long)acc;
    return acc == limit ? LONG_MIN : -(long)acc;
}

/* base 가 런타임 값: cutoff/cutlim 에 나눗셈이 필요하다 */
long digits_any(const char *p, const char **end, int base, bool neg, int *err) {
    return digits(p, end, base, neg, err);
}
/* base 10 고정: cutoff = 214748364, cutlim = 7 또는 8 이 컴파일 시간 상수가 된다 */
long digits_dec(const char *p, const char **end, bool neg, int *err) {
    return digits(p, end, 10, neg, err);
}
