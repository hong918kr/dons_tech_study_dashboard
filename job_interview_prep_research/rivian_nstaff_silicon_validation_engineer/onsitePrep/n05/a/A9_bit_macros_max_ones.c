#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* BEGIN MODEL */
/* n must be 0..31. 1u (not 1) so bit 31 is not a signed-int overflow. */
#define BIT(n)            (1u << (n))
#define BIT_SET(x, n)     ((x) |=  BIT(n))
#define BIT_CLEAR(x, n)   ((x) &= ~BIT(n))
#define BIT_TOGGLE(x, n)  ((x) ^=  BIT(n))
#define BIT_TEST(x, n)    (((x) >> (n)) & 1u)          /* 0 or 1 */

/* Longest run of consecutive 1 bits. Each x &= x << 1 shortens every run by 1,
 * so the loop count is the longest run. No wrap-around between bit 31 and bit 0. */
unsigned max_consecutive_ones(uint32_t x)
{
    unsigned n = 0;
    while (x) {
        x &= x << 1;
        n++;
    }
    return n;
}
/* END MODEL */

int main(void)
{
    uint32_t r = 0;
    BIT_SET(r, 0);
    BIT_SET(r, 31);
    assert(r == 0x80000001u);
    assert(BIT_TEST(r, 31) == 1u && BIT_TEST(r, 0) == 1u && BIT_TEST(r, 5) == 0u);
    BIT_TOGGLE(r, 5);
    assert(r == 0x80000021u);
    BIT_TOGGLE(r, 5);
    assert(r == 0x80000001u);
    BIT_CLEAR(r, 31);
    assert(r == 1u);
    BIT_CLEAR(r, 7);                                   /* clearing a 0 is a no-op */
    assert(r == 1u);
    assert(max_consecutive_ones(0u) == 0u);
    assert(max_consecutive_ones(0xFFFFFFFFu) == 32u);
    assert(max_consecutive_ones(0x6Eu) == 3u);         /* 0110 1110 */
    assert(max_consecutive_ones(0x80000001u) == 1u);   /* no wrap */
    assert(max_consecutive_ones(0x0FF00F00u) == 8u);
    assert(max_consecutive_ones(0x55555555u) == 1u);
    assert(max_consecutive_ones(0xFFFF0000u) == 16u);
    puts("A9 PASS");
    return 0;
}
