// ARM-B01 demo_portable.S — solution.S 와 같은 코드를 Mach-O(macOS) 와 ELF(Linux) 양쪽에서 조립
//   macOS : cc -std=c11 -Wall -Wextra -Werror -O2 harness.c demo_portable.S shared_hostile.S -o /tmp/ARM-B01_port
//   Linux : gcc (또는 clang) 로 같은 명령 — Lima VM 에서 실행 확인
// 차이는 심볼 이름(_ 접두사)과 ELF 전용 지시어(.type/.size/.note.GNU-stack) 뿐. 명령어는 한 글자도 안 바뀐다.
#ifdef __APPLE__
#define SYM(n) _##n
#else
#define SYM(n) n
#endif

// FUNC name : 전역 함수 시작 / ENDF name : (ELF 만) 크기 기록
.macro FUNC name
    .globl  SYM(\name)
#ifndef __APPLE__
    .type   SYM(\name), %function
#endif
    .p2align 2
SYM(\name):
.endm
.macro ENDF name
#ifndef __APPLE__
    .size   SYM(\name), . - SYM(\name)
#endif
.endm

    .text
FUNC add3
    add     x0, x0, x1
    add     x0, x0, x2
    ret
ENDF add3

FUNC max_long
    cmp     x0, x1
    csel    x0, x0, x1, gt
    ret
ENDF max_long

FUNC sum_array
    mov     x2, #0
    cbz     x1, 2f
1:  ldr     x3, [x0], #8
    add     x2, x2, x3
    subs    x1, x1, #1
    b.ne    1b
2:  mov     x0, x2
    ret
ENDF sum_array

FUNC max_int
    cmp     w0, w1
    csel    w0, w0, w1, gt
    ret
ENDF max_int

FUNC add_wide
    add     x0, x0, w1, sxtw
    ret
ENDF add_wide

FUNC regdump_capture
    str     x0, [x7]
    str     x1, [x7, #8]
    str     x2, [x7, #16]
    str     x3, [x7, #24]
    str     x4, [x7, #32]
    str     x5, [x7, #40]
    str     x6, [x7, #48]
    str     x29, [x7, #56]
    str     x30, [x7, #64]
    mov     x9, sp
    str     x9, [x7, #72]
    ret
ENDF regdump_capture

#ifndef __APPLE__
    .section .note.GNU-stack,"",%progbits   // "실행 가능한 스택 필요 없음" 표시 (없으면 ld 경고)
#endif
