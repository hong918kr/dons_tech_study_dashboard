/* DS-M12 추가 연습 — 한 번에 검증 */
#pragma STDC FP_CONTRACT OFF             /* 연습 3: a*b+c 를 FMA 로 합치지 않게 — 반올림 횟수를 분석과 맞춘다 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint32_t row, col; int32_t val; } coo_t;
typedef struct {
    uint32_t nrows, ncols, nnz, cap;
    uint32_t *row_ptr; uint16_t *col_idx; int32_t *val;
} csr_t;

static uint32_t rng = 1u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void csr_bind(csr_t *A, uint32_t *rp, uint16_t *ci, int32_t *v, uint32_t cap) {
    A->row_ptr = rp; A->col_idx = ci; A->val = v; A->cap = cap; A->nrows = A->ncols = A->nnz = 0; rp[0] = 0;
}
static bool csr_check(const csr_t *A) {
    if (A->row_ptr[0] != 0 || A->row_ptr[A->nrows] != A->nnz || A->nnz > A->cap) return false;
    for (uint32_t i = 0; i < A->nrows; i++)
        for (uint32_t k = A->row_ptr[i]; k < A->row_ptr[i + 1u]; k++) {
            if (A->row_ptr[i] > A->row_ptr[i + 1u] || A->col_idx[k] >= A->ncols || A->val[k] == 0) return false;
            if (k > A->row_ptr[i] && A->col_idx[k - 1u] >= A->col_idx[k]) return false;
        }
    return true;
}
static bool csr_same(const csr_t *A, const csr_t *B) {
    return A->nrows == B->nrows && A->ncols == B->ncols && A->nnz == B->nnz &&
           !memcmp(A->row_ptr, B->row_ptr, (A->nrows + 1u) * sizeof *A->row_ptr) &&
           (A->nnz == 0 || (!memcmp(A->col_idx, B->col_idx, A->nnz * sizeof *A->col_idx) &&
                            !memcmp(A->val, B->val, A->nnz * sizeof *A->val)));
}

#define DIM 64u
#define CAP 5000u
static coo_t    g_coo[CAP], g_tmp[CAP];
static uint32_t g_cnt[DIM + 1u], g_rp[4][DIM + 1u], g_work[DIM];
static uint16_t g_ci[4][CAP];
static int32_t  g_v[4][CAP];
static int64_t  g_D[DIM * DIM];
static uint32_t g_moves;                 /* 원소 이동 횟수 (연습 1 측정) */

/* ---------- 연습 1: 정렬 없는 O(nnz + R + C) 정규화 — 2-pass counting (LSD radix) ---------- */
/* 열 기준 안정 counting → 행 기준 안정 counting 이면 행 안이 열 오름차순. 그 뒤 선형 합산 */
static void counting_pass(const coo_t *src, coo_t *dst, uint32_t n, uint32_t nkeys, bool by_row) {
    memset(g_cnt, 0, (nkeys + 1u) * sizeof g_cnt[0]);
    for (uint32_t k = 0; k < n; k++) g_cnt[(by_row ? src[k].row : src[k].col) + 1u]++;
    for (uint32_t c = 0; c < nkeys; c++) g_cnt[c + 1u] += g_cnt[c];
    for (uint32_t k = 0; k < n; k++) { dst[g_cnt[by_row ? src[k].row : src[k].col]++] = src[k]; g_moves++; }
}
static void coo_to_csr_radix(coo_t *a, uint32_t n, uint32_t R, uint32_t C, csr_t *out) {
    counting_pass(a, g_tmp, n, C, false);                  /* 1) 열 기준 */
    counting_pass(g_tmp, a, n, R, true);                   /* 2) 행 기준 (안정 → 열 순서 유지) */
    out->nrows = R; out->ncols = C;
    uint32_t w = 0, k = 0;
    for (uint32_t r = 0; r < R; r++) {
        out->row_ptr[r] = w;
        while (k < n && a[k].row == r) {
            uint32_t c = a[k].col;
            int64_t s = 0;
            while (k < n && a[k].row == r && a[k].col == c) s += a[k++].val;
            if (s != 0) { out->col_idx[w] = (uint16_t)c; out->val[w] = (int32_t)s; w++; }  /* 값 작게 → 넘침 없음 */
        }
    }
    out->row_ptr[R] = w; out->nnz = w;
}
/* 비교 대상: 본문 방식 (행 counting + 행 안 삽입 정렬) 의 이동 횟수만 센다 */
static void coo_to_csr_isort(const coo_t *in, uint32_t n, uint32_t R, csr_t *out) {
    uint32_t *rp = out->row_ptr;
    memset(rp, 0, (R + 1u) * sizeof *rp);
    for (uint32_t k = 0; k < n; k++) rp[in[k].row + 1u]++;
    for (uint32_t r = 0; r < R; r++) { rp[r + 1u] += rp[r]; g_work[r] = rp[r]; }
    for (uint32_t k = 0; k < n; k++) { uint32_t p = g_work[in[k].row]++; out->col_idx[p] = (uint16_t)in[k].col; g_moves++; }
    for (uint32_t r = 0; r < R; r++)
        for (uint32_t i = rp[r] + 1u; i < rp[r + 1u]; i++) {
            uint16_t c = out->col_idx[i]; uint32_t j = i;
            while (j > rp[r] && out->col_idx[j - 1u] > c) { out->col_idx[j] = out->col_idx[j - 1u]; j--; g_moves++; }
            out->col_idx[j] = c;
        }
}

/* ---------- 연습 2: work 버퍼 없는 전치 — row_ptr 시프트 트릭 ---------- */
static void transpose_work(const csr_t *A, csr_t *T) {               /* 본문 방식 (대조용) */
    T->nrows = A->ncols; T->ncols = A->nrows; T->nnz = A->nnz;
    uint32_t *rp = T->row_ptr;
    memset(rp, 0, (A->ncols + 1u) * sizeof *rp);
    for (uint32_t k = 0; k < A->nnz; k++) rp[A->col_idx[k] + 1u]++;
    for (uint32_t c = 0; c < A->ncols; c++) { rp[c + 1u] += rp[c]; g_work[c] = rp[c]; }
    for (uint32_t i = 0; i < A->nrows; i++)
        for (uint32_t k = A->row_ptr[i]; k < A->row_ptr[i + 1u]; k++) {
            uint32_t p = g_work[A->col_idx[k]]++;
            T->col_idx[p] = (uint16_t)i; T->val[p] = A->val[k];
        }
}
static void transpose_shift(const csr_t *A, csr_t *T) {
    T->nrows = A->ncols; T->ncols = A->nrows; T->nnz = A->nnz;
    uint32_t *rp = T->row_ptr;
    memset(rp, 0, (A->ncols + 1u) * sizeof *rp);
    for (uint32_t k = 0; k < A->nnz; k++) rp[A->col_idx[k]]++;       /* rp[c] = 열 c 개수 (한 칸 왼쪽) */
    uint32_t sum = 0;
    for (uint32_t c = 0; c < A->ncols; c++) { uint32_t t = rp[c]; rp[c] = sum; sum += t; }   /* 배타적 누적합 */
    rp[A->ncols] = sum;
    for (uint32_t i = 0; i < A->nrows; i++)                           /* rp[c] 자체를 커서로 */
        for (uint32_t k = A->row_ptr[i]; k < A->row_ptr[i + 1u]; k++) {
            uint32_t p = rp[A->col_idx[k]]++;
            T->col_idx[p] = (uint16_t)i; T->val[p] = A->val[k];
        }
    for (uint32_t c = A->ncols; c > 0; c--) rp[c] = rp[c - 1u];       /* 이제 rp[c] = 끝 = 다음 시작 → 한 칸 밀기 */
    rp[0] = 0;
}

/* ---------- 연습 3: float SpMV — 언제 == 이고 언제 허용 오차가 필요한가 ---------- */
#define FN 200u
static float  f_W[FN * FN], f_val[FN * FN], f_x[FN];
static uint32_t f_rp[FN + 1u];
static uint16_t f_ci[FN * FN];
static void f_dense(const float *x, float *y) {
    for (uint32_t i = 0; i < FN; i++) { float s = 0.0f; for (uint32_t j = 0; j < FN; j++) s += f_W[i * FN + j] * x[j]; y[i] = s; }
}
static void f_csr_seq(const float *x, float *y) {                    /* 왼→오 순차 누적 */
    for (uint32_t i = 0; i < FN; i++) { float s = 0.0f; for (uint32_t k = f_rp[i]; k < f_rp[i + 1u]; k++) s += f_val[k] * x[f_ci[k]]; y[i] = s; }
}
static void f_csr_lanes4(const float *x, float *y) {                 /* SIMD/MAC 어레이처럼 부분합 4개 → 합산 트리 */
    for (uint32_t i = 0; i < FN; i++) {
        float p[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (uint32_t k = f_rp[i]; k < f_rp[i + 1u]; k++) p[(k - f_rp[i]) & 3u] += f_val[k] * x[f_ci[k]];
        y[i] = (p[0] + p[1]) + (p[2] + p[3]);
    }
}

/* ---------- 연습 4: a(i,j) 조회 — 행 안 이진 탐색 ---------- */
static int32_t csr_get(const csr_t *A, uint32_t i, uint32_t j) {
    uint32_t lo = A->row_ptr[i], hi = A->row_ptr[i + 1u];            /* [lo, hi) 에서 lower_bound */
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (A->col_idx[mid] < j) lo = mid + 1u; else hi = mid;
    }
    return (lo < A->row_ptr[i + 1u] && A->col_idx[lo] == j) ? A->val[lo] : 0;
}

/* ---------- 연습 5: nnz 균형 행 분할 — row_ptr 이진 탐색 ---------- */
/* 행 [0,R) 를 K 조각으로: 조각 p 의 시작 행 = row_ptr[r] >= p*nnz/K 인 최소 r */
static void split_rows_by_nnz(const uint32_t *rp, uint32_t R, uint32_t K, uint32_t *start /* K+1 칸 */) {
    uint64_t nnz = rp[R];
    for (uint32_t p = 0; p <= K; p++) {
        uint64_t target = nnz * p / K;
        uint32_t lo = 0, hi = R;
        while (lo < hi) { uint32_t mid = lo + (hi - lo) / 2u; if (rp[mid] < target) lo = mid + 1u; else hi = mid; }
        start[p] = p == K ? R : lo;
    }
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    csr_t A, B, T, U;
    csr_bind(&A, g_rp[0], g_ci[0], g_v[0], CAP);
    csr_bind(&B, g_rp[1], g_ci[1], g_v[1], CAP);
    csr_bind(&T, g_rp[2], g_ci[2], g_v[2], CAP);
    csr_bind(&U, g_rp[3], g_ci[3], g_v[3], CAP);

    /* 연습 1: radix 정규화 == 밀집 누적 참조, 그리고 최악 입력에서 이동 횟수 비교 */
    {
        bool ok = true;
        rng = 777u;
        for (int t = 0; t < 500 && ok; t++) {
            uint32_t R = 1u + xorshift32() % DIM, C = 1u + xorshift32() % DIM, n = xorshift32() % 600u;
            memset(g_D, 0, sizeof g_D);
            for (uint32_t k = 0; k < n; k++) {
                g_coo[k] = (coo_t){ xorshift32() % R, xorshift32() % C, (int32_t)(xorshift32() % 7u) - 3 };
                g_D[g_coo[k].row * C + g_coo[k].col] += g_coo[k].val;
            }
            coo_to_csr_radix(g_coo, n, R, C, &A);
            uint32_t nz = 0;
            for (uint32_t i = 0; i < R * C; i++) nz += g_D[i] != 0;
            ok = csr_check(&A) && A.nnz == nz;
            for (uint32_t i = 0; ok && i < R; i++)
                for (uint32_t k = A.row_ptr[i]; ok && k < A.row_ptr[i + 1u]; k++) ok = g_D[i * C + A.col_idx[k]] == A.val[k];
        }
        CHECK(ok);
        /* 최악: 한 행에 열 내림차순 4000개 (예: 역순으로 들어오는 로그) */
        const uint32_t n = 4000;
        for (uint32_t k = 0; k < n; k++) g_coo[k] = (coo_t){ 0, (DIM - 1u) - (k % DIM), 1 };
        g_moves = 0; coo_to_csr_isort(g_coo, n, 1, &B); uint32_t m_isort = g_moves;
        g_moves = 0; coo_to_csr_radix(g_coo, n, 1, DIM, &A); uint32_t m_radix = g_moves;
        printf("[1] one row, 4000 entries, cols descending: moves  isort=%u  radix=%u\n", m_isort, m_radix);
        CHECK(csr_check(&A) && A.nnz == DIM && A.val[0] == (int32_t)(n / DIM) && m_radix == 2u * n && m_isort > 100u * m_radix);
    }

    /* 연습 2: 시프트 트릭 전치 == work 버퍼 전치 (배열까지 동일) */
    {
        bool ok = true;
        rng = 4242u;
        for (int t = 0; t < 500 && ok; t++) {
            uint32_t R = 1u + xorshift32() % DIM, C = 1u + xorshift32() % DIM, n = xorshift32() % 600u;
            for (uint32_t k = 0; k < n; k++) g_coo[k] = (coo_t){ xorshift32() % R, xorshift32() % C, (int32_t)(xorshift32() % 9u) - 4 };
            coo_to_csr_radix(g_coo, n, R, C, &A);
            transpose_work(&A, &B);
            transpose_shift(&A, &T);
            transpose_shift(&T, &U);
            ok = csr_check(&T) && csr_same(&B, &T) && csr_same(&A, &U);
        }
        CHECK(ok);
    }

    /* 연습 3: float — 순차 CSR == 밀집 (정확히), 4-lane 은 오차 한계 안, Inf 입력에서 밀집만 NaN */
    {
        rng = 99u;
        uint32_t w = 0;
        for (uint32_t i = 0; i < FN; i++) {
            f_rp[i] = w;
            for (uint32_t j = 0; j < FN; j++) {
                bool keep = xorshift32() % 10u == 0;                   /* 밀도 10% */
                float v = keep ? ((float)(xorshift32() % 20001u) - 10000.0f) / 997.0f : 0.0f;
                if (v == 0.0f) keep = false;
                f_W[i * FN + j] = v;
                if (keep) { f_ci[w] = (uint16_t)j; f_val[w] = v; w++; }
            }
        }
        f_rp[FN] = w;
        for (uint32_t j = 0; j < FN; j++) f_x[j] = ((float)(xorshift32() % 20001u) - 10000.0f) / 1013.0f;
        float yd[FN], ys[FN], yl[FN];
        f_dense(f_x, yd); f_csr_seq(f_x, ys); f_csr_lanes4(f_x, yl);
        bool eq = true, within = true;
        uint32_t differ = 0;
        double worst = 0.0;                                            /* |차이| / (u * sum|a x|) 의 최대 */
        const double u = ldexp(1.0, -24);                              /* float 단위 반올림 2^-24 */
        for (uint32_t i = 0; i < FN; i++) {
            eq = eq && yd[i] == ys[i];
            uint32_t n = f_rp[i + 1u] - f_rp[i];
            double mag = 0.0;
            for (uint32_t k = f_rp[i]; k < f_rp[i + 1u]; k++) mag += fabs((double)f_val[k] * f_x[f_ci[k]]);
            double gamma = n * u / (1.0 - n * u), diff = fabs((double)ys[i] - (double)yl[i]);
            within = within && diff <= 2.0 * gamma * mag;
            differ += ys[i] != yl[i];
            if (mag > 0 && diff / (u * mag) > worst) worst = diff / (u * mag);
        }
        printf("[3] float 200x200 10%%: dense==csr_seq rows %s, lanes4 differs in %u/200 rows, worst |diff|/(u*sum|ax|) = %.2f\n",
               eq ? "all equal" : "NOT equal", differ, worst);
        CHECK(eq && within && differ > 0);
        uint32_t zc = 0;                                               /* x 에 Inf: 0*Inf = NaN */
        while (zc < FN && f_W[0 * FN + zc] != 0.0f) zc++;              /* 행 0 에서 0 인 열 찾기 */
        f_x[zc] = INFINITY;
        f_dense(f_x, yd); f_csr_seq(f_x, ys);
        CHECK(zc < FN && isnan(yd[0]) && !isnan(ys[0]));
    }

    /* 연습 4: csr_get == 밀집 (모든 칸) */
    {
        bool ok = true;
        rng = 2024u;
        for (int t = 0; t < 100 && ok; t++) {
            uint32_t R = 1u + xorshift32() % DIM, C = 1u + xorshift32() % DIM, n = xorshift32() % 600u;
            memset(g_D, 0, sizeof g_D);
            for (uint32_t k = 0; k < n; k++) {
                g_coo[k] = (coo_t){ xorshift32() % R, xorshift32() % C, (int32_t)(xorshift32() % 9u) - 4 };
                g_D[g_coo[k].row * C + g_coo[k].col] += g_coo[k].val;
            }
            coo_to_csr_radix(g_coo, n, R, C, &A);
            for (uint32_t i = 0; ok && i < R; i++)
                for (uint32_t j = 0; ok && j < C; j++) ok = csr_get(&A, i, j) == g_D[i * C + j];
        }
        CHECK(ok);
    }

    /* 연습 5: 멱법칙(power-law) 행 길이에서 균등 행 분할 vs nnz 균형 분할 (K = 4) */
    {
        const uint32_t R = DIM, K = 4;
        uint32_t rp[DIM + 1u], st[K + 1u];
        rp[0] = 0;
        for (uint32_t r = 0; r < R; r++) rp[r + 1u] = rp[r] + 1u + 2000u / ((r + 1u) * (r + 1u));  /* 앞 행이 매우 김 */
        uint32_t max_even = 0, max_bal = 0;
        for (uint32_t p = 0; p < K; p++) {
            uint32_t e = rp[(p + 1u) * R / K] - rp[p * R / K];
            if (e > max_even) max_even = e;
        }
        split_rows_by_nnz(rp, R, K, st);
        bool ok = st[0] == 0 && st[K] == R;
        for (uint32_t p = 0; p < K; p++) {
            ok = ok && st[p] <= st[p + 1u];
            uint32_t b = rp[st[p + 1u]] - rp[st[p]];
            if (b > max_bal) max_bal = b;
        }
        printf("[5] nnz=%u, K=4: max part nnz  even-rows=%u  nnz-balanced=%u  (ideal %u, longest row %u)\n",
               rp[R], max_even, max_bal, rp[R] / K, rp[1] - rp[0]);
        CHECK(ok && max_bal < max_even);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
