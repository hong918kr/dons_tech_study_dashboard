/* c2_bsearch.c — C2 노트의 코드 조각 검증
 * cc -std=c11 -O2 -Wall -Wextra -o c2 c2_bsearch.c && ./c2
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ---------- v0: "정확히 일치"만 찾는 이진 탐색 (시계열에는 틀렸다) ---------- */
static int find_exact(const uint64_t *a, unsigned n, uint64_t t)
{
    unsigned lo = 0, hi = n;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2u;
        if (a[mid] == t) return (int)mid;
        if (a[mid] < t)  lo = mid + 1u; else hi = mid;
    }
    return -1;                 /* 없음 */
}

/* ---------- v1: lower_bound / upper_bound ---------- */

/* a[i] >= t 인 첫 i. 없으면 n. */
static unsigned lower_bound(const uint64_t *a, unsigned n, uint64_t t)
{
    unsigned lo = 0, hi = n;                 /* 답은 항상 [lo, hi] 안에 있다 */
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2u;  /* lo <= mid < hi */
        if (a[mid] < t) lo = mid + 1u;       /* mid 는 답이 아니다 */
        else            hi = mid;            /* mid 는 답일 수 있다 */
    }
    return lo;
}

/* a[i] > t 인 첫 i. 곧 "t 이하인 원소의 개수". 없으면 n. */
static unsigned upper_bound(const uint64_t *a, unsigned n, uint64_t t)
{
    unsigned lo = 0, hi = n;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2u;
        if (a[mid] <= t) lo = mid + 1u;      /* 위와 완전히 같고, `<` 가 `<=` 로 바뀐 것뿐 */
        else             hi = mid;
    }
    return lo;
}

/* "t 이하 중 마지막" = upper_bound(t) - 1.  없으면 -1. */
static int last_le(const uint64_t *a, unsigned n, uint64_t t)
{
    unsigned k = upper_bound(a, n, t);
    return (k == 0u) ? -1 : (int)(k - 1u);
}

/* ---------- 손으로 추적하기: 한 단계씩 찍는 upper_bound ---------- */
static unsigned upper_bound_trace(const uint64_t *a, unsigned n, uint64_t t)
{
    unsigned lo = 0, hi = n, step = 0;
    printf("  upper_bound(t=%llu), n=%u\n", (unsigned long long)t, n);
    printf("  step | lo | hi | mid | a[mid] | a[mid] <= t ? | 다음\n");
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2u;
        bool le = a[mid] <= t;
        printf("  %4u | %2u | %2u | %3u | %6llu | %-13s | %s\n",
               ++step, lo, hi, mid, (unsigned long long)a[mid],
               le ? "yes" : "no", le ? "lo = mid+1" : "hi = mid");
        if (le) lo = mid + 1u; else hi = mid;
    }
    printf("  종료: lo == hi == %u\n", lo);
    return lo;
}

static unsigned lower_bound_trace(const uint64_t *a, unsigned n, uint64_t t)
{
    unsigned lo = 0, hi = n, step = 0;
    printf("  lower_bound(t=%llu), n=%u\n", (unsigned long long)t, n);
    printf("  step | lo | hi | mid | a[mid] | a[mid] < t ? | 다음\n");
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2u;
        bool lt = a[mid] < t;
        printf("  %4u | %2u | %2u | %3u | %6llu | %-12s | %s\n",
               ++step, lo, hi, mid, (unsigned long long)a[mid],
               lt ? "yes" : "no", lt ? "lo = mid+1" : "hi = mid");
        if (lt) lo = mid + 1u; else hi = mid;
    }
    printf("  종료: lo == hi == %u\n", lo);
    return lo;
}

/* ---------- 링버퍼 위에서의 이진 탐색 (01_gps_fix_cache 와 같은 모양) ---------- */
#define CAP  8u
#define MASK (CAP - 1u)
typedef struct { uint64_t ts[CAP]; uint32_t head, tail; } ring_t;

/* off 는 tail 기준 오프셋. head/tail 이 uint32 를 넘겨 감싸도 탐색은 영향이 없다. */
static uint64_t ring_at(const ring_t *r, uint32_t off) { return r->ts[(r->tail + off) & MASK]; }

static uint32_t ring_upper_bound(const ring_t *r, uint32_t n, uint64_t t)
{
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (ring_at(r, mid) <= t) lo = mid + 1u; else hi = mid;
    }
    return lo;
}

/* ---------- brute force 와 비교하는 테스트 ---------- */
static unsigned bf_lower(const uint64_t *a, unsigned n, uint64_t t)
{
    for (unsigned i = 0; i < n; i++) if (a[i] >= t) return i;
    return n;
}
static unsigned bf_upper(const uint64_t *a, unsigned n, uint64_t t)
{
    for (unsigned i = 0; i < n; i++) if (a[i] > t) return i;
    return n;
}

static uint32_t rng_state = 12345u;
static uint32_t xr(void)                    /* xorshift32, 고정 시드 */
{
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

int main(void)
{
    /* 중복이 있는 작은 배열 — 손으로 추적하기용 */
    const uint64_t a[] = {100, 200, 200, 350, 500};
    const unsigned n = 5;

    puts("== v0: 정확히 일치만 찾는 이진 탐색 ==");
    printf("find_exact(200) = %d   (있으니 찾는다)\n", find_exact(a, n, 200));
    printf("find_exact(300) = %d   (없다고 답한다 — 시계열에서는 이게 버그다)\n",
           find_exact(a, n, 300));

    puts("\n== v1: lower_bound / upper_bound / last_le ==");
    printf("%-6s %-13s %-13s %-9s %s\n", "t", "lower_bound", "upper_bound", "last_le", "그 시각의 값");
    const uint64_t qs[] = {50, 100, 150, 200, 300, 500, 900};
    for (unsigned i = 0; i < 7; i++) {
        uint64_t t = qs[i];
        int k = last_le(a, n, t);
        char val[48];
        if (k < 0) snprintf(val, sizeof val, "없음 (첫 샘플 이전)");
        else       snprintf(val, sizeof val, "a[%d] = %llu", k, (unsigned long long)a[k]);
        printf("%-6llu %-13u %-13u %-9d %s\n", (unsigned long long)t,
               lower_bound(a, n, t), upper_bound(a, n, t), k, val);
    }

    puts("\n== 손으로 추적 (표를 그대로 노트에 옮긴다) ==");
    upper_bound_trace(a, n, 300);
    puts("");
    lower_bound_trace(a, n, 200);
    puts("");
    upper_bound_trace(a, n, 200);
    puts("  -> 같은 t=200 인데 lower_bound 는 1, upper_bound 는 3. 중복 3개가 그 사이에 있다.");

    puts("\n== 링버퍼 위에서 (감싼 상태) ==");
    ring_t r = {{0}, 0, 0};
    /* 12개를 넣어 3개를 덮어쓴다: 남는 것은 ts = 500..1200 */
    for (int i = 1; i <= 12; i++) {
        if (r.head - r.tail == CAP) r.tail++;
        r.ts[r.head & MASK] = (uint64_t)i * 100u;
        r.head++;
    }
    uint32_t cnt = r.head - r.tail;
    printf("head=%u tail=%u count=%u  내용:", r.head, r.tail, cnt);
    for (uint32_t i = 0; i < cnt; i++) printf(" %llu", (unsigned long long)ring_at(&r, i));
    puts("");
    for (uint64_t t = 400; t <= 1300; t += 250) {
        uint32_t k = ring_upper_bound(&r, cnt, t);
        if (k == 0) printf("t=%4llu -> 창보다 이전 (답 없음)\n", (unsigned long long)t);
        else printf("t=%4llu -> off=%u, 값 %llu\n", (unsigned long long)t, k - 1u,
                    (unsigned long long)ring_at(&r, k - 1u));
    }

    puts("\n== brute force 와 비교 (무작위 200,000 회) ==");
    unsigned trials = 0, fails = 0;
    for (int it = 0; it < 20000; it++) {
        uint64_t arr[16];
        unsigned m = xr() % 17u;                       /* 0..16, 빈 배열 포함 */
        uint64_t cur = xr() % 5u;
        for (unsigned i = 0; i < m; i++) { cur += xr() % 4u; arr[i] = cur; }  /* 비감소 */
        for (int q = 0; q < 10; q++) {
            uint64_t t = xr() % 64u;
            trials++;
            if (lower_bound(arr, m, t) != bf_lower(arr, m, t)) fails++;
            if (upper_bound(arr, m, t) != bf_upper(arr, m, t)) fails++;
        }
    }
    printf("%u 회 비교, 불일치 %u 건 -> %s\n", trials * 2u, fails,
           fails ? "FAIL" : "PASS");
    return 0;
}
