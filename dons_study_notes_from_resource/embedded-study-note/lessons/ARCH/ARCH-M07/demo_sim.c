/* ARCH-M07 demo_sim — 축소 트레이스를 LRU 시뮬레이터에 넣어 미스 표 만들기 (CHECK 없음, 관찰용)
 *
 *   표 1  M6 Super 코어 L1D: 128 KB, 128 B 라인 (sysctl 실측값), 8-way (가정 — Apple 미공개), double
 *         n = 128, 256 × ld = n / 패딩 ld × {ijk, ikj, jki, tiled T=16/32/64}
 *   표 2  M6 Performance 코어 L1D: 64 KB (hw.perflevel1.l1dcachesize), 같은 라인/연관도 가정, n = 256
 *   표 3  Cortex-M7 류 D-cache: 16 KB, 4-way, 32 B 라인, float, n = 128 — 3C 분해
 *         (compulsory = 처음 닿는 라인 수, capacity = 같은 용량 완전 연관 LRU 도 놓치는 것, conflict = 나머지;
 *          Hill & Smith 1989 정의, ARCH-M05 와 같다)
 *
 * 시뮬레이터와 트레이스 규칙은 solution.c 와 같다 (ARCH-M04/M05 모델을 축소해 넣었다).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_sim.c -o sim && ./sim
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int imin(int a, int b) { return a < b ? a : b; }

typedef struct {
    uint32_t sets, ways;
    unsigned off_bits, idx_bits;
    uint32_t *tag; uint8_t *valid; uint64_t *last;
    uint64_t tick, accesses, misses;
    uint8_t *seen; uint32_t seen_base, seen_lines; uint64_t first_touch;   /* compulsory 세기 */
} cache_t;

static unsigned ilog2u(uint32_t x) { unsigned r = 0; while (x > 1) { x >>= 1; r++; } return r; }

static void cache_init(cache_t *c, uint32_t cap, uint32_t line, uint32_t ways, uint32_t base, uint32_t span) {
    memset(c, 0, sizeof *c);
    c->sets = cap / (line * ways); c->ways = ways;
    c->off_bits = ilog2u(line); c->idx_bits = ilog2u(c->sets);
    size_t nl = (size_t)c->sets * ways;
    c->tag = calloc(nl, 4); c->valid = calloc(nl, 1); c->last = calloc(nl, 8);
    c->seen_base = base >> c->off_bits; c->seen_lines = (span >> c->off_bits) + 2;
    c->seen = calloc(c->seen_lines, 1);
    if (!c->tag || !c->valid || !c->last || !c->seen) { fprintf(stderr, "oom\n"); exit(2); }
}
static void cache_free(cache_t *c) { free(c->tag); free(c->valid); free(c->last); free(c->seen); }

static void access1(cache_t *c, uint32_t addr) {
    uint32_t la = addr >> c->off_bits;
    uint32_t set = la & (c->sets - 1), tag = (uint32_t)((uint64_t)la >> c->idx_bits);
    size_t b = (size_t)set * c->ways, victim = b;
    uint64_t oldest = UINT64_MAX;
    c->accesses++; c->tick++;
    if (!c->seen[la - c->seen_base]) { c->seen[la - c->seen_base] = 1; c->first_touch++; }
    for (size_t w = b; w < b + c->ways; w++) {
        if (c->valid[w] && c->tag[w] == tag) { c->last[w] = c->tick; return; }
        uint64_t key = c->valid[w] ? c->last[w] : 0;
        if (key < oldest) { oldest = key; victim = w; }
    }
    c->misses++;
    c->valid[victim] = 1; c->tag[victim] = tag; c->last[victim] = c->tick;
}

enum { V_IJK, V_IKJ, V_JKI, V_TILED };
typedef struct { uint32_t a, b, c; int n, ld; uint32_t elem; } lay_t;
#define AD(base, r, col) ((base) + ((uint32_t)(r) * (uint32_t)L->ld + (uint32_t)(col)) * L->elem)

static void trace(cache_t *c, const lay_t *L, int v, int T) {
    int n = L->n;
    if (v == V_IJK) {
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) {
                for (int k = 0; k < n; k++) { access1(c, AD(L->a, i, k)); access1(c, AD(L->b, k, j)); }
                access1(c, AD(L->c, i, j));
            }
    } else if (v == V_IKJ) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) access1(c, AD(L->c, i, j));
            for (int k = 0; k < n; k++) {
                access1(c, AD(L->a, i, k));
                for (int j = 0; j < n; j++) { access1(c, AD(L->b, k, j)); access1(c, AD(L->c, i, j)); access1(c, AD(L->c, i, j)); }
            }
        }
    } else if (v == V_JKI) {
        for (int j = 0; j < n; j++) {
            for (int i = 0; i < n; i++) access1(c, AD(L->c, i, j));
            for (int k = 0; k < n; k++) {
                access1(c, AD(L->b, k, j));
                for (int i = 0; i < n; i++) { access1(c, AD(L->a, i, k)); access1(c, AD(L->c, i, j)); access1(c, AD(L->c, i, j)); }
            }
        }
    } else {
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) access1(c, AD(L->c, i, j));
        for (int ii = 0; ii < n; ii += T)
            for (int jj = 0; jj < n; jj += T)
                for (int kk = 0; kk < n; kk += T) {
                    int ie = imin(ii + T, n), je = imin(jj + T, n), ke = imin(kk + T, n);
                    for (int i = ii; i < ie; i++)
                        for (int k = kk; k < ke; k++) {
                            access1(c, AD(L->a, i, k));
                            for (int j = jj; j < je; j++) { access1(c, AD(L->b, k, j)); access1(c, AD(L->c, i, j)); access1(c, AD(L->c, i, j)); }
                        }
                }
    }
}

typedef struct { uint64_t acc, miss, comp; } res_t;
static res_t run(uint32_t cap, uint32_t line, uint32_t ways, int n, int ld, uint32_t elem, int v, int T) {
    uint32_t bytes = (uint32_t)n * (uint32_t)ld * elem;
    lay_t L = { 0x10000000u, 0x10000000u + bytes, 0x10000000u + 2 * bytes, n, ld, elem };
    cache_t c;
    cache_init(&c, cap, line, ways, L.a, 3 * bytes);
    trace(&c, &L, v, T);
    res_t r = { c.accesses, c.misses, c.first_touch };
    cache_free(&c);
    return r;
}

static int pad_ld(int n, uint32_t elem, uint32_t line) {
    for (int ld = n;; ld++) {
        uint64_t b = (uint64_t)ld * elem;
        if (b % line == 0 && (b / line) % 2 == 1) return ld;
    }
}

static const char *const vn[] = { "ijk", "ikj", "jki", "tiled" };

static void table_m6(uint32_t cap, const int *ns, int nn) {
    printf("%-6s %-5s %-9s %12s %10s %8s %10s\n", "n", "ld", "variant", "accesses", "misses", "rate%", "miss/n^3");
    for (int t = 0; t < nn; t++) {
        int n = ns[t];
        int lds[2] = { n, pad_ld(n, 8, 128) };
        for (int p = 0; p < 2; p++) {
            static const int vs[] = { V_IJK, V_IKJ, V_JKI, V_TILED, V_TILED, V_TILED };
            static const int Ts[] = { 0, 0, 0, 16, 32, 64 };
            for (int q = 0; q < 6; q++) {
                res_t r = run(cap, 128, 8, n, lds[p], 8, vs[q], Ts[q]);
                char name[16];
                if (vs[q] == V_TILED) snprintf(name, sizeof name, "tiled%d", Ts[q]); else snprintf(name, sizeof name, "%s", vn[vs[q]]);
                printf("%-6d %-5d %-9s %12llu %10llu %8.3f %10.4f\n", n, lds[p], name, (unsigned long long)r.acc,
                       (unsigned long long)r.miss, 100.0 * (double)r.miss / (double)r.acc, (double)r.miss / ((double)n * n * n));
            }
        }
    }
}

int main(void) {
    static const int ns1[] = { 128, 256 };
    printf("== table 1: M6 Super L1D 128 KB / 128 B / 8-way (assoc assumed), double ==\n");
    table_m6(128 * 1024, ns1, 2);
    static const int ns2[] = { 256 };
    printf("== table 2: M6 Performance L1D 64 KB / 128 B / 8-way (assoc assumed), double ==\n");
    table_m6(64 * 1024, ns2, 1);

    printf("== table 3: Cortex-M7-like D-cache 16 KB / 32 B / 4-way, float, n=128 — 3C split ==\n");
    printf("%-5s %-9s %10s %10s %8s %10s %9s %9s\n", "ld", "variant", "accesses", "misses", "rate%", "compuls.", "capacity", "conflict");
    int n = 128;
    int lds[2] = { 128, pad_ld(128, 4, 32) };
    for (int p = 0; p < 2; p++) {
        static const int vs[] = { V_IJK, V_IKJ, V_JKI, V_TILED, V_TILED, V_TILED, V_TILED };
        static const int Ts[] = { 0, 0, 0, 8, 16, 32, 64 };
        for (int q = 0; q < 7; q++) {
            res_t r = run(16 * 1024, 32, 4, n, lds[p], 4, vs[q], Ts[q]);
            res_t fa = run(16 * 1024, 32, 512, n, lds[p], 4, vs[q], Ts[q]);   /* 완전 연관 LRU, 같은 용량 */
            char name[16];
            if (vs[q] == V_TILED) snprintf(name, sizeof name, "tiled%d", Ts[q]); else snprintf(name, sizeof name, "%s", vn[vs[q]]);
            long long cap_m = (long long)fa.miss - (long long)r.comp, conf = (long long)r.miss - (long long)fa.miss;
            printf("%-5d %-9s %10llu %10llu %8.3f %10llu %9lld %9lld\n", lds[p], name, (unsigned long long)r.acc,
                   (unsigned long long)r.miss, 100.0 * (double)r.miss / (double)r.acc, (unsigned long long)r.comp, cap_m, conf);
        }
    }
    return 0;
}
