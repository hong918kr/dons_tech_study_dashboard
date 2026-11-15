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
    stp     x29, x30, [sp, #-32]!       // sp -= 32, [sp] = x29, [sp+8] = x30
    mov     x29, sp                     // 새 프레임 레코드 = 지금 sp
    str     x19, [sp, #16]              // x19 를 쓸 거라 호출자 값 보관
    mov     x19, x0                     // x19 = n (bl 을 건너서 살아남아야 함)
    cmp     x19, #1
    b.hi    1f                          // n > 1 (부호 없음) → 재귀
    bl      _base_hook                  // base case: 훅 호출 (x0..x17, x30 깨짐)
    mov     x0, #1                      // 0! = 1! = 1
    b       2f
1:  sub     x0, x19, #1                 // 인자 = n - 1
    bl      _fact_asm                   // x0 = (n-1)!
    mul     x0, x0, x19                 // x0 = n * (n-1)!  (mod 2^64)
2:  ldr     x19, [sp, #16]              // 호출자 x19 복원
    ldp     x29, x30, [sp], #32         // FP/LR 복원, sp += 32
    ret                                 // pc = x30
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
    stp     x29, x30, [sp, #-32]!       // 프레임 확보 + FP/LR 저장
    mov     x29, sp
    stp     x19, x20, [sp, #16]         // callee-saved 두 개 보관
    mov     x19, x0                     // x19 = n
    cmp     x19, #2
    b.hs    1f                          // n >= 2 → 재귀
    bl      _base_hook                  // 잎(leaf 호출): 훅
    mov     x0, x19                     // fib(0) = 0, fib(1) = 1
    b       2f
1:  sub     x0, x19, #1
    bl      _fib_asm                    // x0 = fib(n-1)
    mov     x20, x0                     // 두 번째 호출 동안 보관
    sub     x0, x19, #2
    bl      _fib_asm                    // x0 = fib(n-2)
    add     x0, x0, x20                 // fib(n-1) + fib(n-2)
2:  ldp     x19, x20, [sp, #16]         // 복원
    ldp     x29, x30, [sp], #32
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
    stp     x29, x30, [sp, #-16]!       // FP/LR 저장 (cb 를 부르니 non-leaf)
    mov     x29, sp
    sub     sp, sp, #48                 // 지역 배열 40 B → 16 의 배수 48 B
    mov     x9, #0                      // i = 0
1:  add     w10, w0, w9                 // w10 = seed + i (32비트, wrap)
    str     w10, [sp, x9, lsl #2]       // a[i] = w10   (주소 = sp + i*4)
    add     x9, x9, #1
    cmp     x9, #10
    b.lo    1b
    cbz     x1, 2f                      // cb == NULL → 바로 합
    mov     x9, x1                      // x9 = cb (x1 은 인자로 덮어쓸 것)
    mov     x0, sp                      // 인자 1 = a (= sp, 16 정렬)
    mov     x1, #10                     // 인자 2 = 10
    blr     x9                          // cb(a, 10) — x0..x17 깨짐, sp/x29 는 보존됨
2:  mov     x0, #0                      // sum = 0 (64비트)
    mov     x9, #0
3:  ldrsw   x10, [sp, x9, lsl #2]       // a[i] 를 부호 확장해 64비트로
    add     x0, x0, x10
    add     x9, x9, #1
    cmp     x9, #10
    b.lo    3b
    mov     sp, x29                     // 지역 영역 해제 (= add sp, sp, #48)
    ldp     x29, x30, [sp], #16
    ret

/* ---------------- Part C: int fp_backtrace(uintptr_t *out, int max) — 크래시 로거용 ----------------
 * leaf 함수: 프레임을 만들지 않는다 → x29 는 아직 "호출자" 의 프레임 레코드, x30 = 내 반환 주소.
 * out[0] = x30, 그 다음 프레임 레코드마다 out[k] = [fp+8] (저장된 LR), fp = [fp] 로 올라간다.
 * 멈춤: max 개를 채움 / fp == 0 / fp 가 16 정렬 아님 / 다음 fp 가 위로(큰 주소로) 안 감.
 */
    .globl  _fp_backtrace
    .p2align 2
_fp_backtrace:
    mov     w9, #0                      // count = 0
    cmp     w1, #0
    b.le    9f                          // max <= 0 → 0
    str     x30, [x0]                   // out[0] = 내 반환 주소
    mov     w9, #1
    mov     x10, x29                    // fp = 호출자의 프레임 레코드
1:  cmp     w9, w1
    b.ge    9f                          // 다 채움
    cbz     x10, 9f                     // 체인 끝 (fp == 0)
    tst     x10, #0xf
    b.ne    9f                          // 정렬 안 된 fp = 깨진 체인
    ldp     x11, x12, [x10]             // x11 = 이전 fp, x12 = 저장된 LR
    str     x12, [x0, w9, uxtw #3]      // out[count] = LR
    add     w9, w9, #1
    cmp     x11, x10
    b.ls    9f                          // 이전 fp <= 지금 fp → 위로 안 감 (0 포함) → 끝
    mov     x10, x11                    // 한 칸 위로
    b       1b
9:  mov     w0, w9                      // 반환 = 채운 개수
    ret
