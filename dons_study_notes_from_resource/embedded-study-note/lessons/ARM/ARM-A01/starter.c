/* ENV: qemu */
/* ARM-A01 starter.c — C 로 쓰는 TODO 두 개 + 테스트 하네스(harness.c) 포함.
 * 빌드: run_qemu.sh starter  →  starter.c + starter.S + shared_*.c + shared_*.S (an385, 그리고 -DHAS_FPU 로 an386)
 */
#include "harness.c"

/* ---- TODO 1: 초기 스택 프레임 ----
 * 새 태스크가 "PendSV 에서 쫓겨난 적이 있는 것처럼" 보이게 16 워드를 쌓는다.
 *   top = (stack_lo + stack_words) 를 8 바이트로 내려 정렬, sp = top - 16 워드
 *   sp[0..7]  = r4..r11 (0x04040404 … 0x0B0B0B0B: 디버깅용 채움 값)
 *   sp[8..15] = r0=arg, r1=0x01010101, r2=0x02020202, r3=0x03030303, r12=0x0C0C0C0C,
 *               lr=task_exit_trap, pc=entry & ~1, xPSR=0x01000000
 *   stack_lo[0], stack_lo[1] = CANARY, t->sp = sp, t->exc_return = 0xFFFFFFFD, t->alive = 1
 */
uint32_t *init_stack(struct tcb *t, void (*entry)(void *), void *arg) {
    uint32_t top = (uint32_t)(uintptr_t)(t->stack_lo + t->stack_words);  /* TODO: 8 정렬 */
    uint32_t *sp = (uint32_t *)(uintptr_t)top - 16;
    (void)entry;                                                        /* TODO: r4..r11 */
    (void)arg;                                                          /* TODO: r0 */
    /* TODO: r1 */
    /* TODO: r2 */
    /* TODO: r3 */
    /* TODO: r12 */
    /* TODO: lr */
    /* TODO: pc */
    /* TODO: xPSR */
    /* TODO: 카나리 */

    t->sp = sp;
    /* TODO: exc_return */
    /* TODO: alive */
    return sp;
}

/* ---- TODO 2: SysTick 이 부르는 틱 처리 ----
 * g_os.ticks 증가, tick_limit 에 닿으면 g_os.stop = 1, 그리고 PendSV 를 pend (ICSR.PENDSVSET, 쓰기만!)
 */
void os_tick(void) {
    /* TODO */

    /* TODO */
}
