/* ENV: qemu */
/* ARM-M09 demo_bugs.S — 7. 자주 하는 실수 재현용. solution.S 를 그대로 쓰되 SVC_Handler 만 버그 판으로 바꾼다.
 * 빌드: run_qemu.sh bugs  →  solution.c + demo_bugs.S(-DBUG=1|2) + shared_startup.c
 *   BUG=1: 스택 선택 없이 항상 MSP  → PSP 태스크의 svc 에서 엉뚱한 "프레임"
 *   BUG=2: b 대신 bl (+ bx lr)     → lr 의 EXC_RETURN 이 사라져 예외 복귀를 못 한다
 */
#define SVC_Handler SVC_Handler_unused          /* solution.S 의 정답 스텁 이름을 비켜 둔다 */
#include "solution.S"
#undef SVC_Handler

    .text
    .thumb
    .global SVC_Handler
    .type   SVC_Handler, %function
    .thumb_func
SVC_Handler:
#if BUG == 1
    mrs     r0, msp             // 버그: EXC_RETURN bit2 를 안 본다
    mov     r1, lr
    b       svc_handler_c
#else
    mrs     r0, msp
    mov     r1, lr
    bl      svc_handler_c       // 버그: bl 이 lr(EXC_RETURN) 을 "여기로 돌아올 주소" 로 덮어쓴다
    bx      lr                  // lr = C 함수가 남긴 아무 값 → 예외 복귀가 아니라 엉뚱한 곳으로 점프
#endif
    .size   SVC_Handler, . - SVC_Handler
