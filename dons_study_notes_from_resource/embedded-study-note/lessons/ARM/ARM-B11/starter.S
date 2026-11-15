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
    // TODO: v0 를 전부 1로 → fmov s0, #1.0 → out[0..1] = v0 의 하위/상위 64비트
    //       v1 을 전부 1로 → mov v1.s[0], w8(=0x3F800000) → out[2..3]
    ret
    FSIZE(vreg_views)

// float dot3(const float *a, const float *b) = fma(a2,b2, fma(a1,b1, a0*b0))
    .globl SYM(dot3)
    FTYPE(dot3)
    .p2align 2
SYM(dot3):
    fmov    s0, wzr                     // TODO: ldr s / fmul / fmadd
    ret
    FSIZE(dot3)

// double dot3d(const double *a, const double *b)  — 같은 모양, d 뷰 + 8바이트 간격
    .globl SYM(dot3d)
    FTYPE(dot3d)
    .p2align 2
SYM(dot3d):
    fmov    d0, xzr                     // TODO
    ret
    FSIZE(dot3d)

// float lerp(float a, float b, float t) = a + t*(b - a)    a=s0 b=s1 t=s2
    .globl SYM(lerp)
    FTYPE(lerp)
    .p2align 2
SYM(lerp):
    fmov    s0, wzr                     // TODO: fsub + fmadd
    ret
    FSIZE(lerp)

// double mix_args(int32_t i, float f, int64_t l, double d) = l*d + (double)i*(double)f
//   i=w0  f=s0  l=x1  d=d1   ← 정수 카운터와 FP 카운터가 따로 간다
    .globl SYM(mix_args)
    FTYPE(mix_args)
    .p2align 2
SYM(mix_args):
    fmov    d0, xzr                     // TODO: scvtf / fcvt / fmul / fmadd
    ret
    FSIZE(mix_args)

// int32_t f2i_sat(float x)  : 0 쪽으로 자르고, 범위 밖은 포화, NaN → 0
    .globl SYM(f2i_sat)
    FTYPE(f2i_sat)
    .p2align 2
SYM(f2i_sat):
    mov     w0, #0                      // TODO
    ret
    FSIZE(f2i_sat)

// int64_t d2l_sat(double x)
    .globl SYM(d2l_sat)
    FTYPE(d2l_sat)
    .p2align 2
SYM(d2l_sat):
    mov     x0, #0                      // TODO
    ret
    FSIZE(d2l_sat)

// uint32_t f2u_sat(float x)  : 음수 → 0, 2^32 이상 → 0xFFFFFFFF, NaN → 0
    .globl SYM(f2u_sat)
    FTYPE(f2u_sat)
    .p2align 2
SYM(f2u_sat):
    mov     w0, #0                      // TODO
    ret
    FSIZE(f2u_sat)

// float i2f(int32_t)   float u2f(uint32_t)   double l2d(int64_t)   double ul2d(uint64_t)
    .globl SYM(i2f)
    FTYPE(i2f)
    .p2align 2
SYM(i2f):
    fmov    s0, wzr                     // TODO
    ret
    FSIZE(i2f)

    .globl SYM(u2f)
    FTYPE(u2f)
    .p2align 2
SYM(u2f):
    fmov    s0, wzr                     // TODO
    ret
    FSIZE(u2f)

    .globl SYM(l2d)
    FTYPE(l2d)
    .p2align 2
SYM(l2d):
    fmov    d0, xzr                     // TODO
    ret
    FSIZE(l2d)

    .globl SYM(ul2d)
    FTYPE(ul2d)
    .p2align 2
SYM(ul2d):
    fmov    d0, xzr                     // TODO
    ret
    FSIZE(ul2d)

/* ================= Part B — 경계: 반올림 모드, t=1 정확성 ================= */

// float lerp_exact(float a, float b, float t) = (a - t*a) + t*b   (fused 2번)
    .globl SYM(lerp_exact)
    FTYPE(lerp_exact)
    .p2align 2
SYM(lerp_exact):
    fmov    s0, wzr                     // TODO: fmsub + fmadd
    ret
    FSIZE(lerp_exact)

// int32_t cvt_ns/ms/ps/as(float) : 반올림 방향만 다른 FCVT 4형제 (모두 포화, NaN → 0)
    .globl SYM(cvt_ns)
    FTYPE(cvt_ns)
    .p2align 2
SYM(cvt_ns):
    mov     w0, #0                      // TODO
    ret
    FSIZE(cvt_ns)

    .globl SYM(cvt_ms)
    FTYPE(cvt_ms)
    .p2align 2
SYM(cvt_ms):
    mov     w0, #0                      // TODO
    ret
    FSIZE(cvt_ms)

    .globl SYM(cvt_ps)
    FTYPE(cvt_ps)
    .p2align 2
SYM(cvt_ps):
    mov     w0, #0                      // TODO
    ret
    FSIZE(cvt_ps)

    .globl SYM(cvt_as)
    FTYPE(cvt_as)
    .p2align 2
SYM(cvt_as):
    mov     w0, #0                      // TODO
    ret
    FSIZE(cvt_as)

/* ================= Part C — 펌웨어: 고정소수점, ADC, 필터 콜백 ================= */

// int32_t float_to_q15_sat(float x) : Q15 (int16 범위 [-32768, 32767]) 로 포화 변환
    .globl SYM(float_to_q15_sat)
    FTYPE(float_to_q15_sat)
    .p2align 2
SYM(float_to_q15_sat):
    mov     w0, #0                      // TODO: fcvtzs w0, s0, #15 + int16 범위로 clamp
    ret
    FSIZE(float_to_q15_sat)

// int32_t float_to_q16(float x) : Q16.16, int32 포화
    .globl SYM(float_to_q16)
    FTYPE(float_to_q16)
    .p2align 2
SYM(float_to_q16):
    mov     w0, #0                      // TODO
    ret
    FSIZE(float_to_q16)

// float q16_to_float(int32_t q) : q / 65536
    .globl SYM(q16_to_float)
    FTYPE(q16_to_float)
    .p2align 2
SYM(q16_to_float):
    fmov    s0, wzr                     // TODO
    ret
    FSIZE(q16_to_float)

// float adc_to_volts(uint32_t raw12, float vref) = raw/4096 * vref   raw=w0 vref=s0
    .globl SYM(adc_to_volts)
    FTYPE(adc_to_volts)
    .p2align 2
SYM(adc_to_volts):
    fmov    s0, wzr                     // TODO
    ret
    FSIZE(adc_to_volts)

// float ema_step(float (*read)(void), float y, float alpha) = y + alpha*(read() - y)
//   비-리프: y, alpha 를 콜백 너머로 살려야 한다 → callee-saved d8/d9 에 두고 저장/복원
    .globl SYM(ema_step)
    FTYPE(ema_step)
    .p2align 2
SYM(ema_step):
    fmov    s0, wzr                     // TODO: 프레임, d8/d9 저장, blr x0, 계산, 복원
    ret
    FSIZE(ema_step)

#ifndef __APPLE__
    .section .note.GNU-stack,"",%progbits
#endif
