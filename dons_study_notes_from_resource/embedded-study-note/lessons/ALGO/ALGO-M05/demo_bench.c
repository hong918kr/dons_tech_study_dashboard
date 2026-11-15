/* ALGO-M05 demo_bench — tie-break 방식별 Kahn 비용
 *   fifo : 준비 큐를 FIFO 로 (O(V+E), 결정적이지만 "가장 작은 id 먼저" 는 아님)
 *   heap : 준비 집합을 min-heap 으로 (O((V+E) log V), 사전순 최소)
 *   scan : 매 단계 0..n-1 선형 스캔 (O(V^2 + E), 사전순 최소)
 * 그래프: random DAG (E = 4V), wide (간선 0: 처음부터 V 개 모두 준비), chain (id 역순 사슬: 매번 준비 1개)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAXV 20000
#define MAXE 80000

static uint32_t n, m, off[MAXV + 1], adj[MAXE], eu[MAXE], ev[MAXE];
static uint32_t indeg[MAXV], q[MAXV], order[MAXV], order2[MAXV];
static uint8_t done_[MAXV];

static uint32_t g_rng = 0x2545F491u;
static uint32_t xs(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

static void build(void) {
    static uint32_t cur[MAXV];
    memset(off, 0, sizeof(uint32_t) * (n + 1));
    for (uint32_t i = 0; i < m; i++) off[eu[i] + 1]++;
    for (uint32_t u = 0; u < n; u++) off[u + 1] += off[u];
    for (uint32_t u = 0; u < n; u++) cur[u] = off[u];
    for (uint32_t i = 0; i < m; i++) adj[cur[eu[i]]++] = ev[i];
}

static void init_indeg(void) {
    memset(indeg, 0, sizeof(uint32_t) * n);
    for (uint32_t i = 0; i < m; i++) indeg[adj[i]]++;
}

static uint32_t kahn_fifo(void) {
    uint32_t h = 0, t = 0, k = 0;
    init_indeg();
    for (uint32_t u = 0; u < n; u++) if (!indeg[u]) q[t++] = u;
    while (h < t) {
        uint32_t u = q[h++]; order[k++] = u;
        for (uint32_t i = off[u]; i < off[u + 1]; i++) if (--indeg[adj[i]] == 0) q[t++] = adj[i];
    }
    return k;
}

static uint32_t hl;
static void hpush(uint32_t x) { uint32_t i = hl++; while (i && q[(i - 1) / 2] > x) { q[i] = q[(i - 1) / 2]; i = (i - 1) / 2; } q[i] = x; }
static uint32_t hpop(void) {
    uint32_t top = q[0], x = q[--hl], i = 0;
    for (;;) { uint32_t c = 2 * i + 1; if (c >= hl) break; if (c + 1 < hl && q[c + 1] < q[c]) c++; if (x <= q[c]) break; q[i] = q[c]; i = c; }
    if (hl) q[i] = x;
    return top;
}
static uint32_t kahn_heap(void) {
    uint32_t k = 0; hl = 0;
    init_indeg();
    for (uint32_t u = 0; u < n; u++) if (!indeg[u]) hpush(u);
    while (hl) {
        uint32_t u = hpop(); order[k++] = u;
        for (uint32_t i = off[u]; i < off[u + 1]; i++) if (--indeg[adj[i]] == 0) hpush(adj[i]);
    }
    return k;
}

static uint32_t kahn_scan(void) {
    init_indeg(); memset(done_, 0, n);
    for (uint32_t k = 0; k < n; k++) {
        uint32_t u = 0;
        while (u < n && (done_[u] || indeg[u])) u++;
        if (u == n) return k;
        done_[u] = 1; order[k] = u;
        for (uint32_t i = off[u]; i < off[u + 1]; i++) indeg[adj[i]]--;
    }
    return n;
}

static double now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e9 + ts.tv_nsec; }

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static double time_it(uint32_t (*f)(void), int reps, uint64_t *sink) {
    double t[9];
    f();                                                    /* warm-up */
    for (int r = 0; r < 9; r++) {
        double t0 = now_ns();
        for (int i = 0; i < reps; i++) { *sink += f(); *sink += order[n / 2]; }
        t[r] = (now_ns() - t0) / reps;
    }
    qsort(t, 9, sizeof t[0], cmp_d);
    return t[4];                                            /* 중앙값 */
}

static void make(const char *shape, uint32_t nn) {
    n = nn; m = 0;
    if (!strcmp(shape, "random")) {
        static uint32_t perm[MAXV];
        for (uint32_t i = 0; i < n; i++) perm[i] = i;
        for (uint32_t i = n; i > 1; i--) { uint32_t j = xs() % i, t = perm[i - 1]; perm[i - 1] = perm[j]; perm[j] = t; }
        m = 4 * n;
        for (uint32_t i = 0; i < m; i++) {
            uint32_t a = xs() % n, b = xs() % n;
            if (a == b) b = (a + 1) % n;
            if (a > b) { uint32_t t = a; a = b; b = t; }
            eu[i] = perm[a]; ev[i] = perm[b];
        }
    } else if (!strcmp(shape, "chain")) {
        for (uint32_t i = 0; i + 1 < n; i++) { eu[m] = n - 1 - i; ev[m] = n - 2 - i; m++; }
    }                                                        /* wide: 간선 없음 */
    build();
}

int main(void) {
    static const char *shapes[] = { "random", "wide", "chain" };
    static const uint32_t sizes[] = { 100, 1000, 10000 };
    uint64_t sink = 0;
    printf("shape   V      E      fifo ns   heap ns   scan ns   heap/fifo  scan/heap  heap==scan\n");
    for (int s = 0; s < 3; s++)
        for (int z = 0; z < 3; z++) {
            make(shapes[s], sizes[z]);
            int reps = sizes[z] <= 100 ? 2000 : sizes[z] <= 1000 ? 200 : 4;
            double tf = time_it(kahn_fifo, reps, &sink);
            double th = time_it(kahn_heap, reps, &sink);
            memcpy(order2, order, sizeof(uint32_t) * n);
            double tsn = time_it(kahn_scan, reps, &sink);
            int same = memcmp(order, order2, sizeof(uint32_t) * n) == 0;
            printf("%-6s %6u %6u %9.0f %9.0f %9.0f %9.2f %10.1f   %s\n", shapes[s], n, m, tf, th, tsn, th / tf, tsn / th,
                   same ? "yes" : "NO");
        }
    printf("sink=%llu\n", (unsigned long long)sink);
    return 0;
}
