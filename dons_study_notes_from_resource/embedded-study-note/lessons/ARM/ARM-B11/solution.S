/* ARM-B11 부동소수점·SIMD 레지스터 기초: V/D/S, FCVT  (AArch64, 맥 M-칩 네이티브)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 harness.c starter.S shared_abi.S -o st && ./st   (solution.S 도 같은 방식)
 * FP 인자: s0..s7 / d0..d7 (정수 인자 x0..x7 과 번호를 따로 센다), 반환: s0 / d0 / w0 / x0
 * callee-saved: d8..d15 의 하위 64비트만. 나머지 v 레지스터는 마음대로 써도 된다.
 */
#ifdef __APPLE__
#define SYM(x) _##x
#define FTYPE(x)
#define FSIZE(x)
#else
#define SYM(x) x
#define FTYPE(x) .type x, %function
#define FSIZE(x) .size x, .-x
#endif

    .text

/* ================= Part A — 레지스터 뷰, FP 인자, 기본 변환 ================= */

// void vreg_views(uint64_t out[4])  : s 쓰기 vs 원소 삽입(INS)이 v 레지스터 상위 비트에 하는 일
    .globl SYM(vreg_views)
    FTYPE(vreg_views)
    .p2align 2
SYM(vreg_views):
    movi    v0.2d, #0xffffffffffffffff  // v0 = 128비트 전부 1
    fmov    s0, #1.0                    // 스칼라 s0 쓰기 → v0[127:32] = 0
    fmov    x1, d0                      // x1 = v0[63:0]
    mov     x2, v0.d[1]                 // x2 = v0[127:64]
    stp     x1, x2, [x0]                // out[0], out[1]
    movi    v1.2d, #0xffffffffffffffff  // v1 = 전부 1
    movz    w8, #0x3f80, lsl #16        // w8 = 0x3F800000 (1.0f 의 비트)
    mov     v1.s[0], w8                 // 원소 삽입: lane 0 만 바뀌고 나머지 보존
    fmov    x1, d1
    mov     x2, v1.d[1]
    stp     x1, x2, [x0, #16]           // out[2], out[3]
    ret
    FSIZE(vreg_views)

// float dot3(const float *a, const float *b) = fma(a2,b2, fma(a1,b1, a0*b0))
    .globl SYM(dot3)
    FTYPE(dot3)
    .p2align 2
SYM(dot3):
    ldr     s0, [x0]                    // s0 = a[0]
    ldr     s1, [x1]                    // s1 = b[0]
    fmul    s0, s0, s1                  // acc = a0*b0        (반올림 1회)
    ldr     s2, [x0, #4]                // a[1]
    ldr     s3, [x1, #4]                // b[1]
    fmadd   s0, s2, s3, s0              // acc = a1*b1 + acc  (곱+합 반올림 1회)
    ldr     s2, [x0, #8]                // a[2]
    ldr     s3, [x1, #8]                // b[2]
    fmadd   s0, s2, s3, s0              // acc = a2*b2 + acc
    ret
    FSIZE(dot3)

// double dot3d(const double *a, const double *b)  — 같은 모양, d 뷰 + 8바이트 간격
    .globl SYM(dot3d)
    FTYPE(dot3d)
    .p2align 2
SYM(dot3d):
    ldr     d0, [x0]
    ldr     d1, [x1]
    fmul    d0, d0, d1
    ldr     d2, [x0, #8]
    ldr     d3, [x1, #8]
    fmadd   d0, d2, d3, d0
    ldr     d2, [x0, #16]
    ldr     d3, [x1, #16]
    fmadd   d0, d2, d3, d0
    ret
    FSIZE(dot3d)

// float lerp(float a, float b, float t) = a + t*(b - a)    a=s0 b=s1 t=s2
    .globl SYM(lerp)
    FTYPE(lerp)
    .p2align 2
SYM(lerp):
    fsub    s1, s1, s0                  // s1 = b - a         (여기서 반올림!)
    fmadd   s0, s2, s1, s0              // s0 = t*(b-a) + a
    ret
    FSIZE(lerp)

// double mix_args(int32_t i, float f, int64_t l, double d) = l*d + (double)i*(double)f
//   i=w0  f=s0  l=x1  d=d1   ← 정수 카운터와 FP 카운터가 따로 간다
    .globl SYM(mix_args)
    FTYPE(mix_args)
    .p2align 2
SYM(mix_args):
    scvtf   d2, w0                      // (double)i   (정확)
    fcvt    d3, s0                      // (double)f   (정확)
    fmul    d2, d2, d3                  // i*f         (반올림)
    scvtf   d3, x1                      // (double)l   (|l| > 2^53 이면 반올림)
    fmadd   d0, d3, d1, d2              // l*d + i*f
    ret
    FSIZE(mix_args)

// int32_t f2i_sat(float x)  : 0 쪽으로 자르고, 범위 밖은 포화, NaN → 0
    .globl SYM(f2i_sat)
    FTYPE(f2i_sat)
    .p2align 2
SYM(f2i_sat):
    fcvtzs  w0, s0
    ret
    FSIZE(f2i_sat)

// int64_t d2l_sat(double x)
    .globl SYM(d2l_sat)
    FTYPE(d2l_sat)
    .p2align 2
SYM(d2l_sat):
    fcvtzs  x0, d0
    ret
    FSIZE(d2l_sat)

// uint32_t f2u_sat(float x)  : 음수 → 0, 2^32 이상 → 0xFFFFFFFF, NaN → 0
    .globl SYM(f2u_sat)
    FTYPE(f2u_sat)
    .p2align 2
SYM(f2u_sat):
    fcvtzu  w0, s0
    ret
    FSIZE(f2u_sat)

// float i2f(int32_t)   float u2f(uint32_t)   double l2d(int64_t)   double ul2d(uint64_t)
    .globl SYM(i2f)
    FTYPE(i2f)
    .p2align 2
SYM(i2f):
    scvtf   s0, w0                      // signed → float (FPCR 반올림 = 기본 RNE)
    ret
    FSIZE(i2f)

    .globl SYM(u2f)
    FTYPE(u2f)
    .p2align 2
SYM(u2f):
    ucvtf   s0, w0                      // unsigned → float
    ret
    FSIZE(u2f)

    .globl SYM(l2d)
    FTYPE(l2d)
    .p2align 2
SYM(l2d):
    scvtf   d0, x0
    ret
    FSIZE(l2d)

    .globl SYM(ul2d)
    FTYPE(ul2d)
    .p2align 2
SYM(ul2d):
    ucvtf   d0, x0
    ret
    FSIZE(ul2d)

/* ================= Part B — 경계: 반올림 모드, t=1 정확성 ================= */

// float lerp_exact(float a, float b, float t) = (a - t*a) + t*b   (fused 2번)
    .globl SYM(lerp_exact)
    FTYPE(lerp_exact)
    .p2align 2
SYM(lerp_exact):
    fmsub   s3, s2, s0, s0              // s3 = a - t*a   (t=1 이면 정확히 0)
    fmadd   s0, s2, s1, s3              // s0 = t*b + s3  (t=1 이면 정확히 b)
    ret
    FSIZE(lerp_exact)

// int32_t cvt_ns/ms/ps/as(float) : 반올림 방향만 다른 FCVT 4형제 (모두 포화, NaN → 0)
    .globl SYM(cvt_ns)
    FTYPE(cvt_ns)
    .p2align 2
SYM(cvt_ns):
    fcvtns  w0, s0                      // nearest, ties to even
    ret
    FSIZE(cvt_ns)

    .globl SYM(cvt_ms)
    FTYPE(cvt_ms)
    .p2align 2
SYM(cvt_ms):
    fcvtms  w0, s0                      // toward −∞ (floor)
    ret
    FSIZE(cvt_ms)

    .globl SYM(cvt_ps)
    FTYPE(cvt_ps)
    .p2align 2
SYM(cvt_ps):
    fcvtps  w0, s0                      // toward +∞ (ceil)
    ret
    FSIZE(cvt_ps)

    .globl SYM(cvt_as)
    FTYPE(cvt_as)
    .p2align 2
SYM(cvt_as):
    fcvtas  w0, s0                      // nearest, ties away from zero (roundf)
    ret
    FSIZE(cvt_as)

/* ================= Part C — 펌웨어: 고정소수점, ADC, 필터 콜백 ================= */

// int32_t float_to_q15_sat(float x) : Q15 (int16 범위 [-32768, 32767]) 로 포화 변환
    .globl SYM(float_to_q15_sat)
    FTYPE(float_to_q15_sat)
    .p2align 2
SYM(float_to_q15_sat):
    fcvtzs  w0, s0, #15                 // trunc(x * 2^15), int32 포화, NaN → 0
    mov     w1, #32767
    cmp     w0, w1
    csel    w0, w0, w1, lt              // w0 = min(w0, 32767)
    mov     w1, #-32768
    cmp     w0, w1
    csel    w0, w0, w1, gt              // w0 = max(w0, -32768)
    ret
    FSIZE(float_to_q15_sat)

// int32_t float_to_q16(float x) : Q16.16, int32 포화
    .globl SYM(float_to_q16)
    FTYPE(float_to_q16)
    .p2align 2
SYM(float_to_q16):
    fcvtzs  w0, s0, #16                 // trunc(x * 65536)
    ret
    FSIZE(float_to_q16)

// float q16_to_float(int32_t q) : q / 65536
    .globl SYM(q16_to_float)
    FTYPE(q16_to_float)
    .p2align 2
SYM(q16_to_float):
    scvtf   s0, w0, #16                 // (float)(q * 2^-16), 반올림 1회
    ret
    FSIZE(q16_to_float)

// float adc_to_volts(uint32_t raw12, float vref) = raw/4096 * vref   raw=w0 vref=s0
    .globl SYM(adc_to_volts)
    FTYPE(adc_to_volts)
    .p2align 2
SYM(adc_to_volts):
    ucvtf   s1, w0, #12                 // s1 = raw / 2^12  (raw < 2^24 이면 정확)
    fmul    s0, s1, s0                  // * vref
    ret
    FSIZE(adc_to_volts)

// float ema_step(float (*read)(void), float y, float alpha) = y + alpha*(read() - y)
//   비-리프: y, alpha 를 콜백 너머로 살려야 한다 → callee-saved d8/d9 에 두고 저장/복원
    .globl SYM(ema_step)
    FTYPE(ema_step)
    .p2align 2
SYM(ema_step):
    stp     x29, x30, [sp, #-32]!       // 프레임 + LR 저장 (B08), sp 16B 정렬 유지
    mov     x29, sp
    stp     d8, d9, [sp, #16]           // 내가 쓸 callee-saved 의 하위 64비트 저장
    fmov    s8, s0                      // y     → s8 (콜백이 지우지 못함)
    fmov    s9, s1                      // alpha → s9
    blr     x0                          // s0 = read()   (v0-v7, v16-v31 은 파괴될 수 있음)
    fsub    s0, s0, s8                  // x - y
    fmadd   s0, s9, s0, s8              // y + alpha*(x - y)
    ldp     d8, d9, [sp, #16]           // 복원 (주의: ldp d 는 v8/v9 상위 64비트를 0으로)
    ldp     x29, x30, [sp], #32
    ret
    FSIZE(ema_step)

#ifndef __APPLE__
    .section .note.GNU-stack,"",%progbits
#endif
