/* ARM-B05 조건 플래그와 분기: CMP, B.cond, CSEL — AArch64 함수 17개
 * C 프로토타입은 harness.c 에 있다. 모두 leaf 함수: 스택·x19..x28 안 씀, x8/x9 만 임시로 사용.
 * Mac(Mach-O)과 Linux(ELF) 양쪽에서 어셈블되도록 FUNC/ENDF 매크로를 쓴다.
 */
#ifdef __APPLE__
#define FUNC(n) .globl _##n %% .p2align 2 %% _##n:
#define ENDF(n)
#else
#define FUNC(n) .global n ; .type n,%function ; .p2align 2 ; n:
#define ENDF(n) .size n, .-n
#endif

    .text

/* ================= Part A: max / min — 분기 vs branchless ================= */

/* int64_t smax_br(int64_t a, int64_t b) — signed, 분기 */
FUNC(smax_br)
    cmp     x0, x1              // 플래그 ← a - b (결과는 버림)
    b.ge    1f                  // a >= b (signed: N==V) → x0 이 이미 답
    mov     x0, x1              // 아니면 b
1:  ret
ENDF(smax_br)

/* int64_t smax_sel(int64_t a, int64_t b) — signed, branchless */
FUNC(smax_sel)
    cmp     x0, x1              // 플래그 ← a - b
    csel    x0, x0, x1, gt      // x0 = (a > b) ? a : b
    ret
ENDF(smax_sel)

/* int64_t smin_br(int64_t a, int64_t b) */
FUNC(smin_br)
    cmp     x0, x1
    b.le    1f                  // a <= b (signed: Z==1 || N!=V) → a
    mov     x0, x1
1:  ret
ENDF(smin_br)

/* int64_t smin_sel(int64_t a, int64_t b) */
FUNC(smin_sel)
    cmp     x0, x1
    csel    x0, x0, x1, lt      // (a < b) ? a : b
    ret
ENDF(smin_sel)

/* uint64_t umax_br(uint64_t a, uint64_t b) — unsigned 조건 HS */
FUNC(umax_br)
    cmp     x0, x1
    b.hs    1f                  // a >= b (unsigned: C==1) → a
    mov     x0, x1
1:  ret
ENDF(umax_br)

/* uint64_t umax_sel(uint64_t a, uint64_t b) — unsigned 조건 HI */
FUNC(umax_sel)
    cmp     x0, x1
    csel    x0, x0, x1, hi      // (a > b unsigned: C==1 && Z==0) ? a : b
    ret
ENDF(umax_sel)

/* uint64_t umin_br(uint64_t a, uint64_t b) */
FUNC(umin_br)
    cmp     x0, x1
    b.ls    1f                  // a <= b (unsigned: C==0 || Z==1) → a
    mov     x0, x1
1:  ret
ENDF(umin_br)

/* uint64_t umin_sel(uint64_t a, uint64_t b) */
FUNC(umin_sel)
    cmp     x0, x1
    csel    x0, x0, x1, lo      // (a < b unsigned: C==0) ? a : b
    ret
ENDF(umin_sel)

/* ================= Part B: abs / sign / clamp — 경계값 ================= */

/* int64_t sabs_br(int64_t x) — abs(INT64_MIN) == INT64_MIN (ISA 처럼 wrap) */
FUNC(sabs_br)
    tbz     x0, #63, 1f         // 부호 비트 0 → 음수 아님 → 그대로
    neg     x0, x0              // x = 0 - x  (INT64_MIN 은 다시 INT64_MIN)
1:  ret
ENDF(sabs_br)

/* int64_t sabs_sel(int64_t x) */
FUNC(sabs_sel)
    cmp     x0, #0              // 플래그 ← x - 0
    csneg   x0, x0, x0, ge      // ge ? x : -x   (별칭: cneg x0, x0, lt)
    ret
ENDF(sabs_sel)

/* int64_t sign_br(int64_t x) — -1 / 0 / +1 */
FUNC(sign_br)
    cbz     x0, 1f              // x == 0 → x0(=0) 그대로 반환
    tbnz    x0, #63, 2f         // 부호 비트 1 → 음수
    mov     x0, #1              // 양수
1:  ret
2:  mov     x0, #-1             // (movn x0, #0)
    ret
ENDF(sign_br)

/* int64_t sign_sel(int64_t x) */
FUNC(sign_sel)
    asr     x8, x0, #63         // x8 = (x < 0) ? -1 : 0   (플래그 안 바꿈)
    cmp     x0, #0
    csinc   x0, x8, xzr, le     // le ? x8 : xzr + 1   → -1 / 0 / 1
    ret
ENDF(sign_sel)

/* int64_t clamp_br(int64_t x, int64_t lo, int64_t hi) == min(max(x, lo), hi) */
FUNC(clamp_br)
    cmp     x0, x1
    b.ge    1f                  // x >= lo → 아래쪽 OK
    mov     x0, x1              // x < lo → lo
1:  cmp     x0, x2
    b.le    2f                  // x <= hi → 위쪽 OK
    mov     x0, x2              // x > hi → hi
2:  ret
ENDF(clamp_br)

/* int64_t clamp_sel(int64_t x, int64_t lo, int64_t hi) */
FUNC(clamp_sel)
    cmp     x0, x1
    csel    x0, x0, x1, ge      // max(x, lo)
    cmp     x0, x2
    csel    x0, x0, x2, le      // min(그 값, hi)
    ret
ENDF(clamp_sel)

/* ================= Part C: 펌웨어 — 포화 덧셈, tick 비교, 상태 레지스터 스캔 ================= */

/* int64_t sat_add_s64(int64_t a, int64_t b) — 넘치면 INT64_MAX / INT64_MIN 으로 포화 */
FUNC(sat_add_s64)
    asr     x9, x0, #63                 // a<0 ? -1 : 0
    eor     x9, x9, #0x7fffffffffffffff // a<0 ? INT64_MIN : INT64_MAX  (포화값 미리 계산)
    adds    x0, x0, x1                  // a + b, V = signed overflow
    csel    x0, x9, x0, vs              // 넘쳤으면 포화값
    ret
ENDF(sat_add_s64)

/* int tick_reached(uint32_t now, uint32_t deadline) — 32비트 tick wrap 안전 */
FUNC(tick_reached)
    cmp     w0, w1              // now - deadline (mod 2^32), N = 결과의 bit31
    cset    w0, pl              // N==0 → (int32_t)(now - deadline) >= 0 → 1
    ret
ENDF(tick_reached)

/* int64_t find_fault(const uint32_t *st, size_t n) — ERR(bit5)|OVR(bit6) 첫 인덱스, 없으면 -1 */
FUNC(find_fault)
    mov     x8, #0              // i = 0
    cbz     x1, 3f              // n == 0 → 메모리 안 읽고 -1
1:  ldr     w9, [x0, x8, lsl #2]    // w9 = st[i]
    tst     w9, #0x60           // ERR|OVR 중 하나라도? (ands wzr, w9, #0x60)
    b.ne    2f                  // Z==0 → 찾음
    add     x8, x8, #1          // i++
    cmp     x8, x1
    b.lo    1b                  // i < n (unsigned) → 계속
3:  mov     x0, #-1             // 못 찾음
    ret
2:  mov     x0, x8              // 찾은 인덱스
    ret
ENDF(find_fault)
