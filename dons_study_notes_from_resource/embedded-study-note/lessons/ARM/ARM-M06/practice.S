/* ARM-M06 추가 연습 답 (AArch64 NEON). 테스트는 practice.c
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o pr && ./pr
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

// 연습 1. float dot_f32_asm4(const float *a, const float *b, size_t n)
// 누산기 4개(v0..v3)로 FMLA 지연을 숨긴다: 16개/회, 그다음 4개/회, 그다음 스칼라 꼬리.
    .globl SYM(dot_f32_asm4)
    FTYPE(dot_f32_asm4)
    .p2align 2
SYM(dot_f32_asm4):
    movi    v0.16b, #0
    movi    v1.16b, #0
    movi    v2.16b, #0
    movi    v3.16b, #0
    cmp     x2, #16
    b.lo    2f
1:  ld1     {v4.4s, v5.4s, v6.4s, v7.4s}, [x0], #64     // a 16개 (레지스터 4개 한 번에)
    ld1     {v16.4s, v17.4s, v18.4s, v19.4s}, [x1], #64 // b 16개
    fmla    v0.4s, v4.4s, v16.4s        // 서로 독립인 체인 4개 → 앞 FMLA 결과를 기다리지 않는다
    fmla    v1.4s, v5.4s, v17.4s
    fmla    v2.4s, v6.4s, v18.4s
    fmla    v3.4s, v7.4s, v19.4s
    sub     x2, x2, #16
    cmp     x2, #16
    b.hs    1b
2:  cmp     x2, #4
    b.lo    3f
    ld1     {v4.4s}, [x0], #16          // 남은 4개 묶음 (최대 3번)
    ld1     {v16.4s}, [x1], #16
    fmla    v0.4s, v4.4s, v16.4s
    sub     x2, x2, #4
    b       2b
3:  fadd    v0.4s, v0.4s, v1.4s         // 누산기 4 → 1 (트리)
    fadd    v2.4s, v2.4s, v3.4s
    fadd    v0.4s, v0.4s, v2.4s
    faddp   v0.4s, v0.4s, v0.4s         // lane 4 → 1
    faddp   s0, v0.2s
    cbz     x2, 5f
4:  ldr     s1, [x0], #4
    ldr     s2, [x1], #4
    fmadd   s0, s1, s2, s0
    subs    x2, x2, #1
    b.ne    4b
5:  ret
    FSIZE(dot_f32_asm4)

// 연습 2. int32_t dot_s8_nodot(const int8_t *a, const int8_t *b, size_t n)   SDOT 없는 코어(Cortex-A53/A72)용
// SMULL/SMULL2: int8×int8 → int16 (|곱| <= 16384 라 int16 에 들어감), SADALP: int16 쌍 → int32 lane 에 누적.
    .globl SYM(dot_s8_nodot)
    FTYPE(dot_s8_nodot)
    .p2align 2
SYM(dot_s8_nodot):
    movi    v0.16b, #0
    cmp     x2, #16
    b.lo    2f
1:  ld1     {v1.16b}, [x0], #16
    ld1     {v2.16b}, [x1], #16
    smull   v3.8h, v1.8b, v2.8b         // 아래 8바이트 곱 → int16 x8
    smull2  v4.8h, v1.16b, v2.16b       // 위 8바이트 곱 → int16 x8
    sadalp  v0.4s, v3.8h                // acc[k] += p[2k] + p[2k+1]
    sadalp  v0.4s, v4.8h
    sub     x2, x2, #16
    cmp     x2, #16
    b.hs    1b
2:  addv    s0, v0.4s
    fmov    w3, s0
    cbz     x2, 4f
3:  ldrsb   w4, [x0], #1
    ldrsb   w5, [x1], #1
    madd    w3, w4, w5, w3
    subs    x2, x2, #1
    b.ne    3b
4:  mov     w0, w3
    ret
    FSIZE(dot_s8_nodot)

// 연습 3. float maxabs_f32(const float *x, size_t n)   max |x[i]|, n = 0 이면 0 (NaN 입력은 계약 밖)
    .globl SYM(maxabs_f32)
    FTYPE(maxabs_f32)
    .p2align 2
SYM(maxabs_f32):
    movi    v0.16b, #0                  // |x| >= 0 이므로 0 에서 시작
    cmp     x1, #4
    b.lo    2f
1:  ld1     {v1.4s}, [x0], #16
    fabs    v1.4s, v1.4s                // 부호 비트만 지움
    fmax    v0.4s, v0.4s, v1.4s         // lane 별 최댓값
    sub     x1, x1, #4
    cmp     x1, #4
    b.hs    1b
2:  fmaxv   s0, v0.4s                   // 가로 최댓값 (FADDV 는 없지만 FMAXV 는 있다)
    cbz     x1, 4f
3:  ldr     s1, [x0], #4
    fabs    s1, s1
    fmax    s0, s0, s1
    subs    x1, x1, #1
    b.ne    3b
4:  ret
    FSIZE(maxabs_f32)

// 연습 4. void saxpy_f32_asm(float *y, float a, const float *x, size_t n)   y[i] = fma(a, x[i], y[i])
// 인자: x0 = y, s0 = a, x1 = x, x2 = n (정수·FP 카운터가 따로라 n 은 x2)
    .globl SYM(saxpy_f32_asm)
    FTYPE(saxpy_f32_asm)
    .p2align 2
SYM(saxpy_f32_asm):
    cmp     x2, #4
    b.lo    2f
1:  ld1     {v1.4s}, [x0]               // y 4개 (포인터는 아직 안 움직임)
    ld1     {v2.4s}, [x1], #16          // x 4개
    fmla    v1.4s, v2.4s, v0.s[0]       // by-element: 스칼라 a 를 dup 없이 4 lane 에 곱함
    st1     {v1.4s}, [x0], #16
    sub     x2, x2, #4
    cmp     x2, #4
    b.hs    1b
2:  cbz     x2, 4f
3:  ldr     s1, [x0]
    ldr     s2, [x1], #4
    fmadd   s1, s0, s2, s1              // y = a*x + y (반올림 1회, fmaf 와 비트 동일)
    str     s1, [x0], #4
    subs    x2, x2, #1
    b.ne    3b
4:  ret
    FSIZE(saxpy_f32_asm)

#ifndef __APPLE__
    .section .note.GNU-stack,"",%progbits
#endif
