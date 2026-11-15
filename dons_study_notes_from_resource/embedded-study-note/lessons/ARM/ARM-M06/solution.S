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
    cmp     x3, #4                      // 4개 미만이면 바로 꼬리로
    b.lo    2f
1:  ld1     {v0.4s}, [x1], #16          // a[i..i+3] (정렬 불필요), x1 += 16
    ld1     {v1.4s}, [x2], #16          // b[i..i+3]
    fadd    v0.4s, v0.4s, v1.4s         // lane 4개 동시 덧셈
    st1     {v0.4s}, [x0], #16          // dst[i..i+3]
    sub     x3, x3, #4
    cmp     x3, #4
    b.hs    1b                          // 남은 게 4개 이상이면 반복
2:  cbz     x3, 4f                      // 꼬리 0..3개
3:  ldr     s0, [x1], #4                // 스칼라 꼬리: 한 원소씩
    ldr     s1, [x2], #4
    fadd    s0, s0, s1
    str     s0, [x0], #4
    subs    x3, x3, #1
    b.ne    3b
4:  ret
    FSIZE(vadd_f32_asm)

// float dot_f32_asm(const float *a, const float *b, size_t n)   Σ a[i]*b[i] (lane 4개로 재결합됨)
    .globl SYM(dot_f32_asm)
    FTYPE(dot_f32_asm)
    .p2align 2
SYM(dot_f32_asm):
    movi    v0.16b, #0                  // acc = {0,0,0,0}
    cmp     x2, #4
    b.lo    2f
1:  ld1     {v1.4s}, [x0], #16
    ld1     {v2.4s}, [x1], #16
    fmla    v0.4s, v1.4s, v2.4s         // acc[k] += a[i+k]*b[i+k] (lane 별 fused, 반올림 1회)
    sub     x2, x2, #4
    cmp     x2, #4
    b.hs    1b
2:  faddp   v0.4s, v0.4s, v0.4s         // {a0+a1, a2+a3, a0+a1, a2+a3}  (FADDV 는 없다)
    faddp   s0, v0.2s                   // s0 = (a0+a1) + (a2+a3)
    cbz     x2, 4f
3:  ldr     s1, [x0], #4                // 꼬리: 리덕션 결과에 이어서 스칼라 FMA
    ldr     s2, [x1], #4
    fmadd   s0, s1, s2, s0
    subs    x2, x2, #1
    b.ne    3b
4:  ret
    FSIZE(dot_f32_asm)

// int32_t dot_s8_asm(const int8_t *a, const int8_t *b, size_t n)   계약: n <= 131071 (결과가 int32 에 들어감)
    .globl SYM(dot_s8_asm)
    FTYPE(dot_s8_asm)
    .p2align 2
SYM(dot_s8_asm):
    movi    v0.16b, #0                  // acc = {0,0,0,0} (int32 x4)
    cmp     x2, #16
    b.lo    2f
1:  ld1     {v1.16b}, [x0], #16         // int8 x16
    ld1     {v2.16b}, [x1], #16
    sdot    v0.4s, v1.16b, v2.16b       // acc[k] += Σ_{j=0..3} a[4k+j]*b[4k+j]  (부호 있는 8비트 곱 4개 → int32)
    sub     x2, x2, #16
    cmp     x2, #16
    b.hs    1b
2:  addv    s0, v0.4s                   // 정수 lane 4개 합 (32비트, 감싸기)
    fmov    w3, s0                      // 벡터 부분 → GPR
    cbz     x2, 4f
3:  ldrsb   w4, [x0], #1                // 꼬리: 부호 확장 바이트 로드 (ldrb 쓰면 버그)
    ldrsb   w5, [x1], #1
    madd    w3, w4, w5, w3              // w3 += a*b
    subs    x2, x2, #1
    b.ne    3b
4:  mov     w0, w3
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
    movi    v3.16b, #0                  // acc64 = {0,0} (int64 x2)
5:  movi    v0.16b, #0                  // acc32 = {0,0,0,0}
    mov     x6, #32767                  // 이번 블록에 남은 SDOT 횟수
6:  cmp     x2, #16
    b.lo    7f
    cbz     x6, 7f                      // 블록 예산 소진 → 비우기
    ld1     {v1.16b}, [x0], #16
    ld1     {v2.16b}, [x1], #16
    sdot    v0.4s, v1.16b, v2.16b
    sub     x2, x2, #16
    sub     x6, x6, #1
    b       6b
7:  sadalp  v3.2d, v0.4s                // acc64[k] += acc32[2k] + acc32[2k+1] (부호 확장 후 덧셈)
    cmp     x2, #16
    b.hs    5b                          // 아직 16바이트 이상 남았으면 새 블록
    addp    d0, v3.2d                   // 64비트 lane 2개 합 (ADDV 는 .2d 가 없다)
    fmov    x3, d0
    cbz     x2, 9f
8:  ldrsb   w4, [x0], #1
    ldrsb   w5, [x1], #1
    smaddl  x3, w4, w5, x3              // x3 += (int64)a*b
    subs    x2, x2, #1
    b.ne    8b
9:  mov     x0, x3
    ret
    FSIZE(dot_s8_long_asm)

#ifndef __APPLE__
    .section .note.GNU-stack,"",%progbits
#endif
