/* ALGO-B03 보정 테이블 조회: 이진 탐색 + 선형 보간
 *   Part A  seg_find (구간 찾기: upper_bound - 1, 오름/내림 자동) · div_round (반올림 나눗셈, 동점은 0에서 먼 쪽)
 *           · lut_eval (양 끝 포화 + int64 정수 선형 보간)
 *   Part B  lut_validate (크기 · 순단조 · 키 폭 검사) + 내림차순 키(역조회) · int32 넘침 · 음수 반올림 대칭 테스트
 *   Part C  ntc_read (12-bit NTC 코드 → centi-°C, 고장/포화 상태, int32 빠른 경로)
 *           · ulut_eval (균등 64코드 간격 LUT: 탐색 없이 시프트로 색인)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   (리눅스는 끝에 -lm)
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { const int32_t *x; const int32_t *y; int n; } lut_t;   /* x = 키(ADC 코드 등), y = 값 */

/* ================= Part A: 구간 찾기 + 반올림 + 보간 ================= */

/* x[0..n-1] 은 순단조(strictly monotone) — 오름차순이든 내림차순이든. n >= 2.
 * 전제: q 는 x[0] 과 x[n-1] 사이(양 끝 포함).
 * 반환: q 를 담는 구간 번호 i (0..n-2).
 *   오름차순: x[i] <= q < x[i+1]   내림차순: x[i] >= q > x[i+1]   q == x[n-1] 이면 n-2. */
int seg_find(const int32_t *x, int n, int32_t q) {
    int asc = x[0] < x[n - 1];
    int lo = 0, hi = n;                 /* 불변식: [0,lo) = q "이전 또는 같음", [hi,n) = q "이후" */
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        int before = asc ? (x[mid] <= q) : (x[mid] >= q);
        if (before) lo = mid + 1; else hi = mid;
    }
    int i = lo - 1;                     /* lo = upper_bound → 그 앞이 구간 시작 */
    if (i > n - 2) i = n - 2;           /* q == x[n-1] (마지막 점 정확히) */
    if (i < 0) i = 0;                   /* 전제 위반 방어 (q 가 첫 점 앞) */
    return i;
}

/* num / den 을 가장 가까운 정수로. 동점(.5)은 0에서 먼 쪽 (lround 와 같은 규칙).
 * 전제: den > 0, |num| <= 2^62.  C 의 '/' 는 0 쪽으로 자른다(C11 6.5.5p6) → 부호를 나눠 처리. */
int64_t div_round(int64_t num, int64_t den) {
    if (num >= 0) return (num + den / 2) / den;
    return -((-num + den / 2) / den);
}

/* 표 밖이면 가까운 끝 값으로 포화, 안이면 선형 보간. 결과 = round_half_away(정확한 보간값 v).
 * 전제: lut_validate(t) == LUT_OK. */
int32_t lut_eval(const lut_t *t, int32_t q) {
    const int32_t *x = t->x, *y = t->y;
    int n = t->n;
    int asc = x[0] < x[n - 1];
    if (asc ? q <= x[0] : q >= x[0]) return y[0];                 /* 첫 끝 포화 (정확히 같아도 y[0]) */
    if (asc ? q >= x[n - 1] : q <= x[n - 1]) return y[n - 1];     /* 마지막 끝 포화 */
    int i = seg_find(x, n, q);
    int64_t d = (int64_t)x[i + 1] - x[i];                         /* int64: 키 차이가 int32 를 넘을 수 있다 */
    int64_t s = (int64_t)q - x[i];
    if (d < 0) { d = -d; s = -s; }                                /* 내림차순 키 → 0 <= s <= d 로 맞춤 */
    int64_t num = (int64_t)y[i] * (d - s) + (int64_t)y[i + 1] * s; /* |num| <= 2^31 * d < 2^62 */
    return (int32_t)div_round(num, d);                            /* 결과는 y[i], y[i+1] 사이 → int32 안 */
}

/* ================= Part B: 표 검사 ================= */

enum { LUT_OK = 0, LUT_ERR_SIZE = -1, LUT_ERR_ORDER = -2, LUT_ERR_SPAN = -3 };

/* n >= 2, 포인터 유효, x 가 한 방향으로 순단조(같은 값 금지 → 0 나눗셈), |x[n-1]-x[0]| <= INT32_MAX. */
int lut_validate(const lut_t *t) {
    if (!t || !t->x || !t->y || t->n < 2) return LUT_ERR_SIZE;
    int asc = t->x[0] < t->x[1];
    for (int i = 0; i + 1 < t->n; i++) {
        if (asc ? !(t->x[i] < t->x[i + 1]) : !(t->x[i] > t->x[i + 1])) return LUT_ERR_ORDER;
    }
    int64_t span = (int64_t)t->x[t->n - 1] - t->x[0];
    if (span < 0) span = -span;
    if (span > INT32_MAX) return LUT_ERR_SPAN;
    return LUT_OK;
}

/* ================= Part C: NTC 서미스터 채널 ================= */
/* 표는 demo_gen_table.c 가 만든 것 (모델: NTC 10k@25°C, B=3950, 고정 10k 위쪽, 12-bit, 코드 중심 (c+0.5)/4096).
 * 5 °C 간격 → 코드 간격은 곡선이 휘는 양 끝에서 촘촘하다. 코드 오름차순 = 온도 내림차순. */
#define NTC_N 34
static const int32_t NTC_CODE[NTC_N] = {
    141, 160, 181, 205, 234, 267, 305, 349, 401, 461, 531, 612, 707, 815, 939, 1081, 1241,
    1419, 1614, 1825, 2048, 2278, 2510, 2738, 2955, 3156, 3337, 3495, 3630, 3741, 3830, 3901, 3955, 3996};
static const int32_t NTC_CENTI[NTC_N] = {
    12511, 11993, 11497, 11007, 10495, 9994, 9497, 9003, 8500, 8001, 7501, 7003, 6498, 6000, 5502, 5000, 4499,
    3999, 3500, 2999, 2499, 2000, 1500, 1000, 501, 1, -499, -999, -1502, -2002, -2500, -3003, -3500, -3999};

#define NTC_SHORT_MAX 15     /* 코드 <= 15  : NTC 가 GND 로 단락 (모델상 > 400 °C) */
#define NTC_OPEN_MIN 4080    /* 코드 >= 4080: NTC 단선 → 풀업으로 끝까지 */
typedef enum { NTC_OK = 0, NTC_HOT_CLAMP, NTC_COLD_CLAMP, NTC_FAULT_SHORT, NTC_FAULT_OPEN } ntc_status_t;

/* 고장이면 *centi 를 건드리지 않는다. 표 밖(정상 범위 밖)이면 끝 값으로 포화 + 상태로 알린다.
 * 보간은 int32 만 쓴다: |centi| * (구간 폭) 의 최댓값이 2^31 보다 작다는 것을 하네스가 확인(lut_i32_bound). */
ntc_status_t ntc_read(uint16_t code, int32_t *centi) {
    if (code <= NTC_SHORT_MAX) return NTC_FAULT_SHORT;
    if (code >= NTC_OPEN_MIN) return NTC_FAULT_OPEN;
    int32_t q = (int32_t)code;
    if (q < NTC_CODE[0]) { *centi = NTC_CENTI[0]; return NTC_HOT_CLAMP; }
    if (q > NTC_CODE[NTC_N - 1]) { *centi = NTC_CENTI[NTC_N - 1]; return NTC_COLD_CLAMP; }
    int i = seg_find(NTC_CODE, NTC_N, q);
    int32_t d = NTC_CODE[i + 1] - NTC_CODE[i], s = q - NTC_CODE[i];
    int32_t num = NTC_CENTI[i] * (d - s) + NTC_CENTI[i + 1] * s;   /* 최대 800822 < 2^31 (lut_i32_bound) */
    *centi = num >= 0 ? (num + d / 2) / d : -((-num + d / 2) / d);
    return NTC_OK;
}

/* 균등 LUT: 코드 128 + 64 j (j = 0..61) 의 모델 온도. 키를 저장하지 않는다 — 색인 = 시프트. */
#define ULUT_C0 128
#define ULUT_SHIFT 6
#define ULUT_N 62
static const int32_t ULUT[ULUT_N] = {
    12916, 11264, 10153, 9320, 8656, 8103, 7630, 7215, 6846, 6513, 6208, 5928,
    5667, 5424, 5194, 4977, 4771, 4574, 4385, 4204, 4029, 3859, 3694, 3534,
    3378, 3225, 3075, 2928, 2783, 2640, 2499, 2359, 2220, 2082, 1944, 1806,
    1669, 1531, 1392, 1252, 1111, 969, 824, 677, 527, 373, 216, 53,
    -115, -290, -473, -665, -868, -1086, -1320, -1577, -1861, -2185, -2563, -3027,
    -3643, -4613};

/* 코드 → centi-°C. 범위 [128, 4032] 밖은 끝 값. 나눗셈 없음: j = off >> 6, f = off & 63, 반올림도 시프트. */
int32_t ulut_eval(uint16_t code) {
    const int32_t step = 1 << ULUT_SHIFT, half = step / 2;
    const int32_t cmax = ULUT_C0 + (ULUT_N - 1) * step;            /* 4032 */
    int32_t c = (int32_t)code;
    if (c <= ULUT_C0) return ULUT[0];
    if (c >= cmax) return ULUT[ULUT_N - 1];
    int32_t off = c - ULUT_C0;
    int32_t j = off >> ULUT_SHIFT, f = off & (step - 1);           /* off >= 0 이라 시프트 안전 */
    int32_t num = ULUT[j] * (step - f) + ULUT[j + 1] * f;
    return num >= 0 ? (num + half) >> ULUT_SHIFT : -((-num + half) >> ULUT_SHIFT);  /* 음수 >> 는 구현 정의 → 부호 분리 */
}

/* ================= 테스트 하네스 (수정하지 않음) ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int32_t rnd_i32(void) { return (int32_t)((int64_t)xs32() - 2147483648LL); }   /* 정의된 변환 */
static int64_t rnd_range(int64_t lo, int64_t hi) {                                     /* [lo, hi] */
    uint64_t r = ((uint64_t)xs32() << 32) | xs32();
    return lo + (int64_t)(r % (uint64_t)(hi - lo + 1));
}

/* 기준 구현 — 풀이와 다른 방법: 선형 스캔 + __int128(GCC/Clang 확장, 테스트 쪽에서만) + 몫/나머지 반올림 */
static int ref_seg(const int32_t *x, int n, int32_t q) {
    int asc = x[0] < x[n - 1];
    for (int i = 0; i < n - 2; i++)
        if (asc ? (q < x[i + 1]) : (q > x[i + 1])) return i;
    return n - 2;
}
static int64_t ref_div(__int128 num, __int128 den) {       /* den > 0, 동점은 0에서 먼 쪽 */
    __int128 q = num / den, r = num % den;
    if (r < 0) r = -r;
    if (2 * r >= den) q += (num < 0) ? -1 : 1;
    return (int64_t)q;
}
static int32_t ref_eval(const lut_t *t, int32_t q) {
    const int32_t *x = t->x, *y = t->y;
    int n = t->n, asc = x[0] < x[n - 1];
    if (asc ? q <= x[0] : q >= x[0]) return y[0];
    if (asc ? q >= x[n - 1] : q <= x[n - 1]) return y[n - 1];
    int i = ref_seg(x, n, q);
    __int128 d = (__int128)x[i + 1] - x[i], s = (__int128)q - x[i], dy = (__int128)y[i + 1] - y[i];
    if (d < 0) { d = -d; s = -s; }
    return (int32_t)ref_div((__int128)y[i] * d + dy * s, d);     /* y0 + dy*s/d 를 한 분수로 */
}
/* 모델(double): 코드 → centi-°C. demo_gen_table.c 와 같은 식 */
static double model_centi(int code) {
    double x = (code + 0.5) / 4096.0, r = 10000.0 * x / (1.0 - x);
    return 100.0 * (1.0 / (1.0 / 298.15 + log(r / 10000.0) / 3950.0) - 273.15);
}
/* int32 빠른 경로의 전제: 모든 구간에서 max(|y0|,|y1|) * 폭 <= INT32_MAX ? 최댓값을 돌려준다 */
static int64_t lut_i32_bound(const lut_t *t) {
    int64_t worst = 0;
    for (int i = 0; i + 1 < t->n; i++) {
        int64_t a = llabs((long long)t->y[i]), b = llabs((long long)t->y[i + 1]);
        int64_t d = llabs((long long)t->x[i + 1] - t->x[i]);
        int64_t p = (a > b ? a : b) * d;
        if (p > worst) worst = p;
    }
    return worst;
}

int main(void) {
    const lut_t NTC = {NTC_CODE, NTC_CENTI, NTC_N};

    /* ---------- Part A ---------- */
    static const int32_t a4[] = {10, 20, 30, 40}, d4[] = {40, 30, 20, 10}, a2[] = {-5, 5};
    CHECK(seg_find(a4, 4, 10) == 0 && seg_find(a4, 4, 15) == 0 && seg_find(a4, 4, 20) == 1 &&
          seg_find(a4, 4, 39) == 2 && seg_find(a4, 4, 40) == 2);
    CHECK(seg_find(d4, 4, 40) == 0 && seg_find(d4, 4, 35) == 0 && seg_find(d4, 4, 30) == 1 &&
          seg_find(d4, 4, 11) == 2 && seg_find(d4, 4, 10) == 2);
    CHECK(seg_find(a2, 2, -5) == 0 && seg_find(a2, 2, 0) == 0 && seg_find(a2, 2, 5) == 0);
    {   /* 무작위 표 1000개(오름/내림 반반) × 범위 안 모든 q: 선형 스캔과 대조 */
        int bad = 0; long nq = 0;
        for (int k = 0; k < 1000; k++) {
            int32_t x[24]; int n = 2 + (int)(xs32() % 22);
            x[0] = (int32_t)rnd_range(-1000, 1000);
            for (int i = 1; i < n; i++) x[i] = x[i - 1] + 1 + (int32_t)(xs32() % 20);
            if (k & 1) for (int i = 0; i < n / 2; i++) { int32_t tmp = x[i]; x[i] = x[n - 1 - i]; x[n - 1 - i] = tmp; }
            int32_t lo = x[0] < x[n - 1] ? x[0] : x[n - 1], hi = x[0] < x[n - 1] ? x[n - 1] : x[0];
            for (int32_t q = lo; q <= hi; q++, nq++) if (seg_find(x, n, q) != ref_seg(x, n, q)) bad++;
        }
        printf("[A] seg_find: 1000 tables, %ld queries, mismatches=%d\n", nq, bad);
        CHECK(bad == 0);
    }
    CHECK(div_round(7, 2) == 4 && div_round(-7, 2) == -4 && div_round(5, 2) == 3 && div_round(-5, 2) == -3);
    CHECK(div_round(4, 3) == 1 && div_round(-4, 3) == -1 && div_round(5, 3) == 2 && div_round(-5, 3) == -2);
    CHECK(div_round(0, 7) == 0 && div_round(1, 3) == 0 && div_round(-1, 3) == 0 && div_round(-1, 2) == -1);
    {   /* 무작위 20만 쌍: 기준 반올림과 대조 + 부호 대칭 f(-a) = -f(a) */
        int bad = 0, asym = 0;
        for (int k = 0; k < 200000; k++) {
            int64_t num = rnd_range(-((INT64_C(1) << 62) - 1), (INT64_C(1) << 62) - 1);   /* 폭 2^63-1: int64 안 */
            int64_t den = (k & 1) ? rnd_range(1, INT32_MAX) : rnd_range(1, 64);
            if (div_round(num, den) != ref_div(num, den)) bad++;
            if (div_round(-num, den) != -div_round(num, den)) asym++;
        }
        printf("[A] div_round: 200000 random pairs, mismatches=%d, sign-asymmetric=%d\n", bad, asym);
        CHECK(bad == 0 && asym == 0);
    }
    {
        static const int32_t x[] = {0, 10}, y[] = {0, 100};
        const lut_t t = {x, y, 2};
        CHECK(lut_eval(&t, 5) == 50 && lut_eval(&t, 1) == 10 && lut_eval(&t, 0) == 0 && lut_eval(&t, 10) == 100);
        CHECK(lut_eval(&t, -3) == 0 && lut_eval(&t, 12) == 100 && lut_eval(&t, INT32_MIN) == 0 && lut_eval(&t, INT32_MAX) == 100);
    }
    {   int bad = 0;
        for (int k = 0; k < NTC_N; k++) if (lut_eval(&NTC, NTC_CODE[k]) != NTC_CENTI[k]) bad++;
        CHECK(bad == 0);                                      /* 표의 점에 정확히 맞으면 표 값 그대로 */
    }
    {   /* 무작위 표 2000개 (키 폭 최대 INT32_MAX, 값 int32 전체, 오름/내림) × 표 점 + 무작위 q 60개 */
        int bad = 0; long nq = 0;
        for (int k = 0; k < 2000; k++) {
            int32_t x[16], y[16]; int n = 2 + (int)(xs32() % 15);
            int64_t maxgap = (int64_t)INT32_MAX / (n - 1);
            if (k % 3 == 0) maxgap = 50;                       /* 작은 폭 → 동점(.5)이 자주 생긴다 */
            x[0] = (int32_t)rnd_range(INT32_MIN / 2, 0);
            for (int i = 1; i < n; i++) x[i] = (int32_t)(x[i - 1] + rnd_range(1, maxgap));
            for (int i = 0; i < n; i++) y[i] = (k % 4 == 0) ? (int32_t)rnd_range(-3, 3) : rnd_i32();
            if (k & 1) for (int i = 0; i < n / 2; i++) {
                int32_t a = x[i], b = y[i]; x[i] = x[n - 1 - i]; y[i] = y[n - 1 - i]; x[n - 1 - i] = a; y[n - 1 - i] = b; }
            const lut_t t = {x, y, n};
            for (int i = 0; i < n; i++, nq++) if (lut_eval(&t, x[i]) != ref_eval(&t, x[i])) bad++;
            int64_t lo = x[0] < x[n - 1] ? x[0] : x[n - 1], hi = x[0] < x[n - 1] ? x[n - 1] : x[0];
            for (int j = 0; j < 60; j++, nq++) {
                int32_t q = (int32_t)rnd_range(lo - 100 < INT32_MIN ? INT32_MIN : lo - 100, hi + 100 > INT32_MAX ? INT32_MAX : hi + 100);
                if (lut_eval(&t, q) != ref_eval(&t, q)) bad++;
            }
        }
        printf("[A] lut_eval: 2000 random tables, %ld queries, mismatches=%d\n", nq, bad);
        CHECK(bad == 0);
    }

    /* ---------- Part B ---------- */
    {
        static const int32_t dup[] = {1, 2, 2, 3}, zig[] = {1, 3, 2}, dsc[] = {5, 3, 1}, yy[] = {0, 0, 0, 0};
        static const int32_t wide[] = {INT32_MIN, 0}, okw[] = {-1073741824, 1073741823};
        const lut_t t_dup = {dup, yy, 4}, t_zig = {zig, yy, 3}, t_dsc = {dsc, yy, 3}, t_one = {dsc, yy, 1};
        const lut_t t_wide = {wide, yy, 2}, t_okw = {okw, yy, 2}, t_null = {NULL, yy, 3};
        CHECK(lut_validate(&NTC) == LUT_OK && lut_validate(&t_dsc) == LUT_OK && lut_validate(&t_okw) == LUT_OK);
        CHECK(lut_validate(&t_one) == LUT_ERR_SIZE && lut_validate(&t_null) == LUT_ERR_SIZE && lut_validate(NULL) == LUT_ERR_SIZE);
        CHECK(lut_validate(&t_dup) == LUT_ERR_ORDER && lut_validate(&t_zig) == LUT_ERR_ORDER);
        CHECK(lut_validate(&t_wide) == LUT_ERR_SPAN);
    }
    {   /* int32 로 곱하면 넘치는 표: 12-bit 코드 → µV (3.3 V 풀스케일). 3300000 * 2048 = 6.8e9 > 2^31 */
        static const int32_t x[] = {0, 4095}, y[] = {0, 3300000};
        const lut_t t = {x, y, 2};
        CHECK(lut_eval(&t, 2048) == 1650403 && lut_eval(&t, 4094) == 3299194 && lut_eval(&t, 1) == 806);
    }
    {   /* 값이 int32 전 범위: 차이 y1 - y0 자체가 int32 를 넘는다. q=500 → 정확히 -0.5 → -1 (0에서 먼 쪽) */
        static const int32_t x[] = {0, 1000}, y[] = {INT32_MIN, INT32_MAX};
        static const int32_t xw[] = {-1073741824, 1073741823}, yr[] = {INT32_MAX, INT32_MIN};
        const lut_t t = {x, y, 2}, tw = {xw, yr, 2};
        CHECK(lut_eval(&t, 500) == -1 && lut_eval(&t, 0) == INT32_MIN && lut_eval(&t, 1000) == INT32_MAX);
        CHECK(lut_eval(&t, 999) == 2143188680 && lut_eval(&tw, 0) == -2 && lut_eval(&tw, 0) == ref_eval(&tw, 0));  /* -1.50000000023 */
    }
    {   /* 역조회: 키 = 온도(내림차순), 값 = 코드. 경보 임계값 85.00 °C 를 ADC 코드로 바꿀 때 */
        const lut_t inv = {NTC_CENTI, NTC_CODE, NTC_N};
        int bad = 0;
        for (int k = 0; k < NTC_N; k++) if (lut_eval(&inv, NTC_CENTI[k]) != NTC_CODE[k]) bad++;
        CHECK(bad == 0 && lut_validate(&inv) == LUT_OK);
        CHECK(lut_eval(&inv, 8500) == 401 && lut_eval(&inv, 2500) == ref_eval(&inv, 2500));
        CHECK(lut_eval(&inv, 20000) == 141 && lut_eval(&inv, -5000) == 3996);   /* 포화 */
        printf("[B] inverse: 85.00 C -> code %d, 25.00 C -> code %d\n", lut_eval(&inv, 8500), lut_eval(&inv, 2500));
    }
    {   /* 0..4095 전부: 값 부호 반전 → 결과 부호 반전 / 표를 뒤집어도(내림차순 키) 같은 결과 / 단조 */
        static int32_t neg[NTC_N], rx[NTC_N], ry[NTC_N];
        for (int k = 0; k < NTC_N; k++) {
            neg[k] = -NTC_CENTI[k]; rx[k] = NTC_CODE[NTC_N - 1 - k]; ry[k] = NTC_CENTI[NTC_N - 1 - k]; }
        const lut_t tneg = {NTC_CODE, neg, NTC_N}, trev = {rx, ry, NTC_N};
        int asym = 0, rev = 0, mono = 0;
        for (int c = 0; c <= 4095; c++) {
            int32_t v = lut_eval(&NTC, c);
            if (lut_eval(&tneg, c) != -v) asym++;
            if (lut_eval(&trev, c) != v) rev++;
            if (c > 0 && v > lut_eval(&NTC, c - 1)) mono++;
        }
        printf("[B] all 4096 codes: sign-asymmetric=%d, reversed-table diffs=%d, monotonic violations=%d\n", asym, rev, mono);
        CHECK(asym == 0);
        CHECK(rev == 0);
        CHECK(mono == 0);
    }

    /* ---------- Part C ---------- */
    {
        int tab = 0;
        for (int k = 0; k < NTC_N; k++) if (fabs(NTC_CENTI[k] - model_centi(NTC_CODE[k])) > 0.5) tab++;
        CHECK(tab == 0 && lut_validate(&NTC) == LUT_OK);                /* 표 = 모델 ±0.5 centi */
        printf("[C] int32 path bound: max |y|*width = %lld (limit %d)\n", (long long)lut_i32_bound(&NTC), INT32_MAX);
        CHECK(lut_i32_bound(&NTC) <= INT32_MAX);
    }
    {
        int32_t v = 0;
        CHECK(ntc_read(2048, &v) == NTC_OK && v == 2499);               /* 표 점 정확히 (24.99 °C) */
        CHECK(ntc_read(141, &v) == NTC_OK && v == 12511 && ntc_read(3996, &v) == NTC_OK && v == -3999);
        CHECK(ntc_read(140, &v) == NTC_HOT_CLAMP && v == 12511 && ntc_read(3997, &v) == NTC_COLD_CLAMP && v == -3999);
        v = 777;
        CHECK(ntc_read(0, &v) == NTC_FAULT_SHORT && ntc_read(15, &v) == NTC_FAULT_SHORT && ntc_read(16, &v) == NTC_HOT_CLAMP);
        v = 777;
        CHECK(ntc_read(4095, &v) == NTC_FAULT_OPEN && v == 777 && ntc_read(4079, &v) == NTC_COLD_CLAMP);
    }
    {   /* 0..4095 전부: 상태 분류, 고장 시 출력 보존, int32 경로 == int64 경로(lut_eval), 모델 대비 최대 오차 */
        int st_bad = 0, keep_bad = 0, path_bad = 0, n_ok = 0, worst_c = -1;
        double worst = 0.0;
        for (int c = 0; c <= 4095; c++) {
            int32_t v = INT32_MIN;
            ntc_status_t st = ntc_read((uint16_t)c, &v);
            ntc_status_t want = c <= NTC_SHORT_MAX ? NTC_FAULT_SHORT : c >= NTC_OPEN_MIN ? NTC_FAULT_OPEN
                              : c < NTC_CODE[0] ? NTC_HOT_CLAMP : c > NTC_CODE[NTC_N - 1] ? NTC_COLD_CLAMP : NTC_OK;
            if (st != want) st_bad++;
            if (want >= NTC_FAULT_SHORT) { if (v != INT32_MIN) keep_bad++; continue; }
            if (v != lut_eval(&NTC, c)) path_bad++;
            if (st == NTC_OK) {
                n_ok++;
                double e = fabs(v - model_centi(c));
                if (e > worst) { worst = e; worst_c = c; }
            }
        }
        printf("[C] ntc_read: %d in-range codes, status mismatches=%d, fault-output-touched=%d, int32!=int64 path=%d\n",
               n_ok, st_bad, keep_bad, path_bad);
        printf("[C] non-uniform %d pts (code+centi as int32 = %zu B): max |err| = %.2f centi @ code %d\n",
               NTC_N, sizeof NTC_CODE + sizeof NTC_CENTI, worst, worst_c);
        CHECK(st_bad == 0 && keep_bad == 0);
        CHECK(path_bad == 0);
        CHECK(n_ok == NTC_CODE[NTC_N - 1] - NTC_CODE[0] + 1 && worst <= 20.0);
    }
    {   /* 균등 LUT: 기준(double 로 /64 — 2의 거듭제곱이라 정확) + 같은 범위에서 모델 대비 오차 */
        int ref_bad = 0, grid_bad = 0, worst_c = -1;
        double worst = 0.0;
        for (int c = 0; c <= 4095; c++) {
            int cc = c < ULUT_C0 ? ULUT_C0 : c > ULUT_C0 + 64 * (ULUT_N - 1) ? ULUT_C0 + 64 * (ULUT_N - 1) : c;
            int j = (cc - ULUT_C0) / 64, f = (cc - ULUT_C0) % 64;
            int32_t want = (j == ULUT_N - 1) ? ULUT[j] : (int32_t)lround((ULUT[j] * (64.0 - f) + ULUT[j + 1] * (double)f) / 64.0);
            int32_t got = ulut_eval((uint16_t)c);
            if (got != want) ref_bad++;
            if (c >= NTC_CODE[0] && c <= NTC_CODE[NTC_N - 1]) {
                double e = fabs(got - model_centi(c));
                if (e > worst) { worst = e; worst_c = c; }
            }
        }
        for (int j = 0; j < ULUT_N; j++) if (ulut_eval((uint16_t)(ULUT_C0 + 64 * j)) != ULUT[j]) grid_bad++;
        printf("[C] uniform %d pts step 64 (centi as int32 = %zu B): max |err| = %.2f centi @ code %d (same code range)\n",
               ULUT_N, sizeof ULUT, worst, worst_c);
        CHECK(ref_bad == 0 && grid_bad == 0);
        CHECK(worst > 0.0 && worst <= 100.0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
