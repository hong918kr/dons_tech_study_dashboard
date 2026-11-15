/* ARM-B11 테스트 하네스 — starter.S / solution.S 공용.
 * cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_abi.S -o sol && ./sol
 *
 * 규칙: 범위 밖 float→int 변환은 C 에서 UB (C11 6.3.1.4p1) → 그런 입력은 C 로 계산하지 않고
 *       손으로 쓴 기댓값 또는 "먼저 범위를 검사하는" 참조 모델로 비교한다.
 *       fused 연산의 참조는 a*b+c 가 아니라 fmaf/fma 로 명시한다 (-ffp-contract 에 안 휘둘리게).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- asm 함수 (starter.S / solution.S) ---- */
extern void     vreg_views(uint64_t out[4]);
extern float    dot3(const float *a, const float *b);
extern double   dot3d(const double *a, const double *b);
extern float    lerp(float a, float b, float t);
extern double   mix_args(int32_t i, float f, int64_t l, double d);
extern int32_t  f2i_sat(float x);
extern int64_t  d2l_sat(double x);
extern uint32_t f2u_sat(float x);
extern float    i2f(int32_t v);
extern float    u2f(uint32_t v);
extern double   l2d(int64_t v);
extern double   ul2d(uint64_t v);
extern float    lerp_exact(float a, float b, float t);
extern int32_t  cvt_ns(float x);
extern int32_t  cvt_ms(float x);
extern int32_t  cvt_ps(float x);
extern int32_t  cvt_as(float x);
extern int32_t  float_to_q15_sat(float x);
extern int32_t  float_to_q16(float x);
extern float    q16_to_float(int32_t q);
extern float    adc_to_volts(uint32_t raw12, float vref);
extern float    ema_step(float (*read)(void), float y, float alpha);

/* ---- shared_abi.S (테스트 도구) ---- */
typedef float (*ema_fn)(float (*)(void), float, float);
extern void  clobber_fp_scratch(void);
extern float cs_check_call(ema_fn fn, float (*read)(void), float y, float alpha, uint32_t *mask);

/* ---- 하네스 ---- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x2545F4914F6CDD1Dull;          /* 고정 시드 xorshift64 */
static uint64_t xs(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static float  rnd_f(float lo, float hi) { return lo + (hi - lo) * (float)(xs() >> 40) * 0x1p-24f; }
static double rnd_d(double lo, double hi) { return lo + (hi - lo) * (double)(xs() >> 11) * 0x1p-53; }
static float  bits_f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t f_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint64_t d_bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
/* 비트까지 같다 (−0 ≠ +0 구분), NaN 끼리는 같다고 본다 */
static int same_f(float a, float b) { return (isnan(a) && isnan(b)) || f_bits(a) == f_bits(b); }
static int same_d(double a, double b) { return (isnan(a) && isnan(b)) || d_bits(a) == d_bits(b); }

/* 참조 모델: 범위를 먼저 검사하고, 안전한 경우에만 C 캐스트 (UB 없음) */
static int32_t ref_f2i_sat(float x) {
    if (isnan(x)) return 0;
    if (x >= 0x1p31f) return INT32_MAX;
    if (x < -0x1p31f) return INT32_MIN;
    return (int32_t)x;                  /* 이제 |trunc(x)| 가 int32 안 → 정의됨 */
}
static int64_t ref_d2l_sat(double x) {
    if (isnan(x)) return 0;
    if (x >= 0x1p63) return INT64_MAX;
    if (x < -0x1p63) return INT64_MIN;
    return (int64_t)x;
}
static uint32_t ref_f2u_sat(float x) {
    if (isnan(x) || x <= -1.0f) return 0;   /* (-1, 0) 은 0 으로 잘림 → C 에서도 정의됨 */
    if (x >= 0x1p32f) return UINT32_MAX;
    return (uint32_t)x;
}
static int32_t ref_q15(float x) {
    if (isnan(x)) return 0;
    double v = (double)x * 32768.0;     /* float × 2^15 은 double 에서 정확 */
    if (v >= 32767.0) return 32767;
    if (v <= -32768.0) return -32768;
    return (int32_t)v;
}

/* Part C 콜백: 샘플을 하나 돌려주고, caller-saved FP 레지스터를 전부 망가뜨린다 */
static const float g_samples[6] = { 1.0f, 1.0f, 0.5f, 2.0f, -1.0f, 1.25f };
static int g_reads;
static float sensor_read(void) {
    float v = g_samples[g_reads % 6];
    g_reads++;
    clobber_fp_scratch();
    return v;
}

int main(void) {
    int bad;

    /* ================= Part A ================= */
    uint64_t v[4] = { 0x1111111111111111ull, 0x1111111111111111ull, 0x1111111111111111ull, 0x1111111111111111ull };
    vreg_views(v);
    CHECK(v[0] == 0x000000003F800000ull);           /* s0 = 1.0f, d0 상위 32비트 0 */
    CHECK(v[1] == 0);                               /* v0[127:64] 도 0 */
    CHECK(v[2] == 0xFFFFFFFF3F800000ull);           /* mov v1.s[0] 은 나머지 lane 보존 */
    CHECK(v[3] == 0xFFFFFFFFFFFFFFFFull);

    const float a1[3] = { 1.0f, 2.0f, 3.0f }, b1[3] = { 4.0f, 5.0f, 6.0f };
    CHECK(dot3(a1, b1) == 32.0f);
    /* fused 가 보이는 입력: x = 1+2^-12, x*x = 1 + 2^-11 + 2^-24 */
    const float x12 = 1.0f + 0x1p-12f;
    const float a2[3] = { 1.0f, x12, 0.0f }, b2[3] = { -(1.0f + 0x1p-11f), x12, 0.0f };
    CHECK(dot3(a2, b2) == 0x1p-24f);                /* fmadd: 곱을 반올림하지 않음 */
    float p0 = a2[0] * b2[0];                       /* 문장을 나누면 contraction 안 됨 */
    float p1 = a2[1] * b2[1];
    float unfused = p0 + p1;
    CHECK(unfused == 0.0f);                         /* 비융합 C 는 0 — 참조로 쓰면 틀린다 */
    bad = 0;
    for (int i = 0; i < 200000; i++) {
        float a[3], b[3];
        for (int k = 0; k < 3; k++) { a[k] = rnd_f(-100, 100); b[k] = rnd_f(-100, 100); }
        float ref = fmaf(a[2], b[2], fmaf(a[1], b[1], a[0] * b[0]));
        if (!same_f(dot3(a, b), ref)) bad++;
    }
    CHECK(bad == 0);
    const double ad[3] = { 1.5, -2.0, 0.25 }, bd[3] = { 2.0, 3.0, 8.0 };
    CHECK(dot3d(ad, bd) == -1.0);
    bad = 0;
    for (int i = 0; i < 200000; i++) {
        double a[3], b[3];
        for (int k = 0; k < 3; k++) { a[k] = rnd_d(-1e6, 1e6); b[k] = rnd_d(-1e6, 1e6); }
        double ref = fma(a[2], b[2], fma(a[1], b[1], a[0] * b[0]));
        if (!same_d(dot3d(a, b), ref)) bad++;
    }
    CHECK(bad == 0);

    CHECK(lerp(0.0f, 10.0f, 0.25f) == 2.5f);
    CHECK(lerp(-4.0f, 4.0f, 0.5f) == 0.0f);
    bad = 0;
    for (int i = 0; i < 200000; i++) {
        float a = rnd_f(-1e4f, 1e4f), b = rnd_f(-1e4f, 1e4f), t = rnd_f(0, 1);
        if (!same_f(lerp(a, b, t), fmaf(t, b - a, a))) bad++;
    }
    CHECK(bad == 0);

    CHECK(mix_args(3, 0.5f, -4, 0.25) == 0.5);      /* 3*0.5 + (-4)*0.25 */
    CHECK(mix_args(INT32_MIN, 1.0f, INT64_MAX, 1.0) == 0x1p63 - 0x1p31);
    bad = 0;
    for (int i = 0; i < 200000; i++) {
        int32_t ii = (int32_t)xs(); float f = rnd_f(-1e3f, 1e3f);
        int64_t ll = (int64_t)xs(); double d = rnd_d(-1.0, 1.0);
        double ref = fma((double)ll, d, (double)ii * (double)f);
        if (!same_d(mix_args(ii, f, ll, d), ref)) bad++;
    }
    CHECK(bad == 0);

    CHECK(f2i_sat(2.9f) == 2);
    CHECK(f2i_sat(-2.9f) == -2);                    /* 0 쪽으로 자름 (floor 아님) */
    CHECK(d2l_sat(-7.75) == -7);
    CHECK(f2u_sat(4000000000.0f) == 4000000000u);   /* int32 로는 못 담는 값 */
    bad = 0;
    for (int i = 0; i < 200000; i++) {
        float x = rnd_f(-2e9f, 2e9f);
        if (f2i_sat(x) != (int32_t)x) bad++;        /* |x| < 2^31: C 캐스트도 정의됨 */
    }
    CHECK(bad == 0);
    CHECK(i2f(16777217) == 16777216.0f);            /* 2^24+1 은 float 로 못 담음 → 짝수 쪽 */
    CHECK(i2f(-1) == -1.0f);
    CHECK(u2f(0xFFFFFFFFu) == 4294967296.0f);       /* 반올림해서 2^32 */
    CHECK(i2f((int32_t)0xFFFFFFFFu) == -1.0f);      /* 같은 비트, 다른 명령 = 다른 값 */
    CHECK(l2d(INT64_MIN) == -0x1p63);
    CHECK(ul2d(UINT64_MAX) == 0x1p64);
    bad = 0;
    for (int i = 0; i < 200000; i++) {
        uint64_t r = xs();
        if (!same_f(i2f((int32_t)r), (float)(int32_t)r)) bad++;
        if (!same_f(u2f((uint32_t)r), (float)(uint32_t)r)) bad++;
        if (!same_d(l2d((int64_t)r), (double)(int64_t)r)) bad++;
        if (!same_d(ul2d(r), (double)r)) bad++;
    }
    CHECK(bad == 0);

    /* ================= Part B ================= */
    /* FCVTZS 경계: 기댓값은 손으로 (C 캐스트였다면 UB) */
    CHECK(f2i_sat(NAN) == 0);
    CHECK(f2i_sat(-NAN) == 0);
    CHECK(f2i_sat(INFINITY) == INT32_MAX);
    CHECK(f2i_sat(-INFINITY) == INT32_MIN);
    CHECK(f2i_sat(3e9f) == INT32_MAX);
    CHECK(f2i_sat(-3e9f) == INT32_MIN);
    CHECK(f2i_sat(0x1p31f) == INT32_MAX);           /* 2147483648.0f: 딱 1 넘침 */
    CHECK(f2i_sat(-0x1p31f) == INT32_MIN);          /* -2^31 은 정확히 표현 가능 */
    CHECK(f2i_sat(2147483520.0f) == 2147483520);    /* 2^31 아래 가장 큰 float */
    CHECK(f2i_sat(-0.0f) == 0);
    CHECK(f2i_sat(0x1p-149f) == 0 && f2i_sat(-0x1p-149f) == 0);   /* 비정규수 */
    CHECK(f2i_sat(0.99999994f) == 0 && f2i_sat(-0.99999994f) == 0);
    CHECK(d2l_sat(NAN) == 0);
    CHECK(d2l_sat(INFINITY) == INT64_MAX && d2l_sat(-INFINITY) == INT64_MIN);
    CHECK(d2l_sat(0x1p63) == INT64_MAX && d2l_sat(-0x1p63) == INT64_MIN);
    CHECK(d2l_sat(9223372036854774784.0) == 9223372036854774784LL);  /* 2^63 아래 최대 double */
    CHECK(d2l_sat(1e300) == INT64_MAX);
    CHECK(f2u_sat(-1.0f) == 0 && f2u_sat(-0.5f) == 0 && f2u_sat(-INFINITY) == 0);
    CHECK(f2u_sat(NAN) == 0);
    CHECK(f2u_sat(0x1p32f) == UINT32_MAX && f2u_sat(INFINITY) == UINT32_MAX);
    CHECK(f2u_sat(4294967040.0f) == 4294967040u);   /* 2^32 아래 최대 float */
    bad = 0;                                        /* 임의 비트 패턴 (NaN·Inf·비정규 포함) */
    for (int i = 0; i < 1000000; i++) {
        uint64_t r = xs();
        float x = bits_f((uint32_t)r);
        double dx; memcpy(&dx, &r, 8);
        if (f2i_sat(x) != ref_f2i_sat(x)) bad++;
        if (f2u_sat(x) != ref_f2u_sat(x)) bad++;
        if (d2l_sat(dx) != ref_d2l_sat(dx)) bad++;
    }
    CHECK(bad == 0);

    /* 반올림 방향 표: 입력      2.5  -2.5  3.5  -3.5  0.5  -0.5  1.4  -1.6 */
    static const float rin[8] = { 2.5f, -2.5f, 3.5f, -3.5f, 0.5f, -0.5f, 1.4f, -1.6f };
    static const int32_t e_ns[8] = { 2, -2, 4, -4, 0, 0, 1, -2 };      /* ties to even  */
    static const int32_t e_as[8] = { 3, -3, 4, -4, 1, -1, 1, -2 };     /* ties away     */
    static const int32_t e_ms[8] = { 2, -3, 3, -4, 0, -1, 1, -2 };     /* floor         */
    static const int32_t e_ps[8] = { 3, -2, 4, -3, 1, 0, 2, -1 };      /* ceil          */
    static const int32_t e_zs[8] = { 2, -2, 3, -3, 0, 0, 1, -1 };      /* trunc         */
    int bns = 0, bas = 0, bms = 0, bps = 0, bzs = 0;
    for (int i = 0; i < 8; i++) {
        bns += cvt_ns(rin[i]) != e_ns[i];  bas += cvt_as(rin[i]) != e_as[i];
        bms += cvt_ms(rin[i]) != e_ms[i];  bps += cvt_ps(rin[i]) != e_ps[i];
        bzs += f2i_sat(rin[i]) != e_zs[i];
    }
    CHECK(bns == 0); CHECK(bas == 0); CHECK(bms == 0); CHECK(bps == 0); CHECK(bzs == 0);
    bad = 0;                                        /* 범위 안: libm 과 비교 (k/4 로 tie 자주) */
    for (int i = 0; i < 200000; i++) {
        float x = (i & 1) ? rnd_f(-1e6f, 1e6f) : (float)((int32_t)(xs() % 4001) - 2000) * 0.25f;
        if (cvt_ns(x) != (int32_t)nearbyintf(x)) bad++;
        if (cvt_as(x) != (int32_t)roundf(x)) bad++;
        if (cvt_ms(x) != (int32_t)floorf(x)) bad++;
        if (cvt_ps(x) != (int32_t)ceilf(x)) bad++;
    }
    CHECK(bad == 0);
    CHECK(cvt_ns(NAN) == 0 && cvt_as(NAN) == 0 && cvt_ms(NAN) == 0 && cvt_ps(NAN) == 0);
    CHECK(cvt_ms(-INFINITY) == INT32_MIN && cvt_ps(3e9f) == INT32_MAX);
    CHECK(cvt_as(-3e9f) == INT32_MIN && cvt_ns(INFINITY) == INT32_MAX);
    CHECK(cvt_ps(-0.5f) == 0);                      /* 정수 결과엔 −0 이 없다 */

    /* lerp 의 t = 1 정확성 */
    CHECK(lerp(1e8f, 0.1f, 1.0f) == 0.0f);          /* b-a 반올림 → b 로 못 돌아옴 (기댓값 손계산) */
    CHECK(lerp_exact(1e8f, 0.1f, 1.0f) == 0.1f);
    CHECK(lerp_exact(1e8f, 0.1f, 0.0f) == 1e8f);
    bad = 0;
    int miss_lerp = 0;
    for (int i = 0; i < 200000; i++) {
        float a = rnd_f(-1e6f, 1e6f), b = rnd_f(-1e6f, 1e6f), t = rnd_f(0, 1);
        if (!same_f(lerp_exact(a, b, 1.0f), b) && !(b == 0.0f && lerp_exact(a, b, 1.0f) == 0.0f)) bad++;
        if (lerp_exact(a, b, 0.0f) != a) bad++;
        if (!same_f(lerp_exact(a, b, t), fmaf(t, b, fmaf(-t, a, a)))) bad++;
        if (lerp(a, b, 1.0f) != b) miss_lerp++;     /* 관찰용: a+t(b-a) 가 b 를 놓친 횟수 */
    }
    CHECK(bad == 0);
    printf("info: lerp(a,b,1) != b in %d / 200000 random cases; lerp_exact: 0\n", miss_lerp);

    /* ================= Part C ================= */
    CHECK(float_to_q15_sat(0.5f) == 16384);
    CHECK(float_to_q15_sat(-1.0f) == -32768);
    CHECK(float_to_q15_sat(1.0f) == 32767);         /* 32768 → int16 포화 */
    CHECK(float_to_q15_sat(-0.000030517578125f) == -1);  /* -2^-15 */
    CHECK(float_to_q15_sat(1e10f) == 32767 && float_to_q15_sat(-1e10f) == -32768);
    CHECK(float_to_q15_sat(NAN) == 0);
    bad = 0;
    for (int i = 0; i < 200000; i++) {
        float x = (i & 1) ? rnd_f(-1.5f, 1.5f) : bits_f((uint32_t)xs());
        if (float_to_q15_sat(x) != ref_q15(x)) bad++;
    }
    CHECK(bad == 0);
    CHECK(float_to_q16(1.5f) == 98304);
    CHECK(float_to_q16(-0.5f) == -32768);
    CHECK(float_to_q16(40000.0f) == INT32_MAX);     /* 40000 * 65536 > 2^31 → 포화 */
    CHECK(q16_to_float(98304) == 1.5f);
    CHECK(q16_to_float(-1) == -0x1p-16f);
    CHECK(q16_to_float(INT32_MIN) == -32768.0f);
    bad = 0;
    for (int i = 0; i < 200000; i++) {
        int32_t q = (int32_t)xs();
        if (!same_f(q16_to_float(q), (float)((double)q / 65536.0))) bad++;
        float x = rnd_f(-30000.0f, 30000.0f);
        if (float_to_q16(x) != (int32_t)((double)x * 65536.0)) bad++;
    }
    CHECK(bad == 0);
    CHECK(adc_to_volts(0, 3.3f) == 0.0f);
    CHECK(adc_to_volts(2048, 3.3f) == 3.3f * 0.5f);
    CHECK(adc_to_volts(4096, 2.5f) == 2.5f);
    bad = 0;
    for (uint32_t raw = 0; raw < 4096; raw++)       /* 12비트 전수 */
        if (!same_f(adc_to_volts(raw, 3.3f), ((float)raw / 4096.0f) * 3.3f)) bad++;
    CHECK(bad == 0);

    /* EMA 필터: 콜백이 caller-saved FP 를 다 망가뜨려도, d8-d15 를 지켜도 맞아야 한다 */
    g_reads = 0;
    float y = 0.0f, yref = 0.0f;
    uint32_t mask = 0, upper_seen = 0;
    bad = 0;
    for (int i = 0; i < 6; i++) {
        y = cs_check_call(ema_step, sensor_read, y, 0.25f, &mask);
        yref = fmaf(0.25f, g_samples[i] - yref, yref);
        if (!same_f(y, yref)) bad++;
        if (mask & 0xFFu) bad += 100;               /* d8-d15 하위 64비트 손상 = ABI 위반 */
        upper_seen |= mask >> 8;
    }
    CHECK(bad == 0);
    CHECK(g_reads == 6);
    printf("info: ema y=%a (%.9g), v8-v15 upper-64 changed mask=0x%02x (allowed by AAPCS64)\n",
           (double)y, (double)y, upper_seen);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
