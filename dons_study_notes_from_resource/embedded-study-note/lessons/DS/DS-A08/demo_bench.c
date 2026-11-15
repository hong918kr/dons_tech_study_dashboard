/* demo_bench.c — 시간 측정 (-O2, 이 맥). 조회·범위 스캔을 B+ 트리 / AVL / 정렬 배열로 비교.
 * 측정값의 비율만 볼 것 (절댓값은 이 기계의 부하에 따라 흔들린다).
 * build: cc -std=c11 -O2 demo_bench.c -o db && ./db
 */
#define DEMO_NO_MAIN
#include "demo_measure.c"
#include <time.h>

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}
static int cmp_u32(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

#define RUNS 7
#define LOOKUPS 2000000u
static uint32_t *g_k, *g_sorted, *g_probe;
static volatile uint64_t g_sink;

static void med3(const char *name, double *v, int runs, double per) {
    qsort(v, (size_t)runs, sizeof *v, cmp_d);
    printf("| %s | %.1f | %.1f | %.1f |\n", name, v[runs / 2] / per, v[0] / per, v[runs - 1] / per);
}
int main(void) {
    static bt_t t;
    static uint32_t ks[4096], vs[4096];
    g_k = malloc(sizeof(uint32_t) * 3000000u);
    g_sorted = malloc(sizeof(uint32_t) * 3000000u);
    g_probe = malloc(sizeof(uint32_t) * LOOKUPS);
    if (!g_k || !g_sorted || !g_probe) return 1;
    uint32_t sizes[] = { 10000u, 1000000u };
    for (int s = 0; s < 2; s++) {
        uint32_t n = sizes[s];
        bt_init(&t, g_big, DEMO_PAGES, LEAF_MAX, NODE_MAX);
        av_n = 0;
        uint32_t ar = 0, cnt = 0;
        g_rng = 0x1234ABCDu;
        while (t.nkeys < n) {
            uint32_t k = rnd();
            if (bt_insert(&t, k, k) == BT_NEW) { g_k[cnt++] = k; ar = av_ins(ar, k); }
        }
        memcpy(g_sorted, g_k, n * sizeof(uint32_t));
        qsort(g_sorted, n, sizeof(uint32_t), cmp_u32);
        for (uint32_t i = 0; i < LOOKUPS; i++) g_probe[i] = g_k[rnd() % cnt];
        double bp[RUNS], av[RUNS], sa[RUNS], scan_b[RUNS], scan_a[RUNS];
        for (int r = 0; r < RUNS; r++) {
            uint64_t sink = 0;
            double t0 = now_ns();
            for (uint32_t i = 0; i < LOOKUPS; i++) { uint32_t v = 0; bt_search(&t, g_probe[i], &v); sink += v; }
            bp[r] = now_ns() - t0;
            t0 = now_ns();
            for (uint32_t i = 0; i < LOOKUPS; i++) {
                uint32_t k = g_probe[i], x = ar;
                while (x && av_key[x] != k) x = k < av_key[x] ? av_l[x] : av_r[x];
                sink += x;
            }
            av[r] = now_ns() - t0;
            t0 = now_ns();
            for (uint32_t i = 0; i < LOOKUPS; i++) sink += lb_u32(g_sorted, n, g_probe[i]);
            sa[r] = now_ns() - t0;
            /* 범위 스캔 1000 키 × 20000 회 */
            t0 = now_ns();
            for (uint32_t i = 0; i < 20000u; i++) sink += bt_scan(&t, g_probe[i], 0xFFFFFFFFu, ks, vs, 1000u);
            scan_b[r] = now_ns() - t0;
            t0 = now_ns();
            for (uint32_t i = 0; i < 20000u; i++) {
                /* AVL 중위 순회: lo 이상 첫 노드부터 명시적 스택으로 1000 개 */
                uint32_t stk[64], sp = 0, x = ar, lo = g_probe[i], got = 0;
                while (x) { if (av_key[x] >= lo) { stk[sp++] = x; x = av_l[x]; } else x = av_r[x]; }
                while (sp && got < 1000u) {
                    x = stk[--sp]; sink += av_key[x]; got++;
                    x = av_r[x];
                    while (x) { stk[sp++] = x; x = av_l[x]; }
                }
            }
            scan_a[r] = now_ns() - t0;
            g_sink += sink;
        }
        printf("\nn = %u, %u lookups, %d runs (ns/lookup: median, min, max)\n", n, LOOKUPS, RUNS);
        printf("| structure | median ns | min | max |\n|---|---|---|---|\n");
        med3("B+ tree 4 KB page, cap 510", bp, RUNS, LOOKUPS);
        med3("AVL (index links)", av, RUNS, LOOKUPS);
        med3("sorted array, binary search", sa, RUNS, LOOKUPS);
        printf("\nn = %u, scan 1000 keys, 20000 scans (ns/scan: median, min, max)\n", n);
        printf("| structure | median ns | min | max |\n|---|---|---|---|\n");
        med3("B+ tree leaf chain", scan_b, RUNS, 20000.0);
        med3("AVL in-order", scan_a, RUNS, 20000.0);
    }
    return 0;
}
