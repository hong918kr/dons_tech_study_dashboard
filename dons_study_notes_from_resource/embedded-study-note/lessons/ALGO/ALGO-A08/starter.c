/* ALGO-A08 스트리밍 통계: Welford 분산과 저수지 샘플링
 *
 * 샘플을 저장하지 않고(O(1) 또는 O(k) RAM) 통계와 대표 표본을 유지한다. 비교 기준은 "전부 저장" 방식.
 *   Part A  Welford 온라인 평균/분산 (double · float), Chan 병합(두 누산기 합치기), 정수 이동(shift) 누산기
 *           vs naive Σx²−(Σx)²/n (float/double) vs 두 번 훑기(two-pass) vs __int128 정확값
 *   Part B  정수 EMA y += (x−y)>>k 의 정체 구간(dead band) — 바닥 나눗셈 asr_floor, 고정소수점 누산기로 고치기,
 *           스텝 응답 전수 검사, 실수 EMA 대비 오차 한계(증명한 부등식)를 무작위 스트림에서 매 샘플 확인
 *   Part C  편향 없는 범위 난수(거부 샘플링), 저수지 샘플링 Algorithm R (k개), 카이제곱 균일성 검정 2종
 *           (k-부분집합 균일 · 포함 확률 k/n, 상관 보정), 펌웨어 지연 텔레메트리 요약 vs 전부 저장
 *
 * 모든 테스트는 결정적 (고정 시드 xorshift32). 실행 시간은 CHECK 에 넣지 않는다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so -lm && ./so   (starter.c 도 같은 명령)
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================= 공통: xorshift32 ======================= */
/* 출력은 [1, 2^32−1] — 0 은 절대 안 나온다 (상태 0 은 고정점). Part C 의 rand_below 가 이 점을 어떻게 다루는지 5. 풀이. */
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static uint32_t xs_word(void *ctx) { return xs32((uint32_t *)ctx); }

/* ======================= Part A — Welford ======================= */
typedef struct { uint32_t n; double mean, m2; } welford_t;   /* m2 = Σ(x − mean)² */
typedef struct { uint32_t n; float mean, m2; } welford_f_t;  /* Cortex-M4F 처럼 단정도 FPU 만 있을 때 */

/* 한 샘플 추가: δ = x − mean_old, mean += δ/n, m2 += δ·(x − mean_new). n 은 먼저 늘린다. */
static void welford_push(welford_t *w, double x) {
    (void)w; (void)x;
    /* TODO: n++, δ = x − mean, mean += δ/n, m2 += δ·(x − mean_new) */
}

/* 같은 식을 float 로 (M4F: VDIV.F32 1개, VMUL/VADD 몇 개). */
static void welford_f_push(welford_f_t *w, float x) {
    (void)w; (void)x;
    /* TODO: welford_push 와 같은 식을 float 로 */
}

/* 표본 분산 s² = m2/(n−1). n < 2 이면 0 (정의). */
static double welford_var(const welford_t *w) { return w->n > 1 ? w->m2 / (double)(w->n - 1) : 0.0; }
static double welford_f_var(const welford_f_t *w) { return w->n > 1 ? (double)w->m2 / (double)(w->n - 1) : 0.0; }

/* Chan 병합: a ← a ∪ b.  δ = mean_b − mean_a,  n = na + nb,
 * mean = mean_a + δ·nb/n,  m2 = m2_a + m2_b + δ²·na·nb/n.  빈 쪽은 항등원 (0 으로 나누지 않는다). */
static void welford_merge(welford_t *a, const welford_t *b) {
    (void)a; (void)b;
    /* TODO: 빈 쪽 처리 → δ, mean, m2, n 갱신 */
}

/* naive 한 번 훑기 (교과서 공식) — 비교용, 주어진 코드. */
typedef struct { uint32_t n; double s1, s2; } naive_t;
typedef struct { uint32_t n; float s1, s2; } naive_f_t;
static void naive_push(naive_t *a, double x) { a->n++; a->s1 += x; a->s2 += x * x; }
static void naive_f_push(naive_f_t *a, float x) { a->n++; a->s1 += x; a->s2 += x * x; }
static double naive_var(const naive_t *a) {
    return a->n > 1 ? (a->s2 - a->s1 * a->s1 / (double)a->n) / (double)(a->n - 1) : 0.0;
}
static double naive_f_var(const naive_f_t *a) {
    return a->n > 1 ? (double)((a->s2 - a->s1 * a->s1 / (float)a->n) / (float)(a->n - 1)) : 0.0;
}

/* 정수 ADC 데이터용: 첫 샘플 K 를 빼고 정확한 정수 합을 든다 (d = x − K).
 * s1 = Σd (int64), s2 = Σd² (uint64). 넘칠 샘플은 거부하고 false (누산기는 그대로). */
typedef struct { uint32_t n; int32_t k0; int64_t s1; uint64_t s2; } istats_t;

static bool istats_push(istats_t *s, int32_t x) {
    (void)s; (void)x;
    return false; /* TODO: 첫 샘플이면 K 저장, d = x − K, d² 를 넘침 없이, 넘칠 거면 거부 */
}
static double istats_mean(const istats_t *s) { return s->n ? (double)s->k0 + (double)s->s1 / (double)s->n : 0.0; }
static double istats_var(const istats_t *s) {
    if (s->n < 2) return 0.0;
    double m = (double)s->s1 / (double)s->n;          /* = mean − K, 보통 σ 몇 배 이내 → 상쇄가 작다 */
    return ((double)s->s2 - m * (double)s->s1) / (double)(s->n - 1);
}

/* ======================= Part B — 정수 EMA ======================= */
/* floor(v / 2^k), k = 0..31. 음수의 >> (C11 6.5.7p5: 구현 정의)에 기대지 않는다.
 * int32_t 는 2의 보수가 보장되므로 ~v = −v−1 (v < 0 이면 ≥ 0). */
static int32_t asr_floor(int32_t v, unsigned k) {
    (void)k;
    return v; /* TODO: floor(v / 2^k) — 음수 >> 없이 */
}

/* 흔한 정수 EMA (주어진 코드): y += floor((x − y)/2^k).  |x|,|y| < 2^30 가정 (x − y 넘침 방지). */
static int32_t ema_shift_step(int32_t y, int32_t x, unsigned k) { return y + asr_floor(x - y, k); }
/* 같은 것을 C 의 / (0 쪽으로 버림)로 — 비교용 */
static int32_t ema_trunc_step(int32_t y, int32_t x, unsigned k) { return y + (x - y) / (int32_t)(1 << k); }
/* 반올림 변형: y += floor((x − y + 2^(k−1))/2^k), k ≥ 1 — 비교용 */
static int32_t ema_round_step(int32_t y, int32_t x, unsigned k) {
    return y + asr_floor(x - y + (int32_t)(1 << (k - 1)), k);
}

/* 고친 EMA: acc = y·2^k 를 들고 다닌다 (소수부 k 비트가 acc 안에 산다).
 * acc += x − floor(acc/2^k);  출력 y = floor(acc/2^k).  |x| ≤ 2^15, k ≤ 15 이면 int32 로 넘치지 않는다. */
typedef struct { int32_t acc; unsigned k; } ema_fx_t;
static void ema_fx_init(ema_fx_t *e, int32_t x0, unsigned k) { e->k = k; e->acc = x0 * (int32_t)(1 << k); }
static int32_t ema_fx_out(const ema_fx_t *e) { return asr_floor(e->acc, e->k); }

static int32_t ema_fx_step(ema_fx_t *e, int32_t x) {
    (void)x;
    return ema_fx_out(e); /* TODO: acc 갱신 */
}

/* ======================= Part C — 범위 난수 · 저수지 샘플링 ======================= */
/* next() 는 [0, 2^bits) 의 균일한 단어를 낸다고 가정 (bits = 1..32). 반환: [0, bound) 균일.
 * 거부 샘플링: 맨 아래 2^bits mod bound 개 단어를 버리면 남은 단어 수가 bound 의 배수 → r % bound 균일.
 * 계약 위반(bound == 0 또는 bound > 2^bits)은 0 을 돌려준다 (펌웨어라면 assert). */
static uint32_t rand_below(uint32_t (*next)(void *), void *ctx, uint32_t bound, unsigned bits) {
    (void)bits;
    return bound ? (uint32_t)(next(ctx) % bound) : 0; /* TODO: 이건 나머지 편향(modulo bias)이 있다 → 거부 샘플링 */
}

#define RES_MAX 64
typedef struct {
    uint32_t k, seen, rng;
    int32_t item[RES_MAX];
    uint32_t pos[RES_MAX];        /* 그 표본이 스트림의 몇 번째(0부터)였나 — 검증·디버그용 */
} reservoir_t;

static void reservoir_init(reservoir_t *r, uint32_t k, uint32_t seed) {
    memset(r, 0, sizeof *r);
    r->k = k > RES_MAX ? RES_MAX : k;
    r->rng = seed ? seed : 1u;
}
static void reservoir_reset(reservoir_t *r) { uint32_t k = r->k, s = r->rng; reservoir_init(r, k, s); } /* rng 는 이어 쓴다 */
static uint32_t reservoir_size(const reservoir_t *r) { return r->seen < r->k ? r->seen : r->k; }

/* Algorithm R: 처음 k 개는 그대로. i 번째(0부터, i ≥ k) 항목은 j = U[0, i] 를 뽑아 j < k 면 칸 j 를 교체.
 * seen 이 UINT32_MAX 에 닿으면 더 받지 않고 false (64-bit 카운터·난수로 넓히는 건 8. Q&A). */
static bool reservoir_push(reservoir_t *r, int32_t x) {
    (void)r; (void)x;
    return false; /* TODO: Algorithm R (seen < k 면 채우기, 아니면 j = U[0, seen] 이 k 미만일 때 교체) */
}

/* 카이제곱 생존함수 P(χ²_df > x), df 짝수 = 2m 일 때 닫힌 꼴: e^{−x/2} Σ_{j<m} (x/2)^j / j!  (주어진 코드) */
static double chi2_sf_even(double x, unsigned df) {
    if (df == 0 || df % 2) return -1.0;
    double h = x / 2, t = 1.0, s = 1.0;
    for (unsigned j = 1; j < df / 2; j++) { t *= h / j; s += t; }
    return exp(-h) * s;
}

/* ======================= 테스트 하네스 ======================= */
#ifndef ALGO_A08_NO_MAIN
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static double relerr(double got, double want) { return fabs(got - want) / fabs(want); }
/* u·κ, κ = 1 + |평균|/표준편차 : Welford 의 상대오차가 이 크기에 비례한다 (5. 풀이 — 실측) */
static double ukappa(double mean, double var) { return ldexp(1.0, -53) * (1.0 + fabs(mean) / sqrt(var)); }

/* 정확한 표본 분산: n·Σx² − (Σx)² 를 __int128 로 정확히 → 나눗셈 한 번 (상대오차 ≈ 2^-52 수준) */
static double var_exact_i64(const int64_t *x, uint32_t n) {
    __int128 s1 = 0, s2 = 0;
    for (uint32_t i = 0; i < n; i++) { s1 += x[i]; s2 += (__int128)x[i] * x[i]; }
    __int128 num = (__int128)n * s2 - s1 * s1;
    return (double)num / ((double)n * (double)(n - 1));
}
static double mean_exact_i64(const int64_t *x, uint32_t n) {
    __int128 s1 = 0;
    for (uint32_t i = 0; i < n; i++) s1 += x[i];
    int64_t q = (int64_t)(s1 / n), r = (int64_t)(s1 % n);
    return (double)q + (double)r / (double)n;
}
/* "전부 저장" 방식의 두 번 훑기 (double) */
static double var_two_pass(const int64_t *x, uint32_t n) {
    double m = 0;
    for (uint32_t i = 0; i < n; i++) m += (double)x[i];
    m /= n;
    double s = 0;
    for (uint32_t i = 0; i < n; i++) { double d = (double)x[i] - m; s += d * d; }
    return s / (n - 1);
}
static void gen_offset(int64_t *x, uint32_t n, int64_t off, int64_t half, uint32_t seed) {
    for (uint32_t i = 0; i < n; i++) x[i] = off + (int64_t)(xs32(&seed) % (uint32_t)(2 * half + 1)) - half;
}

#define NA 100000
static int64_t g_x[NA];

static void test_a_small(void) {
    const double v[8] = {2, 4, 4, 4, 5, 5, 7, 9};      /* 평균 5, 모분산 4, 표본분산 32/7 */
    welford_t w = {0};
    CHECK(welford_var(&w) == 0.0 && w.mean == 0.0);    /* n = 0 */
    welford_push(&w, 3.5);
    CHECK(w.n == 1 && w.mean == 3.5 && welford_var(&w) == 0.0);  /* n = 1: 분산 0 */
    w = (welford_t){0};
    for (int i = 0; i < 8; i++) welford_push(&w, v[i]);
    printf("  small: n=%u mean=%.17g m2=%.17g s2=%.17g (want 5, 32, 32/7)\n", w.n, w.mean, w.m2, welford_var(&w));
    CHECK(w.n == 8 && w.mean == 5.0 && w.m2 == 32.0);
    welford_f_t f = {0};
    for (int i = 0; i < 8; i++) welford_f_push(&f, (float)v[i]);
    CHECK(f.n == 8 && f.mean == 5.0f && f.m2 == 32.0f);
    welford_t c = {0};                                  /* 상수 스트림: 분산은 정확히 0, 음수 금지 */
    for (int i = 0; i < 1000; i++) welford_push(&c, 123456789.25);
    CHECK(c.m2 == 0.0 && c.mean == 123456789.25);
}

static void test_a_cancel(void) {
    /* 큰 평균 · 작은 분산: double naive 는 무너지고 Welford/two-pass 는 버틴다 */
    static const struct { int64_t off, half; } cs[] = {{0, 100}, {100000000, 100}, {1000000000, 1}};
    double nv[3], wv[3];
    for (int c = 0; c < 3; c++) {
        gen_offset(g_x, NA, cs[c].off, cs[c].half, 2024u + (uint32_t)c);
        double ex = var_exact_i64(g_x, NA), em = mean_exact_i64(g_x, NA);
        naive_t a = {0}; welford_t w = {0};
        for (uint32_t i = 0; i < NA; i++) { naive_push(&a, (double)g_x[i]); welford_push(&w, (double)g_x[i]); }
        nv[c] = relerr(naive_var(&a), ex); wv[c] = relerr(welford_var(&w), ex);
        double tp = relerr(var_two_pass(g_x, NA), ex);
        printf("  off=%-11lld ±%-4lld exact s2=%-12.6f naive=%-13.6f(rel %.1e) welford rel %.1e two-pass rel %.1e mean abs err %.1e\n",
               (long long)cs[c].off, (long long)cs[c].half, ex, naive_var(&a), nv[c], wv[c], tp, fabs(w.mean - em));
        CHECK(wv[c] <= 16 * ukappa(em, ex) + NA * ldexp(1.0, -53));  /* Welford: 오차 ≈ u·κ + (n 에 비례하는 합산 오차) */
        CHECK(tp < 1e-9);
        CHECK(fabs(w.mean - em) <= 1e-13 * (double)cs[c].off + 1e-9);
    }
    CHECK(nv[0] < 1e-9);       /* 평균 0 근처면 naive 도 괜찮다 */
    CHECK(nv[1] > 1e-6);       /* 평균 1e8, σ≈58: 유효 숫자 대부분 잃음 */
    CHECK(nv[2] > 0.5);        /* 평균 1e9, σ≈0.8: 쓰레기 (음수가 나올 수도) */
}

static void test_a_merge(void) {
    uint32_t seed = 77u, worst_ok = 1;
    double worst = 0;
    for (int t = 0; t < 300; t++) {
        uint32_t n = 2 + xs32(&seed) % 3000;
        int64_t off = (int64_t)(xs32(&seed) % 2000000000u) - 1000000000;
        gen_offset(g_x, n, off, 1 + (int64_t)(xs32(&seed) % 1000), xs32(&seed));
        /* 0~8 군데 무작위로 잘라 (빈 조각 포함) 조각마다 Welford → 차례로 병합 */
        uint32_t cut[10], nc = xs32(&seed) % 9;
        cut[0] = 0;
        for (uint32_t c = 1; c <= nc; c++) cut[c] = xs32(&seed) % (n + 1);
        cut[nc + 1] = n;
        for (uint32_t i = 1; i <= nc + 1; i++)              /* 삽입 정렬 */
            for (uint32_t j = i; j > 0 && cut[j - 1] > cut[j]; j--) { uint32_t tmp = cut[j]; cut[j] = cut[j - 1]; cut[j - 1] = tmp; }
        welford_t all = {0}, one = {0};
        for (uint32_t c = 0; c <= nc; c++) {
            welford_t part = {0};
            for (uint32_t i = cut[c]; i < cut[c + 1]; i++) welford_push(&part, (double)g_x[i]);
            welford_merge(&all, &part);
        }
        for (uint32_t i = 0; i < n; i++) welford_push(&one, (double)g_x[i]);
        double ex = var_exact_i64(g_x, n), e = relerr(welford_var(&all), ex) / ukappa(all.mean, ex);
        if (e > worst) worst = e;
        if (all.n != n || e > 16 || fabs(all.mean - mean_exact_i64(g_x, n)) > 1e-12 * (fabs((double)off) + 1e3)) worst_ok = 0;
    }
    printf("  merge: 300 streams, 0-8 random cuts (empty chunks allowed): worst rel err / (u*kappa) = %.2f\n", worst);
    CHECK(worst_ok);
    welford_t a = {0}, b = {0}, z = {0};                /* 빈 + 빈, 빈 + x, x + 빈 */
    welford_merge(&a, &z);
    CHECK(a.n == 0 && a.mean == 0.0 && a.m2 == 0.0);
    welford_push(&b, 10.0); welford_push(&b, 14.0);
    welford_merge(&a, &b);
    CHECK(a.n == 2 && a.mean == 12.0 && a.m2 == 8.0);
    welford_merge(&a, &z);
    CHECK(a.n == 2 && a.mean == 12.0 && a.m2 == 8.0);
    welford_t p = {0}, q = {0};                         /* 단일 샘플끼리 병합 = Welford 한 걸음 */
    welford_push(&p, 1e9 + 1); welford_push(&q, 1e9 + 3);
    welford_merge(&p, &q);
    CHECK(p.n == 2 && p.mean == 1e9 + 2 && p.m2 == 2.0);
}

static void test_a_float_adc(void) {
    /* 24-bit ADC, 큰 DC 오프셋(로드셀 영점 ≈ 8,000,000 count) + 잡음 ±8: 모두 float 로 정확히 표현됨 (< 2^24) */
    gen_offset(g_x, NA, 8000000, 8, 99u);
    double ex = var_exact_i64(g_x, NA);
    naive_f_t nf = {0}; welford_f_t wf = {0}; istats_t is = {0};
    bool ok = true;
    for (uint32_t i = 0; i < NA; i++) {
        naive_f_push(&nf, (float)g_x[i]);
        welford_f_push(&wf, (float)g_x[i]);
        ok &= istats_push(&is, (int32_t)g_x[i]);
    }
    double en = relerr(naive_f_var(&nf), ex), ew = relerr(welford_f_var(&wf), ex), ei = relerr(istats_var(&is), ex);
    printf("  float ADC 8e6±8, n=%d: exact s2=%.6f | naive float %.6g (rel %.1e) | welford float %.6f (rel %.1e) | int shift %.6f (rel %.1e)\n",
           NA, ex, naive_f_var(&nf), en, welford_f_var(&wf), ew, istats_var(&is), ei);
    CHECK(ok && is.n == NA);
    CHECK(ei < 1e-12);
    CHECK(relerr(istats_mean(&is), mean_exact_i64(g_x, NA)) < 1e-15);
    CHECK(en > 1.0);           /* 단정도 naive: 완전히 틀림 */
    CHECK(ew < 0.2 && ew < en / 10);  /* Welford float 는 훨씬 낫지만 정확하진 않다 (값은 5. 풀이) */
}

static void test_a_istats_edges(void) {
    istats_t s = {0};
    CHECK(istats_var(&s) == 0.0 && istats_mean(&s) == 0.0);
    CHECK(istats_push(&s, INT32_MIN));                  /* K = INT32_MIN */
    CHECK(istats_push(&s, INT32_MAX));                  /* d = 2^32 − 1: d² 는 uint64 에 딱 들어간다 */
    CHECK(s.s2 == 18446744065119617025ull);             /* (2^32 − 1)² */
    CHECK(!istats_push(&s, INT32_MAX) && s.n == 2);     /* 한 번 더 넣으면 s2 가 넘친다 → 거부, 상태 보존 */
    CHECK(fabs(istats_mean(&s) - (-0.5)) < 1e-9);
    CHECK(relerr(istats_var(&s), 4294967295.0 * 4294967295.0 / 2.0) < 1e-12);
}

static void test_b_asr(void) {
    int bad = 0;
    for (unsigned k = 0; k <= 12; k++)
        for (int32_t v = -5000; v <= 5000; v++)
            if (asr_floor(v, k) != (int32_t)floor((double)v / (double)(1 << k))) bad++;
    static const int32_t ed[] = {INT32_MIN, INT32_MIN + 1, -1, 0, 1, INT32_MAX};
    for (unsigned k = 0; k < 32; k++)
        for (int i = 0; i < 6; i++)
            if (asr_floor(ed[i], k) != (int32_t)floor((double)ed[i] / ldexp(1.0, (int)k))) bad++;
    printf("  asr_floor: 13 shifts x 10001 values + 32 x 6 edges, mismatches=%d  (this compiler: -5>>1 = %d)\n",
           bad, (int)(-5 >> 1));
    CHECK(bad == 0);
}

/* 0 에서 출발해 상수 X 를 넣고 더 안 변할 때까지 → 최종 오차 X − y */
/* y0 에서 출발해 상수 X 를 넣고 더 안 변할 때까지 → 최종 오차 X − y */
static int32_t settle_plain(int32_t (*step)(int32_t, int32_t, unsigned), int32_t y0, int32_t X, unsigned k, int *iters) {
    int32_t y = y0;
    for (int it = 0; it < 1000000; it++) {
        int32_t y2 = step(y, X, k);
        if (y2 == y) { *iters = it; return X - y; }
        y = y2;
    }
    *iters = -1;
    return INT32_MIN;
}

static void test_b_step(void) {
    static const int32_t y0s[] = {0, -700, 700};      /* 출발점 3개 → 위로 가는 스텝과 아래로 가는 스텝 모두 */
    int bad_shift = 0, bad_trunc = 0, bad_round = 0, bad_fx = 0, maxit = 0;
    for (unsigned k = 1; k <= 8; k++) {
        int32_t B = (1 << k) - 1, h = 1 << (k - 1);
        for (int s = 0; s < 3; s++)
        for (int32_t X = -600; X <= 600; X++) {
            int it;
            int32_t y0 = y0s[s], e0 = X - y0;                     /* 처음 오차 */
            /* floor 시프트: 위로 가는 스텝(e0 > 0)은 정확히 min(e0, 2^k−1) 아래에서 멈추고, 아래로는 끝까지 간다 */
            int32_t want = e0 > 0 ? (e0 < B ? e0 : B) : 0;
            if (settle_plain(ema_shift_step, y0, X, k, &it) != want || it < 0) bad_shift++;
            if (it > maxit) maxit = it;
            /* 0 쪽 버림(/): 양쪽 대칭으로 |e0| 와 2^k−1 중 작은 것 */
            int32_t ae = e0 < 0 ? -e0 : e0, wt = (ae < B ? ae : B) * (e0 < 0 ? -1 : 1);
            if (settle_plain(ema_trunc_step, y0, X, k, &it) != wt || it < 0) bad_trunc++;
            /* 반올림: 정체 구간 e ∈ [−2^(k−1), 2^(k−1)−1] — 밖에서 출발하면 그 구간 끝에서 멈춘다 */
            int32_t wr = e0 >= 0 ? (e0 < h - 1 ? e0 : h - 1) : (e0 > -h ? e0 : -h);
            if (settle_plain(ema_round_step, y0, X, k, &it) != wr || it < 0) bad_round++;
            /* 고정소수점: 위·아래 모두 출력이 정확히 X */
            ema_fx_t e; ema_fx_init(&e, y0, k);
            int32_t prev = e.acc, out = 0, ok = 0;
            for (int t = 0; t < 1000000; t++) {
                out = ema_fx_step(&e, X);
                if (e.acc == prev) { ok = 1; break; }
                prev = e.acc;
            }
            if (!ok || out != X) bad_fx++;
        }
    }
    printf("  step y0 -> X (y0 = 0/-700/700, X = -600..600, k = 1..8): shift-floor bad=%d trunc bad=%d round bad=%d fixed-point bad=%d (max iters %d)\n",
           bad_shift, bad_trunc, bad_round, bad_fx, maxit);
    CHECK(bad_shift == 0);
    CHECK(bad_trunc == 0);
    CHECK(bad_round == 0);
    CHECK(bad_fx == 0);
    /* 손 계산 한 줄: k = 4, 0 → 100 은 y = 85 에서 멈춘다 (100 − 85 = 15 = 2^4 − 1) */
    int it;
    CHECK(settle_plain(ema_shift_step, 0, 100, 4, &it) == 15);
}

#define NB 20000
static int32_t g_adc[NB];

static void test_b_track(void) {
    /* 12-bit ADC 랜덤 워크 + 가끔 큰 계단: 실수 EMA Y (double) 와 매 샘플 비교 */
    uint32_t s = 4242u;
    int32_t v = 2048;
    for (int i = 0; i < NB; i++) {
        uint32_t r = xs32(&s);
        if (r % 500 == 0) v = (int32_t)(xs32(&s) % 4096);
        else v += (int32_t)(r % 9) - 4;
        if (v < 0) v = 0;
        if (v > 4095) v = 4095;
        g_adc[i] = v;
    }
    static const unsigned ks[] = {1, 2, 4, 6, 8, 10};
    int viol = 0;
    for (int a = 0; a < 6; a++) {
        unsigned k = ks[a];
        double Y = g_adc[0], lagmax = 0, fxmax = 0, alpha = ldexp(1.0, -(int)k);
        int32_t y = g_adc[0];
        ema_fx_t e; ema_fx_init(&e, g_adc[0], k);
        for (int i = 1; i < NB; i++) {
            Y += (g_adc[i] - Y) * alpha;
            y = ema_shift_step(y, g_adc[i], k);
            int32_t yf = ema_fx_step(&e, g_adc[i]);
            double lag = Y - y, df = yf - Y;
            if (lag < -1e-6 || lag > (1 << k) - 1 + 1e-6) viol++;     /* 증명: 0 ≤ Y − y < 2^k */
            if (df <= -1 - 1e-6 || df >= 1 + 1e-6) viol++;            /* 증명: |y_fx − Y| < 1 */
            if (lag > lagmax) lagmax = lag;
            if (fabs(df) > fxmax) fxmax = fabs(df);
        }
        printf("  track k=%-2u: plain lag max %.3f (bound 2^k-1=%d)  fixed-point |err| max %.3f (bound 1)\n",
               k, lagmax, (1 << k) - 1, fxmax);
    }
    CHECK(viol == 0);
}

static void test_b_extremes(void) {
    /* int16 전 범위 계단, k = 15: acc = ±2^30 근처. UBSan 으로 넘침이 없는지 본다 */
    static const int32_t xs[] = {INT16_MIN, INT16_MAX, INT16_MIN, 0, INT16_MAX};
    ema_fx_t e; ema_fx_init(&e, 0, 15);
    int32_t out = 0;
    bool ok = true;
    for (int a = 0; a < 5; a++) {
        for (int t = 0; t < 2000000; t++) out = ema_fx_step(&e, xs[a]);
        ok &= (out == xs[a]);
    }
    CHECK(ok);
    ema_fx_init(&e, INT16_MIN, 15);
    CHECK(ema_fx_out(&e) == INT16_MIN && e.acc == -(1 << 30));
}

/* 8-bit 카운터 단어원: 0,1,...,255,0,... */
typedef struct { uint32_t next, used; } ctr_t;
static uint32_t ctr_word(void *ctx) { ctr_t *c = ctx; c->used++; uint32_t w = c->next; c->next = (c->next + 1) & 255u; return w; }

static void test_c_randbelow(void) {
    /* 모든 bound = 1..256 에 대해, 8-bit 단어 한 주기(256개)를 정확히 소비할 때 각 나머지가 같은 횟수 */
    int bad = 0;
    for (uint32_t b = 1; b <= 256; b++) {
        ctr_t c = {0, 0};
        uint32_t cnt[256] = {0}, calls = 0;
        while (c.used < 256 && calls < 1000) { uint32_t r = rand_below(ctr_word, &c, b, 8); if (r < 256) cnt[r]++; calls++; }
        for (uint32_t v = 0; v < b; v++) if (cnt[v] != 256 / b) bad++;
        if (calls != 256 - 256 % b || c.used != 256) bad++;
    }
    printf("  rand_below exhaustive 8-bit: bounds 1..256, bad=%d\n", bad);
    CHECK(bad == 0);
    ctr_t c0 = {0, 0};
    CHECK(rand_below(ctr_word, &c0, 0, 8) == 0 && rand_below(ctr_word, &c0, 257, 8) == 0 && c0.used == 0);
    /* 32-bit, bound = 3·2^30: r % bound 이면 [0, 2^30) 이 1/2, 거부 샘플링이면 1/3 */
    uint32_t s = 2026u, s2 = 99u, low = 0, lowmod = 0, N = 300000;
    for (uint32_t i = 0; i < N; i++) {
        if (rand_below(xs_word, &s, 3u << 30, 32) < (1u << 30)) low++;
        if (xs32(&s2) % (3u << 30) < (1u << 30)) lowmod++;
    }
    printf("  bound=3*2^30: P(r<2^30) rejection=%.4f (want 1/3)  plain modulo=%.4f\n", (double)low / N, (double)lowmod / N);
    CHECK(fabs((double)low / N - 1.0 / 3) < 0.004);
}

static void test_c_small(void) {
    reservoir_t r;
    reservoir_init(&r, 8, 5u);
    for (int i = 0; i < 5; i++) CHECK(reservoir_push(&r, 100 + i));
    bool ok = reservoir_size(&r) == 5;                      /* n ≤ k: 전부 순서대로 */
    for (uint32_t i = 0; i < 5; i++) ok &= r.item[i] == 100 + (int32_t)i && r.pos[i] == i;
    CHECK(ok);
    reservoir_init(&r, 4, 9u);
    for (int i = 0; i < 1000; i++) reservoir_push(&r, 7 * i);  /* 표본 값 = 그 위치의 스트림 값, 위치는 서로 다름 */
    ok = reservoir_size(&r) == 4 && r.seen == 1000;
    for (uint32_t i = 0; i < 4; i++) {
        ok &= r.pos[i] < 1000 && r.item[i] == 7 * (int32_t)r.pos[i];
        for (uint32_t j = 0; j < i; j++) ok &= r.pos[i] != r.pos[j];
    }
    CHECK(ok);
    reservoir_init(&r, 4, 9u);
    r.seen = UINT32_MAX;                                     /* 카운터 끝: 거부 */
    CHECK(!reservoir_push(&r, 1) && r.seen == UINT32_MAX);
}

static void test_c_subset(void) {
    /* n = 7, k = 3 → 35 개의 3-부분집합이 모두 1/35. T = 70,000 → 칸마다 기대 2000. Pearson χ², df = 34 */
    enum { N7 = 7, K3 = 3, CELLS = 35, T = 70000 };
    int idx[128];
    int nc = 0;
    for (int m = 0; m < 128; m++) idx[m] = __builtin_popcount((unsigned)m) == K3 ? nc++ : -1;
    uint32_t cnt[CELLS] = {0};
    reservoir_t r;
    reservoir_init(&r, K3, 31337u);
    int bad = 0;
    for (int t = 0; t < T; t++) {
        reservoir_reset(&r);
        for (int i = 0; i < N7; i++) reservoir_push(&r, i);
        unsigned m = 0;
        for (uint32_t j = 0; j < K3; j++) m |= 1u << (r.item[j] & 7);
        if (m >= 128 || idx[m] < 0) { bad++; continue; }
        cnt[idx[m]]++;
    }
    double E = (double)T / CELLS, X = 0;
    for (int c = 0; c < CELLS; c++) X += (cnt[c] - E) * (cnt[c] - E) / E;
    double p = chi2_sf_even(X, CELLS - 1);
    printf("  subsets n=7 k=3: T=%d, 35 cells, chi2=%.2f df=34 p=%.3f (bad trials %d)\n", T, X, p, bad);
    CHECK(bad == 0);
    CHECK(p > 0.001);
}

static void test_c_inclusion(void) {
    /* n = 101, k = 10: 각 항목 포함 확률 k/n. 포함 횟수는 칸끼리 음의 상관 → Pearson X 에 (n−1)/(n−k) 를 곱해야 χ²(n−1) */
    enum { N = 101, K = 10, T = 20000 };
    uint32_t cnt[N] = {0};
    reservoir_t r;
    reservoir_init(&r, K, 4711u);
    for (int t = 0; t < T; t++) {
        reservoir_reset(&r);
        for (int i = 0; i < N; i++) reservoir_push(&r, i);
        for (uint32_t j = 0; j < K; j++) if (r.item[j] >= 0 && r.item[j] < N) cnt[r.item[j]]++;
    }
    double E = (double)T * K / N, X = 0;
    for (int i = 0; i < N; i++) X += (cnt[i] - E) * (cnt[i] - E) / E;
    double Xc = X * (N - 1) / (N - K);
    double p = chi2_sf_even(Xc, N - 1), pu = chi2_sf_even(X, N - 1);
    printf("  inclusion n=101 k=10: T=%d, E=%.1f/item, Pearson X=%.2f (p as-if-chi2 %.3f), corrected X*(n-1)/(n-k)=%.2f df=100 p=%.3f\n",
           T, E, X, pu, Xc, p);
    CHECK(p > 0.001);
    CHECK(fabs(chi2_sf_even(124.342, 100) - 0.05) < 1e-4);   /* 표의 임계값과 닫힌 꼴이 맞는지 */
}

static int cmp_i32(const void *a, const void *b) { int32_t x = *(const int32_t *)a, y = *(const int32_t *)b; return (x > y) - (x < y); }
static int32_t qnear(const int32_t *s, uint32_t n, double q) { uint32_t i = (uint32_t)ceil(q * n); return s[i ? i - 1 : 0]; }

#define NC 50000
static int32_t g_lat[NC], g_sorted[NC];
static uint32_t g_rank[NC];
static uint32_t g_ord[NC];
static int cmp_ord(const void *a, const void *b) {  /* (값, 위치) 사전식 → 동률도 고유 순위 */
    uint32_t i = *(const uint32_t *)a, j = *(const uint32_t *)b;
    if (g_lat[i] != g_lat[j]) return g_lat[i] < g_lat[j] ? -1 : 1;
    return (i > j) - (i < j);
}

static void test_c_telemetry(void) {
    /* CAN 요청→응답 지연(µs) 스트림: 기본 120..135, 5% 꼬리 +50..449, 0.1% 스파이크 5000+ */
    uint32_t s = 8086u;
    for (uint32_t i = 0; i < NC; i++) {
        int32_t v = 120 + (int32_t)(xs32(&s) % 16);
        uint32_t r = xs32(&s) % 1000;
        if (r < 50) v += 50 + (int32_t)(xs32(&s) % 400);
        else if (r == 999) v += 5000 + (int32_t)(xs32(&s) % 3000);
        g_lat[i] = v;
    }
    /* 펌웨어 쪽: 저장 없이 한 번 훑기 */
    istats_t is = {0}; welford_t w = {0}; ema_fx_t e; reservoir_t res;
    ema_fx_init(&e, g_lat[0], 4);
    reservoir_init(&res, 64, 1234u);
    int32_t mn = INT32_MAX, mx = INT32_MIN;
    bool ok = true;
    for (uint32_t i = 0; i < NC; i++) {
        int32_t x = g_lat[i];
        ok &= istats_push(&is, x);
        welford_push(&w, x);
        ema_fx_step(&e, x);
        ok &= reservoir_push(&res, x);
        if (x < mn) mn = x;
        if (x > mx) mx = x;
    }
    /* 전부 저장한 쪽 */
    static int64_t x64[NC];
    for (uint32_t i = 0; i < NC; i++) { x64[i] = g_lat[i]; g_sorted[i] = g_lat[i]; g_ord[i] = i; }
    qsort(g_sorted, NC, sizeof g_sorted[0], cmp_i32);
    qsort(g_ord, NC, sizeof g_ord[0], cmp_ord);
    for (uint32_t r = 0; r < NC; r++) g_rank[g_ord[r]] = r;
    double ex = var_exact_i64(x64, NC), em = mean_exact_i64(x64, NC);
    int32_t smp[RES_MAX] = {0};
    uint32_t kk = reservoir_size(&res), below = 0;
    for (uint32_t j = 0; j < kk; j++) { smp[j] = res.item[j]; if (g_rank[res.pos[j]] < NC / 2) below++; }
    qsort(smp, kk, sizeof smp[0], cmp_i32);
    printf("  telemetry N=%d: mean %.4f (exact %.4f)  sd %.4f (exact %.4f)  welford sd %.4f  min %d max %d  ema(k=4) %d\n",
           NC, istats_mean(&is), em, sqrt(istats_var(&is)), sqrt(ex), sqrt(welford_var(&w)), mn, mx, ema_fx_out(&e));
    printf("  quantiles  full-store p50/p90/p99 = %d/%d/%d   reservoir k=64 p50/p90/p99 = %d/%d/%d   samples below true median: %u/64\n",
           qnear(g_sorted, NC, .5), qnear(g_sorted, NC, .9), qnear(g_sorted, NC, .99),
           qnear(smp, kk, .5), qnear(smp, kk, .9), qnear(smp, kk, .99), below);
    printf("  RAM: full store %zu B  vs  summary %zu B (istats %zu + ema %zu + reservoir %zu + min/max 8)\n",
           sizeof g_lat, sizeof is + sizeof e + sizeof res + 8, sizeof is, sizeof e, sizeof res);
    CHECK(ok && kk == 64);
    CHECK(relerr(istats_var(&is), ex) < 1e-12 && relerr(istats_mean(&is), em) < 1e-14);
    CHECK(relerr(welford_var(&w), ex) < 1e-12);
    CHECK(mn == g_sorted[0] && mx == g_sorted[NC - 1]);
    CHECK(below >= 22 && below <= 42);   /* Bin(64, 1/2) 에서 [22, 42] 밖일 확률 0.8% (초기하면 더 작다) */
    bool pos_ok = true;
    for (uint32_t j = 0; j < kk; j++) pos_ok &= res.item[j] == g_lat[res.pos[j]];
    CHECK(pos_ok);
}

int main(void) {
    printf("== Part A: Welford, Chan merge, shifted integer sums ==\n");
    test_a_small();
    test_a_cancel();
    test_a_merge();
    test_a_float_adc();
    test_a_istats_edges();
    printf("== Part B: integer EMA dead band and fixed-point fix ==\n");
    test_b_asr();
    test_b_step();
    test_b_track();
    test_b_extremes();
    printf("== Part C: unbiased bounded random, reservoir sampling, chi-square ==\n");
    test_c_randbelow();
    test_c_small();
    test_c_subset();
    test_c_inclusion();
    test_c_telemetry();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
