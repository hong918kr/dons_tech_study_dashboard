/* ALGO-M13 추가 연습 — 한 번에 검증
 * 연습 1 합이 K 인 부분 배열 개수 (LeetCode 560) — 누적합 + 정적 해시 테이블
 * 연습 2 자기 자신을 뺀 곱 (LeetCode 238) — 앞쪽 곱 × 뒤쪽 곱, 나눗셈 없이
 * 연습 3 2D 차분 배열 — 직사각형 증가 여러 번 → 2D 누적으로 복원 (센서 시야 겹침 지도)
 * 연습 4 줄 버퍼 박스 필터 — 열 합(column sum) O(w) 메모리로 integral image 와 같은 결과
 * 연습 5 국소 분산 — 제곱의 integral image 를 uint32 로 감아 저장 (창 넓이 <= 66051 이면 정확)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 본문과 같은 integral image / rect_sum (uint32, 감겨도 차는 정확) */
static void integral_build(const uint16_t *img, size_t w, size_t h, uint32_t *I) {
    size_t W = w + 1;
    for (size_t x = 0; x <= w; x++) I[x] = 0;
    for (size_t y = 0; y < h; y++) {
        uint32_t row = 0;
        I[(y + 1) * W] = 0;
        for (size_t x = 0; x < w; x++) { row += img[y * w + x]; I[(y + 1) * W + x + 1] = I[y * W + x + 1] + row; }
    }
}
static uint32_t rect_sum(const uint32_t *I, size_t w, size_t x0, size_t y0, size_t x1, size_t y1) {
    size_t W = w + 1;
    return I[y1 * W + x1] - I[y0 * W + x1] - I[y1 * W + x0] + I[y0 * W + x0];
}

/* ---- 연습 1: 합이 정확히 k 인 연속 부분 배열의 개수 ----
 * [l, r) 의 합 = P[r] − P[l] = k  ⇔  P[l] = P[r] − k. r 을 왼→오 훑으며 "지금까지 본 P 값의 개수"를 해시에 센다. */
#define MAX_N1 4096
#define HCAP 8192                                   /* 2의 거듭제곱, >= 2·(n+1) → 적재율 <= 1/2 */
static int64_t h_key[HCAP];
static uint32_t h_cnt[HCAP];
static bool h_used[HCAP];
static uint32_t h_find(int64_t key) {              /* 선형 탐사(linear probing): key 의 칸 또는 빈 칸 */
    uint32_t i = (uint32_t)(((uint64_t)key * 0x9E3779B97F4A7C15ull) >> 51) & (HCAP - 1);
    while (h_used[i] && h_key[i] != key) i = (i + 1) & (HCAP - 1);
    return i;
}
static uint32_t h_get(int64_t key) { uint32_t i = h_find(key); return h_used[i] ? h_cnt[i] : 0; }   /* 조회는 삽입하지 않는다 */
static void h_inc(int64_t key) {                   /* 항목 수 <= n+1 <= HCAP/2 이라 빈 칸이 늘 있다 */
    uint32_t i = h_find(key);
    if (!h_used[i]) { h_used[i] = true; h_key[i] = key; h_cnt[i] = 0; }
    h_cnt[i]++;
}
static uint64_t subarray_sum_k(const int32_t *a, size_t n, int64_t k) {
    memset(h_used, 0, sizeof h_used);
    uint64_t count = 0;
    int64_t P = 0;
    h_inc(0);                                       /* P[0] = 0: 빈 앞부분 */
    for (size_t r = 0; r < n; r++) {
        P += a[r];                                  /* P = P[r+1] */
        count += h_get(P - k);                      /* P[l] = P[r+1] − k 인 l (< r+1) 의 개수 */
        h_inc(P);
    }
    return count;
}

/* ---- 연습 2: out[i] = a[0]·…·a[n−1] / a[i] 를 나눗셈 없이. 추가 메모리 O(1) (출력 제외) ---- */
static void product_except_self(const int64_t *a, size_t n, int64_t *out) {
    int64_t left = 1;
    for (size_t i = 0; i < n; i++) { out[i] = left; left *= a[i]; }          /* out[i] = a[0..i) 의 곱 */
    int64_t right = 1;
    for (size_t i = n; i-- > 0;) { out[i] *= right; right *= a[i]; }         /* × a(i..n) 의 곱 */
}

/* ---- 연습 3: 2D 차분 배열. D 는 (w+1)×(h+1), 폭 W = w+1 ---- */
static void diff2_add(int32_t *D, size_t w, size_t x0, size_t y0, size_t x1, size_t y1, int32_t v) {
    if (x0 >= x1 || y0 >= y1) return;
    size_t W = w + 1;
    D[y0 * W + x0] += v; D[y0 * W + x1] -= v;
    D[y1 * W + x0] -= v; D[y1 * W + x1] += v;
}
static void diff2_rebuild(int32_t *D, size_t w, size_t h, int32_t *out) {   /* D 를 제자리 2D 누적 → out (w×h) */
    size_t W = w + 1;
    for (size_t y = 0; y < h; y++)
        for (size_t x = 0; x < w; x++) {
            int32_t up = y ? D[(y - 1) * W + x] : 0, lf = x ? D[y * W + x - 1] : 0, ul = (x && y) ? D[(y - 1) * W + x - 1] : 0;
            D[y * W + x] += up + lf - ul;
            out[y * w + x] = D[y * W + x];
        }
}

/* ---- 연습 4: 줄 버퍼 박스 필터 — 잘린 (2r+1)² 창의 합. col[x] = 현재 세로 창 안의 열 합 (w 칸) ----
 * 행 y 로 내려갈 때 행 y+r 을 더하고 행 y−r−1 을 뺀다. 스트리밍이면 최근 2r+2 행만 들고 있으면 된다. */
static void box_sum_colsum(const uint16_t *img, size_t w, size_t h, size_t r, uint32_t *col, uint32_t *out) {
    for (size_t x = 0; x < w; x++) {
        col[x] = 0;
        for (size_t y = 0; y <= r && y < h; y++) col[x] += img[y * w + x];
    }
    for (size_t y = 0; y < h; y++) {
        if (y > 0) {
            if (y + r < h) for (size_t x = 0; x < w; x++) col[x] += img[(y + r) * w + x];
            if (y >= r + 1) for (size_t x = 0; x < w; x++) col[x] -= img[(y - r - 1) * w + x];
        }
        uint32_t s = 0;
        for (size_t x = 0; x <= r && x < w; x++) s += col[x];
        for (size_t x = 0; x < w; x++) {
            if (x > 0) {
                if (x + r < w) s += col[x + r];
                if (x >= r + 1) s -= col[x - r - 1];
            }
            out[y * w + x] = s;
        }
    }
}

/* ---- 연습 5: 제곱의 integral image (uint32, 감김 허용) → 창의 분산 분자 N·Σx² − (Σx)² (uint64, 정확) ---- */
static void integral_sq_build(const uint16_t *img, size_t w, size_t h, uint32_t *I2) {
    size_t W = w + 1;
    for (size_t x = 0; x <= w; x++) I2[x] = 0;
    for (size_t y = 0; y < h; y++) {
        uint32_t row = 0;
        I2[(y + 1) * W] = 0;
        for (size_t x = 0; x < w; x++) {
            uint32_t v = img[y * w + x];
            row += v * v;
            I2[(y + 1) * W + x + 1] = I2[y * W + x + 1] + row;
        }
    }
}
static uint64_t var_numer(const uint32_t *I, const uint32_t *I2, size_t w, size_t x0, size_t y0, size_t x1, size_t y1) {
    uint64_t N = (uint64_t)(x1 - x0) * (y1 - y0);
    uint64_t s = rect_sum(I, w, x0, y0, x1, y1), s2 = rect_sum(I2, w, x0, y0, x1, y1);
    return N * s2 - s * s;                          /* = N²·분산 >= 0 (코시–슈바르츠) */
}

#define MW 320
#define MH 240
static int32_t g_a[MAX_N1];
static int64_t g_p[16], g_o[16];
static int32_t g_D[(MW + 1) * (MH + 1)], g_out2[MW * MH], g_bf2[MW * MH];
static uint16_t g_img[300 * 300];
static uint32_t g_I[301 * 301], g_I2[301 * 301], g_col[MW], g_box[MW * MH];

int main(void) {
    uint32_t seed = 0x2468ACE1u;

    /* 연습 1 */
    const int32_t e1[3] = { 1, 1, 1 }, e2[3] = { 1, 2, 3 };
    CHECK(subarray_sum_k(e1, 3, 2) == 2);           /* LeetCode 560 예제 */
    CHECK(subarray_sum_k(e2, 3, 3) == 2);
    CHECK(subarray_sum_k(e1, 0, 0) == 0);           /* 빈 배열: 빈 부분 배열은 세지 않는다 */
    const int32_t z[4] = { 0, 0, 0, 0 };
    CHECK(subarray_sum_k(z, 4, 0) == 10);           /* 전부 0: 4·5/2 */
    int bad1 = 0;
    for (int t = 0; t < 300; t++) {
        size_t n = (size_t)(xs32(&seed) % 400u);
        for (size_t i = 0; i < n; i++) g_a[i] = (int32_t)(xs32(&seed) % 7u) - 3;   /* −3..3: 합 0 근처가 많음 */
        int64_t k = (int64_t)(xs32(&seed) % 11u) - 5;
        uint64_t bf = 0;
        for (size_t l = 0; l < n; l++) { int64_t s = 0; for (size_t r = l; r < n; r++) { s += g_a[r]; bf += s == k; } }
        bad1 += subarray_sum_k(g_a, n, k) != bf;
    }
    CHECK(bad1 == 0);
    for (size_t i = 0; i < MAX_N1; i++) g_a[i] = (i & 1) ? INT32_MIN : INT32_MAX;   /* 큰 값: int64 누적 필요 */
    /* P[2j] = −j, P[2j+1] = INT32_MAX − j → 합 −1 은 길이 2 창뿐: 짝·짝 2048쌍 + 홀·홀 2047쌍 = 4095 */
    CHECK(subarray_sum_k(g_a, MAX_N1, -1) == 4095);

    /* 연습 2 */
    const int64_t p1[4] = { 1, 2, 3, 4 }, p2[5] = { -1, 1, 0, -3, 3 };
    product_except_self(p1, 4, g_o);
    CHECK(g_o[0] == 24 && g_o[1] == 12 && g_o[2] == 8 && g_o[3] == 6);              /* LeetCode 238 예제 */
    product_except_self(p2, 5, g_o);
    CHECK(g_o[0] == 0 && g_o[1] == 0 && g_o[2] == 9 && g_o[3] == 0 && g_o[4] == 0);
    int bad2 = 0;
    for (int t = 0; t < 2000; t++) {
        size_t n = 1 + (size_t)(xs32(&seed) % 12u);
        for (size_t i = 0; i < n; i++) g_p[i] = (int64_t)(xs32(&seed) % 11u) - 5;   /* 0 이 0·1·여러 개 */
        product_except_self(g_p, n, g_o);
        for (size_t i = 0; i < n; i++) { int64_t q = 1; for (size_t j = 0; j < n; j++) if (j != i) q *= g_p[j]; bad2 += g_o[i] != q; }
    }
    CHECK(bad2 == 0);

    /* 연습 3: 320×240 지도에 직사각형 500개 (센서 시야) — 브루트포스로 칸마다 더한 것과 같아야 */
    int bad3 = 0;
    for (int t = 0; t < 5; t++) {
        size_t w = 1 + xs32(&seed) % MW, h = 1 + xs32(&seed) % MH;
        memset(g_D, 0, sizeof(int32_t) * (w + 1) * (h + 1));
        memset(g_bf2, 0, sizeof(int32_t) * w * h);
        for (int k = 0; k < 500; k++) {
            size_t x0 = xs32(&seed) % (w + 1), x1 = xs32(&seed) % (w + 1), y0 = xs32(&seed) % (h + 1), y1 = xs32(&seed) % (h + 1);
            if (x0 > x1) { size_t tmp = x0; x0 = x1; x1 = tmp; }
            if (y0 > y1) { size_t tmp = y0; y0 = y1; y1 = tmp; }
            int32_t v = (int32_t)(xs32(&seed) % 21u) - 10;
            diff2_add(g_D, w, x0, y0, x1, y1, v);
            for (size_t y = y0; y < y1; y++) for (size_t x = x0; x < x1; x++) g_bf2[y * w + x] += v;
        }
        diff2_rebuild(g_D, w, h, g_out2);
        bad3 += memcmp(g_out2, g_bf2, sizeof(int32_t) * w * h) != 0;
    }
    CHECK(bad3 == 0);
    memset(g_D, 0, sizeof(int32_t) * 4 * 3);        /* 손 계산: 3×2 지도, [0,2)×[0,2) +1, [1,3)×[1,2) +2 */
    diff2_add(g_D, 3, 0, 0, 2, 2, 1); diff2_add(g_D, 3, 1, 1, 3, 2, 2);
    diff2_rebuild(g_D, 3, 2, g_out2);
    CHECK(g_out2[0] == 1 && g_out2[1] == 1 && g_out2[2] == 0 && g_out2[3] == 1 && g_out2[4] == 3 && g_out2[5] == 2);

    /* 연습 4: 줄 버퍼 박스 합 == integral image 의 잘린 창 합 (모든 픽셀) */
    int bad4 = 0;
    const size_t rs[6] = { 0, 1, 3, 7, 15, 400 };   /* r >= w, h 인 경우도 */
    for (int t = 0; t < 6; t++) {
        size_t w = t == 5 ? 37 : MW, h = t == 5 ? 23 : MH, r = rs[t];
        for (size_t i = 0; i < w * h; i++) g_img[i] = (uint16_t)(xs32(&seed) % 4096u);
        integral_build(g_img, w, h, g_I);
        box_sum_colsum(g_img, w, h, r, g_col, g_box);
        for (size_t y = 0; y < h; y++) for (size_t x = 0; x < w; x++) {
            size_t x0 = x >= r ? x - r : 0, x1 = x + r + 1 < w ? x + r + 1 : w;
            size_t y0 = y >= r ? y - r : 0, y1 = y + r + 1 < h ? y + r + 1 : h;
            bad4 += g_box[y * w + x] != rect_sum(g_I, w, x0, y0, x1, y1);
        }
    }
    CHECK(bad4 == 0);

    /* 연습 5: 국소 분산. 랜덤 8비트 200×150, 창 3×3·7×7·15×15 — uint64 브루트포스와 대조 */
    int bad5 = 0;
    {
        size_t w = 200, h = 150;
        for (size_t i = 0; i < w * h; i++) g_img[i] = (uint16_t)(xs32(&seed) & 0xFFu);
        integral_build(g_img, w, h, g_I);
        integral_sq_build(g_img, w, h, g_I2);
        for (int q = 0; q < 3000; q++) {
            size_t k = (q % 3 == 0) ? 3 : (q % 3 == 1) ? 7 : 15;
            size_t x0 = xs32(&seed) % (w - k + 1), y0 = xs32(&seed) % (h - k + 1);
            uint64_t s = 0, s2 = 0, N = k * k;
            for (size_t y = y0; y < y0 + k; y++) for (size_t x = x0; x < x0 + k; x++) { uint64_t v = g_img[y * w + x]; s += v; s2 += v * v; }
            bad5 += var_numer(g_I, g_I2, w, x0, y0, x0 + k, y0 + k) != N * s2 - s * s;
        }
    }
    CHECK(bad5 == 0);
    /* 경계: 300×300 전부 255 → 제곱 integral 전체 = 90000·65025 = 5,852,250,000 > 2^32 (감김) */
    for (size_t i = 0; i < 300 * 300; i++) g_img[i] = 255;
    integral_sq_build(g_img, 300, 300, g_I2);
    CHECK(g_I2[301 * 301 - 1] != 0 && (uint64_t)g_I2[301 * 301 - 1] == 5852250000ull % 4294967296ull);
    CHECK(rect_sum(g_I2, 300, 10, 10, 267, 267) == 66049u * 65025u);         /* 257×257 = 66049 <= 66051: 정확 */
    CHECK((uint64_t)rect_sum(g_I2, 300, 10, 10, 268, 267) == 66306ull * 65025u - 4294967296ull);  /* 258×257 = 66306 > 66051: 2^32 만큼 모자람 */

    printf("practice: subarray-sum-k bad=%d, product bad=%d, diff2 bad=%d, colsum box bad=%d, variance bad=%d\n",
           bad1, bad2, bad3, bad4, bad5);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
