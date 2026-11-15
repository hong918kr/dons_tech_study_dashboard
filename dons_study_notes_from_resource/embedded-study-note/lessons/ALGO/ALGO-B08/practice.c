/* ALGO-B08 추가 연습 — 한 번에 검증
 *
 *   연습 1  부호 있는 12비트 ADC(차동 입력, -2048..2047) 계수 정렬 — 오프셋(bias) 더하기
 *   연습 2  int32 기수 정렬 — 부호 비트 뒤집기(x ^ 0x80000000)로 순서 보존 매핑
 *   연습 3  11비트 자리 3 패스 (11+11+10) — 홀수 패스면 결과가 tmp 에 있다 → 복사 1 번
 *   연습 4  한 번 읽어 4 개 히스토그램 + 쓸모없는 패스 건너뛰기 (모두 같은 자리값) + 홀짝 맞추기
 *   연습 5  LeetCode 75 Sort Colors — 값 3 종 계수 정렬 (2 패스)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- 연습 1 ---------------- */
#define I12_MIN  (-2048)
#define I12_MAX  2047
#define I12_SPAN 4096u
/* 키 = x - I12_MIN (0..4095). 범위 밖이면 -1, a 그대로. */
static int count_sort_i12(int16_t *a, size_t n, uint32_t *cnt) {
    memset(cnt, 0, I12_SPAN * sizeof cnt[0]);
    for (size_t i = 0; i < n; i++) {
        if (a[i] < I12_MIN || a[i] > I12_MAX) return -1;
        cnt[a[i] - I12_MIN]++;
    }
    size_t k = 0;
    for (uint32_t u = 0; u < I12_SPAN; u++)
        for (uint32_t c = cnt[u]; c > 0; c--)
            a[k++] = (int16_t)((int32_t)u + I12_MIN);
    return 0;
}

/* ---------------- 연습 2 ---------------- */
/* (uint32_t)x 는 2의 보수 비트 그대로 (C11 6.3.1.3: 값 + 2^32). 부호 비트를 뒤집으면
 * INT32_MIN → 0, -1 → 0x7FFFFFFF, 0 → 0x80000000, INT32_MAX → 0xFFFFFFFF : 순서 보존. */
static inline uint32_t i32_key(int32_t x) { return (uint32_t)x ^ 0x80000000u; }
static void radix_sort_i32(int32_t *a, int32_t *tmp, size_t n) {
    int32_t *src = a, *dst = tmp;
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        uint32_t cnt[256] = {0};
        for (size_t i = 0; i < n; i++) cnt[(i32_key(src[i]) >> shift) & 0xFFu]++;
        uint32_t sum = 0;
        for (unsigned d = 0; d < 256u; d++) { uint32_t c = cnt[d]; cnt[d] = sum; sum += c; }
        for (size_t i = 0; i < n; i++) dst[cnt[(i32_key(src[i]) >> shift) & 0xFFu]++] = src[i];
        int32_t *t = src; src = dst; dst = t;
    }
}

/* ---------------- 연습 3 ---------------- */
/* 자리 폭 11, 11, 10 비트 → 3 패스. 카운트 2048 칸 = 8 KB (static: 스택에 두지 않는다, 재진입 불가).
 * 반환: 마지막에 memcpy 로 되돌린 횟수 (0 또는 1). */
static uint32_t g_cnt11[2048];
static int radix_sort_u32_11(uint32_t *a, uint32_t *tmp, size_t n) {
    static const unsigned shifts[3] = { 0, 11, 22 };
    uint32_t *src = a, *dst = tmp;
    for (int p = 0; p < 3; p++) {
        unsigned sh = shifts[p];
        memset(g_cnt11, 0, sizeof g_cnt11);
        for (size_t i = 0; i < n; i++) g_cnt11[(src[i] >> sh) & 0x7FFu]++;
        uint32_t sum = 0;
        for (unsigned d = 0; d < 2048u; d++) { uint32_t c = g_cnt11[d]; g_cnt11[d] = sum; sum += c; }
        for (size_t i = 0; i < n; i++) dst[g_cnt11[(src[i] >> sh) & 0x7FFu]++] = src[i];
        uint32_t *t = src; src = dst; dst = t;
    }
    if (src != a) { memcpy(a, src, n * sizeof a[0]); return 1; }   /* 3 = 홀수 → 결과는 tmp 에 */
    return 0;
}

/* ---------------- 연습 4 ---------------- */
/* 한 번 읽어 자리 4 개의 히스토그램을 모두 만든다 (4 KB). 어떤 자리의 버킷 하나가 n 개를 다 가지면
 * 그 패스는 순서를 바꾸지 않으므로 건너뛴다. 실행한 패스 수를 *passes 에, 끝 복사 여부를 반환. */
static int radix_sort_u32_skip(uint32_t *a, uint32_t *tmp, size_t n, int *passes) {
    uint32_t cnt[4][256];
    memset(cnt, 0, sizeof cnt);
    for (size_t i = 0; i < n; i++) {
        uint32_t x = a[i];
        cnt[0][x & 0xFFu]++; cnt[1][(x >> 8) & 0xFFu]++;
        cnt[2][(x >> 16) & 0xFFu]++; cnt[3][x >> 24]++;
    }
    uint32_t *src = a, *dst = tmp;
    *passes = 0;
    for (unsigned p = 0; p < 4; p++) {
        unsigned sh = 8u * p;
        if (n == 0 || cnt[p][(a[0] >> sh) & 0xFFu] == n) continue;   /* 모두 같은 자리값 → 건너뜀 */
        uint32_t sum = 0;
        for (unsigned d = 0; d < 256u; d++) { uint32_t c = cnt[p][d]; cnt[p][d] = sum; sum += c; }
        for (size_t i = 0; i < n; i++) dst[cnt[p][(src[i] >> sh) & 0xFFu]++] = src[i];
        uint32_t *t = src; src = dst; dst = t;
        (*passes)++;
    }
    if (src != a) { memcpy(a, src, n * sizeof a[0]); return 1; }
    return 0;
}

/* ---------------- 연습 5 ---------------- */
/* LeetCode 75 Sort Colors: 값은 0, 1, 2 뿐. 1 패스로 세고 1 패스로 다시 쓴다. 범위 밖이면 -1. */
static int sort_colors(int *a, size_t n) {
    size_t c[3] = { 0, 0, 0 };
    for (size_t i = 0; i < n; i++) {
        if (a[i] < 0 || a[i] > 2) return -1;
        c[a[i]]++;
    }
    size_t k = 0;
    for (int v = 0; v < 3; v++)
        for (size_t j = 0; j < c[v]; j++) a[k++] = v;
    return 0;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x9E3779B9u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static int cmp_i16(const void *p, const void *q) { int16_t x = *(const int16_t *)p, y = *(const int16_t *)q; return (x > y) - (x < y); }
static int cmp_i32(const void *p, const void *q) { int32_t x = *(const int32_t *)p, y = *(const int32_t *)q; return (x > y) - (x < y); }
static int cmp_u32(const void *p, const void *q) { uint32_t x = *(const uint32_t *)p, y = *(const uint32_t *)q; return (x > y) - (x < y); }
static int cmp_int(const void *p, const void *q) { int x = *(const int *)p, y = *(const int *)q; return (x > y) - (x < y); }

#define NMAX 4000
static int16_t S16[NMAX], R16[NMAX];
static int32_t S32[NMAX], R32[NMAX], T32[NMAX];
static uint32_t U[NMAX], UR[NMAX], UT[NMAX];
static uint32_t CNT[4096];
static int C[NMAX], CR[NMAX];

int main(void) {
    /* 연습 1 */
    {
        int bad = 0;
        for (int t = 0; t < 200; t++) {
            size_t n = xs32() % 2001u;
            for (size_t i = 0; i < n; i++) S16[i] = (int16_t)((int32_t)(xs32() % 4096u) + I12_MIN);
            memcpy(R16, S16, n * sizeof S16[0]);
            qsort(R16, n, sizeof R16[0], cmp_i16);
            bad += count_sort_i12(S16, n, CNT) != 0 || memcmp(S16, R16, n * sizeof S16[0]) != 0;
        }
        CHECK(bad == 0);
        int16_t e[5] = { 2047, -2048, 0, -1, 1 }, ew[5] = { -2048, -1, 0, 1, 2047 };
        CHECK(count_sort_i12(e, 5, CNT) == 0 && memcmp(e, ew, sizeof e) == 0);
        int16_t o[3] = { 5, -2049, 2 }, ok[3] = { 5, -2049, 2 };
        CHECK(count_sort_i12(o, 3, CNT) == -1 && memcmp(o, ok, sizeof o) == 0);
    }
    /* 연습 2 */
    {
        int bad = 0;
        for (int t = 0; t < 200; t++) {
            size_t n = xs32() % 4001u;
            for (size_t i = 0; i < n; i++) S32[i] = (int32_t)(xs32() & 0x7FFFFFFFu) - (int32_t)(xs32() & 0x7FFFFFFFu);
            memcpy(R32, S32, n * sizeof S32[0]);
            qsort(R32, n, sizeof R32[0], cmp_i32);
            radix_sort_i32(S32, T32, n);
            bad += memcmp(S32, R32, n * sizeof S32[0]) != 0;
        }
        CHECK(bad == 0);
        int32_t e[6] = { 0, INT32_MIN, -1, INT32_MAX, 1, INT32_MIN }, et[6];
        int32_t ew[6] = { INT32_MIN, INT32_MIN, -1, 0, 1, INT32_MAX };
        radix_sort_i32(e, et, 6);
        CHECK(memcmp(e, ew, sizeof e) == 0);
        /* 부호 뒤집기를 빼면? 음수(0x8xxxxxxx..)가 양수 뒤로 간다 */
        CHECK((uint32_t)-1 > (uint32_t)1 && i32_key(-1) < i32_key(1));
    }
    /* 연습 3 */
    {
        int bad = 0, copies = 0;
        for (int t = 0; t < 100; t++) {
            size_t n = 1 + xs32() % 4000u;
            for (size_t i = 0; i < n; i++) U[i] = xs32();
            memcpy(UR, U, n * sizeof U[0]);
            qsort(UR, n, sizeof UR[0], cmp_u32);
            copies += radix_sort_u32_11(U, UT, n);
            bad += memcmp(U, UR, n * sizeof U[0]) != 0;
        }
        CHECK(bad == 0);
        CHECK(copies == 100);                       /* 3 패스면 매번 복사 1 번 */
    }
    /* 연습 4 */
    {
        int bad = 0, passes = -1, copied;
        for (int t = 0; t < 100; t++) {
            size_t n = xs32() % 4001u;
            for (size_t i = 0; i < n; i++) U[i] = xs32();
            memcpy(UR, U, n * sizeof U[0]);
            qsort(UR, n, sizeof UR[0], cmp_u32);
            radix_sort_u32_skip(U, UT, n, &passes);
            bad += memcmp(U, UR, n * sizeof U[0]) != 0;
        }
        CHECK(bad == 0);
        for (size_t i = 0; i < 3000; i++) U[i] = xs32() & 0xFFFu;      /* 12비트 ADC 를 uint32 에 담음 */
        memcpy(UR, U, 3000 * sizeof U[0]); qsort(UR, 3000, sizeof UR[0], cmp_u32);
        copied = radix_sort_u32_skip(U, UT, 3000, &passes);
        CHECK(passes == 2 && copied == 0 && memcmp(U, UR, 3000 * sizeof U[0]) == 0);
        for (size_t i = 0; i < 3000; i++) U[i] = 0xC0FFEE00u | (xs32() & 0xFFu);  /* 아래 바이트만 다름 */
        memcpy(UR, U, 3000 * sizeof U[0]); qsort(UR, 3000, sizeof UR[0], cmp_u32);
        copied = radix_sort_u32_skip(U, UT, 3000, &passes);
        CHECK(passes == 1 && copied == 1 && memcmp(U, UR, 3000 * sizeof U[0]) == 0);
        for (size_t i = 0; i < 100; i++) U[i] = 42u;
        copied = radix_sort_u32_skip(U, UT, 100, &passes);
        CHECK(passes == 0 && copied == 0 && U[0] == 42u && U[99] == 42u);
    }
    /* 연습 5 */
    {
        int bad = 0;
        for (int t = 0; t < 200; t++) {
            size_t n = xs32() % 300u;
            for (size_t i = 0; i < n; i++) C[i] = (int)(xs32() % 3u);
            memcpy(CR, C, n * sizeof C[0]);
            qsort(CR, n, sizeof CR[0], cmp_int);
            bad += sort_colors(C, n) != 0 || memcmp(C, CR, n * sizeof C[0]) != 0;
        }
        CHECK(bad == 0);
        int ex[6] = { 2, 0, 2, 1, 1, 0 }, exw[6] = { 0, 0, 1, 1, 2, 2 };
        CHECK(sort_colors(ex, 6) == 0 && memcmp(ex, exw, sizeof ex) == 0);
        int bx[3] = { 0, 3, 1 };
        CHECK(sort_colors(bx, 3) == -1);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
