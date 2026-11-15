/* ARCH-M03 분기 예측 실측: 정렬 데이터 vs 랜덤 데이터 — 체크 테스트 (결정적인 것만)
 *
 *   Part A  임계값 필터 세 판: branchy (if) · branchless 마스크 (-(x>=t) & x) · 히스토그램 오라클,
 *           정렬 입력을 만드는 counting sort. 세 판이 모든 입력에서 같은 합을 내야 한다.
 *   Part B  미스 비용을 숫자로: 2비트 카운터 미스 수 (모델 예측), 최소제곱 직선 (미스율 → ns),
 *           미스당 ns/사이클 계산, 비교 연산자 없이 넘침 없는 int32 마스크 ge_mask_i32.
 *   Part C  펌웨어: ADC 블록에서 임계값 이상 샘플의 합·개수를 분기 없이 (int32, int16 부호 비트 판).
 *
 * 시간은 CHECK 하지 않는다 → demo_bench.c + demo_kernels.c (M6 실측), demo_cm.c (Cortex-M asm).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <limits.h>
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

/* ======================= Part A — 세 가지 필터와 정렬 입력 ======================= */
/* a[i] >= t 인 원소의 합. t 는 0..256 (256 이면 아무것도 안 고른다). 평범한 if 로. */
static uint64_t sum_ge_branchy(const uint8_t *a, size_t n, unsigned t) {
    (void)a; (void)n; (void)t;
    return 0; /* TODO: a[i] >= t 이면 s += a[i] */
}

/* 같은 것을 if / ?: 없이: 비교 결과(0/1)를 마스크(0 / 전부 1)로 바꿔 AND. */
static uint64_t sum_ge_mask(const uint8_t *a, size_t n, unsigned t) {
    (void)a; (void)n; (void)t;
    return 0; /* TODO: (x >= t) 는 0/1 → 부호 없는 0 - 1 = 전부 1. 그 마스크와 x 를 AND 해서 더한다 */
}

/* 오라클: 값별 개수(히스토그램)를 센 뒤 v >= t 인 v 에 대해 v * count[v] 를 더한다. 원소 순서와 무관. */
static uint64_t sum_ge_hist(const uint8_t *a, size_t n, unsigned t) {
    (void)a; (void)n; (void)t;
    return 0; /* TODO: cnt[256] 을 채우고 v = t..255 에 대해 v * cnt[v] */
}

/* 오름차순 counting sort (O(n + 256)). 벤치마크의 "정렬 입력" 을 만든다. */
static void sort_u8(uint8_t *a, size_t n) {
    (void)a; (void)n; /* TODO: 값별 개수를 센 뒤 0 부터 개수만큼 다시 쓴다 */
}

/* ======================= Part B — 미스 비용을 숫자로 ======================= */
/* 분기 "a[i] >= t" 하나를 2비트 포화 카운터 하나로 예측할 때의 미스 수.
 * 상태 0,1 = not-taken 예측, 2,3 = taken 예측. taken 이면 +1 (최대 3), 아니면 -1 (최소 0). init = 시작 상태. */
static size_t sim_2bit(const uint8_t *a, size_t n, unsigned t, unsigned init) {
    (void)a; (void)n; (void)t; (void)init;
    return 0; /* TODO: 예측 = 상태의 윗비트. 예측 != 실제면 miss++, 그 다음 상태를 포화 증감 */
}

/* 최소제곱 직선 y = icpt + slope * x. n < 2 또는 x 가 전부 같으면 slope = icpt = 0. */
static double fit_slope(const double *x, const double *y, int n, double *icpt) {
    (void)x; (void)y; (void)n;
    *icpt = 0;
    return 0; /* TODO: slope = Σ(x-x̄)(y-ȳ) / Σ(x-x̄)², icpt = ȳ - slope·x̄. 퇴화 입력 처리 */
}

/* 미스당 ns: (못 맞히는 입력의 ns/원소 - 잘 맞히는 입력의 ns/원소) / 원소당 미스율. 미스율 <= 0 이면 0. */
static double miss_cost_ns(double ns_unpred, double ns_pred, double miss_rate) {
    (void)ns_unpred; (void)ns_pred; (void)miss_rate;
    return 0; /* TODO */
}

/* x >= t 이면 0xFFFFFFFF, 아니면 0. 규칙: 비교 연산자(< <= > >= == !=)·if·?: 금지, 넘침(UB) 금지.
 * 힌트: int64 에서 x - t 는 넘치지 않는다. 부호 비트를 unsigned 시프트로 꺼낸다. */
static uint32_t ge_mask_i32(int32_t x, int32_t t) {
    (void)x; (void)t;
    return 0; /* TODO: 64비트 차의 부호 비트 → 0/1 → 마스크 */
}

/* (주어짐, 틀린 판) 32비트 차의 부호 비트. 넘침을 unsigned 로 감아서 UB 는 없지만 답이 틀린다. */
static uint32_t ge_mask_i32_naive(int32_t x, int32_t t) {
    uint32_t d = (uint32_t)x - (uint32_t)t;
    return (d >> 31) - 1u;
}

/* ======================= Part C — ADC 블록 (펌웨어) ======================= */
/* s[i] >= t 인 샘플의 합을 돌려주고 개수를 *count 에. 분기 없이 ge_mask_i32 로 (ISR 실행 시간이 데이터와 무관하게). */
static int64_t adc_sum_ge(const int32_t *s, size_t n, int32_t t, size_t *count) {
    (void)s; (void)n; (void)t;
    *count = 0;
    return 0; /* TODO: ge_mask_i32 의 한 비트로 64비트 마스크를 만들어 합에, 그 비트를 개수에 */
}

/* int16 샘플 판: (int32)s - t 는 넘치지 않으므로 32비트 부호 비트로 충분하다. 비교 연산자 금지.
 * 음수의 >> 는 구현 정의(C11 6.5.7p5) → unsigned 시프트로 부호 비트를 꺼낸다. */
static int32_t adc_sum_ge_i16(const int16_t *s, size_t n, int16_t t) {
    (void)s; (void)n; (void)t;
    return 0; /* TODO: int32 차 d 의 부호 비트 (unsigned 시프트) → 0/1 → -1/0 마스크 */
}

/* ============================== 테스트 ============================== */
static uint64_t ref_sum_ge(const uint8_t *a, size_t n, unsigned t) {   /* 테스트 전용 기준 */
    uint64_t s = 0;
    for (size_t i = 0; i < n; i++) s += (a[i] >= t) ? a[i] : 0;
    return s;
}

static bool is_sorted_u8(const uint8_t *a, size_t n) {
    for (size_t i = 1; i < n; i++) if (a[i - 1] > a[i]) return false;
    return true;
}

static void test_part_a(void) {
    puts("-- Part A: three filters agree, counting sort --");
    uint8_t ramp[256];
    for (int i = 0; i < 256; i++) ramp[i] = (uint8_t)i;
    CHECK(sum_ge_branchy(ramp, 256, 128) == 24512);
    CHECK(sum_ge_mask(ramp, 256, 128) == 24512);
    CHECK(sum_ge_hist(ramp, 256, 128) == 24512);
    CHECK(sum_ge_mask(ramp, 256, 0) == 32640 && sum_ge_hist(ramp, 256, 0) == 32640);
    CHECK(sum_ge_mask(ramp, 256, 255) == 255 && sum_ge_hist(ramp, 256, 255) == 255);
    CHECK(sum_ge_branchy(ramp, 256, 256) == 0 && sum_ge_mask(ramp, 256, 256) == 0 && sum_ge_hist(ramp, 256, 256) == 0);
    CHECK(sum_ge_branchy(ramp, 0, 0) == 0 && sum_ge_mask(ramp, 0, 0) == 0 && sum_ge_hist(ramp, 0, 0) == 0);

    static uint8_t buf[5000], cpy[5000];
    const size_t sizes[] = {1, 2, 3, 7, 64, 255, 1000, 4999};
    const unsigned ts[] = {0, 1, 2, 127, 128, 129, 200, 254, 255, 256};
    bool ok_rand = true, ok_sorted = true, ok_const = true;
    for (size_t si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
        size_t n = sizes[si];
        for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xorshift32();
        memcpy(cpy, buf, n);
        sort_u8(cpy, n);
        for (size_t ti = 0; ti < sizeof ts / sizeof ts[0]; ti++) {
            unsigned t = ts[ti];
            uint64_t r = ref_sum_ge(buf, n, t);
            if (sum_ge_branchy(buf, n, t) != r || sum_ge_mask(buf, n, t) != r || sum_ge_hist(buf, n, t) != r) ok_rand = false;
            if (sum_ge_branchy(cpy, n, t) != r || sum_ge_mask(cpy, n, t) != r || sum_ge_hist(cpy, n, t) != r) ok_sorted = false;
        }
    }
    memset(buf, 255, 4999);
    if (sum_ge_mask(buf, 4999, 255) != 4999u * 255u || sum_ge_mask(buf, 4999, 256) != 0) ok_const = false;
    memset(buf, 0, 4999);
    if (sum_ge_mask(buf, 4999, 0) != 0 || sum_ge_hist(buf, 4999, 1) != 0) ok_const = false;
    CHECK(ok_rand);    /* 8 크기 × 10 임계값, 랜덤 입력 */
    CHECK(ok_sorted);  /* 같은 데이터를 정렬해도 합은 같다 (순서와 무관) */
    CHECK(ok_const);

    /* counting sort: 정렬됐고, 값별 개수가 그대로 (순열) */
    for (size_t i = 0; i < 5000; i++) buf[i] = (uint8_t)(xorshift32() % 7u * 37u);
    memcpy(cpy, buf, 5000);
    sort_u8(cpy, 5000);
    CHECK(is_sorted_u8(cpy, 5000));
    bool perm = true;
    for (unsigned v = 0; v < 256; v++) {
        size_t c1 = 0, c2 = 0;
        for (size_t i = 0; i < 5000; i++) { c1 += buf[i] == v; c2 += cpy[i] == v; }
        if (c1 != c2) perm = false;
    }
    CHECK(perm);
    sort_u8(cpy, 0);   /* n = 0 은 아무것도 안 한다 */
    CHECK(cpy[0] == 0 && cpy[4999] == 6 * 37);
}

static bool near(double a, double b, double eps) { return (a > b ? a - b : b - a) <= eps; }

static void test_part_b(void) {
    puts("-- Part B: 2-bit counter model, line fit, miss cost, overflow-safe mask --");
    static uint8_t a[1 << 20];
    uint8_t ramp[256];
    for (int i = 0; i < 256; i++) ramp[i] = (uint8_t)i;
    CHECK(sim_2bit(ramp, 256, 128, 1) == 2);    /* 정렬: N...N T T(둘 다 미스) T...T → 미스 2 */
    CHECK(sim_2bit(ramp, 256, 0, 1) == 1);      /* 항상 taken: 첫 번째만 미스 */
    CHECK(sim_2bit(ramp, 256, 256, 1) == 0);    /* 항상 not-taken, 약한 NT 에서 시작 */
    CHECK(sim_2bit(ramp, 256, 256, 3) == 2);    /* 강한 T 에서 시작하면 두 번 미스한 뒤 적응 */
    for (int i = 0; i < 1000; i++) a[i] = (i & 1) ? 0 : 255;   /* T N T N ... */
    CHECK(sim_2bit(a, 1000, 128, 1) == 1000);   /* 01 에서 시작: 매번 반대로 예측 → 100% 미스 */
    CHECK(sim_2bit(a, 1000, 128, 0) == 500);    /* 00 에서 시작: 00↔01 사이를 오가며 T 만 미스 → 50% */
    for (size_t i = 0; i < sizeof a; i++) a[i] = (uint8_t)xorshift32();
    double r128 = (double)sim_2bit(a, sizeof a, 128, 1) / (double)sizeof a;
    double r64 = (double)sim_2bit(a, sizeof a, 64, 1) / (double)sizeof a;
    printf("      2-bit sim on 1M random bytes: t=128 miss %.4f, t=64 miss %.4f (best static: 0.5, 0.25)\n", r128, r64);
    CHECK(near(r128, 0.5, 0.005));
    CHECK(near(r64, 0.30, 0.005));              /* p = 0.75 에서 2비트 카운터는 0.25 가 아니라 0.30 (연습 1) */

    double x0[] = {0, 0.1, 0.2, 0.5}, y0[] = {0.25, 0.65, 1.05, 2.25}, ic = -1;
    CHECK(near(fit_slope(x0, y0, 4, &ic), 4.0, 1e-9) && near(ic, 0.25, 1e-9));
    double xd[] = {0.3, 0.3, 0.3}, yd[] = {1, 2, 3};
    CHECK(fit_slope(xd, yd, 3, &ic) == 0 && ic == 0);
    CHECK(fit_slope(x0, y0, 1, &ic) == 0 && ic == 0);
    /* Apple M6 실측 (demo_bench sweep, 랜덤 u8 4M, t = 0,16,...,256): x = min(p,1-p), y = branchy ns/원소 */
    const double mx[] = {0.0000, 0.0623, 0.1248, 0.1871, 0.2495, 0.3122, 0.3747, 0.4371, 0.4996,
                         0.4377, 0.3755, 0.3128, 0.2502, 0.1875, 0.1248, 0.0624, 0.0000};
    const double my[] = {0.286, 0.544, 0.797, 1.050, 1.306, 1.683, 1.986, 2.242, 2.387,
                         2.335, 1.997, 1.712, 1.458, 1.153, 0.861, 0.570, 0.247};
    double slope = fit_slope(mx, my, 17, &ic);
    printf("      M6 sweep fit: ns/elem = %.3f + %.3f * missrate\n", ic, slope);
    CHECK(slope > 4.0 && slope < 5.0);
    CHECK(ic > 0.15 && ic < 0.40);

    /* M6 실측 (demo_bench main, u8 16M, t=128): branchy 정렬 0.273, 랜덤 2.440 ns/원소, 미스율 0.5 */
    double c = miss_cost_ns(2.440, 0.273, 0.5), ghz = 4.31;
    printf("      miss cost = %.2f ns = %.1f cycles @ %.2f GHz (dependent-add estimate)\n", c, c * ghz, ghz);
    CHECK(near(c, 4.334, 1e-9));
    CHECK(c * ghz > 15 && c * ghz < 25);
    CHECK(miss_cost_ns(2.0, 0.3, 0.0) == 0 && miss_cost_ns(2.0, 0.3, -1.0) == 0);

    /* ge_mask_i32: 경계값 전수 쌍 + 랜덤 20만 쌍 */
    const int32_t sp[] = {INT32_MIN, INT32_MIN + 1, -65536, -129, -1, 0, 1, 127, 128, 65535, INT32_MAX - 1, INT32_MAX};
    const size_t ns = sizeof sp / sizeof sp[0];
    bool ok_sp = true, ok_rand = true;
    for (size_t i = 0; i < ns; i++)
        for (size_t j = 0; j < ns; j++)
            if (ge_mask_i32(sp[i], sp[j]) != ((sp[i] >= sp[j]) ? 0xFFFFFFFFu : 0u)) ok_sp = false;
    for (int k = 0; k < 200000; k++) {
        int32_t x = (int32_t)xorshift32(), t = (int32_t)xorshift32();
        if (k & 1) {   /* 같거나 바로 옆 값도 많이 (int64 로 계산해 int32 범위 안일 때만) */
            int64_t near_t = (int64_t)x + (int64_t)(xorshift32() % 5u) - 2;
            if (near_t >= INT32_MIN && near_t <= INT32_MAX) t = (int32_t)near_t;
        }
        if (ge_mask_i32(x, t) != ((x >= t) ? 0xFFFFFFFFu : 0u)) ok_rand = false;
    }
    CHECK(ok_sp);
    CHECK(ok_rand);
    CHECK(ge_mask_i32(INT32_MAX, -1) == 0xFFFFFFFFu && ge_mask_i32(INT32_MIN, 1) == 0);
    /* 32비트 차를 쓰는 순진한 판은 넘침 쌍에서 틀린다 (그래서 Part B 의 함정) */
    CHECK(ge_mask_i32_naive(5, 3) == 0xFFFFFFFFu && ge_mask_i32_naive(3, 5) == 0);
    CHECK(ge_mask_i32_naive(INT32_MAX, -1) == 0);           /* 틀린 답: MAX >= -1 인데 0 */
    CHECK(ge_mask_i32_naive(INT32_MIN, 1) == 0xFFFFFFFFu);  /* 틀린 답: MIN < 1 인데 전부 1 */
}

static int64_t ref_adc(const int32_t *s, size_t n, int32_t t, size_t *cnt) {
    int64_t sum = 0; size_t c = 0;
    for (size_t i = 0; i < n; i++) if (s[i] >= t) { sum += s[i]; c++; }
    *cnt = c;
    return sum;
}

static void test_part_c(void) {
    puts("-- Part C: ADC block, branchless sum/count --");
    static int32_t s[4096];
    /* 센서 같은 신호: 삼각파 (진폭 2000, 주기 512) + 노이즈 ±64, 기준 1000 */
    for (int i = 0; i < 4096; i++) {
        int ph = i % 512, tri = ph < 256 ? ph * 16 - 2048 : (511 - ph) * 16 - 2048;
        s[i] = 1000 + tri + (int32_t)(xorshift32() % 129u) - 64;
    }
    size_t c = 0, rc = 0;
    int64_t sum = adc_sum_ge(s, 4096, 1500, &c), rs = ref_adc(s, 4096, 1500, &rc);
    printf("      triangle+noise, t=1500: count %zu, sum %lld\n", c, (long long)sum);
    CHECK(sum == rs && c == rc && c > 0 && c < 4096);
    CHECK(adc_sum_ge(s, 4096, INT32_MIN, &c) == ref_adc(s, 4096, INT32_MIN, &rc) && c == 4096);
    CHECK(adc_sum_ge(s, 4096, INT32_MAX, &c) == 0 && c == 0);
    CHECK(adc_sum_ge(s, 0, 0, &c) == 0 && c == 0);

    const int32_t ext[] = {INT32_MIN, INT32_MAX, -1, 0, 1, INT32_MAX, INT32_MIN + 1, 42};
    bool ok_ext = true;
    const int32_t tt[] = {INT32_MIN, -1, 0, 1, 42, INT32_MAX};
    for (size_t k = 0; k < sizeof tt / sizeof tt[0]; k++) {
        int64_t a1 = adc_sum_ge(ext, 8, tt[k], &c), a2 = ref_adc(ext, 8, tt[k], &rc);
        if (a1 != a2 || c != rc) ok_ext = false;
    }
    CHECK(ok_ext);
    CHECK(adc_sum_ge(ext, 8, INT32_MAX, &c) == 2LL * INT32_MAX && c == 2);

    bool ok_r = true;
    for (int i = 0; i < 4096; i++) s[i] = (int32_t)xorshift32();
    for (int k = 0; k < 16; k++) {
        int32_t t = (int32_t)xorshift32();
        if (adc_sum_ge(s, 4096, t, &c) != ref_adc(s, 4096, t, &rc) || c != rc) ok_r = false;
    }
    CHECK(ok_r);

    static int16_t h[2048];
    for (int i = 0; i < 2048; i++) h[i] = (int16_t)(xorshift32() & 0xFFFFu);
    h[0] = INT16_MIN; h[1] = INT16_MAX; h[2] = -1; h[3] = 0;
    bool ok16 = true;
    const int16_t t16[] = {INT16_MIN, -1, 0, 1, 1000, INT16_MAX};
    for (size_t k = 0; k < sizeof t16 / sizeof t16[0]; k++) {
        int32_t r = 0;
        for (int i = 0; i < 2048; i++) if (h[i] >= t16[k]) r += h[i];
        if (adc_sum_ge_i16(h, 2048, t16[k]) != r) ok16 = false;
    }
    CHECK(ok16);
    int16_t pair[2] = {INT16_MAX, INT16_MIN};
    CHECK(adc_sum_ge_i16(pair, 2, INT16_MIN) == -1 && adc_sum_ge_i16(pair, 2, INT16_MAX) == INT16_MAX);
}

int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
