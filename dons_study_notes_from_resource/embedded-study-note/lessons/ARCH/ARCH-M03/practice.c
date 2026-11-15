/* ARCH-M03 추가 연습 — 한 번에 검증
 *   연습 1  2비트 카운터의 iid 미스율 닫힌 식 m(p) = pq / (1 - 2pq)  vs  시뮬레이션
 *   연습 2  branchless 필터 복사 (stream compaction): 무조건 쓰고, 조건부로 전진
 *   연습 3  branchless lower_bound (분기 대신 주소 선택)
 *   연습 4  손익분기 미스율: 언제 branchy 가 csel 보다 빠른가 (M6 실측 숫자로)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

static uint32_t rng = 0x2545F491u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static bool near(double a, double b, double eps) { return (a > b ? a - b : b - a) <= eps; }

/* ---------- 연습 1 ---------- */
static size_t sim_2bit(const uint8_t *a, size_t n, unsigned t, unsigned init) {
    unsigned c = init & 3u;
    size_t miss = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned taken = a[i] >= t;
        if ((c >> 1) != taken) miss++;
        if (taken) { if (c < 3) c++; } else { if (c > 0) c--; }
    }
    return miss;
}

/* 독립 동일 분포(iid) taken 확률 p 에서 2비트 카운터의 정상 상태 미스율.
 * 상태 k 의 정상 확률 ∝ p^k q^(3-k) (출생-사망 사슬) → 미스율 = pq / (p² + q²) = pq / (1 - 2pq). */
static double two_bit_iid_missrate(double p) {
    double q = 1.0 - p, pq = p * q;
    return pq / (1.0 - 2.0 * pq);
}

/* ---------- 연습 2 ---------- */
static size_t filter_ge_branchy(const uint8_t *a, size_t n, unsigned t, uint8_t *out) {
    size_t k = 0;
    for (size_t i = 0; i < n; i++)
        if (a[i] >= t) out[k++] = a[i];
    return k;
}

/* out 은 n 바이트 이상이어야 한다: 버릴 원소도 out[k] 에 일단 쓰고, k 는 고를 때만 1 늘린다 (k <= i 라 범위 안). */
static size_t filter_ge_branchless(const uint8_t *a, size_t n, unsigned t, uint8_t *out) {
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        out[k] = a[i];
        k += (size_t)(a[i] >= t);
    }
    return k;
}

/* ---------- 연습 3 ---------- */
static size_t lower_bound_linear(const int32_t *a, size_t n, int32_t key) {
    size_t i = 0;
    while (i < n && a[i] < key) i++;
    return i;
}

/* 첫 a[i] >= key 의 위치 (없으면 n). 루프 횟수는 n 에만 의존 (log2 n), 데이터에 따른 분기는 없다. */
static size_t lower_bound_branchless(const int32_t *a, size_t n, int32_t key) {
    if (n == 0) return 0;
    const int32_t *base = a;
    size_t len = n;
    while (len > 1) {
        size_t half = len / 2;
        base += (size_t)(base[half - 1] < key) * half;   /* clang 은 보통 csel 로 만든다 */
        len -= half;
    }
    return (size_t)(base - a) + (size_t)(*base < key);
}

/* ---------- 연습 4 ---------- */
/* branchy 비용 ≈ ns_branchy_pred + m · cost,  csel 비용 ≈ ns_csel (데이터 무관).
 * 둘이 같아지는 미스율 m* = (ns_csel - ns_branchy_pred) / cost.  m* <= 0 이면 csel 이 언제나 이긴다. */
static double breakeven_missrate(double ns_branchy_pred, double ns_csel, double cost_ns) {
    if (!(cost_ns > 0)) return 0;
    return (ns_csel - ns_branchy_pred) / cost_ns;
}

int main(void) {
    puts("-- 연습 1: 2-bit counter iid closed form --");
    CHECK(near(two_bit_iid_missrate(0.5), 0.5, 1e-12));
    CHECK(near(two_bit_iid_missrate(0.75), 0.3, 1e-12));
    CHECK(two_bit_iid_missrate(1.0) == 0 && two_bit_iid_missrate(0.0) == 0);
    static uint8_t a[1 << 20];
    for (size_t i = 0; i < sizeof a; i++) a[i] = (uint8_t)xorshift32();
    const unsigned ts[] = {16, 64, 128, 192, 240};
    bool ok = true;
    for (int k = 0; k < 5; k++) {
        size_t taken = 0;
        for (size_t i = 0; i < sizeof a; i++) taken += a[i] >= ts[k];
        double p = (double)taken / (double)sizeof a;
        double sim = (double)sim_2bit(a, sizeof a, ts[k], 1) / (double)sizeof a, th = two_bit_iid_missrate(p);
        printf("      t=%3u p=%.4f  sim %.4f  formula %.4f  best-static %.4f\n", ts[k], p, sim, th, p < 0.5 ? p : 1 - p);
        if (!near(sim, th, 0.004)) ok = false;
    }
    CHECK(ok);

    puts("-- 연습 2: branchless filter copy --");
    static uint8_t out1[5000], out2[5000];
    bool ok2 = true;
    const unsigned ft[] = {0, 1, 100, 128, 255, 256};
    for (int k = 0; k < 6; k++) {
        for (size_t i = 0; i < 5000; i++) a[i] = (uint8_t)xorshift32();
        size_t k1 = filter_ge_branchy(a, 5000, ft[k], out1), k2 = filter_ge_branchless(a, 5000, ft[k], out2);
        if (k1 != k2 || memcmp(out1, out2, k1) != 0) ok2 = false;
    }
    CHECK(ok2);
    CHECK(filter_ge_branchless(a, 0, 0, out2) == 0);
    CHECK(filter_ge_branchless(a, 5000, 0, out2) == 5000 && filter_ge_branchless(a, 5000, 256, out2) == 0);

    puts("-- 연습 3: branchless lower_bound --");
    static int32_t s[1000];
    bool ok3 = true;
    for (size_t n = 0; n <= 1000; n += (n < 20 ? 1 : 97)) {
        int32_t v = -500;
        for (size_t i = 0; i < n; i++) { v += (int32_t)(xorshift32() % 3u); s[i] = v; }   /* 정렬, 중복 많음 */
        for (int32_t key = -505; key <= v + 3; key += (n < 20 ? 1 : 7))
            if (lower_bound_branchless(s, n, key) != lower_bound_linear(s, n, key)) ok3 = false;
        if (lower_bound_branchless(s, n, INT32_MIN) != 0 || lower_bound_branchless(s, n, INT32_MAX) != lower_bound_linear(s, n, INT32_MAX)) ok3 = false;
    }
    CHECK(ok3);
    int32_t one[1] = {7};
    CHECK(lower_bound_branchless(one, 1, 7) == 0 && lower_bound_branchless(one, 1, 8) == 1 && lower_bound_branchless(one, 0, 8) == 0);

    puts("-- 연습 4: break-even miss rate (M6 numbers) --");
    /* u8 합 (demo_bench main): branchy 정렬 0.273, csel 정렬 0.244, 미스당 4.33 ns */
    double m1 = breakeven_missrate(0.273, 0.244, 4.33);
    /* 조건부 FNV-1a t=224 (demo_bench hash): branchy 정렬 0.450, csel(-O2 natural) 정렬 1.239, 미스당 4.50 ns */
    double m2 = breakeven_missrate(0.450, 1.239, 4.50);
    printf("      sum: m* = %.4f (<= 0 -> csel always wins)   hash: m* = %.3f\n", m1, m2);
    CHECK(m1 <= 0);
    CHECK(near(m2, 0.1753, 0.001));
    /* hash t=224 랜덤: 미스율 ≈ min(p,1-p) = 0.125 < m* → branchy 예상 0.450 + 0.125·4.50 = 1.01 ns < csel 1.24 */
    CHECK(0.450 + 0.125 * 4.50 < 1.239);
    CHECK(breakeven_missrate(1.0, 2.0, 0.0) == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
