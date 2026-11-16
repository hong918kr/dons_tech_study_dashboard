/* MEM-B11 메모리 대역폭 측정: STREAM Copy/Scale/Add/Triad — 결정적(deterministic) 부품 검사
 *
 * Part A: STREAM 4 커널 (Copy c=a, Scale b=q*c, Add c=a+b, Triad a=b+q*c) 을 [lo,hi) 구간으로 구현,
 *         STREAM 방식 검증기 (스칼라로 같은 순서를 재생 → 배열별 평균 상대 오차 ≤ eps),
 *         바이트 계산 규약 (Copy/Scale 16 B, Add/Triad 24 B per iter — write-allocate 미포함) 과
 *         write-allocate 포함 "실제 트래픽" (24 B / 32 B → 3/2, 4/3 배)
 * Part B: 배열 크기 규칙 (배열 하나 ≥ 4 × LLC 바이트, 그리고 ≥ 1,000,000 원소),
 *         스레드 작업 분할 chunk_range: [0,n) 을 t 개 연속 구간으로 정확히 한 번씩 (내부 경계는 align 배수)
 * Part C: 측정 결과 요약 (STREAM 처럼 첫 반복 제외, 최소 시간 → best GB/s), 피크 대역폭 (MT/s × 버스 바이트,
 *         정수 MB/s), 효율 %, 포화 스레드 수 찾기
 *
 * 실제 측정 (pthreads 1..12, 크기 스윕, write-allocate / non-temporal 실험) 은 demo_stream.c.
 * 여기는 시간과 무관한 검사만 — 항상 같은 결과, < 1 s.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

enum { K_COPY, K_SCALE, K_ADD, K_TRIAD, K_N };
static const char *KNAME[K_N] = { "Copy", "Scale", "Add", "Triad" };

#define STREAM_EPS      1e-13          /* stream.c 의 double 허용 오차 */
#define MIN_ELEMS       1000000u       /* STREAM 규칙의 하한: 1 M 원소 */
#define LINE_BYTES      128u           /* hw.cachelinesize (M6) */
#define LINE_DBL        (LINE_BYTES / sizeof(double))   /* 16 */

/* 이 레슨의 LLC 가정 (M6, 이 맥): sysctl L2 = 20 MiB(perflevel0) + 20 MiB(perflevel1) + 8 MiB(perflevel2)
 * 를 따로 센 값(공유 여부 미확인 → 보수적으로 합) + SLC(시스템 레벨 캐시, sysctl 에 없음) 32 MiB 가정 = 80 MiB. */
#define LLC_ASSUMED_M6  (80ull << 20)

/* ======================= Part A: 커널 · 검증 · 바이트 ======================= */

/* 커널 k 를 [lo,hi) 에 한 번. 배열 3개는 서로 겹치지 않는다 (restrict). */
static void stream_kernel(int k, double *restrict a, double *restrict b, double *restrict c,
                          double q, size_t lo, size_t hi) {
    (void)k; (void)a; (void)b; (void)c; (void)q; (void)lo; (void)hi;
    /* TODO: switch (k) — Copy c=a, Scale b=q*c, Add c=a+b, Triad a=b+q*c, i in [lo,hi) */
}

/* STREAM 바이트 규약: 반복 1 번에 "프로그램이 읽고 쓰는" double 수 × 8. write-allocate 의 RFO 는 세지 않는다. */
static uint64_t stream_bytes(int k, uint64_t n) {
    (void)k; (void)n;
    return 0; /* TODO: Copy/Scale 16 B, Add/Triad 24 B per iteration (RFO 미포함), 그 외 0 */
}

/* write-allocate 캐시면 쓰기 대상 라인을 먼저 읽어 온다 (RFO) → 쓰기 1 개당 8 B 가 더 버스를 지난다. */
static uint64_t actual_bytes(int k, uint64_t n, bool write_allocate) {
    (void)k; (void)n; (void)write_allocate;
    return 0; /* TODO: stream_bytes + (write_allocate ? 쓰기 1 개 × 8 B × n : 0), 잘못된 k 는 0 */
}

/* STREAM checkSTREAMresults 와 같은 재생: 초기 a=1,b=2,c=0, 타이밍 보정용 a*=2, 그다음 ntimes 번
 * Copy → Scale → Add → Triad. 배열 원소는 모두 같은 값이므로 스칼라 하나로 충분하다. */
static void stream_expected(int ntimes, double q, double *aj, double *bj, double *cj) {
    (void)ntimes; (void)q;
    *aj = 0; *bj = 0; *cj = 0; /* TODO: a=1,b=2,c=0 → a*=2 → ntimes × (c=a; b=q*c; c=a+b; a=b+q*c) */
}

/* 배열별 평균 절대 오차 / 기대값 > eps 이면 그 배열을 실패로. 반환 비트마스크: bit0 a, bit1 b, bit2 c (0 = 통과). */
static unsigned stream_check(const double *a, const double *b, const double *c, size_t n,
                             int ntimes, double q, double eps) {
    (void)a; (void)b; (void)c; (void)n; (void)ntimes; (void)q; (void)eps;
    return 7u; /* TODO: 배열별 평균 |x-기대값| / 기대값 > eps → bit (a=1,b=2,c=4). n == 0 이면 7 */
}

/* GB/s (10^9 B/s, STREAM 과 같은 십진 단위) */
static double gbs(uint64_t bytes, double seconds) { return seconds > 0 ? (double)bytes / seconds * 1e-9 : 0.0; }

/* ======================= Part B: 배열 크기 · 작업 분할 ======================= */

/* 배열 하나가 ≥ 4 × LLC 바이트 이고 ≥ MIN_ELEMS 원소가 되는 최소 원소 수. elem == 0 이면 0. */
static uint64_t stream_min_elems(uint64_t llc_bytes, size_t elem) {
    (void)llc_bytes; (void)elem;
    return 0; /* TODO: max(ceil(4·LLC / elem), MIN_ELEMS), elem == 0 → 0 */
}

/* 스레드 k (0..t-1) 의 구간 [lo,hi). 경계 b_j = floor(n·j/t) 를 align 의 배수로 내림 (b_0 = 0, b_t = n).
 * → 구간들이 [0,n) 을 빈틈·겹침 없이 덮고, 내부 경계가 캐시라인(align 원소)에 맞아 경계 라인을 두 스레드가
 *   같이 쓰지 않는다 (false sharing, ARCH-M08). t == 0 또는 k >= t 이면 빈 구간. */
static size_t boundary(size_t n, unsigned t, unsigned j, size_t align) {
    (void)n; (void)t; (void)j; (void)align;
    return 0; /* TODO: j==0 → 0, j>=t → n, 그 외 floor(n·j/t) 를 align 배수로 내림 (64비트 곱) */
}
static void chunk_range(size_t n, unsigned t, unsigned k, size_t align, size_t *lo, size_t *hi) {
    (void)n; (void)t; (void)k; (void)align; (void)boundary;
    *lo = *hi = 0; /* TODO: t==0 || k>=t → 빈 구간, 아니면 [boundary(k), boundary(k+1)) */
}

/* ======================= Part C: 결과 요약 · 피크 · 효율 ======================= */

typedef struct { double best_gbs, avg_s, min_s, max_s; } stream_stat;

/* STREAM 보고 방식: 첫 반복(k=0, 워밍업)은 버리고, 나머지에서 min/avg/max 시간, best rate = bytes / min.
 * ntimes < 2 이거나 시간이 0 이하인 값이 있으면 -1. */
static int stream_summarize(const double *secs, int ntimes, uint64_t bytes, stream_stat *out) {
    (void)secs; (void)ntimes; (void)bytes; (void)out;
    return -1; /* TODO: k=1..ntimes-1 만, 시간 <= 0 이면 -1, min/avg/max, best_gbs = gbs(bytes, min) */
}

/* 이론 피크 (MB/s, 10^6) = MT/s × 버스 폭(바이트). bus_bits 가 0 이거나 8 의 배수가 아니면 0. */
static uint64_t peak_mbs(uint32_t mts, uint32_t bus_bits) {
    (void)mts; (void)bus_bits;
    return 0; /* TODO: mts × bus_bits/8 (정수), 0 이거나 8 의 배수가 아니면 0 */
}

static double efficiency_pct(double gbs_meas, uint64_t peak) {
    return peak ? 100.0 * gbs_meas * 1000.0 / (double)peak : 0.0;
}

/* gbs[0..nt) = 스레드 1..nt 의 측정값. max 의 frac 이상에 처음 도달하는 스레드 수 (1-based). nt == 0 → 0. */
static unsigned saturation_threads(const double *g, unsigned nt, double frac) {
    (void)g; (void)frac;
    return nt; /* TODO: max 를 찾고, g[i] >= frac·max 인 첫 i+1. nt == 0 → 0 */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define NT 1000
static double A[NT], B[NT], C[NT];

static void init_arrays(size_t n) {
    for (size_t i = 0; i < n; i++) { A[i] = 1.0; B[i] = 2.0; C[i] = 0.0; }
    for (size_t i = 0; i < n; i++) A[i] = 2.0 * A[i];
}

/* t 스레드로 "나눠서" 돌린 것처럼: 구간마다 커널을 부른다 (실제 병렬은 demo). */
static void run_partitioned(size_t n, unsigned t, size_t align, int ntimes, double q) {
    for (int r = 0; r < ntimes; r++)
        for (int k = 0; k < K_N; k++)
            for (unsigned w = 0; w < t; w++) {
                size_t lo, hi;
                chunk_range(n, t, w, align, &lo, &hi);
                if (hi > n) hi = n;
                stream_kernel(k, A, B, C, q, lo, hi);
            }
}

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 분할 검사: 연속 · 정확히 한 번 · 내부 경계 정렬 · 균형 */
static bool partition_ok(size_t n, unsigned t, size_t align) {
    size_t prev_hi = 0, lo, hi;
    size_t fl = n / t, ce = (n + t - 1) / t;
    for (unsigned k = 0; k < t; k++) {
        chunk_range(n, t, k, align, &lo, &hi);
        if (lo != prev_hi || hi < lo || hi > n) return false;
        if (k > 0 && align > 1 && lo % align != 0) return false;
        size_t sz = hi - lo, slack = align > 1 ? align - 1 : 0;
        if (sz > ce + slack || sz + slack < fl) return false;
        prev_hi = hi;
    }
    return prev_hi == n;
}

int main(void) {
    const double q = 3.0;
    const int NTIMES = 10;

    /* ---- Part A: 커널 정확성 ---- */
    {
        double aj, bj, cj;
        stream_expected(1, q, &aj, &bj, &cj);
        CHECK(aj == 30.0 && bj == 6.0 && cj == 8.0);        /* c=2, b=6, c=8, a=6+24=30 */
        stream_expected(NTIMES, q, &aj, &bj, &cj);
        CHECK(aj == 2.0 * pow(15.0, NTIMES));                /* 반복마다 ×15, 2·15^10 < 2^53 → 정확 */
        printf("expected after %d iters: a=%.0f b=%.0f c=%.0f\n", NTIMES, aj, bj, cj);

        init_arrays(NT);
        run_partitioned(NT, 1, 1, NTIMES, q);
        CHECK(A[0] == aj && B[NT - 1] == bj && C[NT / 2] == cj);   /* 정수값이라 ==  */
        CHECK(stream_check(A, B, C, NT, NTIMES, q, STREAM_EPS) == 0);

        /* 같은 결과가 t = 1..8, align = 1 / 16 어느 분할로 돌려도 나와야 한다 */
        int bad = 0;
        for (unsigned t = 1; t <= 8; t++)
            for (size_t al = 1; al <= LINE_DBL; al += LINE_DBL - 1) {
                init_arrays(NT);
                run_partitioned(NT, t, al, NTIMES, q);
                if (stream_check(A, B, C, NT, NTIMES, q, STREAM_EPS) != 0) bad++;
            }
        CHECK(bad == 0);

        /* 결함 주입 1: 한 원소가 틀림 (예: 경계 off-by-one 으로 한 스레드가 마지막 원소를 안 씀) */
        init_arrays(NT);
        run_partitioned(NT, 1, 1, NTIMES, q);
        A[NT - 1] = 0.0;
        CHECK(stream_check(A, B, C, NT, NTIMES, q, STREAM_EPS) == 1u);
        /* 결함 주입 2: 마지막 반복의 Add 가 "지워짐" (컴파일러가 결과 미사용 루프를 지운 상황) */
        init_arrays(NT);
        run_partitioned(NT, 1, 1, NTIMES - 1, q);
        stream_kernel(K_COPY, A, B, C, q, 0, NT);
        stream_kernel(K_SCALE, A, B, C, q, 0, NT);
        stream_kernel(K_TRIAD, A, B, C, q, 0, NT);
        CHECK(stream_check(A, B, C, NT, NTIMES, q, STREAM_EPS) == (1u | 4u));   /* c 틀림 → a 도 틀림 */
        /* 결함 주입 3: 1 ulp 수준 오차는 통과 (eps 의 의미) */
        init_arrays(NT);
        run_partitioned(NT, 1, 1, NTIMES, q);
        B[3] = nextafter(B[3], INFINITY);
        CHECK(stream_check(A, B, C, NT, NTIMES, q, STREAM_EPS) == 0);
        CHECK(stream_check(A, B, C, 0, NTIMES, q, STREAM_EPS) == 7u);
    }

    /* ---- Part A: 바이트 규약 ---- */
    {
        const uint64_t n = 10000000u;
        CHECK(stream_bytes(K_COPY, n) == 160000000u && stream_bytes(K_SCALE, n) == 160000000u);
        CHECK(stream_bytes(K_ADD, n) == 240000000u && stream_bytes(K_TRIAD, n) == 240000000u);
        CHECK(stream_bytes(K_N, n) == 0 && actual_bytes(-1, n, true) == 0);
        int ratio_ok = 1;
        for (int k = 0; k < K_N; k++) {
            uint64_t s = stream_bytes(k, n), w = actual_bytes(k, n, true);
            if (actual_bytes(k, n, false) != s) ratio_ok = 0;
            if (k <= K_SCALE ? (2 * w != 3 * s) : (3 * w != 4 * s)) ratio_ok = 0;   /* 3/2, 4/3 */
        }
        CHECK(ratio_ok);
        /* 2^26 원소 Triad 를 16 ms 에 → 24·2^26 / 0.016 s = 100.66 GB/s */
        double g = gbs(stream_bytes(K_TRIAD, 1u << 26), 0.016);
        CHECK(fabs(g - 100.663296) < 1e-6);
        CHECK(gbs(1000, 0.0) == 0.0);
        for (int k = 0; k < K_N; k++)
            printf("  %-5s n=1e7: STREAM %3llu MB, with write-allocate %3llu MB\n", KNAME[k],
                   (unsigned long long)(stream_bytes(k, n) / 1000000u),
                   (unsigned long long)(actual_bytes(k, n, true) / 1000000u));
    }

    /* ---- Part B: 배열 크기 규칙 ---- */
    {
        CHECK(stream_min_elems(8ull << 20, 8) == 4194304u);        /* stream.c 예: 8 MB L3 → ≥ 4 M */
        CHECK(stream_min_elems(1ull << 20, 8) == MIN_ELEMS);        /* 작은 캐시 → 1 M 하한 */
        CHECK(stream_min_elems(0, 8) == MIN_ELEMS && stream_min_elems(1, 0) == 0);
        CHECK(stream_min_elems(LLC_ASSUMED_M6, 8) == 41943040u);    /* 80 MiB 가정 → 40 Mi 원소 = 320 MiB/배열 */
        CHECK(stream_min_elems(3, 8) == MIN_ELEMS && stream_min_elems(2000001, 8) == 1000001u);   /* ceil */
        CHECK((1u << 26) >= stream_min_elems(LLC_ASSUMED_M6, 8));   /* demo 기본 N = 2^26 은 규칙을 만족 */
        printf("min elems for LLC %llu MiB: %llu (%.0f MiB per array)\n",
               (unsigned long long)(LLC_ASSUMED_M6 >> 20),
               (unsigned long long)stream_min_elems(LLC_ASSUMED_M6, 8),
               stream_min_elems(LLC_ASSUMED_M6, 8) * 8.0 / (1 << 20));
    }

    /* ---- Part B: 작업 분할 ---- */
    {
        static const size_t ns[] = { 0, 1, 2, 7, 15, 16, 17, 31, 1000, 1023, 1024, 1025, 999983,
                                     (1u << 20) + 3, 41943040u, (1u << 26) };
        int fails = 0, cases = 0;
        for (size_t i = 0; i < sizeof ns / sizeof ns[0]; i++)
            for (unsigned t = 1; t <= 12; t++)
                for (size_t al = 1; al <= LINE_DBL; al += LINE_DBL - 1) {
                    cases++;
                    if (!partition_ok(ns[i], t, al)) fails++;
                }
        uint32_t s = 0x2545F491u;
        for (int r = 0; r < 2000; r++) {
            size_t n = xs32(&s) % 5000000u;
            unsigned t = 1 + xs32(&s) % 8;
            size_t al = (xs32(&s) & 1) ? LINE_DBL : 1;
            cases++;
            if (!partition_ok(n, t, al)) fails++;
        }
        printf("partition cases: %d, failures: %d\n", cases, fails);
        CHECK(cases == 2384 && fails == 0);
        /* 정확히 한 번 덮는지 원소 단위로 세어 보기 (n=1025, t=7, align=16) */
        static unsigned char cover[1025];
        for (unsigned k = 0; k < 7; k++) {
            size_t lo, hi;
            chunk_range(1025, 7, k, LINE_DBL, &lo, &hi);
            for (size_t i = lo; i < hi && i < 1025; i++) cover[i]++;
        }
        int once = 1;
        for (size_t i = 0; i < 1025; i++) if (cover[i] != 1) once = 0;
        CHECK(once);
        size_t lo, hi;
        chunk_range(1000, 3, 1, 1, &lo, &hi);  CHECK(lo == 333 && hi == 666);
        chunk_range(1000, 3, 1, 16, &lo, &hi); CHECK(lo == 320 && hi == 656);
        chunk_range(1000, 3, 2, 16, &lo, &hi); CHECK(lo == 656 && hi == 1000);
        chunk_range(1, 8, 0, 1, &lo, &hi);     CHECK(lo == 0 && hi == 0);    /* t > n: 앞쪽은 빈 구간 */
        chunk_range(1, 8, 7, 1, &lo, &hi);     CHECK(lo == 0 && hi == 1);
        chunk_range(100, 0, 0, 1, &lo, &hi);   CHECK(lo == 0 && hi == 0);
        chunk_range(100, 4, 4, 1, &lo, &hi);   CHECK(lo == 0 && hi == 0);
    }

    /* ---- Part C: 요약 · 피크 · 효율 · 포화 ---- */
    {
        const double secs[] = { 0.050, 0.020, 0.016, 0.018 };
        stream_stat st = { 0, 0, 0, 0 };
        CHECK(stream_summarize(secs, 4, stream_bytes(K_TRIAD, 1u << 26), &st) == 0);
        CHECK(st.min_s == 0.016 && st.max_s == 0.020 && fabs(st.avg_s - 0.018) < 1e-12);  /* 0.050 은 제외 */
        CHECK(fabs(st.best_gbs - 100.663296) < 1e-6);
        const double bad1[] = { 0.05, 0.0, 0.02 };
        CHECK(stream_summarize(bad1, 3, 100, &st) == -1 && stream_summarize(secs, 1, 100, &st) == -1);

        CHECK(peak_mbs(3200, 64) == 25600u);       /* DDR4-3200 64-bit DIMM (MEM-B04 와 같은 값) */
        CHECK(peak_mbs(6400, 128) == 102400u);     /* M2: LPDDR5-6400 × 128-bit → Apple 표기 "100GB/s" */
        CHECK(peak_mbs(9600, 128) == 153600u);     /* M5: Apple 153GB/s ↔ 9600 MT/s × 128-bit (추정) */
        CHECK(peak_mbs(10667, 128) == 170672u);    /* M6: Apple "up to 170GB/s" ↔ ≈10667 MT/s × 128-bit (추정) */
        CHECK(peak_mbs(0, 64) == 0 && peak_mbs(3200, 0) == 0 && peak_mbs(3200, 12) == 0);
        CHECK(fabs(efficiency_pct(120.0, 170000u) - 70.588235) < 1e-5);
        CHECK(efficiency_pct(50.0, 0) == 0.0);

        const double curve[] = { 40, 75, 100, 115, 118, 119, 117, 118 };
        CHECK(saturation_threads(curve, 8, 0.90) == 4);   /* 0.9 × 119 = 107.1 → 115 (t=4) */
        CHECK(saturation_threads(curve, 8, 0.99) == 5);   /* 117.81 → 118 (t=5) */
        CHECK(saturation_threads(curve, 0, 0.9) == 0 && saturation_threads(curve, 1, 0.9) == 1);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
