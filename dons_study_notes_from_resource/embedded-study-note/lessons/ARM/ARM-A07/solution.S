/* ARM-A07 포화·고정소수점 — 손으로 쓴 AArch64 NEON (맥 M-칩 네이티브, 리눅스 ELF 도 같은 파일)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c solution.S shared_bugs.S -o /tmp/ARM-A07_solution
 * 인자: x0..x4, int16_t 인자는 w3 의 하위 16비트. 반환 없음.
 * v0..v7, v16..v31 만 쓴다 (v8..v15 하위 64비트는 callee-saved).
 * 명세 (solution.c 머리말): acc = SQDMLAL 누적 (탭 순서), y = SQRSHRN(acc, #16).
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

// void q15_scale_asm(int16_t *y, const int16_t *x, size_t n, int16_t g)   y[i] = SQRDMULH(x[i], g)
    .globl SYM(q15_scale_asm)
    FTYPE(q15_scale_asm)
    .p2align 2
SYM(q15_scale_asm):
    dup     v1.8h, w3                   // g 를 8 lane 에 복제 (h1 = lane 0 = g, 꼬리에서도 쓴다)
    lsr     x5, x2, #3                  // 8개 묶음 수
    cbz     x5, 2f
1:  ld1     {v0.8h}, [x1], #16
    sqrdmulh v0.8h, v0.8h, v1.8h        // (2·x·g + 2^15) >> 16, 포화
    st1     {v0.8h}, [x0], #16
    subs    x5, x5, #1
    b.ne    1b
2:  ands    x5, x2, #7                  // 꼬리 0..7 개
    b.eq    4f
3:  ldr     h0, [x1], #2
    sqrdmulh h0, h0, h1                 // 스칼라 형태 (Hd, Hn, Hm)
    str     h0, [x0], #2
    subs    x5, x5, #1
    b.ne    3b
4:  ret
    FSIZE(q15_scale_asm)

// void fir_q15_asm(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y)   (T >= 1)
//   y[i] = SQRSHRN( SQDMLAL 누적 k=0..T-1 of h[k]·x[i+T-1-k] , #16 )
//   lane j 가 출력 i+j 를 맡는다 → 탭 순서는 명세 그대로 (탭 방향으로 나누면 포화 때 틀린다)
    .globl SYM(fir_q15_asm)
    FTYPE(fir_q15_asm)
    .p2align 2
SYM(fir_q15_asm):
    sub     x6, x3, #1
    add     x6, x0, x6, lsl #1          // x6 = &x[i+T-1] (i = 0): 출력 i 의 가장 새 샘플
    lsr     x7, x1, #3                  // 8개 묶음 수
    cbz     x7, 3f
1:  movi    v4.4s, #0                   // acc lane 0..3
    movi    v5.4s, #0                   // acc lane 4..7
    mov     x8, x6                      // px = &x[i+T-1-k] (k = 0)
    mov     x9, x2                      // ph = &h[0]
    mov     x10, x3                     // k 카운터
2:  ld1     {v0.8h}, [x8]               // x[i+T-1-k .. +7]: lane j = 출력 i+j 의 입력
    sub     x8, x8, #2                  // k+1 → 한 샘플 과거
    ld1r    {v1.8h}, [x9], #2           // h[k] 를 8 lane 에 복제
    sqdmull  v2.4s, v0.4h, v1.4h        // sat32(2·x·h)  lane 0..3
    sqdmull2 v3.4s, v0.8h, v1.8h        //               lane 4..7
    sqadd   v4.4s, v4.4s, v2.4s         // sat32(acc + p)  ← 두 번째 포화
    sqadd   v5.4s, v5.4s, v3.4s
    subs    x10, x10, #1
    b.ne    2b
    sqrshrn  v6.4h, v4.4s, #16          // sat16((acc + 2^15) >> 16)
    sqrshrn2 v6.8h, v5.4s, #16
    st1     {v6.8h}, [x4], #16
    add     x6, x6, #16                 // i += 8
    subs    x7, x7, #1
    b.ne    1b
3:  ands    x7, x1, #7                  // 꼬리 출력 0..7 개: 스칼라 형태 (Sd, Hn, Hm)
    b.eq    6f
4:  movi    d4, #0
    mov     x8, x6
    mov     x9, x2
    mov     x10, x3
5:  ldr     h0, [x8]
    sub     x8, x8, #2
    ldr     h1, [x9], #2
    sqdmull s2, h0, h1
    sqadd   s4, s4, s2
    subs    x10, x10, #1
    b.ne    5b
    sqrshrn h6, s4, #16
    str     h6, [x4], #2
    add     x6, x6, #2
    subs    x7, x7, #1
    b.ne    4b
6:  ret
    FSIZE(fir_q15_asm)
