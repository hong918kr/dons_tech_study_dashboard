/* ==== answer ==== */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint8_t bit;
    bool    now_set;                             /* true: 0->1, false: 1->0 */
} bit_change_t;

/* Lists changed bits LSB-first. Returns total number of changes (may exceed max_out). */
size_t reg_diff(uint32_t old_v, uint32_t new_v, bit_change_t *out, size_t max_out)
{
    uint32_t changed = old_v ^ new_v;
    size_t n = 0;
    for (unsigned bit = 0; changed != 0; bit++, changed >>= 1) {
        if ((changed & 1u) == 0)
            continue;
        if (n < max_out) {
            out[n].bit = (uint8_t)bit;
            out[n].now_set = ((new_v >> bit) & 1u) != 0;
        }
        n++;
    }
    return n;
}
/* ==== tests ==== */
#include <assert.h>
#include <stdio.h>

int main(void)
{
    bit_change_t c[32];
    size_t n = reg_diff(0x000000F0u, 0x00000118u, c, 32);
    assert(n == 5);
    const uint8_t bits[5] = { 3, 5, 6, 7, 8 };
    const bool set[5] = { true, false, false, false, true };
    for (size_t i = 0; i < n; i++) {
        assert(c[i].bit == bits[i] && c[i].now_set == set[i]);
        printf("  bit %2u: %s\n", c[i].bit, c[i].now_set ? "0->1" : "1->0");
    }
    /* rose / fell one-liners agree */
    uint32_t o = 0x000000F0u, nw = 0x00000118u;
    assert((~o & nw) == 0x108u && (o & ~nw) == 0x0E0u);

    assert(reg_diff(0x12345678u, 0x12345678u, c, 32) == 0);           /* no change */
    n = reg_diff(0, 0xFFFFFFFFu, c, 32);                               /* all rise */
    assert(n == 32 && c[0].bit == 0 && c[31].bit == 31 && c[31].now_set);
    n = reg_diff(0x80000000u, 0, c, 32);                               /* bit 31 fall */
    assert(n == 1 && c[0].bit == 31 && !c[0].now_set);
    /* truncated output: count is still total, out not overrun */
    bit_change_t small[2] = { { 0xEE, true }, { 0xEE, true } };
    bit_change_t guard = { 0xAB, true };
    n = reg_diff(0, 0x0000000Fu, small, 2);
    assert(n == 4 && small[0].bit == 0 && small[1].bit == 1 && guard.bit == 0xAB);
    assert(reg_diff(0, 0xFFu, NULL, 0) == 8);                          /* count only */
    printf("B7 PASS\n");
    return 0;
}
