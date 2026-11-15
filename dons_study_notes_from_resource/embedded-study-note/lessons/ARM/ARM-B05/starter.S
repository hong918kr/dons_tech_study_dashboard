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
    // TODO: cmp + b.ge 로 a >= b 면 a, 아니면 b
    mov     x0, #0
    ret
ENDF(smax_br)

/* int64_t smax_sel(int64_t a, int64_t b) — signed, branchless */
FUNC(smax_sel)
    // TODO: cmp + csel (조건 gt)
    mov     x0, #0
    ret
ENDF(smax_sel)

/* int64_t smin_br(int64_t a, int64_t b) */
FUNC(smin_br)
    // TODO: cmp + b.le
    mov     x0, #0
    ret
ENDF(smin_br)

/* int64_t smin_sel(int64_t a, int64_t b) */
FUNC(smin_sel)
    // TODO: cmp + csel (조건 lt)
    mov     x0, #0
    ret
ENDF(smin_sel)

/* uint64_t umax_br(uint64_t a, uint64_t b) — unsigned 조건 HS */
FUNC(umax_br)
    // TODO: cmp + b.hs (unsigned!)
    mov     x0, #0
    ret
ENDF(umax_br)

/* uint64_t umax_sel(uint64_t a, uint64_t b) — unsigned 조건 HI */
FUNC(umax_sel)
    // TODO: cmp + csel (조건 hi — gt 아님!)
    mov     x0, #0
    ret
ENDF(umax_sel)

/* uint64_t umin_br(uint64_t a, uint64_t b) */
FUNC(umin_br)
    // TODO: cmp + b.ls
    mov     x0, #0
    ret
ENDF(umin_br)

/* uint64_t umin_sel(uint64_t a, uint64_t b) */
FUNC(umin_sel)
    // TODO: cmp + csel (조건 lo)
    mov     x0, #0
    ret
ENDF(umin_sel)

/* ================= Part B: abs / sign / clamp — 경계값 ================= */

/* int64_t sabs_br(int64_t x) — abs(INT64_MIN) == INT64_MIN (ISA 처럼 wrap) */
FUNC(sabs_br)
    // TODO: tbz 로 부호 비트(63) 검사, 음수면 neg
    mov     x0, #0
    ret
ENDF(sabs_br)

/* int64_t sabs_sel(int64_t x) */
FUNC(sabs_sel)
    // TODO: cmp x0, #0 + csneg (또는 별칭 cneg)
    mov     x0, #0
    ret
ENDF(sabs_sel)

/* int64_t sign_br(int64_t x) — -1 / 0 / +1 */
FUNC(sign_br)
    // TODO: cbz 로 0 처리, tbnz 로 음수 처리, 나머지는 1
    mov     x0, #0
    ret
ENDF(sign_br)

/* int64_t sign_sel(int64_t x) */
FUNC(sign_sel)
    // TODO: asr x8, x0, #63 로 -1/0 만들고, cmp + csinc 로 양수만 1
    mov     x0, #0
    ret
ENDF(sign_sel)

/* int64_t clamp_br(int64_t x, int64_t lo, int64_t hi) == min(max(x, lo), hi) */
FUNC(clamp_br)
    // TODO: x < lo 면 lo, 그다음 x > hi 면 hi (분기 2개)
    mov     x0, #0
    ret
ENDF(clamp_br)

/* int64_t clamp_sel(int64_t x, int64_t lo, int64_t hi) */
FUNC(clamp_sel)
    // TODO: cmp/csel 두 쌍 (ge 다음 le)
    mov     x0, #0
    ret
ENDF(clamp_sel)

/* ================= Part C: 펌웨어 — 포화 덧셈, tick 비교, 상태 레지스터 스캔 ================= */

/* int64_t sat_add_s64(int64_t a, int64_t b) — 넘치면 INT64_MAX / INT64_MIN 으로 포화 */
FUNC(sat_add_s64)
    // TODO: 포화값을 먼저 만들고 (asr, eor), adds 후 csel ..., vs
    mov     x0, #0
    ret
ENDF(sat_add_s64)

/* int tick_reached(uint32_t now, uint32_t deadline) — 32비트 tick wrap 안전 */
FUNC(tick_reached)
    // TODO: w 레지스터로 cmp, cset ... pl (hs 도 ge 도 아님)
    mov     w0, #0
    ret
ENDF(tick_reached)

/* int64_t find_fault(const uint32_t *st, size_t n) — ERR(bit5)|OVR(bit6) 첫 인덱스, 없으면 -1 */
FUNC(find_fault)
    // TODO: cbz 로 n==0 처리, 루프: ldr w9 / tst w9, #0x60 / b.ne / i++ / cmp / b.lo
    mov     x0, #0
    ret
ENDF(find_fault)
