/* ALGO-A08 추가 연습 — 한 번에 검증
 *   연습 1  Algorithm L (Li 1994): 건너뛰기(skip)로 난수 호출을 O(k·(1 + log(N/k))) 로 — 균일성 χ² + 호출 수
 *   연습 2  슬라이딩 윈도우 분산: Welford 교체(replace) 갱신 — 매 샘플 __int128 정확값과 대조
 *   연습 3  병합 가능한 저수지: bottom-k (무작위 키 최솟값 k개) — 두 코어 표본 합치기, k-부분집합 χ²
 *   연습 4  온라인 공분산·기울기: 자이로 바이어스 vs 온도 (온도 보상 계수) — 정확값과 대조
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice -lm && ./practice
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
/* 거부 샘플링 [0, bound), 32-bit 단어 (본문 Part C) */
static uint32_t rand_below(uint32_t *s, uint32_t bound) {
    uint32_t t = (0u - bound) % bound;                 /* = 2^32 mod bound */
    for (;;) { uint32_t r = xs32(s); if (r >= t) return r % bound; }
}
/* (0,1) 개구간 실수: xorshift32 출력은 1..2^32−1 이라 (r + 0.5)/2^32 는 0 도 1 도 아니다 */
static double unif01(uint32_t *s) { return ((double)xs32(s) + 0.5) * (1.0 / 4294967296.0); }
static double chi2_sf_even(double x, unsigned df) {
    double h = x / 2, t = 1.0, s = 1.0;
    for (unsigned j = 1; j < df / 2; j++) { t *= h / j; s += t; }
    return exp(-h) * s;
}

/* ---------------- 연습 1: Algorithm L ---------------- */
#define KMAX 64
typedef struct {
    uint32_t k, rng, calls;
    uint64_t seen, next;          /* next = 다음에 넣을 항목의 (0부터) 위치 */
    double W;
    int32_t item[KMAX];
} resL_t;

static double uL(resL_t *r) { r->calls++; return unif01(&r->rng); }
static void resL_skip(resL_t *r) {
    double s = floor(log(uL(r)) / log(1.0 - r->W));   /* 기하분포 건너뛰기 */
    r->next = s > 1e18 ? UINT64_MAX : r->next + (uint64_t)s + 1;
}
static void resL_init(resL_t *r, uint32_t k, uint32_t seed) {
    memset(r, 0, sizeof *r);
    r->k = k; r->rng = seed;
}
static void resL_push(resL_t *r, int32_t x) {
    if (r->seen < r->k) {
        r->item[r->seen] = x;
        if (r->seen + 1 == r->k) {                     /* 다 찼다: W 와 첫 건너뛰기 */
            r->W = exp(log(uL(r)) / r->k);
            r->next = r->seen;
            resL_skip(r);
        }
    } else if (r->seen == r->next) {
        r->calls++;
        r->item[rand_below(&r->rng, r->k)] = x;
        r->W *= exp(log(uL(r)) / r->k);
        resL_skip(r);
    }
    r->seen++;
}

static void ex1(void) {
    /* 부분집합 균일성: n = 7, k = 3, 35 칸, df 34 */
    int idx[128], nc = 0;
    for (int m = 0; m < 128; m++) idx[m] = __builtin_popcount((unsigned)m) == 3 ? nc++ : -1;
    uint32_t cnt[35] = {0}, seed = 555u;
    resL_t r;
    for (int t = 0; t < 70000; t++) {
        resL_init(&r, 3, seed);
        for (int i = 0; i < 7; i++) resL_push(&r, i);
        seed = r.rng;
        unsigned m = 0;
        for (int j = 0; j < 3; j++) m |= 1u << r.item[j];
        if (idx[m] >= 0) cnt[idx[m]]++;
    }
    double E = 2000, X = 0;
    for (int c = 0; c < 35; c++) X += (cnt[c] - E) * (cnt[c] - E) / E;
    double ps = chi2_sf_even(X, 34);
    /* 포함 균일성: n = 201, k = 10, T = 20000, 보정 계수 (n−1)/(n−k) */
    uint32_t inc[201] = {0};
    for (int t = 0; t < 20000; t++) {
        resL_init(&r, 10, seed);
        for (int i = 0; i < 201; i++) resL_push(&r, i);
        seed = r.rng;
        for (int j = 0; j < 10; j++) inc[r.item[j]]++;
    }
    double Ei = 20000.0 * 10 / 201, Xi = 0;
    for (int i = 0; i < 201; i++) Xi += (inc[i] - Ei) * (inc[i] - Ei) / Ei;
    double Xc = Xi * 200 / 191, pi = chi2_sf_even(Xc, 200);
    /* 난수 호출 수: N = 10^6, k = 64 — Algorithm R 은 N − k 번 */
    resL_init(&r, 64, 99u);
    for (int i = 0; i < 1000000; i++) resL_push(&r, i);
    double expect = 64.0 * (1 + log(1e6 / 64));
    printf("ex1 Algorithm L: subsets chi2=%.2f p=%.3f | inclusion corrected X=%.1f p=%.3f | N=1e6 k=64: %u random calls (R needs %d; k(1+ln(N/k))=%.0f)\n",
           X, ps, Xc, pi, r.calls, 1000000 - 64, expect);
    CHECK(ps > 0.001);
    CHECK(pi > 0.001);
    CHECK(r.calls < 4 * expect);
}

/* ---------------- 연습 2: 슬라이딩 윈도우 분산 ---------------- */
#define WIN 64
typedef struct { int32_t buf[WIN]; uint32_t n, head; double mean, m2; } winvar_t;

static void winvar_push(winvar_t *w, int32_t x) {
    if (w->n < WIN) {                                  /* 채우는 중: 보통 Welford */
        w->buf[w->n++] = x;
        double d = x - w->mean;
        w->mean += d / w->n;
        w->m2 += d * (x - w->mean);
        return;
    }
    int32_t old = w->buf[w->head];                     /* 가득: old 를 빼고 x 를 넣는 교체 한 번 */
    w->buf[w->head] = x;
    w->head = (w->head + 1) % WIN;
    double mo = w->mean;
    w->mean += (double)(x - old) / WIN;
    w->m2 += (double)(x - old) * ((x - w->mean) + (old - mo));
    if (w->m2 < 0) w->m2 = 0;                          /* 반올림으로 아주 작은 음수가 될 수 있다 */
}

static void ex2(void) {
    enum { NS = 200000 };
    static int32_t xs[NS];
    uint32_t s = 31u;
    int32_t v = 1000000;
    for (int i = 0; i < NS; i++) { v += (int32_t)(xs32(&s) % 21) - 10; xs[i] = v + (int32_t)(xs32(&s) % 7); }
    winvar_t w; memset(&w, 0, sizeof w);
    double worst = 0;
    int bad = 0;
    for (int i = 0; i < NS; i++) {
        winvar_push(&w, xs[i]);
        if (i + 1 < WIN) continue;
        __int128 s1 = 0, s2 = 0;
        for (int j = i + 1 - WIN; j <= i; j++) { s1 += xs[j]; s2 += (__int128)xs[j] * xs[j]; }
        double ex = (double)((__int128)WIN * s2 - s1 * s1) / ((double)WIN * (WIN - 1));
        double got = w.m2 / (WIN - 1), e = fabs(got - ex) / (ex > 1 ? ex : 1);
        if (e > worst) worst = e;
        if (e > 1e-6) bad++;
    }
    printf("ex2 sliding-window variance W=%d over %d samples (mean ~1e6): worst rel err %.2e, windows > 1e-6: %d\n",
           WIN, NS, worst, bad);
    CHECK(bad == 0);
}

/* ---------------- 연습 3: bottom-k 병합 가능한 저수지 ---------------- */
typedef struct { uint32_t k, n, rng; uint64_t key[KMAX]; int32_t item[KMAX]; } botk_t;

/* 키 = (난수 32비트 << 32) | 전역 위치 → 같은 키 없음 (동률은 위치로 깸) */
static void botk_offer(botk_t *b, uint64_t key, int32_t x) {
    if (b->n < b->k) { b->key[b->n] = key; b->item[b->n++] = x; return; }
    uint32_t mx = 0;
    for (uint32_t j = 1; j < b->k; j++) if (b->key[j] > b->key[mx]) mx = j;
    if (key < b->key[mx]) { b->key[mx] = key; b->item[mx] = x; }
}
static void botk_push(botk_t *b, int32_t x, uint32_t gpos) { botk_offer(b, ((uint64_t)xs32(&b->rng) << 32) | gpos, x); }
static void botk_merge(botk_t *a, const botk_t *b) { for (uint32_t j = 0; j < b->n; j++) botk_offer(a, b->key[j], b->item[j]); }

static void ex3(void) {
    int idx[128], nc = 0;
    for (int m = 0; m < 128; m++) idx[m] = __builtin_popcount((unsigned)m) == 3 ? nc++ : -1;
    uint32_t cnt[35] = {0}, sa = 1001u, sb = 2002u;
    for (int t = 0; t < 70000; t++) {
        botk_t A = {.k = 3, .rng = sa}, Bk = {.k = 3, .rng = sb};
        for (int i = 0; i < 4; i++) botk_push(&A, i, (uint32_t)i);     /* 코어 0: 항목 0..3 */
        for (int i = 4; i < 7; i++) botk_push(&Bk, i, (uint32_t)i);    /* 코어 1: 항목 4..6 */
        sa = A.rng; sb = Bk.rng;
        botk_merge(&A, &Bk);
        unsigned m = 0;
        for (uint32_t j = 0; j < A.n; j++) m |= 1u << A.item[j];
        if (A.n == 3 && idx[m] >= 0) cnt[idx[m]]++;
    }
    double X = 0;
    for (int c = 0; c < 35; c++) X += (cnt[c] - 2000.0) * (cnt[c] - 2000.0) / 2000.0;
    double p = chi2_sf_even(X, 34);
    printf("ex3 bottom-k merged from 2 cores (4 + 3 items, k=3): chi2=%.2f df=34 p=%.3f\n", X, p);
    CHECK(p > 0.001);
}

/* ---------------- 연습 4: 온라인 공분산 ---------------- */
typedef struct { uint32_t n; double mx, my, sxx, sxy; } cov_t;
static void cov_push(cov_t *c, double x, double y) {
    c->n++;
    double dx = x - c->mx;
    c->mx += dx / c->n;
    c->my += (y - c->my) / c->n;
    c->sxx += dx * (x - c->mx);
    c->sxy += dx * (y - c->my);                        /* 옛 평균 쪽 하나, 새 평균 쪽 하나 */
}

static void ex4(void) {
    /* 온도 x = 25.00°C ± (0.01°C 단위 정수, 2500 ± 2000), 자이로 바이어스 y = 3e6 + 0.75·x + 잡음 (raw LSB) */
    enum { NS = 50000 };
    static int64_t X[NS], Y[NS];
    uint32_t s = 4u;
    for (int i = 0; i < NS; i++) {
        X[i] = 2500 + (int64_t)(xs32(&s) % 4001) - 2000;
        Y[i] = 3000000 + (3 * X[i]) / 4 + (int64_t)(xs32(&s) % 41) - 20;
    }
    cov_t c = {0};
    for (int i = 0; i < NS; i++) cov_push(&c, (double)X[i], (double)Y[i]);
    __int128 sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < NS; i++) { sx += X[i]; sy += Y[i]; sxx += (__int128)X[i] * X[i]; sxy += (__int128)X[i] * Y[i]; }
    double exx = (double)((__int128)NS * sxx - sx * sx), exy = (double)((__int128)NS * sxy - sx * sy);
    double slope = c.sxy / c.sxx, eslope = exy / exx;
    double ecov = exy / ((double)NS * (NS - 1)), cov = c.sxy / (NS - 1);
    printf("ex4 online covariance: cov %.6f (exact %.6f, rel %.1e)  slope %.6f LSB per 0.01C (exact %.6f)\n",
           cov, ecov, fabs(cov - ecov) / ecov, slope, eslope);
    CHECK(fabs(cov - ecov) / ecov < 1e-9);
    CHECK(fabs(slope - eslope) < 1e-9 && fabs(slope - 0.75) < 0.01);
}

int main(void) {
    ex1();
    ex2();
    ex3();
    ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
