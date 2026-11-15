/* ALGO-B02 추가 연습 — 한 번에 검증
 *   연습 1  search_range: x 의 [첫, 끝] 인덱스 (LeetCode 34)
 *   연습 2  분기 없는 lower_bound: 반복 횟수가 데이터와 무관 (ceil(log2 n))
 *   연습 3  중복 있는 회전 배열 탐색 (LeetCode 81) + 최악 O(n) 탐침 수 실측
 *   연습 4  두 단계 회전 탐색: 회전점(LeetCode 153) + lower_bound
 *   연습 5  원형 플래시 로그: 가장 오래된 레코드(seq 회전점) + ts >= T 인 첫 레코드
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x9E3779B9u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}

/* 본문 Part A/B 와 같은 구현 (이 파일만으로 컴파일되도록 복사) */
static size_t lower_bound_i32(const int32_t *a, size_t n, int32_t x) {
    size_t lo = 0, hi = n;
    while (lo < hi) { size_t mid = lo + (hi - lo) / 2; if (a[mid] < x) lo = mid + 1; else hi = mid; }
    return lo;
}
static size_t upper_bound_i32(const int32_t *a, size_t n, int32_t x) {
    size_t lo = 0, hi = n;
    while (lo < hi) { size_t mid = lo + (hi - lo) / 2; if (a[mid] <= x) lo = mid + 1; else hi = mid; }
    return lo;
}

/* ---------- 연습 1: search_range ---------- */
static void search_range(const int32_t *a, size_t n, int32_t x, ptrdiff_t *first, ptrdiff_t *last) {
    size_t l = lower_bound_i32(a, n, x), u = upper_bound_i32(a, n, x);
    if (l == u) { *first = -1; *last = -1; }          /* 개수 = u - l = 0 */
    else        { *first = (ptrdiff_t)l; *last = (ptrdiff_t)u - 1; }
}

/* ---------- 연습 2: 분기 없는 lower_bound ---------- */
/* 불변식: 답은 [base, base+len] 안. len 이 ceil(len/2) 로 줄어든다 → 반복 횟수는 n 에만 의존 */
static size_t lb_branchless(const int32_t *a, size_t n, int32_t x, unsigned *iters) {
    *iters = 0;
    if (n == 0) return 0;
    const int32_t *base = a;
    size_t len = n;
    while (len > 1) {
        size_t half = len / 2;
        base = (base[half] < x) ? base + half : base;  /* 조건 선택 → arm64 에서 csel (asm 확인은 md) */
        len -= half;
        (*iters)++;
    }
    return (size_t)(base - a) + (*base < x);
}

/* ---------- 연습 3: 중복 있는 회전 배열 (있으면 1) ---------- */
static int rot_contains_dup(const int32_t *a, size_t n, int32_t x, unsigned *probes) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        (*probes)++;
        if (a[mid] == x) return 1;
        if (a[lo] == a[mid] && a[mid] == a[hi - 1]) { lo++; hi--; continue; }   /* 어느 쪽이 정렬됐는지 모름 → 양 끝 하나씩 버림 */
        if (a[lo] <= a[mid]) {
            if (a[lo] <= x && x < a[mid]) hi = mid; else lo = mid + 1;
        } else {
            if (a[mid] < x && x <= a[hi - 1]) lo = mid + 1; else hi = mid;
        }
    }
    return 0;
}

/* ---------- 연습 4: 두 단계 회전 탐색 (값은 모두 다름) ---------- */
static size_t rot_min_index(const int32_t *a, size_t n) {
    if (n == 0) return 0;
    size_t lo = 0, hi = n;
    while (lo < hi) { size_t mid = lo + (hi - lo) / 2; if (a[mid] > a[n - 1]) lo = mid + 1; else hi = mid; }
    return lo;
}
static ptrdiff_t rot_search_two_step(const int32_t *a, size_t n, int32_t x) {
    if (n == 0) return -1;
    size_t k = rot_min_index(a, n);                 /* [k, n) 와 [0, k) 가 각각 정렬됨 */
    size_t lo = (x <= a[n - 1]) ? k : 0, hi = (x <= a[n - 1]) ? n : k;
    size_t i = lo + lower_bound_i32(a + lo, hi - lo, x);
    return (i < hi && a[i] == x) ? (ptrdiff_t)i : -1;
}

/* ---------- 연습 5: 원형 플래시 로그 ---------- */
typedef struct { uint32_t seq; uint32_t ts_ms; } ring_rec_t;
/* 가득 찬 링: seq 는 논리 순서로 증가(서로 다름), 물리 배열은 그 회전 */
static size_t ring_oldest(const ring_rec_t *r, size_t n) {
    if (n == 0) return 0;
    size_t lo = 0, hi = n;
    while (lo < hi) { size_t mid = lo + (hi - lo) / 2; if (r[mid].seq > r[n - 1].seq) lo = mid + 1; else hi = mid; }
    return lo;
}
/* ts >= t 인 첫 레코드의 "논리" 인덱스 (0 = 가장 오래된 것), 없으면 n. 물리 = (oldest + i) % n */
static size_t ring_first_at_or_after(const ring_rec_t *r, size_t n, uint32_t t) {
    size_t old = ring_oldest(r, n), lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        size_t p = old + mid; if (p >= n) p -= n;   /* % 대신 비교+뺄셈 (나눗셈기 없는 MCU) */
        if (r[p].ts_ms < t) lo = mid + 1; else hi = mid;
    }
    return lo;
}

#define MAXN 50

int main(void) {
    /* 연습 1 */
    {
        const int32_t a[] = { 5, 7, 7, 8, 8, 10 };
        ptrdiff_t f, l;
        search_range(a, 6, 8, &f, &l);  CHECK(f == 3 && l == 4);
        search_range(a, 6, 6, &f, &l);  CHECK(f == -1 && l == -1);
        search_range(NULL, 0, 0, &f, &l); CHECK(f == -1 && l == -1);
        int32_t b[MAXN];
        unsigned long bad = 0;
        for (size_t n = 0; n <= MAXN; n++) {
            int32_t v = 0;
            for (size_t i = 0; i < n; i++) { v += (int32_t)(xorshift32() % 3); b[i] = v; }
            for (int32_t x = -1; x <= v + 1; x++) {
                ptrdiff_t wf = -1, wl = -1;
                for (size_t i = 0; i < n; i++) if (b[i] == x) { if (wf < 0) wf = (ptrdiff_t)i; wl = (ptrdiff_t)i; }
                search_range(b, n, x, &f, &l);
                if (f != wf || l != wl) bad++;
            }
        }
        CHECK(bad == 0);
    }

    /* 연습 2: 전수 대조 + 반복 횟수가 x 와 무관한지 */
    {
        int32_t b[MAXN];
        unsigned long bad = 0, iter_vary = 0;
        for (size_t n = 0; n <= MAXN; n++) {
            int32_t v = -10;
            for (size_t i = 0; i < n; i++) { v += (int32_t)(xorshift32() % 3); b[i] = v; }
            unsigned first_it = 0, it;
            for (int32_t x = -11; x <= v + 1; x++) {
                if (lb_branchless(b, n, x, &it) != lower_bound_i32(b, n, x)) bad++;
                if (x == -11) first_it = it; else if (it != first_it) iter_vary++;
            }
            unsigned want = 0; size_t m = n; while (m > 1) { m -= m / 2; want++; }   /* ceil(log2 n) */
            if (first_it != want) iter_vary++;
        }
        CHECK(bad == 0);
        CHECK(iter_vary == 0);
        const int32_t e[3] = { INT32_MIN, 0, INT32_MAX };
        unsigned it;
        CHECK(lb_branchless(e, 3, INT32_MAX, &it) == 2 && lb_branchless(e, 3, INT32_MIN, &it) == 0);
    }

    /* 연습 3: 중복 회전 배열 전수 대조 + 최악 탐침 수 */
    {
        int32_t base[MAXN], a[MAXN];
        unsigned long bad = 0, queries = 0;
        unsigned pr;
        for (size_t n = 0; n <= 30; n++) {
            int32_t v = 0;
            for (size_t i = 0; i < n; i++) { v += (int32_t)(xorshift32() % 4 == 0); base[i] = v; }   /* 중복 많음 */
            for (size_t r = 0; r < (n ? n : 1); r++) {
                for (size_t i = 0; i < n; i++) a[i] = base[(i + r) % n];
                for (int32_t x = -1; x <= v + 1; x++) {
                    int want = 0;
                    for (size_t i = 0; i < n; i++) want |= (a[i] == x);
                    pr = 0;
                    if (rot_contains_dup(a, n, x, &pr) != want) bad++;
                    queries++;
                }
            }
        }
        CHECK(bad == 0);
        /* 적대적 입력: 길이 50, 전부 1 이고 한 칸만 0. 0 을 찾는다 */
        unsigned worst = 0;
        for (size_t p = 0; p < MAXN; p++) {
            for (size_t i = 0; i < MAXN; i++) a[i] = (i == p) ? 0 : 1;
            pr = 0;
            int ok = rot_contains_dup(a, MAXN, 0, &pr);
            if (!ok) bad++;
            if (pr > worst) worst = pr;
        }
        unsigned absent = 0;
        for (size_t i = 0; i < MAXN; i++) a[i] = 1;
        rot_contains_dup(a, MAXN, 2, &absent);
        printf("연습 3: %lu queries, mismatches=%lu; n=50 adversarial worst probes=%u, all-equal absent probes=%u\n",
               queries, bad, worst, absent);
        CHECK(bad == 0);
        CHECK(worst > 6);                   /* 값이 다를 때의 상한 ceil(log2 51) = 6 을 넘는다 = O(n) 쪽 */
    }

    /* 연습 4: 두 단계 vs 선형 */
    {
        int32_t a[MAXN];
        unsigned long bad = 0;
        for (size_t n = 0; n <= MAXN; n++)
            for (size_t r = 0; r < (n ? n : 1); r++) {
                for (size_t i = 0; i < n; i++) a[i] = 3 * (int32_t)((i + r) % n) - 40;
                for (int32_t x = -42; x <= 3 * (int32_t)n - 40; x++) {
                    ptrdiff_t want = -1;
                    for (size_t i = 0; i < n; i++) if (a[i] == x) { want = (ptrdiff_t)i; break; }
                    if (rot_search_two_step(a, n, x) != want) bad++;
                }
            }
        CHECK(bad == 0);
    }

    /* 연습 5: 원형 로그, 길이 0~40 모든 회전 */
    {
        ring_rec_t logical[40], ring[40];
        unsigned long bad_old = 0, bad_ts = 0;
        for (size_t n = 0; n <= 40; n++) {
            uint32_t ts = 1000;
            for (size_t i = 0; i < n; i++) { ts += xorshift32() % 3; logical[i] = (ring_rec_t){ 500u + (uint32_t)i, ts }; }
            for (size_t k = 0; k < (n ? n : 1); k++) {          /* k = 가장 오래된 레코드의 물리 위치 */
                for (size_t i = 0; i < n; i++) ring[(k + i) % n] = logical[i];
                if (n && ring_oldest(ring, n) != k) bad_old++;
                for (uint32_t t = 999; t <= ts + 1; t++) {
                    size_t want = 0; while (want < n && logical[want].ts_ms < t) want++;
                    if (ring_first_at_or_after(ring, n, t) != want) bad_ts++;
                }
            }
        }
        CHECK(bad_old == 0);
        CHECK(bad_ts == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
