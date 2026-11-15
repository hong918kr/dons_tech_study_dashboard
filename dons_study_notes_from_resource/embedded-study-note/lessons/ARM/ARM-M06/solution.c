/* ARM-M06 NEON 기초: 벡터 덧셈·내적·리덕션 — 스칼라 C / intrinsics / 손 asm 3버전 + 테스트 하네스
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c solution.S shared_bugs.S -o sol && ./sol
 *       (starter: cc ... starter.c starter.S shared_bugs.S -o st && ./st)
 * 규칙: float 내적은 lane 재결합 때문에 비트가 스칼라와 달라도 된다 → 오차 한계 γ_n·Σ|a_i b_i| 로 비교.
 *       int8 내적은 정확해야 한다 → int64 참조와 비교. int32 오버플로가 나는 입력은 C 로 계산하지 않는다.
 */
#include <arm_neon.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DOT_S8_MAX_N 131071u   /* (2^31-1)/16384: 결과가 int32 에 들어가는 최대 n (최악 곱 (-128)(-128)) */

/* SDOT intrinsic(vdotq_s32) 은 FEAT_DotProd 가 켜진 함수에서만 쓸 수 있다 (리눅스 generic armv8-a 대비) */
#define DOTPROD __attribute__((target("+dotprod")))   /* "+" 는 gcc 필수, clang 도 허용 */

/* ---- 버전 3: 손 asm (starter.S / solution.S) ---- */
extern void    vadd_f32_asm(float *dst, const float *a, const float *b, size_t n);
extern float   dot_f32_asm(const float *a, const float *b, size_t n);
extern int32_t dot_s8_asm(const int8_t *a, const int8_t *b, size_t n);
extern int64_t dot_s8_long_asm(const int8_t *a, const int8_t *b, size_t n);
/* ---- shared_bugs.S: 일부러 틀린 버전 (테스트가 잡아야 한다) ---- */
extern void    bug_vadd_notail(float *dst, const float *a, const float *b, size_t n);
extern float   bug_dot_f32_halfreduce(const float *a, const float *b, size_t n);
extern int32_t bug_dot_s8_udot(const int8_t *a, const int8_t *b, size_t n);
extern int32_t bug_dot_s8_ldrb(const int8_t *a, const int8_t *b, size_t n);
extern int64_t bug_dot_s8_naive64(const int8_t *a, const int8_t *b, size_t n);

/* ---- 버전 1: 스칼라 C (기준선). 자동 벡터화를 끈다 — clang -S 로 확인 (lesson 5장) ---- */
#if defined(__clang__)
#define NOVEC _Pragma("clang loop vectorize(disable) interleave(disable)")
#define NOVEC_FN __attribute__((noinline))
#else
#define NOVEC
#define NOVEC_FN __attribute__((noinline, optimize("no-tree-vectorize")))
#endif

NOVEC_FN void vadd_f32_scalar(float *dst, const float *a, const float *b, size_t n) {
    NOVEC for (size_t i = 0; i < n; i++) dst[i] = a[i] + b[i];
}
NOVEC_FN float dot_f32_scalar(const float *a, const float *b, size_t n) {
    float s = 0.0f;
    NOVEC for (size_t i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}
NOVEC_FN int32_t dot_s8_scalar(const int8_t *a, const int8_t *b, size_t n) {   /* n <= DOT_S8_MAX_N */
    int32_t s = 0;
    NOVEC for (size_t i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}

/* ---- 버전 2: NEON intrinsics (arm_neon.h) ---- */
void vadd_f32_neon(float *dst, const float *a, const float *b, size_t n) {
    size_t i = 0;
    for (; i + 4 <= n; i += 4)
        vst1q_f32(dst + i, vaddq_f32(vld1q_f32(a + i), vld1q_f32(b + i)));
    for (; i < n; i++) dst[i] = a[i] + b[i];
}

float dot_f32_neon(const float *a, const float *b, size_t n) {
    float32x4_t acc = vdupq_n_f32(0.0f);
    size_t i = 0;
    for (; i + 4 <= n; i += 4)
        acc = vfmaq_f32(acc, vld1q_f32(a + i), vld1q_f32(b + i));
    float s = vaddvq_f32(acc);
    for (; i < n; i++) s = fmaf(a[i], b[i], s);
    return s;
}

DOTPROD int32_t dot_s8_neon(const int8_t *a, const int8_t *b, size_t n) {
    int32x4_t acc = vdupq_n_s32(0);
    size_t i = 0;
    for (; i + 16 <= n; i += 16)
        acc = vdotq_s32(acc, vld1q_s8(a + i), vld1q_s8(b + i));
    int32_t s = vaddvq_s32(acc);
    for (; i < n; i++) s += a[i] * b[i];
    return s;
}

/* ---- Part C: 펌웨어 커널 ---- */
/* int8 완전연결층 (키워드 인식 마지막 층): y[r] = bias[r] + W[r,:]·x, 가장 큰 y 의 행 번호를 돌려준다 (동점이면 앞 행).
 * W 는 row-major rows×cols. cols <= DOT_S8_MAX_N, |y[r]| 는 int32 범위라고 가정. rows == 0 이면 0. */
size_t fc_s8_argmax(const int8_t *W, const int8_t *x, const int32_t *bias, int32_t *y,
                    size_t rows, size_t cols) {
    size_t best = 0;
    for (size_t r = 0; r < rows; r++) {
        y[r] = bias[r] + dot_s8_asm(W + r * cols, x, cols);
        if (y[r] > y[best]) best = r;
    }
    return best;
}

/* 두 마이크 / 프리앰블 상관기: corr(L) = Σ_{i<n} x[i]·y[i+L], L = 0..max_lag 중 최대인 L (동점이면 작은 L).
 * y 는 n + max_lag 개. */
size_t best_lag_f32(const float *x, const float *y, size_t n, size_t max_lag) {
    size_t best = 0;
    float best_c = -INFINITY;
    for (size_t L = 0; L <= max_lag; L++) {
        float c = dot_f32_asm(x, y + L, n);
        if (c > best_c) { best_c = c; best = L; }
    }
    return best;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x2545F4914F6CDD1Dull;          /* 고정 시드 xorshift64 */
static uint64_t xs(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static float rnd_f(float lo, float hi) { return lo + (hi - lo) * (float)(xs() >> 40) * 0x1p-24f; }
static int8_t rnd_s8(void) { return (int8_t)(uint8_t)(xs() >> 56); }   /* -128..127 전 범위 */
static uint32_t f_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static int same_f(float a, float b) { return (isnan(a) && isnan(b)) || f_bits(a) == f_bits(b); }

typedef void    (*vadd_fn)(float *, const float *, const float *, size_t);
typedef float   (*dotf_fn)(const float *, const float *, size_t);
typedef int32_t (*dots8_fn)(const int8_t *, const int8_t *, size_t);

enum { MAXN = 67, PAD = 8 };
#define CANARY 12345.678f

/* n = 0..67 × 시작 오프셋 0..3 (16바이트 정렬 깨기) × in-place. dst 앞뒤 canary 도 확인. 실패 수를 돌려준다 */
static int suite_vadd(vadd_fn f) {
    static const float special[] = { INFINITY, -INFINITY, NAN, -0.0f, 0x1p-149f, 3e38f, -3e38f, 1.0f };
    float a[MAXN + PAD], b[MAXN + PAD], d[MAXN + 2 * PAD], ref[MAXN + PAD];
    int fails = 0;
    for (int inplace = 0; inplace <= 1; inplace++)
    for (size_t off = 0; off < 4; off++)
    for (size_t n = 0; n <= MAXN; n++) {
        for (size_t i = 0; i < MAXN + PAD; i++) { a[i] = rnd_f(-4, 4); b[i] = rnd_f(-4, 4); }
        a[xs() % (MAXN + PAD)] = special[xs() % 8];            /* 특수값 하나씩 섞기 */
        b[xs() % (MAXN + PAD)] = special[xs() % 8];
        for (size_t i = 0; i < n; i++) ref[i] = a[off + i] + b[off + i];   /* 원소별 + 는 순서와 무관하게 비트까지 같다 */
        for (size_t i = 0; i < MAXN + 2 * PAD; i++) d[i] = CANARY;
        float *dst = inplace ? a + off : d + PAD + off;
        f(dst, a + off, b + off, n);
        for (size_t i = 0; i < n; i++) fails += !same_f(dst[i], ref[i]);
        if (!inplace) for (size_t i = 0; i < MAXN + 2 * PAD; i++)
            if (i < PAD + off || i >= PAD + off + n) fails += d[i] != CANARY;
    }
    return fails;
}

/* 오차 한계: 어떤 순서로 더하든 |ŝ − s| <= γ_n Σ|a_i b_i|, γ_n = n·u/(1 − n·u), u = 2^-24 (Higham 식 3.5).
 * 참조 s 는 double (float 곱은 double 에서 정확, 합의 오차 γ_n(2^-53) 도 한계에 더한다). */
static double g_max_ratio, g_max_ratio16;   /* 관찰된 최대 |err| / bound (전체, n >= 16) */
static int dot_f32_ok(float got, const float *a, const float *b, size_t n) {
    double s = 0, sa = 0;
    for (size_t i = 0; i < n; i++) { double p = (double)a[i] * b[i]; s += p; sa += fabs(p); }
    double uf = 0x1p-24, ud = 0x1p-53, k = (double)n;
    double bound = (k * uf / (1 - k * uf) + k * ud / (1 - k * ud)) * sa;
    double err = fabs((double)got - s);
    if (bound > 0 && err / bound > g_max_ratio) g_max_ratio = err / bound;
    if (bound > 0 && n >= 16 && err / bound > g_max_ratio16) g_max_ratio16 = err / bound;
    return isfinite(got) && err <= bound;
}
static int suite_dot_f32(dotf_fn f) {
    float a[MAXN + PAD], b[MAXN + PAD];
    int fails = 0;
    for (size_t off = 0; off < 4; off++)
    for (size_t n = 0; n <= MAXN; n++) {
        for (int t = 0; t < 4; t++) {                            /* 무작위 [-1,1] */
            for (size_t i = 0; i < n + off; i++) { a[i] = rnd_f(-1, 1); b[i] = rnd_f(-1, 1); }
            fails += !dot_f32_ok(f(a + off, b + off, n), a + off, b + off, n);
        }
        int64_t exact = 0;                                       /* 작은 정수: 어떤 순서든 정확 → 비트 일치 */
        for (size_t i = 0; i < n + off; i++) { a[i] = (float)((int)(xs() % 17) - 8); b[i] = (float)((int)(xs() % 17) - 8); }
        for (size_t i = 0; i < n; i++) exact += (int64_t)a[off + i] * (int64_t)b[off + i];
        fails += !same_f(f(a + off, b + off, n), (float)exact) && !(exact == 0 && f(a + off, b + off, n) == 0.0f);
    }
    return fails;
}

/* int8: n = 0..67(+경계 근처) × 오프셋 0..15, 전 범위 바이트 + 전부 -128 입력. int64 참조와 정확히 일치해야 한다 */
static int suite_dot_s8_range(dots8_fn f, int lo, int hi) {
    int8_t a[160 + 16], b[160 + 16];
    static const size_t ns[] = { 0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 32, 33, 47, 48, 49, 63, 64, 65, 66, 67, 127, 128, 159, 160 };
    int fails = 0;
    for (size_t off = 0; off < 16; off++)
    for (size_t k = 0; k < sizeof ns / sizeof ns[0]; k++) {
        size_t n = ns[k];
        for (int t = 0; t < 3; t++) {
            for (size_t i = 0; i < n + off; i++) {
                if (t == 2) a[i] = b[i] = (int8_t)lo;                                  /* 같은 값끼리: lo=-128 이면 최악 곱 */
                else { a[i] = (int8_t)(lo + (int)(xs() % (uint64_t)(hi - lo + 1))); b[i] = (int8_t)(lo + (int)(xs() % (uint64_t)(hi - lo + 1))); }
            }
            int64_t ref = 0;
            for (size_t i = 0; i < n; i++) ref += (int64_t)a[off + i] * b[off + i];
            fails += (int64_t)f(a + off, b + off, n) != ref;
        }
    }
    return fails;
}
static int suite_dot_s8(dots8_fn f) { return suite_dot_s8_range(f, -128, 127); }

static int64_t ref_dot_s8(const int8_t *a, const int8_t *b, size_t n) {
    int64_t s = 0;
    for (size_t i = 0; i < n; i++) s += (int64_t)a[i] * b[i];
    return s;
}

int main(void) {
    /* ---------- Part A: 세 버전이 같은 계약을 지키나 ---------- */
    CHECK(suite_vadd(vadd_f32_scalar) == 0);                   /* 기준선 자체 확인 */
    CHECK(suite_vadd(vadd_f32_neon) == 0);
    CHECK(suite_vadd(vadd_f32_asm) == 0);
    {
        float a[6] = { 1, 2, 3, 4, 5, 0x1p-149f }, b[6] = { 10, 20, 30, 40, -0.0f, 0x1p-149f }, d[6] = { 0 };
        vadd_f32_asm(d, a, b, 6);
        CHECK(d[0] == 11 && d[3] == 44 && d[4] == 5);
        CHECK(d[5] == 0x1p-148f);                               /* 비정규수도 그대로 (FPCR.FZ = 0) */
        vadd_f32_neon(d, a, b, 6);
        CHECK(d[1] == 22 && d[5] == 0x1p-148f);
    }
    CHECK(suite_dot_f32(dot_f32_scalar) == 0);
    CHECK(suite_dot_f32(dot_f32_neon) == 0);
    CHECK(suite_dot_f32(dot_f32_asm) == 0);
    {
        float a[9] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 }, b[9] = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };
        CHECK(dot_f32_asm(a, b, 9) == 45.0f && dot_f32_neon(a, b, 9) == 45.0f);
        CHECK(dot_f32_asm(a, a, 8) == 204.0f && dot_f32_neon(a, a, 8) == 204.0f);
        CHECK(dot_f32_asm(a, b, 0) == 0.0f && dot_f32_neon(a, b, 0) == 0.0f);
    }
    CHECK(suite_dot_s8(dot_s8_scalar) == 0);
    CHECK(suite_dot_s8(dot_s8_neon) == 0);
    CHECK(suite_dot_s8(dot_s8_asm) == 0);
    {
        int8_t a[16], b[16];
        for (int i = 0; i < 16; i++) { a[i] = -128; b[i] = -128; }
        CHECK(dot_s8_asm(a, b, 16) == 262144 && dot_s8_neon(a, b, 16) == 262144);
        for (int i = 0; i < 16; i++) { a[i] = (int8_t)(i - 8); b[i] = 3; }
        CHECK(dot_s8_asm(a, b, 16) == -24 && dot_s8_asm(a, b, 13) == -3 * (8 + 7 + 6 + 5 + 4 + 3 + 2 + 1) + 3 * (1 + 2 + 3 + 4));
    }
    {   /* 계약의 끝 n = 131071: 결과가 int32 의 끝에 닿는다 */
        size_t n = DOT_S8_MAX_N;
        int8_t *a = malloc(n), *b = malloc(n);
        memset(a, 0x80, n); memset(b, 0x80, n);                 /* 전부 -128 */
        CHECK(dot_s8_asm(a, b, n) == 2147467264 && dot_s8_neon(a, b, n) == 2147467264);
        memset(b, 0x7f, n);                                     /* -128 × 127 */
        CHECK(dot_s8_asm(a, b, n) == -2130690176 && dot_s8_neon(a, b, n) == -2130690176);
        free(a); free(b);
    }

    /* ---------- Part B: 테스트가 버그를 잡나 + 오버플로 지평선 ---------- */
    CHECK(suite_vadd(bug_vadd_notail) > 0);                     /* 꼬리 빠뜨림 */
    double keep = g_max_ratio, keep16 = g_max_ratio16;          /* 버그 버전의 오차는 통계에서 뺀다 */
    CHECK(suite_dot_f32(bug_dot_f32_halfreduce) > 0);           /* 리덕션 반쪽 */
    g_max_ratio = keep; g_max_ratio16 = keep16;
    CHECK(suite_dot_s8(bug_dot_s8_udot) > 0);                   /* UDOT */
    CHECK(suite_dot_s8_range(bug_dot_s8_udot, 0, 127) == 0);    /* …0..127 만 넣는 약한 테스트는 UDOT 을 못 잡는다 */
    CHECK(suite_dot_s8(bug_dot_s8_ldrb) > 0);                   /* 꼬리 ldrb */
    {
        size_t big = 3 * 524272 + 37;
        int8_t *a = malloc(big), *b = malloc(big);
        memset(a, 0x80, big); memset(b, 0x80, big);
        CHECK(dot_s8_long_asm(a, b, 524287) == 8589918208LL);   /* 32767 회 SDOT + 꼬리 15 */
        CHECK(bug_dot_s8_naive64(a, b, 524287) == 8589918208LL);/* 지평선 바로 앞: 버그 버전도 맞다 */
        CHECK(dot_s8_long_asm(a, b, 524288) == 8589934592LL);   /* 2^33 */
        CHECK(bug_dot_s8_naive64(a, b, 524288) == -8589934592LL);/* lane 4개가 각각 2^31 → -2^31 로 감쌈 (ISA 정의) */
        CHECK(dot_s8_long_asm(a, b, big) == (int64_t)big * 16384);
        for (size_t i = 0; i < big; i++) { a[i] = rnd_s8(); b[i] = rnd_s8(); }
        CHECK(dot_s8_long_asm(a, b, big) == ref_dot_s8(a, b, big));
        int bad = 0;
        for (size_t n = 0; n <= 200; n++) bad += dot_s8_long_asm(a + n % 7, b, n) != ref_dot_s8(a + n % 7, b, n);
        CHECK(bad == 0);
        free(a); free(b);
    }
    {   /* 재결합: 무작위 입력에서 asm 결과 비트가 스칼라와 얼마나 다른가 (정보) */
        float a[64], b[64];
        int diff = 0, diffn = 0;
        for (int t = 0; t < 1000; t++) {
            for (int i = 0; i < 64; i++) { a[i] = rnd_f(-1, 1); b[i] = rnd_f(-1, 1); }
            diff += f_bits(dot_f32_asm(a, b, 64)) != f_bits(dot_f32_scalar(a, b, 64));
            diffn += f_bits(dot_f32_asm(a, b, 64)) != f_bits(dot_f32_neon(a, b, 64));
        }
        printf("info: dot_f32 n=64: asm != scalar bits in %d/1000, asm != neon in %d/1000; max |err|/bound = %.4f (n>=16: %.4f)\n",
               diff, diffn, g_max_ratio, g_max_ratio16);
        CHECK(g_max_ratio <= 1.0);
    }

    /* ---------- Part C: 펌웨어 커널 ---------- */
    {
        enum { ROWS = 12, COLS = 250 };                         /* 12 키워드 × (25 프레임 × 10 MFCC) */
        static int8_t W[ROWS * COLS], x[COLS];
        int32_t bias[ROWS], y[ROWS], yref[ROWS];
        for (int trial = 0; trial < 20; trial++) {
            for (int i = 0; i < ROWS * COLS; i++) W[i] = rnd_s8();
            for (int i = 0; i < COLS; i++) x[i] = rnd_s8();
            for (int r = 0; r < ROWS; r++) bias[r] = (int32_t)(xs() % 20001) - 10000;
            int plant = trial % ROWS;                           /* 이 행을 x 와 같게 → 확실한 1등 */
            if (trial & 1) memcpy(W + plant * COLS, x, COLS);
            size_t best = 0;
            for (int r = 0; r < ROWS; r++) {
                yref[r] = bias[r] + (int32_t)ref_dot_s8(W + r * COLS, x, COLS);
                if (yref[r] > yref[best]) best = (size_t)r;
            }
            size_t got = fc_s8_argmax(W, x, bias, y, ROWS, COLS);
            CHECK(got == best && memcmp(y, yref, sizeof y) == 0 && (!(trial & 1) || got == (size_t)plant));
        }
        int32_t y1[1] = { 0 };
        CHECK(fc_s8_argmax(W, x, bias, y1, 1, 0) == 0 && y1[0] == bias[0]);   /* cols = 0 → y = bias */
    }
    {
        enum { N = 250, MAXLAG = 40 };
        static float xm[N], ym[N + MAXLAG];
        static const size_t delays[] = { 17, 0, 40, 3, 29 };
        for (size_t k = 0; k < 5; k++) {
            size_t D = delays[k];
            for (int i = 0; i < N; i++) xm[i] = (xs() & 1) ? 1.0f : -1.0f;      /* ±1 의사잡음 (프리앰블) */
            for (int i = 0; i < N + MAXLAG; i++) ym[i] = rnd_f(-0.3f, 0.3f);    /* 잡음 */
            for (int i = 0; i < N; i++) ym[i + D] += 0.8f * xm[i];               /* 마이크 2 = 지연 + 감쇠 */
            CHECK(best_lag_f32(xm, ym, N, MAXLAG) == D);
        }
        CHECK(best_lag_f32(xm, ym, 0, MAXLAG) == 0);           /* n = 0: 모두 0 → 첫 lag */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
