/* ENV: qemu */
/* BM-M02 시스템 IRQ 우선순위 설계와 선점 검증 — 진짜 Cortex-M3 (qemu-system-arm -M mps2-an385)
 *
 * 한 파일 = startup (BM-B08 축약판) + NVIC 우선순위 드라이버 + 설계 표 + 트레이스 하네스.
 * 가짜 주변장치 IRQ 는 mps2-an385 에서 아무 장치도 연결되지 않은 줄(14..17)에 둔다 (QEMU hw/arm/mps2.c v10.2.1).
 * IRQ 는 소프트웨어로만 건다: NVIC_STIR / NVIC_ISPR (외부 IRQ), SCB_ICSR PENDSTSET / PENDSVSET (SysTick, PendSV).
 * 빌드/실행 (Lima VM elinux 안): bash run_qemu.sh solution|starter|gdb|tailchain
 *   arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror \
 *     -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns -T bm_m02.ld <this>.c -lgcc
 */
#include <stddef.h>
#include <stdint.h>

/* ---------------- 세미호스팅 (BM-B08 축약판) ---------------- */
static int semi(int op, const void *arg) {
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
static void sh_puts(const char *s) { semi(0x04, s); }                /* SYS_WRITE0 */
static void sh_hex(uint32_t v) {
    char b[11];
    b[0] = '0'; b[1] = 'x'; b[10] = 0;
    for (int i = 9; i >= 2; --i, v >>= 4) b[i] = "0123456789abcdef"[v & 0xFu];
    sh_puts(b);
}
static void sh_dec(uint32_t v) {
    char b[11];
    int i = 10;
    b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    sh_puts(&b[i]);
}
__attribute__((noreturn)) static void sh_exit(int code) {
    uint32_t blk[2] = { 0x20026u, (uint32_t)code };                  /* SYS_EXIT_EXTENDED → qemu exit = code */
    semi(0x20, blk);
    semi(0x18, (const void *)(uintptr_t)(code ? 0x20024u : 0x20026u));
    for (;;) { }
}

/* ---------------- 레지스터 (ARMv7-M ARM / Cortex-M3 Generic UG 기준 — 확인 권장) ---------------- */
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG8(a)  (*(volatile uint8_t *)(uintptr_t)(a))
#define SCB_ICTR   0xE000E004u   /* INTLINESNUM [3:0]: 외부 IRQ 줄 수 = 32·(N+1) */
#define NVIC_ISER0 0xE000E100u
#define NVIC_ICER0 0xE000E180u
#define NVIC_ISPR0 0xE000E200u
#define NVIC_ICPR0 0xE000E280u
#define NVIC_IABR0 0xE000E300u
#define NVIC_IPR0  0xE000E400u   /* IRQ 당 1 바이트, 바이트 접근 가능 */
#define NVIC_STIR  0xE000EF00u   /* IRQ 번호를 쓰면 pending */
#define SCB_ICSR   0xE000ED04u   /* PENDSVSET 28, PENDSTSET 26, ISRPENDING 22, VECTPENDING [20:12], RETTOBASE 11 */
#define SCB_AIRCR  0xE000ED0Cu   /* VECTKEY [31:16] = 0x05FA (읽으면 0xFA05), PRIGROUP [10:8] */
#define SCB_SHPR1  0xE000ED18u   /* SHPR1..3: 시스템 예외 4..15 의 우선순위 바이트 */
#define SCB_SHPR3  0xE000ED20u   /* [23:16] PendSV(14), [31:24] SysTick(15) */
#define ICSR_PENDSVSET  (1u << 28)
#define ICSR_PENDSTSET  (1u << 26)
#define ICSR_RETTOBASE  (1u << 11)
static inline void barrier(void) { __asm__ volatile("dsb 0xf\n\tisb 0xf" ::: "memory"); }

/* ---------------- 이 시스템의 인터럽트 소스 ---------------- */
enum { SRC_PWM, SRC_ADC, SRC_URX, SRC_UTX, SRC_TICK, SRC_PSV, N_SRC };
/* 트레이스 글자: 대문자 = 진입, 소문자 = 복귀, '.' = 스레드(main) 로 돌아옴 */
static const char k_letter[N_SRC] = { 'P', 'A', 'R', 'T', 'S', 'V' };
static const char *const k_name[N_SRC] = { "MOTOR_PWM", "ADC", "UART_RX", "UART_TX", "SysTick", "PendSV" };
/* CMSIS IRQn 관례: 외부 IRQ 는 0 이상, SysTick = -1, PendSV = -2.
 * 14..17 = mps2-an385 에서 아무 장치도 안 붙은 줄 (UART0-2 = 0..5, 타이머 8/9/10, SPI 11, UART 오버플로 12,
 * 이더넷 13, UART3/4 = 18..21, ADC-SPI 22, SPI 24 → 빈 줄: 6 7 14 15 16 17 23 25..31) */
static const int8_t k_irqn[N_SRC] = { 14, 15, 16, 17, -1, -2 };

/* ---------------- 설계: 우선순위 비트 수와 그룹 분할 ---------------- */
#define PRIO_BITS 4u          /* 설계 대상 = STM32F4 (__NVIC_PRIO_BITS 4). QEMU 는 8비트 구현 (Part A 탐침) */

/* TODO 1: PRIGROUP 선택 (AIRCR[10:8]). 그룹(선점) 비트 = 7 - PRIGROUP 개 (최대 PRIO_BITS) */
#define DESIGN_PRIGROUP 0u    /* TODO: 몇으로? (지금은 리셋값 0) */

typedef struct { uint8_t group; uint8_t sub; } prio_t;
/* TODO 2: 우선순위 표 (숫자가 작을수록 급함). 근거는 레슨 5. 풀이의 표. */
static const prio_t k_design[N_SRC] = {
    [SRC_PWM]  = { 0, 0 },    /* TODO */
    [SRC_ADC]  = { 0, 0 },    /* TODO */
    [SRC_URX]  = { 0, 0 },    /* TODO */
    [SRC_UTX]  = { 0, 0 },    /* TODO */
    [SRC_TICK] = { 0, 0 },    /* TODO */
    [SRC_PSV]  = { 0, 0 },    /* TODO */
};

/* ---------------- NVIC 우선순위 드라이버 (CMSIS core_cm3.h 와 같은 의미) ---------------- */
/* TODO 3: AIRCR 에 PRIGROUP 쓰기. VECTKEY 0x05FA 를 [31:16] 에 같이 써야 쓰기가 먹힌다. */
static void aircr_set_prigroup(uint32_t prigroup) {
    (void)prigroup;                                   /* TODO */
}
static uint32_t aircr_get_prigroup(void) { return (REG32(SCB_AIRCR) >> 8) & 7u; }

/* TODO 4: (group, sub) → PRIO_BITS 비트 값 (CMSIS NVIC_EncodePriority) */
static uint32_t prio_encode(uint32_t prigroup, uint32_t group, uint32_t sub) {
    (void)prigroup; (void)group; (void)sub;           /* TODO */
    return 0u;
}
/* (주어짐) 반대 방향 — CMSIS NVIC_DecodePriority */
static void prio_decode(uint32_t prio, uint32_t prigroup, uint32_t *group, uint32_t *sub) {
    uint32_t pg = prigroup & 7u;
    uint32_t gbits = (7u - pg) > PRIO_BITS ? PRIO_BITS : 7u - pg;
    uint32_t sbits = (pg + PRIO_BITS) < 7u ? 0u : pg + PRIO_BITS - 7u;
    *group = (prio >> sbits) & ((1u << gbits) - 1u);
    *sub = prio & ((1u << sbits) - 1u);
}

/* TODO 5: 우선순위 쓰기. irqn ≥ 0 → NVIC_IPR 바이트, irqn < 0 → SHPR 바이트 ((irqn & 0xF) - 4).
 * 값은 상위 PRIO_BITS 비트에 산다: 바이트 = prio << (8 - PRIO_BITS). */
static void nvic_set_prio(int irqn, uint32_t prio) {
    (void)irqn; (void)prio;                           /* TODO */
}
/* (주어짐) 바이트 그대로 읽기 — 구현 안 된 하위 비트는 0 으로 읽힌다 */
static uint8_t nvic_get_prio_byte(int irqn) {
    if (irqn >= 0) return REG8(NVIC_IPR0 + (uint32_t)irqn);
    return REG8(SCB_SHPR1 + (((uint32_t)irqn & 0xFu) - 4u));
}
static void nvic_enable(int irqn)  { REG32(NVIC_ISER0) = 1u << (uint32_t)irqn; barrier(); }
static void nvic_disable(int irqn) { REG32(NVIC_ICER0) = 1u << (uint32_t)irqn; barrier(); }

/* (주어짐) 설계 표를 하드웨어에 올린다 */
__attribute__((noinline)) void design_applied(void) { __asm__ volatile("" ::: "memory"); } /* gdb 중단점 */
static void apply_design(void) {
    aircr_set_prigroup(DESIGN_PRIGROUP);
    for (int s = 0; s < N_SRC; ++s)
        nvic_set_prio(k_irqn[s], prio_encode(DESIGN_PRIGROUP, k_design[s].group, k_design[s].sub));
    for (int s = 0; s < N_SRC; ++s)
        if (k_irqn[s] >= 0) { REG32(NVIC_ICPR0) = 1u << k_irqn[s]; nvic_enable(k_irqn[s]); }
    design_applied();
}

/* ---------------- 트레이스 버퍼 (RAM) ---------------- */
#define TRACE_CAP 96u
static char g_trace[TRACE_CAP + 1];
static volatile uint32_t g_tn;
static volatile uint32_t g_depth, g_maxdepth, g_seq;
static volatile uint32_t g_enter_cnt[N_SRC];
static volatile uint32_t g_pend_seq[N_SRC], g_max_wait[N_SRC];
static volatile uint32_t g_iabr_at_enter[N_SRC], g_icsr_at_enter[N_SRC];
static volatile uint32_t g_icsr_after_plan[N_SRC], g_ispr_after_plan[N_SRC];
static const char *volatile g_plan[N_SRC];           /* 진입 시 pending 시킬 소스 글자들 (1회용) */
static void (*volatile g_hook)(int s);               /* Part C 용 진입 훅 */

/* 트레이스 기록. 선점은 pend() 지점에서만 일어나므로 이 RMW 는 이 테스트에선 안 끊긴다 (일반론은 ARM-M10) */
static void trace_put(char c) {
    uint32_t i = g_tn;
    if (i < TRACE_CAP) g_trace[i] = c;
    g_tn = i + 1u;
}
static void trace_reset(void) {
    g_tn = 0; g_depth = 0; g_maxdepth = 0;
    for (uint32_t i = 0; i <= TRACE_CAP; ++i) g_trace[i] = 0;
    for (int s = 0; s < N_SRC; ++s) {
        g_enter_cnt[s] = 0; g_plan[s] = 0; g_max_wait[s] = 0;
        g_iabr_at_enter[s] = 0; g_icsr_at_enter[s] = 0; g_icsr_after_plan[s] = 0; g_ispr_after_plan[s] = 0;
    }
}
static int src_of(char c) {
    for (int s = 0; s < N_SRC; ++s) if (k_letter[s] == c) return s;
    return -1;
}
/* 소프트웨어 pending: 외부 IRQ = STIR, SysTick/PendSV = ICSR. 뒤의 DSB+ISB 로 "지금 바로" 인식시킨다 */
static void pend(int s) {
    g_pend_seq[s] = g_seq;
    if (k_irqn[s] >= 0) REG32(NVIC_STIR) = (uint32_t)k_irqn[s];
    else REG32(SCB_ICSR) = (k_irqn[s] == -1) ? ICSR_PENDSTSET : ICSR_PENDSVSET;
    barrier();
}

/* 모든 핸들러의 공통 몸통 */
static void isr_body(int s) {
    uint32_t wait = g_seq - g_pend_seq[s];             /* pend 이후 다른 핸들러가 몇 번 들어갔나 */
    if (wait > g_max_wait[s]) g_max_wait[s] = wait;
    g_seq = g_seq + 1u;
    trace_put(k_letter[s]);
    g_depth = g_depth + 1u;
    if (g_depth > g_maxdepth) g_maxdepth = g_depth;
    g_enter_cnt[s] = g_enter_cnt[s] + 1u;
    g_iabr_at_enter[s] = REG32(NVIC_IABR0);
    g_icsr_at_enter[s] = REG32(SCB_ICSR);
    const char *plan = g_plan[s];
    if (plan) {
        g_plan[s] = 0;                                  /* 1회용: 자기 자신을 pend 해도 무한 반복 없음 */
        for (; *plan; ++plan) { int t = src_of(*plan); if (t >= 0) pend(t); }
        g_icsr_after_plan[s] = REG32(SCB_ICSR);
        g_ispr_after_plan[s] = REG32(NVIC_ISPR0);
    }
    if (g_hook) g_hook(s);
    g_depth = g_depth - 1u;
    trace_put((char)(k_letter[s] + ('a' - 'A')));
}
void MotorPwm_IRQHandler(void) { isr_body(SRC_PWM); }
void Adc_IRQHandler(void)      { isr_body(SRC_ADC); }
void UartRx_IRQHandler(void)   { isr_body(SRC_URX); }
void UartTx_IRQHandler(void)   { isr_body(SRC_UTX); }
void SysTick_Handler(void)     { isr_body(SRC_TICK); }
void PendSV_Handler(void)      { isr_body(SRC_PSV); }

/* ---------------- startup (BM-B08 축약판) ---------------- */
extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
void Reset_Handler(void);
void Default_Handler(void) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    sh_puts("Default_Handler: unexpected exception IPSR="); sh_dec(ipsr); sh_puts("\n");
    sh_exit(2);
}
void HardFault_Handler(void) {
    sh_puts("HardFault CFSR="); sh_hex(REG32(0xE000ED28u));
    sh_puts(" HFSR="); sh_hex(REG32(0xE000ED2Cu)); sh_puts("\n");
    sh_exit(3);
}
typedef void (*vector_t)(void);
#define D Default_Handler
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + 32] = {
    (vector_t)_estack, Reset_Handler, D, HardFault_Handler, D, D, D, 0, 0, 0, 0,
    D, D, 0, PendSV_Handler, SysTick_Handler,
    D, D, D, D, D, D, D, D, D, D, D, D, D, D,                          /* IRQ0..13  */
    MotorPwm_IRQHandler, Adc_IRQHandler, UartRx_IRQHandler, UartTx_IRQHandler, /* IRQ14..17 */
    D, D, D, D, D, D, D, D, D, D, D, D, D, D,                          /* IRQ18..31 */
};
int main(void);
__attribute__((noreturn)) void Reset_Handler(void) {
    const uint32_t *src = _sidata;
    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;
    for (uint32_t *dst = _sbss; dst < _ebss;) *dst++ = 0u;
    sh_exit(main());
}

/* ---------------- 테스트 하네스 ---------------- */
static int pass, total;
#define CHECK(expr) do { total++; if (expr) { pass++; } else { \
    sh_puts("FAIL " __FILE__ ":"); sh_dec(__LINE__); sh_puts("  " #expr "\n"); } } while (0)
static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void pad(const char *s, uint32_t w) {
    uint32_t n = 0;
    while (s[n]) ++n;
    sh_puts(s);
    for (; n < w; ++n) sh_puts(" ");
}

/* 시나리오: start 를 스레드에서 pend → 각 핸들러는 진입 시 plan 의 소스들을 순서대로 pend */
typedef struct {
    const char *id, *what, *start;
    const char *plan[N_SRC];
    const char *want;
} scn_t;
static const char *run_scn(const scn_t *sc) {
    trace_reset();
    for (int s = 0; s < N_SRC; ++s) g_plan[s] = sc->plan[s];
    for (const char *p = sc->start; *p; ++p) { int t = src_of(*p); if (t >= 0) pend(t); }
    trace_put('.');
    return g_trace;
}
static int check_scn(const scn_t *sc) {
    const char *got = run_scn(sc);
    int ok = str_eq(got, sc->want);
    pad(sc->id, 4); pad(sc->what, 40);
    sh_puts(" got "); pad(got, 13); sh_puts(" want "); pad(sc->want, 13);
    sh_puts(ok ? " PASS\n" : " FAIL\n");
    return ok;
}

/* ---------------- Part A: QEMU 가 무엇을 구현했나 + 설계 적용 ---------------- */
static void part_a(void) {
    sh_puts("== Part A: probe + design ==\n");
    uint32_t ictr = REG32(SCB_ICTR) & 0xFu;
    REG32(NVIC_ISER0) = 0xFFFFFFFFu; REG32(NVIC_ISER0 + 4u) = 0xFFFFFFFFu;
    uint32_t en0 = REG32(NVIC_ISER0), en1 = REG32(NVIC_ISER0 + 4u);
    REG32(NVIC_ICER0) = 0xFFFFFFFFu; REG32(NVIC_ICER0 + 4u) = 0xFFFFFFFFu; barrier();
    REG8(NVIC_IPR0 + 25u) = 0xFFu;                        /* 빈 줄 25 로 탐침 */
    uint8_t ipr_ff = REG8(NVIC_IPR0 + 25u);
    REG8(NVIC_IPR0 + 25u) = 0u;
    uint32_t prio_bits = 0;
    for (uint32_t b = ipr_ff; b & 0x80u; b = (b << 1) & 0xFFu) ++prio_bits;
    sh_puts("probe: ICTR.INTLINESNUM="); sh_dec(ictr);
    sh_puts(" ISER0 after 0xFFFFFFFF="); sh_hex(en0);
    sh_puts(" ISER1="); sh_hex(en1);
    sh_puts("\nprobe: IPR[25] write 0xFF reads "); sh_hex(ipr_ff);
    sh_puts(" -> implemented priority bits = "); sh_dec(prio_bits); sh_puts("\n");
    CHECK(ictr == 0u && en0 == 0xFFFFFFFFu && en1 == 0u);   /* 외부 IRQ 32 개 (mps2.c num-irq 32) */
    CHECK(prio_bits >= PRIO_BITS);                           /* 설계 4비트가 이 코어에서 표현 가능 */

    /* encode/decode: 모든 PRIGROUP × 표현 가능한 (group, sub) 왕복 */
    uint32_t bad = 0, cases = 0;
    for (uint32_t pg = 0; pg < 8u; ++pg)
        for (uint32_t p = 0; p < (1u << PRIO_BITS); ++p) {
            uint32_t g, s;
            prio_decode(p, pg, &g, &s);
            ++cases;
            if (prio_encode(pg, g, s) != p) ++bad;
        }
    sh_puts("encode/decode round trip: "); sh_dec(cases - bad); sh_puts("/"); sh_dec(cases); sh_puts("\n");
    CHECK(bad == 0u);
    CHECK(prio_encode(4u, 2u, 1u) == 5u && prio_encode(3u, 15u, 0u) == 15u && prio_encode(7u, 0u, 9u) == 9u);

    apply_design();
    sh_puts("AIRCR="); sh_hex(REG32(SCB_AIRCR));
    sh_puts(" SHPR3="); sh_hex(REG32(SCB_SHPR3));
    sh_puts(" IPR[12..15]="); sh_hex(REG32(NVIC_IPR0 + 12u));
    sh_puts(" IPR[16..19]="); sh_hex(REG32(NVIC_IPR0 + 16u));
    sh_puts(" ISER0="); sh_hex(REG32(NVIC_ISER0)); sh_puts("\n");
    sh_puts("source     IRQn  byte  group sub\n");
    for (int s = 0; s < N_SRC; ++s) {
        uint32_t g, sb, byte = nvic_get_prio_byte(k_irqn[s]);
        prio_decode(byte >> (8u - PRIO_BITS), aircr_get_prigroup(), &g, &sb);
        pad(k_name[s], 10);
        if (k_irqn[s] < 0) { sh_puts(" -"); sh_dec((uint32_t)-k_irqn[s]); sh_puts("   "); }
        else { sh_puts(" "); sh_dec((uint32_t)k_irqn[s]); sh_puts("   "); }
        sh_hex(byte); sh_puts("  "); sh_dec(g); sh_puts("     "); sh_dec(sb); sh_puts("\n");
    }
    CHECK((REG32(SCB_AIRCR) >> 16) == 0xFA05u && aircr_get_prigroup() == 4u);
    CHECK(REG32(NVIC_IPR0 + 12u) == 0x20000000u);              /* 워드 IPR3 = IRQ12..15: 바이트3 = IRQ15 = 0x20 */
    CHECK(nvic_get_prio_byte(14) == 0x00u && nvic_get_prio_byte(15) == 0x20u);
    CHECK(nvic_get_prio_byte(16) == 0x40u && nvic_get_prio_byte(17) == 0x40u);
    CHECK(REG32(SCB_SHPR3) == 0x50F00000u);                    /* SysTick 0x50 [31:24], PendSV 0xF0 [23:16] */
}

/* ---------------- 시나리오 표 (기대 트레이스 = 설계에서 손으로 유도) ---------------- */
static const scn_t k_scn[] = {
    { "S1", "higher group preempts (nesting)", "R", { [SRC_URX] = "A", [SRC_ADC] = "P" }, "RAPpar." },
    { "S2", "lower group waits", "P", { [SRC_PWM] = "A" }, "PpAa." },
    { "S3", "same group, better sub: no preempt", "S", { [SRC_TICK] = "R" }, "SsRr." },
    { "S3b", "same group, worse sub: no preempt", "R", { [SRC_URX] = "S" }, "RrSs." },
    { "S4", "4 pending: sub, then IRQ number", "P", { [SRC_PWM] = "VSTR" }, "PpRrTtSsVv." },
    { "S5", "PendSV last, tail-chained", "R", { [SRC_URX] = "VA" }, "RAarVv." },
    { "S6", "self-pend: no re-entry, runs again", "T", { [SRC_UTX] = "T" }, "TtTt." },
};

static void part_b(void) {
    sh_puts("== Part B: preemption traces (designed table) ==\n");
    for (uint32_t i = 0; i < sizeof k_scn / sizeof k_scn[0]; ++i) {
        CHECK(check_scn(&k_scn[i]));
        if (i == 0) {   /* S1: PWM 안에서 IABR = RX·ADC·PWM 셋 다 active */
            sh_puts("    S1 IABR0 at PWM entry="); sh_hex(g_iabr_at_enter[SRC_PWM]);
            sh_puts(" max depth="); sh_dec(g_maxdepth); sh_puts("\n");
            CHECK(g_iabr_at_enter[SRC_PWM] == ((1u << 14) | (1u << 15) | (1u << 16)) && g_maxdepth == 3u);
        }
        if (i == 4) {   /* S4: PWM 안에서 4개 pend 직후 NVIC 가 고른 다음 예외 = VECTPENDING */
            uint32_t icsr = g_icsr_after_plan[SRC_PWM];
            sh_puts("    S4 after pends: ICSR="); sh_hex(icsr);
            sh_puts(" VECTPENDING="); sh_dec((icsr >> 12) & 0x1FFu);
            sh_puts(" ISPR0="); sh_hex(g_ispr_after_plan[SRC_PWM]); sh_puts("\n");
            CHECK(((icsr >> 12) & 0x1FFu) == 32u);                    /* 예외 번호 32 = IRQ16 UART_RX */
            CHECK((icsr & (ICSR_PENDSVSET | ICSR_PENDSTSET)) == (ICSR_PENDSVSET | ICSR_PENDSTSET));
            CHECK(g_ispr_after_plan[SRC_PWM] == ((1u << 16) | (1u << 17)));
        }
        if (i == 5) {   /* S5: PendSV 진입 시 다른 active 없음 (RETTOBASE=1) */
            sh_puts("    S5 ICSR at PendSV entry="); sh_hex(g_icsr_at_enter[SRC_PSV]);
            sh_puts(" (VECTACTIVE="); sh_dec(g_icsr_at_enter[SRC_PSV] & 0x1FFu); sh_puts(")\n");
            CHECK((g_icsr_at_enter[SRC_PSV] & ICSR_RETTOBASE) && (g_icsr_at_enter[SRC_PSV] & 0x1FFu) == 14u);
        }
    }

    sh_puts("== Part B: injected faults ==\n");
    /* F1: VECTKEY 없이 AIRCR 쓰기 → 무시된다 */
    REG32(SCB_AIRCR) = (0u << 8);
    barrier();
    sh_puts("F1  AIRCR write without VECTKEY -> PRIGROUP still "); sh_dec(aircr_get_prigroup()); sh_puts("\n");
    CHECK(aircr_get_prigroup() == DESIGN_PRIGROUP);
    /* F1b: AIRCR |= 키|5<<8 → 읽은 0xFA05 와 OR 되어 키 필드 = 0xFFFF → 역시 무시 */
    REG32(SCB_AIRCR) = REG32(SCB_AIRCR) | (0x05FAu << 16) | (5u << 8);
    barrier();
    sh_puts("F1b AIRCR |= (0x05FA<<16)|(5<<8) -> PRIGROUP still "); sh_dec(aircr_get_prigroup()); sh_puts("\n");
    CHECK(aircr_get_prigroup() == DESIGN_PRIGROUP);

    /* F2: PRIGROUP = 0 (그룹 7비트) 로 잘못 설정 → 0x40 vs 0x50 이 다른 그룹이 되어 서브가 선점으로 변한다 */
    static const scn_t f2 = { "F2", "PRIGROUP=0: sub becomes preemption", "S", { [SRC_TICK] = "R" }, "SRrs." };
    aircr_set_prigroup(0u);
    CHECK(check_scn(&f2));
    aircr_set_prigroup(DESIGN_PRIGROUP);

    /* F3: 시프트를 잊은 우선순위: UART_RX 에 encode 값 4 를 바이트에 그대로 → 0x04 = 그룹 0 (PWM 급!) */
    static const scn_t f3 = { "F3", "RX prio byte 0x04 (no <<4): preempts ADC", "A", { [SRC_ADC] = "R" }, "ARra." };
    static const scn_t f3ok = { "F3'", "RX prio byte 0x40 (correct): waits", "A", { [SRC_ADC] = "R" }, "AaRr." };
    CHECK(check_scn(&f3ok));
    REG8(NVIC_IPR0 + 16u) = (uint8_t)prio_encode(DESIGN_PRIGROUP, k_design[SRC_URX].group, k_design[SRC_URX].sub);
    sh_puts("    F3 IPR[16] reads "); sh_hex(nvic_get_prio_byte(16));
    sh_puts(" (QEMU keeps low bits; a 4-bit part would read 0x00 — both = group 0)\n");
    CHECK(check_scn(&f3));
    nvic_set_prio(16, prio_encode(DESIGN_PRIGROUP, k_design[SRC_URX].group, k_design[SRC_URX].sub));

    /* F4: 꺼진(disabled) IRQ 를 pend → 안 돈다, ISPR 에 남아 있다가 enable 순간 실행 */
    trace_reset();
    nvic_disable(16);
    REG32(NVIC_ISPR0) = 1u << 16; barrier();
    trace_put('.');
    uint32_t ispr = REG32(NVIC_ISPR0);
    nvic_enable(16);
    trace_put('.');
    sh_puts("F4  pend while disabled: ISPR0="); sh_hex(ispr);
    sh_puts(" trace "); sh_puts(g_trace); sh_puts(" (want .Rr.)\n");
    CHECK((ispr & (1u << 16)) && str_eq(g_trace, ".Rr."));
}

/* ---------------- Part C: 20 kHz 모터 제어 루프 버스트 (결정적 이벤트 스크립트) ---------------- */
#define C_PERIODS 400u
static volatile uint32_t c_period, c_nested_done, c_rx_done, c_rx_bytes, c_lines, c_tx_bytes;
static volatile uint32_t c_psv_not_base, c_nested_at[8], c_nested_n;
static void burst_hook(int s) {
    uint32_t k = c_period;
    switch (s) {
    case SRC_PWM:                                   /* 듀티 갱신 → ADC 변환 시작 (EOC 는 PWM 뒤) */
        pend(SRC_ADC);
        if (k % 20u == 18u) pend(SRC_TICK);         /* 1 ms 틱이 같은 순간에 */
        break;
    case SRC_ADC:                                   /* 전류 루프 계산 중 ... */
        if (k % 3u == 0u && !c_rx_done) { c_rx_done = 1; pend(SRC_URX); }  /* 바이트 도착 */
        if (k % 8u == 5u && !c_nested_done) {       /* ADC ISR 이 길어져 다음 PWM 에지가 온다 */
            c_nested_done = 1;
            if (c_nested_n < 8u) c_nested_at[c_nested_n] = k;
            c_nested_n = c_nested_n + 1u;
            pend(SRC_PWM);
        }
        break;
    case SRC_URX:                                   /* 8 바이트째 = '\n' → 명령 해석은 PendSV 로 미룬다 */
        c_rx_bytes = c_rx_bytes + 1u;
        if (c_rx_bytes % 8u == 0u) pend(SRC_PSV);
        break;
    case SRC_UTX:
        c_tx_bytes = c_tx_bytes + 1u;
        break;
    case SRC_PSV:                                   /* 명령 해석 (+ 응답 1 바이트 송신) */
        if (!(REG32(SCB_ICSR) & ICSR_RETTOBASE) || g_depth != 1u) c_psv_not_base = c_psv_not_base + 1u;
        c_lines = c_lines + 1u;
        pend(SRC_UTX);
        break;
    default: break;
    }
}
static void part_c(void) {
    static const struct { uint32_t k; const char *want; } k_logs[] = {
        { 0, "PpAaRr." }, { 1, "PpAa." }, { 5, "PpAPpaAa." }, { 18, "PpAaRrSs." }, { 21, "PpAPpaAaRrVTtv." },
    };
    sh_puts("== Part C: 20 kHz control-loop burst, "); sh_dec(C_PERIODS); sh_puts(" periods ==\n");
    uint32_t total_cnt[N_SRC] = { 0 }, max_wait[N_SRC] = { 0 }, maxdepth = 0, log_ok = 0;
    g_hook = burst_hook;
    for (uint32_t k = 0; k < C_PERIODS; ++k) {
        trace_reset();
        c_period = k; c_nested_done = 0; c_rx_done = 0;
        pend(SRC_PWM);                              /* 타이머 업데이트 이벤트 = 주기 시작 */
        trace_put('.');
        for (int s = 0; s < N_SRC; ++s) {
            total_cnt[s] += g_enter_cnt[s];
            if (g_max_wait[s] > max_wait[s]) max_wait[s] = g_max_wait[s];
        }
        if (g_maxdepth > maxdepth) maxdepth = g_maxdepth;
        for (uint32_t j = 0; j < sizeof k_logs / sizeof k_logs[0]; ++j)
            if (k_logs[j].k == k) {
                int ok = str_eq(g_trace, k_logs[j].want);
                log_ok += (uint32_t)ok;
                sh_puts(k < 10u ? "  k=" : " k="); sh_dec(k); sh_puts("  "); pad(g_trace, 16);
                if (ok) sh_puts("  (as designed)\n");
                else { sh_puts("  FAIL want "); sh_puts(k_logs[j].want); sh_puts("\n"); }
            }
    }
    g_hook = 0;
    sh_puts("  counts:");
    for (int s = 0; s < N_SRC; ++s) { sh_puts(" "); sh_puts(k_name[s]); sh_puts("="); sh_dec(total_cnt[s]); }
    sh_puts("\n  max wait (handler entries between pend and entry):");
    for (int s = 0; s < N_SRC; ++s) { sh_puts(" "); sh_puts(k_name[s]); sh_puts("="); sh_dec(max_wait[s]); }
    sh_puts("\n  max nesting depth="); sh_dec(maxdepth);
    sh_puts("  PWM edge during ADC at k="); for (uint32_t i = 0; i < c_nested_n && i < 3u; ++i) { sh_dec(c_nested_at[i]); sh_puts(","); }
    sh_puts("...  PendSV not at base="); sh_dec(c_psv_not_base); sh_puts("\n");

    uint32_t nest = 0, rx = 0, tick = 0;
    for (uint32_t k = 0; k < C_PERIODS; ++k) { nest += (k % 8u == 5u); rx += (k % 3u == 0u); tick += (k % 20u == 18u); }
    CHECK(log_ok == sizeof k_logs / sizeof k_logs[0]);
    CHECK(total_cnt[SRC_PWM] == C_PERIODS + nest && total_cnt[SRC_ADC] == total_cnt[SRC_PWM]);
    CHECK(total_cnt[SRC_URX] == rx && total_cnt[SRC_TICK] == tick);
    CHECK(total_cnt[SRC_PSV] == rx / 8u && total_cnt[SRC_UTX] == rx / 8u);
    CHECK(max_wait[SRC_PWM] == 0u);                 /* 모터 PWM 은 한 번도 줄 서지 않았다 */
    CHECK(max_wait[SRC_UTX] == 0u);                 /* PendSV 안에서 pend → 즉시 선점 */
    CHECK(maxdepth == 2u && c_psv_not_base == 0u);
}

int main(void) {
#ifdef ONLY_S5   /* run_qemu.sh tailchain: S5 하나만 돌려 QEMU -d int 로그를 짧게 */
    apply_design();
    return str_eq(run_scn(&k_scn[5]), k_scn[5].want) ? 0 : 1;
#endif
    sh_puts("BM-M02 priority design on qemu mps2-an385 (Cortex-M3)\n");
    part_a();
    part_b();
    part_c();
    sh_dec((uint32_t)pass); sh_puts("/"); sh_dec((uint32_t)total); sh_puts(" passed\n");
    return pass == total ? 0 : 1;
}
