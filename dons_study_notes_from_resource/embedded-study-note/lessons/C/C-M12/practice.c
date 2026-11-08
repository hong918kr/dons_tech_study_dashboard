/* C-M12 추가 연습 — 한 번에 검증
 *   연습 1  포화 덧셈/뺄셈 (DSP 의 QADD/QSUB 흉내)
 *   연습 2  size_t 덧셈/곱셈 검사 (calloc(n, sz) 크기 계산)
 *   연습 3  (lo + hi) / 2 없는 이진 탐색 (size_t 반열린 구간)
 *   연습 4  검사하는 나머지 + 음수 오프셋 링 버퍼 인덱스
 * 모두 UB 없이: strict 빌드 + -fsanitize=address,undefined 에서 깨끗해야 한다.
 */
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---------------- 연습 1: 포화 산술 ---------------- */
static int32_t sat_add_i32(int32_t a, int32_t b) {
    if (b > 0 && a > INT32_MAX - b) return INT32_MAX;
    if (b < 0 && a < INT32_MIN - b) return INT32_MIN;
    return a + b;
}
static int32_t sat_sub_i32(int32_t a, int32_t b) {
    if (b < 0 && a > INT32_MAX + b) return INT32_MAX;
    if (b > 0 && a < INT32_MIN + b) return INT32_MIN;
    return a - b;
}

/* ---------------- 연습 2: size_t 검사 ---------------- */
/* unsigned 는 넘치면 UB 가 아니라 "한 바퀴 돈다" — 정의된 동작이지만 크기 계산에선 똑같이 버그다 */
static bool add_size(size_t a, size_t b, size_t *out) {
    if (a > SIZE_MAX - b) return false;
    *out = a + b;
    return true;
}
static bool mul_size(size_t n, size_t sz, size_t *out) {
    if (sz != 0 && n > SIZE_MAX / sz) return false;
    *out = n * sz;
    return true;
}

/* ---------------- 연습 3: 이진 탐색 ---------------- */
/* 오름차순 a[0..n) 에서 key 의 인덱스, 없으면 n.  [lo, hi) 반열린 구간 + size_t → 음수도 넘침도 없다 */
static size_t bsearch_i32(const int32_t *a, size_t n, int32_t key) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;           /* (lo + hi) / 2 는 거대한 배열에서 넘친다 */
        if (a[mid] < key)      lo = mid + 1;
        else if (a[mid] > key) hi = mid;
        else                   return mid;
    }
    return n;
}

/* ---------------- 연습 4: 나머지와 링 인덱스 ---------------- */
/* INT32_MIN % -1 도 UB 다 (C11 6.5.5p6: a/b 가 표현 불가면 a/b 와 a%b 둘 다 UB) */
static bool rem_i32(int32_t a, int32_t b, int32_t *out) {
    if (b == 0 || (a == INT32_MIN && b == -1)) return false;
    *out = a % b;
    return true;
}
/* i 를 0..n-1 로 접는다 (수학의 mod, 음수 i 도). n > 0. 예: head 에서 -3 칸 뒤 */
static uint32_t wrap_index(int32_t i, uint32_t n) {
    if (i >= 0) return (uint32_t)i % n;
    uint32_t mag = 0u - (uint32_t)i;               /* |i| — int→unsigned 변환은 모듈로로 정의됨 */
    uint32_t r = mag % n;
    return r == 0 ? 0u : n - r;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0xC0FFEE11u;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int32_t rnd_i32(void) {
    uint32_t u = rnd();
    if (u & 1u) return (int32_t)(u >> 1) - (int32_t)(rnd() >> 1);
    return (u & 2u) ? INT32_MAX - (int32_t)(u % 128u) : INT32_MIN + (int32_t)(u % 128u);
}
static int32_t clamp64(int64_t v) { return v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : (int32_t)v; }

int main(void) {
    /* 연습 1 */
    CHECK(sat_add_i32(INT32_MAX, 1) == INT32_MAX && sat_add_i32(INT32_MIN, -1) == INT32_MIN);
    CHECK(sat_sub_i32(0, INT32_MIN) == INT32_MAX && sat_sub_i32(INT32_MIN, 1) == INT32_MIN);
    CHECK(sat_add_i32(-5, 3) == -2 && sat_sub_i32(-5, 3) == -8);
    {
        int bad = 0;
        for (int k = 0; k < 50000; k++) {
            int32_t a = rnd_i32(), b = rnd_i32();
            if (sat_add_i32(a, b) != clamp64((int64_t)a + b)) bad++;
            if (sat_sub_i32(a, b) != clamp64((int64_t)a - b)) bad++;
        }
        CHECK(bad == 0);
    }

    /* 연습 2 */
    size_t s = 0;
    CHECK(add_size(SIZE_MAX - 1, 1, &s) && s == SIZE_MAX && !add_size(SIZE_MAX, 1, &s));
    CHECK(mul_size(0, SIZE_MAX, &s) && s == 0 && mul_size(SIZE_MAX, 0, &s) && s == 0);
    CHECK(mul_size(SIZE_MAX / 16, 16, &s) && s == SIZE_MAX / 16 * 16);
    CHECK(!mul_size(SIZE_MAX / 16 + 1, 16, &s));
    CHECK(!mul_size((size_t)1 << (sizeof(size_t) * 4), (size_t)1 << (sizeof(size_t) * 4), &s)); /* 2^32 × 2^32 */

    /* 연습 3 */
    static int32_t arr[1000];
    for (int i = 0; i < 1000; i++) arr[i] = 3 * i - 1500;          /* -1500, -1497, ... */
    {
        int bad = 0;
        for (int i = 0; i < 1000; i++) if (bsearch_i32(arr, 1000, arr[i]) != (size_t)i) bad++;
        for (int i = 0; i < 1000; i++) if (bsearch_i32(arr, 1000, arr[i] + 1) != 1000) bad++;
        CHECK(bad == 0);
    }
    CHECK(bsearch_i32(arr, 1000, INT32_MIN) == 1000 && bsearch_i32(arr, 1000, INT32_MAX) == 1000);
    CHECK(bsearch_i32(arr, 0, 5) == 0 && bsearch_i32(NULL, 0, 5) == 0);  /* 빈 배열: 접근 0번 */
    CHECK(bsearch_i32(arr, 1, -1500) == 0);

    /* 연습 4 */
    int32_t r = 0;
    CHECK(!rem_i32(INT32_MIN, -1, &r) && !rem_i32(5, 0, &r));
    CHECK(rem_i32(-7, 2, &r) && r == -1 && rem_i32(7, -2, &r) && r == 1);   /* 부호는 피제수를 따른다 */
    CHECK(rem_i32(INT32_MIN, 1, &r) && r == 0 && rem_i32(INT32_MIN, INT32_MAX, &r) && r == -1);
    CHECK(wrap_index(-1, 8) == 7 && wrap_index(-8, 8) == 0 && wrap_index(-9, 8) == 7 && wrap_index(9, 8) == 1);
    CHECK(wrap_index(INT32_MIN, 8) == 0 && wrap_index(INT32_MIN, 7) == (uint32_t)((INT32_MIN % 7 + 7) % 7));
    {
        int bad = 0;
        static const uint32_t ns[] = { 1, 2, 3, 7, 8, 10, 1000, 65536, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu };
        for (int k = 0; k < 20000; k++) {
            int32_t i = rnd_i32();
            uint32_t n = ns[rnd() % (sizeof ns / sizeof ns[0])];
            int64_t want = ((int64_t)i % (int64_t)n + (int64_t)n) % (int64_t)n;
            if (wrap_index(i, n) != (uint32_t)want) bad++;
        }
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
