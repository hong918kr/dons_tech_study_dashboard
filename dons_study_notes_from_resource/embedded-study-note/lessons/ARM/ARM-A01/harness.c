/* ENV: qemu */
/* ARM-A01 harness.c — 펌웨어 테스트. starter.c / solution.c 가 맨 위에서 #include 한다.
 * 빌드: run_qemu.sh [starter|solution|fpu]  →  <v>.c(+이 파일) + <v>.S + shared_*.c + shared_*.S
 *   -mcpu=cortex-m3 → QEMU mps2-an385 / -DHAS_FPU -mcpu=cortex-m4 -mfloat-abi=hard → mps2-an386
 * 출력: PASS/FAIL 줄, "N/M passed", SYS_EXIT 코드 (0 = 전부 통과).
 * -DBUGS -DBUGSEL=n (run_qemu.sh bugs) 이면 Part B 결함 주입 실험 n 만 돌린다 (demo_bugs.S 의 잘못된 핸들러를 VTOR 로
 * 끼우거나 우선순위를 바꿈). "기대한 고장이 실제로 났다" 가 PASS.
 */
#include "a01.h"

static uint32_t g_stk[4][256] __attribute__((aligned(8)));
static uint32_t g_tiny_area[128 + 96] __attribute__((aligned(8), unused));   /* [0..127] 희생 가드, [128..] 작은 스택 96 워드 */
static struct tcb T[4];
static struct rc RC[4];

__attribute__((unused)) static void nl(void) { sh_puts("\n"); }
__attribute__((unused)) static void kv(const char *k, uint32_t v) { sh_puts(k); sh_dec(v); }
__attribute__((unused)) static void kx(const char *k, uint32_t v) { sh_puts(k); sh_hex(v); }

/* 태스크 n 개 준비: i 번째는 entry_i(arg_i), 스택 g_stk[i] */
__attribute__((unused)) static void make_task(uint32_t i, char name, void (*entry)(void *), void *arg) {
    tcb_setup(&T[i], i, name, g_stk[i], 256u);
    init_stack(&T[i], entry, arg);
}
__attribute__((unused)) static void prio_setup(uint32_t pendsv, uint32_t systick) {
    SCB_SHPR2 = 0u;                                         /* SVCall = 0 (최고) */
    SCB_SHPR3 = (systick << 24) | (pendsv << 16);
}
__attribute__((unused)) static void report_rc(const struct rc *s) {
    sh_puts("   task "); sh_putc(s->name); kv(": rounds=", s->rounds); kv(" windows_switched=", s->windows_switched);
    kv(" mismatches=", s->mismatches); kx(" mask=", s->mism_mask);
#ifdef HAS_FPU
    kx(" fmask=", s->fmism_mask);
#endif
    nl();
}
__attribute__((unused)) static void report_os(const char *tag, uint32_t mask) {
    sh_puts(tag); kv(" ticks=", g_os.ticks); kv(" switches=", g_os.switches); kv(" pendsv=", g_os.pendsv_calls);
    kv(" from_handler=", g_os.from_handler); kx(" main_mask=", mask); nl();
}
__attribute__((unused)) static int trace_round_robin(uint32_t n) {                  /* trace[i+1] == trace[i] 의 다음 (A→B→…→A) */
    if (g_os.ntrace < 4u) return 0;
    for (uint32_t i = 0; i + 1 < g_os.ntrace; i++) {
        char a = g_os.trace[i], b = g_os.trace[i + 1];
        if (b != (char)('A' + ((uint32_t)(a - 'A') + 1u) % n)) return 0;
    }
    return 1;
}
__attribute__((unused)) static void print_trace(uint32_t max) {
    sh_puts("   trace: ");
    for (uint32_t i = 0; i < g_os.ntrace && i < max; i++) sh_putc(g_os.trace[i]);
    nl();
}

/* 반환하는 태스크 (exit trap 확인용). 반환값이 r0 에 확실히 남도록 uint32_t 를 돌려주는 함수로 쓰고,
 * init_stack 의 entry 타입(void (*)(void *))으로는 void (*)(void) 를 거쳐 캐스트한다 (-Wcast-function-type 회피).
 * 호출은 C 가 아니라 하드웨어(예외 복귀)가 하므로 AAPCS 대로 r0 = arg, 끝나면 r0 = 반환값, LR 로 점프. */
__attribute__((unused)) static uint32_t task_ret_fn(void *arg) {
    for (int i = 0; i < 3; i++) os_yield();                 /* 몇 번 양보한 뒤 */
    return (uint32_t)(uintptr_t)arg ^ 0x5A5A0000u;
}
#define task_ret ((void (*)(void *))(void (*)(void))task_ret_fn)

/* 스택을 일부러 많이 쓰는 재귀 (작은 스택 오버플로 실험). volatile 배열 + 호출 뒤 읽기 → 꼬리 재귀 최적화 불가 */
__attribute__((noinline)) static uint32_t deep(uint32_t n) {
    volatile uint32_t buf[8];
    for (uint32_t i = 0; i < 8u; i++) buf[i] = n * 8u + i;
    if (n == 0u) { os_yield(); return buf[7]; }             /* 가장 깊은 곳에서 스위치 → PendSV 가 sp 를 검사 */
    uint32_t r = deep(n - 1u);
    return r + buf[n & 7u];
}
static volatile uint32_t g_deep_result;
__attribute__((unused)) static void task_deep(void *arg) {
    g_deep_result = deep((uint32_t)(uintptr_t)arg);
    for (;;) os_yield();                                    /* 살아남으면 계속 양보 */
}

#ifndef BUGS
int main(void) {
#ifdef HAS_FPU
    sh_puts("ARM-A01 PendSV context switch in asm (QEMU mps2-an386, Cortex-M4F)\n");
#else
    sh_puts("ARM-A01 PendSV context switch in asm (QEMU mps2-an385, Cortex-M3)\n");
#endif
    uint32_t mask;

    /* ================= Part A0: init_stack 단위 테스트 ================= */
    sh_puts("A0 init_stack\n");
    tcb_setup(&T[0], 0, 'A', g_stk[0], 256u);
    uint32_t *sp = init_stack(&T[0], task_regcheck, (void *)0x12345678u);
    CHECK(sp == &g_stk[0][256 - 16] && T[0].sp == sp);
    CHECK(sp[15] == XPSR_INIT);                                                   /* xPSR: T 비트만 */
    CHECK(((uint32_t)(uintptr_t)task_regcheck & 1u) == 1u);                       /* 함수 포인터에는 Thumb 비트가 있다 */
    CHECK(sp[14] == ((uint32_t)(uintptr_t)task_regcheck & ~1u));                  /* 프레임 PC 는 bit0 = 0 */
    CHECK(sp[13] == (uint32_t)(uintptr_t)task_exit_trap);                         /* LR = exit trap (bit0 = 1 그대로) */
    CHECK(sp[8] == 0x12345678u);                                                  /* r0 = arg */
    CHECK(sp[0] == 0x04040404u && sp[7] == 0x0B0B0B0Bu && sp[12] == 0x0C0C0C0Cu); /* r4, r11, r12 채움 값 */
    CHECK(sp[9] == 0x01010101u && sp[10] == 0x02020202u && sp[11] == 0x03030303u);
    CHECK(g_stk[0][0] == CANARY && g_stk[0][1] == CANARY);
    CHECK(T[0].exc_return == EXC_RETURN_THREAD_PSP && T[0].alive == 1u);
    tcb_setup(&T[1], 1, 'B', g_stk[1], 255u);                                     /* top = &g_stk[1][255] → 4 mod 8 */
    sp = init_stack(&T[1], task_regcheck, 0);
    kx("   top(misaligned)=", (uint32_t)(uintptr_t)&g_stk[1][255]); kx(" -> sp=", (uint32_t)(uintptr_t)sp); nl();
    CHECK(((uint32_t)(uintptr_t)sp & 7u) == 0u && sp == &g_stk[1][254 - 16]);     /* 8 바이트로 내려 정렬 */
    CHECK(g_stk[1][254] == 0xA5A5A5A5u);                                          /* 정렬로 버린 칸은 안 건드림 */

    /* ================= Part A1: 두 검사 태스크, SysTick 선점만 ================= */
    sh_puts("A1 two regcheck tasks, SysTick -> PENDSVSET -> PendSV\n   output: ");
    prio_setup(0xFFu, 0x40u);                               /* PendSV 최저, SysTick 0x40 */
    rc_setup(&RC[0], 'A', 0x1111u); rc_setup(&RC[1], 'B', 0x2222u);
    make_task(0, 'A', task_regcheck, &RC[0]);
    make_task(1, 'B', task_regcheck, &RC[1]);
    struct tcb *l2[2] = { &T[0], &T[1] };
    mask = os_run(l2, 2, 300);
    nl(); report_os("A1", mask); report_rc(&RC[0]); report_rc(&RC[1]); print_trace(40);
    CHECK(mask == 0u);                                      /* main 의 r4–r11 이 그대로 */
    CHECK(g_os_msp_after == g_os_msp_before);               /* MSP 도 그대로 */
    CHECK((g_os_control_after & 3u) == 0u);                 /* 특권 + MSP 로 돌아옴 */
    CHECK(g_os.ticks == 300u && g_os.switches >= 290u);
    CHECK(g_os.from_handler == 0u && g_os.from_main == 0u);
    CHECK(RC[0].mismatches == 0u && RC[1].mismatches == 0u);
    CHECK(RC[0].windows_switched >= 50u && RC[1].windows_switched >= 50u);   /* 창 안에서 실제로 스위치됨 */
    CHECK(trace_round_robin(2));                            /* ABAB… */
    CHECK(g_irq.runs == 0u);

    /* ================= Part A2: 세 태스크 + 자발적 yield ================= */
    sh_puts("A2 three tasks, C yields every round\n");
    rc_setup(&RC[0], 'A', 0x3333u); rc_setup(&RC[1], 'B', 0x4444u); rc_setup(&RC[2], 'C', 0x5555u);
    RC[2].yield_every = 1u;
    RC[0].printed = RC[1].printed = RC[2].printed = 24u;    /* 글자 출력은 A1 / C1 에서만 */
    make_task(0, 'A', task_regcheck, &RC[0]);
    make_task(1, 'B', task_regcheck, &RC[1]);
    make_task(2, 'C', task_regcheck, &RC[2]);
    struct tcb *l3[3] = { &T[0], &T[1], &T[2] };
    mask = os_run(l3, 3, 300);
    report_os("A2", mask); report_rc(&RC[0]); report_rc(&RC[1]); report_rc(&RC[2]); print_trace(40);
    kv("   C yields=", RC[2].yields); nl();
    CHECK(mask == 0u && g_os_msp_after == g_os_msp_before);
    CHECK(RC[0].mismatches == 0u && RC[1].mismatches == 0u && RC[2].mismatches == 0u);
    CHECK(RC[2].yields >= 50u && g_os.switches > g_os.ticks);   /* yield 만큼 틱보다 스위치가 많다 */
    CHECK(trace_round_robin(3));                            /* ABCABC… */

    /* ================= Part A3: 반환하는 태스크 → exit trap ================= */
    sh_puts("A3 task returns -> LR = task_exit_trap\n");
    rc_setup(&RC[0], 'A', 0x6666u);
    RC[0].printed = 24u;
    make_task(0, 'A', task_regcheck, &RC[0]);
    make_task(1, 'D', task_ret, (void *)0x0000BEEFu);
    mask = os_run(l2, 2, 100);
    report_os("A3", mask); report_rc(&RC[0]);
    kx("   exited_id=", g_os.exited_id); kx(" exited_r0=", g_os.exited_r0); nl();
    CHECK(g_os.exited_id == 2u && T[1].alive == 0u);
    CHECK(g_os.exited_r0 == (0x0000BEEFu ^ 0x5A5A0000u));
    CHECK(RC[0].mismatches == 0u && RC[0].rounds >= 10u && mask == 0u);

    /* ================= Part B4: 바쁜 IRQ + PendSV 최저 우선순위 ================= */
    sh_puts("B4 busy IRQ6 (0x80) preempted by SysTick (0x40), PendSV 0xFF\n");
    prio_setup(0xFFu, 0x40u);
    NVIC_IPR(IRQ_BUSY) = 0x80u;
    NVIC_ISER0 = 1u << IRQ_BUSY;
    irq_setup(0x7777u);
    rc_setup(&RC[0], 'A', 0x8888u); rc_setup(&RC[1], 'B', 0x9999u);
    RC[0].irq_every = 3u;
    RC[0].printed = RC[1].printed = 24u;
    make_task(0, 'A', task_regcheck, &RC[0]);
    make_task(1, 'B', task_regcheck, &RC[1]);
    mask = os_run(l2, 2, 300);
    NVIC_ICER0 = 1u << IRQ_BUSY;
    report_os("B4", mask); report_rc(&RC[0]); report_rc(&RC[1]);
    kv("   irq runs=", g_irq.runs); kv(" windows_with_tick=", g_irq.windows_with_tick);
    kv(" ticks_in_windows=", g_irq.ticks_in_window); kv(" irq mismatches=", g_irq.mismatches); nl();
    CHECK(g_irq.runs >= 3u && g_irq.windows_with_tick == g_irq.runs);   /* IRQ 창마다 SysTick 이 끼어들었고 */
    CHECK(g_irq.mismatches == 0u && g_os.from_handler == 0u);           /* 그래도 PendSV 는 IRQ 가 끝난 뒤에만 */
    CHECK(RC[0].mismatches == 0u && RC[1].mismatches == 0u);

    /* ================= Part B5: 작은 스택 오버플로 → 카나리 ================= */
    sh_puts("B5 tiny stack (96 words) + recursion, canary check in os_schedule\n");
    for (uint32_t depth = 1; depth <= 12u; depth += 11u) {
        for (uint32_t i = 0; i < 128u; i++) g_tiny_area[i] = 0x6A6A6A6Au;     /* 가드 칠하기 */
        rc_setup(&RC[0], 'A', 0xAAAAu + depth);
        RC[0].printed = 24u;
        make_task(0, 'A', task_regcheck, &RC[0]);
        tcb_setup(&T[1], 1, 'E', &g_tiny_area[128], 96u);
        init_stack(&T[1], task_deep, (void *)(uintptr_t)depth);
        g_deep_result = 0;
        mask = os_run(l2, 2, 60);
        uint32_t lowest = 128u;
        for (uint32_t i = 0; i < 128u; i++) if (g_tiny_area[i] != 0x6A6A6A6Au) { lowest = i; break; }
        sh_puts("   depth="); sh_dec(depth); kx(" overflow_id=", g_os.overflow_id); kx(" overflow_sp=", g_os.overflow_sp);
        kx(" stack_lo=", (uint32_t)(uintptr_t)&g_tiny_area[128]); kv(" guard words touched=", 128u - lowest);
        kv(" E.alive=", T[1].alive); kv(" A.rounds=", RC[0].rounds); nl();
        if (depth == 1u) {
            CHECK(g_os.overflow_id == 0u && T[1].alive == 1u && lowest == 128u);   /* 들어감: 가드 안 건드림 */
        } else {
            CHECK(g_os.overflow_id == 2u && T[1].alive == 0u);                     /* 넘침: 잡혀서 E 만 죽음 */
            CHECK(g_os.overflow_sp < (uint32_t)(uintptr_t)&g_tiny_area[128] && lowest < 128u);
            CHECK(RC[0].mismatches == 0u && RC[0].rounds >= 5u);                   /* A 는 계속 정상 */
        }
    }

#ifdef HAS_FPU
    /* ================= Part C: FPU 태스크 (s16–s31 수동 저장, EXC_RETURN bit4) ================= */
    sh_puts("C1 FPU-aware switch: F, G use s0-s31, I is integer-only\n");
    kx("   FPCCR at reset = ", FPCCR); nl();
    CHECK((FPCCR & 0xC0000000u) == 0xC0000000u);            /* ASPEN = LSPEN = 1 (lazy stacking 켜짐) */
    sh_puts("   output: ");
    prio_setup(0xFFu, 0x40u);
    rc_setup(&RC[0], 'F', 0xF00Du); rc_setup(&RC[1], 'G', 0xFEEDu); rc_setup(&RC[2], 'I', 0xBEEFu);
    make_task(0, 'F', task_fpcheck, &RC[0]);
    make_task(1, 'G', task_fpcheck, &RC[1]);
    make_task(2, 'I', task_regcheck, &RC[2]);
    mask = os_run(l3, 3, 300);
    nl(); report_os("C1", mask); report_rc(&RC[0]); report_rc(&RC[1]); report_rc(&RC[2]);
    kx("   EXC_RETURN saved: F=", T[0].exc_return); kx(" G=", T[1].exc_return); kx(" I=", T[2].exc_return); nl();
    CHECK(mask == 0u && g_os_msp_after == g_os_msp_before);
    CHECK(RC[0].mismatches == 0u && RC[1].mismatches == 0u && RC[2].mismatches == 0u);
    CHECK(RC[0].fmism_mask == 0u && RC[1].fmism_mask == 0u);
    CHECK(RC[0].windows_switched >= 30u && RC[1].windows_switched >= 30u);
    CHECK(T[0].exc_return == 0xFFFFFFEDu && T[1].exc_return == 0xFFFFFFEDu);   /* FP 문맥 있는 PSP 태스크 */
    CHECK(T[2].exc_return == 0xFFFFFFFDu);                                       /* 정수 전용 → 기본 프레임 */
#endif

    nl(); sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}
#else  /* BUGS */
/* ================= Part B 결함 주입: 잘못된 SVC/PendSV 를 RAM 벡터 테이블로 끼워 넣는다 ================= */
void SVC_Handler(void);
void PendSV_Handler(void);
void SVC_bug_no_r11(void);
void PendSV_bug_no_r11(void);
void PendSV_bug_no_isb(void);
#ifdef HAS_FPU
void PendSV_bug_no_fp(void);
#endif
static void (*g_ram_vec[48])(void) __attribute__((aligned(256)));
__attribute__((unused)) static void use_handlers(void (*svc)(void), void (*pendsv)(void)) {
    for (int i = 0; i < 48; i++) g_ram_vec[i] = g_vectors[i];
    g_ram_vec[11] = svc;
    g_ram_vec[14] = pendsv;
    SCB_VTOR = (uint32_t)(uintptr_t)g_ram_vec;              /* 256 B 정렬 (48 엔트리 → 2 의 거듭제곱으로 올림) */
    __asm__ volatile("dsb\n isb" ::: "memory");
}
/* 초기 프레임을 "r4–r10 7 칸" 짜리로: r4..r10 을 한 칸 위로 밀고 sp 를 한 칸 올린다 (r11 칸이 사라짐) */
__attribute__((unused)) static void frame_to_7(struct tcb *t) {
    uint32_t *sp = t->sp;
    for (int k = 6; k >= 0; k--) sp[k + 1] = sp[k];
    t->sp = sp + 1;
}
__attribute__((unused)) static uint32_t run_ab(uint32_t ticks, uint32_t irq_every, uint32_t seed, int seven) {
    rc_setup(&RC[0], 'A', seed); rc_setup(&RC[1], 'B', seed + 1u);
    RC[0].irq_every = irq_every;
    RC[0].printed = RC[1].printed = 16u;                    /* 결함 실험은 글자 8 개씩만 */
    make_task(0, 'A', task_regcheck, &RC[0]);
    make_task(1, 'B', task_regcheck, &RC[1]);
    if (seven) { frame_to_7(&T[0]); frame_to_7(&T[1]); }
    struct tcb *l2[2] = { &T[0], &T[1] };
    return os_run(l2, 2, ticks);
}
#ifndef HAS_FPU
/* BUGSEL 2, 3 은 HardFault 로 끝난다 → 여기서 관찰값을 검사하고 바로 종료 (복구하지 않는다) */
static volatile uint32_t g_expect_hf;
extern char os_start_ret[];
void hardfault_c(const uint32_t *msp, uint32_t lr, const uint32_t *psp) {   /* demo_bugs.S 스텁이 b 로 넘어온다 */
    uint32_t cfsr = SCB_CFSR, hfsr = SCB_HFSR;
    nl(); sh_puts("   HardFault: EXC_RETURN="); sh_hex(lr); kx(" CFSR=", cfsr); kx(" HFSR=", hfsr);
    kv(" g_cur=", g_cur->id); nl();
    sh_puts("   MSP "); sh_hex((uint32_t)(uintptr_t)msp); sh_puts(":");
    for (int i = 0; i < 8; i++) { sh_puts(" "); sh_hex(msp[i]); }
    nl();
    sh_puts("   PSP "); sh_hex((uint32_t)(uintptr_t)psp); sh_puts(":");
    for (int i = 0; i < 8 && (uint32_t)(uintptr_t)(psp + i) < 0x20010000u; i++) { sh_puts(" "); sh_hex(psp[i]); }
    nl();
    if (!g_expect_hf) sh_exit(3);
    CHECK(cfsr == (1u << 18) && (hfsr & (1u << 30)));       /* UFSR.INVPC → HardFault 로 승격 (FORCED) */
#if BUGSEL == 2
    report_os("   BUG2 at fault:", 0); report_rc(&RC[0]); report_rc(&RC[1]);
    kv("   irq runs=", g_irq.runs); kv(" irq mismatches=", g_irq.mismatches); kx(" irq mask=", g_irq.mism_mask);
    kx(" last PendSV lr=", g_os.last_exc_return); nl();
    CHECK(g_os.from_handler > 0u);                          /* PendSV 가 IRQ6 를 끊고 들어왔다 (lr = 0xFFFFFFF1) */
    CHECK(g_os.stop == 1u && lr == 0xFFFFFFF9u);            /* main 복귀 경로(EXC_RETURN F9)에서 터졌다 */
    CHECK(g_irq.mismatches + RC[0].mismatches + RC[1].mismatches > 0u);   /* 그 전에 r4–r11 이 이미 샜다 */
    CHECK(msp == (const uint32_t *)(uintptr_t)(g_main_msp + 40u));       /* 새 프레임 없이 "현재 스택" = main 의 svc 프레임 */
    CHECK(msp[6] == (uint32_t)(uintptr_t)os_start_ret);                   /* 그 프레임의 PC = svc 다음 명령 */
#elif BUGSEL == 5
    kv("   from_main=", g_os.from_main); kx(" last PendSV lr=", g_os.last_exc_return); nl();
    CHECK(g_os.from_main > 0u);                             /* PendSV 가 svc 전의 main 을 끊었다 */
#elif BUGSEL == 3
    kx("   B init frame at ", (uint32_t)(uintptr_t)&g_stk[1][256 - 16]); nl();
    CHECK(g_cur == &T[1]);                                  /* 처음으로 PendSV 를 통해 들어가는 B 에서 */
    CHECK(psp == &g_stk[1][256 - 16 + 7 + 8]);              /* r11 칸부터 8 워드를 "하드웨어 프레임" 으로 꺼냈다 */
    CHECK(msp[0] == 0x0B0B0B0Bu && msp[1] == (uint32_t)(uintptr_t)&RC[1]);    /* r0 = r11 칸, r1 = arg 칸 … 한 칸씩 밀림 */
    CHECK(msp[6] == ((uint32_t)(uintptr_t)task_exit_trap & ~1u));          /* PC = 초기 프레임의 LR 칸 */
    CHECK((msp[7] & 0x1FFu) == ((uint32_t)(uintptr_t)task_regcheck & 0x1FEu) && (msp[7] & 0x1FFu) != 0u);  /* IPSR ≠ 0 */
    /* ↑ QEMU 가 MSP 에 다시 쌓은 프레임: xPSR 칸에 들어간 값 = 원래 PC 칸 (entry 주소). 그 하위 9 비트가 IPSR → Thread 복귀 실패 */
#endif
    nl(); sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" expected bug outcomes observed\n");
    sh_exit(g_pass == g_total ? 0 : 1);
}
#endif
int main(void) {
    uint32_t mask;
#if BUGSEL == 1
    sh_puts("ARM-A01 BUGS 1 (mps2-an385)\n");
    prio_setup(0xFFu, 0x40u);
    sh_puts("BUG1a SVC+PendSV both {r4-r10}, init frame 15 words (r11 forgotten everywhere)\n   output: ");
    use_handlers(SVC_bug_no_r11, PendSV_bug_no_r11);
    mask = run_ab(200, 0, 0x1234u, 1);
    nl();
    report_os("BUG1a", mask); report_rc(&RC[0]); report_rc(&RC[1]);
    CHECK(RC[0].mismatches > 0u && RC[1].mismatches > 0u);                 /* 잡혔다 */
    CHECK(RC[0].mism_mask == (1u << 11) && RC[1].mism_mask == (1u << 11)); /* 정확히 r11 만 */
    CHECK(mask == (1u << 11));                                             /* main 의 r11 도 (SVC 가 안 쌓음) */

    sh_puts("BUG3 no isb after msr psp\n   output: ");
    use_handlers(SVC_Handler, PendSV_bug_no_isb);
    mask = run_ab(200, 0, 0x4321u, 0);
    nl();
    report_os("BUG3", mask); report_rc(&RC[0]); report_rc(&RC[1]);
    CHECK(mask == 0u && RC[0].mismatches == 0u && RC[1].mismatches == 0u && RC[0].windows_switched >= 50u);  /* 차이 없음 */
#elif BUGSEL == 2
    sh_puts("ARM-A01 BUGS 2 (mps2-an385)\n");
    sh_puts("BUG2 PendSV priority 0x40 (= SysTick, above busy IRQ6 0x80)\n   output: ");
    prio_setup(0x40u, 0x40u);
    NVIC_IPR(IRQ_BUSY) = 0x80u;
    NVIC_ISER0 = 1u << IRQ_BUSY;
    irq_setup(0x7777u);
    g_expect_hf = 1u;
    mask = run_ab(300, 3, 0x8888u, 0);
    nl(); report_os("BUG2 (no fault?!)", mask);
    CHECK(0 && "BUG2 should have faulted");
#elif BUGSEL == 5
    sh_puts("ARM-A01 BUGS 5 (mps2-an385)\n");
    prio_setup(0xFFu, 0x40u);
    sh_puts("BUG5 control run (BASEPRI guard in os_run)\n   output: ");
    mask = run_ab(100, 0, 0x5151u, 0);
    nl(); report_os("BUG5c", mask);
    CHECK(mask == 0u && g_os.from_main == 0u && RC[0].mismatches == 0u);
    sh_puts("BUG5 race: SysTick already pending when os_run reaches svc, no BASEPRI guard\n   output: ");
    g_expect_hf = 1u;
    rc_setup(&RC[0], 'A', 0x5252u); rc_setup(&RC[1], 'B', 0x5353u);
    RC[0].printed = RC[1].printed = 16u;
    make_task(0, 'A', task_regcheck, &RC[0]);
    make_task(1, 'B', task_regcheck, &RC[1]);
    g_os.ntasks = 2; g_os.task[0] = &T[0]; g_os.task[1] = &T[1];
    g_os.ticks = 0; g_os.tick_limit = 100; g_os.stop = 0; g_os.from_main = 0; g_os.switches = 0;
    g_wd_ticks = 0; g_cur = &T[0];
    SYST_RVR = TICK_RELOAD - 1u; SYST_CVR = 0u; SYST_CSR = 7u;
    SCB_ICSR = 1u << 26;                                    /* PENDSTSET: "svc 직전에 틱이 왔다" 를 강제로 */
    mask = os_start();                                      /* (위에서 이미 PendSV 가 main 을 끊었다) */
    SYST_CSR = 0u;
    nl(); report_os("BUG5 (no fault)", mask);
    kv("   from_main=", g_os.from_main); kx(" A.mism mask=", RC[0].mism_mask); kx(" B.mism mask=", RC[1].mism_mask); nl();
    CHECK(g_os.from_main > 0u);
    CHECK(mask != 0u || RC[0].mismatches + RC[1].mismatches > 0u);
#elif BUGSEL == 3
    sh_puts("ARM-A01 BUGS 3 (mps2-an385)\n");
    prio_setup(0xFFu, 0x40u);
    sh_puts("BUG1b PendSV {r4-r10} but SVC + init frame still 8 slots (expect HardFault)\n   output: ");
    use_handlers(SVC_Handler, PendSV_bug_no_r11);
    g_expect_hf = 1u;
    mask = run_ab(200, 0, 0x5678u, 0);
    nl(); report_os("BUG1b (no fault?!)", mask);
    CHECK(0 && "BUG1b should have faulted");
#else
    sh_puts("ARM-A01 BUGS 4 (mps2-an386, Cortex-M4F)\n");
    prio_setup(0xFFu, 0x40u);
    struct tcb *l2[2] = { &T[0], &T[1] };
    for (int lspen = 0; lspen <= 1; lspen++) {
        if (lspen) FPCCR |= 1u << 30; else FPCCR &= ~(1u << 30);
        sh_puts("BUG4 PendSV ignores EXC_RETURN bit4 (no s16-s31 save), FPCCR.LSPEN="); sh_dec((uint32_t)lspen);
        sh_puts("\n   output: ");
        use_handlers(SVC_Handler, PendSV_bug_no_fp);
        rc_setup(&RC[0], 'F', 0xF00Du); rc_setup(&RC[1], 'G', 0xFEEDu);
        RC[0].printed = RC[1].printed = 16u;
        make_task(0, 'F', task_fpcheck, &RC[0]);
        make_task(1, 'G', task_fpcheck, &RC[1]);
        mask = os_run(l2, 2, 200);
        nl();
        report_os("BUG4", mask); report_rc(&RC[0]); report_rc(&RC[1]);
        CHECK(mask == 0u && RC[0].mism_mask == 0u && RC[1].mism_mask == 0u);  /* 정수 레지스터는 멀쩡 */
        CHECK((RC[0].fmism_mask & 0xFFFF0000u) == 0xFFFF0000u);              /* s16–s31 은 항상 깨짐 */
        if (!lspen) CHECK(RC[0].fmism_mask == 0xFFFF0000u);                  /* 즉시 저장: s0–s15 는 하드웨어가 지킴 */
        else        CHECK(RC[0].fmism_mask == 0xFFFFFFFFu);                  /* lazy: s0–s15 도 깨짐! */
    }
    uint32_t fpccr = FPCCR, fpcar = REG32(0xE000EF38u);
    kx("   after lazy run, back in main: FPCCR=", fpccr); kx(" FPCAR=", fpcar);
    kx(" (F stack ", (uint32_t)(uintptr_t)g_stk[0]); kx(" G stack ", (uint32_t)(uintptr_t)g_stk[1]); sh_puts(")\n");
    CHECK((fpccr & 1u) == 1u);                              /* LSPACT 가 1 인 채로 남았다 (FP 명령을 한 번도 안 써서) */
    kv("   g_cur at stop = ", g_cur->id); nl();
    CHECK(fpcar >= (uint32_t)(uintptr_t)g_cur->stack_lo && fpcar < (uint32_t)(uintptr_t)(g_cur->stack_lo + 256));  /* 마지막 태스크 스택 */
    FPCCR = fpccr & ~1u;                                    /* 다음 FP 명령이 F 스택에 s0–s15 를 쏟지 않게 LSPACT 정리 */

    sh_puts("BUG4 control: correct PendSV, LSPEN=1\n   output: ");
    use_handlers(SVC_Handler, PendSV_Handler);
    rc_setup(&RC[0], 'F', 0xF00Du); rc_setup(&RC[1], 'G', 0xFEEDu);
    RC[0].printed = RC[1].printed = 16u;
    make_task(0, 'F', task_fpcheck, &RC[0]);
    make_task(1, 'G', task_fpcheck, &RC[1]);
    mask = os_run(l2, 2, 200);
    nl();
    report_os("BUG4c", mask); report_rc(&RC[0]); report_rc(&RC[1]);
    kx("   FPCCR back in main=", FPCCR); nl();
    CHECK(RC[0].fmism_mask == 0u && RC[1].fmism_mask == 0u && RC[0].mismatches == 0u && RC[1].mismatches == 0u);
    CHECK((FPCCR & 1u) == 0u);                              /* 올바른 스위처는 vstmdb 로 lazy 저장을 끝낸다 */
#endif
    nl(); sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" expected bug outcomes observed\n");
    return g_pass == g_total ? 0 : 1;
}
#endif
