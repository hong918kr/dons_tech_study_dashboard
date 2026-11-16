/* ARM-A07 추가 연습 — 답 + 테스트 (한 번에 검증)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-A07_practice && /tmp/ARM-A07_practice
 * 연습 1 FPSR.QC 로 "이 블록에서 포화가 났나" 알아내기   연습 2 SQRDMLAH(융합) vs SQADD(SQRDMULH) — 언제 다른가
 * 연습 3 값싼 16비트 누산 FIR 과 오차 한계 [-T, 0]      연습 4 float → Q15 (FCVTZS #15 + SQXTN)
 * 오라클·FIR 은 solution.c 의 축약판 (레슨끼리 #include 하지 않는다).
 */
#include <arm_neon.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint32_t qc_get(void);
void     qc_clear(void);
void     fir16_asm(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y);

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t xs(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static int16_t r16(void) { return (int16_t)(uint16_t)xs(); }

/* ---- 오라클 (solution.c 축약판) ---- */
static int64_t fdiv(int64_t v, int64_t d) { return v >= 0 ? v / d : -((-v + d - 1) / d); }
static int32_t o_sat32(int64_t v) { return (int32_t)(v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v); }
static int16_t o_sat16(int64_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
static int32_t o_mac(int32_t acc, int16_t a, int16_t b) { return o_sat32((int64_t)acc + o_sat32(2 * (int64_t)a * b)); }
static int16_t o_round(int32_t L) { return o_sat16(fdiv((int64_t)L + 0x8000, 65536)); }
static int16_t o_mul(int16_t a, int16_t b) { return o_sat16(fdiv(2 * (int64_t)a * b, 65536)); }
static void o_fir(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y) {
    for (size_t i = 0; i < n; i++) {
        int32_t acc = 0;
        for (size_t k = 0; k < T; k++) acc = o_mac(acc, h[k], x[i + T - 1 - k]);
        y[i] = o_round(acc);
    }
}
/* NEON FIR (solution.c fir_q15_neon 축약판) */
static void fir_q15_neon(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y) {
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        int32x4_t lo = vdupq_n_s32(0), hi = vdupq_n_s32(0);
        for (size_t k = 0; k < T; k++) {
            int16x8_t xv = vld1q_s16(x + i + T - 1 - k);
            lo = vqdmlal_n_s16(lo, vget_low_s16(xv), h[k]);
            hi = vqdmlal_high_n_s16(hi, xv, h[k]);
        }
        vst1q_s16(y + i, vcombine_s16(vqrshrn_n_s32(lo, 16), vqrshrn_n_s32(hi, 16)));
    }
    for (; i < n; i++) {
        int32_t acc = 0;
        for (size_t k = 0; k < T; k++) acc = vqdmlalh_s16(acc, h[k], x[i + T - 1 - k]);
        y[i] = vqrshrns_n_s32(acc, 16);
    }
}

/* ===== 연습 1: 블록 단위 포화 감지 (FPSR.QC) =====
 * 컴파일러는 QC 를 모른다 (demo_qc_reorder.c): 같은 함수에 인라인되면 포화 명령이 지워지거나 옮겨진다.
 * → noinline 경계 + 결과를 포인터 인자로 내보내기: x/h/y 가 "남이 볼 수 있는 메모리"가 되어
 *   qc_clear() 뒤에 읽고, qc_get() 전에 써야 한다. */
__attribute__((noinline)) int fir_q15_checked(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y) {
    qc_clear();
    fir_q15_neon(x, n, h, T, y);
    return (int)qc_get();
}
__attribute__((noinline)) static uint32_t sqadd_qc(int16_t *y, const int16_t *a, const int16_t *b) {
    vst1q_s16(y, vqaddq_s16(vld1q_s16(a), vld1q_s16(b)));
    return qc_get();                        /* QC 를 지우지 않고 읽기만 → sticky 확인용 */
}

/* ===== 연습 2: 융합 vs 두 단계 ===== */
__attribute__((target("arch=armv8.1-a"))) static int16_t mla_fused(int16_t acc, int16_t a, int16_t b) { return vqrdmlahh_s16(acc, a, b); }
static int16_t mla_two_step(int16_t acc, int16_t a, int16_t b) { return vqaddh_s16(acc, vqrdmulhh_s16(a, b)); }
static int16_t o_mla_fused(int16_t acc, int16_t a, int16_t b) {    /* 명세: 한 번에 넓게 → 한 번 포화 */
    return o_sat16(fdiv((int64_t)acc * 65536 + 2 * (int64_t)a * b + 0x8000, 65536));
}
static int mla_differs_pred(int16_t acc, int16_t a, int16_t b) { return a == -32768 && b == -32768 && acc < 0; }

/* ===== 연습 3: 16비트 누산 FIR 오라클 ===== */
static void o_fir16(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y) {
    for (size_t i = 0; i < n; i++) {
        int16_t acc = 0;
        for (size_t k = 0; k < T; k++) acc = o_sat16((int64_t)acc + o_mul(x[i + T - 1 - k], h[k]));
        y[i] = acc;
    }
}

/* ===== 연습 4: float → Q15 ===== */
void f32_to_q15_neon(int16_t *y, const float *f, size_t n) {
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        int32x4_t q = vcvtq_n_s32_f32(vld1q_f32(f + i), 15);       /* FCVTZS #15: ×2^15, 0 쪽 버림, int32 포화, NaN→0 */
        vst1_s16(y + i, vqmovn_s32(q));                             /* SQXTN: int16 으로 포화 */
    }
    for (; i < n; i++) y[i] = vqmovns_s32(vcvts_n_s32_f32(f[i], 15));
}
static int16_t o_f2q15(float f) {                                   /* C 기준: UB 인 변환을 하지 않는다 */
    if (isnan(f)) return 0;
    double v = (double)f * 32768.0;
    if (v >= 32767.0) return 32767;
    if (v <= -32768.0) return -32768;
    return (int16_t)(int32_t)v;                                     /* 범위 안이라 정의됨, 0 쪽 버림 */
}

int main(void) {
    /* ---- 연습 1 ---- */
    {
        int16_t h[16], x[64 + 15], y[64];
        for (int k = 0; k < 16; k++) h[k] = (int16_t)((int)(xs() % 4095) - 2047);    /* Σ|h| <= 16·2047 < 32767 */
        for (int j = 0; j < 64 + 15; j++) x[j] = r16();
        CHECK(fir_q15_checked(x, 64, h, 16, y) == 0);                /* 안전한 필터: 포화 없음 */
        for (int k = 0; k < 16; k++) h[k] = 30000;
        for (int j = 0; j < 64 + 15; j++) x[j] = (j / 5) & 1 ? -32768 : 32767;
        CHECK(fir_q15_checked(x, 64, h, 16, y) == 1);                /* 뜨거운 필터 + 구형파: 포화 */
        CHECK(fir_q15_checked(x, 3, h, 16, y) == 1);                 /* 꼬리 경로(스칼라 SQDMLAL)도 QC 를 세운다 */
        int16_t big[8], one[8];
        for (int i = 0; i < 8; i++) { big[i] = 32767; one[i] = 1; }
        qc_clear();
        uint32_t q1 = sqadd_qc(y, big, one);                         /* 포화 → QC = 1 */
        uint32_t q2 = sqadd_qc(y + 8, one, one);                     /* 포화 아님 → QC 그대로 1 (sticky) */
        qc_clear();
        CHECK(q1 == 1 && q2 == 1 && qc_get() == 0 && y[0] == 32767 && y[8] == 2);
    }
    /* ---- 연습 2 ---- */
    {
        static const int16_t E[] = { -32768, -32767, -16384, -2, -1, 0, 1, 2, 16384, 32766, 32767 };
        enum { NE = sizeof E / sizeof E[0] };
        int bad_fused = 0, bad_pred = 0, ndiff = 0;
        for (int32_t a = -32768; a <= 32767; a++)
            for (int j = 0; j < NE; j++) for (int m = 0; m < NE; m++) {
                int16_t acc = E[m], b = E[j];
                int16_t f = mla_fused(acc, (int16_t)a, b), t = mla_two_step(acc, (int16_t)a, b);
                bad_fused += f != o_mla_fused(acc, (int16_t)a, b);
                bad_pred += (f != t) != mla_differs_pred(acc, (int16_t)a, b);
                ndiff += f != t;
            }
        for (int32_t acc = -32768; acc <= 32767; acc++) {             /* a = b = -32768 에서 acc 전부 */
            int16_t f = mla_fused((int16_t)acc, -32768, -32768), t = mla_two_step((int16_t)acc, -32768, -32768);
            bad_pred += (f != t) != mla_differs_pred((int16_t)acc, -32768, -32768);
            bad_pred += mla_differs_pred((int16_t)acc, -32768, -32768) && f - t != 1;   /* 차이는 정확히 1 */
        }
        for (int t = 0; t < (1 << 20); t++) {
            int16_t acc = r16(), a = r16(), b = r16();
            bad_fused += mla_fused(acc, a, b) != o_mla_fused(acc, a, b);
            bad_pred += (mla_fused(acc, a, b) != mla_two_step(acc, a, b)) != mla_differs_pred(acc, a, b);
        }
        printf("info: fused vs two-step differ in %d of %d grid cases\n", ndiff, 65536 * NE * NE);
        CHECK(bad_fused == 0 && bad_pred == 0);
        CHECK(mla_fused(-1, -32768, -32768) == 32767 && mla_two_step(-1, -32768, -32768) == 32766);
    }
    /* ---- 연습 3 ---- */
    {
        static int16_t x[100 + 64], y16[100 + 1], ya[100], yb[100], h[64];
        static const size_t Ts[] = { 1, 2, 3, 8, 15, 16, 31, 64 };
        int bad = 0, bad_bound = 0, maxd = 0; long sumd = 0, cnt = 0;
        for (size_t ti = 0; ti < sizeof Ts / sizeof Ts[0]; ti++)
        for (size_t n = 0; n <= 100; n += (n < 20 ? 1 : 16)) {
            size_t T = Ts[ti];
            for (int rep = 0; rep < 6; rep++) {
                int hot = rep >= 4;
                int32_t m = (int32_t)((32767 - T) / T);              /* Σ|h| <= 32767-T: 16비트 누산도 포화 없음 */
                for (size_t k = 0; k < T; k++) h[k] = hot ? r16() : (int16_t)((int32_t)(xs() % (uint64_t)(2 * m + 1)) - m);
                for (size_t j = 0; j < n + T - 1; j++) x[j] = (rep == 5) ? (j & 2 ? -32768 : 32767) : r16();
                y16[n] = 0x5A5A;
                fir16_asm(x, n, h, T, y16);
                o_fir16(x, n, h, T, ya);
                for (size_t i = 0; i < n; i++) bad += y16[i] != ya[i];
                bad += y16[n] != 0x5A5A;
                if (!hot) {
                    o_fir(x, n, h, T, yb);
                    for (size_t i = 0; i < n; i++) {
                        int d = ya[i] - yb[i];
                        bad_bound += d > 0 || d < -(int)T;
                        if (-d > maxd) maxd = -d;
                        sumd += d; cnt++;
                    }
                }
            }
        }
        printf("info: fir16 - fir32 (safe filters): max |d| = %d LSB, mean d = %.2f LSB over %ld outputs\n",
               maxd, cnt ? (double)sumd / (double)cnt : 0.0, cnt);
        CHECK(bad == 0);
        CHECK(bad_bound == 0);
    }
    /* ---- 연습 4 ---- */
    {
        static const float sp[] = { 0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 0.99997f, 0.999985f, -0.999985f,
                                    1.0f / 32768, -1.0f / 32768, 0.9f / 32768, -0.9f / 32768, 1e30f, -1e30f,
                                    0x1p-149f, -0x1p-149f, 2.0f, -2.0f };
        enum { NSP = sizeof sp / sizeof sp[0] };
        float f[NSP + 4]; int16_t y[NSP + 4];
        memcpy(f, sp, sizeof sp);
        f[NSP] = INFINITY; f[NSP + 1] = -INFINITY; f[NSP + 2] = NAN; f[NSP + 3] = -NAN;
        f32_to_q15_neon(y, f, NSP + 4);
        int bad = 0;
        for (int i = 0; i < NSP + 4; i++) bad += y[i] != o_f2q15(f[i]);
        CHECK(bad == 0);
        CHECK(y[2] == 32767 && y[3] == -32768 && y[4] == 16384 && y[11] == 0 && y[12] == 0 && y[NSP + 2] == 0);
        static float rf[4099]; static int16_t ry[4099];
        for (int t = 0; t < 256; t++) {
            for (int i = 0; i < 4099; i++) {
                uint32_t u = (uint32_t)xs();
                if (t & 1) memcpy(&rf[i], &u, 4);                    /* 아무 비트 패턴 (NaN, Inf, 비정규수 포함) */
                else rf[i] = (float)((double)(int32_t)u / 2147483648.0 * 1.25);
            }
            f32_to_q15_neon(ry, rf, 4099);
            for (int i = 0; i < 4099; i++) bad += ry[i] != o_f2q15(rf[i]);
        }
        CHECK(bad == 0);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
