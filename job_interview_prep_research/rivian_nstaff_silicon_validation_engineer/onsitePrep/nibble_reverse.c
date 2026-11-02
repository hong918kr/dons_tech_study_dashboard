/* Tech-screen problem (2026-10-30): "Reverse order of input buffer data:
 * 0x1234567890abcdef -> 0xfedcba0987654321"  == reverse the 16 nibbles (hex digits).
 * cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined nibble_reverse.c && ./a.out
 */
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* A. Don's answer, cleaned up: pop the low nibble of `in`, push it into `res`. 16 iterations. */
uint64_t rev_nibbles64(uint64_t in)
{
    uint64_t res = 0;
    for (int i = 0; i < 16; i++) {
        res = (res << 4) | (in & 0xFu);
        in >>= 4;
    }
    return res;
}

/* B. O(log n) mask-and-swap: reverse bytes, then swap the two nibbles inside each byte. */
uint64_t bswap64(uint64_t x)
{
    x = ((x >> 8)  & 0x00FF00FF00FF00FFull) | ((x & 0x00FF00FF00FF00FFull) << 8);
    x = ((x >> 16) & 0x0000FFFF0000FFFFull) | ((x & 0x0000FFFF0000FFFFull) << 16);
    return (x >> 32) | (x << 32);
}

uint64_t rev_nibbles64_fast(uint64_t x)
{
    x = ((x >> 4) & 0x0F0F0F0F0F0F0F0Full) | ((x & 0x0F0F0F0F0F0F0F0Full) << 4);
    return bswap64(x);
}

/* C. Buffer version ("input buffer data"), any length, in place.
 * buf[0] is the most significant byte as written: {12 34 .. ef} -> {fe dc .. 21}. */
static inline uint8_t swap_nib(uint8_t b) { return (uint8_t)((b << 4) | (b >> 4)); }

void rev_nibbles_buf(uint8_t *buf, size_t n)
{
    if (n == 0)
        return;
    size_t i = 0, j = n - 1;
    while (i < j) {
        uint8_t a = buf[i];
        buf[i] = swap_nib(buf[j]);
        buf[j] = swap_nib(a);
        i++;
        j--;
    }
    if (i == j)
        buf[i] = swap_nib(buf[i]);      /* odd length: middle byte only swaps its nibbles */
}

/* D. Generic: reverse 64/w units of w bits (w = 1 bits, 4 nibbles, 8 bytes, 16, 32). */
uint64_t rev_units64(uint64_t x, unsigned w)
{
    uint64_t mask = (1ull << w) - 1u;   /* w <= 32, so no 1<<64 UB */
    uint64_t res = 0;
    for (unsigned i = 0; i < 64u / w; i++) {
        res = (res << w) | (x & mask);
        x >>= w;
    }
    return res;
}

int main(void)
{
    const uint64_t in = 0x1234567890abcdefull;

    CHECK(rev_nibbles64(in) == 0xfedcba0987654321ull);
    CHECK(rev_nibbles64_fast(in) == 0xfedcba0987654321ull);
    CHECK(rev_nibbles64(0) == 0 && rev_nibbles64(~0ull) == ~0ull);
    CHECK(rev_nibbles64(0xFull) == 0xF000000000000000ull);
    CHECK(rev_nibbles64(rev_nibbles64(in)) == in);                 /* involution */

    CHECK(bswap64(in) == 0xefcdab9078563412ull);                   /* byte reverse != nibble reverse */
    CHECK(rev_units64(in, 8) == bswap64(in));
    CHECK(rev_units64(in, 4) == rev_nibbles64(in));
    uint64_t bits = rev_units64(in, 1);
    printf("bit-reverse  0x%016" PRIx64 "\n", bits);
    CHECK(bits == 0xf7b3d5091e6a2c48ull);
    CHECK(rev_units64(bits, 1) == in);

    uint8_t b8[8] = { 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef };
    const uint8_t e8[8] = { 0xfe, 0xdc, 0xba, 0x09, 0x87, 0x65, 0x43, 0x21 };
    rev_nibbles_buf(b8, 8);
    CHECK(memcmp(b8, e8, 8) == 0);
    uint8_t b3[3] = { 0x12, 0x34, 0x56 };
    rev_nibbles_buf(b3, 3);
    CHECK(b3[0] == 0x65 && b3[1] == 0x43 && b3[2] == 0x21);
    uint8_t b1[1] = { 0xab };
    rev_nibbles_buf(b1, 1);
    CHECK(b1[0] == 0xba);
    rev_nibbles_buf(NULL, 0);

    uint64_t r = 0x9E3779B97F4A7C15ull;                            /* loop vs fast, 10k random */
    for (int k = 0; k < 10000; k++) {
        r ^= r << 13; r ^= r >> 7; r ^= r << 17;
        if (rev_nibbles64(r) != rev_nibbles64_fast(r)) { CHECK(0); break; }
    }

    printf("in           0x%016" PRIx64 "\nnibble-rev   0x%016" PRIx64 "\nbyte-rev     0x%016" PRIx64 "\n",
           in, rev_nibbles64(in), bswap64(in));
    puts(fails ? "FAIL" : "ALL PASS");
    return fails != 0;
}
