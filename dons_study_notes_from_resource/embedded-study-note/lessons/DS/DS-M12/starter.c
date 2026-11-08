/* DS-M12 희소 행렬 CSR과 SpMV
 * COO → CSR (counting sort + 중복 합산), CSR SpMV, CSR 전치(= CSC), 가지치기(pruning) → CSR,
 * 희소도별 메모리/시간을 밀집(dense) 버전과 비교.
 * 값은 int32 (정수라 밀집 결과와 == 로 정확히 비교), 곱·누적은 int64. 자료구조에 malloc 없음 — 버퍼는 호출자 제공.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum { CSR_OK = 0, CSR_ERANGE = -1, CSR_ENOSPC = -2, CSR_EOVERFLOW = -3 };
#define CSR_MAX_DIM 65536u               /* col_idx 가 uint16_t → 열 수 상한 (전치하면 행 수도) */

typedef struct { uint32_t row, col; int32_t val; } coo_t;   /* 좌표(COO) 트리플릿 */

typedef struct {
    uint32_t  nrows, ncols;
    uint32_t  nnz;                       /* = row_ptr[nrows] */
    uint32_t  cap;                       /* col_idx / val 칸 수 */
    uint32_t *row_ptr;                   /* nrows+1 칸. 행 i 의 원소 = [row_ptr[i], row_ptr[i+1]) */
    uint16_t *col_idx;                   /* 행 안에서 엄격히 오름차순 */
    int32_t  *val;                       /* 0 은 저장하지 않는다 (정규형, canonical) */
} csr_t;

static void csr_bind(csr_t *A, uint32_t *rp, uint16_t *ci, int32_t *v, uint32_t cap) {
    A->row_ptr = rp; A->col_idx = ci; A->val = v; A->cap = cap;
    A->nrows = A->ncols = A->nnz = 0; rp[0] = 0;
}
/* nrows x ncols 의 빈 행렬(유효한 CSR). 실패 경로도 이 상태로 남긴다 */
static void csr_set_empty(csr_t *A, uint32_t nrows, uint32_t ncols) {
    A->nrows = nrows; A->ncols = ncols; A->nnz = 0;
    for (uint32_t i = 0; i <= nrows; i++) A->row_ptr[i] = 0;
}
/* 저장 바이트: row_ptr 4B x (R+1) + 원소당 (col 2B + val 4B) */
static uint32_t csr_bytes(const csr_t *A) { return 4u * (A->nrows + 1u) + 6u * A->nnz; }

/* ---------------- Part A: COO → CSR, SpMV ---------------- */
/* 중복 좌표는 합산, 합이 0 이면 버린다(입력의 0 도). 합이 int32 를 넘으면 CSR_EOVERFLOW.
 * 필요: out->cap >= n_in (정리 전 칸 수), work 는 nrows 칸. 실패하면 out = 빈 행렬. */
static int coo_to_csr(const coo_t *in, uint32_t n_in, uint32_t nrows, uint32_t ncols,
                      csr_t *out, uint32_t *work) {
    (void)in; (void)n_in; (void)work;
    csr_set_empty(out, nrows <= CSR_MAX_DIM ? nrows : 0, ncols);
    /* TODO: 차원/용량/범위 검사 → 1) 행별 개수를 row_ptr[r+1] 에 → 2) 누적합 → 3) work[r] 커서로 흩뿌리기
     *       → 4) 행마다 열 기준 삽입 정렬, 같은 열은 int64 로 합산(넘치면 EOVERFLOW), 0 이면 버리며 압축 */
    return CSR_OK;
}

/* y = A x   (x: ncols 칸, y: nrows 칸) */
static void csr_spmv(const csr_t *A, const int32_t *x, int64_t *y) {
    (void)x;
    /* TODO: 행 i 마다 s = sum (int64)val[k] * x[col_idx[k]],  k in [row_ptr[i], row_ptr[i+1]) */
    for (uint32_t i = 0; i < A->nrows; i++) y[i] = 0;
}

/* ---------------- Part B: 전치 (= CSC), 전치 곱 ---------------- */
/* T = A^T. T 의 버퍼: row_ptr ncols+1 칸, cap >= A->nnz. work 는 A->ncols 칸. 실패하면 T = 빈 행렬 */
static int csr_transpose(const csr_t *A, csr_t *T, uint32_t *work) {
    (void)work;
    csr_set_empty(T, A->ncols, A->nrows);
    /* TODO: 행 수/용량 검사 → 열별 개수 → 누적합 → work[c] 커서 → A 를 행 순서대로 돌며 T 에 (i, val) 흩뿌리기 */
    return CSR_OK;
}

/* y = A^T x  전치를 만들지 않고 (x: nrows 칸, y: ncols 칸) — 흩뿌리기(scatter) */
static void csr_spmv_t(const csr_t *A, const int32_t *x, int64_t *y) {
    (void)x;
    /* TODO: y = 0 으로 시작, 행 i 의 원소마다 y[col_idx[k]] += val[k] * x[i]  (int64) */
    for (uint32_t c = 0; c < A->ncols; c++) y[c] = 0;
}

/* ---------------- Part C: 가지치기(magnitude pruning) → CSR ---------------- */
/* 밀집 R x C 가중치에서 |w| > thresh 인 것만 CSR 로. thresh = 0 이면 "0 만 버리기". 실패하면 out = 빈 행렬 */
static int csr_from_dense_prune(const int32_t *W, uint32_t R, uint32_t C, int32_t thresh, csr_t *out) {
    (void)W; (void)thresh;
    csr_set_empty(out, R <= CSR_MAX_DIM ? R : 0, C);
    /* TODO: 행마다 row_ptr[i] = w, |W[i][j]| > thresh (int64 로 절댓값) 이면 (j, W) 추가. 칸 부족이면 빈 행렬 + ENOSPC */
    return CSR_OK;
}

/* ---------------- 검사 도구 (주어짐) ---------------- */
/* 정규형 CSR 불변식: row_ptr[0]=0, 단조 증가, row_ptr[R]=nnz<=cap, 열 < ncols 이고 행 안에서 엄격 증가, 값 != 0 */
static bool csr_check(const csr_t *A) {
    if (A->row_ptr[0] != 0 || A->row_ptr[A->nrows] != A->nnz || A->nnz > A->cap) return false;
    for (uint32_t i = 0; i < A->nrows; i++) {
        uint32_t b = A->row_ptr[i], e = A->row_ptr[i + 1u];
        if (b > e) return false;
        for (uint32_t k = b; k < e; k++) {
            if (A->col_idx[k] >= A->ncols || A->val[k] == 0) return false;
            if (k > b && A->col_idx[k - 1u] >= A->col_idx[k]) return false;
        }
    }
    return true;
}
/* CSR 을 밀집으로 펼쳐 D 와 같은지 (D 는 R x C, int64) */
static bool csr_equals_dense(const csr_t *A, const int64_t *D, uint32_t R, uint32_t C) {
    if (A->nrows != R || A->ncols != C) return false;
    uint32_t nz = 0;
    for (uint32_t i = 0; i < R * C; i++) nz += D[i] != 0;
    if (nz != A->nnz) return false;
    for (uint32_t i = 0; i < R; i++)
        for (uint32_t k = A->row_ptr[i]; k < A->row_ptr[i + 1u]; k++)
            if (D[i * C + A->col_idx[k]] != A->val[k]) return false;
    return true;
}
static bool csr_same(const csr_t *A, const csr_t *B) {               /* 배열까지 완전히 같은가 */
    return A->nrows == B->nrows && A->ncols == B->ncols && A->nnz == B->nnz &&
           !memcmp(A->row_ptr, B->row_ptr, (A->nrows + 1u) * sizeof *A->row_ptr) &&
           (A->nnz == 0 || (!memcmp(A->col_idx, B->col_idx, A->nnz * sizeof *A->col_idx) &&
                            !memcmp(A->val, B->val, A->nnz * sizeof *A->val)));
}
/* 밀집 참조: y = W x (W 는 int32 R x C) */
static void dense_gemv(const int32_t *W, uint32_t R, uint32_t C, const int32_t *x, int64_t *y) {
    for (uint32_t i = 0; i < R; i++) {
        int64_t s = 0;
        for (uint32_t j = 0; j < C; j++) s += (int64_t)W[(size_t)i * C + j] * x[j];
        y[i] = s;
    }
}

static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/* ---------------- 버퍼 (정적) ---------------- */
#define SM_DIM 40u                       /* 랜덤 테스트 행렬 최대 크기 */
#define SM_CAP 400u
#define BIG_N  512u                      /* Part C 측정: 512 x 512 */
static uint32_t s_rp[3][SM_DIM + 1u], s_work[SM_DIM];
static uint16_t s_ci[3][SM_CAP];
static int32_t  s_v[3][SM_CAP];
static coo_t    s_coo[SM_CAP];
static int64_t  s_D[SM_DIM * SM_DIM], s_DT[SM_DIM * SM_DIM];
static int32_t  s_x[SM_DIM];
static int64_t  s_y1[SM_DIM], s_y2[SM_DIM];
static uint32_t b_rp[BIG_N + 1u];
static uint16_t b_ci[BIG_N * BIG_N];
static int32_t  b_v[BIG_N * BIG_N], b_W[BIG_N * BIG_N], b_Wp[BIG_N * BIG_N], b_x[BIG_N];
static int64_t  b_y1[BIG_N], b_y2[BIG_N];
static volatile int64_t g_sink;

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    csr_t A, B, T;
    csr_bind(&A, s_rp[0], s_ci[0], s_v[0], SM_CAP);
    csr_bind(&T, s_rp[1], s_ci[1], s_v[1], SM_CAP);
    csr_bind(&B, s_rp[2], s_ci[2], s_v[2], SM_CAP);

    /* 예제: 4 x 5, 순서 뒤섞임 + 중복 (0,3)=15+5, 상쇄 (1,2)=7-7 */
    static const coo_t ex[] = { {2, 4, 40}, {0, 3, 15}, {3, 0, 50}, {0, 0, 10}, {2, 1, 30},
                                {0, 3, 5},  {3, 2, 60}, {1, 2, 7},  {1, 2, -7} };
    static const uint32_t ex_rp[] = { 0, 2, 2, 4, 6 };
    static const uint16_t ex_ci[] = { 0, 3, 1, 4, 0, 2 };
    static const int32_t  ex_v[]  = { 10, 20, 30, 40, 50, 60 };

    /* Part A: 예제 변환과 SpMV */
    {
        int rc = coo_to_csr(ex, 9, 4, 5, &A, s_work);
        CHECK(rc == CSR_OK && csr_check(&A) && A.nnz == 6);
        CHECK(!memcmp(A.row_ptr, ex_rp, sizeof ex_rp) && !memcmp(A.col_idx, ex_ci, sizeof ex_ci) &&
              !memcmp(A.val, ex_v, sizeof ex_v));
        static const int32_t x[] = { 1, 2, 3, 4, 5 };
        int64_t y[4] = { -1, -1, -1, -1 };
        csr_spmv(&A, x, y);
        CHECK(y[0] == 90 && y[1] == 0 && y[2] == 260 && y[3] == 230);
    }
    /* Part A: 경계 — 빈 입력, 범위 밖, 용량 부족, 합산 넘침 */
    {
        CHECK(coo_to_csr(ex, 0, 3, 4, &A, s_work) == CSR_OK && csr_check(&A) && A.nnz == 0 && A.nrows == 3);
        static const coo_t bad[] = { {0, 0, 1}, {1, 5, 2} };           /* 열 5 >= 5 */
        CHECK(coo_to_csr(bad, 2, 4, 5, &A, s_work) == CSR_ERANGE && csr_check(&A) && A.nnz == 0);
        csr_t small;
        csr_bind(&small, s_rp[2], s_ci[2], s_v[2], 8);
        CHECK(coo_to_csr(ex, 9, 4, 5, &small, s_work) == CSR_ENOSPC && csr_check(&small) && small.nnz == 0);
        static const coo_t ov[] = { {1, 1, INT32_MAX}, {1, 1, 1} };
        CHECK(coo_to_csr(ov, 2, 2, 2, &A, s_work) == CSR_EOVERFLOW && csr_check(&A) && A.nnz == 0);
        static const coo_t mid[] = { {1, 1, INT32_MAX}, {1, 1, INT32_MAX}, {1, 1, -INT32_MAX} };
        CHECK(coo_to_csr(mid, 3, 2, 2, &A, s_work) == CSR_OK && A.nnz == 1 && A.val[0] == INT32_MAX);
    }
    /* Part A: 랜덤 2000회 — 밀집 누적 참조와 대조 (중복·상쇄·0 입력이 많도록 값 [-3,3]) */
    {
        bool ok = true;
        uint32_t dups = 0, cancels = 0;
        rng_state = 0xC5A12u;
        for (int t = 0; t < 2000 && ok; t++) {
            uint32_t R = 1u + xorshift32() % SM_DIM, C = 1u + xorshift32() % SM_DIM;
            uint32_t n = xorshift32() % (SM_CAP + 1u);
            memset(s_D, 0, sizeof s_D);
            for (uint32_t k = 0; k < n; k++) {
                coo_t e = { xorshift32() % R, xorshift32() % C, (int32_t)(xorshift32() % 7u) - 3 };
                s_coo[k] = e;
                int64_t *d = &s_D[e.row * C + e.col];
                if (*d != 0 && *d + e.val == 0) cancels++;
                dups += *d != 0;
                *d += e.val;
            }
            ok = coo_to_csr(s_coo, n, R, C, &A, s_work) == CSR_OK && csr_check(&A) &&
                 csr_equals_dense(&A, s_D, R, C);
            for (uint32_t j = 0; j < C; j++) s_x[j] = (int32_t)(xorshift32() % 2001u) - 1000;
            csr_spmv(&A, s_x, s_y1);
            for (uint32_t i = 0; ok && i < R; i++) {
                int64_t s = 0;
                for (uint32_t j = 0; j < C; j++) s += s_D[i * C + j] * s_x[j];
                ok = s == s_y1[i];
            }
        }
        CHECK(ok && dups > 1000 && cancels > 100);
        printf("[A] random 2000 COO->CSR + SpMV vs dense: %s (dup hits=%u, cancel-to-0=%u)\n",
               ok ? "match" : "MISMATCH", dups, cancels);
    }

    /* Part B: 예제 전치와 전치 곱 */
    {
        coo_to_csr(ex, 9, 4, 5, &A, s_work);
        static const uint32_t t_rp[] = { 0, 2, 3, 4, 5, 6 };
        static const uint16_t t_ci[] = { 0, 3, 2, 3, 0, 2 };
        static const int32_t  t_v[]  = { 10, 50, 30, 60, 20, 40 };
        CHECK(csr_transpose(&A, &T, s_work) == CSR_OK && csr_check(&T) && T.nrows == 5 && T.ncols == 4);
        CHECK(!memcmp(T.row_ptr, t_rp, sizeof t_rp) && !memcmp(T.col_idx, t_ci, sizeof t_ci) &&
              !memcmp(T.val, t_v, sizeof t_v));
        static const int32_t x[] = { 1, 2, 3, 4 };
        int64_t y[5] = { -1, -1, -1, -1, -1 };
        csr_spmv_t(&A, x, y);
        CHECK(y[0] == 210 && y[1] == 90 && y[2] == 240 && y[3] == 20 && y[4] == 120);
    }
    /* Part B: 경계 — nnz 0, 1 x N, N x 1, 용량 부족 */
    {
        CHECK(coo_to_csr(ex, 0, 3, 7, &A, s_work) == CSR_OK && csr_transpose(&A, &T, s_work) == CSR_OK &&
              csr_check(&T) && T.nrows == 7 && T.ncols == 3 && T.nnz == 0);
        static const coo_t row1[] = { {0, 6, 1}, {0, 0, 2}, {0, 3, 3} };
        CHECK(coo_to_csr(row1, 3, 1, 7, &A, s_work) == CSR_OK && csr_transpose(&A, &T, s_work) == CSR_OK &&
              csr_check(&T) && T.nrows == 7 && T.nnz == 3 && T.row_ptr[1] == 1 && T.row_ptr[7] == 3 &&
              T.val[0] == 2 && T.val[1] == 3 && T.val[2] == 1);
        CHECK(csr_transpose(&T, &B, s_work) == CSR_OK && csr_same(&A, &B));   /* N x 1 → 다시 1 x N */
        csr_t small;
        csr_bind(&small, s_rp[2], s_ci[2], s_v[2], 2);
        CHECK(csr_transpose(&A, &small, s_work) == CSR_ENOSPC && csr_check(&small) && small.nnz == 0);
    }
    /* Part B: 랜덤 1000회 — 전치 == 밀집 전치, 두 번 전치 == 원본, A^T x 두 방법 == 밀집 */
    {
        bool ok = true;
        rng_state = 0x7A5Eu;
        for (int t = 0; t < 1000 && ok; t++) {
            uint32_t R = 1u + xorshift32() % SM_DIM, C = 1u + xorshift32() % SM_DIM;
            uint32_t n = xorshift32() % (SM_CAP + 1u);
            memset(s_D, 0, sizeof s_D);
            for (uint32_t k = 0; k < n; k++) {
                coo_t e = { xorshift32() % R, xorshift32() % C, (int32_t)(xorshift32() % 201u) - 100 };
                s_coo[k] = e;
                s_D[e.row * C + e.col] += e.val;
            }
            for (uint32_t i = 0; i < R; i++)
                for (uint32_t j = 0; j < C; j++) s_DT[j * R + i] = s_D[i * C + j];
            ok = coo_to_csr(s_coo, n, R, C, &A, s_work) == CSR_OK &&
                 csr_transpose(&A, &T, s_work) == CSR_OK && csr_check(&T) && csr_equals_dense(&T, s_DT, C, R) &&
                 csr_transpose(&T, &B, s_work) == CSR_OK && csr_same(&A, &B);
            for (uint32_t i = 0; i < R; i++) s_x[i] = (int32_t)(xorshift32() % 2001u) - 1000;
            csr_spmv_t(&A, s_x, s_y1);
            csr_spmv(&T, s_x, s_y2);
            for (uint32_t j = 0; ok && j < C; j++) {
                int64_t s = 0;
                for (uint32_t i = 0; i < R; i++) s += s_DT[j * R + i] * s_x[i];
                ok = s == s_y1[j] && s == s_y2[j];
            }
        }
        CHECK(ok);
        printf("[B] random 1000 transpose / double-transpose / A^T x: %s\n", ok ? "match" : "MISMATCH");
    }

    /* Part C: 가지치기 → CSR, SpMV == 가지친 밀집 GEMV */
    {
        bool ok = true;
        static int32_t W[24 * 30], Wp[24 * 30];
        rng_state = 31337u;
        for (uint32_t i = 0; i < 24u * 30u; i++) W[i] = (int32_t)(xorshift32() % 2001u) - 1000;
        W[5] = INT32_MIN; W[6] = 0;                                    /* abs(INT32_MIN) 함정 + 0 */
        const int32_t th[] = { 0, 100, 500, 900, 999 };
        for (unsigned t = 0; t < 5 && ok; t++) {
            uint32_t kept = 0;
            for (uint32_t i = 0; i < 24u * 30u; i++) {
                int64_t a = W[i] < 0 ? -(int64_t)W[i] : W[i];
                Wp[i] = a > th[t] ? W[i] : 0;
                kept += a > th[t];
            }
            csr_t P;
            csr_bind(&P, b_rp, b_ci, b_v, BIG_N * BIG_N);
            ok = csr_from_dense_prune(W, 24, 30, th[t], &P) == CSR_OK && csr_check(&P) && P.nnz == kept &&
                 P.nrows == 24 && P.ncols == 30;
            for (uint32_t j = 0; j < 30; j++) s_x[j] = (int32_t)(xorshift32() % 2001u) - 1000;
            csr_spmv(&P, s_x, s_y1);
            dense_gemv(Wp, 24, 30, s_x, s_y2);
            ok = ok && !memcmp(s_y1, s_y2, 24 * sizeof s_y1[0]);
        }
        CHECK(ok);
        csr_t small;
        csr_bind(&small, s_rp[2], s_ci[2], s_v[2], 10);
        CHECK(csr_from_dense_prune(W, 24, 30, 0, &small) == CSR_ENOSPC && csr_check(&small) && small.nnz == 0);
    }
    /* Part C: 희소도별 메모리/시간 — 512 x 512, |w| 는 1..1000 균등 → 임계값 t 면 밀도 ≈ (1000-t)/1000 */
    {
        const uint32_t N = BIG_N, REPS = 200, BATCH = 5;
        const int32_t th[] = { 0, 500, 750, 900, 950, 990, 999 };
        csr_t P;
        csr_bind(&P, b_rp, b_ci, b_v, BIG_N * BIG_N);
        rng_state = 2026u;
        for (uint32_t i = 0; i < N * N; i++) {
            int32_t m = (int32_t)(1u + xorshift32() % 1000u);
            b_W[i] = (xorshift32() & 1u) ? m : -m;
        }
        for (uint32_t j = 0; j < N; j++) b_x[j] = (int32_t)(xorshift32() % 255u) - 127;
        bool ok = true;
        printf("[C] 512x512 int32, sparsity | nnz    | dense B | CSR B   | CSR/dense | dense ns | CSR ns  | speedup\n");
        for (unsigned t = 0; t < sizeof th / sizeof th[0]; t++) {
            for (uint32_t i = 0; i < N * N; i++) b_Wp[i] = (b_W[i] < 0 ? -b_W[i] : b_W[i]) > th[t] ? b_W[i] : 0;
            ok = ok && csr_from_dense_prune(b_W, N, N, th[t], &P) == CSR_OK && csr_check(&P);
            double best_d = 1e30, best_s = 1e30;
            for (uint32_t b = 0; b < BATCH; b++) {
                double t0 = now_ns();
                for (uint32_t r = 0; r < REPS; r++) { dense_gemv(b_Wp, N, N, b_x, b_y1); g_sink = b_y1[r % N]; }
                double t1 = now_ns();
                for (uint32_t r = 0; r < REPS; r++) { csr_spmv(&P, b_x, b_y2); g_sink = b_y2[r % N]; }
                double t2 = now_ns();
                if ((t1 - t0) / REPS < best_d) best_d = (t1 - t0) / REPS;
                if ((t2 - t1) / REPS < best_s) best_s = (t2 - t1) / REPS;
            }
            ok = ok && !memcmp(b_y1, b_y2, N * sizeof b_y1[0]);
            uint32_t dense_b = 4u * N * N, csr_b = csr_bytes(&P);
            printf("[C]          %5.1f%%        | %6u | %7u | %7u | %8.3f  | %8.0f | %7.0f | %6.2fx\n",
                   100.0 * (1.0 - (double)P.nnz / (N * N)), P.nnz, dense_b, csr_b, (double)csr_b / dense_b,
                   best_d, best_s, best_d / best_s);
        }
        CHECK(ok);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
