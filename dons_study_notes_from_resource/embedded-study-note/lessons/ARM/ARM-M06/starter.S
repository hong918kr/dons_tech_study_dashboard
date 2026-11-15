/* ARM-M06 NEON 기초: 벡터 덧셈·내적·리덕션 — 손으로 쓴 AArch64 NEON (맥 M-칩 네이티브, ELF 도 같은 파일)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c solution.S shared_bugs.S -o sol && ./sol
 * 인자: x0..x3 (포인터, size_t n), float 반환 s0, 정수 반환 w0/x0.
 * v0..v7, v16..v31 은 마음대로 써도 된다. v8..v15 는 하위 64비트가 callee-saved → 이 파일은 안 쓴다.
 * SDOT 은 FEAT_DotProd (ARMv8.2 선택, v8.4 필수). Apple 기본 타깃은 이미 포함, 리눅스 generic armv8-a 는 아님
 * → .arch_extension dotprod 로 이 파일에서만 켠다 (플래그 없이 빌드되게).
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

    .arch_extension dotprod
    .text

/* ================= Part A — 벡터 덧셈 / float 내적 / int8 내적 ================= */

// void vadd_f32_asm(float *dst, const float *a, const float *b, size_t n)   dst[i] = a[i] + b[i]
    .globl SYM(vadd_f32_asm)
    FTYPE(vadd_f32_asm)
    .p2align 2
SYM(vadd_f32_asm):
    // TODO: 4개씩 ld1 {v.4s} ×2 → fadd v.4s → st1, 남은 0..3개는 ldr s / fadd s / str s 꼬리
    ret                                 // 스텁: dst 를 건드리지 않는다
    FSIZE(vadd_f32_asm)

// float dot_f32_asm(const float *a, const float *b, size_t n)   Σ a[i]*b[i] (lane 4개로 재결합됨)
    .globl SYM(dot_f32_asm)
    FTYPE(dot_f32_asm)
    .p2align 2
SYM(dot_f32_asm):
    // TODO: movi 로 acc=0 → ld1 ×2 + fmla v.4s 루프 → faddp ×2 로 가로 합 → fmadd 꼬리
    movi    v0.16b, #0                  // 스텁: 0.0f
    ret
    FSIZE(dot_f32_asm)

// int32_t dot_s8_asm(const int8_t *a, const int8_t *b, size_t n)   계약: n <= 131071 (결과가 int32 에 들어감)
    .globl SYM(dot_s8_asm)
    FTYPE(dot_s8_asm)
    .p2align 2
SYM(dot_s8_asm):
    // TODO: acc=0 → ld1 {v.16b} ×2 + sdot v.4s 루프 → addv s → fmov w → ldrsb/madd 꼬리
    mov     w0, #0                      // 스텁: 0
    ret
    FSIZE(dot_s8_asm)

/* ================= Part B — 오버플로 지평선: int32 lane 을 주기적으로 int64 로 비운다 ================= */

// int64_t dot_s8_long_asm(const int8_t *a, const int8_t *b, size_t n)   어떤 n 이든 정확
// lane 하나는 SDOT 1회에 곱 4개를 받는다. 최악 곱 (-128)(-128) = 16384, int32 lane 은 131071개까지 안전
// → 블록 = 131071/4 = 32767 회 (= 524272 바이트) 마다 SADALP 로 int64 x2 누산기에 비우고 0으로.
    .globl SYM(dot_s8_long_asm)
    FTYPE(dot_s8_long_asm)
    .p2align 2
SYM(dot_s8_long_asm):
    // TODO: 블록(32767회 SDOT)마다 sadalp v3.2d, v0.4s 로 비우기 → addp d → smaddl 꼬리
    mov     x0, #0                      // 스텁: 0
    ret
    FSIZE(dot_s8_long_asm)

#ifndef __APPLE__
    .section .note.GNU-stack,"",%progbits
#endif
