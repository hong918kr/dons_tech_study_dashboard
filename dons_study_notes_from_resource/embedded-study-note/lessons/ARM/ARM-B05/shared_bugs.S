/* ARM-B05 일부러 버그를 심은 함수 (mutation). 모든 빌드(starter/solution/practice)에 같이 링크된다.
 * 하네스는 "내 테스트가 이 버그를 잡아내는가?" 를 검사한다 — 잡으면 PASS.
 */
#ifdef __APPLE__
#define FUNC(n) .globl _##n %% .p2align 2 %% _##n:
#define ENDF(n)
#else
#define FUNC(n) .global n ; .type n,%function ; .p2align 2 ; n:
#define ENDF(n) .size n, .-n
#endif

    .text

/* BUG: unsigned max 인데 signed 조건 GT 를 썼다 (정답은 HI) */
FUNC(umax_bug_gt)
    cmp     x0, x1
    csel    x0, x0, x1, gt      // <-- 버그: 0xFFFF...FF 를 -1 로 본다
    ret
ENDF(umax_bug_gt)

/* BUG: signed max 인데 unsigned 조건 HI 를 썼다 (정답은 GT) */
FUNC(smax_bug_hi)
    cmp     x0, x1
    csel    x0, x0, x1, hi      // <-- 버그: -1 을 0xFFFF...FF(최대) 로 본다
    ret
ENDF(smax_bug_hi)

/* BUG: tick 비교를 그냥 unsigned now >= deadline 로 했다 (wrap 에서 틀림) */
FUNC(tick_naive_hs)
    cmp     w0, w1
    cset    w0, hs              // <-- 버그: deadline 이 wrap 직전, now 가 wrap 직후면 0
    ret
ENDF(tick_naive_hs)

/* BUG: 차이의 부호(PL) 대신 signed 비교 GE 를 썼다 (signed overflow 구간에서 틀림) */
FUNC(tick_bug_ge)
    cmp     w0, w1
    cset    w0, ge              // <-- 버그: GE 는 N==V, 우리가 원하는 건 N==0
    ret
ENDF(tick_bug_ge)
