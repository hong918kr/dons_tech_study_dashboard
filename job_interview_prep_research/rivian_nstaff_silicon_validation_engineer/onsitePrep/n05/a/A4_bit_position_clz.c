#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* BEGIN MODEL */
/* Index of highest set bit (0..31), -1 if x == 0. Binary search: 5 steps. */
int msb_index(uint32_t x)
{
    if (x == 0u)
        return -1;
    int n = 0;
    if (x >> 16) { n += 16; x >>= 16; }
    if (x >> 8)  { n += 8;  x >>= 8;  }
    if (x >> 4)  { n += 4;  x >>= 4;  }
    if (x >> 2)  { n += 2;  x >>= 2;  }
    if (x >> 1)  { n += 1; }
    return n;
}

/* Index of lowest set bit, -1 if x == 0. Isolate it with x & -x, then msb. */
int lsb_index(uint32_t x)
{
    return msb_index(x & (~x + 1u));     /* ~x + 1u == -x in unsigned */
}

/* Count leading zeros. clz32(0) == 32 by definition here (the builtin is UB at 0). */
unsigned clz32(uint32_t x)
{
    return (unsigned)(31 - msb_index(x));   /* x == 0: 31 - (-1) = 32 */
}
/* END MODEL */

int main(void)
{
    assert(msb_index(0u) == -1);
    assert(msb_index(1u) == 0);
    assert(msb_index(0x80000000u) == 31);
    assert(msb_index(0x00012345u) == 16);
    assert(msb_index(0xFFFFFFFFu) == 31);
    assert(lsb_index(0u) == -1);
    assert(lsb_index(0x68u) == 3);
    assert(lsb_index(0x80000000u) == 31);
    assert(lsb_index(0xFFFFFFFFu) == 0);
    assert(clz32(0u) == 32u);
    assert(clz32(1u) == 31u);
    assert(clz32(0x80000000u) == 0u);
    assert(clz32(0x00012345u) == 15u);
    uint32_t s = 777u;
    for (int i = 0; i < 100000; i++) {
        s = s * 1664525u + 1013904223u;
        uint32_t v = s >> (s & 31u);          /* vary magnitude */
        if (v == 0u)
            continue;
        assert(clz32(v) == (unsigned)__builtin_clz(v));
        assert(lsb_index(v) == __builtin_ctz(v));
    }
    puts("A4 PASS");
    return 0;
}
