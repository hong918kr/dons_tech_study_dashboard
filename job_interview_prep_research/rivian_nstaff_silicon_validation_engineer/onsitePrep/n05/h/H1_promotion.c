/* H1 · integer promotion — "what does this print?"
 * Every expression here is well-defined (the UB one, u16*u16, lives in
 * H1_promotion_ub.c). Each line prints the value and asserts it. */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    /* (a) ~ on a uint8_t: operand is promoted to int FIRST, then inverted */
    uint8_t x = 0x0F;
    int a = (~x == 0xF0);               /* ~x is int 0xFFFFFFF0, not 0xF0 */
    printf("(a) ~x == 0xF0 -> %d   (~x = 0x%X)\n", a, (unsigned)~x);
    assert(a == 0);
    assert((uint8_t)~x == 0xF0);        /* fix: cast back to the narrow type */

    /* (b) shift of a uint8_t is done in int: bits are NOT lost */
    uint8_t b = 0xF0;
    printf("(b) b << 4 = 0x%X, (uint8_t)(b << 4) = 0x%X\n",
           (unsigned)(b << 4), (unsigned)(uint8_t)(b << 4));
    assert((b << 4) == 0xF00);
    assert((uint8_t)(b << 4) == 0x00);

    /* (c) uint8_t + uint8_t is an int addition */
    uint8_t p = 200, q = 100;
    uint8_t s8 = (uint8_t)(p + q);
    printf("(c) p + q = %d, stored in uint8_t = %u\n", p + q, (unsigned)s8);
    assert(p + q == 300 && s8 == 44);

    /* (d) uint32_t + 1 wraps in 32 bits BEFORE widening to 64 */
    uint32_t big = 0xFFFFFFFFu;
    uint64_t w_bad  = big + 1;              /* computed as uint32_t -> 0 */
    uint64_t w_good = (uint64_t)big + 1;    /* cast first */
    printf("(d) big + 1 = %llu, (uint64_t)big + 1 = %llu\n",
           (unsigned long long)w_bad, (unsigned long long)w_good);
    assert(w_bad == 0 && w_good == 0x100000000ull);

    /* (e) int8_t -> uint32_t: sign-extend, then reduce mod 2^32 */
    int8_t neg = -1;
    uint32_t u = (uint32_t)neg;
    printf("(e) (uint32_t)(int8_t)-1 = 0x%08X\n", (unsigned)u);
    assert(u == 0xFFFFFFFFu);

    /* (f) plain char: signedness is implementation-defined */
    /* (clang flags `c == 0xFF` with a literal as always-false on signed-char
     * targets; `ff` is a runtime int so the comparison really executes) */
    char c = (char)0xFF;
    volatile int ff = 0xFF;
    printf("(f) CHAR_MIN = %d, (int)c = %d, c == 0xFF -> %d\n",
           CHAR_MIN, (int)c, c == ff);
#if CHAR_MIN < 0          /* Apple arm64, x86 Linux: signed char */
    assert((int)c == -1 && !(c == ff));
#else                     /* Linux aarch64 / ARM EABI: unsigned char */
    assert((int)c == 255 && (c == ff));
#endif

    /* (g) uint16_t - uint16_t < 0 is TRUE (int math); uint32_t version is FALSE */
    uint16_t h1 = 1, h2 = 2;
    uint32_t w1 = 1, w2 = 2;
    printf("(g) u16: (h1 - h2 < 0) -> %d   u32: w1 - w2 = 0x%X (never < 0)\n",
           (h1 - h2 < 0), (unsigned)(w1 - w2));
    assert(h1 - h2 < 0);
    assert(w1 - w2 == 0xFFFFFFFFu);

    /* (h) the safe u16*u16: widen one operand to unsigned 32 first */
    uint16_t m = 0xFFFF;
    uint32_t prod = (uint32_t)m * m;
    printf("(h) (uint32_t)m * m = 0x%08X\n", (unsigned)prod);
    assert(prod == 0xFFFE0001u);

    puts("H1 PASS");
    return 0;
}
