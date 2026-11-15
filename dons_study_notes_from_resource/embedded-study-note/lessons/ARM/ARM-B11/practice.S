/* ARM-B11 추가 연습 답 (asm 부분) — practice.c 가 테스트한다
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_abi.S -o pr && ./pr
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

// 연습 1. uint32_t f2i_flags(float x, int32_t *out) : FCVTZS 후 FPSR 누적 예외 비트를 돌려준다
//   FPSR: bit0 IOC(invalid) bit1 DZC bit2 OFC bit3 UFC bit4 IXC(inexact) bit7 IDC
    .globl SYM(f2i_flags)
    FTYPE(f2i_flags)
    .p2align 2
SYM(f2i_flags):
    msr     fpsr, xzr                   // 누적(sticky) 플래그 지우기
    fcvtzs  w8, s0
    str     w8, [x0]                    // *out = 결과
    mrs     x9, fpsr                    // 플래그 읽기 (EL0 에서 허용)
    mov     w10, #0x9f                  // 0x9f(1001_1111)는 논리 즉시값이 안 됨 (ARM-B03)
    and     w0, w9, w10                 // 예외 비트만
    ret
    FSIZE(f2i_flags)

// 연습 2. float fmin_arm(float, float) / fminnm_arm(float, float) : NaN 처리 차이
    .globl SYM(fmin_arm)
    FTYPE(fmin_arm)
    .p2align 2
SYM(fmin_arm):
    fmin    s0, s0, s1                  // NaN 이 하나라도 있으면 NaN
    ret
    FSIZE(fmin_arm)

    .globl SYM(fminnm_arm)
    FTYPE(fminnm_arm)
    .p2align 2
SYM(fminnm_arm):
    fminnm  s0, s0, s1                  // quiet NaN 은 무시하고 숫자 쪽 (= C fminf)
    ret
    FSIZE(fminnm_arm)

// 연습 3. uint32_t duty_to_ccr(float duty, uint32_t arr) = clamp(round_away(duty*arr), 0, arr)
//   duty=s0  arr=w0
    .globl SYM(duty_to_ccr)
    FTYPE(duty_to_ccr)
    .p2align 2
SYM(duty_to_ccr):
    ucvtf   s1, w0                      // (float)arr
    fmul    s0, s0, s1                  // duty * arr
    fcvtau  w8, s0                      // ties-away 반올림, 음수/NaN → 0, 큰 값 → UINT32_MAX
    cmp     w8, w0
    csel    w0, w8, w0, ls              // min(w8, arr)  (부호 없는 비교 LS)
    ret
    FSIZE(duty_to_ccr)

// 연습 4. float fabs_gpr(float) / copysign_gpr(float mag, float sgn) : GPR 로 옮겨 비트 연산
    .globl SYM(fabs_gpr)
    FTYPE(fabs_gpr)
    .p2align 2
SYM(fabs_gpr):
    fmov    w8, s0                      // 비트 그대로 GPR 로 (변환 아님)
    and     w8, w8, #0x7fffffff         // 부호 비트 지우기
    fmov    s0, w8
    ret
    FSIZE(fabs_gpr)

    .globl SYM(copysign_gpr)
    FTYPE(copysign_gpr)
    .p2align 2
SYM(copysign_gpr):
    fmov    w8, s0                      // mag
    fmov    w9, s1                      // sgn
    bfxil   w9, w8, #0, #31             // w9[30:0] = w8[30:0], w9[31] (부호) 유지
    fmov    s0, w9
    ret
    FSIZE(copysign_gpr)

// 연습 5 비교용: 하드웨어 FCVTZS (C 의 soft_fcvtzs 와 대조)
    .globl SYM(fcvtzs_hw)
    FTYPE(fcvtzs_hw)
    .p2align 2
SYM(fcvtzs_hw):
    fcvtzs  w0, s0
    ret
    FSIZE(fcvtzs_hw)

#ifndef __APPLE__
    .section .note.GNU-stack,"",%progbits
#endif
