/* TOOL-B12 demo: bugs that are NOT undefined behaviour, so -fsanitize=undefined
 * stays silent; only the opt-in checks catch them.
 *   cc -std=c11 -g -O1 -fsanitize=undefined demo_strict.c -o ds && ./ds
 *   cc -std=c11 -g -O1 -fsanitize=undefined,unsigned-integer-overflow,implicit-conversion \
 *      demo_strict.c -o ds2 && ./ds2 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int16_t q15_mul_naive(int16_t a, int16_t b)
{
    return (a * b) >> 15;          /* 32768 -> int16_t: implementation-defined, not UB */
}

static int span_ok_naive(size_t cap, size_t off, size_t len)
{
    return off + len <= cap;       /* size_t wraps (defined) -> wrong "ok" */
}

static void put_le16_naive(uint8_t *p, uint16_t v)
{
    p[0] = v;                      /* implicit uint16_t -> uint8_t truncation */
    p[1] = v >> 8;
}

int main(int argc, char **argv)
{
    (void)argv;
    int16_t m1 = (int16_t)(INT16_MIN + argc - 1);
    printf("q15_mul_naive(-1.0,-1.0) = %d (want 32767)\n", q15_mul_naive(m1, m1));
    printf("span_ok_naive(64, 8, SIZE_MAX) = %d (want 0)\n", span_ok_naive(64, 8, SIZE_MAX));
    uint8_t b[2];
    put_le16_naive(b, (uint16_t)(0x1234 + argc - 1));
    printf("put_le16_naive -> %02X %02X\n", b[0], b[1]);
    return 0;
}
