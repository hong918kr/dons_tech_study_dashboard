/* ARM-A05 int8 GEMM 마이크로커널: SDOT·SMMLA — 패킹 + 커널 3개 + 드라이버 + 양자화 FC 레이어 + 테스트 하네스
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c solution.S shared_bugs.S -o /tmp/ARM-A05_v && /tmp/ARM-A05_v
 *       (starter: 같은 명령에 starter.c starter.S)
 * C[MxN] int32 = A[MxK] int8 · B[KxN] int8, 행 우선(row-major), 행 간격 lda/ldb/ldc (원소 수).
 * 정확성 기준: int64 로 누산하는 스칼라 참조. int32 누산기는 K <= 131071 이면 어떤 int8 입력에도 넘치지 않는다 (Part B).
 * -DNO_MAIN 으로 빌드하면 main 을 빼고 demo_bench.c 가 커널을 가져다 쓴다.
 */
#include <arm_neon.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DOTPROD __attribute__((target("+dotprod")))   /* vdotq_laneq_s32 는 FEAT_DotProd 함수에서만 */
#define GEMM_K_SAFE 131071u   /* ⌊(2^31−1) / 16384⌋ : 최악 곱 (−128)(−128) = 16384 를 이만큼 더해도 int32 안 */

/* ---- 마이크로커널 공통 시그니처 (solution.S / shared_bugs.S / 아래 intrinsics) ---- */
typedef void (*ukernel_fn)(const int8_t *Ap, const int8_t *Bp, size_t kb, int32_t *C, size_t ldc);
typedef struct { const char *name; size_t MR, NR, KU; ukernel_fn fn; } kdesc_t;

extern void sdot_4x4_kernel(const int8_t *, const int8_t *, size_t, int32_t *, size_t);    /* .S, Part A-1 */
extern void smmla_8x8_kernel(const int8_t *, const int8_t *, size_t, int32_t *, size_t);   /* .S, Part A-2 */
extern void bug_udot_4x4_kernel(const int8_t *, const int8_t *, size_t, int32_t *, size_t);
extern void bug_smmla_nozip_kernel(const int8_t *, const int8_t *, size_t, int32_t *, size_t);

/* ---- 버전 0: 스칼라 (기준선). 자동 벡터화를 꺼서 "진짜 스칼라" 로 남긴다 (5장에서 asm 확인) ---- */
#if defined(__clang__)
#define NOVEC _Pragma("clang loop vectorize(disable) interleave(disable)")
#define NOVEC_FN __attribute__((noinline))
#else   /* gcc (리눅스 VM 빌드) */
#define NOVEC
#define NOVEC_FN __attribute__((noinline, optimize("no-tree-vectorize")))
#endif
NOVEC_FN
void gemm_scalar(size_t M, size_t N, size_t K, const int8_t *A, size_t lda,
                 const int8_t *B, size_t ldb, int32_t *C, size_t ldc) {
    for (size_t i = 0; i < M; i++)
        for (size_t j = 0; j < N; j++) {
            int32_t s = 0;                                    /* K <= GEMM_K_SAFE 계약 */
            NOVEC for (size_t k = 0; k < K; k++) s += A[i * lda + k] * B[k * ldb + j];
            C[i * ldc + j] = s;
        }
}

/* ======================= Part A — 패킹 (TODO) ======================= */
/* A (MxK, lda) → MR 행 패널들. 패널 p, k-블록 b (깊이 KU) 의 바이트 (r, t) 위치:
 *   out[((p*kb + b)*MR + r)*KU + t] = A[(p*MR + r)*lda + b*KU + t]   (범위 밖이면 0)
 * kb = ceil(K/KU). 총 크기 = ceil(M/MR)*MR * kb*KU 바이트. SDOT 은 KU=4, SMMLA 는 KU=8 — 공식은 같다. */
void pack_A(const int8_t *A, size_t M, size_t K, size_t lda, size_t MR, size_t KU, int8_t *out) {
    size_t kb = (K + KU - 1) / KU;
    for (size_t i0 = 0; i0 < M; i0 += MR)
        for (size_t b = 0; b < kb; b++)
            for (size_t r = 0; r < MR; r++)
                for (size_t t = 0; t < KU; t++) {
                    size_t i = i0 + r, k = b * KU + t;
                    *out++ = (i < M && k < K) ? A[i * lda + k] : 0;
                }
}

/* B (KxN, ldb) → NR 열 패널들. out[((q*kb + b)*NR + c)*KU + t] = B[(b*KU + t)*ldb + q*NR + c] (밖이면 0).
 * 즉 열 하나의 KU 깊이가 연속 — SDOT 의 "lane = 열 4바이트", SMMLA 의 "2x8 = 열쌍" 이 그대로 나온다. */
void pack_B(const int8_t *B, size_t K, size_t N, size_t ldb, size_t NR, size_t KU, int8_t *out) {
    size_t kb = (K + KU - 1) / KU;
    for (size_t j0 = 0; j0 < N; j0 += NR)
        for (size_t b = 0; b < kb; b++)
            for (size_t c = 0; c < NR; c++)
                for (size_t t = 0; t < KU; t++) {
                    size_t j = j0 + c, k = b * KU + t;
                    *out++ = (j < N && k < K) ? B[k * ldb + j] : 0;
                }
}

/* Part C 용: 가중치가 W[N][K] (출력 채널 행 우선, ldw) 로 저장된 경우. B = W^T 와 같은 패킹을 만든다. */
void pack_B_nk(const int8_t *W, size_t N, size_t K, size_t ldw, size_t NR, size_t KU, int8_t *out) {
    size_t kb = (K + KU - 1) / KU;
    for (size_t j0 = 0; j0 < N; j0 += NR)
        for (size_t b = 0; b < kb; b++)
            for (size_t c = 0; c < NR; c++)
                for (size_t t = 0; t < KU; t++) {
                    size_t j = j0 + c, k = b * KU + t;
                    *out++ = (j < N && k < K) ? W[j * ldw + k] : 0;   /* 연속 k → memcpy 로 바꿀 수 있다 */
                }
}

/* ======================= Part A — SDOT 8x8 (intrinsics, TODO) ======================= */
/* 패킹: MR=8, NR=8, KU=4. 블록마다 A 32 B (a0 = 행 0..3, a1 = 행 4..7), B 32 B (b0 = 열 0..3, b1 = 열 4..7).
 * vdotq_laneq_s32(acc, b, a, L): acc.s[j] += Σ_t b.b[4j+t] · a.b[4L+t]  → 행 (4h+L) 의 열 4개. 누산기 16개. */
#define ROW8(r, a, L) do { acc[r][0] = vdotq_laneq_s32(acc[r][0], b0, a, L); \
                           acc[r][1] = vdotq_laneq_s32(acc[r][1], b1, a, L); } while (0)
DOTPROD void sdot_8x8_kernel_neon(const int8_t *Ap, const int8_t *Bp, size_t kb, int32_t *C, size_t ldc) {
    int32x4_t acc[8][2];
    for (int r = 0; r < 8; r++) acc[r][0] = acc[r][1] = vdupq_n_s32(0);
    for (size_t b = 0; b < kb; b++, Ap += 32, Bp += 32) {
        int8x16_t a0 = vld1q_s8(Ap), a1 = vld1q_s8(Ap + 16);
        int8x16_t b0 = vld1q_s8(Bp), b1 = vld1q_s8(Bp + 16);
        ROW8(0, a0, 0); ROW8(1, a0, 1); ROW8(2, a0, 2); ROW8(3, a0, 3);
        ROW8(4, a1, 0); ROW8(5, a1, 1); ROW8(6, a1, 2); ROW8(7, a1, 3);
    }
    for (int r = 0; r < 8; r++) {
        vst1q_s32(C + (size_t)r * ldc, acc[r][0]);
        vst1q_s32(C + (size_t)r * ldc + 4, acc[r][1]);
    }
}

const kdesc_t KD_SDOT4 = {"sdot4x4(.S)", 4, 4, 4, sdot_4x4_kernel};
const kdesc_t KD_SDOT8 = {"sdot8x8(neon)", 8, 8, 4, sdot_8x8_kernel_neon};
const kdesc_t KD_MMLA8 = {"smmla8x8(.S)", 8, 8, 8, smmla_8x8_kernel};

/* ======================= 드라이버 (완성) ======================= */
static size_t rup(size_t x, size_t m) { return (x + m - 1) / m * m; }
size_t packA_bytes(const kdesc_t *kd, size_t M, size_t K) { return rup(M, kd->MR) * rup(K, kd->KU); }
size_t packB_bytes(const kdesc_t *kd, size_t K, size_t N) { return rup(N, kd->NR) * rup(K, kd->KU); }
size_t gemm_ws_bytes(const kdesc_t *kd, size_t M, size_t N, size_t K) {
    return packA_bytes(kd, M, K) + packB_bytes(kd, K, N);
}

/* 패킹된 패널로 타일을 돈다. 꽉 찬 타일은 C 에 바로, 가장자리 타일은 임시 타일에 받아 유효 부분만 복사. */
void gemm_packed(const kdesc_t *kd, size_t M, size_t N, size_t K,
                 const int8_t *Ap, const int8_t *Bp, int32_t *C, size_t ldc) {
    size_t KU = kd->KU, kb = (K + KU - 1) / KU;
    size_t pa = kd->MR * kb * KU, pb = kd->NR * kb * KU;   /* 패널 하나의 바이트 수 */
    int32_t tile[8 * 8];
    for (size_t j0 = 0, q = 0; j0 < N; j0 += kd->NR, q++)
        for (size_t i0 = 0, p = 0; i0 < M; i0 += kd->MR, p++) {
            const int8_t *a = Ap + p * pa, *b = Bp + q * pb;
            if (i0 + kd->MR <= M && j0 + kd->NR <= N) {
                kd->fn(a, b, kb, C + i0 * ldc + j0, ldc);
            } else {
                kd->fn(a, b, kb, tile, kd->NR);
                size_t mr = M - i0 < kd->MR ? M - i0 : kd->MR, nr = N - j0 < kd->NR ? N - j0 : kd->NR;
                for (size_t r = 0; r < mr; r++)
                    memcpy(C + (i0 + r) * ldc + j0, tile + r * kd->NR, nr * sizeof(int32_t));
            }
        }
}

/* ws: gemm_ws_bytes() 이상. 테스트는 ws 를 쓰레기(0x5A)로 채운 뒤 부른다 → 패딩을 안 쓰면 들킨다. */
void gemm_s8(const kdesc_t *kd, size_t M, size_t N, size_t K, const int8_t *A, size_t lda,
             const int8_t *B, size_t ldb, int32_t *C, size_t ldc, int8_t *ws) {
    int8_t *Ap = ws, *Bp = ws + packA_bytes(kd, M, K);
    pack_A(A, M, K, lda, kd->MR, kd->KU, Ap);
    pack_B(B, K, N, ldb, kd->NR, kd->KU, Bp);
    gemm_packed(kd, M, N, K, Ap, Bp, C, ldc);
}

/* ======================= Part C — 양자화 FC 레이어 (TODO) ======================= */
/* 재양자화: y = clamp(zp + round_half_up(((int64)acc + bias) * mult / 2^shift), −128, 127)
 * 계약: 0 <= mult < 2^24, 1 <= shift <= 40, |acc|,|bias| < 2^31. round_half_up = floor(x + 1/2). */
int8_t requant_s8(int32_t acc, int32_t bias, int32_t mult, int shift, int zp) {
    int64_t p = ((int64_t)acc + bias) * mult;                    /* |p| < 2^32 · 2^24 = 2^56, 넘침 없음 */
    int64_t r = (p + ((int64_t)1 << (shift - 1))) >> shift;      /* 음수 >> : 구현 정의 (clang/gcc = 산술) */
    int64_t y = r + zp;
    return (int8_t)(y > 127 ? 127 : y < -128 ? -128 : y);
}

/* Y[m][n] = requant_s8(Σ_k X[m][k]·W[n][k], bias[n], mult[n], shift, zp_out)
 * X: MxK (ldx = K), Wp = pack_B_nk(W, N, K, K, 8, 8) 로 미리 패킹(모델 로드 때 1번),
 * acc: MxN int32 작업 버퍼, ws: packA_bytes(&KD_MMLA8, M, K) 이상. SMMLA 8x8 커널 사용. */
void qlinear_s8(const int8_t *X, size_t M, size_t K, const int8_t *Wp, size_t N,
                const int32_t *bias, const int32_t *mult, int shift, int zp_out,
                int8_t *Y, int32_t *acc, int8_t *ws) {
    pack_A(X, M, K, K, KD_MMLA8.MR, KD_MMLA8.KU, ws);
    gemm_packed(&KD_MMLA8, M, N, K, ws, Wp, acc, N);
    for (size_t m = 0; m < M; m++)
        for (size_t n = 0; n < N; n++)
            Y[m * N + n] = requant_s8(acc[m * N + n], bias[n], mult[n], shift, zp_out);
}

#ifndef NO_MAIN
/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;                     /* 고정 시드 xorshift64 */
static uint64_t xs(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static int8_t rnd_s8(void) { return (int8_t)(uint8_t)(xs() >> 56); }          /* −128..127 전 범위 */
static void fill_s8(int8_t *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = rnd_s8(); }

/* 참조: int64 누산 (넘침 없음) */
static void gemm_ref64(size_t M, size_t N, size_t K, const int8_t *A, size_t lda,
                       const int8_t *B, size_t ldb, int64_t *C) {
    for (size_t i = 0; i < M; i++)
        for (size_t j = 0; j < N; j++) {
            int64_t s = 0;
            for (size_t k = 0; k < K; k++) s += (int64_t)A[i * lda + k] * B[k * ldb + j];
            C[i * N + j] = s;
        }
}

#define MAXD 40
static int8_t  gA[MAXD * MAXD], gB[MAXD * MAXD], gWS[2 * 64 * 64];
static int32_t gC[(MAXD + 8) * (MAXD + 8)];
static int64_t gR[MAXD * MAXD];
#define CANARY ((int32_t)0x7EADBEEF)

/* 한 모양을 돌려 틀린 원소 수를 센다. C 는 ldc = N+8 (열 방향 여유) + 아래 8행 여유를 CANARY 로 채워
 * "M x N 바깥에 쓰지 않았나" 도 같이 본다 (바깥이 바뀌면 bad += 1000). */
typedef void (*gemm_fn)(const kdesc_t *, size_t, size_t, size_t, const int8_t *, size_t,
                        const int8_t *, size_t, int32_t *, size_t, int8_t *);
static long run_shape(const kdesc_t *kd, gemm_fn g, size_t M, size_t N, size_t K, int nonneg) {
    for (size_t i = 0; i < M * K; i++) gA[i] = nonneg ? (int8_t)(xs() >> 57) : rnd_s8();
    for (size_t i = 0; i < K * N; i++) gB[i] = nonneg ? (int8_t)(xs() >> 57) : rnd_s8();
    size_t ldc = N + 8, rows = M + 8;
    for (size_t i = 0; i < rows * ldc; i++) gC[i] = CANARY;
    memset(gWS, 0x5A, gemm_ws_bytes(kd, M, N, K));
    g(kd, M, N, K, gA, K, gB, N, gC, ldc, gWS);
    gemm_ref64(M, N, K, gA, K, gB, N, gR);
    long bad = 0;
    for (size_t i = 0; i < rows; i++)
        for (size_t j = 0; j < ldc; j++) {
            int inside = i < M && j < N;
            if (inside) bad += gC[i * ldc + j] != (int32_t)gR[i * N + j];
            else if (gC[i * ldc + j] != CANARY) bad += 1000;
        }
    return bad;
}

/* ---- Part B 용 버그 드라이버 (TODO 아님) ---- */
static void pack_nopad(const int8_t *X, size_t R, size_t K, size_t ld, size_t RB, size_t KU, int8_t *out, int isB) {
    size_t kb = (K + KU - 1) / KU;                      /* 패딩 자리를 건너뛰기만 한다 (0 을 안 씀) */
    for (size_t r0 = 0; r0 < R; r0 += RB)
        for (size_t b = 0; b < kb; b++)
            for (size_t r = 0; r < RB; r++)
                for (size_t t = 0; t < KU; t++, out++) {
                    size_t i = r0 + r, k = b * KU + t;
                    if (i < R && k < K) *out = isB ? X[k * ld + i] : X[i * ld + k];
                }
}
static void bug_gemm_nopadB(const kdesc_t *kd, size_t M, size_t N, size_t K, const int8_t *A, size_t lda,
                            const int8_t *B, size_t ldb, int32_t *C, size_t ldc, int8_t *ws) {
    int8_t *Ap = ws, *Bp = ws + packA_bytes(kd, M, K);
    pack_A(A, M, K, lda, kd->MR, kd->KU, Ap);              /* A 는 정상 (0 패딩) */
    pack_nopad(B, N, K, ldb, kd->NR, kd->KU, Bp, 1);       /* B 만 패딩 안 함 */
    gemm_packed(kd, M, N, K, Ap, Bp, C, ldc);
}
static void bug_gemm_nopadAB(const kdesc_t *kd, size_t M, size_t N, size_t K, const int8_t *A, size_t lda,
                             const int8_t *B, size_t ldb, int32_t *C, size_t ldc, int8_t *ws) {
    int8_t *Ap = ws, *Bp = ws + packA_bytes(kd, M, K);
    pack_nopad(A, M, K, lda, kd->MR, kd->KU, Ap, 0);       /* 둘 다 패딩 안 함 */
    pack_nopad(B, N, K, ldb, kd->NR, kd->KU, Bp, 1);
    gemm_packed(kd, M, N, K, Ap, Bp, C, ldc);
}
static void bug_gemm_noedge(const kdesc_t *kd, size_t M, size_t N, size_t K, const int8_t *A, size_t lda,
                            const int8_t *B, size_t ldb, int32_t *C, size_t ldc, int8_t *ws) {
    int8_t *Ap = ws, *Bp = ws + packA_bytes(kd, M, K);     /* 가장자리 타일도 C 에 통째로 씀 */
    pack_A(A, M, K, lda, kd->MR, kd->KU, Ap);
    pack_B(B, K, N, ldb, kd->NR, kd->KU, Bp);
    size_t kb = (K + kd->KU - 1) / kd->KU;
    for (size_t j0 = 0, q = 0; j0 < N; j0 += kd->NR, q++)
        for (size_t i0 = 0, p = 0; i0 < M; i0 += kd->MR, p++)
            kd->fn(Ap + p * kd->MR * kb * kd->KU, Bp + q * kd->NR * kb * kd->KU, kb, C + i0 * ldc + j0, ldc);
}

static const kdesc_t KD_BUG_UDOT  = {"bug_udot4x4", 4, 4, 4, bug_udot_4x4_kernel};
static const kdesc_t KD_BUG_NOZIP = {"bug_nozip8x8", 8, 8, 8, bug_smmla_nozip_kernel};

static void test_pack(void) {
    /* 손으로 그린 예 (5장 그림): A 5x6, MR=4, KU=4 → 패널 2개 x 블록 2개 x 16 B = 64 B */
    int8_t A[5 * 6], out[64];
    for (int i = 0; i < 30; i++) A[i] = (int8_t)(10 * (i / 6) + i % 6);   /* A[i][k] = 10i + k */
    memset(out, 0x5A, sizeof out);
    pack_A(A, 5, 6, 6, 4, 4, out);
    static const int8_t want[64] = {
        0, 1, 2, 3, 10, 11, 12, 13, 20, 21, 22, 23, 30, 31, 32, 33,      /* 패널0 블록0 */
        4, 5, 0, 0, 14, 15, 0, 0, 24, 25, 0, 0, 34, 35, 0, 0,            /* 패널0 블록1 (k=6,7 → 0) */
        40, 41, 42, 43, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,              /* 패널1 블록0 (행 5..7 → 0) */
        44, 45, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(memcmp(out, want, 64) == 0);
    CHECK(packA_bytes(&KD_SDOT4, 5, 6) == 64);
    /* B 3x5 (K=3, N=5), NR=4, KU=8 → 패널 2개 x 블록 1개 x 32 B */
    int8_t B[3 * 5], ob[64];
    for (int i = 0; i < 15; i++) B[i] = (int8_t)(10 * (i / 5) + i % 5);   /* B[k][j] = 10k + j */
    memset(ob, 0x5A, sizeof ob);
    pack_B(B, 3, 5, 5, 4, 8, ob);
    CHECK(ob[0] == 0 && ob[1] == 10 && ob[2] == 20 && ob[3] == 0 && ob[7] == 0);   /* 열 0: k=0,1,2 그다음 0 */
    CHECK(ob[8] == 1 && ob[9] == 11 && ob[10] == 21 && ob[24 + 2] == 23);
    CHECK(ob[32] == 4 && ob[33] == 14 && ob[34] == 24 && ob[40] == 0 && ob[63] == 0);   /* 패널1: 열 4 만 진짜 */
    /* 랜덤: 패킹 결과를 "읽는 쪽" 공식으로 되짚기 + pack_B_nk(W) == pack_B(W^T) */
    long bad = 0;
    for (int it = 0; it < 300; it++) {
        size_t M = 1 + xs() % 23, K = xs() % 41, lda = K + xs() % 3, MR = (xs() & 1) ? 4 : 8, KU = (xs() & 1) ? 4 : 8;
        static int8_t X[23 * 43], P[24 * 48], Q[24 * 48], T[43 * 23];
        fill_s8(X, M * lda);
        memset(P, 0x5A, sizeof P);
        pack_A(X, M, K, lda, MR, KU, P);
        size_t kb = (K + KU - 1) / KU;
        for (size_t i = 0; i < rup(M, MR); i++)
            for (size_t k = 0; k < kb * KU; k++) {
                int8_t v = P[(((i / MR) * kb + k / KU) * MR + i % MR) * KU + k % KU];
                bad += v != ((i < M && k < K) ? X[i * lda + k] : 0);
            }
        for (size_t i = 0; i < M; i++)                      /* T = X^T (K x M) */
            for (size_t k = 0; k < K; k++) T[k * M + i] = X[i * lda + k];
        memset(P, 0x11, sizeof P); memset(Q, 0x22, sizeof Q);
        pack_B_nk(X, M, K, lda, MR, KU, P);
        pack_B(T, K, M, M, MR, KU, Q);
        bad += memcmp(P, Q, rup(M, MR) * kb * KU) != 0;
    }
    CHECK(bad == 0);
}

static void test_kernel_unit(void) {
    /* 타일 하나: A[r][k] = r+1, B[k][c] = (c+1)·(−1)^k, K = 7 → C = (r+1)(c+1)·(+1 −1 +1 −1 +1 −1 +1) = (r+1)(c+1).
     * 커널을 직접 부르고 ldc 간격(=11) 바깥이 그대로인지도 확인. */
    int8_t Ap[8 * 8], Bp[8 * 8];
    int32_t C[8 * 11];
    const kdesc_t *ks[3] = {&KD_SDOT4, &KD_SDOT8, &KD_MMLA8};
    for (int s = 0; s < 3; s++) {
        const kdesc_t *kd = ks[s];
        int8_t A[8 * 7], B[7 * 8];
        for (int r = 0; r < 8; r++) for (int k = 0; k < 7; k++) A[r * 7 + k] = (int8_t)(r + 1);
        for (int k = 0; k < 7; k++) for (int c = 0; c < 8; c++) B[k * 8 + c] = (int8_t)((c + 1) * ((k & 1) ? -1 : 1));
        pack_A(A, kd->MR, 7, 7, kd->MR, kd->KU, Ap);
        pack_B(B, 7, kd->NR, 8, kd->NR, kd->KU, Bp);
        for (int i = 0; i < 8 * 11; i++) C[i] = CANARY;
        kd->fn(Ap, Bp, (7 + kd->KU - 1) / kd->KU, C, 11);
        int ok = 1;
        for (size_t r = 0; r < 8; r++)
            for (size_t c = 0; c < 11; c++) {
                int32_t want = (r < kd->MR && c < kd->NR) ? (int32_t)((r + 1) * (c + 1)) : CANARY;
                ok &= C[r * 11 + c] == want;
            }
        CHECK(ok);
        for (int i = 0; i < 8 * 11; i++) C[i] = CANARY;      /* kb = 0 → 0 타일 */
        kd->fn(Ap, Bp, 0, C, 11);
        CHECK(C[0] == 0 && C[(kd->MR - 1) * 11 + kd->NR - 1] == 0 && C[kd->NR] == CANARY);
    }
}

static void test_all_shapes(void) {
    /* M, N = 1..17, K = 0..37 전부 (10,982 모양) x 커널 3개, 전 범위 랜덤 int8 */
    const kdesc_t *ks[3] = {&KD_SDOT4, &KD_SDOT8, &KD_MMLA8};
    for (int s = 0; s < 3; s++) {
        long bad = 0, shapes = 0;
        for (size_t M = 1; M <= 17; M++)
            for (size_t N = 1; N <= 17; N++)
                for (size_t K = 0; K <= 37; K++, shapes++) bad += run_shape(ks[s], gemm_s8, M, N, K, 0) != 0;
        printf("info: %-14s %ld shapes, %ld bad\n", ks[s]->name, shapes, bad);
        CHECK(bad == 0);
    }
    /* M == 0 또는 N == 0: 아무것도 쓰지 않는다 */
    CHECK(run_shape(&KD_MMLA8, gemm_s8, 0, 5, 9, 0) == 0 && run_shape(&KD_SDOT4, gemm_s8, 3, 0, 9, 0) == 0);
}

static void test_strided_random(void) {
    /* lda > K, ldb > N, ldc > N 인 부분행렬 + 큰 모양, 스칼라 기준선도 같이 */
    static int8_t A[70 * 310], B[300 * 80];
    static int32_t C[4][70 * 90];
    static int64_t R[70 * 80];
    static int8_t ws[80 * 304 + 72 * 304];
    long bad[4] = {0, 0, 0, 0};
    const kdesc_t *ks[3] = {&KD_SDOT4, &KD_SDOT8, &KD_MMLA8};
    for (int it = 0; it < 24; it++) {
        size_t M = 1 + xs() % 64, N = 1 + xs() % 72, K = xs() % 300;
        size_t lda = K + xs() % 7, ldb = N + xs() % 5, ldc = N + xs() % 9;
        fill_s8(A, M * lda); fill_s8(B, K * ldb);
        for (size_t i = 0; i < M; i++)
            for (size_t j = 0; j < N; j++) {
                int64_t s = 0;
                for (size_t k = 0; k < K; k++) s += (int64_t)A[i * lda + k] * B[k * ldb + j];
                R[i * N + j] = s;
            }
        for (int s = 0; s < 3; s++) {
            memset(ws, 0x5A, sizeof ws);
            gemm_s8(ks[s], M, N, K, A, lda, B, ldb, C[s], ldc, ws);
        }
        gemm_scalar(M, N, K, A, lda, B, ldb, C[3], ldc);
        for (int s = 0; s < 4; s++)
            for (size_t i = 0; i < M; i++)
                for (size_t j = 0; j < N; j++) bad[s] += C[s][i * ldc + j] != (int32_t)R[i * N + j];
    }
    CHECK(bad[0] == 0); CHECK(bad[1] == 0); CHECK(bad[2] == 0); CHECK(bad[3] == 0);
}

static void test_horizon(void) {
    /* 2x2 출력, K 길이 극단값 −128 (모두 곱 = +16384). K = 131071 → 2147467264 (정확),
     * K = 131072 → 2^31 = 감쌈 → INT32_MIN (SDOT/SMMLA 는 포화 안 함). −128·127 은 더 안전 (|곱| 16256). */
    size_t Ks[2] = {GEMM_K_SAFE, GEMM_K_SAFE + 1};
    static int8_t A[2 * 131080], B[131080 * 2], ws[16 * 131080];
    const kdesc_t *ks[3] = {&KD_SDOT4, &KD_SDOT8, &KD_MMLA8};
    for (int s = 0; s < 3; s++)
        for (int h = 0; h < 2; h++) {
            size_t K = Ks[h];
            memset(A, 0x80, 2 * K); memset(B, 0x80, 2 * K);       /* 0x80 = −128 */
            B[1] = 127;                                          /* B[0][1] = 127 → 열 1 은 하나만 −16256 */
            int32_t C[4];
            gemm_s8(ks[s], 2, 2, K, A, K, B, 2, C, 2, ws);
            int64_t exact0 = (int64_t)K * 16384, exact1 = (int64_t)(K - 1) * 16384 - 16256;
            if (h == 0) CHECK(C[0] == exact0 && C[0] == 2147467264 && C[3] == exact1);
            else        CHECK(exact0 == 2147483648LL && C[0] == INT32_MIN && C[2] == INT32_MIN
                              && C[1] == (int32_t)exact1);          /* 열 1 = 2147451008 은 아직 안 넘침 */
        }
}

static void test_bugs(void) {
    /* 1) UDOT: 음수 없는 데이터에선 통과, 전 범위 데이터에선 실패 */
    long b_pos = 0, b_full = 0;
    for (size_t K = 1; K <= 20; K++) {
        b_pos += run_shape(&KD_BUG_UDOT, gemm_s8, 7, 9, K, 1) != 0;
        b_full += run_shape(&KD_BUG_UDOT, gemm_s8, 7, 9, K, 0) != 0;
    }
    printf("info: bug udot      : 0..127 data bad shapes %ld/20, full-range bad %ld/20\n", b_pos, b_full);
    CHECK(b_pos == 0 && b_full == 20);
    /* 2) SMMLA 결과를 zip 없이 저장 */
    long b_zip = run_shape(&KD_BUG_NOZIP, gemm_s8, 8, 8, 16, 0);
    printf("info: bug nozip     : 8x8x16 wrong elements %ld/64\n", b_zip);
    CHECK(b_zip > 0);
    /* 3) K 패딩: B 만 안 하면 여전히 맞다 (A 의 0 이 가린다). A·B 둘 다 안 하면 K % KU != 0 일 때만 틀린다 */
    long onlyB = 0, both_tail = 0, both_full = 0;
    for (size_t K = 1; K <= 24; K++) {
        onlyB += run_shape(&KD_MMLA8, bug_gemm_nopadB, 5, 6, K, 0) != 0;
        if (K % 8) both_tail += run_shape(&KD_MMLA8, bug_gemm_nopadAB, 5, 6, K, 0) != 0;
        else       both_full += run_shape(&KD_MMLA8, bug_gemm_nopadAB, 5, 6, K, 0) != 0;
    }
    printf("info: bug no K-pad  : B only bad %ld/24, A&B bad (K%%8!=0) %ld/21, A&B bad (K%%8==0) %ld/3\n",
           onlyB, both_tail, both_full);
    CHECK(onlyB == 0 && both_tail == 21 && both_full == 0);
    /* 4) 가장자리 타일을 C 에 통째로: 값은 맞아도 M x N 바깥(카나리)을 덮는다 */
    long e_full = run_shape(&KD_SDOT8, bug_gemm_noedge, 16, 8, 9, 0), e_edge = run_shape(&KD_SDOT8, bug_gemm_noedge, 13, 5, 9, 0);
    printf("info: bug no edge   : 16x8 bad %ld, 13x5 bad %ld (>=1000 = canary hit)\n", e_full, e_edge);
    CHECK(e_full == 0 && e_edge >= 1000);
}

static int64_t ref_requant(int64_t acc_plus_bias, int64_t mult, int shift, int zp) {
    int64_t num = acc_plus_bias * mult + ((int64_t)1 << (shift - 1)), d = (int64_t)1 << shift;
    int64_t q = num / d;                                     /* C 나눗셈은 0 쪽 버림 → floor 로 보정 */
    if (num % d != 0 && num < 0) q--;
    int64_t y = q + zp;
    return y > 127 ? 127 : y < -128 ? -128 : y;
}

static void test_qlinear(void) {
    /* requant 의 이름 붙은 경우: 반올림은 +∞ 쪽 반 (−0.5 → 0, −1.5 → −1), 포화, zp */
    CHECK(requant_s8(0, 0, 1, 1, 0) == 0);
    CHECK(requant_s8(1, 0, 1, 1, 0) == 1);       /* 0.5 → 1 */
    CHECK(requant_s8(-1, 0, 1, 1, 0) == 0);      /* −0.5 → 0 */
    CHECK(requant_s8(-3, 0, 1, 1, 0) == -1);     /* −1.5 → −1 */
    CHECK(requant_s8(-5, 0, 1, 2, 0) == -1);     /* −1.25 → −1 */
    CHECK(requant_s8(1000000, 0, 1 << 20, 20, 0) == 127 && requant_s8(-1000000, 0, 1 << 20, 20, 0) == -128);
    CHECK(requant_s8(100, 28, 3, 2, -10) == 86);  /* (128·3)/4 = 96, −10 → 86 */
    CHECK(requant_s8(INT32_MAX, INT32_MAX, (1 << 24) - 1, 40, 0) == 127 && requant_s8(INT32_MIN, INT32_MIN, (1 << 24) - 1, 40, 0) == -128);
    long bad = 0;
    for (int it = 0; it < 200000; it++) {
        int32_t acc = (int32_t)(uint32_t)xs(), bias = (int32_t)(uint32_t)xs() >> (xs() % 31);
        int32_t mult = (int32_t)(xs() % (1u << 24)); int sh = 1 + (int)(xs() % 40), zp = (int)(xs() % 256) - 128;
        bad += requant_s8(acc, bias, mult, sh, zp) != ref_requant((int64_t)acc + bias, mult, sh, zp);
    }
    CHECK(bad == 0);
    /* 레이어: M (배치) = 1, 3, 8, 13 / K, N 랜덤, 가중치는 W[N][K] 를 1번 미리 패킹 */
    static int8_t X[13 * 200], W[40 * 200], Wp[40 * 200 + 8 * 208], Y[13 * 40], ws[16 * 208];
    static int32_t acc[13 * 40], bias[40], mult[40];
    size_t Ms[4] = {1, 3, 8, 13};
    long lbad = 0;
    for (int it = 0; it < 40; it++) {
        size_t M = Ms[it % 4], N = 1 + xs() % 40, K = 1 + xs() % 200;
        fill_s8(X, M * K); fill_s8(W, N * K);
        for (size_t n = 0; n < N; n++) { bias[n] = (int32_t)(xs() % 20001) - 10000; mult[n] = (int32_t)(xs() % 4096); }
        int sh = 12 + (int)(xs() % 8), zp = (int)(xs() % 21) - 10;
        pack_B_nk(W, N, K, K, 8, 8, Wp);
        qlinear_s8(X, M, K, Wp, N, bias, mult, sh, zp, Y, acc, ws);
        for (size_t m = 0; m < M; m++)
            for (size_t n = 0; n < N; n++) {
                int64_t s = 0;
                for (size_t k = 0; k < K; k++) s += (int64_t)X[m * K + k] * W[n * K + k];
                lbad += Y[m * N + n] != ref_requant(s + bias[n], mult[n], sh, zp);
            }
    }
    CHECK(lbad == 0);
}

int main(void) {
    test_pack();
    test_kernel_unit();
    test_all_shapes();
    test_strided_random();
    test_horizon();
    test_bugs();
    test_qlinear();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
