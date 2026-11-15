/* ALGO-B03 demo_error.c — 모델(double) 대비 오차를 구간별로, 그리고 코드별 CSV 로
 *   표·함수는 solution.c 의 것을 그대로 복사했다 (파일 하나로 빌드되게).
 *   구간별: 측정 최대 |오차| (정수 결과 - 모델), 순수 보간 오차(모델 노드를 double 로 직선 연결), 이론 상한 h^2/8 * max|f''|
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_error.c -o demo_error && ./demo_error [csv]
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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


static double model_centi(double code) {
    double x = (code + 0.5) / 4096.0, r = 10000.0 * x / (1.0 - x);
    return 100.0 * (1.0 / (1.0 / 298.15 + log(r / 10000.0) / 3950.0) - 273.15);
}
static double f2(double c) { return model_centi(c + 1) - 2 * model_centi(c) + model_centi(c - 1); }  /* f'' (코드 단위) */

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "csv") == 0) {           /* 그림용: code, 비균등 오차, 균등 오차 (부호 있음) */
        printf("code,err_nonuniform,err_uniform\n");
        for (int c = NTC_CODE[0]; c <= NTC_CODE[NTC_N - 1]; c++) {
            int32_t v; ntc_read((uint16_t)c, &v);
            printf("%d,%.3f,%.3f\n", c, v - model_centi(c), ulut_eval((uint16_t)c) - model_centi(c));
        }
        return 0;
    }
    printf("seg  codes        h   T range (C)      measured  interp-only  bound h^2/8*max|f''|  (centi-C)\n");
    double gm = 0, gi = 0;
    for (int i = 0; i + 1 < NTC_N; i++) {
        int a = NTC_CODE[i], b = NTC_CODE[i + 1], h = b - a;
        double ya = model_centi(a), yb = model_centi(b), meas = 0, interp = 0, fmax = 0;
        for (int c = a; c <= b; c++) {
            int32_t v; ntc_read((uint16_t)c, &v);
            double m = model_centi(c), lin = ya + (yb - ya) * (c - a) / h;
            if (fabs(v - m) > meas) meas = fabs(v - m);
            if (fabs(lin - m) > interp) interp = fabs(lin - m);
            if (fabs(f2(c)) > fmax) fmax = fabs(f2(c));
        }
        if (meas > gm) gm = meas;
        if (interp > gi) gi = interp;
        printf("%2d  %4d..%4d  %3d  %6.2f..%6.2f   %7.2f   %7.2f      %7.2f\n", i, a, b, h,
               NTC_CENTI[i] / 100.0, NTC_CENTI[i + 1] / 100.0, meas, interp, h * h / 8.0 * fmax);
    }
    printf("non-uniform: max measured %.2f, max interp-only %.2f centi\n", gm, gi);
    /* 균등 LUT: 같은 코드 범위를 8 구역으로 나눠 최대 오차 */
    printf("uniform step 64, max |err| per code band:\n");
    for (int k = 0; k < 8; k++) {
        int lo = NTC_CODE[0] + k * (NTC_CODE[NTC_N - 1] - NTC_CODE[0] + 1) / 8;
        int hi = NTC_CODE[0] + (k + 1) * (NTC_CODE[NTC_N - 1] - NTC_CODE[0] + 1) / 8 - 1;
        double mu = 0, mn = 0;
        for (int c = lo; c <= hi; c++) {
            int32_t v; ntc_read((uint16_t)c, &v);
            if (fabs(ulut_eval((uint16_t)c) - model_centi(c)) > mu) mu = fabs(ulut_eval((uint16_t)c) - model_centi(c));
            if (fabs(v - model_centi(c)) > mn) mn = fabs(v - model_centi(c));
        }
        printf("  codes %4d..%4d (%7.2f..%7.2f C): uniform %6.2f  non-uniform %6.2f\n", lo, hi,
               model_centi(lo) / 100, model_centi(hi) / 100, mu, mn);
    }
    /* 균등 간격을 바꿔 가며: 표를 모델에서 바로 만들고(lround) ulut_eval 과 같은 정수 보간(시프트 반올림) */
    printf("uniform LUT vs step (codes 128.., same error range %d..%d):\n", NTC_CODE[0], NTC_CODE[NTC_N - 1]);
    for (int sh = 6; sh >= 4; sh--) {                  /* 128 간격은 마지막 점이 코드 4096 → 모델 밖이라 제외 */
        int step = 1 << sh, npts = (NTC_CODE[NTC_N - 1] - 128 + step - 1) / step + 1;
        static int32_t tab[512];
        for (int j = 0; j < npts; j++) tab[j] = (int32_t)lround(model_centi(128 + step * j));
        double mu = 0; int mc = 0;
        for (int c = NTC_CODE[0]; c <= NTC_CODE[NTC_N - 1]; c++) {
            int off = c - 128, j = off >> sh, f = off & (step - 1);
            int32_t num = tab[j] * (step - f) + (f ? tab[j + 1] * f : 0);
            int32_t v = num >= 0 ? (num + step / 2) >> sh : -((-num + step / 2) >> sh);
            if (fabs(v - model_centi(c)) > mu) { mu = fabs(v - model_centi(c)); mc = c; }
        }
        printf("  step %3d: %3d points (%4d B as int32, %4d B as int16)  max |err| %6.2f centi @ code %d\n",
               step, npts, npts * 4, npts * 2, mu, mc);
    }
    return 0;
}
