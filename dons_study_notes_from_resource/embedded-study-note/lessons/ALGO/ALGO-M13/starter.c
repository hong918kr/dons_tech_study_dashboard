/* ALGO-M13 누적합과 차분 배열: 1D·2D
 *
 * Part A: 1D 누적합 — 12비트 ADC 샘플(uint16) → uint32 누적합, 부호 있는 값(int32) → int64 누적합,
 *         반열린 구간 [l, r) 의 합과 반올림 평균. 모든 질의 O(1).
 * Part B: 차분 배열 — 구간 증가 여러 번 → 한 번의 누적으로 복원. 전력 타임라인 피크(제자리 차분→누적).
 *         uint16 으로 감긴(wrap) 누적합으로도 길이 ≤ 257 구간의 8비트 합은 정확하다 (모듈러 논증).
 * Part C: 2D 누적합(integral image) — 640×480 프레임(8/12비트), 직사각형 합(포함–배제),
 *         자동 노출(AE) 존 평균, 적응 이진화(Bradley–Roth).
 *
 * 테스트: 브루트포스(uint64 로 직접 더하기)와 xorshift32 고정 시드 랜덤 대조 + 작은 크기 전수 +
 *         LeetCode 303 / 1109 예제 + 넘침 경계값(1,048,832 · 257 · 78,336,000 · 1,257,984,000).
 * 규칙: 알고리즘 안에서 malloc 없음 — 버퍼는 호출자가 준다(테스트는 static 배열).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- Part A: 1D 누적합 ---------------- */

/* P[0] = 0, P[i+1] = a[0] + … + a[i]  (P 는 n+1 칸). uint32 덧셈은 넘치면 mod 2^32 로 감긴다(정의된 동작). */
static void prefix_build_u16(const uint16_t *a, size_t n, uint32_t *P) {
    (void)a; (void)n;
    P[0] = 0; /* TODO: P[i+1] = P[i] + a[i] */
}

/* 반열린 구간 [l, r) 의 합, 0 <= l <= r <= n. 진짜 합 < 2^32 이면 P 가 감겼어도 정확하다. */
static uint32_t range_sum_u32(const uint32_t *P, size_t l, size_t r) {
    (void)P; (void)l; (void)r;
    return 0; /* TODO: 두 칸 빼기 */
}

/* [l, r) 의 평균을 반올림(.5 는 올림). 빈 구간은 0. 합 + len/2 는 uint32 를 넘을 수 있어 uint64 로. */
static uint16_t range_mean_u16(const uint32_t *P, size_t l, size_t r) {
    (void)P; (void)l; (void)r;
    return 0; /* TODO: 빈 구간 처리 + (합 + len/2) / len — 덧셈은 uint64 로 */
}

/* 부호 있는 값: int32 를 int64 에 누적 (n < 2^32 이면 |P| < 2^63 이라 넘침 없음) */
static void prefix_build_i32(const int32_t *a, size_t n, int64_t *P) {
    (void)a; (void)n;
    P[0] = 0; /* TODO: int64 에 누적 */
}

static int64_t range_sum_i64(const int64_t *P, size_t l, size_t r) {
    (void)P; (void)l; (void)r;
    return 0; /* TODO */
}

/* ---------------- Part B: 차분 배열 ---------------- */

/* D 는 n+1 칸. [l, r) 에 v 를 더하는 갱신을 O(1) 로 기록한다. l >= r 또는 r > n 이면 무시하고 false. */
static bool diff_add(int32_t *D, size_t n, size_t l, size_t r, int32_t v) {
    (void)D; (void)n; (void)l; (void)r; (void)v;
    return false; /* TODO: 시작에 +v, 끝(r)에 −v. 빈/잘못된 구간은 false */
}

/* 기록된 갱신을 한 번에 반영: a[i] += D[0] + … + D[i] */
static void diff_apply(const int32_t *D, size_t n, int32_t *a) {
    (void)D; (void)n; (void)a; /* TODO: D 의 누적합을 a 에 더하기 */
}

/* 전력 타임라인: 부하 k 는 [t0, t1) ms 동안 mA 를 끈다. buf 는 T+1 칸 — 차분 배열로 쓰고 제자리에서 누적.
 * 끝나면 buf[t] = 시각 t 의 총 전류 (t < T). t1 > T 는 T 로 자르고, t0 >= t1 인 부하는 무시.
 * 반환: 최대 전류 (T == 0 이면 0), *t_at = 처음으로 최대가 된 시각. */
typedef struct { uint16_t t0, t1; int16_t mA; } load_t;

static int32_t power_peak(const load_t *L, size_t m, size_t T, int32_t *buf, size_t *t_at) {
    (void)L; (void)m; (void)T; (void)buf;
    *t_at = 0;
    return 0; /* TODO: buf 를 0 → 부하마다 diff_add(t1 은 T 로 자름) → 제자리 누적하며 최대값과 첫 시각 */
}

/* 8비트 샘플을 uint16 누적합에 감아(wrap) 저장 — 메모리 절반. P16 은 n+1 칸. */
static void prefix_build_u8_wrap16(const uint8_t *a, size_t n, uint16_t *P16) {
    (void)a; (void)n;
    P16[0] = 0; /* TODO: uint16 로 감기며 누적 */
}

/* 진짜 합 <= 65535 (길이 <= 257 이면 보장) 일 때 정확. uint16 - uint16 은 int 로 승격되므로 다시 잘라야 한다. */
static uint16_t range_sum_wrap16(const uint16_t *P16, size_t l, size_t r) {
    (void)P16; (void)l; (void)r;
    return 0; /* TODO: 빼고 uint16 로 자르기 */
}

/* ---------------- Part C: 2D 누적합 (integral image) ---------------- */

#define MAX_W 640
#define MAX_H 480

/* I 는 (w+1)×(h+1), 행 우선, 폭 W = w+1. I[y][x] = img 의 [0, x) × [0, y) 직사각형 합.
 * 픽셀은 uint16 (8비트 또는 RAW12). 640×480×4095 = 1,257,984,000 < 2^32 → uint32 로 넘침 없음. */
static void integral_build(const uint16_t *img, size_t w, size_t h, uint32_t *I) {
    (void)img; (void)w; (void)h;
    I[0] = 0; /* TODO: 0 행·0 열은 0, 나머지는 위 칸 + 이 행의 누적 */
}

/* [x0, x1) × [y0, y1) 의 합 — 포함–배제 4번 읽기. uint32 연산이라 중간값이 "음수"로 감겨도 결과는 정확. */
static uint32_t rect_sum(const uint32_t *I, size_t w, size_t x0, size_t y0, size_t x1, size_t y1) {
    (void)I; (void)w; (void)x0; (void)y0; (void)x1; (void)y1;
    return 0; /* TODO: 네 모서리 포함–배제 */
}

/* 자동 노출(AE) 측광: 프레임을 zx × zy 존으로 나눠 존마다 반올림 평균. 존 경계 x_i = i·w/zx (나누어떨어지지 않아도 전부 덮음). */
static void ae_zone_means(const uint32_t *I, size_t w, size_t h, size_t zx, size_t zy, uint16_t *mean) {
    (void)I; (void)w; (void)h;
    memset(mean, 0, zx * zy * sizeof *mean); /* TODO: 존마다 rect_sum / 넓이 (반올림, 넓이 0 이면 0) */
}

/* Bradley–Roth 적응 이진화: 픽셀 주변 (2r+1)² 창(테두리에서는 잘린 창)의 평균보다 t% 이상 어두우면 0, 아니면 255.
 * 정수식: img·cnt·100 <= sum·(100 − t) 이면 0. 나눗셈 없음. */
static void adaptive_threshold(const uint16_t *img, const uint32_t *I, size_t w, size_t h,
                               size_t r, uint32_t t_pct, uint8_t *out) {
    (void)img; (void)I; (void)r; (void)t_pct;
    memset(out, 255, w * h); /* TODO: 잘린 창 [x−r, x+r] × [y−r, y+r] 의 합과 픽셀 수로 비교 */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 브루트포스 기준: 매번 uint64 / int64 로 직접 더한다 (누적합을 쓰지 않는 다른 길) */
static uint64_t bf_sum_u16(const uint16_t *a, size_t l, size_t r) { uint64_t s = 0; for (size_t i = l; i < r; i++) s += a[i]; return s; }
static int64_t bf_sum_i32(const int32_t *a, size_t l, size_t r) { int64_t s = 0; for (size_t i = l; i < r; i++) s += a[i]; return s; }
static uint64_t bf_rect(const uint16_t *img, size_t w, size_t x0, size_t y0, size_t x1, size_t y1) {
    uint64_t s = 0;
    for (size_t y = y0; y < y1; y++) for (size_t x = x0; x < x1; x++) s += img[y * w + x];
    return s;
}
static uint8_t bf_thresh_px(const uint16_t *img, size_t w, size_t h, size_t r, uint32_t t, size_t x, size_t y) {
    size_t y0 = y >= r ? y - r : 0, y1 = y + r + 1 < h ? y + r + 1 : h;
    size_t x0 = x >= r ? x - r : 0, x1 = x + r + 1 < w ? x + r + 1 : w;
    uint64_t s = bf_rect(img, w, x0, y0, x1, y1), cnt = (uint64_t)(x1 - x0) * (y1 - y0);
    return ((uint64_t)img[y * w + x] * cnt * 100u <= s * (100u - t)) ? 0 : 255;
}

#define NBIG 1100000u           /* 1D 넘침 경계 실험: 1.1 M 샘플 */
#define NMAX12 1048832u         /* floor((2^32 − 1) / 4095): 12비트 샘플이 uint32 합에 들어가는 최대 개수 */
static uint16_t g_a16[NBIG];
static uint32_t g_P[NBIG + 1];
static uint64_t g_P64[NBIG + 1];
static int32_t g_a32[2048], g_b32[2048];
static int64_t g_Pi[2049];
static int32_t g_D[2049], g_cur[2049];
static uint8_t g_a8[8192];
static uint16_t g_P16[8193];
static uint16_t g_img[MAX_W * MAX_H];
static uint32_t g_I[(MAX_W + 1) * (MAX_H + 1)];
static uint8_t g_out[MAX_W * MAX_H];

int main(void) {
    uint32_t seed = 0x13579BDFu;

    /* ===== Part A: 1D 누적합 ===== */
    prefix_build_u16(g_a16, 0, g_P);
    CHECK(g_P[0] == 0 && range_sum_u32(g_P, 0, 0) == 0);           /* 빈 배열 */
    CHECK(range_mean_u16(g_P, 0, 0) == 0);                          /* 빈 구간 평균 = 0 (0 으로 나누지 않음) */

    int bad_a = 0, n_q = 0;
    for (int t = 0; t < 300; t++) {
        size_t n = (size_t)(xs32(&seed) % 301u);
        for (size_t i = 0; i < n; i++) g_a16[i] = (uint16_t)(t % 5 == 0 ? 4095 : xs32(&seed) % 4096u);
        prefix_build_u16(g_a16, n, g_P);
        if (n <= 40) {                                              /* 작은 n: 모든 (l, r) 전수 */
            for (size_t l = 0; l <= n; l++) for (size_t r = l; r <= n; r++, n_q++)
                bad_a += range_sum_u32(g_P, l, r) != bf_sum_u16(g_a16, l, r);
        } else {
            for (int q = 0; q < 2000; q++, n_q++) {
                size_t l = xs32(&seed) % (n + 1), r = xs32(&seed) % (n + 1);
                if (l > r) { size_t tmp = l; l = r; r = tmp; }
                bad_a += range_sum_u32(g_P, l, r) != bf_sum_u16(g_a16, l, r);
                if (r > l) { uint64_t s = bf_sum_u16(g_a16, l, r), len = r - l;
                             bad_a += range_mean_u16(g_P, l, r) != (s + len / 2) / len; n_q++; }
            }
        }
    }
    printf("A: u16 prefix sum/mean vs brute force: %d checks, mismatches=%d\n", n_q, bad_a);
    CHECK(bad_a == 0);

    /* 반올림 평균 손 계산: {100, 101} → 100.5 → 101,  {100, 100, 101} → 100.33 → 100 */
    g_a16[0] = 100; g_a16[1] = 101; g_a16[2] = 100;
    prefix_build_u16(g_a16, 3, g_P);
    CHECK(range_mean_u16(g_P, 0, 2) == 101 && range_mean_u16(g_P, 0, 3) == 100);

    /* LeetCode 303 예제: [-2, 0, 3, -5, 2, -1], 포함 구간 (0,2)=1 (2,5)=-1 (0,5)=-3 → 반열린 [0,3) [2,6) [0,6) */
    const int32_t lc303[6] = { -2, 0, 3, -5, 2, -1 };
    prefix_build_i32(lc303, 6, g_Pi);
    CHECK(range_sum_i64(g_Pi, 0, 3) == 1 && range_sum_i64(g_Pi, 2, 6) == -1 && range_sum_i64(g_Pi, 0, 6) == -3);

    /* int32 극값: 합이 int32 를 한참 넘어도 int64 누적은 정확 */
    for (size_t i = 0; i < 2048; i++) g_a32[i] = (i & 1) ? INT32_MIN : INT32_MAX;
    prefix_build_i32(g_a32, 2048, g_Pi);
    CHECK(range_sum_i64(g_Pi, 0, 2048) == -1024);                   /* (2^31 − 1 − 2^31) × 1024 */
    CHECK(range_sum_i64(g_Pi, 0, 1) == INT32_MAX && range_sum_i64(g_Pi, 1, 2) == INT32_MIN);
    for (size_t i = 0; i < 2048; i++) g_a32[i] = INT32_MAX;
    prefix_build_i32(g_a32, 2048, g_Pi);
    CHECK(range_sum_i64(g_Pi, 0, 2048) == (int64_t)INT32_MAX * 2048);
    int bad_i = 0;
    for (int t = 0; t < 200; t++) {
        size_t n = 1 + (size_t)(xs32(&seed) % 2048u);
        for (size_t i = 0; i < n; i++) g_a32[i] = (int32_t)xs32(&seed);   /* 전 범위 무작위 */
        prefix_build_i32(g_a32, n, g_Pi);
        for (int q = 0; q < 200; q++) {
            size_t l = xs32(&seed) % (n + 1), r = xs32(&seed) % (n + 1);
            if (l > r) { size_t tmp = l; l = r; r = tmp; }
            bad_i += range_sum_i64(g_Pi, l, r) != bf_sum_i32(g_a32, l, r);
        }
    }
    printf("A: i32 prefix (int64) vs brute force: 40000 queries, mismatches=%d\n", bad_i);
    CHECK(bad_i == 0);

    /* uint32 넘침 경계: 1.1 M 개 4095 → P 는 감긴다. 길이 <= 1,048,832 구간은 그래도 정확, 그보다 길면 틀린다 */
    for (size_t i = 0; i < NBIG; i++) g_a16[i] = 4095;
    prefix_build_u16(g_a16, NBIG, g_P);
    CHECK(g_P[NBIG] == (uint32_t)(4095ull * NBIG));                 /* 전체 합 4,504,500,000 → mod 2^32 = 209,532,704 */
    CHECK(range_sum_u32(g_P, 0, NMAX12) == 4095u * NMAX12);         /* 4,294,967,040: 딱 들어감 */
    CHECK(range_sum_u32(g_P, 51167, 51167 + NMAX12) == 4095u * NMAX12);   /* P[r] 만 감겨도 정확 */
    CHECK(range_sum_u32(g_P, 0, NMAX12 + 1) == 3839u);              /* 4,294,971,135 − 2^32 = 3839: 한 칸 넘으면 틀림 */
    CHECK(range_mean_u16(g_P, 0, NMAX12) == 4095);                  /* 합 + len/2 = 4,295,491,456 > 2^32: uint32 로 더하면 0 */
    int bad_w = 0;                                                  /* 값 3900..4095: 총합 > 2^32 이 되게 */
    g_P64[0] = 0;
    for (size_t i = 0; i < NBIG; i++) { g_a16[i] = (uint16_t)(3900u + xs32(&seed) % 196u); g_P64[i + 1] = g_P64[i] + g_a16[i]; }
    prefix_build_u16(g_a16, NBIG, g_P);
    for (int q = 0; q < 100000; q++) {
        size_t len = xs32(&seed) % (NMAX12 + 1), l = xs32(&seed) % (NBIG - len + 1);
        bad_w += (uint64_t)range_sum_u32(g_P, l, l + len) != g_P64[l + len] - g_P64[l];
    }
    printf("A: wrapped u32 prefix (n=%u, total=%llu > 2^32): 100000 queries len<=%u, mismatches=%d\n",
           NBIG, (unsigned long long)g_P64[NBIG], NMAX12, bad_w);
    CHECK(bad_w == 0);

    /* ===== Part B: 차분 배열 ===== */
    /* LeetCode 1109 예제: n=5, [[1,2,10],[2,3,20],[2,5,25]] (1-기준 포함 구간) → [10,55,45,25,25] */
    memset(g_D, 0, sizeof g_D); memset(g_b32, 0, sizeof g_b32);
    diff_add(g_D, 5, 0, 2, 10); diff_add(g_D, 5, 1, 3, 20); diff_add(g_D, 5, 1, 5, 25);
    diff_apply(g_D, 5, g_b32);
    CHECK(g_b32[0] == 10 && g_b32[1] == 55 && g_b32[2] == 45 && g_b32[3] == 25 && g_b32[4] == 25);
    CHECK(!diff_add(g_D, 5, 3, 3, 7) && !diff_add(g_D, 5, 4, 2, 7) && !diff_add(g_D, 5, 2, 6, 7));  /* 빈·뒤집힌·밖 */
    CHECK(g_D[3] == -20 && g_D[4] == 0 && g_D[5] == -25);           /* 무시된 갱신은 D 를 건드리지 않는다 */

    int bad_ex = 0;                                                 /* 전수: n = 1..10, 모든 (l, r) 단일 갱신 */
    for (size_t n = 1; n <= 10; n++)
        for (size_t l = 0; l <= n; l++) for (size_t r = l; r <= n; r++) {
            memset(g_D, 0, sizeof(int32_t) * (n + 1));
            for (size_t i = 0; i < n; i++) g_a32[i] = g_b32[i] = (int32_t)i;
            diff_add(g_D, n, l, r, 3);
            diff_apply(g_D, n, g_a32);
            for (size_t i = l; i < r; i++) g_b32[i] += 3;
            bad_ex += memcmp(g_a32, g_b32, sizeof(int32_t) * n) != 0;
        }
    CHECK(bad_ex == 0);

    int bad_d = 0;
    for (int t = 0; t < 400; t++) {
        size_t n = 1 + (size_t)(xs32(&seed) % 200u), m = (size_t)(xs32(&seed) % 301u);
        memset(g_D, 0, sizeof(int32_t) * (n + 1));
        for (size_t i = 0; i < n; i++) g_a32[i] = g_b32[i] = (int32_t)(xs32(&seed) % 2001u) - 1000;
        for (size_t k = 0; k < m; k++) {
            size_t l = xs32(&seed) % (n + 1), r = (k % 7 == 0) ? n : xs32(&seed) % (n + 1);   /* r == n 자주 */
            if (l > r) { size_t tmp = l; l = r; r = tmp; }
            int32_t v = (int32_t)(xs32(&seed) % 2001u) - 1000;
            diff_add(g_D, n, l, r, v);
            for (size_t i = l; i < r; i++) g_b32[i] += v;              /* 브루트포스: O(r − l) */
        }
        diff_apply(g_D, n, g_a32);
        bad_d += memcmp(g_a32, g_b32, sizeof(int32_t) * n) != 0;
    }
    printf("B: difference array vs brute force: 400 arrays (n<=200, <=300 updates), mismatches=%d\n", bad_d);
    CHECK(bad_d == 0);

    /* 전력 타임라인 손 계산 (ms 0..9): MCU 5 mA 상시, 무선 TX 120 mA [2,5), IMU 30 mA [4,8), 모터 80 mA [7,9) */
    const load_t loads[4] = { { 0, 10, 5 }, { 2, 5, 120 }, { 4, 8, 30 }, { 7, 9, 80 } };
    size_t tp = 99;
    CHECK(power_peak(loads, 4, 10, g_cur, &tp) == 155 && tp == 4);
    const int32_t want[10] = { 5, 5, 125, 125, 155, 35, 35, 115, 85, 5 };
    CHECK(memcmp(g_cur, want, sizeof want) == 0);
    CHECK(power_peak(loads, 4, 0, g_cur, &tp) == 0 && tp == 0);     /* 빈 타임라인 */
    const load_t clip[2] = { { 3, 60000, 50 }, { 6, 6, 999 } };      /* t1 > T 는 자르고, t0 == t1 은 무시 */
    CHECK(power_peak(clip, 2, 8, g_cur, &tp) == 50 && tp == 3 && g_cur[7] == 50 && g_cur[2] == 0);
    int bad_p = 0;
    for (int t = 0; t < 300; t++) {
        size_t T = 1 + (size_t)(xs32(&seed) % 2000u), m = (size_t)(xs32(&seed) % 65u);
        load_t L[64];
        for (size_t k = 0; k < m; k++) {
            L[k].t0 = (uint16_t)(xs32(&seed) % (T + 10)); L[k].t1 = (uint16_t)(xs32(&seed) % (T + 10));
            L[k].mA = (int16_t)(xs32(&seed) % 501u);
        }
        int32_t bf_best = 0; size_t bf_t = 0;
        for (size_t s = 0; s < T; s++) {                            /* 브루트포스: 시각마다 모든 부하 검사 O(T·m) */
            int32_t c = 0;
            for (size_t k = 0; k < m; k++) if (L[k].t0 <= s && s < L[k].t1) c += L[k].mA;
            if (s == 0 || c > bf_best) { bf_best = c; bf_t = s; }
        }
        bad_p += power_peak(L, m, T, g_cur, &tp) != bf_best || tp != bf_t;
    }
    printf("B: power_peak vs brute force: 300 timelines (T<=2000 ms, <=64 loads), mismatches=%d\n", bad_p);
    CHECK(bad_p == 0);

    /* uint16 로 감긴 누적합: 8192 개 8비트 샘플(총합 ~1 M, 16번 이상 감김), 길이 <= 257 구간은 전부 정확 */
    for (size_t i = 0; i < 8192; i++) g_a8[i] = (uint8_t)xs32(&seed);
    prefix_build_u8_wrap16(g_a8, 8192, g_P16);
    int bad16 = 0, n16 = 0;
    for (size_t l = 0; l + 257 <= 8192; l++, n16++) {               /* 길이 257 은 모든 시작점 */
        uint64_t s = 0; for (size_t i = l; i < l + 257; i++) s += g_a8[i];
        bad16 += range_sum_wrap16(g_P16, l, l + 257) != s;
    }
    for (int q = 0; q < 20000; q++, n16++) {
        size_t len = xs32(&seed) % 258u, l = xs32(&seed) % (8192 - len + 1);
        uint64_t s = 0; for (size_t i = l; i < l + len; i++) s += g_a8[i];
        bad16 += range_sum_wrap16(g_P16, l, l + len) != s;
    }
    printf("B: wrap16 prefix of 8-bit samples: %d windows (len<=257), mismatches=%d\n", n16, bad16);
    CHECK(bad16 == 0);
    memset(g_a8, 0xFF, 600);                                        /* 최악: 전부 255 */
    prefix_build_u8_wrap16(g_a8, 600, g_P16);
    CHECK(range_sum_wrap16(g_P16, 100, 357) == 65535);              /* 255 × 257 = 65535: 경계 딱 */
    CHECK(range_sum_wrap16(g_P16, 100, 358) == 254);                /* 255 × 258 = 65790 → mod 65536 = 254: 경계 밖 */

    /* ===== Part C: integral image ===== */
    /* 7×5 프레임, 12비트, 모든 직사각형(빈 것 포함) 전수: (8·9/2)·(6·7/2) = 36·21 = 756 개 */
    for (size_t i = 0; i < 35; i++) g_img[i] = (uint16_t)(xs32(&seed) % 4096u);
    integral_build(g_img, 7, 5, g_I);
    int bad_r = 0, n_r = 0;
    for (size_t y0 = 0; y0 <= 5; y0++) for (size_t y1 = y0; y1 <= 5; y1++)
        for (size_t x0 = 0; x0 <= 7; x0++) for (size_t x1 = x0; x1 <= 7; x1++, n_r++)
            bad_r += rect_sum(g_I, 7, x0, y0, x1, y1) != bf_rect(g_img, 7, x0, y0, x1, y1);
    CHECK(n_r == 756 && bad_r == 0);
    integral_build(g_img, 5, 0, g_I);                               /* 높이 0 */
    CHECK(rect_sum(g_I, 5, 0, 0, 5, 0) == 0);
    integral_build(g_img, 0, 4, g_I);                               /* 폭 0 */
    CHECK(rect_sum(g_I, 0, 0, 0, 0, 4) == 0);

    for (int t = 0; t < 60; t++) {                                  /* 작은 랜덤 프레임 60개 × 직사각형 300개 */
        size_t w = 1 + xs32(&seed) % 48u, h = 1 + xs32(&seed) % 48u;
        for (size_t i = 0; i < w * h; i++) g_img[i] = (uint16_t)(xs32(&seed) % 4096u);
        integral_build(g_img, w, h, g_I);
        for (int q = 0; q < 300; q++, n_r++) {
            size_t x0 = xs32(&seed) % (w + 1), x1 = xs32(&seed) % (w + 1), y0 = xs32(&seed) % (h + 1), y1 = xs32(&seed) % (h + 1);
            if (x0 > x1) { size_t tmp = x0; x0 = x1; x1 = tmp; }
            if (y0 > y1) { size_t tmp = y0; y0 = y1; y1 = tmp; }
            bad_r += rect_sum(g_I, w, x0, y0, x1, y1) != bf_rect(g_img, w, x0, y0, x1, y1);
        }
    }
    printf("C: rect_sum vs brute force (small frames): %d rects, mismatches=%d\n", n_r, bad_r);
    CHECK(bad_r == 0);

    /* 640×480 8비트 랜덤 프레임: 전체 합 + 랜덤 직사각형 300개 */
    const size_t W = MAX_W, H = MAX_H;
    for (size_t i = 0; i < W * H; i++) g_img[i] = (uint16_t)(xs32(&seed) & 0xFFu);
    integral_build(g_img, W, H, g_I);
    CHECK(rect_sum(g_I, W, 0, 0, W, H) == bf_rect(g_img, W, 0, 0, W, H));
    int bad_big = 0;
    for (int q = 0; q < 300; q++) {
        size_t x0 = xs32(&seed) % (W + 1), x1 = xs32(&seed) % (W + 1), y0 = xs32(&seed) % (H + 1), y1 = xs32(&seed) % (H + 1);
        if (x0 > x1) { size_t tmp = x0; x0 = x1; x1 = tmp; }
        if (y0 > y1) { size_t tmp = y0; y0 = y1; y1 = tmp; }
        bad_big += rect_sum(g_I, W, x0, y0, x1, y1) != bf_rect(g_img, W, x0, y0, x1, y1);
    }
    printf("C: 640x480 8-bit frame: 300 random rects, mismatches=%d\n", bad_big);
    CHECK(bad_big == 0);

    /* AE 존 8×6 (80×80 존) 평균 vs 브루트포스, 존 합 = 전체 합 */
    uint16_t zm[8 * 6];
    ae_zone_means(g_I, W, H, 8, 6, zm);
    int bad_z = 0; uint64_t zsum = 0;
    for (size_t j = 0; j < 6; j++) for (size_t i = 0; i < 8; i++) {
        uint64_t s = bf_rect(g_img, W, i * 80, j * 80, (i + 1) * 80, (j + 1) * 80);
        zsum += s;
        bad_z += zm[j * 8 + i] != (s + 3200) / 6400;
    }
    CHECK(bad_z == 0 && zsum == rect_sum(g_I, W, 0, 0, W, H));
    /* 나누어떨어지지 않는 존: 100×75 프레임을 7×5 존으로, 그리고 존이 픽셀보다 많은 경우 (w=3, zx=5) */
    for (size_t i = 0; i < 100 * 75; i++) g_img[i] = (uint16_t)(xs32(&seed) % 4096u);
    integral_build(g_img, 100, 75, g_I);
    uint16_t zm2[7 * 5];
    ae_zone_means(g_I, 100, 75, 7, 5, zm2);
    for (size_t j = 0; j < 5; j++) for (size_t i = 0; i < 7; i++) {
        size_t x0 = i * 100 / 7, x1 = (i + 1) * 100 / 7, y0 = j * 75 / 5, y1 = (j + 1) * 75 / 5;
        uint64_t a = (x1 - x0) * (y1 - y0), s = bf_rect(g_img, 100, x0, y0, x1, y1);
        bad_z += zm2[j * 7 + i] != (s + a / 2) / a;
    }
    CHECK(bad_z == 0);
    integral_build(g_img, 3, 2, g_I);
    uint16_t zm3[5];
    ae_zone_means(g_I, 3, 2, 5, 1, zm3);
    CHECK(zm3[0] == 0 && zm3[2] == 0 && zm3[1] == (uint16_t)((g_img[0] + g_img[3] + 1) / 2));   /* 폭 0 존 → 0 */

    /* 최악 값: 전부 255 / 전부 4095 인 640×480 — 27비트 / 31비트 */
    for (size_t i = 0; i < W * H; i++) g_img[i] = 255;
    integral_build(g_img, W, H, g_I);
    CHECK(rect_sum(g_I, W, 0, 0, W, H) == 78336000u);
    for (size_t i = 0; i < W * H; i++) g_img[i] = 4095;
    integral_build(g_img, W, H, g_I);
    CHECK(rect_sum(g_I, W, 0, 0, W, H) == 1257984000u);
    CHECK(rect_sum(g_I, W, 1, 1, W, H) == 4095u * 639u * 479u);

    /* 적응 이진화: 320×240 — 왼→오른쪽으로 밝아지는 조명 + 어두운 획(글자 흉내) + 잡음. 전 픽셀 브루트포스 대조 */
    size_t w2 = 320, h2 = 240;
    for (size_t y = 0; y < h2; y++) for (size_t x = 0; x < w2; x++) {
        uint32_t base = 40 + (uint32_t)x * 180 / w2;                /* 조명 그라데이션 */
        bool stroke = ((x / 6) % 5 == 0 && (y / 30) % 2 == 0) || (y % 37 < 3);
        uint32_t v = stroke ? base / 3 : base;
        g_img[y * w2 + x] = (uint16_t)(v + xs32(&seed) % 9u);
    }
    integral_build(g_img, w2, h2, g_I);
    adaptive_threshold(g_img, g_I, w2, h2, 7, 15, g_out);
    int bad_t = 0; size_t dark = 0, dark_stroke = 0, n_stroke = 0;
    for (size_t y = 0; y < h2; y++) for (size_t x = 0; x < w2; x++) {
        bad_t += g_out[y * w2 + x] != bf_thresh_px(g_img, w2, h2, 7, 15, x, y);
        bool stroke = ((x / 6) % 5 == 0 && (y / 30) % 2 == 0) || (y % 37 < 3);
        dark += g_out[y * w2 + x] == 0;
        n_stroke += stroke;
        dark_stroke += stroke && g_out[y * w2 + x] == 0;
    }
    printf("C: adaptive threshold 320x240 r=7 t=15%%: mismatches=%d, dark=%zu, stroke px=%zu, stroke px marked dark=%zu\n",
           bad_t, dark, n_stroke, dark_stroke);
    CHECK(bad_t == 0);
    CHECK(dark_stroke * 100 >= n_stroke * 95);                      /* 그라데이션이 있어도 획의 95% 이상을 잡는다 */

    /* 640×480 12비트 잡음 프레임, r = 15: 무작위 3000 픽셀만 브루트포스 대조 */
    for (size_t i = 0; i < W * H; i++) g_img[i] = (uint16_t)(xs32(&seed) % 4096u);
    integral_build(g_img, W, H, g_I);
    adaptive_threshold(g_img, g_I, W, H, 15, 10, g_out);
    int bad_t2 = 0;
    for (int q = 0; q < 3000; q++) {
        size_t x = q < 4 ? (q & 1) * (W - 1) : xs32(&seed) % W, y = q < 4 ? (size_t)(q >> 1) * (H - 1) : xs32(&seed) % H;  /* 네 모서리 포함 */
        bad_t2 += g_out[y * W + x] != bf_thresh_px(g_img, W, H, 15, 10, x, y);
    }
    CHECK(bad_t2 == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
