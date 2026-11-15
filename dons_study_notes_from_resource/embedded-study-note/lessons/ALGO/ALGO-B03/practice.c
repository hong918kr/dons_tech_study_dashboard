/* ALGO-B03 practice.c — 추가 연습 4개 한 번에 검증
 *   연습 1  ntc_read_hint   : 직전 구간 기억 → 천천히 변하는 입력에서 이진 탐색 생략
 *   연습 2  ntc_read_q16    : 구간 기울기를 Q16 으로 미리 계산 → 조회 때 나눗셈 0번
 *   연습 3  place_knots     : 허용 오차 E 를 지키는 가장 긴 구간을 욕심쟁이(greedy)로 → 표 점 수 최소화
 *   연습 4  alarm_code      : 온도 경보 임계값 → ADC 코드 임계값 (단조 함수 위 이진 탐색, 역보간과 비교)
 *   본문 함수(seg_find, div_round, lut_eval, ntc_read)와 표는 solution.c 에서 복사.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   (리눅스는 끝에 -lm)
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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


/* ---------- 연습 1: 직전 구간 기억 (hint) ---------- */
static long g_searches;                                   /* 이진 탐색으로 떨어진 횟수 (측정용) */
ntc_status_t ntc_read_hint(uint16_t code, int *hint, int32_t *centi) {
    int32_t q = (int32_t)code;
    if (code <= NTC_SHORT_MAX || code >= NTC_OPEN_MIN || q < NTC_CODE[0] || q > NTC_CODE[NTC_N - 1])
        return ntc_read(code, centi);                     /* 드문 경로는 본문 함수에 맡긴다 */
    int i = *hint;
    if (i < 0 || i > NTC_N - 2) i = 0;
    if (!(NTC_CODE[i] <= q && (q < NTC_CODE[i + 1] || i == NTC_N - 2))) {
        if (i + 1 <= NTC_N - 2 && NTC_CODE[i + 1] <= q && (q < NTC_CODE[i + 2] || i + 1 == NTC_N - 2)) i++;
        else if (i - 1 >= 0 && NTC_CODE[i - 1] <= q && q < NTC_CODE[i]) i--;
        else { i = seg_find(NTC_CODE, NTC_N, q); g_searches++; }
    }
    *hint = i;
    int32_t d = NTC_CODE[i + 1] - NTC_CODE[i], s = q - NTC_CODE[i];
    int32_t num = NTC_CENTI[i] * (d - s) + NTC_CENTI[i + 1] * s;
    *centi = num >= 0 ? (num + d / 2) / d : -((-num + d / 2) / d);
    return NTC_OK;
}

/* ---------- 연습 2: Q16 기울기 미리 계산 ---------- */
static int32_t SLOPE_Q16[NTC_N - 1];                      /* round(dy * 65536 / d); |dy|*65536 <= 518*65536 < 2^31 */
void slope_init(void) {
    for (int i = 0; i + 1 < NTC_N; i++)
        SLOPE_Q16[i] = (int32_t)div_round((int64_t)(NTC_CENTI[i + 1] - NTC_CENTI[i]) * 65536, NTC_CODE[i + 1] - NTC_CODE[i]);
}
ntc_status_t ntc_read_q16(uint16_t code, int32_t *centi) {
    int32_t q = (int32_t)code;
    if (code <= NTC_SHORT_MAX || code >= NTC_OPEN_MIN || q < NTC_CODE[0] || q > NTC_CODE[NTC_N - 1])
        return ntc_read(code, centi);
    int i = seg_find(NTC_CODE, NTC_N, q);
    int32_t p = SLOPE_Q16[i] * (q - NTC_CODE[i]);         /* |p| <= |dy| * 65536 (반올림 몫 1개 여유) */
    int32_t delta = p >= 0 ? (p + 32768) >> 16 : -((-p + 32768) >> 16);   /* 음수 >> 는 구현 정의 → 부호 분리 */
    *centi = NTC_CENTI[i] + delta;
    return NTC_OK;
}

/* ---------- 연습 3: 허용 오차를 지키는 최소 점 배치 (greedy) ---------- */
static double model_centi(double code) {
    double x = (code + 0.5) / 4096.0, r = 10000.0 * x / (1.0 - x);
    return 100.0 * (1.0 / (1.0 / 298.15 + log(r / 10000.0) / 3950.0) - 273.15);
}
static double seg_err(int a, int b) {                     /* 모델 노드를 직선으로 이었을 때 [a,b] 최대 오차 */
    double ya = model_centi(a), yb = model_centi(b), e = 0;
    for (int c = a + 1; c < b; c++) {
        double d = fabs(ya + (yb - ya) * (c - a) / (b - a) - model_centi(c));
        if (d > e) e = d;
    }
    return e;
}
int place_knots(int lo, int hi, double tol, int *knots, int max) {
    int n = 0, a = lo;
    knots[n++] = a;
    while (a < hi) {
        int b = a + 1;
        while (b < hi && seg_err(a, b + 1) <= tol) b++;   /* 갈 수 있는 데까지 늘린다 */
        if (n >= max) return -1;
        knots[n++] = b;
        a = b;
    }
    return n;
}

/* ---------- 연습 4: 경보 임계값을 코드로 ---------- */
/* ntc_read 값은 코드가 커질수록 감소(단조). "value(c) >= thr" 가 참인 가장 큰 코드를 이진 탐색으로.
 * 하나도 없으면 NTC_CODE[0]-1. ISR 에서는 code <= 결과 이면 경보 — 보간도 나눗셈도 없다. */
int alarm_code(int32_t thr_centi) {
    int lo = NTC_CODE[0], hi = NTC_CODE[NTC_N - 1] + 1;    /* [lo,hi) 안에서 첫 거짓 찾기 (lower_bound 모양) */
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        int32_t v; ntc_read((uint16_t)mid, &v);
        if (v >= thr_centi) lo = mid + 1; else hi = mid;
    }
    return lo - 1;
}

/* ---------- 테스트 ---------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t g_rng = 0xC0FFEE11u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

int main(void) {
    /* 연습 1 */
    {
        int hint = 0, bad = 0; long n_walk = 0;
        int32_t w = 2048, wmin = 4096, wmax = -1;
        g_searches = 0;
        for (int k = 0; k < 200000; k++, n_walk++) {           /* 천천히 변하는 입력 */
            w += (int32_t)(xs32() % 7) - 3;                       /* 걸음 -3..+3 */
            if (w < 100) w = 100;                                  /* 양 끝 포화·고장 구간도 조금 지나가게 */
            if (w > 4050) w = 4050;
            if (w < wmin) wmin = w;
            if (w > wmax) wmax = w;
            int32_t a = -7, b = -7;
            ntc_status_t sa = ntc_read((uint16_t)w, &a), sb = ntc_read_hint((uint16_t)w, &hint, &b);
            if (sa != sb || a != b) bad++;
        }
        long walk_searches = g_searches;
        g_searches = 0;
        for (int k = 0; k < 200000; k++) {                      /* 무작위 입력 */
            uint16_t c = (uint16_t)(xs32() % 4096);
            int32_t a = -7, b = -7;
            ntc_status_t sa = ntc_read(c, &a), sb = ntc_read_hint(c, &hint, &b);
            if (sa != sb || a != b) bad++;
        }
        printf("[1] hint: mismatches=%d, binary searches: walk(codes %d..%d) %ld / %ld, random %ld / 200000\n",
               bad, wmin, wmax, walk_searches, n_walk, g_searches);
        CHECK(bad == 0);
        CHECK(walk_searches * 100 < n_walk);                   /* 천천히 변하면 1% 미만만 탐색 */
    }
    /* 연습 2 */
    {
        slope_init();
        int diff = 0, maxd = 0, bad_status = 0;
        double worst = 0;
        for (int c = 0; c <= 4095; c++) {
            int32_t a = 0, b = 0;
            ntc_status_t sa = ntc_read((uint16_t)c, &a), sb = ntc_read_q16((uint16_t)c, &b);
            if (sa != sb) bad_status++;
            if (sa != NTC_OK) continue;
            if (a != b) diff++;
            if (abs(a - b) > maxd) maxd = abs(a - b);
            if (fabs(b - model_centi(c)) > worst) worst = fabs(b - model_centi(c));
        }
        printf("[2] q16 slope: codes differing from exact division=%d, max diff=%d centi, max |err| vs model=%.2f\n",
               diff, maxd, worst);
        CHECK(bad_status == 0 && maxd <= 1);
        CHECK(worst <= 20.0);
    }
    /* 연습 3 */
    {
        int k1[128], k2[128];
        int n1 = place_knots(NTC_CODE[0], NTC_CODE[NTC_N - 1], 18.22, k1, 128);   /* 5 °C 표의 순수 보간 최대 오차 */
        int n2 = place_knots(NTC_CODE[0], NTC_CODE[NTC_N - 1], 5.0, k2, 128);
        int ok1 = n1 > 1, ok2 = n2 > 1;
        for (int i = 0; ok1 && i + 1 < n1; i++) if (seg_err(k1[i], k1[i + 1]) > 18.22) ok1 = 0;
        for (int i = 0; ok2 && i + 1 < n2; i++) if (seg_err(k2[i], k2[i + 1]) > 5.0) ok2 = 0;
        printf("[3] greedy knots: tol 18.22 centi -> %d points (5 C table: %d), tol 5.00 -> %d points\n", n1, NTC_N, n2);
        printf("    tol 18.22 knots:");
        for (int i = 0; i < n1; i++) printf(" %d", k1[i]);
        printf("\n");
        CHECK(ok1 && ok2 && k1[0] == NTC_CODE[0] && k1[n1 - 1] == NTC_CODE[NTC_N - 1]);
        CHECK(n1 < NTC_N);
    }
    /* 연습 4 */
    {
        static int32_t val[4096];
        for (int c = 0; c <= 4095; c++) ntc_read((uint16_t)c, &val[c]);
        const lut_t inv = {NTC_CENTI, NTC_CODE, NTC_N};
        int wrong = 0, est_diff = 0, est_maxd = 0;
        for (int32_t thr = NTC_CENTI[NTC_N - 1]; thr <= NTC_CENTI[0]; thr++) {
            int ac = alarm_code(thr);
            for (int c = NTC_CODE[0]; c <= NTC_CODE[NTC_N - 1]; c++)
                if ((c <= ac) != (val[c] >= thr)) { wrong++; break; }
            int est = lut_eval(&inv, thr);                    /* 역보간 추정치 */
            if (est != ac) { est_diff++; if (abs(est - ac) > est_maxd) est_maxd = abs(est - ac); }
        }
        int n_thr = NTC_CENTI[0] - NTC_CENTI[NTC_N - 1] + 1;
        printf("[4] alarm_code: %d thresholds, wrong decisions=%d; inverse-LUT estimate differs in %d (max %d code)\n",
               n_thr, wrong, est_diff, est_maxd);
        printf("    85.00 C -> code <= %d, 87.50 C -> code <= %d (inverse estimate %d)\n",
               alarm_code(8500), alarm_code(8750), lut_eval(&inv, 8750));
        CHECK(wrong == 0);
        CHECK(alarm_code(8500) == 401 && alarm_code(NTC_CENTI[0] + 1) == NTC_CODE[0] - 1 && alarm_code(-5000) == NTC_CODE[NTC_N - 1]);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
