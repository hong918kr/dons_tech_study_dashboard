/* ARM-B05 추가 연습 검증: practice.S + shared_bugs.S 와 함께 빌드
 *   cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-B05_practice
 */
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int64_t cmp3_s64(int64_t a, int64_t b);
int     in_range_u(uint64_t x, uint64_t lo, uint64_t hi);
int     in_range_s(int64_t x, int64_t lo, int64_t hi);
size_t  count_neg(const int64_t *a, size_t n);
int64_t abs_sat(int64_t x);

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t xs(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static int64_t rnd_s(void) {
    switch (xs() & 3) {
    case 0:  return (int64_t)xs();
    case 1:  return (int64_t)(xs() % 17) - 8;
    case 2:  return INT64_MAX - (int64_t)(xs() % 8);
    default: return INT64_MIN + (int64_t)(xs() % 8);
    }
}
static const int64_t EDGE[] = { 0, 1, -1, 2, -2, 7, -7, INT64_MAX, INT64_MAX - 1, INT64_MIN, INT64_MIN + 1,
                                0x7FFFFFFF, 0x80000000LL, 0xFFFFFFFFLL, -0x80000000LL };
#define NEDGE (sizeof EDGE / sizeof EDGE[0])
#define NRAND 100000
static int64_t pick(int k) { return k < (int)NEDGE ? EDGE[k] : rnd_s(); }

int main(void) {
    /* 연습 1 */
    CHECK(cmp3_s64(-1, 1) == -1 && cmp3_s64(1, -1) == 1 && cmp3_s64(5, 5) == 0);
    CHECK(cmp3_s64(INT64_MIN, INT64_MAX) == -1 && cmp3_s64(INT64_MAX, INT64_MIN) == 1);   /* a-b 가 넘쳐도 맞다 */
    { int bad = 0; for (int i = 0; i < (int)NEDGE + 300; i++) for (int j = 0; j < (int)NEDGE + 300; j++) {
          int64_t a = pick(i), b = pick(j); bad += cmp3_s64(a, b) != (int64_t)((a > b) - (a < b)); }
      CHECK(bad == 0); }
    /* 연습 2 */
    CHECK(in_range_u(5, 0, 10) && !in_range_u(11, 0, 10) && in_range_u(10, 0, 10) && in_range_u(0, 0, 10));
    CHECK(!in_range_u(2, 3, 9) && in_range_u(UINT64_MAX, 0, UINT64_MAX) && !in_range_u(0, 1, UINT64_MAX));
    { int bad = 0; for (int k = 0; k < NRAND; k++) {
          uint64_t x = (uint64_t)rnd_s(), lo = (uint64_t)rnd_s(), hi = (uint64_t)rnd_s();
          if (lo > hi) { uint64_t t = lo; lo = hi; hi = t; }                /* 전제 lo <= hi */
          bad += in_range_u(x, lo, hi) != (lo <= x && x <= hi); }
      CHECK(bad == 0); }
    /* 연습 3 */
    CHECK(in_range_s(-3, -5, 5) && !in_range_s(-6, -5, 5) && !in_range_s(6, -5, 5) && in_range_s(5, -5, 5));
    CHECK(!in_range_s(0, 5, -5));                                            /* lo > hi → 항상 0 */
    { int bad = 0; for (int i = 0; i < (int)NEDGE + 40; i++) for (int j = 0; j < (int)NEDGE + 40; j++) for (int k = 0; k < (int)NEDGE + 40; k++) {
          int64_t x = pick(k), lo = pick(i), hi = pick(j); bad += in_range_s(x, lo, hi) != (lo <= x && x <= hi); }
      CHECK(bad == 0); }
    /* 연습 4 */
    {
        static int64_t arr[1000];
        size_t want = 0;
        for (int k = 0; k < 1000; k++) { arr[k] = rnd_s(); want += arr[k] < 0; }
        const int64_t e[4] = {0, -1, INT64_MIN, INT64_MAX};
        CHECK(count_neg(NULL, 0) == 0 && count_neg(e, 4) == 2 && count_neg(e, 1) == 0);
        CHECK(count_neg(arr, 1000) == want);
    }
    /* 연습 5: INT64_MIN 은 C 로 계산하지 않고 손으로 */
    CHECK(abs_sat(INT64_MIN) == INT64_MAX && abs_sat(INT64_MIN + 1) == INT64_MAX && abs_sat(-7) == 7 && abs_sat(0) == 0);
    { int bad = 0; for (int k = 0; k < NRAND; k++) { int64_t x = rnd_s();
          int64_t want = x == INT64_MIN ? INT64_MAX : (x < 0 ? -x : x); bad += abs_sat(x) != want; }
      CHECK(bad == 0); }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
