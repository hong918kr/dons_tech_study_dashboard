/* ARM-B08 스택 프레임과 재귀: FP/LR 저장 — AArch64 (Apple arm64, Mach-O)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 harness.c starter.S(또는 solution.S) shared_regcheck.S -o t && ./t
 *
 * 규칙 (AAPCS64 + Apple):
 *  - 다른 함수를 부르는 함수(non-leaf)는 x30(LR)을 저장해야 한다 → 프롤로그 stp x29, x30.
 *  - x29(FP) 는 항상 "유효한 프레임 레코드" {이전 FP, 돌아갈 LR} 를 가리킨다 (Apple 은 생략 금지).
 *  - sp 는 항상 16 의 배수. 프레임 크기도 16 의 배수로 올림.
 *  - x19..x28 은 callee-saved: 쓰면 내가 저장/복원. x0..x17 은 호출로 깨진다고 가정.
 *  - x18 은 Apple 에서 플랫폼 예약 → 건드리지 않는다.
 */
    .text

/* ---------------- Part A: uint64_t fact_asm(uint64_t n) — 재귀, n! mod 2^64 ----------------
 * 프레임 32 B:  [x29+0] 호출자 FP | [x29+8] LR | [x29+16] 호출자 x19 | [x29+24] 패딩
 * base case (n <= 1) 에서 C 훅 base_hook() 를 부른다 (하네스가 FP 체인을 걷는다).
 */
    .globl  _fact_asm
    .p2align 2
_fact_asm:
    // TODO: 프롤로그 (stp x29, x30 / mov x29, sp / x19 저장), base case 에서 bl _base_hook,
    //       재귀 bl _fact_asm, mul, 에필로그. 프레임 32 B.
    mov     x0, #0                      // 스텁: 틀린 값, 안전하게 바로 복귀
    ret
    .globl  _fact_asm_end
_fact_asm_end:                          // 끝 표시 (하네스가 LR 범위 검사에 사용)
    udf     #0                          // 실행되면 즉시 예외 — 여기로 흘러오면 버그

/* ---------------- Part A: uint64_t fib_asm(uint64_t n) — 순진한 재귀 ----------------
 * 프레임 32 B:  [x29+0] FP | [x29+8] LR | [x29+16] 호출자 x19 | [x29+24] 호출자 x20
 * x19 = n, x20 = fib(n-1) — 두 번째 bl 을 건너서 살아남아야 하는 값 두 개.
 */
    .globl  _fib_asm
    .p2align 2
_fib_asm:
    // TODO: 프롤로그 (x19 = n, x20 = fib(n-1) 저장), base case 에서 bl _base_hook,
    //       bl _fib_asm 두 번, add, 에필로그. 프레임 32 B.
    mov     x0, #0                      // 스텁
    ret
    .globl  _fib_asm_end
_fib_asm_end:
    udf     #0

/* ---------------- Part A/C: int64_t local_array_sum(int32_t seed, void (*cb)(int32_t *a, size_t n)) ----
 * int32_t a[10] 을 스택에 만든다: a[i] = seed + i (32비트 wrap). cb != NULL 이면 cb(a, 10) 호출 후
 * (cb 가 바꿨을 수도 있는) a[] 를 다시 읽어 int64 로 합한다.
 * 프레임: [x29+0] FP | [x29+8] LR | [x29-48 .. x29-9] a[0..9] (40 B → 48 B 로 올림)
 */
    .globl  _local_array_sum
    .p2align 2
_local_array_sum:
    // TODO: 프롤로그, sub sp, sp, #48, a[i] = seed + i 채우기, cb 가 있으면 cb(sp, 10),
    //       ldrsw 로 64비트 합, mov sp, x29, 에필로그.
    mov     x0, #0                      // 스텁 (콜백도 안 부름)
    ret

/* ---------------- Part C: int fp_backtrace(uintptr_t *out, int max) — 크래시 로거용 ----------------
 * leaf 함수: 프레임을 만들지 않는다 → x29 는 아직 "호출자" 의 프레임 레코드, x30 = 내 반환 주소.
 * out[0] = x30, 그 다음 프레임 레코드마다 out[k] = [fp+8] (저장된 LR), fp = [fp] 로 올라간다.
 * 멈춤: max 개를 채움 / fp == 0 / fp 가 16 정렬 아님 / 다음 fp 가 위로(큰 주소로) 안 감.
 */
    .globl  _fp_backtrace
    .p2align 2
_fp_backtrace:
    // TODO: out[0] = x30, 그 다음 fp = x29 부터 ldp (이전 fp, LR) 로 체인을 따라 올라가며 기록.
    //       멈춤 조건 4개 (max / fp == 0 / 정렬 / 위로 안 감). 반환 = 개수.
    mov     w0, #0                      // 스텁: 0 개
    ret
