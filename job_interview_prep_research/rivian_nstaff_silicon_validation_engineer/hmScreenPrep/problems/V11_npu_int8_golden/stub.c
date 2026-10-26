/*
 * V11  AI Accelerator Validation: INT8 GEMM Golden Model + Bit-Exact Compare
 *
 * The NPU computes a quantized fully-connected / 1x1-conv layer as an int8 GEMM.
 * You write the C golden model the compiler team signs off on, a comparator that
 * characterises mismatches, and a seeded stimulus generator.
 *
 *   acc[m][n] = bias[n] + sum_k (A[m][k] - a_zp) * B[k][n]        int32 accumulate
 *   C[m][n]   = clamp(requant(acc, mult[c], shift[c]) + out_zp, act_min, act_max)
 *               c = n (per-channel) or 0 (per-tensor); relu -> act_min = out_zp
 *   A[M][K], B[K][N], C[M][N] row-major int8; weights symmetric (zero point 0).
 *
 *   requant(x, mult, shift) = RDBPOT(SRDHM(x, mult), shift)   (TFLite / gemmlowp)
 *     SRDHM(a, b): high 32 bits of 2*a*b, rounded with this exact nudge (note: a
 *                  negative tie rounds toward 0, e.g. -1.5 -> -1; bit-exact = formula-exact):
 *         if a == b == INT32_MIN return INT32_MAX;
 *         p = (int64)a * b;  nudge = p >= 0 ? 1<<30 : 1 - (1<<30);
 *         return (int32)((p + nudge) / 2^31);          (C division, truncates to 0)
 *     RDBPOT(x, e): x / 2^e rounded half away from zero, 0 <= e <= 31:
 *         mask = (1<<e) - 1; rem = x & mask; thr = (mask >> 1) + (x < 0);
 *         return (x >> e) + (rem > thr);               (arithmetic shift)
 *   real scale = mult / 2^31 / 2^shift,  mult in [2^30, 2^31)
 *
 * Implement:
 *   srdhm, rdbpot, requant_i8   the fixed-point pieces above
 *   gemm_ref                    golden GEMM; acc_out (may be NULL) receives the int32 acc
 *   cmp_tensors                 default mode bit-exact; CMP_TOL1 accepts |diff| <= 1.
 *                               Report mismatches (outside tolerance), max |diff|,
 *                               histogram of |diff| (=0, =1, >1), first mismatch in
 *                               row-major order (-1,-1 if none), per-column mismatch count.
 *   diagnose                    turn a report into a likely bug class (rules in Diag enum).
 *   gen_tensor                  seeded LCG stimulus with corner values (see function).
 *   No malloc. M, N <= CMP_MAX_N, K <= 64.
 *
 * Build & run:  ./run.sh V11
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define CMP_MAX_N  32
#define NPU_TILE   8          /* NPU output tile width (columns) */

typedef struct {
    int32_t        a_zp;         /* input zero point */
    int32_t        out_zp;       /* output zero point */
    const int32_t *mult;         /* [N] if per_channel else [1] */
    const int8_t  *shift;        /* right shift, same indexing as mult */
    bool           per_channel;
    bool           relu;         /* act_min = out_zp instead of -128 */
} QParams;

typedef enum { CMP_BITEXACT = 0, CMP_TOL1 = 1 } CmpMode;

typedef struct {
    uint32_t mismatches;          /* elements outside the tolerance */
    uint32_t hist[3];             /* |diff| == 0, == 1, > 1   (all elements) */
    int32_t  max_abs;             /* max |diff| over all elements */
    int      first_m, first_n;    /* first mismatch, row-major; -1,-1 if none */
    uint32_t col_bad[CMP_MAX_N];  /* mismatches per output column */
} CmpReport;

typedef enum {
    DIAG_PASS = 0,     /* no mismatch */
    DIAG_TILE_EDGE,    /* N % tile != 0 and every bad column is in the last partial tile */
    DIAG_ONE_CHANNEL,  /* every mismatch is in a single column (bias / per-channel param) */
    DIAG_ROUNDING,     /* max |diff| == 1, spread over several columns */
    DIAG_CORRUPTION    /* anything else: large, scattered diffs (overflow, data path) */
} Diag;

/* ======================= your code ======================= */

int32_t srdhm(int32_t a, int32_t b)
{
    /* TODO: SaturatingRoundingDoublingHighMul (formula in the header comment) */
    (void)a; (void)b;
    return 0;
}

int32_t rdbpot(int32_t x, int e)
{
    /* TODO: RoundingDivideByPowerOfTwo */
    (void)x; (void)e;
    return 0;
}

int8_t requant_i8(int32_t acc, int32_t mult, int shift, int32_t out_zp,
                  int32_t act_min, int32_t act_max)
{
    /* TODO: rdbpot(srdhm(acc, mult), shift) + out_zp, then clamp */
    (void)acc; (void)mult; (void)shift; (void)out_zp; (void)act_min; (void)act_max;
    return 0;
}

void gemm_ref(const int8_t *A, const int8_t *B, const int32_t *bias, int M, int K, int N,
              const QParams *q, int8_t *C, int32_t *acc_out)
{
    /* TODO: int32 accumulate, per-channel or per-tensor requant, relu clamp */
    (void)A; (void)B; (void)bias; (void)K; (void)q; (void)acc_out;
    memset(C, 0, (size_t)M * (size_t)N);
}

void cmp_tensors(const int8_t *gold, const int8_t *hw, int M, int N, CmpMode mode, CmpReport *r)
{
    /* TODO: mismatches, max_abs, hist, first mismatch, col_bad */
    (void)gold; (void)hw; (void)M; (void)N; (void)mode;
    memset(r, 0, sizeof *r);
}

Diag diagnose(const CmpReport *r, int N, int tile)
{
    /* TODO: PASS / TILE_EDGE / ONE_CHANNEL / ROUNDING / CORRUPTION */
    (void)r; (void)N; (void)tile;
    return DIAG_CORRUPTION;
}

/*
 * LCG: s = s * 1664525 + 1013904223 (mod 2^32), one step per element.
 * If (s >> 8) % 8 == 0 the element is a corner value CORNER[(s >> 4) & 3]
 * with CORNER = { -128, 127, 0, zp }; otherwise it is (int8_t)(s >> 24).
 */
void gen_tensor(int8_t *t, size_t n, uint32_t seed, int8_t zp)
{
    /* TODO */
    (void)seed; (void)zp;
    memset(t, 0, n);
}

/* ============ provided: NPU behavioural model with injectable bugs (do not edit) ============ */
/* Stands in for the RTL / FPGA image. Computes tile by tile, NPU_TILE output columns at a time. */

typedef enum { BUG_NONE, BUG_TRUNC_ROUND, BUG_ACC16, BUG_NO_BIAS_CH, BUG_TILE_EDGE } NpuBug;
#define NPU_BUG_CH 5          /* channel whose bias is dropped for BUG_NO_BIAS_CH */

static int32_t hw_requant(int32_t x, int32_t mult, int shift, bool trunc)
{
    int64_t p = (int64_t)x * mult;
    if (trunc) {                                  /* bug: floor instead of round, both steps */
        int32_t hi = (int32_t)(p >> 31);
        return hi >> shift;
    }
    int64_t nudge = p >= 0 ? (1LL << 30) : (1 - (1LL << 30));
    int32_t hi = (int32_t)((p + nudge) / (1LL << 31));
    int32_t mask = (int32_t)((1u << shift) - 1u);
    return (hi >> shift) + ((hi & mask) > (mask >> 1) + (hi < 0));
}

static void npu_gemm(const int8_t *A, const int8_t *B, const int32_t *bias, int M, int K, int N,
                     const QParams *q, int8_t *C, NpuBug bug)
{
    int32_t lo = q->relu ? q->out_zp : -128;
    for (int n0 = 0; n0 < N; n0 += NPU_TILE) {
        int w = (N - n0 < NPU_TILE) ? N - n0 : NPU_TILE;
        int kmax = (bug == BUG_TILE_EDGE && w < NPU_TILE) ? K - 1 : K;   /* remainder-path bug */
        for (int m = 0; m < M; m++)
            for (int j = 0; j < w; j++) {
                int n = n0 + j;
                int32_t acc = (bug == BUG_NO_BIAS_CH && n == NPU_BUG_CH) ? 0 : bias[n];
                for (int k = 0; k < kmax; k++) {
                    acc += ((int32_t)A[m * K + k] - q->a_zp) * (int32_t)B[k * N + n];
                    if (bug == BUG_ACC16) acc = (int16_t)(uint16_t)(uint32_t)acc;  /* 16-bit wrap */
                }
                int c = q->per_channel ? n : 0;
                int32_t v = hw_requant(acc, q->mult[c], q->shift[c], bug == BUG_TRUNC_ROUND) + q->out_zp;
                C[m * N + n] = (int8_t)(v < lo ? lo : v > 127 ? 127 : v);
            }
    }
}

int main(void)
{
    /* 1. fixed-point building blocks */
    CHECK(srdhm(1 << 30, 1 << 30) == 1 << 29);           /* 0.5 * 0.5 = 0.25 */
    CHECK(srdhm(INT32_MIN, INT32_MIN) == INT32_MAX);     /* the one overflow case */
    CHECK(srdhm(-1000, 1 << 30) == -500);
    CHECK(srdhm(3, 1 << 30) == 2);                       /* 1.5 rounds away from 0 */
    CHECK(srdhm(-3, 1 << 30) == -1);                     /* -1.5 -> -1 (negative tie: toward 0) */
    CHECK(rdbpot(5, 1) == 3 && rdbpot(-5, 1) == -3);     /* 2.5 -> 3, -2.5 -> -3 */
    CHECK(rdbpot(4, 1) == 2 && rdbpot(-7, 2) == -2);     /* -1.75 -> -2 */
    CHECK(rdbpot(123, 0) == 123);
    /* scale 0.75 * 2^-4 = 0.046875 */
    CHECK(requant_i8(1000, 0x60000000, 4, 0, -128, 127) == 47);    /* 46.875 */
    CHECK(requant_i8(-1000, 0x60000000, 4, 0, -128, 127) == -47);
    CHECK(requant_i8(1000, 0x60000000, 4, 90, -128, 127) == 127);  /* saturate */
    CHECK(requant_i8(-1000, 0x60000000, 4, 10, 10, 127) == 10);    /* relu clamp */

    /* 2. hand-checkable 2x3x2 GEMM, per-tensor, a_zp = 1, out_zp = -1 */
    const int8_t A2[2 * 3] = { 11, 21, 31,
                               -9, 1, 127 };
    const int8_t B2[3 * 2] = { 1, -1,
                               2, 0,
                               3, 4 };
    const int32_t bias2[2] = { 10, -20 };
    const int32_t m1[1] = { 0x40000000 };     /* 0.5 */
    const int8_t  s1[1] = { 1 };              /* * 2^-1 -> scale 0.25 */
    QParams q2 = { 1, -1, m1, s1, false, false };
    int8_t C2[4]; int32_t acc2[4];
    gemm_ref(A2, B2, bias2, 2, 3, 2, &q2, C2, acc2);
    CHECK(acc2[0] == 150 && acc2[1] == 90 && acc2[2] == 378 && acc2[3] == 494);
    CHECK(C2[0] == 37 && C2[1] == 22 && C2[2] == 94 && C2[3] == 123);
    q2.relu = true; q2.out_zp = -100;
    gemm_ref(A2, B2, bias2, 2, 3, 2, &q2, C2, NULL);
    CHECK(C2[0] == -62 && C2[1] == -77 && C2[2] == -5 && C2[3] == 24);

    /* 3. stimulus generator: deterministic, hits every corner value */
    int8_t g[64], g2[64];
    gen_tensor(g, 64, 1u, 5);
    gen_tensor(g2, 64, 1u, 5);
    CHECK(memcmp(g, g2, 64) == 0);
    CHECK(g[0] == 60 && g[1] == 94 && g[2] == -127 && g[3] == -76);
    int seen[4] = { 0 };
    for (int i = 0; i < 64; i++) {
        if (g[i] == -128) seen[0]++;
        if (g[i] == 127) seen[1]++;
        if (g[i] == 0) seen[2]++;
        if (g[i] == 5) seen[3]++;
    }
    CHECK(seen[0] > 0 && seen[1] > 0 && seen[2] > 0 && seen[3] > 0);

    /* 4. comparator on a tiny hand-made pair */
    const int8_t ga[6] = { 0, 10, -5, 7, 100, -128 };
    const int8_t ha[6] = { 0, 11, -5, 4, 100, -127 };
    CmpReport r;
    cmp_tensors(ga, ha, 2, 3, CMP_BITEXACT, &r);
    CHECK(r.mismatches == 3 && r.max_abs == 3 && r.first_m == 0 && r.first_n == 1);
    CHECK(r.hist[0] == 3 && r.hist[1] == 2 && r.hist[2] == 1);
    CHECK(r.col_bad[0] == 1 && r.col_bad[1] == 1 && r.col_bad[2] == 1);
    cmp_tensors(ga, ha, 2, 3, CMP_TOL1, &r);
    CHECK(r.mismatches == 1 && r.first_m == 1 && r.first_n == 0 && r.hist[1] == 2);
    cmp_tensors(ga, ga, 2, 3, CMP_BITEXACT, &r);
    CHECK(r.mismatches == 0 && r.first_m == -1 && r.first_n == -1 && diagnose(&r, 3, 8) == DIAG_PASS);

    /* 5. full layer vs the NPU model: M=6, K=32, N=12 (tiles: cols 0-7 full, 8-11 partial) */
    enum { M = 6, K = 32, N = 12 };
    int8_t A[M * K], B[K * N], gold[M * N], hw[M * N];
    int32_t bias[N], acc[M * N], mult[N];
    int8_t shift[N];
    gen_tensor(A, sizeof A, 1u, 3);
    gen_tensor(B, sizeof B, 2u, 0);
    for (int n = 0; n < N; n++) {
        bias[n] = (n % 2 ? -1 : 1) * (1000 + 250 * n);
        mult[n] = 0x40000000 + n * 0x02000000;   /* 0.5 .. 0.84 */
        shift[n] = 9;                            /* scale ~ 1/1000 .. 1/600 */
    }
    QParams q = { 3, -4, mult, shift, true, false };
    gemm_ref(A, B, bias, M, K, N, &q, gold, acc);
    CHECK(gold[0] == 12 && gold[1] == 50 && gold[11] == -3 && gold[M * N - 1] == -71);

    /* 5a. bug-free NPU is bit-exact */
    npu_gemm(A, B, bias, M, K, N, &q, hw, BUG_NONE);
    cmp_tensors(gold, hw, M, N, CMP_BITEXACT, &r);
    CHECK(r.mismatches == 0 && r.hist[0] == M * N && diagnose(&r, N, NPU_TILE) == DIAG_PASS);

    /* 5b. truncation instead of rounding: only +-1, fails bit-exact, passes tolerance */
    npu_gemm(A, B, bias, M, K, N, &q, hw, BUG_TRUNC_ROUND);
    cmp_tensors(gold, hw, M, N, CMP_BITEXACT, &r);
    CHECK(r.mismatches == 37 && r.max_abs == 1 && r.hist[2] == 0);
    CHECK(r.first_m == 0 && r.first_n == 0);
    CHECK(diagnose(&r, N, NPU_TILE) == DIAG_ROUNDING);
    cmp_tensors(gold, hw, M, N, CMP_TOL1, &r);
    CHECK(r.mismatches == 0);

    /* 5c. 16-bit accumulator wrap: errors only where the true |acc| needs > 16 bits */
    npu_gemm(A, B, bias, M, K, N, &q, hw, BUG_ACC16);
    cmp_tensors(gold, hw, M, N, CMP_BITEXACT, &r);
    CHECK(r.mismatches == 27 && r.max_abs == 86 && r.hist[2] == 27);
    CHECK(diagnose(&r, N, NPU_TILE) == DIAG_CORRUPTION);
    int big = 0, bad_small = 0;
    for (int i = 0; i < M * N; i++) {
        bool wide = acc[i] > INT16_MAX || acc[i] < INT16_MIN;
        if (wide) big++;
        if (gold[i] != hw[i] && !wide) bad_small++;
    }
    CHECK(big == 27 && bad_small == 0);        /* every failure is a wide accumulator */

    /* 5d. bias dropped on channel 5: one bad column, every row */
    npu_gemm(A, B, bias, M, K, N, &q, hw, BUG_NO_BIAS_CH);
    cmp_tensors(gold, hw, M, N, CMP_BITEXACT, &r);
    CHECK(r.mismatches == 6 && r.max_abs == 3 && r.col_bad[NPU_BUG_CH] == 6 && r.first_m == 0 && r.first_n == 5);
    CHECK(diagnose(&r, N, NPU_TILE) == DIAG_ONE_CHANNEL);

    /* 5e. last partial tile drops k = K-1: failures confined to columns >= 8 */
    npu_gemm(A, B, bias, M, K, N, &q, hw, BUG_TILE_EDGE);
    cmp_tensors(gold, hw, M, N, CMP_BITEXACT, &r);
    uint32_t below = 0;
    for (int n = 0; n < 8; n++) below += r.col_bad[n];
    CHECK(below == 0 && r.mismatches == 16 && r.first_m == 0 && r.first_n == 8);
    CHECK(diagnose(&r, N, NPU_TILE) == DIAG_TILE_EDGE);
    CHECK(diagnose(&r, 16, NPU_TILE) != DIAG_TILE_EDGE);   /* N % 8 == 0: no partial tile */

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
