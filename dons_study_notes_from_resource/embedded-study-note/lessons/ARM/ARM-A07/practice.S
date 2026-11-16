/* ARM-A07 추가 연습 — asm 부분 (practice.c 가 테스트한다)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-A07_practice
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

// 연습 1: FPSR.QC (bit 27) — NEON 포화 명령이 포화하면 1 이 되고, 지울 때까지 남는다 (sticky)
// uint32_t qc_get(void)
    .globl SYM(qc_get)
    FTYPE(qc_get)
    .p2align 2
SYM(qc_get):
    mrs     x0, fpsr
    ubfx    x0, x0, #27, #1             // QC 비트만
    ret
    FSIZE(qc_get)

// void qc_clear(void)
    .globl SYM(qc_clear)
    FTYPE(qc_clear)
    .p2align 2
SYM(qc_clear):
    mrs     x0, fpsr
    bic     x0, x0, #(1 << 27)          // QC 만 0 (IOC 등 다른 누적 플래그는 그대로)
    msr     fpsr, x0
    ret
    FSIZE(qc_clear)

// 연습 3: 값싼 16비트 누산 FIR — acc16 = SQADD.8h(acc16, SQDMULH(x, h[k])), y = acc16
// void fir16_asm(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y)   (T >= 1)
    .globl SYM(fir16_asm)
    FTYPE(fir16_asm)
    .p2align 2
SYM(fir16_asm):
    sub     x6, x3, #1
    add     x6, x0, x6, lsl #1          // &x[i+T-1]
    lsr     x7, x1, #3
    cbz     x7, 3f
1:  movi    v4.8h, #0
    mov     x8, x6
    mov     x9, x2
    mov     x10, x3
2:  ld1     {v0.8h}, [x8]
    sub     x8, x8, #2
    ld1     {v1.h}[0], [x9], #2         // h[k] → lane 0
    sqdmulh v2.8h, v0.8h, v1.h[0]       // by-element 형태: 복제 없이 lane 0 을 곱한다
    sqadd   v4.8h, v4.8h, v2.8h         // 16비트 포화 누적
    subs    x10, x10, #1
    b.ne    2b
    st1     {v4.8h}, [x4], #16
    add     x6, x6, #16
    subs    x7, x7, #1
    b.ne    1b
3:  ands    x7, x1, #7
    b.eq    6f
4:  movi    d4, #0
    mov     x8, x6
    mov     x9, x2
    mov     x10, x3
5:  ldr     h0, [x8]
    sub     x8, x8, #2
    ldr     h1, [x9], #2
    sqdmulh h2, h0, h1
    sqadd   h4, h4, h2
    subs    x10, x10, #1
    b.ne    5b
    str     h4, [x4], #2
    add     x6, x6, #2
    subs    x7, x7, #1
    b.ne    4b
6:  ret
    FSIZE(fir16_asm)
