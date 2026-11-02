/* H8 · signed/unsigned done right (the surprising versions: H8_snippets.c). */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Linux-style wrap-safe "a is after b" for a free-running 32-bit counter.
 * (uint32_t -> int32_t of a value > INT32_MAX is implementation-defined,
 *  C11 6.3.1.3p3; gcc/clang/armcc define it as modulo 2^32.) */
static bool time_after_eq(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }

/* mixed signed/unsigned "less than" without the implicit conversion */
static bool lt_int_uint(int a, unsigned b) { return a < 0 || (unsigned)a < b; }

/* reverse walk over a buffer: correct for n == 0 too */
static size_t reverse_sum_idx(const uint8_t *buf, size_t n, size_t *last_idx) {
    size_t sum = 0;
    for (size_t i = n; i-- > 0; ) { sum += buf[i]; *last_idx = i; }
    return sum;
}

/* length that may arrive as a signed int (from a protocol field) */
static int copy_checked(uint8_t *dst, size_t cap, const uint8_t *src, int len) {
    if (len < 0 || (size_t)len > cap) return -1;    /* check sign BEFORE converting */
    memcpy(dst, src, (size_t)len);
    return len;
}

int main(void) {
    assert(-1 < (int)sizeof(int));                   /* compare in the signed domain */
    assert(lt_int_uint(-1, 1u) && !lt_int_uint(5, 1u));   /* test the sign first */

    const char *s = "abc";
    assert(!(strlen(s) > 5));                        /* not strlen(s) - 5 > 0         */

    assert(time_after_eq(0x00000010u, 0xFFFFFFF0u)); /* 0x20 ticks late: deadline hit */
    assert(!time_after_eq(0xFFFFFFF0u, 0x00000010u));
    assert(time_after_eq(100u, 100u));

    static const uint8_t b[3] = {1, 2, 3};
    size_t last = 99;
    assert(reverse_sum_idx(b, 3, &last) == 6 && last == 0);
    last = 99;
    assert(reverse_sum_idx(b, 0, &last) == 0 && last == 99);   /* n == 0: no iterations */

    uint8_t dst[4];
    assert(copy_checked(dst, sizeof dst, b, -1) == -1);
    assert(copy_checked(dst, sizeof dst, b, 5) == -1);
    assert(copy_checked(dst, sizeof dst, b, 3) == 3 && dst[2] == 3);

    printf("time_after_eq(0x10, 0xFFFFFFF0)=%d  (int32_t)(0x10-0xFFFFFFF0)=%d\n",
           time_after_eq(0x10u, 0xFFFFFFF0u), (int)(int32_t)(0x10u - 0xFFFFFFF0u));
    puts("H8 PASS");
    return 0;
}
