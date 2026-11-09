/* SIV-A10 demo — random-pattern-resistant 결함: AND(k) 체인의 모든 net stuck-at 을 잡는 데 필요한 패턴 수.
 * uniform LFSR24 vs weighted(7/8) vs 반반 나누기 + test point(CP 2, OP 2). 시드 3개 중 최악.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_resistant.c -o demo_resistant && ./demo_resistant
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define P24 0x1C20001u   /* x^24+x^23+x^22+x^17+1 */
#define MAXG 64
typedef struct { int npi, ng, npo; struct { uint8_t or_; uint16_t a, b; } g[MAXG]; uint16_t po[4]; } cut_t;
typedef struct { int net, val; } fault_t;

static uint32_t gstep(uint32_t s) { s <<= 1; if (s & (1u << 24)) s ^= P24; return s; }
static int addg(cut_t *c, int or_, int a, int b) {
    int i = c->ng++; c->g[i].or_ = (uint8_t)or_; c->g[i].a = (uint16_t)a; c->g[i].b = (uint16_t)b; return c->npi + i;
}
static uint32_t eval(const cut_t *c, uint32_t pi, fault_t f) {
    uint8_t v[128];
    for (int i = 0; i < c->npi; i++) { v[i] = (pi >> i) & 1u; if (f.net == i) v[i] = (uint8_t)f.val; }
    for (int i = 0; i < c->ng; i++) {
        uint8_t a = v[c->g[i].a], b = v[c->g[i].b];
        uint8_t o = c->g[i].or_ ? (a | b) : (a & b);
        int out = c->npi + i;
        v[out] = f.net == out ? (uint8_t)f.val : o;
    }
    uint32_t r = 0;
    for (int k = 0; k < c->npo; k++) r |= (uint32_t)v[c->po[k]] << k;
    return r;
}
static uint32_t next_uni(uint32_t *s, int npi) { uint32_t p = *s & ((1u << npi) - 1u); *s = gstep(*s); return p; }
static uint32_t next_wt(uint32_t *s, int npi) {
    uint32_t p = 0;
    for (int i = 0; i < npi; i++) {
        uint32_t b = 0;
        for (int w = 0; w < 3; w++) { b |= (*s >> 23) & 1u; *s = gstep(*s); }
        p |= b << i;
    }
    return p;
}
typedef uint32_t (*src_fn)(uint32_t *, int);
#define CAP (1u << 23)
static uint32_t patterns_to_full(const cut_t *c, src_fn src, uint32_t seed) {
    uint32_t worst = 0;
    fault_t none = { -1, 0 };
    for (int net = 0; net < c->npi + c->ng; net++)
        for (int v = 0; v <= 1; v++) {
            fault_t f = { net, v };
            uint32_t s = seed, i;
            for (i = 0; i < CAP; i++) {
                uint32_t p = src(&s, c->npi);
                if (eval(c, p, f) != eval(c, p, none)) break;
            }
            if (i == CAP) return CAP;
            if (i + 1 > worst) worst = i + 1;
        }
    return worst;
}
static void build_chain(cut_t *c, int k) {
    memset(c, 0, sizeof *c); c->npi = k;
    int n = 0; for (int i = 1; i < k; i++) n = addg(c, 0, n, i);
    c->po[c->npo++] = (uint16_t)n;
}
static void build_tp(cut_t *c, int k) {
    memset(c, 0, sizeof *c); c->npi = k + 2;
    int h = k / 2, l = 0, r = h;
    for (int i = 1; i < h; i++) l = addg(c, 0, l, i);
    for (int i = h + 1; i < k; i++) r = addg(c, 0, r, i);
    int lc = addg(c, 1, l, k), rc = addg(c, 1, r, k + 1);
    c->po[c->npo++] = (uint16_t)addg(c, 0, lc, rc);
    c->po[c->npo++] = (uint16_t)l; c->po[c->npo++] = (uint16_t)r;
}
int main(void) {
    static const uint32_t seeds[3] = { 0xACE1, 0x2BEEF1, 0x777777 };
    { uint32_t s = 1; uint32_t n = 0; do { s = gstep(s); n++; } while (s != 1 && n < (1u<<25));
      printf("LFSR24 period = %u (expect %u)\n", n, (1u << 24) - 1); }
    printf("patterns to reach 100%% net stuck-at coverage (worst of 3 seeds; cap %u)\n", CAP);
    printf("k   faults  uniform      weighted(7/8)  uniform+TP(2CP+2OP)   2^k\n");
    for (int k = 8; k <= 20; k += 4) {
        cut_t c, t;
        build_chain(&c, k); build_tp(&t, k);
        uint32_t u = 0, w = 0, tp = 0;
        for (int i = 0; i < 3; i++) {
            uint32_t a = patterns_to_full(&c, next_uni, seeds[i]), b = patterns_to_full(&c, next_wt, seeds[i]), d = patterns_to_full(&t, next_uni, seeds[i]);
            if (a > u) u = a;
            if (b > w) w = b;
            if (d > tp) tp = d;
        }
        printf("%-3d %-7d %-12u %-14u %-21u %u\n", k, 2 * (c.npi + c.ng), u, w, tp, 1u << k);
    }
    return 0;
}
