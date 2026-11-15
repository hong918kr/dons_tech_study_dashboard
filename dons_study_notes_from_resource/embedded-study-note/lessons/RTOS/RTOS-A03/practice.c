/* RTOS-A03 추가 연습 — 한 번에 검증
 *  연습 1: MCU 용 스트리밍 통계 (표본 저장 없이 min/max/HWM/Welford + 두 창(window) 합치기)
 *  연습 2: 32비트 CYCCNT 랩어라운드에 안전한 실행 시간과 릴리즈 지터
 *  연습 3: 시계 양자화 — 한 칸 41.67 ns 시계로 100 ns 를 재면 무엇이 나오나
 *  연습 4: 무작위 테스트가 희귀 경로(3연속 고장)를 볼 확률과 필요한 실행 횟수 (정확한 DP)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static double absd(double v) { return v < 0 ? -v : v; }

/* ---------- 연습 1: 스트리밍 통계 ---------- */
typedef struct { uint32_t n, min, max, hwm_idx; double mean, m2; } stream_t;
static void st_init(stream_t *s) { memset(s, 0, sizeof *s); }
static void st_push(stream_t *s, uint32_t x) {
    if (s->n == 0 || x < s->min) s->min = x;
    if (s->n == 0 || x > s->max) { s->max = x; s->hwm_idx = s->n; }
    s->n++;
    double d = (double)x - s->mean;
    s->mean += d / s->n;
    s->m2 += d * ((double)x - s->mean);
}
/* 두 창의 통계 합치기 (Chan · Golub · LeVeque 1979 의 병렬 분산 공식). b 는 a 뒤에 이어진 구간 */
static stream_t st_merge(const stream_t *a, const stream_t *b) {
    if (a->n == 0) return *b;
    if (b->n == 0) return *a;
    stream_t r;
    r.n = a->n + b->n;
    r.min = a->min < b->min ? a->min : b->min;
    if (b->max > a->max) { r.max = b->max; r.hwm_idx = a->n + b->hwm_idx; } else { r.max = a->max; r.hwm_idx = a->hwm_idx; }
    double d = b->mean - a->mean;
    r.mean = a->mean + d * b->n / r.n;
    r.m2 = a->m2 + b->m2 + d * d * ((double)a->n * b->n / r.n);
    return r;
}

/* ---------- 연습 2: 랩 안전 ---------- */
static uint32_t dur_u32(uint32_t t0, uint32_t t1) { return t1 - t0; }              /* 모듈로 2^32 */
/* 주기 P 로 출시돼야 할 k 번째 시작 시각 start[k] 의 편차 = (int32)(start[k] - (start[0] + k*P)).
 * 반환 = max - min (릴리즈 지터), 최대 지각/최대 조기도 돌려준다 */
static uint32_t release_jitter(const uint32_t *start, uint32_t n, uint32_t P, int32_t *late, int32_t *early) {
    int32_t lo = 0, hi = 0;
    for (uint32_t k = 0; k < n; k++) {
        int32_t dev = (int32_t)(start[k] - (start[0] + k * P));
        if (dev < lo) lo = dev;
        if (dev > hi) hi = dev;
    }
    *late = hi; *early = lo;
    return (uint32_t)(hi - lo);
}

/* ---------- 연습 3: 양자화 ---------- */
/* 진짜 구간 [phase, phase + D) 를 q 단위 눈금 시계로 재면: floor((phase + D)/q) - floor(phase/q) */
static uint32_t quant_ticks(uint32_t D, uint32_t q, uint32_t phase) { return (phase + D) / q - phase / q; }

/* ---------- 연습 4: 3연속 고장이 n 스텝 안에 한 번이라도 나올 확률 (마르코프 DP) ---------- */
/* 상태 = 지금까지 연속 고장 수 0/1/2, 흡수 = 3 도달. p = 스텝당 고장 확률 */
static double p_hit_within(double p, uint32_t n) {
    double a0 = 1, a1 = 0, a2 = 0, hit = 0;
    for (uint32_t k = 0; k < n; k++) {
        double n0 = (a0 + a1 + a2) * (1 - p), n1 = a0 * p, n2 = a1 * p;
        hit += a2 * p;
        a0 = n0; a1 = n1; a2 = n2;
    }
    return hit;
}
static uint32_t runs_needed(double p, double conf, uint32_t cap) {     /* hit >= conf 가 되는 최소 n */
    double a0 = 1, a1 = 0, a2 = 0, hit = 0;
    for (uint32_t k = 1; k <= cap; k++) {
        double n0 = (a0 + a1 + a2) * (1 - p), n1 = a0 * p, n2 = a1 * p;
        hit += a2 * p;
        a0 = n0; a1 = n1; a2 = n2;
        if (hit >= conf) return k;
    }
    return 0;
}

int main(void) {
    /* 연습 1 */
    {
        enum { N = 30000 };
        static uint32_t x[N];
        uint32_t s = 99;
        for (int i = 0; i < N; i++) x[i] = 900000u + xs32(&s) % 5000u + (i == 17321 ? 40000u : 0u);
        stream_t all, a, b; st_init(&all); st_init(&a); st_init(&b);
        for (int i = 0; i < N; i++) { st_push(&all, x[i]); if (i < 12000) st_push(&a, x[i]); else st_push(&b, x[i]); }
        double mean = 0; for (int i = 0; i < N; i++) mean += (x[i] - 900000.0); mean = mean / N + 900000.0;
        double var = 0; for (int i = 0; i < N; i++) var += (x[i] - mean) * (x[i] - mean); var /= N;    /* 두 번 훑기 기준값 */
        CHECK(all.n == N && all.max == x[17321] && all.hwm_idx == 17321);
        CHECK(absd(all.mean - mean) < 1e-6 && absd(all.m2 / N - var) < 1e-3 * var);
        stream_t m = st_merge(&a, &b);
        CHECK(m.n == N && m.min == all.min && m.max == all.max && m.hwm_idx == 17321);
        CHECK(absd(m.mean - all.mean) < 1e-6 && absd(m.m2 - all.m2) < 1e-6 * all.m2);
        stream_t e; st_init(&e);
        stream_t m2 = st_merge(&e, &a);
        CHECK(m2.n == a.n && m2.max == a.max);
        printf("info: stream n=%u max=%u at #%u mean=%.3f sd^2=%.1f (two-pass %.1f)\n", all.n, all.max, all.hwm_idx,
               all.mean, all.m2 / N, var);
    }
    /* 연습 2 */
    {
        CHECK(dur_u32(0xFFFFFF00u, 0x00000100u) == 0x200u);                 /* 랩을 한 번 넘어도 */
        CHECK(dur_u32(5, 5) == 0);
        uint32_t P = 16800, st[6];                                            /* 168 MHz, 10 kHz */
        int32_t dev[6] = {0, 35, -12, 80, 3, -20};
        for (int k = 0; k < 6; k++) st[k] = 0xFFFF0000u + (uint32_t)k * P + (uint32_t)dev[k];  /* 도중에 0 을 지난다 */
        int32_t late, early;
        CHECK(release_jitter(st, 6, P, &late, &early) == 100 && late == 80 && early == -20);
        CHECK(st[5] < st[0]);                                                 /* 진짜로 랩했다 */
        /* 168 MHz 에서 CYCCNT 한 바퀴 = 2^32 / 168e6 ≈ 25.6 s — 그보다 긴 구간은 32비트로 못 잰다 */
        CHECK((uint64_t)UINT32_MAX / 168000000u == 25);
    }
    /* 연습 3 */
    {
        uint32_t q = 4167, D = 10000;                                         /* 단위 10 ps: 41.67 ns 눈금, 100 ns 구간 */
        uint32_t lo = UINT32_MAX, hi = 0; uint64_t sum = 0;
        for (uint32_t ph = 0; ph < q; ph++) {
            uint32_t t = quant_ticks(D, q, ph);
            if (t < lo) lo = t;
            if (t > hi) hi = t;
            sum += t;
        }
        CHECK(lo == D / q && hi == D / q + 1);                                /* 2 칸 또는 3 칸 */
        CHECK(sum == D);                                                      /* 위상 평균 × q = 진짜 길이 (정확) */
        uint32_t zero = 0;
        for (uint32_t ph = 0; ph < q; ph++) zero += quant_ticks(1000, q, ph) == 0;   /* 10 ns 짜리 함수 */
        CHECK(zero == q - 1000);                                              /* 76% 확률로 0 칸 */
        printf("info: 100 ns with 41.67 ns ticks -> %u..%u ticks; 10 ns -> 0 ticks in %u/%u phases\n", lo, hi, zero, q);
    }
    /* 연습 4 */
    {
        double p20k = p_hit_within(0.01, 20000);
        uint32_t n99 = runs_needed(0.01, 0.99, 10000000u);
        double p0 = p_hit_within(0.0, 1000000);
        printf("info: P(3-in-a-row within 20000 | p=1%%) = %.4f ; runs for 99%% = %u\n", p20k, n99);
        CHECK(p20k > 0.015 && p20k < 0.025);
        CHECK(n99 > 4500000u && n99 < 4800000u);
        CHECK(p0 == 0.0 && runs_needed(0.5, 0.99, 1000) > 0 && runs_needed(0.5, 0.99, 1000) < 60);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
