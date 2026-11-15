/* ALGO-B02 이진 탐색과 경계: lower_bound / upper_bound
 *
 * 반개구간 [lo, hi) 불변식 하나로 네 가지 탐색을 짠다.
 *   Part A  lower_bound / upper_bound / find_exact(첫 등장 인덱스 또는 -1)
 *   Part B  회전 정렬 배열(값은 모두 다름): rot_min_index(회전점) / rot_search(한 번의 이진 탐색)
 *   Part C  플래시 로그: ts >= T 인 첫 레코드, [t0, t1) 개수, 순차 기록 영역의 첫 지워진(erased) 페이지
 *           — 플래시 읽기 횟수를 세어 ceil(log2(n+1)) 이하인지 검사
 *
 * 테스트: 길이 0~50 모든 배열(중복 포함, 결정적 생성) × min-1..max+1 모든 타깃을 선형 탐색과 전수 대조.
 *         회전 배열은 길이 0~50 의 모든 회전. 동적 할당 없음.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st   (solution.c 도 같은 명령)
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ======================= Part A: 경계 탐색 ======================= */
/* 불변식: [0, lo) 는 모두 < x,  [hi, n) 는 모두 >= x,  [lo, hi) 는 아직 모름.  끝나면 lo == hi = 답 */
size_t lower_bound_i32(const int32_t *a, size_t n, int32_t x) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;      /* lo <= mid < hi : 항상 유효한 인덱스 */
        if (a[mid] < x) lo = mid + 1;         /* a[mid] 까지 < x 쪽으로 */
        else            hi = mid;             /* a[mid] 부터 >= x 쪽으로 */
    }
    return lo;
}

/* 불변식: [0, lo) 는 모두 <= x,  [hi, n) 는 모두 > x */
size_t upper_bound_i32(const int32_t *a, size_t n, int32_t x) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (a[mid] <= x) lo = mid + 1;
        else             hi = mid;
    }
    return lo;
}

/* x 의 첫 등장 인덱스, 없으면 -1 */
ptrdiff_t find_exact(const int32_t *a, size_t n, int32_t x) {
    size_t i = lower_bound_i32(a, n, x);
    return (i < n && a[i] == x) ? (ptrdiff_t)i : -1;
}

/* ======================= Part B: 회전 정렬 배열 (값은 모두 다름) ======================= */
/* 최솟값 인덱스 = "a[i] <= a[n-1]" 이 처음 참이 되는 i  (앞 블록은 전부 > a[n-1], 뒤 블록은 전부 <= a[n-1]).
 * n == 0 이면 0. 회전 안 됨이면 0. */
size_t rot_min_index(const int32_t *a, size_t n) {
    if (n == 0) return 0;
    int32_t last = a[n - 1];
    size_t lo = 0, hi = n;                    /* [0,lo) : > last,  [hi,n) : <= last */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (a[mid] > last) lo = mid + 1;
        else               hi = mid;
    }
    return lo;
}

/* 한 번의 이진 탐색: mid 를 기준으로 [lo, mid] 와 [mid, hi) 중 하나는 반드시 정렬되어 있다. 없으면 -1 */
ptrdiff_t rot_search(const int32_t *a, size_t n, int32_t x) {
    size_t lo = 0, hi = n;                    /* 답이 있다면 [lo, hi) 안에 있다 */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (a[mid] == x) return (ptrdiff_t)mid;
        if (a[lo] <= a[mid]) {                /* 왼쪽 [lo, mid] 정렬됨 */
            if (a[lo] <= x && x < a[mid]) hi = mid;
            else                          lo = mid + 1;
        } else {                              /* 오른쪽 [mid, hi-1] 정렬됨 */
            if (a[mid] < x && x <= a[hi - 1]) lo = mid + 1;
            else                              hi = mid;
        }
    }
    return -1;
}

/* ======================= Part C: 플래시 상주 로그 ======================= */
typedef struct { uint32_t ts_ms; uint16_t event; uint16_t arg; } log_rec_t;   /* 8 B, ts 오름차순(같은 ms 중복 가능) */

typedef struct {
    const log_rec_t *rec;     /* 실제 칩이라면 XIP 플래시 주소 또는 SPI NOR 읽기 */
    size_t n;
    unsigned reads;           /* 플래시 읽기 횟수 (테스트가 확인) */
} flash_log_t;

static uint32_t log_read_ts(flash_log_t *f, size_t i) { f->reads++; return f->rec[i].ts_ms; }

/* ts >= t 인 첫 레코드 인덱스, 없으면 f->n. 반드시 log_read_ts 로만 읽는다. */
size_t log_first_at_or_after(flash_log_t *f, uint32_t t) {
    size_t lo = 0, hi = f->n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (log_read_ts(f, mid) < t) lo = mid + 1;
        else                         hi = mid;
    }
    return lo;
}

/* t0 <= ts < t1 인 레코드 개수 (t1 <= t0 이면 0) */
size_t log_count_in(flash_log_t *f, uint32_t t0, uint32_t t1) {
    if (t1 <= t0) return 0;
    return log_first_at_or_after(f, t1) - log_first_at_or_after(f, t0);
}

/* 순차 기록 영역: 페이지 0..k-1 은 기록됨, k..N-1 은 지워짐(헤더 워드 == 0xFFFFFFFF). k 를 찾는다. */
typedef struct {
    const uint32_t *page_hdr; /* 각 페이지의 첫 워드 (헤더) */
    size_t npages;
    unsigned reads;
} flash_region_t;

static int page_is_erased(flash_region_t *r, size_t p) { r->reads++; return r->page_hdr[p] == 0xFFFFFFFFu; }

size_t first_erased_page(flash_region_t *r) {
    size_t lo = 0, hi = r->npages;            /* [0,lo) : 기록됨,  [hi,N) : 지워짐 */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (!page_is_erased(r, mid)) lo = mid + 1;
        else                         hi = mid;
    }
    return lo;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}

/* 선형 기준 구현 (테스트 쪽) */
static size_t ref_lower(const int32_t *a, size_t n, int32_t x) { size_t i = 0; while (i < n && a[i] < x) i++; return i; }
static size_t ref_upper(const int32_t *a, size_t n, int32_t x) { size_t i = 0; while (i < n && a[i] <= x) i++; return i; }
static ptrdiff_t ref_find(const int32_t *a, size_t n, int32_t x) {
    for (size_t i = 0; i < n; i++) if (a[i] == x) return (ptrdiff_t)i;
    return -1;
}
static size_t ref_argmin(const int32_t *a, size_t n) {
    size_t k = 0;
    for (size_t i = 1; i < n; i++) if (a[i] < a[k]) k = i;
    return k;
}

/* ceil(log2(n+1)) = n 을 이진수로 쓸 때의 비트 수 */
static unsigned probe_bound(size_t n) { unsigned b = 0; while (n) { b++; n >>= 1; } return b; }

#define MAXN 50
enum { PAT_RANDOM_DUP, PAT_ALL_EQUAL, PAT_STEP3, PAT_NEAR_MAX, PAT_NEAR_MIN, PAT_COUNT };

/* 길이 n, 패턴 p 의 비내림차순 배열 (결정적) */
static void make_sorted(int32_t *a, size_t n, int p) {
    int32_t v;
    switch (p) {
    case PAT_RANDOM_DUP: v = -(int32_t)n;   for (size_t i = 0; i < n; i++) { v += (int32_t)(xorshift32() % 3); a[i] = v; } break;
    case PAT_ALL_EQUAL:                     for (size_t i = 0; i < n; i++) a[i] = 7; break;
    case PAT_STEP3:                         for (size_t i = 0; i < n; i++) a[i] = 3 * (int32_t)i - 40; break;
    case PAT_NEAR_MAX: v = INT32_MAX - (int32_t)n; for (size_t i = 0; i < n; i++) { v += (int32_t)(xorshift32() % 2); a[i] = v; }
                       if (n) a[n - 1] = INT32_MAX; break;
    default:           v = INT32_MIN;       for (size_t i = 0; i < n; i++) { a[i] = v; v += (int32_t)(xorshift32() % 2); } break;
    }
}

int main(void) {
    /* ---- Part A: 이름 붙은 경계 사례 ---- */
    const int32_t one[1] = { 5 };
    const int32_t dup[8] = { 1, 2, 2, 2, 5, 5, 9, 9 };
    const int32_t ext[4] = { INT32_MIN, INT32_MIN, 0, INT32_MAX };
    CHECK(lower_bound_i32(NULL, 0, 3) == 0 && upper_bound_i32(NULL, 0, 3) == 0 && find_exact(NULL, 0, 3) == -1);
    CHECK(lower_bound_i32(one, 1, 4) == 0 && lower_bound_i32(one, 1, 5) == 0 && lower_bound_i32(one, 1, 6) == 1);
    CHECK(upper_bound_i32(one, 1, 4) == 0 && upper_bound_i32(one, 1, 5) == 1 && upper_bound_i32(one, 1, 6) == 1);
    CHECK(lower_bound_i32(dup, 8, 2) == 1 && upper_bound_i32(dup, 8, 2) == 4);   /* 2 가 3개 = 4 - 1 */
    CHECK(lower_bound_i32(dup, 8, 3) == 4 && upper_bound_i32(dup, 8, 3) == 4);   /* 없는 값: 둘이 같다 */
    CHECK(lower_bound_i32(dup, 8, 9) == 6 && upper_bound_i32(dup, 8, 9) == 8);
    CHECK(find_exact(dup, 8, 5) == 4 && find_exact(dup, 8, 9) == 6 && find_exact(dup, 8, 3) == -1 && find_exact(dup, 8, 10) == -1);
    CHECK(lower_bound_i32(ext, 4, INT32_MIN) == 0 && upper_bound_i32(ext, 4, INT32_MIN) == 2);
    CHECK(lower_bound_i32(ext, 4, INT32_MAX) == 3 && upper_bound_i32(ext, 4, INT32_MAX) == 4 && find_exact(ext, 4, INT32_MAX) == 3);

    /* ---- Part A: 길이 0~50 × 패턴 5개 × 모든 타깃 전수 대조 ---- */
    {
        int32_t a[MAXN];
        unsigned long arrays = 0, queries = 0, bad_lb = 0, bad_ub = 0, bad_find = 0;
        for (size_t n = 0; n <= MAXN; n++)
            for (int p = 0; p < PAT_COUNT; p++) {
                make_sorted(a, n, p);
                arrays++;
                int64_t mn = n ? a[0] : 0, mx = n ? a[n - 1] : 0;
                for (int64_t t = mn - 1; t <= mx + 1; t++) {
                    if (t < INT32_MIN || t > INT32_MAX) continue;
                    int32_t x = (int32_t)t;
                    queries++;
                    if (lower_bound_i32(a, n, x) != ref_lower(a, n, x)) bad_lb++;
                    if (upper_bound_i32(a, n, x) != ref_upper(a, n, x)) bad_ub++;
                    if (find_exact(a, n, x) != ref_find(a, n, x)) bad_find++;
                }
            }
        printf("Part A exhaustive: %lu arrays, %lu queries, mismatches lower=%lu upper=%lu find=%lu\n",
               arrays, queries, bad_lb, bad_ub, bad_find);
        CHECK(bad_lb == 0);
        CHECK(bad_ub == 0);
        CHECK(bad_find == 0);
    }

    /* ---- Part B: 회전 배열 ---- */
    const int32_t r1[7] = { 4, 5, 6, 7, 0, 1, 2 };
    CHECK(rot_min_index(r1, 7) == 4 && rot_search(r1, 7, 0) == 4 && rot_search(r1, 7, 3) == -1);
    CHECK(rot_search(r1, 7, 4) == 0 && rot_search(r1, 7, 2) == 6 && rot_search(r1, 7, 7) == 3);
    CHECK(rot_min_index(NULL, 0) == 0 && rot_search(NULL, 0, 1) == -1);
    CHECK(rot_min_index(one, 1) == 0 && rot_search(one, 1, 5) == 0 && rot_search(one, 1, 6) == -1);
    {
        const int32_t r2[2] = { INT32_MAX, INT32_MIN };
        CHECK(rot_min_index(r2, 2) == 1 && rot_search(r2, 2, INT32_MIN) == 1 && rot_search(r2, 2, INT32_MAX) == 0);
    }
    {   /* 길이 0~50, 모든 회전, min-1..max+1 모든 타깃 */
        int32_t base[MAXN], a[MAXN];
        unsigned long cases = 0, queries = 0, bad_min = 0, bad_search = 0;
        for (size_t n = 0; n <= MAXN; n++) {
            for (size_t i = 0; i < n; i++) base[i] = 3 * (int32_t)i - 40;
            size_t nrot = n ? n : 1;
            for (size_t r = 0; r < nrot; r++) {
                for (size_t i = 0; i < n; i++) a[i] = base[(i + r) % n];   /* 왼쪽으로 r 칸 회전 */
                cases++;
                if (rot_min_index(a, n) != (n ? ref_argmin(a, n) : 0)) bad_min++;
                int64_t lo_t = n ? base[0] - 1 : -1, hi_t = n ? base[n - 1] + 1 : 1;
                for (int64_t t = lo_t; t <= hi_t; t++) {
                    queries++;
                    if (rot_search(a, n, (int32_t)t) != ref_find(a, n, (int32_t)t)) bad_search++;
                }
            }
        }
        printf("Part B exhaustive: %lu rotated arrays, %lu queries, mismatches min=%lu search=%lu\n",
               cases, queries, bad_min, bad_search);
        CHECK(bad_min == 0);
        CHECK(bad_search == 0);
    }

    /* ---- Part C: 플래시 로그 (1000 레코드, 같은 ms 중복 포함, ts 는 0xFFFF0000 부터 — UINT32_MAX 근처) ---- */
    {
        static log_rec_t recs[1000];
        uint32_t ts = 0xFFFF0000u;
        for (size_t i = 0; i < 1000; i++) {
            uint32_t r = xorshift32() % 8;
            ts += (r < 3) ? 0 : r * 7;           /* 약 3/8 확률로 같은 ms */
            recs[i] = (log_rec_t){ ts, (uint16_t)(i & 0xFF), (uint16_t)i };
        }
        unsigned long queries = 0, bad = 0, over_budget = 0, bad_cnt = 0;
        unsigned maxreads = 0;
        flash_log_t f = { recs, 1000, 0 };
        for (size_t i = 0; i <= 1000; i++) {
            uint32_t cand[4];
            int nc = 0;
            if (i < 1000) { cand[nc++] = recs[i].ts_ms - 1; cand[nc++] = recs[i].ts_ms; cand[nc++] = recs[i].ts_ms + 1; }
            else          { cand[nc++] = 0; cand[nc++] = UINT32_MAX; }
            for (int c = 0; c < nc; c++) {
                uint32_t t = cand[c];
                size_t want = 0; while (want < 1000 && recs[want].ts_ms < t) want++;
                f.reads = 0;
                size_t got = log_first_at_or_after(&f, t);
                queries++;
                if (got != want) bad++;
                if (f.reads > probe_bound(1000) || f.reads == 0) over_budget++;
                if (f.reads > maxreads) maxreads = f.reads;
                uint32_t t1 = t + 50u;           /* [t, t+50) 개수: 덧셈이 넘치면 t1 <= t → 0 */
                size_t cnt = 0;
                if (t1 > t) for (size_t k = 0; k < 1000; k++) cnt += (recs[k].ts_ms >= t && recs[k].ts_ms < t1);
                if (log_count_in(&f, t, t1) != cnt) bad_cnt++;
            }
        }
        printf("Part C log: %lu queries, mismatches=%lu count_mismatches=%lu, max flash reads=%u (bound %u)\n",
               queries, bad, bad_cnt, maxreads, probe_bound(1000));
        CHECK(bad == 0);
        CHECK(bad_cnt == 0);
        CHECK(over_budget == 0);
        flash_log_t empty = { NULL, 0, 0 };
        CHECK(log_first_at_or_after(&empty, 123) == 0 && empty.reads == 0);
    }
    {   /* 순차 기록 영역: N = 0..64 와 1024, 모든 기록 위치 k */
        static uint32_t hdr[1024];
        static const size_t sizes[] = { 0, 1, 2, 3, 7, 8, 63, 64, 1024 };
        unsigned long cases = 0, bad = 0, over_budget = 0;
        for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
            size_t N = sizes[s];
            for (size_t k = 0; k <= N; k++) {
                for (size_t p = 0; p < N; p++)
                    hdr[p] = (p < k) ? (0xA5000000u | (uint32_t)p) : 0xFFFFFFFFu;
                if (k > 0) hdr[k - 1] = 0xFFFF0000u;  /* 전원 차단으로 반쯤 쓰인 헤더: 0 비트가 있으니 '기록됨' */
                flash_region_t r = { hdr, N, 0 };
                cases++;
                if (first_erased_page(&r) != k) bad++;
                if (r.reads > probe_bound(N)) over_budget++;
            }
        }
        printf("Part C region: %lu cases, mismatches=%lu, over-budget=%lu\n", cases, bad, over_budget);
        CHECK(bad == 0);
        CHECK(over_budget == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
