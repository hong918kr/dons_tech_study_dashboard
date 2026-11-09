/* DS-A07 demo_bench.c — 조회/삽입 시간 (Apple M6, -O2, 틱 41.7 ns 양자화. 비율만 믿을 것)
 * 빌드: cc -std=c11 -O2 demo_bench.c -o demo_bench && ./demo_bench
 * 같은 메모리(슬롯 2^k 개)·같은 키에서 cuckoo(부하 0.45, T0/T1 각 2^(k-1)) vs 선형 탐사(부하 0.45, 0.90).
 * 작업량(탐사 수 최대)과 시간(ns/op, 5회 중앙값과 범위)을 함께 낸다. */
#include <mach/mach_time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CK_EMPTY 0xFFFFFFFFu
#define MAX_SLOTS (1u << 21)
typedef struct { uint32_t key, val; } slot_t;
static slot_t g_tab[MAX_SLOTS];
static uint32_t g_keys[MAX_SLOTS], g_miss[MAX_SLOTS];

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static inline uint32_t mix(uint32_t seed, uint32_t key) {
    uint32_t h = key ^ seed;
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}
static double g_ns_per_tick;
static inline uint64_t ticks(void) { return mach_absolute_time(); }

/* ---- cuckoo (경로 우선, 예산 넉넉) ---- */
static uint32_t ck_cap, ck_s0, ck_s1;
static uint32_t g_path[1u << 20];
static inline uint32_t ck_home(uint32_t w, uint32_t k) { return (w ? ck_cap : 0u) + (mix(w ? ck_s1 : ck_s0, k) & (ck_cap - 1u)); }
static inline uint32_t ck_other(uint32_t s, uint32_t k) { return ck_home(s >= ck_cap ? 0u : 1u, k); }
static bool ck_walk(uint32_t start, uint32_t *len) {
    uint32_t s = start, n = 0;
    for (uint32_t step = 0; step < (1u << 20); step++) {
        g_path[n++] = s;
        if (g_tab[s].key == CK_EMPTY) { *len = n; return true; }
        s = ck_other(s, g_tab[s].key);
    }
    return false;
}
static bool ck_put(uint32_t k, uint32_t *kicks) {
    uint32_t a = ck_home(0, k), b = ck_home(1, k), len;
    *kicks = 0;
    if (g_tab[a].key == CK_EMPTY) { g_tab[a].key = k; g_tab[a].val = k; return true; }
    if (g_tab[b].key == CK_EMPTY) { g_tab[b].key = k; g_tab[b].val = k; return true; }
    if (!ck_walk(a, &len) && !ck_walk(b, &len)) return false;
    for (uint32_t i = len - 1; i > 0; i--) g_tab[g_path[i]] = g_tab[g_path[i - 1]];
    g_tab[g_path[0]].key = k; g_tab[g_path[0]].val = k;
    *kicks = len - 1;
    return true;
}
static inline bool ck_get(uint32_t k, uint32_t *v) {
    uint32_t a = ck_home(0, k);
    if (g_tab[a].key == k) { *v = g_tab[a].val; return true; }
    uint32_t b = ck_home(1, k);
    if (g_tab[b].key == k) { *v = g_tab[b].val; return true; }
    return false;
}

/* ---- 선형 탐사 (DS-B11 형) ---- */
static uint32_t lp_mask;
static bool lp_put(uint32_t k, uint32_t *probes) {
    uint32_t i = mix(0x1234567u, k) & lp_mask, p = 1;
    while (g_tab[i].key != CK_EMPTY) { i = (i + 1) & lp_mask; p++; }
    g_tab[i].key = k; g_tab[i].val = k;
    *probes = p;
    return true;
}
static inline bool lp_get(uint32_t k, uint32_t *v, uint32_t *probes) {
    uint32_t i = mix(0x1234567u, k) & lp_mask, p = 1;
    for (;;) {
        if (g_tab[i].key == k) { *v = g_tab[i].val; *probes = p; return true; }
        if (g_tab[i].key == CK_EMPTY) { *probes = p; return false; }
        i = (i + 1) & lp_mask; p++;
    }
}

static void clear(uint32_t slots) { for (uint32_t i = 0; i < slots; i++) { g_tab[i].key = CK_EMPTY; g_tab[i].val = 0; } }
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

static volatile uint32_t g_sink;
#define REP 5
#define LOOKUPS 4000000u

static void report(const char *name, double *v, const char *extra) {
    qsort(v, REP, sizeof(double), cmp_d);
    printf("  %-28s %7.2f ns  (min %.2f, max %.2f)  %s\n", name, v[REP / 2], v[0], v[REP - 1], extra);
}

static void run(uint32_t slots) {
    uint32_t rng = 0xABCDEF01u, n45 = (uint32_t)(0.45 * slots), n90 = (uint32_t)(0.90 * slots);
    for (uint32_t i = 0; i < n90; i++) g_keys[i] = xs32(&rng);
    uint32_t mr = 0x10203040u;
    for (uint32_t i = 0; i < LOOKUPS; i++) g_miss[i % MAX_SLOTS] = xs32(&mr);
    printf("\nslots=%u (%.1f KiB): n45=%u, n90=%u\n", slots, slots * 8 / 1024.0, n45, n90);

    /* 1) cuckoo 0.45 */
    double hit[REP], miss[REP];
    ck_cap = slots / 2; ck_s0 = 0x9E3779B9u; ck_s1 = 0x7F4A7C15u; clear(slots);
    uint32_t kicks, fails = 0, maxk = 0;
    for (uint32_t i = 0; i < n45; i++) { if (!ck_put(g_keys[i], &kicks)) fails++; else if (kicks > maxk) maxk = kicks; }
    for (int r = 0; r < REP; r++) {
        uint64_t t0 = ticks(); uint32_t acc = 0, v = 0;
        for (uint32_t i = 0; i < LOOKUPS; i++) { (void)ck_get(g_keys[i % n45], &v); acc += v; }
        uint64_t t1 = ticks(); g_sink = acc;
        for (uint32_t i = 0; i < LOOKUPS; i++) { if (ck_get(g_miss[i % MAX_SLOTS], &v)) acc++; }
        uint64_t t2 = ticks(); g_sink = acc;
        hit[r] = (double)(t1 - t0) * g_ns_per_tick / LOOKUPS;
        miss[r] = (double)(t2 - t1) * g_ns_per_tick / LOOKUPS;
    }
    char ex[96];
    snprintf(ex, sizeof ex, "max_probes=2  put_fail=%u max_kicks=%u", fails, maxk);
    report("cuckoo load 0.45  hit", hit, ex);
    report("cuckoo load 0.45  miss", miss, "max_probes=2");

    /* 2) linear 0.45, 0.90 */
    for (int which = 0; which < 2; which++) {
        uint32_t n = which ? n90 : n45, maxp = 0, maxm = 0, p = 0;
        lp_mask = slots - 1u; clear(slots);
        for (uint32_t i = 0; i < n; i++) { lp_put(g_keys[i], &p); if (p > maxp) maxp = p; }
        uint32_t v = 0;
        for (uint32_t i = 0; i < 1u << 20; i++) { lp_get(g_miss[i], &v, &p); if (p > maxm) maxm = p; }
        for (int r = 0; r < REP; r++) {
            uint64_t t0 = ticks(); uint32_t acc = 0;
            for (uint32_t i = 0; i < LOOKUPS; i++) { lp_get(g_keys[i % n], &v, &p); acc += v; }
            uint64_t t1 = ticks(); g_sink = acc;
            for (uint32_t i = 0; i < LOOKUPS; i++) { if (lp_get(g_miss[i % MAX_SLOTS], &v, &p)) acc++; }
            uint64_t t2 = ticks(); g_sink = acc;
            hit[r] = (double)(t1 - t0) * g_ns_per_tick / LOOKUPS;
            miss[r] = (double)(t2 - t1) * g_ns_per_tick / LOOKUPS;
        }
        char nm[40];
        snprintf(ex, sizeof ex, "max_probes(insert)=%u", maxp);
        snprintf(nm, sizeof nm, "linear load %.2f hit", which ? 0.90 : 0.45);
        report(nm, hit, ex);
        snprintf(ex, sizeof ex, "max_probes(miss, 1M keys)=%u", maxm);
        snprintf(nm, sizeof nm, "linear load %.2f miss", which ? 0.90 : 0.45);
        report(nm, miss, ex);
    }
}

/* 삽입 1회의 시간 분포 (틱, 양자화 있음) + 작업량. 표를 0.45 까지 채우는 과정의 마지막 20%. */
static void insert_tail(uint32_t slots) {
    uint32_t rng = 0xCAFED00Du, n = (uint32_t)(0.45 * slots), first = n - n / 5;
    static uint32_t cost[REP][1u << 20];
    double p50[REP], p99[REP], p999[REP], mx[REP];
    uint32_t maxk = 0;
    for (int r = 0; r < REP; r++) {
        ck_cap = slots / 2; ck_s0 = 0x9E3779B9u + (uint32_t)r; ck_s1 = 0x7F4A7C15u; clear(slots);
        uint32_t kicks, c = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t k = xs32(&rng);
            uint64_t t0 = ticks();
            (void)ck_put(k, &kicks);
            uint64_t t1 = ticks();
            if (i >= first && c < (1u << 20)) cost[r][c++] = (uint32_t)(t1 - t0);
            if (kicks > maxk) maxk = kicks;
        }
        uint32_t *v = cost[r];
        for (uint32_t i = 1; i < c; i++) { uint32_t x = v[i]; int j = (int)i - 1; while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; } v[j + 1] = x; }
        p50[r] = v[c / 2] * g_ns_per_tick; p99[r] = v[(uint32_t)(c * 0.99)] * g_ns_per_tick;
        p999[r] = v[(uint32_t)(c * 0.999)] * g_ns_per_tick; mx[r] = v[c - 1] * g_ns_per_tick;
    }
    qsort(p50, REP, sizeof(double), cmp_d); qsort(p99, REP, sizeof(double), cmp_d);
    qsort(p999, REP, sizeof(double), cmp_d); qsort(mx, REP, sizeof(double), cmp_d);
    printf("\ninsert ticks→ns, slots=%u, last 20%% of fill to 0.45 (5 runs; median of per-run percentile; max_kicks=%u)\n", slots, maxk);
    printf("  p50 %.1f ns (%.1f..%.1f)  p99 %.1f ns (%.1f..%.1f)  p99.9 %.1f ns (%.1f..%.1f)  max %.1f ns (%.1f..%.1f)\n",
           p50[2], p50[0], p50[4], p99[2], p99[0], p99[4], p999[2], p999[0], p999[4], mx[2], mx[0], mx[4]);
}

int main(void) {
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    g_ns_per_tick = (double)tb.numer / tb.denom;
    printf("timebase: %u/%u -> %.3f ns per tick (coarse; ~41.7 ns granularity expected on this Mac)\n",
           tb.numer, tb.denom, g_ns_per_tick);
    run(1u << 12);
    run(1u << 16);
    run(1u << 20);
    insert_tail(1u << 14);
    return 0;
}
