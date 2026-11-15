/* ALGO-B03 demo_bench.c — 조회 방법별 속도 (이 맥, Apple Silicon, -O2)
 *   1 double 모델(log)   2 선형 스캔 + int32 보간   3 이진 탐색 + int32 (ntc_read)
 *   4 이진 탐색 + int64 + div_round (lut_eval)   5 균등 LUT 시프트 (ulut_eval)   6 코드당 1칸 LUT (int16, 4096칸)
 *   입력 두 가지 (고정 시드 xorshift32, 65536개, 141..3996): random = 매번 무작위 코드, walk = 한 걸음 -3..+3 코드
 *   (천천히 변하는 실제 온도 흉내). 한 번 = 64바퀴 = 4,194,304 조회.
 *   준비 1회 + 7회 측정의 중앙값. 결과 합을 volatile 에 넣어 계산이 지워지지 않게 한다. 절대값보다 비율을 볼 것.
 *   표·함수는 solution.c 에서 복사 (파일 하나로 빌드되게).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define _POSIX_C_SOURCE 199309L
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct { const int32_t *x; const int32_t *y; int n; } lut_t;   /* x = 키(ADC 코드 등), y = 값 */

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
    int32_t num = NTC_CENTI[i] * (d - s) + NTC_CENTI[i + 1] * s;   /* <= 12511 * 232 < 2^31 */
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


static double model_centi(int code) {
    double x = (code + 0.5) / 4096.0, r = 10000.0 * x / (1.0 - x);
    return 100.0 * (1.0 / (1.0 / 298.15 + log(r / 10000.0) / 3950.0) - 273.15);
}
static int32_t m_double(uint16_t c) { return (int32_t)lround(model_centi(c)); }
static int32_t m_linear(uint16_t c) {                      /* 선형 스캔: 구간을 앞에서부터 */
    int32_t q = c; int i = 0;
    while (i < NTC_N - 2 && NTC_CODE[i + 1] <= q) i++;
    int32_t d = NTC_CODE[i + 1] - NTC_CODE[i], s = q - NTC_CODE[i];
    int32_t num = NTC_CENTI[i] * (d - s) + NTC_CENTI[i + 1] * s;
    return num >= 0 ? (num + d / 2) / d : -((-num + d / 2) / d);
}
static int32_t m_binary32(uint16_t c) { int32_t v = 0; ntc_read(c, &v); return v; }
static const lut_t NTC = {NTC_CODE, NTC_CENTI, NTC_N};
static int32_t m_binary64(uint16_t c) { return lut_eval(&NTC, c); }
static int32_t m_uniform(uint16_t c) { return ulut_eval(c); }
static int16_t FULL[4096];
static int32_t m_full(uint16_t c) { return FULL[c & 4095]; }

#define NIN 65536
static uint16_t in_rand[NIN], in_walk[NIN];
static volatile int64_t sink;
static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

typedef int32_t (*fn_t)(uint16_t);
static double run(fn_t f, const uint16_t *in) {
    double t[7];
    for (int r = -1; r < 7; r++) {                         /* r = -1 은 준비(warm-up) */
        double t0 = now_ns(); int64_t acc = 0;
        for (int rep = 0; rep < 64; rep++) for (int i = 0; i < NIN; i++) acc += f(in[i]);
        double dt = now_ns() - t0; sink = acc;
        if (r >= 0) t[r] = dt / (64.0 * NIN);
    }
    qsort(t, 7, sizeof t[0], cmpd);
    return t[3];
}

int main(void) {
    uint32_t s = 0x9E3779B9u; int32_t w = 2048;
    for (int i = 0; i < NIN; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        in_rand[i] = (uint16_t)(141 + s % (3996 - 141 + 1));          /* 무작위: 분기 예측이 안 맞는 최악 */
        w += (int32_t)(s % 7) - 3;                                     /* 걸음 -3..+3: 천천히 변하는 실제 센서 흉내 */
        if (w < 141) w = 141;
        if (w > 3996) w = 3996;
        in_walk[i] = (uint16_t)w;
    }
    for (int c = 0; c < 4096; c++) FULL[c] = (int16_t)lround(model_centi(c));   /* 코드마다 정답 (±0.5 centi) */
    struct { const char *name; fn_t f; const char *mem; } m[] = {
        {"double model (log, FPU)", m_double, "0 B (code only)"},
        {"linear scan + int32", m_linear, "272 B"},
        {"binary + int32 (ntc_read)", m_binary32, "272 B"},
        {"binary + int64 (lut_eval)", m_binary64, "272 B"},
        {"uniform shift (ulut_eval)", m_uniform, "248 B"},
        {"full LUT int16[4096]", m_full, "8192 B"},
    };
    int64_t ref = 0; for (int i = 0; i < NIN; i++) ref += m_binary32(in_rand[i]) + m_binary32(in_walk[i]);
    printf("checksum = %lld\n", (long long)ref);
    printf("%-28s %12s %12s   %s\n", "method", "random ns", "walk ns", "table");
    for (int k = 0; k < 6; k++)
        printf("%-28s %12.2f %12.2f   %s\n", m[k].name, run(m[k].f, in_rand), run(m[k].f, in_walk), m[k].mem);
    return 0;
}
