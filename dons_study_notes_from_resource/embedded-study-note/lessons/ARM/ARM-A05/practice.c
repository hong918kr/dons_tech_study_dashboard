/* ARM-A05 추가 연습 — 한 번에 검증
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-A05_p && /tmp/ARM-A05_p
 * 연습 1: SMMLA / SDOT(by-element) 명령의 C 모델 vs 실제 명령 (실리콘 검증식 "명령 모델" 비교)
 * 연습 2: K > 131071 — K 를 잘라 int32 GEMM 을 여러 번 돌리고 int64 로 모은다
 * 연습 3: uint8 활성값 x int8 가중치 (USMMLA) + 영점(zero-point) 보정
 * 연습 4: 타일 낭비율과 산술 강도 (roofline 의 x 축)
 */
#include <arm_neon.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DOTPROD __attribute__((target("+dotprod")))
#define I8MM    __attribute__((target("+i8mm")))

typedef void (*ukernel_fn)(const int8_t *Ap, const int8_t *Bp, size_t kb, int32_t *C, size_t ldc);
extern void p_smmla_8x8_kernel(const int8_t *, const int8_t *, size_t, int32_t *, size_t);
extern void usmmla_8x8_kernel(const int8_t *, const int8_t *, size_t, int32_t *, size_t);

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint64_t g_rng = 0xD1B54A32D192ED03ull;
static uint64_t xs(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static int8_t rnd_s8(void) { return (int8_t)(uint8_t)(xs() >> 56); }

/* ---- (본문 solution.c 축약판) 패킹 + 8x8 드라이버 ---- */
static size_t rup(size_t x, size_t m) { return (x + m - 1) / m * m; }
static void pack_A(const int8_t *A, size_t M, size_t K, size_t lda, size_t MR, size_t KU, int8_t *out) {
    size_t kb = (K + KU - 1) / KU;
    for (size_t i0 = 0; i0 < M; i0 += MR)
        for (size_t b = 0; b < kb; b++)
            for (size_t r = 0; r < MR; r++)
                for (size_t t = 0; t < KU; t++) {
                    size_t i = i0 + r, k = b * KU + t;
                    *out++ = (i < M && k < K) ? A[i * lda + k] : 0;
                }
}
static void pack_B(const int8_t *B, size_t K, size_t N, size_t ldb, size_t NR, size_t KU, int8_t *out) {
    size_t kb = (K + KU - 1) / KU;
    for (size_t j0 = 0; j0 < N; j0 += NR)
        for (size_t b = 0; b < kb; b++)
            for (size_t c = 0; c < NR; c++)
                for (size_t t = 0; t < KU; t++) {
                    size_t j = j0 + c, k = b * KU + t;
                    *out++ = (j < N && k < K) ? B[k * ldb + j] : 0;
                }
}
/* 8x8, KU=8 고정. ws >= rup(M,8)*rup(K,8) + rup(N,8)*rup(K,8) */
static void gemm8(ukernel_fn fn, size_t M, size_t N, size_t K, const int8_t *A, size_t lda,
                  const int8_t *B, size_t ldb, int32_t *C, size_t ldc, int8_t *ws) {
    size_t kb = (K + 7) / 8, pa = 8 * kb * 8;
    int8_t *Ap = ws, *Bp = ws + rup(M, 8) * kb * 8;
    pack_A(A, M, K, lda, 8, 8, Ap);
    pack_B(B, K, N, ldb, 8, 8, Bp);
    int32_t tile[64];
    for (size_t j0 = 0, q = 0; j0 < N; j0 += 8, q++)
        for (size_t i0 = 0, p = 0; i0 < M; i0 += 8, p++) {
            fn(Ap + p * pa, Bp + q * pa, kb, tile, 8);
            for (size_t r = 0; r < 8 && i0 + r < M; r++)
                for (size_t c = 0; c < 8 && j0 + c < N; c++) C[(i0 + r) * ldc + j0 + c] = tile[r * 8 + c];
        }
}

/* ======================= 연습 1: 명령 모델 ======================= */
/* SMMLA: d(2x2, 행 우선) += n(2x8) · m(2x8)^T. int32 덧셈은 감싸므로(modular) uint32 로 계산해 UB 를 피한다. */
static void smmla_model(int32_t d[4], const int8_t n[16], const int8_t m[16]) {
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            uint32_t s = (uint32_t)d[2 * i + j];
            for (int k = 0; k < 8; k++) s += (uint32_t)(n[8 * i + k] * m[8 * j + k]);
            d[2 * i + j] = (int32_t)s;
        }
}
/* SDOT by-element: d.s[j] += Σ_t n.b[4j+t] · m.b[4L+t] */
static void sdot_lane_model(int32_t d[4], const int8_t n[16], const int8_t m[16], int L) {
    for (int j = 0; j < 4; j++) {
        uint32_t s = (uint32_t)d[j];
        for (int t = 0; t < 4; t++) s += (uint32_t)(n[4 * j + t] * m[4 * L + t]);
        d[j] = (int32_t)s;
    }
}
I8MM static void smmla_hw(int32_t d[4], const int8_t n[16], const int8_t m[16]) {
    vst1q_s32(d, vmmlaq_s32(vld1q_s32(d), vld1q_s8(n), vld1q_s8(m)));
}
DOTPROD static void sdot_lane2_hw(int32_t d[4], const int8_t n[16], const int8_t m[16]) {
    vst1q_s32(d, vdotq_laneq_s32(vld1q_s32(d), vld1q_s8(n), vld1q_s8(m), 2));
}
static void ex1_models(void) {
    long bad_mm = 0, bad_dot = 0;
    for (int it = 0; it < 20000; it++) {
        int8_t n[16], m[16];
        int32_t d0[4], a[4], b[4];
        int mode = it % 4;                               /* 0: 랜덤, 1: 전부 −128, 2: −128/127 섞기, 3: 0 */
        for (int i = 0; i < 16; i++) {
            n[i] = mode == 0 ? rnd_s8() : mode == 1 ? -128 : mode == 2 ? ((xs() & 1) ? 127 : -128) : 0;
            m[i] = mode == 0 ? rnd_s8() : mode == 1 ? -128 : mode == 2 ? ((xs() & 1) ? 127 : -128) : 0;
        }
        for (int i = 0; i < 4; i++) d0[i] = (it & 8) ? (int32_t)(INT32_MAX - (xs() % 100000)) : (int32_t)(uint32_t)xs();
        memcpy(a, d0, 16); memcpy(b, d0, 16);
        smmla_model(a, n, m); smmla_hw(b, n, m);
        bad_mm += memcmp(a, b, 16) != 0;
        memcpy(a, d0, 16); memcpy(b, d0, 16);
        sdot_lane_model(a, n, m, 2); sdot_lane2_hw(b, n, m);
        bad_dot += memcmp(a, b, 16) != 0;
    }
    CHECK(bad_mm == 0);
    CHECK(bad_dot == 0);
    /* 감쌈(포화 아님) 직접 확인: INT32_MAX + 8·16384 */
    int8_t n[16], m[16];
    memset(n, 0x80, 16); memset(m, 0x80, 16);
    int32_t d[4] = {INT32_MAX, 0, 0, 0};
    smmla_hw(d, n, m);
    CHECK(d[0] == (int32_t)(0x7FFFFFFFu + 131072u) && d[0] == -2147352577 && d[1] == 131072);
}

/* ======================= 연습 2: 긴 K ======================= */
#define KC 131064u   /* 8 의 배수이면서 <= 131071 → 한 조각은 int32 안 */
static void gemm_s8_long(size_t M, size_t N, size_t K, const int8_t *A, size_t lda, const int8_t *B, size_t ldb,
                         int64_t *C, int32_t *tmp, int8_t *ws) {
    for (size_t i = 0; i < M * N; i++) C[i] = 0;
    for (size_t k0 = 0; k0 < K; k0 += KC) {
        size_t kc = K - k0 < KC ? K - k0 : KC;
        gemm8(p_smmla_8x8_kernel, M, N, kc, A + k0, lda, B + k0 * ldb, ldb, tmp, N, ws);
        for (size_t i = 0; i < M * N; i++) C[i] += tmp[i];
    }
}
static void ex2_long_k(void) {
    enum { K = 300000 };
    static int8_t A[2 * K], B[K * 3], ws[8 * 131064 * 2];
    int64_t C[6];
    int32_t tmp[6];
    memset(A, 0x80, sizeof A); memset(B, 0x80, sizeof B);
    for (size_t k = 0; k < K; k += 3) B[k * 3 + 2] = 127;          /* 열 2 는 3개 중 1개만 −16256 */
    gemm_s8_long(2, 3, K, A, K, B, 3, C, tmp, ws);
    CHECK(C[0] == 4915200000LL && C[4] == 4915200000LL);            /* 300000 · 16384, int32 로는 불가능 */
    CHECK(C[2] == 200000LL * 16384 + 100000LL * -16256);           /* = 1651200000 */
    /* 한 번에 돌리면(int32 누산) 감싸서 틀린다 — 비교용 */
    static int32_t one[6];
    static int8_t ws1[8 * 300000 * 2];
    gemm8(p_smmla_8x8_kernel, 2, 3, K, A, K, B, 3, one, 3, ws1);
    CHECK(one[0] == (int32_t)(uint32_t)4915200000LL && one[0] == 620232704);   /* 4915200000 − 2^32 */
}

/* ======================= 연습 3: u8 x s8 + 영점 ======================= */
/* 실수값 x ≈ sx·(xq − za) (xq ∈ 0..255, za = 영점), w ≈ sw·wq (대칭, 영점 0).
 * Σ_k (xq−za)·wq = Σ xq·wq − za·colsum(W)[n]   ← colsum 은 가중치 로드 때 1번 계산 */
#define K_U8S8_SAFE 65793u   /* ⌊2^31 / 32640⌋: 최악 |곱| = 255·128 */
static void colsum_s8(const int8_t *W, size_t K, size_t N, int32_t *cs) {
    for (size_t n = 0; n < N; n++) cs[n] = 0;
    for (size_t k = 0; k < K; k++)
        for (size_t n = 0; n < N; n++) cs[n] += W[k * N + n];
}
static void qgemm_u8s8_zp(size_t M, size_t N, size_t K, const uint8_t *X, int za, const int8_t *W,
                          const int32_t *cs, int32_t *C, int8_t *ws) {
    gemm8(usmmla_8x8_kernel, M, N, K, (const int8_t *)X, K, W, N, C, N, ws);   /* 바이트는 그대로, 해석만 u8 */
    for (size_t m = 0; m < M; m++)
        for (size_t n = 0; n < N; n++) C[m * N + n] -= za * cs[n];
}
static void ex3_u8s8(void) {
    static uint8_t X[11 * 90];
    static int8_t W[90 * 21], ws[16 * 96 + 24 * 96];
    static int32_t C[11 * 21], cs[21];
    long bad = 0, bad_raw = 0;
    for (int it = 0; it < 60; it++) {
        size_t M = 1 + xs() % 11, N = 1 + xs() % 21, K = 1 + xs() % 90;
        int za = (int)(xs() % 256);
        for (size_t i = 0; i < M * K; i++) X[i] = (uint8_t)(xs() >> 56);
        for (size_t i = 0; i < K * N; i++) W[i] = rnd_s8();
        colsum_s8(W, K, N, cs);
        qgemm_u8s8_zp(M, N, K, X, za, W, cs, C, ws);
        for (size_t m = 0; m < M; m++)
            for (size_t n = 0; n < N; n++) {
                int64_t s = 0, raw = 0;
                for (size_t k = 0; k < K; k++) {
                    s += (int64_t)((int)X[m * K + k] - za) * W[k * N + n];
                    raw += (int64_t)X[m * K + k] * W[k * N + n];
                }
                bad += C[m * N + n] != s;
                bad_raw += (C[m * N + n] + za * cs[n]) != raw;
            }
    }
    CHECK(bad == 0 && bad_raw == 0);
    /* 부호 확인: X = 255 (u8), W = −1 → USMMLA 는 255·(−1) = −255. SMMLA 로 잘못 돌리면 (−1)(−1) = +1 */
    uint8_t x1[8]; int8_t w1[8]; int32_t c1, c2;
    memset(x1, 255, 8); memset(w1, -1, 8);
    gemm8(usmmla_8x8_kernel, 1, 1, 8, (const int8_t *)x1, 8, w1, 1, &c1, 1, ws);
    gemm8(p_smmla_8x8_kernel, 1, 1, 8, (const int8_t *)x1, 8, w1, 1, &c2, 1, ws);
    CHECK(c1 == -2040 && c2 == 8);
}

/* ======================= 연습 4: 타일 낭비율, 산술 강도 ======================= */
/* 쓸모 있는 출력 비율 (천분율, 내림): M·N / (rup(M,MR)·rup(N,NR)) */
static unsigned util_permille(size_t M, size_t N, size_t MR, size_t NR) {
    return (unsigned)(1000 * M * N / (rup(M, MR) * rup(N, NR)));
}
/* 산술 강도 (ops/byte, x1000 내림): 2MNK / (MK + KN + 4MN)   — A, B 1번 읽고 C(int32) 1번 쓰는 최소 트래픽 */
static uint64_t ai_milli(uint64_t M, uint64_t N, uint64_t K) {
    return 2000 * M * N * K / (M * K + K * N + 4 * M * N);
}
static void ex4_util_ai(void) {
    CHECK(util_permille(1, 1024, 8, 8) == 125);   /* 배치 1 (GEMV) 을 8x8 타일로: 1/8 만 쓸모 */
    CHECK(util_permille(1, 1024, 4, 4) == 250);
    CHECK(util_permille(17, 17, 8, 8) == 501);    /* 289 / 576 */
    CHECK(util_permille(64, 64, 8, 8) == 1000);
    CHECK(ai_milli(64, 64, 64) == 21333 && ai_milli(256, 256, 256) == 85333 && ai_milli(1024, 1024, 1024) == 341333);
    CHECK(ai_milli(1, 1024, 1024) == 1990);       /* GEMV: 바이트당 2 ops → 메모리에 묶인다 */
    printf("info: AI ops/B  64^3 %.2f  256^3 %.2f  1024^3 %.2f  GEMV(1x1024x1024) %.3f\n",
           ai_milli(64, 64, 64) / 1000.0, ai_milli(256, 256, 256) / 1000.0, ai_milli(1024, 1024, 1024) / 1000.0,
           ai_milli(1, 1024, 1024) / 1000.0);
}

int main(void) {
    ex1_models();
    ex2_long_k();
    ex3_u8s8();
    ex4_util_ai();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
