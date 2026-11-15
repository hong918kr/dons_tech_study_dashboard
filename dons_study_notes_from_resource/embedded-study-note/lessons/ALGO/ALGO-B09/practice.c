/* ALGO-B09 추가 연습 — 한 번에 검증
 *
 *   연습 1: count_pairs_eq   — 합이 t 인 인덱스 쌍의 "개수" (중복 많은 정렬 배열, O(n))
 *   연습 2: count_pairs_le   — 합이 t 이하인 쌍의 개수 (두 패킷을 한 프레임에 묶을 수 있는 조합 수)
 *   연습 3: remove_value     — 무효 샘플(센티넬) 제자리 제거, 순서 유지 (LeetCode 27)
 *   연습 4: sorted_squares   — 정렬된 부호 있는 오프셋 → 제곱(에너지) 정렬, 뒤에서 채우기 (LeetCode 977)
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_i32(const void *p, const void *q) {
    int32_t x = *(const int32_t *)p, y = *(const int32_t *)q;
    return (x > y) - (x < y);
}
static void sort_i32(int32_t *a, size_t n) { if (n > 1) qsort(a, n, sizeof a[0], cmp_i32); }

/* ---------- 연습 1: 합이 t 인 쌍 (i < j) 의 개수 ---------- */
static uint64_t count_pairs_eq(const int32_t *a, size_t n, int32_t t) {
    if (n < 2) return 0;
    uint64_t cnt = 0;
    size_t lo = 0, hi = n - 1;
    while (lo < hi) {
        int64_t s = (int64_t)a[lo] + a[hi];
        if (s < t) lo++;
        else if (s > t) hi--;
        else if (a[lo] == a[hi]) {                    /* [lo, hi] 전부 같은 값: C(m, 2) */
            uint64_t m = hi - lo + 1;
            cnt += m * (m - 1) / 2;
            break;
        } else {                                      /* 값이 다른 두 묶음(run): 곱 */
            size_t cl = 1, ch = 1;
            while (lo + cl < hi && a[lo + cl] == a[lo]) cl++;
            while (hi - ch > lo && a[hi - ch] == a[hi]) ch++;
            cnt += (uint64_t)cl * ch;
            lo += cl; hi -= ch;
        }
    }
    return cnt;
}

/* ---------- 연습 2: 합이 t 이하인 쌍 (i < j) 의 개수 ---------- */
static uint64_t count_pairs_le(const int32_t *a, size_t n, int64_t t) {
    if (n < 2) return 0;
    uint64_t cnt = 0;
    size_t lo = 0, hi = n - 1;
    while (lo < hi) {
        if ((int64_t)a[lo] + a[hi] <= t) { cnt += hi - lo; lo++; }   /* a[lo] 는 (lo, hi] 전부와 OK */
        else hi--;
    }
    return cnt;
}

/* ---------- 연습 3: 값 v 를 모두 제거, 나머지 순서 유지. 반환 = 새 길이 ---------- */
static size_t remove_value(int32_t *a, size_t n, int32_t v) {
    size_t w = 0;
    for (size_t r = 0; r < n; r++)
        if (a[r] != v) a[w++] = a[r];
    return w;
}

/* ---------- 연습 4: 정렬된 a → out = a[i]^2 오름차순 (int64) ---------- */
static void sorted_squares(const int32_t *a, size_t n, int64_t *out) {
    size_t lo = 0, hi = n, k = n;                     /* [lo, hi) 가 남은 구간 */
    while (lo < hi) {
        int64_t x = (int64_t)a[lo] * a[lo], y = (int64_t)a[hi - 1] * a[hi - 1];
        if (x > y) { out[--k] = x; lo++; }            /* 가장 큰 제곱은 양 끝 중 하나 */
        else       { out[--k] = y; hi--; }
    }
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x9E3779B9u;
static uint32_t rnd(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static int32_t rnd_in(int32_t lo, int32_t hi) {
    return (int32_t)(lo + (int64_t)(rnd() % (uint32_t)((int64_t)hi - lo + 1)));
}
static int32_t rnd_full(void) { return (int32_t)((int64_t)rnd() - 2147483648LL); }

static uint64_t ref_eq(const int32_t *a, size_t n, int64_t t, int le) {
    uint64_t c = 0;
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++) {
            int64_t s = (int64_t)a[i] + a[j];
            if (le ? s <= t : s == t) c++;
        }
    return c;
}
static int cmp_i64(const void *p, const void *q) {
    int64_t x = *(const int64_t *)p, y = *(const int64_t *)q;
    return (x > y) - (x < y);
}

#define N 80
int main(void) {
    static int32_t a[100000];
    int32_t c[N];
    int64_t o[N], r[N];

    /* 연습 1 */
    { int32_t x[] = { 1, 1, 2, 3, 3, 3, 4, 5, 5 };        /* t=6: (1,5)x4 + (2,4)x1 + (3,3)x3 = 8 */
      CHECK(count_pairs_eq(x, 9, 6) == 8); }
    { int32_t x[] = { INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX };   /* MIN+MAX = -1 → 4쌍 */
      CHECK(count_pairs_eq(x, 4, -1) == 4);
      CHECK(count_pairs_eq(x, 4, -2) == 0); }               /* MAX+MAX 가 wrap 하면 -2 */
    for (size_t i = 0; i < 100000; i++) a[i] = 0;           /* 모두 같다: C(100000, 2) */
    CHECK(count_pairs_eq(a, 100000, 0) == 4999950000ull);   /* 2^32 보다 크다 — 32비트 카운터면 틀림 */
    {   int bad = 0;
        for (int k = 0; k < 20000; k++) {
            size_t n = rnd() % 41;
            for (size_t i = 0; i < n; i++) c[i] = rnd_in(-6, 6);
            sort_i32(c, n);
            int32_t t = rnd_in(-13, 13);
            if (count_pairs_eq(c, n, t) != ref_eq(c, n, t, 0)) bad++;
        }
        printf("ex1 pairs_eq : 20000 random vs O(n^2), mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    /* 연습 2 */
    { int32_t x[] = { 100, 200, 600, 900, 1400 };          /* MTU 1500 에 둘 묶기 → 7쌍 */
      CHECK(count_pairs_le(x, 5, 1500) == 7); }
    {   int bad = 0;
        for (int k = 0; k < 20000; k++) {
            size_t n = rnd() % 41;
            for (size_t i = 0; i < n; i++) c[i] = (k & 1) ? rnd_full() : rnd_in(-50, 50);
            sort_i32(c, n);
            int64_t t = (k & 1) ? (int64_t)rnd_full() * 2 : rnd_in(-100, 100);
            if (count_pairs_le(c, n, t) != ref_eq(c, n, t, 1)) bad++;
        }
        printf("ex2 pairs_le : 20000 random (half full-range) vs O(n^2), mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    /* 연습 3 */
    { int32_t x[] = { 512, -32768, 515, -32768, -32768, 514 };
      size_t k = remove_value(x, 6, -32768);
      CHECK(k == 3 && x[0] == 512 && x[1] == 515 && x[2] == 514); }
    {   int bad = 0;
        for (int k = 0; k < 10000; k++) {
            size_t n = rnd() % 51, m = 0;
            int32_t v = rnd_in(-2, 2), ref[64];
            for (size_t i = 0; i < n; i++) { c[i] = rnd_in(-3, 3); if (c[i] != v) ref[m++] = c[i]; }
            size_t w = remove_value(c, n, v);
            if (w != m || memcmp(c, ref, m * sizeof c[0]) != 0) bad++;
        }
        printf("ex3 remove   : 10000 random vs copy-filter, mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    /* 연습 4 */
    { int32_t x[] = { -4, -1, 0, 3, 10 };                   /* LeetCode 977 예 → 0 1 9 16 100 */
      sorted_squares(x, 5, o);
      CHECK(o[0] == 0 && o[1] == 1 && o[2] == 9 && o[3] == 16 && o[4] == 100); }
    { int32_t x[] = { INT32_MIN, INT32_MAX };               /* 2^62 와 (2^31-1)^2 */
      sorted_squares(x, 2, o);
      CHECK(o[0] == (int64_t)INT32_MAX * INT32_MAX && o[1] == 4611686018427387904LL); }
    {   int bad = 0;
        for (int k = 0; k < 10000; k++) {
            size_t n = rnd() % 51;
            for (size_t i = 0; i < n; i++) c[i] = (k & 1) ? rnd_full() : rnd_in(-20, 20);
            sort_i32(c, n);
            for (size_t i = 0; i < n; i++) r[i] = (int64_t)c[i] * c[i];
            if (n > 1) qsort(r, n, sizeof r[0], cmp_i64);
            sorted_squares(c, n, o);
            if (memcmp(o, r, n * sizeof o[0]) != 0) bad++;
        }
        printf("ex4 squares  : 10000 random vs square+qsort, mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
