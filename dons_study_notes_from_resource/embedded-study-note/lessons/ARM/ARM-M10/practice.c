/* ENV: qemu */
/* ARM-M10 practice.c — 추가 연습 1~5 검증 (QEMU mps2-an385).
 * 빌드: run_qemu.sh practice → practice.c + practice.S + shared_rmw.S + shared_startup.c
 */
#include <stdint.h>

uint32_t crit_enter(void);
void     crit_exit(uint32_t saved);
uint32_t basepri_enter(uint32_t level);
void     basepri_exit(uint32_t old);
uint32_t prio_bits_probe(volatile uint8_t *ipr);
uint32_t fault_enter(void);
void     fault_exit(uint32_t old);
void     svc_call(void);
extern const uint16_t svc_insn[];
/* shared_rmw.S */
void     rmw_inc_inject(volatile uint32_t *p, uint32_t ispr_bits);
uint32_t primask_get(void);
/* shared_startup.c */
void sh_puts(const char *s);
void sh_hex(uint32_t v);
void sh_dec(uint32_t v);
void check(int ok, const char *expr, int line);
extern unsigned g_pass, g_total;
#define CHECK(e) check((e) ? 1 : 0, #e, __LINE__)

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG8(a)  (*(volatile uint8_t  *)(uintptr_t)(a))
#define NVIC_ISER0 REG32(0xE000E100u)
#define NVIC_ISPR0 REG32(0xE000E200u)
#define NVIC_IPR(n) REG8(0xE000E400u + (n))
#define SCB_ICSR   REG32(0xE000ED04u)
#define SCB_SHPR2  REG32(0xE000ED1Cu)      /* [31:24] SVCall 우선순위 */
#define SCB_CFSR   REG32(0xE000ED28u)
#define SCB_HFSR   REG32(0xE000ED2Cu)
#define DSB_ISB()  __asm__ volatile("dsb\n\tisb" ::: "memory")

static void info(const char *k, uint32_t v) { sh_puts("INFO "); sh_puts(k); sh_puts(" = "); sh_hex(v); sh_puts("\n"); }

/* ---- 핸들러 ---- */
volatile uint32_t g_counter, g_low_hits, g_motor_hits, g_nmi_hits, g_svc_hits;
volatile uint32_t g_hf_hits, g_hf_hfsr, g_hf_cfsr, g_hf_pc, g_expect_fault;
volatile uint32_t g_tick_lo, g_tick_hi;                    /* 연습 5: 64비트 틱 (lo, hi) */
volatile uint32_t g_isr_basepri_old = 0xDEADu, g_isr_motor_inside;
void IRQ7_Handler(void) { g_motor_hits++; g_counter++; }
void IRQ6_Handler(void) {
    g_low_hits++;
    if (g_low_hits == 1) {                                 /* 연습 4: ISR 안에서의 BASEPRI 섹션 */
        uint32_t m0 = g_motor_hits;
        uint32_t old = basepri_enter(0x40u);               /* 이미 0xA0 핸들러 안 → 더 좁힌다 */
        g_isr_basepri_old = old;
        NVIC_ISPR0 = 1u << 7;                              /* 모터 0x20 < 0x40 → 바로 선점 */
        DSB_ISB();
        g_isr_motor_inside = g_motor_hits - m0;
        basepri_exit(old);
    } else {                                               /* 연습 5: 틱 ISR 역할 — 64비트 증가 */
        uint32_t lo = g_tick_lo + 1u;
        g_tick_lo = lo;
        if (lo == 0u) g_tick_hi = g_tick_hi + 1u;
    }
}
void NMI_Handler(void) { g_nmi_hits++; }
void SVC_Handler(void) { g_svc_hits++; }
void hardfault_c(uint32_t *frame) {
    g_hf_hfsr = SCB_HFSR; g_hf_cfsr = SCB_CFSR; g_hf_pc = frame[6];
    if (!g_expect_fault) { sh_puts("unexpected HardFault PC="); sh_hex(frame[6]); sh_puts("\n"); for (;;) { } }
    g_hf_hits++; g_expect_fault = 0;
    SCB_CFSR = g_hf_cfsr; SCB_HFSR = g_hf_hfsr;
}

/* ---- 연습 5: 64비트 틱 읽기. between() 이 두 읽기 사이에 ISR 을 한 번 끼워 넣는다 ---- */
static void pend_low(void) { NVIC_ISPR0 = 1u << 6; DSB_ISB(); }
static void nothing(void) { }
static uint64_t tick_read_naive(void (*between)(void)) {
    uint32_t lo = g_tick_lo;
    between();
    uint32_t hi = g_tick_hi;
    return ((uint64_t)hi << 32) | lo;
}
static uint64_t tick_read_crit(void (*between)(void)) {
    uint32_t k = crit_enter();
    uint32_t lo = g_tick_lo;
    between();                                             /* pend 는 되지만 ISR 은 crit_exit 뒤 */
    uint32_t hi = g_tick_hi;
    crit_exit(k);
    return ((uint64_t)hi << 32) | lo;
}
static uint64_t tick_read_hlh(void (*between)(void)) {    /* 락 없이: hi, lo, hi 가 같을 때까지 */
    uint32_t hi1, lo, hi2;
    do {
        hi1 = g_tick_hi;
        lo  = g_tick_lo;
        between();
        between = nothing;                                 /* 주입은 첫 시도에만 */
        hi2 = g_tick_hi;
    } while (hi1 != hi2);
    return ((uint64_t)hi2 << 32) | lo;
}

int main(void) {
    sh_puts("== ARM-M10 practice ==\n");
    NVIC_IPR(6) = 0xA0u; NVIC_IPR(7) = 0x20u;
    NVIC_ISER0 = (1u << 6) | (1u << 7);

    /* 연습 1: 구현된 우선순위 비트 수 탐침 */
    uint32_t bits = prio_bits_probe(&NVIC_IPR(9));
    info("implemented priority bits (QEMU)", bits);
    CHECK(bits == 8u);                                     /* QEMU 10.2 mps2-an385: 8 (BM-M02 와 같음) */
    CHECK(NVIC_IPR(9) == 0u);                              /* 탐침이 원래 값을 복원 */

    /* 연습 2: FAULTMASK — 우선순위 0 IRQ 도 막고, NMI 만 통과 */
    NVIC_IPR(7) = 0x00u;                                   /* 가장 급한 설정 가능 우선순위 */
    uint32_t f = fault_enter();
    NVIC_ISPR0 = 1u << 7;
    SCB_ICSR = 1u << 31;                                   /* NMIPENDSET */
    DSB_ISB();
    uint32_t motor_in = g_motor_hits, nmi_in = g_nmi_hits;
    fault_exit(f);
    DSB_ISB();
    CHECK(f == 0u && motor_in == 0u && nmi_in == 1u);
    CHECK(g_motor_hits == 1u);                             /* fault_exit 뒤에 실행 */
    NVIC_IPR(7) = 0x20u;

    /* 연습 3: PRIMASK 안에서 SVC → 받을 수 없음 → HardFault 로 승격 */
    uint32_t k = crit_enter();
    g_expect_fault = 1;
    svc_call();
    crit_exit(k);
    info("SVC in PRIMASK: HFSR", g_hf_hfsr);
    info("SVC in PRIMASK: CFSR", g_hf_cfsr);
    info("SVC in PRIMASK: stacked PC", g_hf_pc);
    info("svc_insn", (uint32_t)(uintptr_t)svc_insn);
    CHECK(g_hf_hits == 1u && g_svc_hits == 0u);
    CHECK(g_hf_hfsr == 0x40000000u && g_hf_cfsr == 0u);  /* FORCED 만, CFSR 원인 비트 없음 (QEMU 실측) */
    CHECK(g_hf_pc == (((uint32_t)(uintptr_t)svc_insn & ~1u) + 2u));   /* SVC 다음 명령 → 건너뛸 필요 없음 */
    svc_call();                                            /* PRIMASK=0: 정상 SVC */
    CHECK(g_svc_hits == 1u);

    /* 연습 4: IRQ6 안에서 basepri_enter → 모터는 선점, old = 0 */
    NVIC_ISPR0 = 1u << 6;
    DSB_ISB();
    CHECK(g_low_hits == 1u && g_isr_basepri_old == 0u && g_isr_motor_inside == 1u);

    /* 연습 5: 64비트 틱 — lo = 0xFFFFFFFF 에서 ISR 이 끼면 naive 는 찢어진 값 */
    g_tick_hi = 5u; g_tick_lo = 0xFFFFFFFFu;
    uint64_t v = tick_read_naive(pend_low);                /* lo=FFFFFFFF, hi=6 → 6:FFFFFFFF */
    CHECK(v == 0x6FFFFFFFFull);                            /* 실제 값 5:FFFFFFFF 도 6:00000000 도 아님 */
    g_tick_hi = 5u; g_tick_lo = 0xFFFFFFFFu;
    v = tick_read_crit(pend_low);
    CHECK(v == 0x5FFFFFFFFull && g_tick_hi == 6u && g_tick_lo == 0u);   /* 읽은 순간의 일관된 값 */
    g_tick_hi = 5u; g_tick_lo = 0xFFFFFFFFu;
    v = tick_read_hlh(pend_low);
    CHECK(v == 0x600000000ull);                            /* 재시도로 새 값 */

    sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}
