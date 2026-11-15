/* ARM-M06 shared_bugs.S — 일부러 틀린 NEON 함수 5개 (TODO 아님). 하네스가 "테스트가 이 버그를 잡는가"를 CHECK 한다.
 * 각 함수는 solution.S 의 정답과 한두 줄만 다르다. 메모리를 범위 밖으로 읽거나 쓰지는 않는다.
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

// 버그 1: 꼬리를 잊었다 — n % 4 개가 dst 에 안 써진다
    .globl SYM(bug_vadd_notail)
    FTYPE(bug_vadd_notail)
    .p2align 2
SYM(bug_vadd_notail):
    cmp     x3, #4
    b.lo    2f
1:  ld1     {v0.4s}, [x1], #16
    ld1     {v1.4s}, [x2], #16
    fadd    v0.4s, v0.4s, v1.4s
    st1     {v0.4s}, [x0], #16
    sub     x3, x3, #4
    cmp     x3, #4
    b.hs    1b
2:  ret                                 // (꼬리 루프 없음)
    FSIZE(bug_vadd_notail)

// 버그 2: 리덕션을 반만 했다 — faddp s0, v0.2s 는 lane 0+1 만 더한다 (lane 2,3 버려짐)
    .globl SYM(bug_dot_f32_halfreduce)
    FTYPE(bug_dot_f32_halfreduce)
    .p2align 2
SYM(bug_dot_f32_halfreduce):
    movi    v0.16b, #0
    cmp     x2, #4
    b.lo    2f
1:  ld1     {v1.4s}, [x0], #16
    ld1     {v2.4s}, [x1], #16
    fmla    v0.4s, v1.4s, v2.4s
    sub     x2, x2, #4
    cmp     x2, #4
    b.hs    1b
2:  faddp   s0, v0.2s                   // 첫 faddp v0.4s 단계를 빠뜨림
    cbz     x2, 4f
3:  ldr     s1, [x0], #4
    ldr     s2, [x1], #4
    fmadd   s0, s1, s2, s0
    subs    x2, x2, #1
    b.ne    3b
4:  ret
    FSIZE(bug_dot_f32_halfreduce)

// 버그 3: SDOT 대신 UDOT — 바이트를 0..255 로 읽는다. 음수가 없으면 결과가 같아서 테스트를 통과해 버린다
    .globl SYM(bug_dot_s8_udot)
    FTYPE(bug_dot_s8_udot)
    .p2align 2
SYM(bug_dot_s8_udot):
    movi    v0.16b, #0
    cmp     x2, #16
    b.lo    2f
1:  ld1     {v1.16b}, [x0], #16
    ld1     {v2.16b}, [x1], #16
    udot    v0.4s, v1.16b, v2.16b       // ← 부호 없는 곱
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
    FSIZE(bug_dot_s8_udot)

// 버그 4: 꼬리를 ldrb(0 확장)로 읽는다 — 꼬리에 음수 바이트가 있을 때만 틀린다
    .globl SYM(bug_dot_s8_ldrb)
    FTYPE(bug_dot_s8_ldrb)
    .p2align 2
SYM(bug_dot_s8_ldrb):
    movi    v0.16b, #0
    cmp     x2, #16
    b.lo    2f
1:  ld1     {v1.16b}, [x0], #16
    ld1     {v2.16b}, [x1], #16
    sdot    v0.4s, v1.16b, v2.16b
    sub     x2, x2, #16
    cmp     x2, #16
    b.hs    1b
2:  addv    s0, v0.4s
    fmov    w3, s0
    cbz     x2, 4f
3:  ldrb    w4, [x0], #1                // ← 0 확장 (0x80 → 128, 원래는 -128)
    ldrb    w5, [x1], #1
    madd    w3, w4, w5, w3
    subs    x2, x2, #1
    b.ne    3b
4:  mov     w0, w3
    ret
    FSIZE(bug_dot_s8_ldrb)

// 버그 5: int64 를 돌려주지만 int32 lane 을 한 번도 비우지 않는다 — n >= 524288 최악 입력에서 lane 이 감싼다
    .globl SYM(bug_dot_s8_naive64)
    FTYPE(bug_dot_s8_naive64)
    .p2align 2
SYM(bug_dot_s8_naive64):
    movi    v0.16b, #0
    cmp     x2, #16
    b.lo    2f
1:  ld1     {v1.16b}, [x0], #16
    ld1     {v2.16b}, [x1], #16
    sdot    v0.4s, v1.16b, v2.16b       // lane 이 2^31-1 을 넘으면 조용히 감싼다 (ISA 정의, 트랩 없음)
    sub     x2, x2, #16
    cmp     x2, #16
    b.hs    1b
2:  saddlv  d0, v0.4s                   // 이미 감싼 int32 4개를 int64 로 더해 봐야 늦었다
    fmov    x3, d0
    cbz     x2, 4f
3:  ldrsb   w4, [x0], #1
    ldrsb   w5, [x1], #1
    smaddl  x3, w4, w5, x3
    subs    x2, x2, #1
    b.ne    3b
4:  mov     x0, x3
    ret
    FSIZE(bug_dot_s8_naive64)

#ifndef __APPLE__
    .section .note.GNU-stack,"",%progbits
#endif
