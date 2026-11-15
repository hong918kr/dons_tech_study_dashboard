/* ARM-B08 shared_regcheck.S — 주어진 도우미 (TODO 아님). starter/solution/practice 모두에 링크된다.
 *
 * uint64_t call_preserve_check(uintptr_t fn, uint64_t a, uint64_t b, uint64_t *ret);
 *   x19..x28 에 알려진 무늬(위 16비트 0xC5C5, 아래 16비트 = 레지스터 번호 19..28)를 넣고 fn(a, b) 를 부른 뒤
 *   무늬가 그대로인지 본다. *ret = fn 의 반환값.
 *   반환 마스크: 비트 i (0..9) = x(19+i) 가 바뀜,  비트 10 = 돌아왔을 때 sp != x29 (sp/fp 복구 실패)
 *   단순화: sp 와 fp 가 둘 다 망가지면 복구할 방법이 없다 → 그런 버그는 demo_bugs.c 가 자식 프로세스에서 다룬다.
 *
 * 프레임 (112 B, 16의 배수):
 *   [x29+0]  호출자 x29      [x29+8]  호출자 x30(LR)
 *   [x29+16 .. x29+95]       호출자의 x19..x28 (우리가 무늬로 덮어쓰므로 저장 의무)
 *   [x29+96] ret 포인터       [x29+104] 패딩
 */
    .text

    .macro SETPAT reg, k
    movz    \reg, #0xC5C5, lsl #48      // reg = 0xC5C5_0000_0000_0000
    movk    \reg, #\k                   // 아래 16비트 = 레지스터 번호
    .endm

    .macro CHKPAT reg, k
    movz    x10, #0xC5C5, lsl #48       // 기대 무늬 다시 만들기
    movk    x10, #\k
    cmp     \reg, x10
    cset    x11, ne                     // 바뀌었으면 1
    orr     x12, x12, x11, lsl #(\k - 19)   // 마스크의 (k-19) 번 비트
    .endm

    .globl  _call_preserve_check
    .p2align 2
_call_preserve_check:
    stp     x29, x30, [sp, #-112]!      // 프레임 112 B 확보 + 프레임 레코드 저장
    mov     x29, sp                     // fp = 새 프레임 레코드
    stp     x19, x20, [sp, #16]         // 호출자의 callee-saved 보관
    stp     x21, x22, [sp, #32]
    stp     x23, x24, [sp, #48]
    stp     x25, x26, [sp, #64]
    stp     x27, x28, [sp, #80]
    str     x3, [sp, #96]               // ret 포인터 (x3 는 caller-saved → fn 이 깨도 됨)
    mov     x9, x0                      // x9 = fn
    mov     x0, x1                      // fn 의 첫 인자 = a
    mov     x1, x2                      // fn 의 둘째 인자 = b
    SETPAT  x19, 19
    SETPAT  x20, 20
    SETPAT  x21, 21
    SETPAT  x22, 22
    SETPAT  x23, 23
    SETPAT  x24, 24
    SETPAT  x25, 25
    SETPAT  x26, 26
    SETPAT  x27, 27
    SETPAT  x28, 28
    blr     x9                          // fn(a, b) — x0 = 결과
    mov     x12, #0                     // 결과 마스크
    mov     x13, sp
    cmp     x13, x29                    // 정상이라면 sp == x29 (우리 프레임 바닥)
    cset    x11, ne
    orr     x12, x12, x11, lsl #10      // 비트 10
    mov     sp, x29                     // (fp 가 살아 있다고 보고) sp 복구
    ldr     x3, [sp, #96]
    str     x0, [x3]                    // *ret = fn 의 반환값
    CHKPAT  x19, 19
    CHKPAT  x20, 20
    CHKPAT  x21, 21
    CHKPAT  x22, 22
    CHKPAT  x23, 23
    CHKPAT  x24, 24
    CHKPAT  x25, 25
    CHKPAT  x26, 26
    CHKPAT  x27, 27
    CHKPAT  x28, 28
    mov     x0, x12                     // 반환 = 마스크
    ldp     x19, x20, [sp, #16]         // 호출자의 callee-saved 되돌리기
    ldp     x21, x22, [sp, #32]
    ldp     x23, x24, [sp, #48]
    ldp     x25, x26, [sp, #64]
    ldp     x27, x28, [sp, #80]
    ldp     x29, x30, [sp], #112        // 프레임 레코드 복원 + sp 되돌리기
    ret
