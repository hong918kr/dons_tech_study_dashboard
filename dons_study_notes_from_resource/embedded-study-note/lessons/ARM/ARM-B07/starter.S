/* ARM-B07 곱셈·나눗셈: MADD, SMULL, UMULH, SDIV  (AArch64, Apple Mach-O + Linux ELF)
 *
 * build (Mac): cc -std=c11 -Wall -Wextra -Werror -O2 harness.c {starter|solution}.S -o /tmp/ARM-B07_v
 * 모든 함수는 leaf: 스택을 안 쓰고 x0-x7 인자 / x8-x15 임시 레지스터만 쓴다.
 */
#ifdef __APPLE__
#  define SYM(n)      _##n                 /* Mach-O: C 이름 앞에 '_' */
#  define TYPE_FN(n)                       /* Mach-O 에는 .type/.size 가 없다 */
#  define SIZE_FN(n)
#else
#  define SYM(n)      n                    /* ELF: 이름 그대로 */
#  define TYPE_FN(n)  .type n, %function
#  define SIZE_FN(n)  .size n, . - n
#endif

    .text

/* ===================== Part A: 곱셈 ===================== */

/* void mul128_u(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo) */
    .globl SYM(mul128_u)
    TYPE_FN(mul128_u)
    .p2align 2
SYM(mul128_u):
    // TODO: mul 로 하위 64비트, umulh 로 상위 64비트 → *hi([x2]), *lo([x3])
    ret
    SIZE_FN(mul128_u)

/* void mul128_s(int64_t a, int64_t b, int64_t *hi, uint64_t *lo) */
    .globl SYM(mul128_s)
    TYPE_FN(mul128_s)
    .p2align 2
SYM(mul128_s):
    // TODO: mul + smulh → *hi([x2]), *lo([x3])
    ret
    SIZE_FN(mul128_s)

/* uint64_t mulhi_u64(uint64_t a, uint64_t b) — 128비트 곱의 상위 절반만 */
    .globl SYM(mulhi_u64)
    TYPE_FN(mulhi_u64)
    .p2align 2
SYM(mulhi_u64):
    // TODO: umulh
    mov     x0, #0              // stub
    ret
    SIZE_FN(mulhi_u64)

/* uint64_t umull32(uint32_t a, uint32_t b) — 32x32 -> 64 unsigned */
    .globl SYM(umull32)
    TYPE_FN(umull32)
    .p2align 2
SYM(umull32):
    // TODO: umull (32x32 -> 64)
    mov     x0, #0              // stub
    ret
    SIZE_FN(umull32)

/* int64_t smull32(int32_t a, int32_t b) — 32x32 -> 64 signed */
    .globl SYM(smull32)
    TYPE_FN(smull32)
    .p2align 2
SYM(smull32):
    // TODO: smull (32x32 -> 64, signed)
    mov     x0, #0              // stub
    ret
    SIZE_FN(smull32)

/* uint64_t madd_u64(uint64_t a, uint64_t b, uint64_t c) = c + a*b (mod 2^64) */
    .globl SYM(madd_u64)
    TYPE_FN(madd_u64)
    .p2align 2
SYM(madd_u64):
    // TODO: madd — operand 순서 주의
    mov     x0, #0              // stub
    ret
    SIZE_FN(madd_u64)

/* uint64_t msub_u64(uint64_t a, uint64_t b, uint64_t c) = c - a*b (mod 2^64) */
    .globl SYM(msub_u64)
    TYPE_FN(msub_u64)
    .p2align 2
SYM(msub_u64):
    // TODO: msub
    mov     x0, #0              // stub
    ret
    SIZE_FN(msub_u64)

/* int64_t mneg_s64(int64_t a, int64_t b) = -(a*b), 2^64 로 wrap */
    .globl SYM(mneg_s64)
    TYPE_FN(mneg_s64)
    .p2align 2
SYM(mneg_s64):
    // TODO: mneg
    mov     x0, #0              // stub
    ret
    SIZE_FN(mneg_s64)

/* ===================== Part B: 나눗셈과 나머지 ===================== */

/* uint64_t udiv_u64(uint64_t a, uint64_t b) — ISA: b == 0 이면 0 */
    .globl SYM(udiv_u64)
    TYPE_FN(udiv_u64)
    .p2align 2
SYM(udiv_u64):
    // TODO: udiv
    mov     x0, #0              // stub
    ret
    SIZE_FN(udiv_u64)

/* int64_t sdiv_s64(int64_t a, int64_t b) — ISA: b == 0 → 0, INT64_MIN / -1 → INT64_MIN */
    .globl SYM(sdiv_s64)
    TYPE_FN(sdiv_s64)
    .p2align 2
SYM(sdiv_s64):
    // TODO: sdiv
    mov     x0, #0              // stub
    ret
    SIZE_FN(sdiv_s64)

/* int32_t sdiv_s32(int32_t a, int32_t b) — w 레지스터판 */
    .globl SYM(sdiv_s32)
    TYPE_FN(sdiv_s32)
    .p2align 2
SYM(sdiv_s32):
    // TODO: sdiv (w 레지스터)
    mov     w0, #0              // stub
    ret
    SIZE_FN(sdiv_s32)

/* uint64_t mod_u64(uint64_t a, uint64_t b) = a - (a/b)*b   (b == 0 → a) */
    .globl SYM(mod_u64)
    TYPE_FN(mod_u64)
    .p2align 2
SYM(mod_u64):
    // TODO: udiv 로 q, msub 로 a - q*b
    mov     x0, #0              // stub
    ret
    SIZE_FN(mod_u64)

/* int64_t mod_s64(int64_t a, int64_t b) — 부호는 피제수(a)를 따름 (C99 와 같음) */
    .globl SYM(mod_s64)
    TYPE_FN(mod_s64)
    .p2align 2
SYM(mod_s64):
    // TODO: sdiv 로 q, msub 로 a - q*b
    mov     x0, #0              // stub
    ret
    SIZE_FN(mod_s64)

/* ---- 일부러 심은 버그 3개 (starter/solution 동일, 테스트가 잡아야 함) ---- */

/* 버그: signed 나머지에 udiv 를 썼다 */
    .globl SYM(mod_s64_bug)
    TYPE_FN(mod_s64_bug)
    .p2align 2
SYM(mod_s64_bug):
    udiv    x8, x0, x1          // BUG: 음수를 거대한 unsigned 로 본다
    msub    x0, x8, x1, x0
    ret
    SIZE_FN(mod_s64_bug)

/* 버그: signed 상위 64비트에 umulh 를 썼다 */
    .globl SYM(mulhi_s64_bug)
    TYPE_FN(mulhi_s64_bug)
    .p2align 2
SYM(mulhi_s64_bug):
    umulh   x0, x0, x1          // BUG: smulh 여야 한다
    ret
    SIZE_FN(mulhi_s64_bug)

/* 버그: 32x32 를 32비트 곱으로 — C 의 `uint64_t r = a32 * b32;` 와 같은 버그 */
    .globl SYM(umull32_bug)
    TYPE_FN(umull32_bug)
    .p2align 2
SYM(umull32_bug):
    mul     w0, w0, w1          // BUG: 하위 32비트만 남고 x0 상위 32비트는 0
    ret
    SIZE_FN(umull32_bug)

/* ===================== Part C: Q32.32 고정소수점 ===================== */

/* uint64_t q32_mul(uint64_t a, uint64_t b) = (a*b) >> 32  (128비트 곱의 가운데 64비트) */
    .globl SYM(q32_mul)
    TYPE_FN(q32_mul)
    .p2align 2
SYM(q32_mul):
    // TODO: mul + umulh + extr #32
    mov     x0, #0              // stub
    ret
    SIZE_FN(q32_mul)

/* uint64_t calc_mult(uint64_t freq_hz) = round(1e9 * 2^32 / freq_hz)  (Q32.32 ns/tick) */
    .globl SYM(calc_mult)
    TYPE_FN(calc_mult)
    .p2align 2
SYM(calc_mult):
    // TODO: (1e9<<32 + freq/2) / freq  — movz/movk, add ..., lsr #1, udiv
    mov     x0, #0              // stub
    ret
    SIZE_FN(calc_mult)

#ifndef __APPLE__
    .section .note.GNU-stack,"",%progbits   /* ELF: 실행 가능한 스택 불필요 표시 */
#endif
