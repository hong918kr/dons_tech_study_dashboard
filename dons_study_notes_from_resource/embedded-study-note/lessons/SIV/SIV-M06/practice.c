/* SIV-M06 추가 연습 — 한 번에 검증
 *   P1  다른 ITU-T O.150 다항식(PRBS9/11/23)의 주기 + "잘못된 탭"은 짧은 주기
 *   P2  PRBS31 28비트 병렬 생성기 == 직렬 생성기 (SerDes 데이터패스는 32/64비트 폭)
 *   P3  시험 계획: 목표 BER 링크는 5% 만 통과, 10배 좋은 링크는 ≥95% 통과시키는 최소 허용 오류 수 k
 *   P4  16비트 포화 · 읽으면 지워지는(RC) 오류 카운터: 최대 poll 간격과 포화 검출
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define NEAR(a, b, tol) (fabs((double)(a) - (double)(b)) <= (tol))

/* ---- 본문과 같은 Fibonacci LFSR (x^n + x^k + 1, bit0 = 최신) ---- */
typedef struct { int n, k; } poly_t;
static uint32_t pmask(poly_t p) { return (uint32_t)((1ull << p.n) - 1u); }
static int lfsr_next(poly_t p, uint32_t *s) {
    int b = (int)(((*s >> (p.n - 1)) ^ (*s >> (p.k - 1))) & 1u);
    *s = ((*s << 1) | (uint32_t)b) & pmask(p);
    return b;
}
static uint64_t period(poly_t p, uint64_t limit) {
    uint32_t s = 1, s0 = 1;
    for (uint64_t i = 1; i <= limit; i++) { (void)lfsr_next(p, &s); if (s == s0) return i; }
    return 0;
}

/* ---- P2: 28비트 병렬 PRBS31 ----
 * 새 비트 j (j=0 이 가장 먼저) = s[t+j-31] ^ s[t+j-28] = bit(30-j) ^ bit(27-j) 이고 j ≤ 27 이면 모두 "이미 있는" 비트.
 * x = s ^ (s >> 3) 의 bit(27-j) 가 바로 그 값 → 한 번에 28비트. 시간순으로는 x 의 bit27 → bit0. */
static uint32_t prbs31_step28(uint32_t *s) {
    uint32_t x = (*s ^ (*s >> 3)) & 0x0FFFFFFFu;
    *s = ((*s << 28) | x) & 0x7FFFFFFFu;
    return x;                   /* bit27 = 가장 먼저 나가는 비트 */
}

/* ---- P3: Poisson ---- */
static double poisson_cdf(int k, double lam) {
    if (lam <= 0) return 1.0;
    double sum = 0, ll = log(lam);
    for (int i = 0; i <= k; i++) sum += exp((double)i * ll - lam - lgamma((double)i + 1.0));
    return sum > 1.0 ? 1.0 : sum;
}
static double lambda_k(int k, double cl) {
    double lo = 0, hi = (double)k + 20.0;
    for (int it = 0; it < 200; it++) {
        double mid = 0.5 * (lo + hi);
        if (poisson_cdf(k, mid) > 1.0 - cl) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}
/* N = λ_k / target 비트, 오류 ≤ k 면 PASS. 실제 BER = target/ratio 인 링크의 통과 확률 */
static double pass_prob(int k, double cl, double ratio) { return poisson_cdf(k, lambda_k(k, cl) / ratio); }

/* ---- P4: 16비트 포화 RC 카운터 누적 ---- */
typedef struct { uint64_t total; bool saturated; } err_acc_t;
static void acc_poll(err_acc_t *a, uint16_t hw) {          /* hw = 방금 읽은 값 (읽으면 0 으로 지워짐) */
    a->total += hw;
    if (hw == 0xFFFFu) a->saturated = true;                 /* 실제 오류는 그 이상일 수 있음 → total 은 하한 */
}
static uint16_t hw_counter(uint64_t true_errors) { return true_errors >= 0xFFFFu ? 0xFFFFu : (uint16_t)true_errors; }
static double max_poll_s(double gbps, double ber, int cnt_bits) {
    return (ldexp(1.0, cnt_bits) - 1.0) / (gbps * 1e9 * ber);    /* 1u << 32 는 UB → ldexp */
}

int main(void) {
    /* P1 */
    CHECK(period((poly_t){ 9, 5 }, 1000) == 511);
    CHECK(period((poly_t){ 11, 9 }, 5000) == 2047);
    CHECK(period((poly_t){ 23, 18 }, 10000000) == 8388607);
    uint64_t bad = period((poly_t){ 8, 6 }, 1000);          /* 차수 8 의 삼항식은 원시다항식이 없다 */
    printf("P1 x^8+x^6+1 period=%llu (max 255)\n", (unsigned long long)bad);
    CHECK(bad > 0 && bad < 255);

    /* P2 */
    {
        poly_t p31 = { 31, 28 };
        uint32_t ss = 0x2545F491u & 0x7FFFFFFFu, sp = ss;
        int mism = 0;
        for (int w = 0; w < 20000; w++) {
            uint32_t x = prbs31_step28(&sp);
            for (int j = 27; j >= 0; j--) mism += lfsr_next(p31, &ss) != (int)((x >> j) & 1u);
            mism += ss != sp;                                 /* 28비트마다 상태도 같아야 */
        }
        CHECK(mism == 0);
    }

    /* P3 */
    {
        double cl = 0.95, at_target[3], good[3];
        int kmin = -1;
        for (int k = 0; k < 3; k++) {
            at_target[k] = pass_prob(k, cl, 1.0);
            good[k] = pass_prob(k, cl, 10.0);
            printf("P3 k=%d N=%.3fe12 bits (for 1e-12)  pass@target=%.1f%%  pass@target/10=%.1f%%\n", k,
                   lambda_k(k, cl), 100 * at_target[k], 100 * good[k]);
            if (kmin < 0 && good[k] >= 0.95) kmin = k;
        }
        CHECK(NEAR(at_target[0], 0.05, 1e-9) && NEAR(at_target[1], 0.05, 1e-9) && NEAR(at_target[2], 0.05, 1e-9));
        CHECK(NEAR(good[0], 0.741, 0.001) && NEAR(good[1], 0.918, 0.001) && NEAR(good[2], 0.974, 0.001));
        CHECK(kmin == 2);
    }

    /* P4 */
    {
        double t = max_poll_s(25, 1e-6, 16);
        printf("P4 25Gb/s BER 1e-6 16-bit counter: poll every < %.2f s (32-bit: %.0f s)\n", t, max_poll_s(25, 1e-6, 32));
        CHECK(NEAR(t, 2.6214, 0.001));
        static const uint64_t truth[] = { 100, 65535, 70000, 5 };   /* 실제 오류 수 / poll 구간 */
        err_acc_t a = { 0, false };
        for (int i = 0; i < 4; i++) acc_poll(&a, hw_counter(truth[i]));
        CHECK(a.saturated && a.total == 100u + 65535u + 65535u + 5u);   /* 보고: ">= 131175" (실제 135640) */
        err_acc_t b = { 0, false };
        acc_poll(&b, hw_counter(100)); acc_poll(&b, hw_counter(65534));
        CHECK(!b.saturated && b.total == 65634u);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
