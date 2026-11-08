/* DS-M14 관찰용: 독립 근사(1-e^{-kn/m})^k 는 "필터 여러 개의 평균"으로 봐도 맞나?
 * 같은 (n, m, k) 로 서로 다른 키 집합의 필터를 T 개 만들어
 *   - 필터마다 채움률 rho = X/m 와 실측 FP 를 재고
 *   - 평균 FP, E[rho^k], (E[rho])^k, 이론식 3가지를 비교한다.
 * Jensen: x^k 는 [0,1]에서 볼록 → E[rho^k] >= (E[rho])^k. 작은 m 에서 차이가 보인다.
 * cc -std=c11 -Wall -Wextra -Werror -O2 demo_jensen.c -o demo_jensen && ./demo_jensen
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t fnv1a64(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 0x00000100000001B3ull; }
    return h;
}
static uint64_t fmix64(uint64_t x) {
    x ^= x >> 33; x *= 0xFF51AFD7ED558CCDull;
    x ^= x >> 33; x *= 0xC4CEB9FE1A85EC53ull;
    x ^= x >> 33;
    return x;
}
static uint32_t fmix32(uint32_t x) {
    x ^= x >> 16; x *= 0x85EBCA6Bu; x ^= x >> 13; x *= 0xC2B2AE35u; x ^= x >> 16;
    return x;
}

static uint32_t g_w[65536 / 32];
static int g_indep;                      /* 1 = k 개 독립 해시 (비교용), 0 = 더블 해싱 */

/* i 번째 독립 해시: 키와 i 를 섞어 fmix64 */
static uint32_t indep(uint32_t key, uint32_t i) {
    return (uint32_t)fmix64(((uint64_t)i << 32 | key) * 0x9E3779B97F4A7C15ull + i);
}

static void probe(uint32_t key, uint32_t *h1, uint32_t *h2) {
    uint64_t h = fmix64(fnv1a64(&key, sizeof key));
    *h1 = (uint32_t)h; *h2 = (uint32_t)(h >> 32) | 1u;
}
static void add(uint32_t m, uint32_t k, uint32_t key) {
    if (g_indep) {
        for (uint32_t i = 0; i < k; i++) { uint32_t g = indep(key, i) & (m - 1u); g_w[g >> 5] |= 1u << (g & 31u); }
        return;
    }
    uint32_t h1, h2; probe(key, &h1, &h2);
    for (uint32_t i = 0; i < k; i++, h1 += h2) g_w[(h1 & (m - 1u)) >> 5] |= 1u << (h1 & 31u);
}
static bool has(uint32_t m, uint32_t k, uint32_t key) {
    if (g_indep) {
        for (uint32_t i = 0; i < k; i++) {
            uint32_t g = indep(key, i) & (m - 1u);
            if (!((g_w[g >> 5] >> (g & 31u)) & 1u)) return false;
        }
        return true;
    }
    uint32_t h1, h2; probe(key, &h1, &h2);
    for (uint32_t i = 0; i < k; i++, h1 += h2)
        if (!((g_w[(h1 & (m - 1u)) >> 5] >> (h1 & 31u)) & 1u)) return false;
    return true;
}

static void run(uint32_t n, uint32_t m, uint32_t k, uint32_t trials, uint32_t q) {
    double sum_fp = 0, sum_fp2 = 0, sum_rho = 0, sum_rhok = 0, min_fp = 1, max_fp = 0;
    for (uint32_t t = 0; t < trials; t++) {
        memset(g_w, 0, (m / 32u) * sizeof g_w[0]);
        for (uint32_t i = 0; i < n; i++) add(m, k, fmix32((t << 20) | i));          /* 키 집합 t */
        uint32_t x = 0, hit = 0;
        for (uint32_t i = 0; i < m / 32u; i++) x += (uint32_t)__builtin_popcount(g_w[i]);
        for (uint32_t j = 0; j < q; j++) hit += has(m, k, fmix32((t << 20) | (1u << 19) | j));
        double rho = (double)x / m, fp = (double)hit / q;
        sum_rho += rho; sum_rhok += pow(rho, k); sum_fp += fp; sum_fp2 += fp * fp;
        if (fp < min_fp) min_fp = fp;
        if (fp > max_fp) max_fp = fp;
    }
    double T = trials, mean_fp = sum_fp / T, sd_fp = sqrt(sum_fp2 / T - mean_fp * mean_fp);
    double kn = (double)k * n;
    double f_exp = pow(1.0 - exp(-kn / m), k);                       /* 교과서 근사 */
    double f_bin = pow(1.0 - pow(1.0 - 1.0 / m, kn), k);              /* kn 개 독립 균등 위치 */
    double f_dst = pow(1.0 - pow(1.0 - (double)k / m, n), k);         /* 키마다 k 개 서로 다른 위치 */
    printf("n=%u m=%u k=%u  %s  (%u filters x %u queries)\n", n, m, k,
           g_indep ? "k independent hashes" : "double hashing h1+i*h2", trials, q);
    printf("  mean measured FP      %.5f   (per-filter sd %.5f, min %.5f, max %.5f)\n",
           mean_fp, sd_fp, min_fp, max_fp);
    printf("  E[rho^k]              %.5f\n", sum_rhok / T);
    printf("  (E[rho])^k            %.5f   E[rho] = %.4f\n", pow(sum_rho / T, k), sum_rho / T);
    printf("  (1-e^(-kn/m))^k       %.5f\n", f_exp);
    printf("  (1-(1-1/m)^(kn))^k    %.5f\n", f_bin);
    printf("  (1-(1-k/m)^n)^k       %.5f\n", f_dst);
    printf("  mean FP / e-formula   %.3f\n\n", mean_fp / f_exp);
}

int main(void) {
    run(128, 1024, 6, 4000, 4096);
    run(16, 128, 6, 10000, 4096);
    g_indep = 1;
    run(16, 128, 6, 10000, 4096);
    g_indep = 0;
    run(8192, 65536, 6, 200, 20000);
    return 0;
}
