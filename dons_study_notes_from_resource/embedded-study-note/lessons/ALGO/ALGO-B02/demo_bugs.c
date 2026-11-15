/* ALGO-B02 demo: 고전 이진 탐색 버그들을 전수 테스트에 넣어 본다
 *
 * 배열 읽기는 rd() 를 거친다: 범위 밖이면 실제로 읽지 않고 OOB 로 기록, 반복이 200 회를 넘으면 HANG 으로 기록.
 * 그래서 버그 변형도 UB 없이 끝까지 돌고 "틀린 답 / 범위 밖 읽기 / 무한 루프" 를 센다.
 *
 *   ./demo_bugs          : 변형 V0~V4 전수 테스트 + 32비트 인덱스 (lo+hi)/2 오버플로 (unsigned, 정의된 wrap)
 *   ./demo_bugs signed   : int 인덱스 (lo+hi)/2 — 부호 있는 오버플로는 UB 이므로 -fsanitize=undefined 빌드로만 돌릴 것
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bugs.c -o demo_bugs && ./demo_bugs
 *        cc -std=c11 -O1 -fsanitize=undefined demo_bugs.c -o demo_ub && ./demo_ub signed
 */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXN 50
#define STEP_CAP 200
#define FAULT ((size_t)-1)

static const int32_t *g_a;
static size_t g_n;
static int g_oob, g_hang;
static unsigned g_steps;

static int32_t rd(size_t i) {                 /* 범위 검사하는 읽기 */
    if (i >= g_n) { g_oob = 1; return 0; }
    return g_a[i];
}
#define STEP() do { if (++g_steps > STEP_CAP) { g_hang = 1; return FAULT; } } while (0)
#define READ(v, i) do { v = rd(i); if (g_oob) return FAULT; } while (0)

/* V0: 정답 — 반개구간 [lo, hi) */
static size_t v0_ok(size_t n, int32_t x) {
    size_t lo = 0, hi = n; int32_t v;
    while (lo < hi) { STEP(); size_t mid = lo + (hi - lo) / 2; READ(v, mid); if (v < x) lo = mid + 1; else hi = mid; }
    return lo;
}
/* V1: 닫힌구간 [lo, hi] 인데 size_t 로 hi = n - 1, hi = mid - 1 — n == 0 / mid == 0 에서 SIZE_MAX 로 underflow */
static size_t v1_size_t_underflow(size_t n, int32_t x) {
    size_t lo = 0, hi = n - 1, ans = n; int32_t v;
    while (lo <= hi) {
        STEP(); size_t mid = lo + (hi - lo) / 2; READ(v, mid);
        if (v >= x) { ans = mid; hi = mid - 1; } else lo = mid + 1;
    }
    return ans;
}
/* V2: lo = mid (mid + 1 이 아니라) — 구간이 2칸일 때 줄지 않는다 */
static size_t v2_lo_eq_mid(size_t n, int32_t x) {
    size_t lo = 0, hi = n; int32_t v;
    while (lo < hi) { STEP(); size_t mid = lo + (hi - lo) / 2; READ(v, mid); if (v < x) lo = mid; else hi = mid; }
    return lo;
}
/* V3: 반개구간 초기값(hi = n) + 닫힌구간 조건(lo <= hi) 섞기 */
static size_t v3_mixed(size_t n, int32_t x) {
    size_t lo = 0, hi = n; int32_t v;
    while (lo <= hi) { STEP(); size_t mid = lo + (hi - lo) / 2; READ(v, mid); if (v < x) lo = mid + 1; else hi = mid; }
    return lo;
}
/* V4: "아무 일치나" 돌려주는 바닐라 탐색을 '첫 등장' 자리에 씀. 없으면 n 대신 lower_bound 반환 */
static size_t v4_any_match(size_t n, int32_t x) {
    size_t lo = 0, hi = n; int32_t v;
    while (lo < hi) {
        STEP(); size_t mid = lo + (hi - lo) / 2; READ(v, mid);
        if (v == x) return mid;
        if (v < x) lo = mid + 1; else hi = mid;
    }
    return lo;
}

typedef size_t (*lb_fn)(size_t, int32_t);
typedef struct { unsigned long q, wrong, oob, hang; } stat_t;

static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

static stat_t run_suite(lb_fn f, int with_dups) {
    stat_t s = { 0, 0, 0, 0 };
    static int32_t a[MAXN];
    g_rng = 0x2545F491u;
    for (size_t n = 0; n <= MAXN; n++)
        for (int pat = 0; pat < (with_dups ? 3 : 1); pat++) {
            int32_t v = 0;
            for (size_t i = 0; i < n; i++) {
                if (pat == 0) a[i] = 3 * (int32_t)i;                          /* 값이 모두 다름 */
                else if (pat == 1) { v += (int32_t)(xorshift32() % 3); a[i] = v; }
                else a[i] = 7;                                                /* 전부 같음 */
            }
            int64_t mn = n ? a[0] : 0, mx = n ? a[n - 1] : 0;
            for (int64_t t = mn - 1; t <= mx + 1; t++) {
                int32_t x = (int32_t)t;
                size_t want = 0; while (want < n && a[want] < x) want++;
                g_a = a; g_n = n; g_oob = g_hang = 0; g_steps = 0;
                size_t got = f(n, x);
                s.q++;
                if (g_oob) s.oob++;
                else if (g_hang) s.hang++;
                else if (got != want) s.wrong++;
            }
        }
    return s;
}

/* ---- 32비트 인덱스에서 (lo + hi) / 2: 가상 배열 a(i) = i, n = 4,000,000,000 ---- */
static uint32_t lb_u32(uint32_t n, uint32_t x, int buggy, unsigned *steps, int trace) {
    uint32_t lo = 0, hi = n;
    *steps = 0;
    while (lo < hi) {
        if (++*steps > STEP_CAP) return UINT32_MAX;
        uint32_t mid = buggy ? (lo + hi) / 2 : lo + (hi - lo) / 2;   /* unsigned wrap 은 정의된 동작 (mod 2^32) */
        if (trace && *steps <= 4) printf("    step %u: lo=%10u hi=%10u mid=%10u%s\n", *steps, lo, hi, mid, mid < lo ? "  <-- mid < lo !" : "");
        if (mid < x) lo = mid + 1; else hi = mid;                       /* a(mid) = mid */
    }
    return lo;
}

/* ---- int 인덱스: 부호 있는 오버플로 = UB. UBSan 빌드로만 실행 ---- */
static int lb_int_buggy(int n, int x, unsigned *steps) {
    int lo = 0, hi = n;
    *steps = 0;
    while (lo < hi) {
        if (++*steps > STEP_CAP) return -1;
        int mid = (lo + hi) / 2;
        if (*steps <= 3) printf("    step %u: lo=%11d hi=%11d mid=%11d\n", *steps, lo, hi, mid);
        if (mid < x) lo = mid + 1; else hi = mid;
    }
    return lo;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "signed") == 0) {
        unsigned st;
        int r = lb_int_buggy(INT_MAX, 2000000000, &st);
        printf("lb_int_buggy(INT_MAX, 2000000000) = %d, steps=%u%s\n", r, st, r < 0 ? " (step cap hit)" : "");
        return 0;
    }
    static const struct { const char *name; lb_fn f; } V[] = {
        { "V0 half-open (correct)    ", v0_ok },
        { "V1 size_t hi=n-1 inclusive", v1_size_t_underflow },
        { "V2 lo = mid               ", v2_lo_eq_mid },
        { "V3 hi=n + while(lo<=hi)   ", v3_mixed },
        { "V4 any-match as first     ", v4_any_match },
    };
    printf("variant                       | distinct-only: q / wrong / OOB / hang | with dups: q / wrong / OOB / hang\n");
    for (size_t k = 0; k < sizeof V / sizeof V[0]; k++) {
        stat_t d = run_suite(V[k].f, 0), u = run_suite(V[k].f, 1);
        printf("%s    | %5lu / %5lu / %4lu / %4lu          | %5lu / %5lu / %4lu / %4lu\n",
               V[k].name, d.q, d.wrong, d.oob, d.hang, u.q, u.wrong, u.oob, u.hang);
    }

    unsigned st;
    uint32_t N = 4000000000u, X = 3000000000u;
    printf("\nuint32 indices, virtual a(i)=i, n=%u, x=%u (answer must be x)\n", N, X);
    uint32_t r0 = lb_u32(N, X, 0, &st, 0);
    printf("  lo + (hi-lo)/2 : result=%u steps=%u\n", r0, st);
    printf("  (lo + hi)/2    :\n");
    uint32_t r1 = lb_u32(N, X, 1, &st, 1);
    printf("  (lo + hi)/2    : result=%u steps=%u%s\n", r1, st, r1 == UINT32_MAX ? " (HANG: step cap hit)" : "");
    return 0;
}
