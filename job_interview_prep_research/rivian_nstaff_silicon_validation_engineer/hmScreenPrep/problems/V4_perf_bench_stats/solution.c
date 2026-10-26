/*
 * V4  Benchmark Harness: latency / throughput statistics from a 32-bit cycle counter
 * Reference solution.
 *
 * You are writing the micro-benchmark library used to characterize blocks on a new SoC
 * (memcpy, DMA, NPU kernels). The only timer is a free-running 32-bit cycle counter
 * read_cycles() that wraps at 2^32. The HAL is a FAKE at the bottom of this file.
 *
 *   uint32_t read_cycles(void)   free-running, wraps; reading it costs a few cycles itself
 *   typedef void (*bench_op_fn)(void *ctx)   the operation under test
 *
 * Implement:
 *   bench_calibrate(k)   overhead of an EMPTY measurement (t0 = read; t1 = read; t1 - t0),
 *                        taken as the MINIMUM over k tries. k == 0 -> 0.
 *   bench_run(op, ctx, iters, warmup, samples, overhead_out)
 *                        1) overhead = bench_calibrate(BENCH_CAL_K)
 *                        2) call op() `warmup` times, not measured
 *                        3) for i in [0, iters): t0 = read; op(ctx); t1 = read;
 *                           samples[i] = (t1 - t0) - overhead, wrap-safe, clamped at 0
 *                        returns 0, or -EINVAL if op == NULL or (samples == NULL && iters > 0).
 *   bench_stats(samples, n, outlier_x, st)
 *                        min, max, mean (64-bit accumulator, floor), p50, p99 by NEAREST RANK
 *                        (rank = ceil(p * n / 100), value = sorted[rank - 1]),
 *                        outliers = count of samples > p50 * outlier_x.
 *                        May sort `samples` in place (caller's buffer). No malloc.
 *                        n == 0 or NULL pointers -> -EINVAL and *st untouched.
 *   bench_mbps(bytes_per_op, cycles_per_op, cpu_mhz)
 *                        throughput in MB/s (1 MB = 10^6 bytes) = bytes * cpu_mhz / cycles,
 *                        rounded DOWN, no 64-bit overflow for any input; saturates at
 *                        UINT64_MAX; cycles_per_op == 0 -> 0.
 *
 * Build & run:  ./run.sh V4 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* ---------------- HAL (provided) ---------------- */
uint32_t read_cycles(void);
typedef void (*bench_op_fn)(void *ctx);

#define CPU_HZ       1500000000u            /* 1.5 GHz core clock */
#define CPU_MHZ      (CPU_HZ / 1000000u)
#define BENCH_CAL_K  8                      /* empty measurements for calibration */
#define BENCH_MAX_N  4096                   /* max samples per run */

typedef struct {
    uint32_t n;
    uint32_t min, max, mean;    /* cycles, overhead already removed; mean rounded down */
    uint32_t p50, p99;          /* nearest-rank percentiles */
    uint32_t outliers;          /* samples > p50 * outlier_x */
} BenchStats;

/* ---------------- benchmark library: your code ---------------- */

uint32_t bench_calibrate(uint32_t k)
{
    uint32_t best = UINT32_MAX;
    if (k == 0) return 0;
    for (uint32_t i = 0; i < k; i++) {
        uint32_t t0 = read_cycles();
        uint32_t t1 = read_cycles();
        uint32_t d = t1 - t0;                 /* unsigned subtraction: wrap-safe */
        if (d < best) best = d;               /* min = least-disturbed measurement */
    }
    return best;
}

int bench_run(bench_op_fn op, void *ctx, uint32_t iters, uint32_t warmup,
              uint32_t *samples, uint32_t *overhead_out)
{
    if (op == NULL || (samples == NULL && iters > 0)) return -EINVAL;

    uint32_t ovh = bench_calibrate(BENCH_CAL_K);
    if (overhead_out) *overhead_out = ovh;

    for (uint32_t i = 0; i < warmup; i++) op(ctx);       /* caches, TLB, branch predictor */

    for (uint32_t i = 0; i < iters; i++) {
        uint32_t t0 = read_cycles();
        op(ctx);
        uint32_t t1 = read_cycles();
        uint32_t raw = t1 - t0;                           /* correct across one wrap */
        samples[i] = (raw > ovh) ? raw - ovh : 0;
    }
    return 0;
}

static void sort_u32(uint32_t *a, uint32_t n)             /* insertion sort, n <= BENCH_MAX_N */
{
    for (uint32_t i = 1; i < n; i++) {
        uint32_t v = a[i], j = i;
        while (j > 0 && a[j - 1] > v) { a[j] = a[j - 1]; j--; }
        a[j] = v;
    }
}

static uint32_t pct_nearest_rank(const uint32_t *sorted, uint32_t n, uint32_t p)
{
    uint64_t rank = ((uint64_t)p * n + 99u) / 100u;       /* ceil(p*n/100) */
    if (rank == 0) rank = 1;
    return sorted[rank - 1];
}

int bench_stats(uint32_t *samples, uint32_t n, uint32_t outlier_x, BenchStats *st)
{
    if (samples == NULL || st == NULL || n == 0) return -EINVAL;

    sort_u32(samples, n);

    uint64_t sum = 0;                                     /* 32-bit sum overflows fast */
    for (uint32_t i = 0; i < n; i++) sum += samples[i];

    BenchStats s;
    s.n    = n;
    s.min  = samples[0];
    s.max  = samples[n - 1];
    s.mean = (uint32_t)(sum / n);
    s.p50  = pct_nearest_rank(samples, n, 50);
    s.p99  = pct_nearest_rank(samples, n, 99);

    uint64_t lim = (uint64_t)s.p50 * outlier_x;           /* 64-bit: no overflow */
    s.outliers = 0;
    for (uint32_t i = 0; i < n; i++)
        if (samples[i] > lim) s.outliers++;

    *st = s;
    return 0;
}

uint64_t bench_mbps(uint64_t bytes_per_op, uint32_t cycles_per_op, uint32_t cpu_mhz)
{
    if (cycles_per_op == 0) return 0;
    /* bytes * mhz / cycles, split into quotient and remainder so nothing overflows:
     *   bytes = q*cycles + r  ->  result = q*mhz + (r*mhz)/cycles   (exact floor) */
    uint64_t q = bytes_per_op / cycles_per_op;
    uint64_t r = bytes_per_op % cycles_per_op;            /* r < 2^32 */
    if (cpu_mhz != 0 && q > UINT64_MAX / cpu_mhz) return UINT64_MAX;
    uint64_t hi = q * cpu_mhz;
    uint64_t lo = (r * cpu_mhz) / cycles_per_op;          /* r*mhz < 2^64 */
    if (hi > UINT64_MAX - lo) return UINT64_MAX;
    return hi + lo;
}

/* =====================================================================
 * Test harness: fake cycle counter + scripted operation (do not edit)
 * ===================================================================== */
static struct {
    uint32_t cyc;            /* counter value */
    uint32_t read_cost;      /* cycles a read_cycles() call costs (lands after the value is latched) */
    uint32_t extra[64];      /* extra cost injected on read #i (interrupt, cache miss) */
    uint32_t reads;
} F;

uint32_t read_cycles(void)
{
    uint32_t v = F.cyc;
    F.cyc += F.read_cost + (F.reads < 64 ? F.extra[F.reads] : 0);
    F.reads++;
    return v;
}

typedef struct { const uint32_t *dur; uint32_t n, calls; } Script;

static void fake_op(void *ctx)            /* advances the counter by the next scripted duration */
{
    Script *s = ctx;
    F.cyc += s->dur[s->calls % s->n];
    s->calls++;
}

static void fake_reset(uint32_t start)
{
    memset(&F, 0, sizeof F);
    F.cyc = start;
    F.read_cost = 20;
}

int main(void)
{
    static uint32_t smp[BENCH_MAX_N];
    BenchStats st;
    uint32_t ovh = 0;

    /* 1. constant 1000-cycle op: overhead 20 removed, warm-up calls not recorded */
    fake_reset(1000);
    const uint32_t d1[] = { 1000 };
    Script s1 = { d1, 1, 0 };
    CHECK(bench_run(fake_op, &s1, 16, 4, smp, &ovh) == 0);
    CHECK(ovh == 20);
    CHECK(s1.calls == 20);
    CHECK(smp[0] == 1000 && smp[15] == 1000);
    CHECK(bench_stats(smp, 16, 2, &st) == 0);
    CHECK(st.min == 1000 && st.max == 1000 && st.mean == 1000);
    CHECK(st.p50 == 1000 && st.p99 == 1000 && st.outliers == 0);

    /* 2. counter wraps inside iteration 2: t0 = 0xFFFFFF9C, t1 = 0x000000DC */
    fake_reset(0u - 1100u);
    const uint32_t d2[] = { 300 };
    Script s2 = { d2, 1, 0 };
    CHECK(bench_run(fake_op, &s2, 8, 0, smp, &ovh) == 0);
    CHECK(ovh == 20);
    CHECK(smp[1] == 300 && smp[2] == 300 && smp[3] == 300);
    CHECK(bench_stats(smp, 8, 2, &st) == 0);
    CHECK(st.max == 300 && st.min == 300);

    /* 3. calibration is the MIN of K empty reads, not the first or the mean */
    fake_reset(0);
    for (int i = 0; i < 14; i += 2) F.extra[i] = 15;     /* empty pairs 0..6 disturbed: 35 cycles */
    const uint32_t d3[] = { 500, 520, 480 };
    Script s3 = { d3, 3, 0 };
    CHECK(bench_run(fake_op, &s3, 6, 0, smp, &ovh) == 0);
    CHECK(ovh == 20);
    CHECK(smp[0] == 500 && smp[1] == 520 && smp[2] == 480 && smp[5] == 480);
    fake_reset(0);
    F.extra[0] = 15;
    CHECK(bench_calibrate(1) == 35);
    CHECK(bench_calibrate(0) == 0);

    /* 4. known distribution: 95 x 1000, 3 x 1500, 2 x 5000 (shuffled) -> nearest rank */
    uint32_t n = 0;
    for (int i = 0; i < 100; i++)
        smp[n++] = (i == 17 || i == 63) ? 5000 : (i == 5 || i == 40 || i == 88) ? 1500 : 1000;
    CHECK(bench_stats(smp, n, 2, &st) == 0);
    CHECK(st.min == 1000 && st.max == 5000);
    CHECK(st.mean == 1095);
    CHECK(st.p50 == 1000 && st.p99 == 5000);
    CHECK(st.outliers == 2);
    uint32_t four[] = { 40, 10, 30, 20 };                 /* nearest rank, not interpolation */
    CHECK(bench_stats(four, 4, 1, &st) == 0);
    CHECK(st.p50 == 20 && st.p99 == 40 && st.mean == 25 && st.outliers == 2);
    uint32_t big[] = { 0xF0000000u, 0xF0000000u, 0xF0000000u, 0xF0000000u };
    CHECK(bench_stats(big, 4, 0xFFFFFFFFu, &st) == 0);    /* 32-bit sum / p50*x would overflow */
    CHECK(st.mean == 0xF0000000u && st.outliers == 0);

    /* 5. zero-iteration / bad-argument guards */
    fake_reset(0);
    Script s5 = { d1, 1, 0 };
    CHECK(bench_run(fake_op, &s5, 0, 3, NULL, &ovh) == 0 && s5.calls == 3);
    CHECK(bench_run(NULL, &s5, 4, 0, smp, &ovh) == -EINVAL);
    CHECK(bench_run(fake_op, &s5, 4, 0, NULL, &ovh) == -EINVAL);
    st.n = 0xABCD;
    CHECK(bench_stats(smp, 0, 2, &st) == -EINVAL && st.n == 0xABCD);
    CHECK(bench_stats(NULL, 4, 2, &st) == -EINVAL);

    /* 6. throughput MB/s: integer math, floor, overflow-safe */
    CHECK(bench_mbps(4096, 2048, CPU_MHZ) == 3000);
    CHECK(bench_mbps(1000, 3, 1000) == 333333);            /* 333333.33 -> floor */
    CHECK(bench_mbps(1ull << 54, 1000000, CPU_MHZ) == 27021597764222ull);  /* bytes*mhz > 2^64 */
    CHECK(bench_mbps(UINT64_MAX, 1, 2) == UINT64_MAX);     /* saturate */
    CHECK(bench_mbps(4096, 0, CPU_MHZ) == 0);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
