/* DS-A07 demo_load.c — 부하율 한계와 축출 사슬 분포 (작업량 계측: 결정적, 시계 안 씀)
 * 빌드: cc -std=c11 -O2 demo_load.c -o demo_load && ./demo_load
 * 표 1: 첫 실패 시점의 load (cap 별, 시드 다수)     표 2: 경로 예산 K 별 첫 실패 load
 * 표 3: 그 load 까지 실패 없이 갈 확률              표 4: load 구간별 축출 횟수 분포 (경로 우선 vs 고전)
 * 표 5: 해시 품질과 키 분포가 한계를 바꾸는가       표 6: 불가능한 삽입 하나를 판정하는 비용 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CK_EMPTY 0xFFFFFFFFu
#define MAX_PATH (1u << 18)
#define MAX_CAP (1u << 16)

typedef struct { uint32_t key, val; } slot_t;
typedef uint32_t (*hash_fn)(uint32_t seed, uint32_t key);
typedef struct { slot_t *s; uint32_t cap, mask, n, seed[2], limit; hash_fn hash; } ck_t;

static slot_t g_buf[2 * MAX_CAP];
static uint32_t g_path[MAX_PATH + 1];
static uint32_t g_shift;
static uint64_t g_steps;   /* walk 가 읽은 슬롯 수 (작업량) */

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static uint32_t h_mix(uint32_t seed, uint32_t key) {
    uint32_t h = key ^ seed;
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}
/* 곱셈 시프트: 2-universal 정도의 약한 해시. 곱수는 시드(홀수화), 상위 비트를 쓴다. */
static uint32_t h_mulshift(uint32_t seed, uint32_t key) { return ((seed | 1u) * key) >> g_shift; }
/* 시드 없는 비트 잘라 쓰기: 시드 0 → 하위 비트, 시드 1 → 그 위 비트 (펌웨어에서 흔한 "그냥 비트 쓰자") */
static uint32_t h_bits(uint32_t seed, uint32_t key) { return seed & 1u ? key >> (32 - g_shift) : key; }

static void ck_setup(ck_t *t, uint32_t cap, hash_fn h, uint32_t limit, uint32_t s0, uint32_t s1) {
    t->s = g_buf; t->cap = cap; t->mask = cap - 1; t->n = 0; t->limit = limit; t->hash = h;
    t->seed[0] = s0; t->seed[1] = s1;
    for (uint32_t i = 0; i < 2 * cap; i++) { t->s[i].key = CK_EMPTY; t->s[i].val = 0; }
}
static inline uint32_t home(const ck_t *t, uint32_t w, uint32_t k) { return (w ? t->cap : 0) + (t->hash(t->seed[w], k) & t->mask); }
static inline uint32_t other(const ck_t *t, uint32_t s, uint32_t k) { return home(t, s >= t->cap ? 0 : 1, k); }

typedef enum { W_FOUND, W_CYCLE, W_LONG } walk_t;
static walk_t walk(const ck_t *t, uint32_t start, uint32_t *len) {
    uint32_t s = start, tort = start, pow = 1, lam = 0, n = 0;
    for (;;) {
        if (n > t->limit) { *len = n; return W_LONG; }
        g_path[n++] = s;
        g_steps++;
        if (t->s[s].key == CK_EMPTY) { *len = n; return W_FOUND; }
        s = other(t, s, t->s[s].key);
        if (s == tort) { *len = n; return W_CYCLE; }
        if (++lam == pow) { tort = s; pow <<= 1; lam = 0; }
    }
}
/* 경로 우선 삽입. 반환: 축출 횟수(≥0) 또는 실패 -1. 키는 서로 다르다고 가정. */
static int put_path(ck_t *t, uint32_t k) {
    uint32_t a = home(t, 0, k), b = home(t, 1, k), len;
    if (t->s[a].key == CK_EMPTY) { t->s[a].key = k; t->n++; return 0; }
    if (t->s[b].key == CK_EMPTY) { t->s[b].key = k; t->n++; return 0; }
    if (walk(t, a, &len) != W_FOUND && walk(t, b, &len) != W_FOUND) return -1;
    for (uint32_t i = len - 1; i > 0; i--) t->s[g_path[i]] = t->s[g_path[i - 1]];
    t->s[g_path[0]].key = k;
    t->n++;
    return (int)(len - 1);
}
/* 고전 삽입: T0 부터, 빈 칸을 만날 때까지 축출. 반환: 축출 횟수 또는 -1(limit 초과). 실패해도 표를 되돌리지 않는다. */
static int put_classic(ck_t *t, uint32_t k) {
    uint32_t s = home(t, 0, k), carry = k, kicks = 0;
    for (;;) {
        uint32_t old = t->s[s].key;
        t->s[s].key = carry;
        if (old == CK_EMPTY) { t->n++; return (int)kicks; }
        if (kicks == t->limit) return -1;
        kicks++;
        carry = old;
        s = other(t, s, carry);
    }
}

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static int cmp_u(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return (x > y) - (x < y); }
static double pct(const double *v, int n, double p) { return v[(int)(p * (n - 1) + 0.5)]; }

/* 무작위 서로 다른 키(xorshift32 은 주기 안에서 중복 없음)로 첫 실패까지 채운다. */
static double first_failure(uint32_t cap, hash_fn h, uint32_t limit, uint32_t s0, uint32_t s1, uint32_t *rng, bool seq) {
    ck_t t; ck_setup(&t, cap, h, limit, s0, s1);
    uint32_t seqk = xs32(rng) & 0x0FFFFFFFu;
    for (;;) {
        uint32_t k = seq ? seqk++ : xs32(rng);
        if (k == CK_EMPTY) continue;
        if (put_path(&t, k) < 0) break;
        if (t.n >= 2 * cap) break;
    }
    return (double)t.n / (2.0 * cap);
}

static double g_ff[4096];

int main(void) {
    uint32_t rng = 0x12345678u;
    /* 표 1 */
    printf("[1] first-failure load, path budget unlimited (n/(2cap) when the first insert is impossible)\n");
    printf("  cap(x2)  seeds     min     p5    median    p95     max\n");
    static const struct { uint32_t cap; int seeds; } t1[] = {
        {256, 2000}, {1024, 2000}, {4096, 1000}, {16384, 300}, {65536, 60} };
    double keep[2000];
    for (unsigned i = 0; i < sizeof t1 / sizeof t1[0]; i++) {
        int S = t1[i].seeds;
        for (int j = 0; j < S; j++)
            g_ff[j] = first_failure(t1[i].cap, h_mix, MAX_PATH, xs32(&rng), xs32(&rng), &rng, false);
        qsort(g_ff, (size_t)S, sizeof(double), cmp_d);
        printf("  %7u %6d  %.4f  %.4f  %.4f  %.4f  %.4f\n", t1[i].cap, S, g_ff[0], pct(g_ff, S, .05),
               pct(g_ff, S, .50), pct(g_ff, S, .95), g_ff[S - 1]);
        if (t1[i].cap == 4096) memcpy(keep, g_ff, sizeof(double) * (size_t)S);
    }

    /* 표 3: cap=4096, 그 load 까지 첫 실패 없이 간 비율 */
    printf("\n[3] P(reach load a without any failure), cap=4096x2, 1000 seeds, unlimited budget\n");
    double as[] = {0.30, 0.35, 0.40, 0.42, 0.44, 0.46, 0.48, 0.50, 0.52, 0.55};
    for (unsigned i = 0; i < sizeof as / sizeof as[0]; i++) {
        int c = 0;
        for (int j = 0; j < 1000; j++) if (keep[j] > as[i]) c++;
        printf("  a=%.2f  %6.1f%%\n", as[i], c / 10.0);
    }

    /* 표 2: 경로 예산 K */
    printf("\n[2] first-failure load vs path budget K (max evictions), cap=16384x2, 100 seeds\n");
    printf("     K   median     p5    p95\n");
    uint32_t Ks[] = {4, 8, 16, 32, 64, 128, 512, MAX_PATH};
    for (unsigned i = 0; i < sizeof Ks / sizeof Ks[0]; i++) {
        for (int j = 0; j < 100; j++)
            g_ff[j] = first_failure(16384, h_mix, Ks[i], xs32(&rng), xs32(&rng), &rng, false);
        qsort(g_ff, 100, sizeof(double), cmp_d);
        printf("  %6u  %.4f  %.4f  %.4f\n", Ks[i], pct(g_ff, 100, .5), pct(g_ff, 100, .05), pct(g_ff, 100, .95));
    }

    /* 표 4: 부하 구간별 축출 횟수 (cap=16384, 40 시드) */
    printf("\n[4] evictions per insert, by load window (cap=16384x2, 40 seeds, budget unlimited)\n");
    printf("  window            scheme   inserts   P(0)   mean    p99  p99.9    max\n");
    static const double wins[][2] = {{0.20,0.25},{0.35,0.40},{0.43,0.45},{0.47,0.49}};
    enum { W = 4 };
    static uint32_t buf[2][W][1u << 20];
    uint32_t cnt[2][W] = {{0}};
    for (int scheme = 0; scheme < 2; scheme++) {
        for (int tr = 0; tr < 40; tr++) {
            ck_t t; ck_setup(&t, 16384, h_mix, MAX_PATH, xs32(&rng), xs32(&rng));
            for (;;) {
                double load = (double)t.n / 32768.0;
                if (load >= 0.49) break;
                uint32_t k = xs32(&rng);
                if (k == CK_EMPTY) continue;
                int r = scheme == 0 ? put_path(&t, k) : put_classic(&t, k);
                if (r < 0) break;
                for (int w = 0; w < W; w++)
                    if (load >= wins[w][0] && load < wins[w][1]) buf[scheme][w][cnt[scheme][w]++] = (uint32_t)r;
            }
        }
        for (int w = 0; w < W; w++) {
            uint32_t n = cnt[scheme][w];
            if (n == 0) continue;
            qsort(buf[scheme][w], n, sizeof(uint32_t), cmp_u);
            uint64_t sum = 0; uint32_t zero = 0;
            for (uint32_t i = 0; i < n; i++) { sum += buf[scheme][w][i]; if (!buf[scheme][w][i]) zero++; }
            printf("  [%.2f,%.2f)  %10s %8u  %5.1f%%  %5.2f  %5u  %5u  %5u\n",
                   wins[w][0], wins[w][1], scheme ? "classic" : "path-first", n, 100.0 * zero / n,
                   (double)sum / n, buf[scheme][w][(uint32_t)(n * 0.99)],
                   buf[scheme][w][(uint32_t)(n * 0.999)], buf[scheme][w][n - 1]);
        }
    }

    /* 표 5: 해시와 키 분포 */
    printf("\n[5] first-failure load median, cap=16384x2, 60 seeds, unlimited budget\n");
    g_shift = 32 - 14;
    static const struct { const char *name; hash_fn h; bool same_seed; bool seq; } t5[] = {
        {"fmix32 2 seeds, random keys", h_mix, false, false},
        {"fmix32 2 seeds, sequential keys", h_mix, false, true},
        {"fmix32 SAME seed, random keys", h_mix, true, false},
        {"multiply-shift, random keys", h_mulshift, false, false},
        {"multiply-shift, sequential keys", h_mulshift, false, true},
        {"bit slices (no seed), random keys", h_bits, false, false},
        {"bit slices (no seed), sequential", h_bits, false, true},
    };
    printf("  %-34s median    p5     p95\n", "hash / keys");
    for (unsigned i = 0; i < sizeof t5 / sizeof t5[0]; i++) {
        for (int j = 0; j < 60; j++) {
            uint32_t s0 = xs32(&rng), s1 = t5[i].same_seed ? s0 : xs32(&rng);
            g_ff[j] = first_failure(16384, t5[i].h, MAX_PATH, s0, s1, &rng, t5[i].seq);
        }
        qsort(g_ff, 60, sizeof(double), cmp_d);
        printf("  %-34s %.4f  %.4f  %.4f\n", t5[i].name, pct(g_ff, 60, .5), pct(g_ff, 60, .05), pct(g_ff, 60, .95));
    }

    /* 표 6: 불가능한 삽입 (두 walk 모두 사이클) 을 판정하는 데 읽은 슬롯 수 */
    printf("\n[6] cost of the first impossible insert, cap=16384x2, 300 seeds, Brent walk (slots read, both directions)\n");
    static uint32_t cost[300];
    uint32_t got = 0;
    for (int tr = 0; tr < 300; tr++) {
        ck_t t; ck_setup(&t, 16384, h_mix, MAX_PATH, xs32(&rng), xs32(&rng));
        for (;;) {
            uint32_t k = xs32(&rng);
            if (k == CK_EMPTY) continue;
            g_steps = 0;
            if (put_path(&t, k) < 0) { cost[got++] = (uint32_t)g_steps; break; }
            if (t.n >= 2u * 16384u) break;
        }
    }
    qsort(cost, got, sizeof(uint32_t), cmp_u);
    printf("  trials %u  median %u  p95 %u  max %u slots read\n", got, cost[got / 2], cost[(uint32_t)(got * 0.95)], cost[got - 1]);
    return 0;
}
