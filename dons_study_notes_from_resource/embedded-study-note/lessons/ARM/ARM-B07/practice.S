/* ARM-B07 추가 연습 답 (AArch64, Apple Mach-O + Linux ELF)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S -o /tmp/ARM-B07_practice
 */
#ifdef __APPLE__
#  define SYM(n)      _##n
#  define TYPE_FN(n)
#  define SIZE_FN(n)
#else
#  define SYM(n)      n
#  define TYPE_FN(n)  .type n, %function
#  define SIZE_FN(n)  .size n, . - n
#endif

    .text

/* 연습 1: int64_t smulh_via_umulh(int64_t a, int64_t b) — SMULH 없이 signed 상위 64비트
 *   hi_s = umulh(a,b) - (a<0 ? b : 0) - (b<0 ? a : 0)   (mod 2^64) */
    .globl SYM(smulh_via_umulh)
    TYPE_FN(smulh_via_umulh)
    .p2align 2
SYM(smulh_via_umulh):
    umulh   x8, x0, x1          // unsigned 상위 64비트
    and     x9, x1, x0, asr #63 // a<0 이면 b, 아니면 0   (x0 asr 63 = 전부 1 또는 전부 0)
    sub     x8, x8, x9
    and     x9, x0, x1, asr #63 // b<0 이면 a, 아니면 0
    sub     x0, x8, x9
    ret
    SIZE_FN(smulh_via_umulh)

/* 연습 2: uint64_t udiv10(uint64_t x) — 나눗셈 없이 x/10 (magic number) */
    .globl SYM(udiv10)
    TYPE_FN(udiv10)
    .p2align 2
SYM(udiv10):
    mov     x8, #0xCCCCCCCCCCCCCCCC // 논리 즉시값으로 인코딩 가능 (패턴 반복, ARM-B03)
    movk    x8, #0xCCCD             // → 0xCCCCCCCCCCCCCCCD = ceil(2^67 / 10)
    umulh   x8, x0, x8              // (x * M) >> 64
    lsr     x0, x8, #3              // >> 3 더 → 합쳐서 >> 67
    ret
    SIZE_FN(udiv10)

/* 연습 3: int safe_sdiv(int64_t a, int64_t b, int64_t *q, int64_t *r)
 *   0 = OK, -1 = b == 0, -2 = INT64_MIN / -1 (오버플로). 에러면 *q, *r 를 건드리지 않는다. */
    .globl SYM(safe_sdiv)
    TYPE_FN(safe_sdiv)
    .p2align 2
SYM(safe_sdiv):
    cbz     x1, 1f              // b == 0 → -1
    cmn     x1, #1              // b + 1 == 0 ?  (b == -1)
    b.ne    2f
    mov     x8, #0x8000000000000000 // INT64_MIN
    cmp     x0, x8
    b.eq    3f                  // a == INT64_MIN && b == -1 → -2
2:  sdiv    x8, x0, x1          // q
    msub    x9, x8, x1, x0      // r = a - q*b
    str     x8, [x2]
    str     x9, [x3]
    mov     w0, #0
    ret
1:  mov     w0, #-1
    ret
3:  mov     w0, #-2
    ret
    SIZE_FN(safe_sdiv)

/* 연습 4a: int mul_overflow_u64(uint64_t a, uint64_t b, uint64_t *res) — 1 이면 넘침 */
    .globl SYM(mul_overflow_u64)
    TYPE_FN(mul_overflow_u64)
    .p2align 2
SYM(mul_overflow_u64):
    mul     x8, x0, x1          // 잘린 결과 (항상 저장: __builtin_mul_overflow 와 같은 계약)
    umulh   x9, x0, x1          // 상위 64비트가 0 이 아니면 넘침
    str     x8, [x2]
    cmp     x9, #0
    cset    w0, ne
    ret
    SIZE_FN(mul_overflow_u64)

/* 연습 4b: int mul_overflow_s64(int64_t a, int64_t b, int64_t *res) */
    .globl SYM(mul_overflow_s64)
    TYPE_FN(mul_overflow_s64)
    .p2align 2
SYM(mul_overflow_s64):
    mul     x8, x0, x1
    smulh   x9, x0, x1
    str     x8, [x2]
    cmp     x9, x8, asr #63     // hi 가 lo 의 부호 비트 확장(0 또는 -1)과 같아야 64비트에 들어간다
    cset    w0, ne
    ret
    SIZE_FN(mul_overflow_s64)

/* 연습 5: uint64_t q32_mul_round(uint64_t a, uint64_t b) = (a*b + 2^31) >> 32 (반올림) */
    .globl SYM(q32_mul_round)
    TYPE_FN(q32_mul_round)
    .p2align 2
SYM(q32_mul_round):
    mul     x8, x0, x1          // lo
    umulh   x9, x0, x1          // hi
    mov     x10, #0x80000000    // 2^31 (add 즉시값 12비트로는 못 넣는다)
    adds    x8, x8, x10         // lo += 2^31, 올림수 → C 플래그
    adc     x9, x9, xzr         // hi += C   (128비트 덧셈)
    extr    x0, x9, x8, #32
    ret
    SIZE_FN(q32_mul_round)

/* 연습 6: uint64_t dot_u32(const uint32_t *a, const uint32_t *b, size_t n) — UMADDL 누적 */
    .globl SYM(dot_u32)
    TYPE_FN(dot_u32)
    .p2align 2
SYM(dot_u32):
    mov     x8, #0              // acc = 0
    cbz     x2, 2f              // n == 0 → 0
1:  ldr     w9, [x0], #4        // a[i]  (post-index, ARM-B04)
    ldr     w10, [x1], #4       // b[i]
    umaddl  x8, w9, w10, x8     // acc += (uint64_t)a[i] * b[i]   (32x32→64 곱 + 64비트 덧셈)
    subs    x2, x2, #1
    b.ne    1b
2:  mov     x0, x8
    ret
    SIZE_FN(dot_u32)

#ifndef __APPLE__
    .section .note.GNU-stack,"",%progbits
#endif
