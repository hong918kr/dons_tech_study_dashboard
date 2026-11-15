/* ENV: qemu */
/* ARM-M08 harness.c — starter.S / solution.S 공용 펌웨어 테스트 (Cortex-M3, QEMU mps2-an385).
 * 빌드: run_qemu.sh [starter|solution]  →  harness.c + <variant>.S + shared_startup.c
 * 출력: PASS/FAIL 줄, "N/M passed", SYS_EXIT 코드 (0 = 전부 통과, 1 = 실패 있음).
 * starter 가 망가진 테이블로 예외를 일으키지 않도록, 예외를 쓰는 검사는 먼저 "엔트리가 멀쩡한가"를 본다.
 */
#include <stdint.h>

/* ---- <variant>.S ---- */
extern const uint32_t g_vectors[48];
void Reset_Handler(void);
void vtor_relocate(uint32_t *dst, const uint32_t *src, uint32_t nwords);
void vector_patch(uint32_t *table, uint32_t exc_num, uint32_t handler);
uint32_t peek32(uint32_t addr);
void call_blx(uint32_t target);
extern const char call_blx_ret[];
void bx_target(void);

/* ---- shared_startup.c ---- */
void sh_puts(const char *s);
void sh_dec(uint32_t v);
void sh_kv(const char *k, uint32_t v);
__attribute__((noreturn)) void sh_exit(int code);
void check(int ok, const char *expr, int line);
extern unsigned g_pass, g_total;
extern uint32_t _estack[];
#define CHECK(e) check((e) ? 1 : 0, #e, __LINE__)

#define REG32(a)    (*(volatile uint32_t *)(uintptr_t)(a))
#define SCB_ICSR    REG32(0xE000ED04u)
#define SCB_VTOR    REG32(0xE000ED08u)
#define SCB_SHPR2   REG32(0xE000ED1Cu)      /* [31:24] SVCall 우선순위 */
#define SCB_SHPR3   REG32(0xE000ED20u)      /* [31:24] SysTick, [23:16] PendSV */
#define SCB_SHCSR   REG32(0xE000ED24u)      /* bit18 USGFAULTENA */
#define SCB_CFSR    REG32(0xE000ED28u)      /* UFSR = [31:16], INVSTATE = bit17 */
#define SCB_HFSR    REG32(0xE000ED2Cu)      /* bit30 FORCED */
#define SYST_CSR    REG32(0xE000E010u)
#define SYST_RVR    REG32(0xE000E014u)
#define SYST_CVR    REG32(0xE000E018u)
#define NVIC_ISER0  REG32(0xE000E100u)
#define NVIC_ISPR0  REG32(0xE000E200u)
#define USGFAULTENA (1u << 18)
#define CFSR_INVSTATE (1u << 17)
#define HFSR_FORCED (1u << 30)
#define XPSR_T      (1u << 24)
#define NVEC        48u
#define EXC_SVC     11u
#define EXC_SYSTICK 15u
#define EXC_IRQ6    22u

#define FN(f)  ((uint32_t)(uintptr_t)(f))      /* 함수 포인터 → 벡터 워드 (Thumb 이면 홀수) */

/* ---- 핸들러 (C, 강한 정의 → .S 의 weak 별칭을 덮는다). 4 바이트 정렬: Part B 의 짝수 주소가 4n 이 되게 ---- */
volatile uint32_t g_svc_n, g_tick1, g_tick2, g_irq6_n, g_bx_ran;
#define HANDLER __attribute__((aligned(4)))
HANDLER void SVC_Handler(void)      { g_svc_n++; }
HANDLER void SysTick_Handler(void)  { g_tick1++; }
HANDLER void SysTick_Handler2(void) { g_tick2++; }      /* Part C 에서 런타임에 꽂는다 */
HANDLER void Irq6_Handler(void)     { g_irq6_n++; }

/* ---- .data / .bss 초기화 확인용 ---- */
volatile uint32_t g_data_magic = 0xC0FFEE08u;           /* .data: Reset_Handler 가 복사해야 보인다 */
volatile uint32_t g_bss_zero[8];                         /* .bss */

/* ---- RAM 벡터 테이블: 48 워드 = 192 B → 2 의 거듭제곱 올림 256 B 정렬 ---- */
uint32_t g_ram_vectors[NVEC] __attribute__((aligned(256)));

/* ---- fault 기록 (HardFault_Handler/UsageFault_Handler 트램펄린이 b 로 넘어온다) ---- */
struct fault_obs { uint32_t ipsr, cfsr, hfsr, pc, lr, xpsr, exc_return, n; };
volatile struct fault_obs g_f;
volatile uint32_t g_expect_fault;

void fault_record(uint32_t *frame, uint32_t exc_return) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    g_f.ipsr = ipsr;
    g_f.cfsr = SCB_CFSR;
    g_f.hfsr = SCB_HFSR;
    g_f.lr = frame[5];                      /* 쌓인 프레임: r0 r1 r2 r3 r12 lr pc xpsr */
    g_f.pc = frame[6];
    g_f.xpsr = frame[7];
    g_f.exc_return = exc_return;
    g_f.n++;
    if (!g_expect_fault) {                  /* 예상 못 한 fault → 보고하고 종료 */
        sh_puts("unexpected fault\n");
        sh_kv("ipsr", ipsr); sh_kv("cfsr", g_f.cfsr); sh_kv("hfsr", g_f.hfsr);
        sh_kv("stacked pc", g_f.pc); sh_kv("stacked xpsr", g_f.xpsr);
        sh_puts("unexpected fault -> exit 3\n");
        sh_exit(3);
    }
    g_expect_fault = 0;
    frame[7] |= XPSR_T;                     /* 복구: 쌓인 xPSR.T = 1 → 복귀 후 같은 pc 를 Thumb 로 실행 */
    SCB_CFSR = g_f.cfsr;                    /* W1C: 읽은 비트를 써서 지운다 */
    SCB_HFSR = g_f.hfsr;
}

static int entry_ok(uint32_t v) { return v != 0u && (v & 1u) == 1u; }
static const uint32_t *active_table(void) { return (const uint32_t *)(uintptr_t)SCB_VTOR; }
static int active_ok(uint32_t exc) { return entry_ok(active_table()[exc]); }

static void svc0(void) { __asm__ volatile("svc #0" ::: "memory"); }
static uint32_t run_systick(volatile uint32_t *counter, uint32_t want) {
    uint32_t spins = 0;
    SYST_RVR = 2000u - 1u;                                   /* 2000 사이클마다 */
    SYST_CVR = 0u;
    SYST_CSR = 7u;                                           /* CLKSOURCE=cpu | TICKINT | ENABLE */
    while (*counter < want && ++spins < 20000000u) { }
    SYST_CSR = 0u;
    return spins;
}
static void dump_fault(void) {
    sh_kv("ipsr", g_f.ipsr); sh_kv("cfsr", g_f.cfsr); sh_kv("hfsr", g_f.hfsr);
    sh_kv("stacked pc", g_f.pc); sh_kv("stacked lr", g_f.lr);
    sh_kv("stacked xpsr", g_f.xpsr); sh_kv("exc_return", g_f.exc_return);
}
static void clear_obs(void) { g_f.ipsr = g_f.cfsr = g_f.hfsr = g_f.pc = g_f.lr = g_f.xpsr = g_f.exc_return = 0; }

int main(void) {
    sh_puts("ARM-M08 vector table in asm + Thumb bit (QEMU mps2-an385)\n");

    /* ===== Part A: 부팅한 테이블 읽기 ===== */
    sh_puts("[A] boot table\n");
    CHECK(SCB_VTOR == 0u);                                   /* 리셋 VTOR = 0 (Cortex-M3) */
    CHECK(peek32(0u) == FN(_estack));                        /* [0] = 초기 MSP */
    CHECK(peek32(4u) == FN(Reset_Handler) && (peek32(4u) & 1u));
    unsigned bad = 0, first_bad = 0;
    for (uint32_t i = 1; i < NVEC; ++i) {
        if ((i >= 7 && i <= 10) || i == 13) continue;        /* reserved */
        if (!entry_ok(g_vectors[i]) && bad++ == 0u) first_bad = i;
    }
    if (bad) { sh_puts("  bad entries: "); sh_dec(bad); sh_puts(", first = ["); sh_dec(first_bad); sh_puts("]\n"); }
    CHECK(bad == 0u);                                        /* 모든 핸들러 엔트리 ≠ 0, 홀수 */
    CHECK(g_vectors[EXC_SVC] == FN(SVC_Handler));
    CHECK(g_vectors[EXC_SYSTICK] == FN(SysTick_Handler));
    CHECK(g_vectors[EXC_IRQ6] == FN(Irq6_Handler));
    CHECK(g_vectors[2] == g_vectors[16] && g_vectors[16] != 0u);   /* NMI 와 IRQ0 은 둘 다 Default_Handler */
    sh_kv("vec[11] SVC_Handler", g_vectors[EXC_SVC]);
    sh_kv("vec[15] SysTick_Handler", g_vectors[EXC_SYSTICK]);
    CHECK(g_data_magic == 0xC0FFEE08u);                      /* Reset_Handler 의 .data 복사 */
    unsigned nz = 0;
    for (unsigned i = 0; i < 8; ++i) nz += (g_bss_zero[i] != 0u);
    CHECK(nz == 0u);                                         /* .bss (QEMU RAM 은 원래 0 → 약한 검사) */

    if (active_ok(EXC_SVC)) svc0();                          /* 테이블로 SVC */
    CHECK(g_svc_n == 1u);
    if (active_ok(EXC_SYSTICK)) run_systick(&g_tick1, 3u);   /* 테이블로 SysTick */
    CHECK(g_tick1 >= 3u);
    if (active_ok(EXC_IRQ6)) {                               /* 테이블로 IRQ6 (소프트웨어 pend) */
        NVIC_ISER0 = 1u << 6;
        NVIC_ISPR0 = 1u << 6;
        __asm__ volatile("dsb\n isb" ::: "memory");
    }
    CHECK(g_irq6_n == 1u);

    /* ===== Part C (먼저): RAM 으로 재배치 + 런타임 패치 ===== */
    sh_puts("[C] relocate to RAM\n");
    uint32_t base = FN(g_ram_vectors);
    sh_kv("g_ram_vectors", base);
    vtor_relocate(g_ram_vectors, g_vectors, NVEC);
    CHECK(SCB_VTOR == base);
    unsigned diff = 0;
    for (uint32_t i = 0; i < NVEC; ++i) diff += (g_ram_vectors[i] != g_vectors[i]);
    CHECK(diff == 0u);
    vector_patch(g_ram_vectors, EXC_SYSTICK, FN(SysTick_Handler2));
    CHECK(g_ram_vectors[EXC_SYSTICK] == FN(SysTick_Handler2));
    CHECK(g_vectors[EXC_SYSTICK] == FN(SysTick_Handler));   /* FLASH 쪽 원본은 그대로 */
    uint32_t t1_before = g_tick1;
    int relocated = (SCB_VTOR == base) && active_ok(EXC_SYSTICK) && active_table()[EXC_SYSTICK] == FN(SysTick_Handler2);
    if (relocated) run_systick(&g_tick2, 3u);
    CHECK(g_tick2 >= 3u && g_tick1 == t1_before);            /* 새 핸들러만 불렸다 */
    sh_kv("tick1 (flash handler)", g_tick1);
    sh_kv("tick2 (patched RAM handler)", g_tick2);
    uint32_t vtor_saved = SCB_VTOR;
    SCB_VTOR = base | 0x7Fu;                                 /* TBLOFF 는 [29:7] — 아래 7 비트는 무시 */
    uint32_t vtor_rb = SCB_VTOR;
    SCB_VTOR = vtor_saved;                                   /* (starter 에서는 0 으로 되돌림) */
    __asm__ volatile("dsb\n isb" ::: "memory");
    sh_kv("VTOR after writing base|0x7F", vtor_rb);
    CHECK(vtor_rb == base);

    /* ===== Part B: Thumb 비트를 지운 엔트리 → INVSTATE ===== */
    sh_puts("[B] clear the Thumb bit\n");
    int can_b = (SCB_VTOR == base) && active_ok(EXC_SVC) && active_ok(6) && active_ok(3);
    uint32_t svc_even = FN(SVC_Handler) & ~1u;

    /* B1: UsageFault 켬 + SVCall 우선순위 0x80 (UsageFault 0 보다 낮음) → UsageFault 핸들러 */
    clear_obs();
    uint32_t svc_before = g_svc_n;
    if (can_b) {
        SCB_SHCSR |= USGFAULTENA;
        SCB_SHPR2 = 0x80u << 24;
        vector_patch(g_ram_vectors, EXC_SVC, svc_even);      /* 일부러 bit0 = 0 */
        g_expect_fault = 1;
        svc0();
        sh_puts("  B1 (USGFAULTENA=1, SVC prio 0x80):\n"); dump_fault();
    }
    CHECK(g_f.ipsr == 6u);                                   /* UsageFault 핸들러에서 기록 */
    CHECK(g_f.cfsr == CFSR_INVSTATE && g_f.hfsr == 0u);
    CHECK(g_f.pc == svc_even);                               /* 쌓인 pc = 짝수 핸들러 주소 */
    CHECK((g_f.xpsr & XPSR_T) == 0u && (g_f.xpsr & 0x1FFu) == EXC_SVC);
    CHECK(g_f.exc_return == 0xFFFFFFF1u);                    /* Handler(SVC) 안에서 났다 → Handler 로 복귀 */
    CHECK(g_svc_n == svc_before + 1u);                       /* 복구 뒤 SVC_Handler 가 실제로 돌았다 */

    /* B2: UsageFault 끔 → HardFault 로 승격 (FORCED) */
    clear_obs();
    svc_before = g_svc_n;
    if (can_b) {
        SCB_SHCSR &= ~USGFAULTENA;
        g_expect_fault = 1;
        svc0();
        sh_puts("  B2 (USGFAULTENA=0):\n"); dump_fault();
    }
    CHECK(g_f.ipsr == 3u);
    CHECK(g_f.hfsr == HFSR_FORCED && g_f.cfsr == CFSR_INVSTATE);
    CHECK(g_f.pc == svc_even && g_svc_n == svc_before + 1u);

    /* B3: UsageFault 켬, 그러나 SVCall 우선순위 0 = UsageFault 와 같음 → 선점 불가 → 역시 HardFault */
    clear_obs();
    if (can_b) {
        SCB_SHCSR |= USGFAULTENA;
        SCB_SHPR2 = 0u;
        g_expect_fault = 1;
        svc0();
        sh_puts("  B3 (USGFAULTENA=1, SVC prio 0):\n"); dump_fault();
    }
    CHECK(g_f.ipsr == 3u && g_f.hfsr == HFSR_FORCED && g_f.cfsr == CFSR_INVSTATE);

    /* B4: Thread 에서 blx 짝수 주소 (함수 포인터의 bit0 누락) */
    clear_obs();
    if (can_b) {
        vector_patch(g_ram_vectors, EXC_SVC, FN(SVC_Handler));   /* 엔트리 원상복구 */
        g_expect_fault = 1;
        call_blx(FN(bx_target) & ~1u);
        sh_puts("  B4 (blx even, thread):\n"); dump_fault();
    }
    CHECK(g_f.ipsr == 6u && g_f.cfsr == CFSR_INVSTATE);
    CHECK(g_f.pc == (FN(bx_target) & ~1u));
    CHECK(g_f.lr == (FN(call_blx_ret) | 1u));               /* blx 가 남긴 복귀 주소 (홀수) */
    CHECK(g_f.exc_return == 0xFFFFFFF9u);                    /* Thread + MSP 로 복귀 */
    CHECK(g_bx_ran == 1u);                                   /* 복구 뒤 bx_target 이 실행되고 돌아왔다 */
    if (can_b) svc0();
    CHECK(active_table()[EXC_SVC] == FN(SVC_Handler));

    sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}
