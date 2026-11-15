/* ENV: qemu */
/* ARM-M10 harness.c — starter.S / solution.S 공용 펌웨어 테스트 (Cortex-M3, QEMU mps2-an385).
 * 빌드: run_qemu.sh [starter|solution]  →  harness.c + <variant>.S + shared_rmw.S + shared_startup.c
 * 출력: PASS/FAIL 줄 + INFO 줄(실측값), "N/M passed", SYS_EXIT 코드 (0 = 전부 통과, 1 = 실패 있음).
 */
#include <stdint.h>

/* ---- starter.S / solution.S 가 구현하는 함수 (TODO) ---- */
uint32_t crit_enter(void);
void     crit_exit(uint32_t saved);
uint32_t basepri_get(void);
void     basepri_set(uint32_t v);
void     basepri_raise(uint32_t v);            /* MSR BASEPRI_MAX */
uint32_t basepri_enter(uint32_t level);
void     basepri_exit(uint32_t old);
uint32_t prio_encode(uint32_t logical, uint32_t bits);

/* ---- shared_rmw.S (주어짐) ---- */
void     rmw_inc(volatile uint32_t *p);
void     rmw_inc_inject(volatile uint32_t *p, uint32_t ispr_bits);
uint32_t primask_get(void);
void     crit_exit_buggy(uint32_t ignored);
void     do_udf(void);
extern const uint16_t udf_insn[];

/* ---- shared_startup.c ---- */
void sh_puts(const char *s);
void sh_hex(uint32_t v);
void sh_dec(uint32_t v);
void check(int ok, const char *expr, int line);
extern unsigned g_pass, g_total;
#define CHECK(e) check((e) ? 1 : 0, #e, __LINE__)

/* ---- 레지스터 (ARMv7-M ARM B3.2 / B3.3 / B3.4) ---- */
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG8(a)  (*(volatile uint8_t  *)(uintptr_t)(a))
#define SYST_CSR   REG32(0xE000E010u)      /* bit0 ENABLE, bit1 TICKINT, bit2 CLKSOURCE(1 = CPU) */
#define SYST_RVR   REG32(0xE000E014u)
#define SYST_CVR   REG32(0xE000E018u)
#define NVIC_ISER0 REG32(0xE000E100u)
#define NVIC_ICER0 REG32(0xE000E180u)
#define NVIC_ISPR0 REG32(0xE000E200u)
#define NVIC_ICPR0 REG32(0xE000E280u)
#define NVIC_IPR(n) REG8(0xE000E400u + (n))
#define SCB_ICSR   REG32(0xE000ED04u)      /* bit31 NMIPENDSET, bit25 PENDSTCLR */
#define SCB_SHPR3  REG32(0xE000ED20u)      /* [31:24] SysTick 우선순위 */
#define SCB_CFSR   REG32(0xE000ED28u)
#define SCB_HFSR   REG32(0xE000ED2Cu)
#define DSB_ISB()  __asm__ volatile("dsb\n\tisb" ::: "memory")

#define PRIO_BITS 3u                        /* 테스트는 3비트(AN385 실칩·STM32F1 급) 기준 값만 쓴다 */
#define PRIO(l)   prio_encode((l), PRIO_BITS)
#define IRQ_LOW   6u                        /* "통신" ISR  — 덜 급함 */
#define IRQ_MOTOR 7u                        /* "모터" ISR  — 급함 */

/* ---- ISR 들: 모두 같은 공유 카운터를 올린다 ---- */
volatile uint32_t g_counter;
volatile uint32_t g_low_hits, g_motor_hits, g_tick_hits, g_nmi_hits, g_nmi_primask, g_hf_hits;
volatile uint32_t g_expect_fault, g_hf_hfsr, g_hf_cfsr, g_hf_pc;
void IRQ6_Handler(void)    { g_counter++; g_low_hits++; }
void IRQ7_Handler(void)    { g_counter++; g_motor_hits++; }
void SysTick_Handler(void) { g_counter++; g_tick_hits++; }
void NMI_Handler(void)     { g_nmi_hits++; g_nmi_primask = primask_get(); }
void hardfault_c(uint32_t *frame) {
    g_hf_hfsr = SCB_HFSR;
    g_hf_cfsr = SCB_CFSR;
    g_hf_pc   = frame[6];                   /* stacked PC = 고장 난 명령 */
    if (!g_expect_fault) {
        sh_puts("unexpected HardFault, PC="); sh_hex(frame[6]); sh_puts("\n");
        for (;;) { }                        /* timeout 이 잡는다 */
    }
    g_hf_hits++;
    g_expect_fault = 0;
    frame[6] += 2;                          /* udf #0 = 16비트 → 다음 명령으로 (테스트 하네스 전용, ARM-M12) */
    SCB_CFSR = g_hf_cfsr;                   /* W1C */
    SCB_HFSR = g_hf_hfsr;
}

static void info(const char *k, uint32_t v) { sh_puts("INFO "); sh_puts(k); sh_puts(" = "); sh_dec(v); sh_puts("\n"); }

/* ---- 결정적 주입 실험: n 번 RMW, 매번 ldr 와 str 사이에서 IRQ pend ---- */
enum { M_NONE, M_PRIMASK, M_BASEPRI, M_NESTED_OK, M_NESTED_BUGGY };
static uint32_t inject_lost(int mode, uint32_t irq, uint32_t level, uint32_t n) {
    uint32_t c0 = g_counter, h0 = g_low_hits + g_motor_hits;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t k, k2;
        switch (mode) {
        case M_PRIMASK:
            k = crit_enter(); rmw_inc_inject(&g_counter, 1u << irq); crit_exit(k); break;
        case M_BASEPRI:
            k = basepri_enter(level); rmw_inc_inject(&g_counter, 1u << irq); basepri_exit(k); break;
        case M_NESTED_OK:                   /* enter, enter, (안쪽 exit), 바깥 섹션 안에서 RMW */
            k = crit_enter(); k2 = crit_enter(); crit_exit(k2);
            rmw_inc_inject(&g_counter, 1u << irq); crit_exit(k); break;
        case M_NESTED_BUGGY:
            k = crit_enter(); k2 = crit_enter(); crit_exit_buggy(k2);
            rmw_inc_inject(&g_counter, 1u << irq); crit_exit(k); break;
        default:
            rmw_inc_inject(&g_counter, 1u << irq); break;
        }
    }
    DSB_ISB();                              /* 대기 중이던 IRQ 가 있으면 여기서 다 처리됨 */
    uint32_t hits = g_low_hits + g_motor_hits - h0;
    return (n + hits) - (g_counter - c0);   /* 기대 증가량 - 실제 증가량 */
}

/* ---- SysTick 스트레스: main 은 RMW 를 iters 번, SysTick ISR 도 같은 카운터를 올린다 ---- */
#define STRESS_ITERS  20000u
#define STRESS_RELOAD 97u                   /* 98 클럭마다 SysTick (QEMU 시스템 클럭 25 MHz) */
static void other_work(uint32_t *rng) {     /* main 의 "다른 일": 0..7 개 nop — 틱 위상이 고정되지 않게 */
    uint32_t x = *rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;  /* xorshift32, 고정 시드 → 결정적 */
    *rng = x;
    for (uint32_t j = x & 7u; j; j--) __asm__ volatile("nop");
}
static uint32_t stress_lost(int mode, uint32_t level, uint32_t *ticks) {
    uint32_t rng = 2463534242u;
    g_counter = 0; g_tick_hits = 0;
    SYST_RVR = STRESS_RELOAD; SYST_CVR = 0; SYST_CSR = 7u;
    for (uint32_t i = 0; i < STRESS_ITERS; i++) {
        uint32_t k;
        if (mode == M_PRIMASK)      { k = crit_enter(); rmw_inc(&g_counter); crit_exit(k); }
        else if (mode == M_BASEPRI) { k = basepri_enter(level); rmw_inc(&g_counter); basepri_exit(k); }
        else                        { rmw_inc(&g_counter); }
        other_work(&rng);
    }
    SYST_CSR = 0u;
    DSB_ISB();
    *ticks = g_tick_hits;
    return (STRESS_ITERS + g_tick_hits) - g_counter;
}

int main(void) {
    sh_puts("== ARM-M10 PRIMASK / BASEPRI critical sections (QEMU mps2-an385) ==\n");
    NVIC_IPR(IRQ_LOW)   = (uint8_t)PRIO(5);  /* 0xA0 */
    NVIC_IPR(IRQ_MOTOR) = (uint8_t)PRIO(1);  /* 0x20 */
    SCB_SHPR3 = (SCB_SHPR3 & 0x00FFFFFFu) | ((uint32_t)PRIO(4) << 24);   /* SysTick 0x80 */
    NVIC_ISER0 = (1u << IRQ_LOW) | (1u << IRQ_MOTOR);

    /* ---------- Part A: PRIMASK 중첩 ---------- */
    uint32_t k1 = crit_enter();
    CHECK(k1 == 0 && primask_get() == 1);                 /* 켜진 상태에서 진입 → 0 반환, 꺼짐 */
    uint32_t k2 = crit_enter();
    CHECK(k2 == 1 && primask_get() == 1);                 /* 중첩 진입 → 1 반환 */
    crit_exit(k2);
    CHECK(primask_get() == 1);                            /* 안쪽 exit 후에도 아직 꺼져 있어야 */
    crit_exit(k1);
    CHECK(primask_get() == 0);                            /* 바깥 exit → 다시 켜짐 */
    __asm__ volatile("cpsid i" ::: "memory");             /* 이미 꺼진 상태(예: 다른 코드가 끔)에서 진입 */
    uint32_t k3 = crit_enter();
    crit_exit(k3);
    CHECK(k3 == 1 && primask_get() == 1);                 /* exit 이 남의 상태를 켜 버리면 안 된다 */
    __asm__ volatile("cpsie i" ::: "memory");

    /* ---------- Part A: 결정적 주입 — lost update 를 만들고 없앤다 ---------- */
    uint32_t lost;
    lost = inject_lost(M_NONE, IRQ_LOW, 0, 100);
    info("inject none    lost", lost);
    CHECK(lost == 100);                                   /* 매번 ISR 의 증가가 덮어써진다 */
    lost = inject_lost(M_PRIMASK, IRQ_LOW, 0, 100);
    info("inject PRIMASK lost", lost);
    CHECK(lost == 0 && g_low_hits == 200);                /* ISR 은 exit 직후에 실행 — 사라지지 않고 늦을 뿐 */

    /* ---------- Part A: BASEPRI 기본 ---------- */
    basepri_set(0x80u);  CHECK(basepri_get() == 0x80u);
    basepri_set(0u);     CHECK(basepri_get() == 0u);
    basepri_set(0x40u);
    basepri_raise(0x80u); CHECK(basepri_get() == 0x40u);  /* 0x80 은 덜 급함 → 안 바뀜 */
    basepri_raise(0x20u); CHECK(basepri_get() == 0x20u);  /* 더 급함 → 올라감 */
    basepri_raise(0u);    CHECK(basepri_get() == 0x20u);  /* 0 = "마스크 없음" 은 상승이 아님 → 무시 */
    basepri_set(0u);
    CHECK(prio_encode(5, 3) == 0xA0u && prio_encode(5, 8) == 0x05u);
    CHECK(prio_encode(7, 3) == 0xE0u && prio_encode(255, 8) == 0xFFu && prio_encode(3, 4) == 0x30u);
    CHECK(prio_encode(9, 3) == 0x20u);                    /* CMSIS 식 잘림: 9 → 1 (아주 급한 값!) */

    lost = inject_lost(M_BASEPRI, IRQ_LOW, PRIO(5), 100); /* BASEPRI 0xA0, ISR 0xA0 → 마스크됨 (≥) */
    info("inject BASEPRI=0xA0, ISR 0xA0 lost", lost);
    CHECK(lost == 0);
    lost = inject_lost(M_BASEPRI, IRQ_LOW, PRIO(6), 100); /* BASEPRI 0xC0, ISR 0xA0 → 더 급한 ISR 은 통과 */
    info("inject BASEPRI=0xC0, ISR 0xA0 lost", lost);
    CHECK(lost == 100);
    CHECK(basepri_get() == 0u);                           /* exit 이 0 으로 돌려놨다 */

    /* ---------- Part B: 모터 IRQ 는 BASEPRI 섹션 안에서도 돈다 ---------- */
    uint32_t lo0 = g_low_hits, mo0 = g_motor_hits;
    uint32_t old = basepri_enter(PRIO(2));                /* 0x40: 우선순위 값 ≥ 0x40 만 막힌다 */
    NVIC_ISPR0 = (1u << IRQ_LOW) | (1u << IRQ_MOTOR);
    DSB_ISB();
    uint32_t motor_in = g_motor_hits - mo0, low_in = g_low_hits - lo0, low_pending = (NVIC_ISPR0 >> IRQ_LOW) & 1u;
    basepri_exit(old);
    DSB_ISB();
    CHECK(motor_in == 1 && low_in == 0 && low_pending == 1);   /* 모터 0x20 < 0x40 → 실행, 통신 0xA0 → 대기 */
    CHECK(g_low_hits - lo0 == 1);                              /* exit 하자마자 통신 ISR 실행 */
    lost = inject_lost(M_BASEPRI, IRQ_MOTOR, PRIO(2), 50);     /* 모터 ISR 이 같은 카운터를 만지면? */
    info("inject BASEPRI=0x40, motor 0x20 lost", lost);
    CHECK(lost == 50);                                         /* BASEPRI 로는 못 막는다 → PRIMASK 영역 */

    /* ---------- Part B: 중첩 BASEPRI — 안쪽이 마스크를 "낮추면" 안 된다 ---------- */
    NVIC_IPR(IRQ_LOW) = (uint8_t)PRIO(4);                     /* 통신 0x80 로 */
    old = basepri_enter(PRIO(2));                             /* 바깥: 0x40 */
    uint32_t inner = basepri_enter(PRIO(6));                  /* 안쪽: 0xC0 요청 → BASEPRI_MAX 라 0x40 유지 */
    CHECK(inner == 0x40u && basepri_get() == 0x40u);
    lost = inject_lost(M_NONE, IRQ_LOW, 0, 10);               /* 0x80 ≥ 0x40 → 마스크 */
    CHECK(lost == 0);
    basepri_exit(inner);
    basepri_set(PRIO(6));                                     /* 틀린 안쪽: 그냥 쓰기 → 0xC0 으로 낮아짐 */
    lost = inject_lost(M_NONE, IRQ_LOW, 0, 10);               /* 0x80 < 0xC0 → 통과 → lost */
    info("nested naive basepri_set(0xC0) lost", lost);
    CHECK(lost == 10);
    basepri_exit(old);
    CHECK(basepri_get() == 0u);
    NVIC_IPR(IRQ_LOW) = (uint8_t)PRIO(5);

    /* ---------- Part B: PRIMASK 는 NMI 와 HardFault 를 막지 못한다 ---------- */
    k1 = crit_enter();
    SCB_ICSR = 1u << 31;                                      /* NMIPENDSET */
    DSB_ISB();
    g_expect_fault = 1;
    do_udf();                                                 /* UsageFault 꺼짐 → HardFault 로 승격 */
    crit_exit(k1);
    CHECK(g_nmi_hits == 1 && g_nmi_primask == 1);             /* PRIMASK=1 인 채로 NMI 실행 */
    CHECK(g_hf_hits == 1 && (g_hf_hfsr >> 30) == 1u);         /* HFSR.FORCED */
    CHECK((g_hf_cfsr >> 16) & 1u);                            /* CFSR.UNDEFINSTR */
    CHECK(g_hf_pc == ((uint32_t)(uintptr_t)udf_insn & ~1u));  /* stacked PC = udf 자리 */

    /* ---------- Part B: crit_exit 가 "무조건 cpsie" 면 중첩이 깨진다 ---------- */
    lost = inject_lost(M_NESTED_OK, IRQ_LOW, 0, 20);
    info("nested enter/enter/exit, correct exit lost", lost);
    CHECK(lost == 0);
    lost = inject_lost(M_NESTED_BUGGY, IRQ_LOW, 0, 20);
    info("nested enter/enter/exit, buggy cpsie exit lost", lost);
    CHECK(lost == 20);                                        /* 버그 판은 바깥 섹션을 열어 버린다 → 잡힘 */

    /* ---------- Part C: SysTick 스트레스 (main RMW vs SysTick ISR) ---------- */
    uint32_t t_none, t_pm, t_bp, t_bpw;
    uint32_t l_none = stress_lost(M_NONE, 0, &t_none);
    uint32_t l_pm   = stress_lost(M_PRIMASK, 0, &t_pm);
    uint32_t l_bp   = stress_lost(M_BASEPRI, PRIO(4), &t_bp);  /* SysTick 0x80 ≥ 0x80 → 마스크 */
    uint32_t l_bpw  = stress_lost(M_BASEPRI, PRIO(5), &t_bpw); /* 0x80 < 0xA0 → 통과 */
    info("stress none          ticks", t_none); info("stress none           lost", l_none);
    info("stress PRIMASK       ticks", t_pm);   info("stress PRIMASK        lost", l_pm);
    info("stress BASEPRI=0x80  ticks", t_bp);   info("stress BASEPRI=0x80   lost", l_bp);
    info("stress BASEPRI=0xA0  ticks", t_bpw);  info("stress BASEPRI=0xA0   lost", l_bpw);
    CHECK(t_none > 100 && t_pm > 100 && t_bp > 100 && t_bpw > 100);   /* ISR 이 충분히 끼어들었다 */
    CHECK(l_pm == 0);
    CHECK(l_bp == 0);
    CHECK(l_none > 0 && l_bpw > 0);                            /* icount 모드에서 결정적 (run_qemu.sh) */

    sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}
