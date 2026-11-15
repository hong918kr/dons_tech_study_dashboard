/* OS-B11 추가 연습 — 한 번에 검증
 *   연습 1  Clock 도 Belady 이상을 보인다 (Belady 문자열 + 무작위 탐색, FIFO 와 비교)
 *   연습 2  Mattson 한 번 훑기: LRU 스택 거리로 모든 프레임 수의 hit 를 한 번에 (= 프레임별 시뮬레이션)
 *   연습 3  OPT 의 동점 규칙: OSTEP 식(가장 최근 적재) 은 미스 수는 같지만 포함 관계를 깬다
 *   연습 4  dirty bit 를 보는 Clock (enhanced second chance): 미스 + 쓰기 되돌림(writeback) 비용
 *
 * 본문 solution.c 와 독립 (작은 시뮬레이터를 다시 담았다). 시간 측정 없음, 고정 시드.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define MAXF   128
#define MAXP   128
#define MAXN   10000

enum { FIFO, LRU, CLOCK, OPT, OPT_OSTEP, ECLOCK };

typedef struct {
    int pol, nf, used, hand;
    int page[MAXF];
    long loaded[MAXF], last[MAXF], nextuse[MAXF];
    unsigned char use[MAXF], dirty[MAXF];
} cache_t;

typedef struct { long hits, misses, writebacks; } res_t;

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static int victim(cache_t *c) {
    int v = 0;
    switch (c->pol) {
    case FIFO:  for (int f = 1; f < c->used; f++) if (c->loaded[f] < c->loaded[v]) v = f; return v;
    case LRU:   for (int f = 1; f < c->used; f++) if (c->last[f] < c->last[v]) v = f; return v;
    case OPT:   /* 본문과 같은 규칙: 다음 사용이 가장 먼 것, 동점이면 마지막 참조가 오래된 것 */
        for (int f = 1; f < c->used; f++)
            if (c->nextuse[f] > c->nextuse[v] || (c->nextuse[f] == c->nextuse[v] && c->last[f] < c->last[v])) v = f;
        return v;
    case OPT_OSTEP: /* paging-policy.py 규칙: `>=` 로 비교 → 동점이면 가장 최근에 적재된 것 */
        for (int f = 1; f < c->used; f++)
            if (c->nextuse[f] > c->nextuse[v] || (c->nextuse[f] == c->nextuse[v] && c->loaded[f] > c->loaded[v])) v = f;
        return v;
    case CLOCK:
        for (;;) {
            if (!c->use[c->hand]) { v = c->hand; c->hand = (v + 1) % c->nf; return v; }
            c->use[c->hand] = 0; c->hand = (c->hand + 1) % c->nf;
        }
    case ECLOCK:
        /* 1) 한 바퀴: (use 0, dirty 0) 찾기 — 아무것도 안 바꿈
         * 2) 한 바퀴: (use 0, dirty 1) 찾기 — 지나가며 use 를 0 으로
         * 3) 못 찾았으면 이제 use 가 전부 0 → 1) 부터 다시 (반드시 끝난다) */
        for (;;) {
            for (int k = 0; k < c->nf; k++) {
                int f = (c->hand + k) % c->nf;
                if (!c->use[f] && !c->dirty[f]) { c->hand = (f + 1) % c->nf; return f; }
            }
            for (int k = 0; k < c->nf; k++) {
                int f = (c->hand + k) % c->nf;
                if (!c->use[f] && c->dirty[f]) { c->hand = (f + 1) % c->nf; return f; }
                c->use[f] = 0;
            }
        }
    default: return 0;
    }
}

/* wr 가 NULL 이면 전부 읽기. mask 가 NULL 이 아니면 각 시각의 상주 집합 (페이지 < 64). */
static res_t run(int pol, const int *r, const bool *wr, long n, int nf, uint64_t *mask) {
    static long next[MAXN], seen[MAXP];
    for (int p = 0; p < MAXP; p++) seen[p] = n;
    for (long t = n - 1; t >= 0; t--) { next[t] = seen[r[t]]; seen[r[t]] = t; }
    cache_t c;
    memset(&c, 0, sizeof c);
    c.pol = pol; c.nf = nf;
    res_t z = { 0, 0, 0 };
    for (long t = 0; t < n; t++) {
        int f = -1;
        for (int i = 0; i < c.used; i++) if (c.page[i] == r[t]) { f = i; break; }
        if (f >= 0) z.hits++;
        else {
            z.misses++;
            if (c.used < nf) f = c.used++;
            else { f = victim(&c); if (c.dirty[f]) z.writebacks++; }
            c.page[f] = r[t]; c.loaded[f] = t; c.dirty[f] = 0;
        }
        c.last[f] = t; c.nextuse[f] = next[t]; c.use[f] = 1;
        if (wr && wr[t]) c.dirty[f] = 1;
        if (mask) { uint64_t m = 0; for (int i = 0; i < c.used; i++) m |= 1ULL << c.page[i]; mask[t] = m; }
    }
    return z;
}
static int first_anomaly(int pol, const int *r, long n, int maxf) {
    long prev = -1;
    for (int f = 1; f <= maxf; f++) {
        long m = run(pol, r, NULL, n, f, NULL).misses;
        if (f > 1 && m > prev) return f - 1;
        prev = m;
    }
    return 0;
}
static int inclusion_violations(int pol, const int *r, long n, int maxf) {   /* 깨지는 (f, t) 쌍의 수 */
    static uint64_t a[MAXN], b[MAXN];
    int bad = 0;
    for (int f = 1; f < maxf; f++) {
        run(pol, r, NULL, n, f, a);
        run(pol, r, NULL, n, f + 1, b);
        for (long t = 0; t < n; t++) if (a[t] & ~b[t]) bad++;
    }
    return bad;
}

/* ---------- 연습 2: Mattson 스택 거리 — 한 번 훑어서 모든 f 의 LRU hit ---------- */
/* hits[f] (f = 1..maxf) = f 프레임 LRU 의 hit 수 */
static void lru_all_sizes(const int *r, long n, int maxf, long *hits) {
    int stack[MAXP]; int depth = 0;                  /* stack[0] = 가장 최근 (MRU) */
    long dist[MAXP + 1] = { 0 };                     /* dist[d] = 스택 거리 d 인 참조 수 */
    for (long t = 0; t < n; t++) {
        int pos = -1;
        for (int i = 0; i < depth; i++) if (stack[i] == r[t]) { pos = i; break; }
        if (pos >= 0) dist[pos + 1]++;
        else pos = depth++;                          /* 처음 보는 페이지: 거리 ∞ (강제 미스) */
        memmove(&stack[1], &stack[0], (size_t)pos * sizeof stack[0]);
        stack[0] = r[t];
    }
    long acc = 0;
    for (int f = 1; f <= maxf; f++) { acc += (f <= MAXP) ? dist[f] : 0; hits[f] = acc; }
}

int main(void) {
    /* ---------------- 연습 1 ---------------- */
    {
        const int bel[] = { 1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5 };
        long cf[7];
        for (int f = 1; f <= 6; f++) cf[f] = run(CLOCK, bel, NULL, 12, f, NULL).misses;
        CHECK(cf[1] == 12 && cf[2] == 12 && cf[3] == 9 && cf[4] == 10 && cf[5] == 5 && cf[6] == 5);
        CHECK(first_anomaly(CLOCK, bel, 12, 6) == 3);
        uint32_t seed = 2026u;
        int r[30], af = 0, ac = 0;
        for (int trial = 0; trial < 5000; trial++) {
            for (int i = 0; i < 30; i++) r[i] = (int)(xs32(&seed) % 7);
            if (first_anomaly(FIFO, r, 30, 7))  af++;
            if (first_anomaly(CLOCK, r, 30, 7)) ac++;
        }
        printf("ex1: Belady cases in 5000 random strings (7 pages, 30 refs): FIFO=%d CLOCK=%d\n", af, ac);
        CHECK(af > 0 && ac > 0);
    }
    /* ---------------- 연습 2 ---------------- */
    {
        static int w[MAXN];
        uint32_t seed = 7u;
        for (long i = 0; i < MAXN; i++)                          /* 80-20, 100 페이지 */
            w[i] = (xs32(&seed) % 100 < 80) ? (int)(xs32(&seed) % 20) : 20 + (int)(xs32(&seed) % 80);
        long hits[101];
        lru_all_sizes(w, MAXN, 100, hits);
        bool same = true;
        for (int f = 1; f <= 100; f++) if (hits[f] != run(LRU, w, NULL, MAXN, f, NULL).hits) same = false;
        CHECK(same);
        bool mono = true;
        for (int f = 2; f <= 100; f++) if (hits[f] < hits[f - 1]) mono = false;
        CHECK(mono);
        CHECK(hits[100] == MAXN - 100);                           /* 100 프레임 → 강제 미스 100 개만 */
        printf("ex2: one-pass LRU hits f=10/30/50: %ld %ld %ld (same as per-size simulation)\n",
               hits[10], hits[30], hits[50]);
    }
    /* ---------------- 연습 3 ---------------- */
    {
        uint32_t seed = 2026u;
        int r[30], incl_mine = 0, incl_ostep = 0, miss_diff = 0, strings_broken = 0;
        for (int trial = 0; trial < 2000; trial++) {
            for (int i = 0; i < 30; i++) r[i] = (int)(xs32(&seed) % 7);
            for (int f = 1; f <= 7; f++)
                if (run(OPT, r, NULL, 30, f, NULL).misses != run(OPT_OSTEP, r, NULL, 30, f, NULL).misses) miss_diff++;
            incl_mine += inclusion_violations(OPT, r, 30, 7);
            int v = inclusion_violations(OPT_OSTEP, r, 30, 7);
            incl_ostep += v;
            if (v) strings_broken++;
        }
        printf("ex3: OPT tie rule — miss diffs=%d, inclusion violations mine=%d ostep-rule=%d (%d/2000 strings)\n",
               miss_diff, incl_mine, incl_ostep, strings_broken);
        CHECK(miss_diff == 0);                 /* 다시 안 쓰일 페이지 중 누굴 버려도 미스 수는 같다 */
        CHECK(incl_mine == 0);                 /* 크기와 무관한 우선순위 → 스택 알고리즘 */
        CHECK(incl_ostep > 0);                 /* 적재 시각은 프레임 수에 따라 다르다 → 포함 관계가 깨질 수 있다 */
    }
    /* ---------------- 연습 4 ---------------- */
    {
        /* 손 계산: W1 R2 R3 R4 R1, 프레임 3. Clock 은 dirty 인 1 을 내쫓고(writeback 1) 1 을 다시 미스.
         * enhanced 는 깨끗한 2 를 내쫓는다 → R1 hit, writeback 0. */
        const int r[] = { 1, 2, 3, 4, 1 };
        const bool wr[] = { true, false, false, false, false };
        res_t a = run(CLOCK, r, wr, 5, 3, NULL), b = run(ECLOCK, r, wr, 5, 3, NULL);
        CHECK(a.misses == 5 && a.writebacks == 1);
        CHECK(b.misses == 4 && b.writebacks == 0);

        static int w[MAXN]; static bool ww[MAXN];
        uint32_t seed = 11u;
        for (long i = 0; i < MAXN; i++) {                          /* 80-20 읽기 + 쓰기 30% */
            w[i] = (xs32(&seed) % 100 < 80) ? (int)(xs32(&seed) % 20) : 20 + (int)(xs32(&seed) % 80);
            ww[i] = xs32(&seed) % 100 < 30;
        }
        printf("ex4: 80-20 + 30%% writes      misses  writebacks  cost(miss+wb)\n");
        long cost[2][3];
        for (int f = 10; f <= 50; f += 20) {
            res_t c1 = run(CLOCK, w, ww, MAXN, f, NULL), c2 = run(ECLOCK, w, ww, MAXN, f, NULL);
            printf("     f=%2d  CLOCK          %6ld  %10ld  %13ld\n", f, c1.misses, c1.writebacks, c1.misses + c1.writebacks);
            printf("     f=%2d  enhanced CLOCK %6ld  %10ld  %13ld\n", f, c2.misses, c2.writebacks, c2.misses + c2.writebacks);
            cost[0][(f - 10) / 20] = c1.misses + c1.writebacks;
            cost[1][(f - 10) / 20] = c2.misses + c2.writebacks;
        }
        CHECK(cost[1][0] < cost[0][0] && cost[1][1] < cost[0][1]);   /* 고정 시드 11: f=10,30 에서는 이득 */
        CHECK(cost[1][2] > cost[0][2]);        /* f=50 에서는 손해 — 깨끗한 hot 페이지를 버린 대가 (만능 아님) */
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
