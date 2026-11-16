/* ARM-A07 shared_bugs.S — 일부러 틀린 버전 3개 (TODO 아님). Part B 테스트가 잡아야 한다.
 * starter / solution / practice 빌드에 모두 들어간다. 꼬리 처리는 생략: n 은 8의 배수만 (테스트도 그렇게 부른다).
 * 각 함수는 solution.S 의 정답과 한두 줄만 다르다 — 바뀐 줄에 "버그:" 표시.
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

// 버그 1: 이득 곱에 SQRDMULH 대신 SQDMULH — 반올림이 없어 -∞ 쪽으로 버린다 (평균 -0.5 LSB 치우침)
    .globl SYM(bug_scale_sqdmulh)
    FTYPE(bug_scale_sqdmulh)
    .p2align 2
SYM(bug_scale_sqdmulh):
    dup     v1.8h, w3
    lsr     x5, x2, #3
    cbz     x5, 2f
1:  ld1     {v0.8h}, [x1], #16
    sqdmulh v0.8h, v0.8h, v1.8h         // 버그: sqrdmulh 여야 한다
    st1     {v0.8h}, [x0], #16
    subs    x5, x5, #1
    b.ne    1b
2:  ret
    FSIZE(bug_scale_sqdmulh)

// 버그 2: 마지막 좁히기에 SQSHRN (버림) — 반올림 상수 2^15 를 빠뜨림
    .globl SYM(bug_fir_trunc)
    FTYPE(bug_fir_trunc)
    .p2align 2
SYM(bug_fir_trunc):
    sub     x6, x3, #1
    add     x6, x0, x6, lsl #1
    lsr     x7, x1, #3
    cbz     x7, 3f
1:  movi    v4.4s, #0
    movi    v5.4s, #0
    mov     x8, x6
    mov     x9, x2
    mov     x10, x3
2:  ld1     {v0.8h}, [x8]
    sub     x8, x8, #2
    ld1r    {v1.8h}, [x9], #2
    sqdmull  v2.4s, v0.4h, v1.4h
    sqdmull2 v3.4s, v0.8h, v1.8h
    sqadd   v4.4s, v4.4s, v2.4s
    sqadd   v5.4s, v5.4s, v3.4s
    subs    x10, x10, #1
    b.ne    2b
    sqshrn  v6.4h, v4.4s, #16           // 버그: sqrshrn 이어야 한다
    sqshrn2 v6.8h, v5.4s, #16           // 버그
    st1     {v6.8h}, [x4], #16
    add     x6, x6, #16
    subs    x7, x7, #1
    b.ne    1b
3:  ret
    FSIZE(bug_fir_trunc)

// 버그 3: 포화 없는 곱·합 — SMULL + SHL #1 + ADD. 32비트에서 감싼다(wrap): 큰 양수 + 큰 양수 → 음수
    .globl SYM(bug_fir_addwrap)
    FTYPE(bug_fir_addwrap)
    .p2align 2
SYM(bug_fir_addwrap):
    sub     x6, x3, #1
    add     x6, x0, x6, lsl #1
    lsr     x7, x1, #3
    cbz     x7, 3f
1:  movi    v4.4s, #0
    movi    v5.4s, #0
    mov     x8, x6
    mov     x9, x2
    mov     x10, x3
2:  ld1     {v0.8h}, [x8]
    sub     x8, x8, #2
    ld1r    {v1.8h}, [x9], #2
    smull   v2.4s, v0.4h, v1.4h         // 버그: 포화 없는 곱 (2배는 아래 shl — -32768² 에서 감싼다)
    smull2  v3.4s, v0.8h, v1.8h
    shl     v2.4s, v2.4s, #1
    shl     v3.4s, v3.4s, #1
    add     v4.4s, v4.4s, v2.4s         // 버그: sqadd 여야 한다
    add     v5.4s, v5.4s, v3.4s
    subs    x10, x10, #1
    b.ne    2b
    sqrshrn  v6.4h, v4.4s, #16
    sqrshrn2 v6.8h, v5.4s, #16
    st1     {v6.8h}, [x4], #16
    add     x6, x6, #16
    subs    x7, x7, #1
    b.ne    1b
3:  ret
    FSIZE(bug_fir_addwrap)
