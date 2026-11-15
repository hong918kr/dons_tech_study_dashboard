/* ARM-B08 추가 연습 답 — AArch64 (Apple arm64, Mach-O)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_regcheck.S -o p && ./p
 */
    .text

/* ---- 연습 1: uint64_t fact_tail(uint64_t n, uint64_t acc) — 꼬리 호출(b)로 프레임 0 개 ----
 * n <= 1 이면 base_hook 을 부르고 acc 반환 (여기서만 프레임을 만든다).
 * 아니면 acc *= n, n -= 1 하고 b _fact_tail — bl 이 아니므로 LR 그대로, 스택도 그대로.
 */
    .globl  _fact_tail
    .p2align 2
_fact_tail:
    cmp     x0, #1
    b.ls    1f                          // n <= 1 → base
    mul     x1, x1, x0                  // acc *= n
    sub     x0, x0, #1                  // n -= 1
    b       _fact_tail                  // 꼬리 호출: 프레임 없이 점프
1:  stp     x29, x30, [sp, #-32]!       // base 에서만 non-leaf → 프레임
    mov     x29, sp
    str     x19, [sp, #16]
    mov     x19, x1                     // acc 를 bl 너머로 보관
    bl      _base_hook
    mov     x0, x19
    ldr     x19, [sp, #16]
    ldp     x29, x30, [sp], #32
    ret
    .globl  _fact_tail_end
_fact_tail_end:
    udf     #0

/* ---- 연습 2: uint64_t vla_sum(size_t n, void (*cb)(uint64_t *a, size_t n)) — 크기가 런타임인 지역 배열 ----
 * a[i] = i*i, 크기 = round_up(8n, 16). 해제는 mov sp, x29 (크기를 다시 계산할 필요 없음).
 */
    .globl  _vla_sum
    .p2align 2
_vla_sum:
    stp     x29, x30, [sp, #-32]!
    mov     x29, sp
    stp     x19, x20, [sp, #16]         // x19 = n, x20 = a (blr 너머로 필요)
    mov     x19, x0
    lsl     x9, x0, #3                  // 8n
    add     x9, x9, #15
    and     x9, x9, #0xfffffffffffffff0 // 16 의 배수로 올림
    sub     sp, sp, x9                  // 동적 할당 (sub (extended register) 는 sp 허용)
    mov     x20, sp                     // a
    mov     x10, #0
1:  cmp     x10, x19
    b.hs    2f
    mul     x11, x10, x10
    str     x11, [x20, x10, lsl #3]     // a[i] = i*i
    add     x10, x10, #1
    b       1b
2:  cbz     x1, 3f
    mov     x9, x1
    mov     x0, x20
    mov     x1, x19
    blr     x9                          // cb(a, n)
3:  mov     x0, #0
    mov     x10, #0
4:  cmp     x10, x19
    b.hs    5f
    ldr     x11, [x20, x10, lsl #3]
    add     x0, x0, x11
    add     x10, x10, #1
    b       4b
5:  mov     sp, x29                     // VLA + 무엇이든 한 번에 해제
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #32
    ret

/* ---- 연습 3: int64_t tree_sum(const struct node *t) — struct node { int64_t val; node *left, *right; } ----
 * x19 = t, x20 = 왼쪽 합. 두 번째 재귀 호출 너머로 둘 다 필요.
 */
    .globl  _tree_sum
    .p2align 2
_tree_sum:
    cbz     x0, 9f                      // NULL → 0 (leaf 경로: 프레임 불필요)
    stp     x29, x30, [sp, #-32]!
    mov     x29, sp
    stp     x19, x20, [sp, #16]
    mov     x19, x0
    ldr     x0, [x19, #8]               // t->left
    bl      _tree_sum
    mov     x20, x0
    ldr     x0, [x19, #16]              // t->right
    bl      _tree_sum
    add     x0, x0, x20
    ldr     x9, [x19]                   // t->val
    add     x0, x0, x9
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #32
    ret
9:  mov     x0, #0
    ret

/* ---- 연습 4: uint64_t fact_guarded(uint64_t n, uintptr_t limit) — 스택 한계 검사 ----
 * 프레임(32 B)을 만들기 전에 sp - 32 < limit 이면 UINT64_MAX 반환 (펌웨어의 스택 오버플로 가드).
 * 하위 호출이 UINT64_MAX 를 돌려주면 그대로 전파 (n! mod 2^64 는 n >= 2 에서 짝수 → 정상값과 안 겹침).
 */
    .globl  _fact_guarded
    .p2align 2
_fact_guarded:
    mov     x9, sp
    sub     x9, x9, #32                 // 프레임을 만들면 sp 가 될 값
    cmp     x9, x1
    b.lo    8f                          // limit 아래로 내려감 → 거부
    stp     x29, x30, [sp, #-32]!
    mov     x29, sp
    stp     x19, x20, [sp, #16]
    mov     x19, x0                     // n
    mov     x20, x1                     // limit
    mov     x0, #1
    cmp     x19, #1
    b.ls    2f                          // n <= 1 → 1
    sub     x0, x19, #1
    mov     x1, x20
    bl      _fact_guarded
    cmn     x0, #1                      // x0 == UINT64_MAX ?
    b.eq    2f                          // 전파
    mul     x0, x0, x19
2:  ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #32
    ret
8:  mov     x0, #-1                     // UINT64_MAX
    ret

/* 도우미: uintptr_t get_sp(void) — leaf, 호출 순간의 sp (= 호출자의 sp) */
    .globl  _get_sp
    .p2align 2
_get_sp:
    mov     x0, sp
    ret
