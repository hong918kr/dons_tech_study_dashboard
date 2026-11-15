/* ARM-B08 demo_bugs.S — 일부러 틀린 프레임 4종 (테스트 아님, demo_bugs.c 가 자식 프로세스에서 하나씩 실행)
 * build: cc -std=c11 -O2 demo_bugs.c demo_bugs.S shared_regcheck.S -o /tmp/ARM-B08_bugs && /tmp/ARM-B08_bugs
 */
    .text

/* 버그 1: non-leaf 인데 x30 을 저장하지 않음 (x19 만 저장) */
    .globl  _bug_fact_nolr
    .p2align 2
_bug_fact_nolr:
    sub     sp, sp, #16
    str     x19, [sp]                   // x19 는 저장했지만 x29/x30 은 안 함
    mov     x19, x0
    cmp     x19, #1
    b.hi    1f
    mov     x0, #1                      // base case 는 bl 이 없으니 x30 멀쩡 → n <= 1 은 "통과"
    b       2f
1:  sub     x0, x19, #1
    bl      _bug_fact_nolr              // x30 ← 아래 mul 의 주소. 내 반환 주소는 사라졌다
    mul     x0, x0, x19
2:  ldr     x19, [sp]
    add     sp, sp, #16
    ret                                 // x30 = mul 줄 → 같은 줄로 계속 "돌아간다"

/* 버그 2: x19 를 저장하지 않고 씀 (프레임 레코드는 정상) */
    .globl  _bug_fact_noxsave
    .p2align 2
_bug_fact_noxsave:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    mov     x19, x0                     // 호출자의 x19 를 덮어씀
    cmp     x19, #1
    b.hi    1f
    mov     x0, #1
    b       2f
1:  sub     x0, x19, #1
    bl      _bug_fact_noxsave           // 자식도 x19 를 덮는다 → 돌아오면 x19 == 1
    mul     x0, x0, x19
2:  ldp     x29, x30, [sp], #16
    ret

/* 버그 3: 지역 배열 해제를 잊음 (sub sp, sp, #48 의 짝이 없음) */
    .globl  _bug_local_nofree
    .p2align 2
_bug_local_nofree:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    sub     sp, sp, #48
    mov     x9, #0
1:  add     w10, w0, w9
    str     w10, [sp, x9, lsl #2]
    add     x9, x9, #1
    cmp     x9, #10
    b.lo    1b
    mov     x0, #0
    ldp     x29, x30, [sp], #16         // sp 가 배열을 가리킨 채 → x29 = a[1]:a[0], x30 = a[3]:a[2]
    ret                                 // pc = 배열 내용

/* 버그 4: sp 를 8 만 내림 (16 정렬 깨짐) 후 sp 기준 store */
    .globl  _bug_misaligned_sp
    .p2align 2
_bug_misaligned_sp:
    sub     sp, sp, #8                  // sp % 16 == 8
    str     x0, [sp]                    // sp 를 base 로 쓰는 접근 → SP 정렬 검사가 켜져 있으면 fault
    ldr     x0, [sp]
    add     sp, sp, #8
    ret

/* 버그 5: x20 을 루프 카운터로 쓰고 저장 안 함 — 반환값은 맞다 */
    .globl  _bug_sum_clobber_x20
    .p2align 2
_bug_sum_clobber_x20:                   // uint64_t f(uint64_t n) = 0 + 1 + ... + (n-1)
    mov     x0, x0
    mov     x1, #0                      // sum
    mov     x20, #0                     // i  ← callee-saved 인데 저장 안 함
1:  cmp     x20, x0
    b.hs    2f
    add     x1, x1, x20
    add     x20, x20, #1
    b       1b
2:  mov     x0, x1
    ret
