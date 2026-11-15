/* ARM-B09 my_strlen — 일부러 off-by-one 버그를 심은 AArch64 strlen
 *
 *   size_t my_strlen(const char *s);      // x0 = s, 반환 x0 = 길이
 *
 * TODO: lldb 로 한 명령씩 실행해(si) x0/x1/x2/cpsr 를 기록하고 버그를 찾아 고쳐라.
 *       (힌트: 대부분의 테스트는 통과한다. 한 길이에서만 틀리고, 페이지 끝에서는 죽는다.)
 * 같은 파일이 macOS(Mach-O)와 Linux(ELF) 양쪽에서 어셈블되도록 SYM/TYPE/SIZE 매크로를 쓴다.
 */
#ifdef __APPLE__
#  define SYM(n)  _##n                 /* Mach-O: C 이름 앞에 '_' */
#  define TYPE(n)                      /* Mach-O 에는 .type/.size 없음 */
#  define SIZE(n)
#else
#  define SYM(n)  n
#  define TYPE(n) .type n, %function
#  define SIZE(n) .size n, . - n
#endif

    .text
    .globl  SYM(my_strlen)
    TYPE(my_strlen)
    .p2align 2
SYM(my_strlen):                        // leaf 함수: 스택·x29/x30 안 건드림
    sub     x1, x0, #1                 // x1 = p = s - 1 (첫 pre-index 가 s[0] 을 읽게)
1:  ldrb    w2, [x1, #1]!              // pre-index: p += 1 먼저, 그 다음 w2 = *p
    cmp     w2, #0                     // NZCV 갱신 (w2 - 0)
    b.ne    1b                         // *p != 0 이면 계속
    sub     x0, x1, x0                 // 길이 = p - s
    ret                                // br x30
    SIZE(my_strlen)

#ifndef __APPLE__
    .section .note.GNU-stack, "", %progbits
#endif
