/* ARM-B05 추가 연습 답: cmp3 / in_range_u / in_range_s(CCMP) / count_neg(CINC) / abs_sat(CINV) */
#ifdef __APPLE__
#define FUNC(n) .globl _##n %% .p2align 2 %% _##n:
#define ENDF(n)
#else
#define FUNC(n) .global n ; .type n,%function ; .p2align 2 ; n:
#define ENDF(n) .size n, .-n
#endif

    .text

/* 연습 1: int64_t cmp3_s64(int64_t a, int64_t b) — a<b → -1, a==b → 0, a>b → 1 (qsort 비교 함수 모양) */
FUNC(cmp3_s64)
    cmp     x0, x1
    cset    x8, ne              // x8 = (a != b) ? 1 : 0
    cneg    x0, x8, lt          // a < b 면 -x8 = -1   (= csneg x0, x8, x8, ge)
    ret
ENDF(cmp3_s64)

/* 연습 2: int in_range_u(uint64_t x, uint64_t lo, uint64_t hi) — lo <= x <= hi (전제 lo <= hi), 비교 1번 */
FUNC(in_range_u)
    sub     x8, x0, x1          // x - lo   (x < lo 면 wrap 해서 아주 큰 수)
    sub     x9, x2, x1          // hi - lo
    cmp     x8, x9
    cset    w0, ls              // (x - lo) <= (hi - lo)  unsigned
    ret
ENDF(in_range_u)

/* 연습 3: int in_range_s(int64_t x, int64_t lo, int64_t hi) — signed, CCMP 로 && 를 분기 없이 */
FUNC(in_range_s)
    cmp     x0, x1              // x - lo
    ccmp    x0, x2, #0, ge      // ge 면 cmp x, hi / 아니면 NZCV = 0000 (→ le 거짓)
    cset    w0, le              // x <= hi (그리고 앞의 x >= lo)
    ret
ENDF(in_range_s)

/* 연습 4: size_t count_neg(const int64_t *a, size_t n) — 음수 개수, 루프 안에 분기 없음 */
FUNC(count_neg)
    mov     x8, #0              // count = 0
    cbz     x1, 2f              // n == 0 → 0
1:  ldr     x9, [x0], #8        // x9 = *a++
    cmp     x9, #0
    cinc    x8, x8, lt          // 음수면 count++   (= csinc x8, x8, x8, ge)
    subs    x1, x1, #1          // n--, Z = (n == 0)
    b.ne    1b
2:  mov     x0, x8
    ret
ENDF(count_neg)

/* 연습 5: int64_t abs_sat(int64_t x) — abs 인데 INT64_MIN → INT64_MAX 로 포화 */
FUNC(abs_sat)
    cmp     x0, #0
    cneg    x0, x0, lt          // |x|, 단 INT64_MIN 은 INT64_MIN 그대로
    cmp     x0, #0
    cinv    x0, x0, lt          // 아직 음수(= INT64_MIN 뿐)면 ~x = INT64_MAX
    ret
ENDF(abs_sat)
