/* ALGO-M09 demo: 잘못된 그리디의 "가장 작은 반례" 찾기와 실패율
 *
 * 1) 선택 규칙 (최대 비겹침): earliest start / shortest first / fewest conflicts(동점 → 끝이 이른 것) /
 *    earliest end / latest start. 격자 0..G 위 구간의 중복조합을 n, G 가 작은 것부터 전수 → 첫 반례 출력.
 *    fewest conflicts 는 작은 n 에서 반례가 없어 전수 범위를 보고하고, 무작위 탐색으로 반례를 찾는다.
 * 2) 채널 배정 규칙: 시작 순서 + 아무 빈 채널(= 레슨 해법), 시작 순서 first-fit, 끝 순서 first-fit,
 *    입력 순서 first-fit. 무작위 실패율 + 순서쌍 전수로 가장 작은 반례.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_counterexamples.c -o demo_ce && ./demo_ce
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint32_t s, e; } ival_t;
static int ov(ival_t a, ival_t b) { return a.s < b.e && b.s < a.e; }

static uint32_t rng = 0x9E3779B9u;
static uint32_t xs(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

#define NMAX 16
static unsigned brute_max(const ival_t *iv, int n) {
    uint32_t conf[NMAX] = { 0 };
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) if (i != j && ov(iv[i], iv[j])) conf[i] |= 1u << j;
    unsigned best = 0;
    for (uint32_t m = 0; m < (1u << n); m++) {
        int ok = 1; unsigned c = 0;
        for (int i = 0; i < n && ok; i++) if (m >> i & 1) { c++; if (conf[i] & m) ok = 0; }
        if (ok && c > best) best = c;
    }
    return best;
}
enum { R_EARLIEST_END, R_EARLIEST_START, R_SHORTEST, R_FEWEST, R_LATEST_START, R_COUNT };
static const char *const rname[R_COUNT] = { "earliest end", "earliest start", "shortest first", "fewest conflicts", "latest start" };
static unsigned greedy(const ival_t *iv, int n, int rule) {
    uint32_t alive = (1u << n) - 1u;
    unsigned cnt = 0;
    while (alive) {
        int best = -1; uint64_t bk = 0;
        for (int i = 0; i < n; i++) {
            if (!(alive >> i & 1)) continue;
            uint64_t k;
            if (rule == R_EARLIEST_END) k = iv[i].e;
            else if (rule == R_EARLIEST_START) k = iv[i].s;
            else if (rule == R_SHORTEST) k = iv[i].e - iv[i].s;
            else if (rule == R_LATEST_START) k = UINT32_MAX - iv[i].s;
            else {
                unsigned c = 0;
                for (int j = 0; j < n; j++) if (j != i && (alive >> j & 1) && ov(iv[i], iv[j])) c++;
                k = ((uint64_t)c << 32) | iv[i].e;
            }
            if (best < 0 || k < bk) { best = i; bk = k; }
        }
        cnt++;
        for (int j = 0; j < n; j++) if (ov(iv[best], iv[j])) alive &= ~(1u << j);
    }
    return cnt;
}
static void print_iv(const ival_t *iv, int n) {
    for (int i = 0; i < n; i++) printf("[%u,%u)%s", iv[i].s, iv[i].e, i + 1 < n ? " " : "");
}

/* 중복조합(비내림 인덱스) 전수: 규칙 rule 이 최적보다 작은 첫 인스턴스. 찾으면 1 */
static long long g_enum;
static int search_rule(int rule, int n, int G, ival_t *found, unsigned *g, unsigned *o) {
    ival_t cand[64]; int nc = 0;
    for (int s = 0; s < G; s++) for (int e = s + 1; e <= G; e++) cand[nc++] = (ival_t){ (uint32_t)s, (uint32_t)e };
    int idx[NMAX];
    for (int i = 0; i < n; i++) idx[i] = 0;
    for (;;) {
        ival_t iv[NMAX];
        for (int i = 0; i < n; i++) iv[i] = cand[idx[i]];
        g_enum++;
        unsigned gg = greedy(iv, n, rule), oo = brute_max(iv, n);
        if (gg < oo) { memcpy(found, iv, sizeof(ival_t) * (size_t)n); *g = gg; *o = oo; return 1; }
        int p = n - 1;
        while (p >= 0 && idx[p] == nc - 1) p--;
        if (p < 0) return 0;
        idx[p]++;
        for (int q = p + 1; q < n; q++) idx[q] = idx[p];
    }
}

/* ---- 채널 배정 ---- */
static unsigned depth(const ival_t *iv, int n) {        /* 깊이 = 어떤 시작점에서의 겹침 수 최대 */
    unsigned best = 0;
    for (int i = 0; i < n; i++) {
        unsigned c = 0;
        for (int j = 0; j < n; j++) c += (iv[j].s <= iv[i].s && iv[i].s < iv[j].e);
        if (c > best) best = c;
    }
    return best;
}
enum { C_START_ANYFREE, C_START_FIRSTFIT, C_END_FIRSTFIT, C_INPUT_FIRSTFIT, C_COUNT };
static const char *const cname[C_COUNT] = { "start order, earliest-free channel (heap)", "start order, first-fit",
                                            "end order, first-fit", "input order, first-fit" };
/* order[] 순서로 처리하며 색칠, 쓴 색 수를 돌려준다 */
static unsigned color_in_order(const ival_t *iv, int n, const int *order, int anyfree_heap) {
    int col[NMAX]; unsigned used = 0;
    uint32_t last_end[NMAX];
    for (int t = 0; t < n; t++) {
        int i = order[t], c = -1;
        if (anyfree_heap) {                                 /* 마지막 끝이 가장 이른 채널이 비었으면 그것 */
            int b = -1;
            for (unsigned k = 0; k < used; k++) if (b < 0 || last_end[k] < last_end[b]) b = (int)k;
            if (b >= 0 && last_end[b] <= iv[i].s) c = b;
        } else {                                            /* 번호가 가장 작은, 지금까지 배정된 것과 안 겹치는 채널 */
            for (unsigned k = 0; k < used && c < 0; k++) {
                int ok = 1;
                for (int u = 0; u < t && ok; u++) if (col[order[u]] == (int)k && ov(iv[i], iv[order[u]])) ok = 0;
                if (ok) c = (int)k;
            }
        }
        if (c < 0) { c = (int)used++; last_end[c] = 0; }
        col[i] = c;
        if (iv[i].e > last_end[c]) last_end[c] = iv[i].e;
    }
    return used;
}
static unsigned color_rule(const ival_t *iv, int n, int rule) {
    int order[NMAX];
    for (int i = 0; i < n; i++) order[i] = i;
    if (rule != C_INPUT_FIRSTFIT)                           /* 삽입 정렬 (안정) */
        for (int i = 1; i < n; i++) {
            int v = order[i], j = i;
            uint32_t kv = rule == C_END_FIRSTFIT ? iv[v].e : iv[v].s;
            while (j > 0 && (rule == C_END_FIRSTFIT ? iv[order[j - 1]].e : iv[order[j - 1]].s) > kv) { order[j] = order[j - 1]; j--; }
            order[j] = v;
        }
    return color_in_order(iv, n, order, rule == C_START_ANYFREE);
}
/* 순서쌍 전수 (입력 순서가 의미 있으므로): 처음으로 depth 보다 많이 쓰는 인스턴스 */
static int search_color(int rule, int n, int G, ival_t *found, unsigned *u, unsigned *d) {
    ival_t cand[64]; int nc = 0;
    for (int s = 0; s < G; s++) for (int e = s + 1; e <= G; e++) cand[nc++] = (ival_t){ (uint32_t)s, (uint32_t)e };
    long long total = 1;
    for (int i = 0; i < n; i++) total *= nc;
    for (long long code = 0; code < total; code++) {
        ival_t iv[NMAX]; long long c = code;
        for (int i = 0; i < n; i++) { iv[i] = cand[c % nc]; c /= nc; }
        unsigned uu = color_rule(iv, n, rule), dd = depth(iv, n);
        if (uu > dd) { memcpy(found, iv, sizeof(ival_t) * (size_t)n); *u = uu; *d = dd; return 1; }
    }
    return 0;
}

int main(void) {
    printf("== 1) selection rules: smallest counterexample (n, then grid G) ==\n");
    for (int r = R_EARLIEST_START; r <= R_SHORTEST; r++) {
        int done = 0;
        for (int n = 1; n <= 6 && !done; n++)
            for (int G = 1; G <= 8 && !done; G++) {
                ival_t f[NMAX]; unsigned g, o;
                if (search_rule(r, n, G, f, &g, &o)) {
                    printf("%-16s n=%d G=%d: ", rname[r], n, G); print_iv(f, n); printf("  greedy=%u optimal=%u\n", g, o);
                    done = 1;
                }
            }
    }
    {   /* fewest conflicts: 전수로는 어디까지 반례가 없나 */
        int found = 0;
        for (int n = 1; n <= 7 && !found; n++) {
            ival_t f[NMAX]; unsigned g, o;
            g_enum = 0;
            if (search_rule(R_FEWEST, n, 7, f, &g, &o)) {
                printf("fewest conflicts n=%d G=7: ", n); print_iv(f, n); printf("  greedy=%u optimal=%u\n", g, o);
                found = 1;
            } else printf("fewest conflicts: no counterexample with n=%d on grid 0..7 (%lld multisets checked)\n", n, g_enum);
        }
    }
    {   /* fewest conflicts: 무작위 탐색 */
        int best_n = 0; ival_t best[NMAX];
        unsigned bg = 0, bo = 0;
        long long tried = 0, hits = 0;
        for (int n = 8; n <= 14; n++) {
            for (int it = 0; it < 200000; it++) {
                ival_t iv[NMAX];
                uint32_t span = (uint32_t)(2 * n);
                for (int i = 0; i < n; i++) { uint32_t s = xs() % span, len = 1 + xs() % 4; iv[i] = (ival_t){ s, s + len }; }
                tried++;
                unsigned g = greedy(iv, n, R_FEWEST), o = brute_max(iv, n);
                if (g < o) {
                    hits++;
                    if (best_n == 0) { best_n = n; memcpy(best, iv, sizeof(ival_t) * (size_t)n); bg = g; bo = o; }
                }
            }
        }
        printf("fewest conflicts random search n=8..14: %lld instances, %lld counterexamples", tried, hits);
        if (best_n) { printf("; first (n=%d): ", best_n); print_iv(best, best_n); printf("  greedy=%u optimal=%u", bg, bo); }
        printf("\n");
    }

    printf("\n== 2) channel assignment rules: random n<=12 (coords<40, len 1..12), channels used vs depth ==\n");
    {
        long long bad[C_COUNT] = { 0 }; unsigned extra[C_COUNT] = { 0 };
        int cases = 200000;
        rng = 12345u;
        for (int it = 0; it < cases; it++) {
            int n = 1 + (int)(xs() % 12);
            ival_t iv[NMAX];
            for (int i = 0; i < n; i++) { uint32_t s = xs() % 40, len = 1 + xs() % 12; iv[i] = (ival_t){ s, s + len }; }
            unsigned d = depth(iv, n);
            for (int r = 0; r < C_COUNT; r++) {
                unsigned u = color_rule(iv, n, r);
                if (u > d) { bad[r]++; if (u - d > extra[r]) extra[r] = u - d; }
            }
        }
        for (int r = 0; r < C_COUNT; r++)
            printf("%-42s uses more than depth in %6lld / %d (max extra %u)\n", cname[r], bad[r], cases, extra[r]);
    }
    for (int r = C_END_FIRSTFIT; r <= C_INPUT_FIRSTFIT; r++) {
        int done = 0;
        for (int n = 2; n <= 5 && !done; n++)
            for (int G = 2; G <= 6 && !done; G++) {
                ival_t f[NMAX]; unsigned u, d;
                if (search_color(r, n, G, f, &u, &d)) {
                    printf("smallest counterexample, %s: n=%d G=%d: ", cname[r], n, G); print_iv(f, n);
                    printf("  used=%u depth=%u\n", u, d);
                    done = 1;
                }
            }
        if (!done) printf("%s: no counterexample with n<=5, G<=6\n", cname[r]);
    }
    return 0;
}
