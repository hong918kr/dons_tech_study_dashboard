/* ENV: qemu */
/* ARM-A01 practice.c — 추가 연습 답 + 테스트 (Cortex-M3, mps2-an385).
 * 빌드: run_qemu.sh practice  →  practice.c + practice.S + shared_*.c + shared_*.S
 *  연습 1: SVC 없이 첫 태스크 시작 (os_start_direct, practice.S)
 *  연습 2: PENDSVSET 을 한 틱에 3 번 써도 PendSV 는 1 번 (펜딩 비트 1 개 = 합쳐짐)
 *  연습 3: 비특권 태스크 (CONTROL.nPRIV = 1): 스위처는 그대로, cpsid 는 무시됨
 *  연습 4: 스택 최고 수위(high-water mark) — 칠한 0xA5A5A5A5 가 어디까지 지워졌나
 */
#include "a01.h"

/* ---- solution.c 와 같은 TODO 답 (이 파일은 starter/solution 과 따로 빌드된다) ---- */
uint32_t *init_stack(struct tcb *t, void (*entry)(void *), void *arg) {
    uint32_t top = (uint32_t)(uintptr_t)(t->stack_lo + t->stack_words) & ~7u;
    uint32_t *sp = (uint32_t *)(uintptr_t)top - 16;
    for (uint32_t k = 0; k < 8u; k++) sp[k] = 0x01010101u * (4u + k);
    sp[8] = (uint32_t)(uintptr_t)arg;
    sp[9] = 0x01010101u; sp[10] = 0x02020202u; sp[11] = 0x03030303u; sp[12] = 0x0C0C0C0Cu;
    sp[13] = (uint32_t)(uintptr_t)task_exit_trap;
    sp[14] = (uint32_t)(uintptr_t)entry & ~1u;
    sp[15] = XPSR_INIT;
    t->stack_lo[0] = CANARY; t->stack_lo[1] = CANARY;
    t->sp = sp; t->exc_return = EXC_RETURN_THREAD_PSP; t->alive = 1u;
    return sp;
}

/* 연습 2: 한 틱에 PENDSVSET 을 g_pend_writes 번 쓴다 */
static volatile uint32_t g_pend_writes = 1u, g_pend_requests;
void os_tick(void) {
    g_os.ticks++;
    if (g_os.ticks >= g_os.tick_limit) g_os.stop = 1u;
    for (uint32_t i = 0; i < g_pend_writes; i++) { SCB_ICSR = ICSR_PENDSVSET; g_pend_requests++; }
}

volatile uint32_t g_task_npriv;                         /* practice.S 의 SVC_Handler 가 CONTROL 에 쓴다 */
uint32_t os_start_direct(void);
void task_unpriv_probe(void *out);

/* 연습 4: 칠한 스택에서 사용량 (바이트). 카나리 2 워드 위부터 처음으로 칠이 지워진 곳까지 */
uint32_t stack_high_water(const struct tcb *t) {
    uint32_t top = (uint32_t)(uintptr_t)(t->stack_lo + t->stack_words) & ~7u;
    const uint32_t *p = t->stack_lo + 2;
    while ((uint32_t)(uintptr_t)p < top && *p == 0xA5A5A5A5u) p++;
    return top - (uint32_t)(uintptr_t)p;
}

static uint32_t g_stk[3][256] __attribute__((aligned(8)));
static struct tcb T[3];
static struct rc RC[3];
static volatile uint32_t g_probe[3];

static void two_tasks(uint32_t seed) {
    tcb_setup(&T[0], 0, 'A', g_stk[0], 256u); rc_setup(&RC[0], 'A', seed);
    tcb_setup(&T[1], 1, 'B', g_stk[1], 256u); rc_setup(&RC[1], 'B', seed + 1u);
    RC[0].printed = RC[1].printed = 24u;
    init_stack(&T[0], task_regcheck, &RC[0]);
    init_stack(&T[1], task_regcheck, &RC[1]);
}
static void line(const char *tag) {
    sh_puts(tag); sh_puts(" ticks="); sh_dec(g_os.ticks); sh_puts(" switches="); sh_dec(g_os.switches);
    sh_puts(" pendsv="); sh_dec(g_os.pendsv_calls); sh_puts(" A.mism="); sh_dec(RC[0].mismatches);
    sh_puts(" B.mism="); sh_dec(RC[1].mismatches); sh_puts(" A.win="); sh_dec(RC[0].windows_switched); sh_puts("\n");
}

int main(void) {
    sh_puts("ARM-A01 practice (QEMU mps2-an385)\n");
    SCB_SHPR2 = 0u;
    SCB_SHPR3 = (0x40u << 24) | (0xFFu << 16);
    struct tcb *l2[2] = { &T[0], &T[1] };

    /* ---- 연습 1: SVC 없이 시작 ---- */
    two_tasks(0x1001u);
    g_os.ntasks = 2; g_os.task[0] = &T[0]; g_os.task[1] = &T[1];
    g_os.ticks = 0; g_os.tick_limit = 200; g_os.stop = 0; g_os.switches = 0; g_os.pendsv_calls = 0; g_os.ntrace = 0;
    g_wd_ticks = 0;
    g_cur = &T[0];
    uint32_t msp0 = get_msp();
    set_basepri(0x40u);                                 /* 시작 전 틱 막기 (os_run 과 같은 이유) */
    SYST_RVR = TICK_RELOAD - 1u; SYST_CVR = 0u; SYST_CSR = 7u;
    uint32_t mask = os_start_direct();
    SYST_CSR = 0u; SCB_ICSR = ICSR_PENDSTCLR | ICSR_PENDSVCLR;
    line("P1 direct start:");
    CHECK(mask == 0u && get_msp() == msp0 && get_control() == 0u);
    CHECK(RC[0].mismatches == 0u && RC[1].mismatches == 0u && RC[0].windows_switched >= 50u && RC[1].rounds >= 50u);

    /* ---- 연습 2: PENDSVSET 3 번 = PendSV 1 번 ---- */
    two_tasks(0x2002u);
    g_pend_writes = 3u; g_pend_requests = 0;
    mask = os_run(l2, 2, 200);
    g_pend_writes = 1u;
    line("P2 3x PENDSVSET per tick:");
    sh_puts("   requests="); sh_dec(g_pend_requests); sh_puts(" pendsv entries="); sh_dec(g_os.pendsv_calls); sh_puts("\n");
    CHECK(g_pend_requests == 600u && g_os.pendsv_calls == 200u);       /* 펜딩은 비트 하나: 합쳐진다 */
    CHECK(mask == 0u && RC[0].mismatches == 0u && RC[1].mismatches == 0u);

    /* ---- 연습 3: 비특권 태스크 ---- */
    two_tasks(0x3003u);
    tcb_setup(&T[2], 2, 'P', g_stk[2], 256u);
    init_stack(&T[2], task_unpriv_probe, (void *)g_probe);
    struct tcb *l3[3] = { &T[0], &T[1], &T[2] };
    g_task_npriv = 1u;
    mask = os_run(l3, 3, 200);
    g_task_npriv = 0u;
    line("P3 unprivileged tasks:");
    sh_puts("   probe CONTROL="); sh_hex(g_probe[0]); sh_puts(" PRIMASK after cpsid="); sh_hex(g_probe[1]);
    sh_puts(" loops="); sh_dec(g_probe[2]); sh_puts(" main CONTROL after="); sh_hex(g_os_control_after); sh_puts("\n");
    CHECK(g_probe[0] == 3u);                                /* nPRIV | SPSEL */
    CHECK(g_probe[1] == 0u);                                /* 비특권 cpsid i 는 무시된다 */
    CHECK(g_probe[2] > 1000u && RC[0].mismatches == 0u && RC[1].mismatches == 0u);
    CHECK(g_os_control_after == 0u && mask == 0u);          /* main 은 다시 특권 + MSP */

    /* ---- 연습 4: 스택 최고 수위 (방금 돈 연습 3 의 스택) ---- */
    uint32_t hw_a = stack_high_water(&T[0]), hw_p = stack_high_water(&T[2]);
    sh_puts("P4 high-water: A="); sh_dec(hw_a); sh_puts(" B="); sh_dec(stack_high_water(&T[1]));
    sh_puts(" P="); sh_dec(hw_p); sh_puts(" bytes (of 1024)\n");
    CHECK(hw_p == 64u);                                     /* 루프만 도는 태스크: 저장 문맥 16 워드가 전부 */
    CHECK(hw_a >= 8u + 56u && hw_a < 256u);                 /* 최저점 = 검사 단계 (st·카운터 + got[14] + rc_verify 프레임) */
    struct tcb tz; static uint32_t zbuf[32] __attribute__((aligned(8)));
    tcb_setup(&tz, 0, 'Z', zbuf, 32u);
    init_stack(&tz, task_regcheck, 0);
    CHECK(stack_high_water(&tz) == 64u);                    /* 갓 만든 초기 프레임 = 64 B */

    sh_puts("\n"); sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}
