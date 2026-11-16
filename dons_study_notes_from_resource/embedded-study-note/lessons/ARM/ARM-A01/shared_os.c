/* ENV: qemu */
/* ARM-A01 공용 "커널" C 부분 — starter / solution / practice 모두에 링크된다 (TODO 없음).
 * - os_schedule(): PendSV(asm)가 나가는 태스크의 r4–r11 을 쌓은 뒤 부르는 C 함수. 스택 검사 + 라운드로빈.
 * - os_run(): main 이 태스크 목록을 주고 tick_limit 틱 동안 돌린 뒤 main 으로 돌아온다.
 * - SysTick_Handler: 워치독 + TODO os_tick() 호출.
 * - rc_prepare/rc_verify, irq_prepare/irq_verify: asm 검사 태스크·IRQ 가 부르는 패턴 생성/대조.
 * 스케줄러 알고리즘 자체(비트맵·우선순위·PI)는 RTOS-A01 이 다룬다 — 여기선 가장 단순한 라운드로빈.
 */
#include "a01.h"

struct tcb *volatile g_cur;
volatile uint32_t g_main_msp;
volatile struct os g_os;
volatile struct irqstat g_irq;
uint32_t g_wd_ticks;
uint32_t g_os_msp_before, g_os_msp_after, g_os_control_after;
volatile uint32_t g_irq_want[8];           /* IRQ6 asm 이 ldm 으로 읽는 r4–r11 패턴 */

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

void tcb_setup(struct tcb *t, uint32_t id, char name, uint32_t *stack_lo, uint32_t words) {
    t->sp = 0; t->id = id; t->exc_return = 0; t->stack_lo = stack_lo; t->stack_words = words; t->alive = 0;
    t->name = name;
    for (uint32_t i = 0; i < words; i++) stack_lo[i] = 0xA5A5A5A5u;  /* 칠해 두면 gdb 로 사용량이 보인다 */
}

/* ---------------- PendSV → C : 다음 태스크 고르기 ---------------- */
static int stack_ok(const struct tcb *t) {
    return t->sp >= t->stack_lo + 2 && t->stack_lo[0] == CANARY && t->stack_lo[1] == CANARY;
}
struct tcb *os_schedule(uint32_t exc_return) {
    struct tcb *cur = g_cur;
    g_os.pendsv_calls++;
    g_os.last_exc_return = exc_return;
    if (!(exc_return & 8u)) g_os.from_handler++;              /* bit3 = 0 → Handler 모드를 끊었다 (있으면 안 됨) */
    else if (!(exc_return & 4u)) g_os.from_main++;            /* Thread + MSP → main 을 태스크로 착각 (있으면 안 됨) */
    if (cur->alive && !stack_ok(cur)) {                       /* 나가는 태스크 스택 검사 (저장 직후라 sp 가 최저점) */
        g_os.overflow_id = cur->id + 1u;
        g_os.overflow_sp = (uint32_t)(uintptr_t)cur->sp;
        cur->alive = 0;
    }
    if (g_os.stop) goto to_main;
    for (uint32_t k = 1; k <= g_os.ntasks; k++) {             /* 라운드로빈: cur 다음부터 살아 있는 첫 태스크 */
        uint32_t i = (cur->id + k) % g_os.ntasks;
        struct tcb *n = g_os.task[i];
        if (n->alive) {
            if (n != cur) g_os.switches++;
            if (g_os.ntrace < TRACE_LEN) g_os.trace[g_os.ntrace++] = n->name;
            g_cur = n;
            return n;
        }
    }
to_main:                                                      /* 멈춤 요청 또는 살아 있는 태스크 없음 */
    SYST_CSR = 0u;                                            /* 먼저 SysTick 을 끄고 */
    SCB_ICSR = ICSR_PENDSTCLR | ICSR_PENDSVCLR;               /* 그 사이 SysTick 이 PendSV 를 또 pend 했으면 지운다 — 안 지우면 */
                                                              /* main 으로 돌아가자마자 PendSV 가 main 을 끊는다 (gdb 로 실제 재현) */
    return 0;
}

/* ---------------- SysTick: 워치독 + TODO ---------------- */
void SysTick_Handler(void) {
    if (++g_wd_ticks > g_os.tick_limit + 2000u) {             /* 스위처가 main 으로 못 돌아오는 미완성 답 → 무한 대기 대신 종료 */
        sh_puts("\nwatchdog: scheduler did not return to main (ticks="); sh_dec(g_os.ticks);
        sh_puts(" switches="); sh_dec(g_os.switches); sh_puts(")\n");
        sh_puts("FAIL watchdog\n");
        sh_dec(g_pass); sh_puts("/"); sh_dec(g_total + 1u); sh_puts(" passed\n");
        sh_exit(1);
    }
    os_tick();
}

void os_yield(void) {                                         /* Thread 에서 자발적 양보: PendSV 를 pend 만 한다 */
    SCB_ICSR = ICSR_PENDSVSET;
    __asm__ volatile("dsb\n isb" ::: "memory");
}

/* 태스크 함수가 return 하면 초기 프레임의 LR(= task_exit_trap, shared_tasks.S) 이 r0 을 그대로 들고 여기로 온다 */
void task_exit_c(uint32_t r0) {
    g_os.exited_r0 = r0;                                      /* 태스크 반환값 */
    g_os.exited_id = g_cur->id + 1u;
    g_cur->alive = 0;
    for (;;) os_yield();                                      /* 죽은 태스크는 다시 고르지 않으니 안 돌아온다 */
}

/* main → 태스크들 → main. 반환 = os_start 의 "main r4–r11 손상 비트마스크" */
uint32_t os_run(struct tcb **list, uint32_t n, uint32_t tick_limit) {
    g_os.ntasks = n;
    for (uint32_t i = 0; i < n; i++) g_os.task[i] = list[i];
    g_os.ticks = 0; g_os.tick_limit = tick_limit; g_os.stop = 0;
    g_os.switches = 0; g_os.pendsv_calls = 0; g_os.from_handler = 0; g_os.from_main = 0; g_os.ntrace = 0;
    g_os.overflow_id = 0; g_os.exited_id = 0; g_os.rounds_no_tick = 0;
    g_wd_ticks = 0;
    g_cur = list[0];
    set_basepri(0x40u);                                       /* SysTick(0x40)·PendSV 를 막아 둔다: svc #0 전에 틱이 오면 */
                                                              /* PendSV 가 main(MSP) 을 "태스크" 로 착각해 스위치한다 (gdb 로 실제 재현) */
    SYST_RVR = TICK_RELOAD - 1u;
    SYST_CVR = 0u;
    SYST_CSR = 7u;                                            /* CPU 클럭, TICKINT, ENABLE */
    g_os_msp_before = get_msp();
    uint32_t mask = os_start();                               /* svc #0 → 첫 태스크 … PendSV 가 여기로 돌려보낸다 */
    g_os_msp_after = get_msp();
    g_os_control_after = get_control();
    set_basepri(0u);                                          /* starter(SVC 스텁) 처럼 SVC 가 안 풀었을 때 대비 */
    SYST_CSR = 0u;                                            /* 미완성 답(starter)에서도 SysTick 이 남지 않게 */
    SCB_ICSR = ICSR_PENDSTCLR | ICSR_PENDSVCLR;
    return mask;
}

/* ---------------- 레지스터 검사 태스크의 C 부분 (인터럽트 마스크 상태에서 불린다) ---------------- */
void rc_setup(struct rc *st, char name, uint32_t seed) {
    uint8_t *p = (uint8_t *)st;
    for (size_t i = 0; i < sizeof *st; i++) p[i] = 0;
    st->name = name; st->rng = seed; st->spin_min = 2000u; st->spin_mask = 4095u;
}
uint32_t rc_prepare(struct rc *st) {
    for (int k = 0; k < 14; k++) st->want[k] = xs32(&st->rng);
    for (int k = 0; k < 32; k++) st->fwant[k] = xs32(&st->rng);
    st->sw_at_prepare = g_os.switches;
    return st->spin_min + (xs32(&st->rng) & st->spin_mask);   /* 스핀 횟수 (≥ 1) */
}
/* got[0..13] = 창(window) 끝에서 push 한 r0..r12, lr. fgot = s0..s31 (FPU 태스크만, 아니면 NULL) */
void rc_verify(struct rc *st, const uint32_t *got, const uint32_t *fgot) {
    uint32_t m = 0, fm = 0;
    for (int k = 0; k < 14; k++) if (got[k] != st->want[k]) m |= 1u << (k == 13 ? 14 : k);   /* bit14 = lr */
    if (fgot) for (int k = 0; k < 32; k++) if (fgot[k] != st->fwant[k]) fm |= 1u << k;
    if (m || fm) { st->mismatches++; st->mism_mask |= m; st->fmism_mask |= fm; }
    st->rounds++;
    if (g_os.ticks == 0u && ++g_os.rounds_no_tick > 2000u) {  /* 태스크는 도는데 SysTick 이 한 번도 안 옴 */
        sh_puts("\nwatchdog: tasks run but no SysTick (BASEPRI still raised? SysTick off?)\nFAIL watchdog\n");
        sh_dec(g_pass); sh_puts("/"); sh_dec(g_total + 1u); sh_puts(" passed\n");
        sh_exit(1);
    }
    if (g_os.switches != st->sw_at_prepare) {                 /* 창 안에서 다른 태스크가 돌았다 = 진짜 검사 */
        st->windows_switched++;
        if (st->printed < 24u) { sh_putc(st->name); st->printed++; }   /* 교대 출력: 다시 돌아올 때마다 한 글자 */
    }
    if (st->irq_every && st->rounds % st->irq_every == 0u) { st->irqs++; NVIC_ISPR0 = 1u << IRQ_BUSY; }
    if (st->yield_every && st->rounds % st->yield_every == 0u) { st->yields++; os_yield(); }
}

/* ---------------- 바쁜 IRQ6 의 C 부분 ---------------- */
void irq_setup(uint32_t seed) {
    g_irq.rng = seed; g_irq.runs = 0; g_irq.mismatches = 0; g_irq.mism_mask = 0;
    g_irq.ticks_in_window = 0; g_irq.windows_with_tick = 0;
}
uint32_t irq_prepare(void) {
    uint32_t s = g_irq.rng;
    for (int k = 0; k < 8; k++) g_irq_want[k] = g_irq.want[k] = xs32(&s);
    g_irq.rng = s;
    g_irq.tick_at_prepare = g_os.ticks;
    return 60000u;                                            /* 틱 주기(1000 사이클)보다 훨씬 긴 창 → 안에서 SysTick 이 반드시 뜬다 */
}
void irq_verify(const uint32_t *got) {                        /* got[0..7] = r4..r11 */
    uint32_t m = 0;
    for (int k = 0; k < 8; k++) if (got[k] != g_irq.want[k]) m |= 1u << (k + 4);
    if (m) { g_irq.mismatches++; g_irq.mism_mask |= m; }
    g_irq.runs++;
    uint32_t dt = g_os.ticks - g_irq.tick_at_prepare;
    g_irq.ticks_in_window += dt;
    if (dt) g_irq.windows_with_tick++;
}
