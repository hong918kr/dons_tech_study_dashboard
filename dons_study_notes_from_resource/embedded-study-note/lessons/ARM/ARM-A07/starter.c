/* ARM-A07 포화·고정소수점: Q15 FIR — C 기준 구현 / NEON intrinsics / 손 asm (+ Cortex-M4 판은 run_qemu.sh m4)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c solution.S shared_bugs.S -o /tmp/ARM-A07_solution && /tmp/ARM-A07_solution
 *       (starter: starter.c starter.S shared_bugs.S)
 *
 * 이 레슨의 "명세" — 비트 정확(bit-exact) 파이프라인 하나 (ETSI/ITU 음성 코덱 기본 연산과 같은 정의):
 *   L_mult(a,b)    = sat32(2·a·b)                  a,b 는 Q15. -32768·-32768 하나만 포화한다
 *   L_mac(acc,a,b) = sat32(acc + L_mult(a,b))      포화가 두 번 (곱 → 합). 탭 k = 0,1,…,T-1 순서로
 *   round(L)       = L_add(L, 0x8000) >> 16        Q31 → Q15, 반올림은 +∞ 쪽 (0.5 → 1, -0.5 → 0)
 *   y[i] = round( L_mac 누적 (acc=0) over k of (h[k], x[i+T-1-k]) )   x 는 n+T-1 개 (앞 T-1 개 = 과거 샘플)
 * 포화 덧셈은 결합법칙이 없다 → 누적 "순서"까지 명세다. 모든 구현이 이 정의와 비트 단위로 같아야 한다.
 * 테스트의 정답(o_*)은 int64 로 넓게 계산한 오라클이다 — TODO 인 C 기준 구현과는 따로 있다.
 */
#include <arm_neon.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FIR_MAXT    64      /* 탭 수 상한 */
#define CHAIN_BLOCK 64      /* Part C: 한 번에 처리하는 최대 블록 */

/* ======================= Part A-1 — C 기준 구현 (32비트 연산만, UB 없이) ======================= */
int32_t q31_add_sat(int32_t a, int32_t b) {            /* L_add  = SQADD .s / QADD */
    /* TODO: a+b 가 넘치면 INT32_MAX/INT32_MIN 으로. a+b 를 먼저 계산하면 UB — 넘치기 전에 판단 */
    (void)a; (void)b;
    return 0;
}
int32_t q15_lmult(int16_t a, int16_t b) {              /* L_mult = SQDMULL .s / SMULBB+QADD(p,p) */
    /* TODO: p = a·b (int 에 들어간다), 2p 가 넘치는 유일한 경우만 INT32_MAX */
    (void)a; (void)b;
    return 0;
}
int32_t q31_mac(int32_t acc, int16_t a, int16_t b) {   /* L_mac  = SQDMLAL / SMULBB+QDADD */
    (void)acc; (void)a; (void)b; return 0;      /* TODO: 포화 곱 → 포화 합 */
}
int16_t q31_round(int32_t L) {                         /* round  = SQRSHRN #16 (같음을 테스트로 보인다) */
    (void)L; return 0;                                  /* TODO: 0x8000 을 포화 덧셈한 뒤 상위 16비트 */
}
int16_t q15_mul(int16_t a, int16_t b) {                /* SQDMULH: 상위 절반, 버림(-∞ 쪽) */
    (void)a; (void)b; return 0;                 /* TODO: L_mult 의 상위 16비트 */
}
int16_t q15_mulr(int16_t a, int16_t b) {               /* SQRDMULH: 반올림 */
    (void)a; (void)b; return 0;                 /* TODO: L_mult 를 round */
}
int16_t q15_add_sat(int16_t a, int16_t b) {            /* SQADD .h / QADD16 */
    /* TODO: int 로 더한 뒤 [-32768, 32767] 로 자르기 */
    (void)a; (void)b; return 0;
}
void fir_q15_ref(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y) {
    /* TODO: 출력 i 마다 acc = 0 → k = 0..T-1 순서로 q31_mac(acc, h[k], x[i+T-1-k]) → q31_round */
    for (size_t i = 0; i < n; i++) y[i] = 0;
    (void)x; (void)h; (void)T;
}

/* ======================= Part A-2 — NEON intrinsics ======================= */
void q15_mul_neon(int16_t *y, const int16_t *a, const int16_t *b, size_t n) {     /* y = SQDMULH(a,b) */
    /* TODO: 8개씩 vld1q_s16 → vqdmulhq_s16 → vst1q_s16, 꼬리는 vqdmulhh_s16 */
    (void)y; (void)a; (void)b; (void)n;
}
void q15_add_neon(int16_t *y, const int16_t *a, const int16_t *b, size_t n) {     /* y = SQADD(a,b) */
    /* TODO: 8개씩 vqaddq_s16, 꼬리는 vqaddh_s16 */
    (void)y; (void)a; (void)b; (void)n;
}
/* 출력 방향으로 8개씩 벡터화 (lane j = 출력 i+j). 탭 방향은 순서대로 → 명세의 누적 순서 그대로 */
void fir_q15_neon(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y) {
    /* TODO: 8개 출력씩: int32x4_t lo, hi = 0 → k 마다 xv = vld1q_s16(x+i+T-1-k),
     *       lo = vqdmlal_n_s16(lo, vget_low_s16(xv), h[k]), hi = vqdmlal_high_n_s16(hi, xv, h[k])
     *       → vqaddq_s32(·, 0x8000) → vshrn_n_s32(·, 16) → vcombine → vst1q_s16
     *       꼬리: vqdmlalh_s16 누적 → vqrshrns_n_s32(acc, 16) */
    (void)x; (void)n; (void)h; (void)T; (void)y;
}

/* ======================= Part A-3 — 손 asm (solution.S) ======================= */
void q15_scale_asm(int16_t *y, const int16_t *x, size_t n, int16_t g);            /* SQRDMULH by scalar */
void fir_q15_asm(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y);
/* ---- shared_bugs.S: 일부러 틀린 버전 (Part B — 테스트가 잡아야 한다, n 은 8의 배수만) ---- */
void bug_scale_sqdmulh(int16_t *y, const int16_t *x, size_t n, int16_t g);        /* 반올림 없는 SQDMULH */
void bug_fir_trunc(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y);   /* SQSHRN (버림) */
void bug_fir_addwrap(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y); /* SMULL+ADD (감쌈) */

/* ======================= Part C — 센서 체인: FIR → 이득 → 오프셋, 블록 단위 스트리밍 ======================= */
typedef struct {
    const int16_t *h;
    size_t T;
    int16_t gain;                                   /* Q15, SQRDMULH */
    int16_t offset;                                 /* Q15, SQADD */
    int16_t buf[FIR_MAXT - 1 + CHAIN_BLOCK];        /* [과거 T-1 개 | 새 블록 n 개] */
} chain_t;
static int16_t g_offs[CHAIN_BLOCK];                 /* q15_add_neon 용: offset 을 n 개 복제 */
void chain_init(chain_t *c, const int16_t *h, size_t T, int16_t gain, int16_t offset) {   /* 주어짐 */
    memset(c, 0, sizeof *c);                        /* 지연선 = 0 (켜진 직후의 무음) */
    c->h = h; c->T = T; c->gain = gain; c->offset = offset;
}
void chain_process(chain_t *c, const int16_t *in, int16_t *out, size_t n) {   /* n <= CHAIN_BLOCK */
    /* TODO: buf = [과거 T-1 | in] → fir_q15_asm(buf, n, …) → q15_scale_asm(gain) → q15_add_neon(offset)
     *       → 마지막 T-1 개를 buf 앞으로 (겹침 → memmove) */
    (void)c; (void)in; (void)out; (void)n; (void)g_offs;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x2545F4914F6CDD1Dull;          /* 고정 시드 xorshift64 */
static uint64_t xs(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static int16_t r16(void) { return (int16_t)(uint16_t)xs(); }

/* ---- 명세 오라클: int64 로 넓게 계산 후 포화 (TODO 와 독립) ---- */
static int64_t fdiv(int64_t v, int64_t d) { return v >= 0 ? v / d : -((-v + d - 1) / d); }   /* floor(v/d), d>0 */
static int32_t o_sat32(int64_t v) { return (int32_t)(v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v); }
static int16_t o_sat16(int64_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
static int32_t o_add32(int32_t a, int32_t b) { return o_sat32((int64_t)a + b); }
static int32_t o_lmult(int16_t a, int16_t b) { return o_sat32(2 * (int64_t)a * b); }
static int32_t o_mac(int32_t acc, int16_t a, int16_t b) { return o_sat32((int64_t)acc + o_lmult(a, b)); }
static int16_t o_round(int32_t L) { return o_sat16(fdiv((int64_t)L + 0x8000, 65536)); }  /* = SQRSHRN #16 (넓게) */
static int16_t o_mul(int16_t a, int16_t b) { return o_sat16(fdiv(2 * (int64_t)a * b, 65536)); }
static int16_t o_mulr(int16_t a, int16_t b) { return o_sat16(fdiv(2 * (int64_t)a * b + 0x8000, 65536)); }
static int16_t o_add16(int16_t a, int16_t b) { return o_sat16((int64_t)a + b); }
static void o_fir(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y) {
    for (size_t i = 0; i < n; i++) {
        int32_t acc = 0;
        for (size_t k = 0; k < T; k++) acc = o_mac(acc, h[k], x[i + T - 1 - k]);
        y[i] = o_round(acc);
    }
}
static void o_fir_exact(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y) {   /* 포화 없이 */
    for (size_t i = 0; i < n; i++) {
        int64_t s = 0;
        for (size_t k = 0; k < T; k++) s += (int64_t)h[k] * x[i + T - 1 - k];
        y[i] = o_sat16(fdiv(2 * s + 0x8000, 65536));
    }
}
/* (버그 4) 탭 방향 분할: 짝수/홀수 탭을 누산기 두 개에 따로 모은 뒤 SQADD 로 합친다.
 * "탭 방향으로 벡터화"한 SIMD 코드가 계산하는 값을 스칼라로 흉내 낸 것. 포화가 없으면 정답과 같다. */
static void bug_fir_tapsplit(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y) {
    for (size_t i = 0; i < n; i++) {
        int32_t e = 0, o = 0;
        for (size_t k = 0; k < T; k++) {
            if (k & 1) o = o_mac(o, h[k], x[i + T - 1 - k]);
            else       e = o_mac(e, h[k], x[i + T - 1 - k]);
        }
        y[i] = o_round(o_add32(e, o));
    }
}

static const int16_t E16[] = { -32768, -32767, -16385, -16384, -23170, -181, -2, -1,
                               0, 1, 2, 181, 16383, 16384, 23170, 32766, 32767 };
#define NE16 (sizeof E16 / sizeof E16[0])
static const int32_t E32[] = { INT32_MIN, INT32_MIN + 1, -0x7FFF8000, -0x40000000, -0x18000, -0x8001, -0x8000,
                               -1, 0, 1, 0x7FFF, 0x8000, 0x18000, 0x3FFFFFFF, 0x40000000,
                               0x7FFF7FFF, 0x7FFF8000, INT32_MAX - 1, INT32_MAX };
#define NE32 (sizeof E32 / sizeof E32[0])
static int32_t r32_edgy(void) {                         /* 절반은 경계 근처 */
    uint64_t r = xs();
    if (r & 1) return o_sat32((int64_t)E32[(r >> 8) % NE32] + ((int64_t)((r >> 32) % 65537) - 32768) / 2);
    return (int32_t)(uint32_t)(r >> 16);
}

/* ---- Part A-1 스위트 ---- */
typedef int16_t (*bin16_fn)(int16_t, int16_t);
static int suite_add32(void) {
    int fails = 0;
    for (size_t i = 0; i < NE32; i++)
        for (size_t j = 0; j < NE32; j++) fails += q31_add_sat(E32[i], E32[j]) != o_add32(E32[i], E32[j]);
    for (int t = 0; t < (1 << 20); t++) { int32_t a = r32_edgy(), b = r32_edgy(); fails += q31_add_sat(a, b) != o_add32(a, b); }
    return fails;
}
static int suite_bin16(bin16_fn f, bin16_fn o) {        /* 모든 a × 경계 b (양쪽 순서) + 2^20 무작위 */
    int fails = 0;
    for (int32_t a = -32768; a <= 32767; a++)
        for (size_t j = 0; j < NE16; j++)
            fails += (f((int16_t)a, E16[j]) != o((int16_t)a, E16[j])) + (f(E16[j], (int16_t)a) != o(E16[j], (int16_t)a));
    for (int t = 0; t < (1 << 20); t++) { int16_t a = r16(), b = r16(); fails += f(a, b) != o(a, b); }
    return fails;
}
static int16_t w_lmult_hi(int16_t a, int16_t b) { return (int16_t)(q15_lmult(a, b) >> 16); }      /* 래퍼: L_mult 를 */
static int16_t w_lmult_lo(int16_t a, int16_t b) { return (int16_t)(uint16_t)q15_lmult(a, b); }    /* 16비트 둘로 비교 */
static int16_t o_lmult_hi(int16_t a, int16_t b) { return (int16_t)(o_lmult(a, b) >> 16); }
static int16_t o_lmult_lo(int16_t a, int16_t b) { return (int16_t)(uint16_t)o_lmult(a, b); }
static int suite_mac_round(void) {
    int fails = 0;
    for (size_t i = 0; i < NE32; i++)
        for (size_t j = 0; j < NE16; j++)
            for (size_t k = 0; k < NE16; k++) fails += q31_mac(E32[i], E16[j], E16[k]) != o_mac(E32[i], E16[j], E16[k]);
    for (size_t i = 0; i < NE32; i++) fails += q31_round(E32[i]) != o_round(E32[i]);
    for (int t = 0; t < (1 << 20); t++) {
        int32_t L = r32_edgy(); int16_t a = r16(), b = r16();
        fails += (q31_round(L) != o_round(L)) + (q31_mac(L, a, b) != o_mac(L, a, b));
    }
    return fails;
}

/* ---- Part A-2/3 스위트: 배열 연산 (모든 a × 경계 b, 길이 0..40 × 시작 오프셋, canary) ---- */
typedef void (*vec_fn)(int16_t *, const int16_t *, const int16_t *, size_t);
typedef void (*scale_fn)(int16_t *, const int16_t *, size_t, int16_t);
static int16_t A_all[65536], B_buf[65536], Y_buf[65536 + 16];
#define CANARY ((int16_t)0x5A5A)
static int suite_vec(vec_fn f, bin16_fn o) {
    int fails = 0;
    for (int32_t a = -32768; a <= 32767; a++) A_all[a + 32768] = (int16_t)a;
    for (size_t j = 0; j < NE16; j++) {
        for (size_t i = 0; i < 65536; i++) B_buf[i] = E16[j];
        f(Y_buf, A_all, B_buf, 65536);
        for (size_t i = 0; i < 65536; i++) fails += Y_buf[i] != o(A_all[i], E16[j]);
    }
    for (size_t off = 0; off < 8; off++)
        for (size_t n = 0; n <= 40; n++) {
            int16_t a[48], b[48], y[64];
            for (size_t i = 0; i < 48; i++) { a[i] = r16(); b[i] = (xs() & 3) ? r16() : E16[xs() % NE16]; }
            for (size_t i = 0; i < 64; i++) y[i] = CANARY;
            f(y + off + 1, a + off, b + off, n);
            for (size_t i = 0; i < n; i++) fails += y[off + 1 + i] != o(a[off + i], b[off + i]);
            fails += y[off] != CANARY || y[off + 1 + n] != CANARY;
        }
    return fails;
}
static int suite_scale(scale_fn f, int only_x8) {       /* y = SQRDMULH(x, g): 모든 x × 경계 g + 무작위 g */
    int fails = 0;
    for (int32_t a = -32768; a <= 32767; a++) A_all[a + 32768] = (int16_t)a;
    for (size_t j = 0; j < NE16 + 8; j++) {
        int16_t g = j < NE16 ? E16[j] : r16();
        Y_buf[65536] = CANARY;
        f(Y_buf, A_all, 65536, g);
        for (size_t i = 0; i < 65536; i++) fails += Y_buf[i] != o_mulr(A_all[i], g);
        fails += Y_buf[65536] != CANARY;
    }
    for (size_t n = 0; n <= 40; n++) {
        if (only_x8 && n % 8) continue;
        int16_t x[48], y[48]; int16_t g = r16();
        for (size_t i = 0; i < 48; i++) { x[i] = r16(); y[i] = CANARY; }
        f(y, x, n, g);
        for (size_t i = 0; i < n; i++) fails += y[i] != o_mulr(x[i], g);
        fails += y[n] != CANARY;
    }
    return fails;
}

/* ---- FIR 스위트 ---- */
typedef void (*fir_fn)(const int16_t *, size_t, const int16_t *, size_t, int16_t *);
enum { FILT_SAFE, FILT_HOT, FILT_MIN, FILT_ALT, NFILT };          /* 필터 종류 */
enum { SIG_RAND, SIG_SQUARE, SIG_MATCHED, SIG_MIN, NSIG };        /* 신호 종류 */
static uint32_t l1_norm(const int16_t *h, size_t T) {             /* Σ|h_k| (Q15) */
    uint32_t s = 0;
    for (size_t k = 0; k < T; k++) s += (uint32_t)(h[k] < 0 ? -(int32_t)h[k] : h[k]);
    return s;
}
static void make_filter(int16_t *h, size_t T, int kind) {
    for (size_t k = 0; k < T; k++) {
        if (kind == FILT_SAFE) {                                   /* Σ|h| <= 32767: 포화가 절대 안 난다 */
            int32_t m = (int32_t)(32767 / T);
            h[k] = (int16_t)((int32_t)(xs() % (uint64_t)(2 * m + 1)) - m);
        } else if (kind == FILT_HOT) h[k] = r16();                 /* 전 범위 무작위: 포화가 잦다 */
        else if (kind == FILT_MIN) h[k] = -32768;
        else h[k] = (k & 1) ? -32768 : 32767;                      /* 부호 교대, 최대 크기 */
    }
}
static void make_signal(int16_t *x, size_t len, const int16_t *h, size_t T, int kind) {
    size_t period = 2 + xs() % 19;
    for (size_t j = 0; j < len; j++) {
        if (kind == SIG_RAND) x[j] = r16();
        else if (kind == SIG_SQUARE) x[j] = ((j / (period / 2 + 1)) & 1) ? -32768 : 32767;   /* 전 스케일 구형파 */
        else if (kind == SIG_MATCHED) {                            /* 출력마다 최악이 되게: x[i+T-1-k] = sign(h[k]) */
            size_t k = (T - 1) - (j % T);
            x[j] = h[k] >= 0 ? 32767 : -32768;
        } else x[j] = -32768;
    }
}
static const size_t TS[] = { 1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 31, 32, 33, 64 };
static const size_t NS[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 23, 24, 25, 31, 32, 33, 64, 100 };
#define NTS (sizeof TS / sizeof TS[0])
#define NNS (sizeof NS / sizeof NS[0])
/* strong = 0: 안전한 필터 + 무작위 신호만 ("약한 테스트"), 1: 필터 4종 × 신호 4종 */
static int suite_fir(fir_fn f, int strong, int only_x8) {
    static int16_t x[100 + FIR_MAXT], y[100 + 2], want[100];
    int16_t h[FIR_MAXT];
    int fails = 0;
    for (size_t ti = 0; ti < NTS; ti++)
    for (size_t ni = 0; ni < NNS; ni++) {
        size_t T = TS[ti], n = NS[ni];
        if (only_x8 && n % 8) continue;
        for (int fk = 0; fk < (strong ? NFILT : 1); fk++)
        for (int sk = 0; sk < (strong ? NSIG : 1); sk++) {
            make_filter(h, T, fk);
            make_signal(x, n + T - 1, h, T, sk);
            for (size_t i = 0; i < n + 2; i++) y[i] = CANARY;
            o_fir(x, n, h, T, want);
            f(x, n, h, T, y);
            for (size_t i = 0; i < n; i++) fails += y[i] != want[i];
            fails += y[n] != CANARY || y[n + 1] != CANARY;
        }
    }
    return fails;
}
/* 한 출력에서 L_mac 의 "두 번 포화"가 드러나는 손 계산 예 (5장 표) */
static const int16_t H_TWO_SAT[4] = { 32767, -32768, -32768, 16384 };
static const int16_t X_TWO_SAT[4] = { 3, -32768, 32767, -32768 };   /* x[T-1-k]: k=0 → x[3], … */

/* ---- 교차 ISA 골든: 고정 벡터 24세트 × 40 출력 → FNV-1a 해시. 맥(NEON/asm)과 Cortex-M4(run_qemu.sh m4,
 *      m4_harness.c 에 같은 코드)가 같은 값을 내면 두 ISA 가 비트 단위로 같은 필터를 돌린 것이다 ---- */
#define GOLDEN_FNV 0xc93a6433u
static uint32_t golden_hash(fir_fn f) {
    static int16_t gx[40 + FIR_MAXT], gh[FIR_MAXT], gy[40];
    uint32_t s = 2463534242u, hash = 2166136261u;
    for (int set = 0; set < 24; set++) {
        size_t T = (size_t)(1 + (set * 13) % FIR_MAXT);
        for (size_t k = 0; k < T; k++) {                           /* 필터: 안전 / 무작위 / ±최대 */
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            int16_t v = (int16_t)(uint16_t)s;
            gh[k] = set % 3 == 0 ? (int16_t)(v / (int32_t)T) : set % 3 == 1 ? v : (int16_t)((v & 1) ? -32768 : 32767);
        }
        for (size_t j = 0; j < 40 + T - 1; j++) {                  /* 신호: 무작위 / 전 스케일 ± */
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            gx[j] = (set & 4) ? (int16_t)((s & 1) ? -32768 : 32767) : (int16_t)(uint16_t)(s >> 7);
        }
        f(gx, 40, gh, T, gy);
        for (int i = 0; i < 40; i++) hash = (hash ^ (uint16_t)gy[i]) * 16777619u;
    }
    return hash;
}

/* ---- Part C 스위트 ---- */
static int suite_chain(int fk, size_t T, int16_t gain, int16_t offset, int sig) {
    enum { LEN = 1000 };
    static int16_t in[LEN], ext[LEN + FIR_MAXT], want[LEN], got[LEN];
    int16_t h[FIR_MAXT];
    make_filter(h, T, fk);
    make_signal(in, LEN, h, T, sig);
    memset(ext, 0, sizeof ext);                                    /* 한 번에: [0 × (T-1) | 신호] */
    memcpy(ext + T - 1, in, sizeof in);
    o_fir(ext, LEN, h, T, want);
    for (size_t i = 0; i < LEN; i++) want[i] = o_add16(o_mulr(want[i], gain), offset);
    chain_t c;
    chain_init(&c, h, T, gain, offset);
    for (size_t pos = 0; pos < LEN; ) {                            /* 무작위 블록 크기 0..64 */
        size_t n = xs() % (CHAIN_BLOCK + 1);
        if (n > LEN - pos) n = LEN - pos;
        chain_process(&c, in + pos, got + pos, n);
        pos += n;
    }
    int fails = 0;
    for (size_t i = 0; i < LEN; i++) fails += got[i] != want[i];
    return fails;
}

int main(void) {
    /* ---------- Part A-1: C 기준 구현 = 명세 ---------- */
    CHECK(suite_add32() == 0);
    CHECK(suite_bin16(w_lmult_hi, o_lmult_hi) == 0 && suite_bin16(w_lmult_lo, o_lmult_lo) == 0);
    CHECK(suite_bin16(q15_mul, o_mul) == 0);
    CHECK(suite_bin16(q15_mulr, o_mulr) == 0);
    CHECK(suite_bin16(q15_add_sat, o_add16) == 0);
    CHECK(suite_mac_round() == 0);
    CHECK(q15_lmult(-32768, -32768) == INT32_MAX && q15_lmult(-32768, 32767) == -2147418112);
    CHECK(q15_mul(-32768, -32768) == 32767 && q15_mulr(-32768, -32768) == 32767);
    CHECK(q15_mul(-1, 1) == -1 && q15_mulr(-1, 1) == 0);          /* SQDMULH 는 -∞ 쪽 버림 */
    CHECK(q15_mul(1, 16384) == 0 && q15_mulr(1, 16384) == 1);
    CHECK(q15_add_sat(32767, 1) == 32767 && q15_add_sat(-32768, -1) == -32768 && q15_add_sat(-32768, 32767) == -1);
    CHECK(q31_round(0x7FFF8000) == 32767 && q31_round(-0x8000) == 0 && q31_round(-0x8001) == -1);
    CHECK(q31_round(0x18000) == 2 && q31_round(-0x18000) == -1);   /* 0.5 단위 동점은 +∞ 쪽 */
    CHECK(q31_mac(INT32_MIN, -32768, -32768) == -1);               /* 포화 두 번: 한 번이면 0 */
    {
        int sat = 0;                                               /* L_mult 가 포화하는 쌍: a 전체 × b=-32768 */
        for (int32_t a = -32768; a <= 32767; a++) sat += q15_lmult((int16_t)a, -32768) == INT32_MAX;
        CHECK(sat == 1);
    }
    {
        int16_t y[1];
        fir_q15_ref(X_TWO_SAT, 1, H_TWO_SAT, 4, y);
        CHECK(y[0] == 1);                                          /* 손 계산 (5장) — 포화를 한 번만 하면 2 */
    }
    CHECK(suite_fir(fir_q15_ref, 1, 0) == 0);

    /* ---------- Part A-2/3: NEON intrinsics 와 손 asm ---------- */
    CHECK(suite_vec(q15_mul_neon, o_mul) == 0);
    CHECK(suite_vec(q15_add_neon, o_add16) == 0);
    CHECK(suite_scale(q15_scale_asm, 0) == 0);
    CHECK(suite_fir(fir_q15_neon, 1, 0) == 0);
    CHECK(suite_fir(fir_q15_asm, 1, 0) == 0);
    printf("info: golden FNV-1a of the spec (oracle) = 0x%08x\n", golden_hash(o_fir));
    CHECK(golden_hash(o_fir) == GOLDEN_FNV);                       /* 상수 자체 확인 */
    CHECK(golden_hash(fir_q15_ref) == GOLDEN_FNV && golden_hash(fir_q15_neon) == GOLDEN_FNV && golden_hash(fir_q15_asm) == GOLDEN_FNV);
    {
        int16_t y[2] = { 0, 0 }, x8[11] = { 3, -32768, 32767, -32768 };
        fir_q15_neon(X_TWO_SAT, 1, H_TWO_SAT, 4, y);
        fir_q15_asm(X_TWO_SAT, 1, H_TWO_SAT, 4, y + 1);
        CHECK(y[0] == 1 && y[1] == 1);
        int16_t y8[8] = { 0 }, z8[8] = { 0 };                                    /* 같은 계산이 8-lane 경로의 lane 0 에서도 */
        fir_q15_asm(x8, 8, H_TWO_SAT, 4, y8); fir_q15_neon(x8, 8, H_TWO_SAT, 4, z8);
        CHECK(y8[0] == 1 && z8[0] == 1);
    }
    {   /* 명령 자체의 의미 확인: SQDMLAL = 두 번 포화, SQRSHRN #16 = round (넓게) — 경계 전부 */
        int fails = 0;
        for (size_t i = 0; i < NE32; i++) for (size_t j = 0; j < NE16; j++) for (size_t k = 0; k < NE16; k++) {
            int32x4_t r = vqdmlal_s16(vdupq_n_s32(E32[i]), vdup_n_s16(E16[j]), vdup_n_s16(E16[k]));
            fails += vgetq_lane_s32(r, 3) != o_mac(E32[i], E16[j], E16[k]);
        }
        for (int t = 0; t < (1 << 20); t++) {
            int32_t L = t < (int)NE32 ? E32[t] : r32_edgy();
            fails += vqrshrns_n_s32(L, 16) != o_round(L);
            fails += (int16_t)(o_add32(L, 0x8000) >> 16) != o_round(L);   /* ETSI 형 = 넓은 형 */
        }
        CHECK(fails == 0);
    }

    /* ---------- Part B: 버그를 테스트가 잡나 ---------- */
    int b1 = suite_scale(bug_scale_sqdmulh, 1);
    int b2 = suite_fir(bug_fir_trunc, 1, 1);
    int b3w = suite_fir(bug_fir_addwrap, 0, 1), b3s = suite_fir(bug_fir_addwrap, 1, 1);
    int b4w = suite_fir(bug_fir_tapsplit, 0, 0), b4s = suite_fir(bug_fir_tapsplit, 1, 0);
    printf("info: bug mismatches — scale_sqdmulh %d, fir_trunc %d, addwrap weak/strong %d/%d, tapsplit weak/strong %d/%d\n",
           b1, b2, b3w, b3s, b4w, b4s);
    CHECK(b1 > 0);                                                 /* 반올림 누락: 무작위로도 바로 잡힌다 */
    CHECK(b2 > 0);
    CHECK(b3w == 0 && b3s > 0);                                    /* 감쌈: 약한 테스트는 통과시켜 버린다 */
    CHECK(b4w == 0 && b4s > 0);                                    /* 탭 분할: 포화가 있어야 드러난다 */
    {   /* 정리: Σ|h| <= 32767 이면 포화 파이프라인 = 포화 없는 정확 계산 (어떤 입력이든) */
        int16_t h[FIR_MAXT], x[200 + FIR_MAXT], ya[200], yb[200];
        int diff_safe = 0, diff_hot = 0, l1_bad = 0;
        for (int t = 0; t < 400; t++) {
            size_t T = 1 + xs() % FIR_MAXT;
            int fk = (t & 1) ? FILT_HOT : FILT_SAFE;
            make_filter(h, T, fk);
            make_signal(x, 200 + T - 1, h, T, (int)(xs() % NSIG));
            o_fir(x, 200, h, T, ya); o_fir_exact(x, 200, h, T, yb);
            int d = memcmp(ya, yb, sizeof ya) != 0;
            if (fk == FILT_SAFE) { diff_safe += d; l1_bad += l1_norm(h, T) > 32767; }
            else diff_hot += d;
        }
        printf("info: saturating vs exact FIR differ — safe filters %d/200, hot filters %d/200\n", diff_safe, diff_hot);
        CHECK(l1_bad == 0 && diff_safe == 0 && diff_hot > 0);
    }

    /* ---------- Part C: 블록 스트리밍 체인 ---------- */
    CHECK(suite_chain(FILT_SAFE, 1, 32767, 0, SIG_RAND) == 0);
    CHECK(suite_chain(FILT_SAFE, 31, 23170, -100, SIG_RAND) == 0);  /* 이득 0.7071 */
    CHECK(suite_chain(FILT_HOT, 64, 32767, 0, SIG_SQUARE) == 0);
    CHECK(suite_chain(FILT_ALT, 17, -32768, 32767, SIG_MATCHED) == 0);   /* 이득 -1.0, 오프셋 최대 */
    CHECK(suite_chain(FILT_SAFE, 64, 16384, 8192, SIG_SQUARE) == 0);
    {
        int16_t h1[1] = { 32767 }, in[CHAIN_BLOCK], out[CHAIN_BLOCK] = { 0 };
        chain_t c;
        chain_init(&c, h1, 1, 32767, 16384);                          /* 큰 입력 + 오프셋 → 32767 에 붙어야 (감싸면 음수) */
        for (int i = 0; i < CHAIN_BLOCK; i++) in[i] = 30000;
        chain_process(&c, in, out, CHAIN_BLOCK);
        CHECK(out[0] == 32767 && out[CHAIN_BLOCK - 1] == 32767);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
