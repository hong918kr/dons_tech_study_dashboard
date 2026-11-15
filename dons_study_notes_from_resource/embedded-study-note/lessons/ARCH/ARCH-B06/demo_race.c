/* ARCH-B06 demo_race — 같은 회로, 같은 입력, 엔진만 다르게: 2-phase vs in-place(오름차순) vs in-place(내림차순)
 * 사이클별 텍스트 파형을 찍는다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_race.c -o demo_race && ./demo_race
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned (*dfn_t)(const uint8_t *q, unsigned i, unsigned in);
static uint32_t pack(const uint8_t *q, unsigned n) {
    uint32_t v = 0;
    for (unsigned i = 0; i < n; i++) v |= (uint32_t)(q[i] & 1u) << i;
    return v;
}
static void unpack(uint8_t *q, unsigned n, uint32_t v) {
    for (unsigned i = 0; i < n; i++) q[i] = (uint8_t)((v >> i) & 1u);
}
static void tick(uint8_t *q, unsigned n, dfn_t f, unsigned in) {
    uint8_t d[32];
    for (unsigned i = 0; i < n; i++) d[i] = (uint8_t)(f(q, i, in) & 1u);
    memcpy(q, d, n);
}
static void tick_asc(uint8_t *q, unsigned n, dfn_t f, unsigned in) {
    for (unsigned i = 0; i < n; i++) q[i] = (uint8_t)(f(q, i, in) & 1u);
}
static void tick_desc(uint8_t *q, unsigned n, dfn_t f, unsigned in) {
    for (unsigned i = n; i-- > 0;) q[i] = (uint8_t)(f(q, i, in) & 1u);
}
static unsigned shift8_d(const uint8_t *q, unsigned i, unsigned in) { return i == 0 ? (in & 1u) : q[i - 1]; }
static unsigned swap_d(const uint8_t *q, unsigned i, unsigned in) { (void)in; return q[i ^ 1u]; }
static unsigned fib16_d(const uint8_t *q, unsigned i, unsigned in) {
    (void)in;
    return i == 15 ? (unsigned)(q[0] ^ q[2] ^ q[3] ^ q[5]) : q[i + 1];
}
static void bin8(uint32_t v, char *s) { for (int i = 7; i >= 0; i--) *s++ = (char)('0' + ((v >> i) & 1u)); *s = 0; }

int main(void) {
    static const unsigned sin_w[10] = {1, 0, 1, 1, 0, 0, 0, 0, 1, 0};
    uint8_t a[8] = {0}, b[8] = {0}, c[8] = {0};
    char sa[9], sb[9], sc[9];
    printf("8-bit SIPO shift register, q[7..0]\n");
    printf("edge sin | 2-phase   in-place↑  in-place↓\n");
    for (unsigned t = 0; t < 10; t++) {
        tick(a, 8, shift8_d, sin_w[t]);
        tick_asc(b, 8, shift8_d, sin_w[t]);
        tick_desc(c, 8, shift8_d, sin_w[t]);
        bin8(pack(a, 8), sa); bin8(pack(b, 8), sb); bin8(pack(c, 8), sc);
        printf("%4u  %u  | %s  %s   %s\n", t + 1, sin_w[t], sa, sb, sc);
    }
    printf("\nswap (d0=q1, d1=q0), start q1q0=01\n");
    uint8_t x[2], y[2], z[2];
    unpack(x, 2, 1); unpack(y, 2, 1); unpack(z, 2, 1);
    for (unsigned t = 1; t <= 3; t++) {
        tick(x, 2, swap_d, 0); tick_asc(y, 2, swap_d, 0); tick_desc(z, 2, swap_d, 0);
        printf("edge %u: 2-phase %u%u  in-place↑ %u%u  in-place↓ %u%u\n", t,
               x[1], x[0], y[1], y[0], z[1], z[0]);
    }
    printf("\n16-bit Fibonacci LFSR (taps 16,14,13,11), seed 0xACE1\n");
    uint8_t f[16], g[16], h[16];
    unpack(f, 16, 0xACE1); unpack(g, 16, 0xACE1); unpack(h, 16, 0xACE1);
    for (unsigned t = 1; t <= 4; t++) {
        tick(f, 16, fib16_d, 0); tick_asc(g, 16, fib16_d, 0); tick_desc(h, 16, fib16_d, 0);
        printf("edge %u: 2-phase 0x%04X  in-place↑ 0x%04X  in-place↓ 0x%04X\n", t,
               (unsigned)pack(f, 16), (unsigned)pack(g, 16), (unsigned)pack(h, 16));
    }
    return 0;
}
