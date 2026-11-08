/* ENV: qemu */
/* ARM-M11 데모 펌웨어 — Cortex-M3 (qemu-system-arm -M mps2-an385), 세미호스팅 출력, SYS_EXIT 로 종료.
 *   1) 단위 테스트: m_atomic_inc / m_cas / m_mutex_try_lock (demo_m3_prims.S)
 *   2) SysTick 스트레스: main 이 카운터를 N 번 올리는 동안 SysTick ISR 도 같은 카운터를 올린다.
 *      LDREX/STREX 판은 항상 정확, ldr/add/str 판은 ISR 이 사이에 끼면 증가를 잃는다.
 *   3) gdb 시나리오: scenario_begin() 뒤 5 라운드의 m_atomic_inc. demo_gdb_isr.gdb 가
 *      LDREX 와 STREX 사이에서 멈추고 PendSV/IRQ30 을 pend 하거나 카운터를 직접 고친다.
 * build/run: run_qemu.sh
 */
#include <stdatomic.h>
#include <stdint.h>

extern uint32_t m_atomic_inc(volatile uint32_t *p, uint32_t *attempts);
extern uint32_t m_cas(volatile uint32_t *p, uint32_t expected, uint32_t desired);
extern int m_mutex_try_lock(volatile uint32_t *lock);
extern void m_mutex_unlock(volatile uint32_t *lock);
extern uint32_t m_plain_inc(volatile uint32_t *p);

#define REG32(a)   (*(volatile uint32_t *)(a))
#define SCB_ICSR   REG32(0xE000ED04u)   /* bit28 PENDSVSET */
#define NVIC_ISER0 REG32(0xE000E100u)
#define NVIC_ISPR0 REG32(0xE000E200u)
#define SYST_CSR   REG32(0xE000E010u)
#define SYST_RVR   REG32(0xE000E014u)
#define SYST_CVR   REG32(0xE000E018u)

#ifndef TICK_RELOAD
#define TICK_RELOAD 400u
#endif
#ifndef STRESS_N
#define STRESS_N 200000u
#endif

/* ---------------- 세미호스팅 (bkpt 0xab) ---------------- */
static int semi(int op, const void *arg) {
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
static void puts_(const char *s) { semi(0x04, s); }              /* SYS_WRITE0 */
static void put_u(uint32_t v) {
    char b[12];
    int i = 11;
    b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    puts_(&b[i]);
}
static void exit_(int ok) {                                      /* SYS_EXIT */
    semi(0x18, (const void *)(uintptr_t)(ok ? 0x20026u : 0x20024u)); /* AppExit / RunTimeError */
    for (;;) { }
}

/* ---------------- 공유 데이터 ---------------- */
volatile uint32_t g_counter;
volatile uint32_t g_isr_mode;   /* 0: ISR 도 같은 카운터를 m_atomic_inc, 1: 카운터는 건드리지 않음 */
volatile uint32_t g_isr_hits;   /* ISR 안에서만 쓰고, main 은 ISR 을 끈 뒤 읽는다 */
volatile uint32_t g_isr_last_ipsr;
volatile uint32_t g_lock;

static void isr_body(void) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    g_isr_last_ipsr = ipsr;
    g_isr_hits++;
    if (g_isr_mode == 0u) {
        uint32_t a;
        (void)m_atomic_inc(&g_counter, &a);
    }
}
void SysTick_Handler(void) { isr_body(); }
void PendSV_Handler(void) { isr_body(); }
void IRQ30_Handler(void) { isr_body(); }

void HardFault_Handler(void) { puts_("HardFault!\n"); exit_(0); }
void Default_Handler(void) { puts_("unexpected exception\n"); exit_(0); }

/* C11 판 (비교용 — objdump 로 컴파일러가 낸 명령을 본다) */
__attribute__((noinline, used)) uint32_t c11_inc_relaxed(_Atomic uint32_t *p) {
    return atomic_fetch_add_explicit(p, 1u, memory_order_relaxed) + 1u;
}
__attribute__((noinline, used)) uint32_t c11_inc_seq_cst(_Atomic uint32_t *p) {
    return atomic_fetch_add(p, 1u) + 1u;
}

/* ---------------- 테스트 ---------------- */
static unsigned g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; puts_("PASS " #e "\n"); } \
                      else puts_("FAIL " #e "\n"); } while (0)

static void unit_tests(void) {
    volatile uint32_t x = 41u;
    uint32_t a = 0;
    CHECK(m_atomic_inc(&x, &a) == 42u && x == 42u && a == 1u);
    CHECK(m_cas(&x, 42u, 7u) == 42u && x == 7u);          /* 성공 */
    CHECK(m_cas(&x, 42u, 9u) == 7u && x == 7u);           /* 실패: 값 그대로 */
    g_lock = 0u;
    CHECK(m_mutex_try_lock(&g_lock) == 1 && g_lock == 1u);
    CHECK(m_mutex_try_lock(&g_lock) == 0 && g_lock == 1u); /* 이미 잠김 */
    m_mutex_unlock(&g_lock);
    CHECK(g_lock == 0u && m_mutex_try_lock(&g_lock) == 1);
    m_mutex_unlock(&g_lock);
    _Atomic uint32_t y = 5u;
    CHECK(c11_inc_relaxed(&y) == 6u && c11_inc_seq_cst(&y) == 7u);
}

static void stress(int use_atomic) {
    uint32_t retries = 0, a;
    g_counter = 0u; g_isr_hits = 0u; g_isr_mode = 0u;
    SYST_RVR = TICK_RELOAD; SYST_CVR = 0u; SYST_CSR = 7u;  /* enable | TICKINT | CPU clock */
    for (uint32_t i = 0; i < STRESS_N; i++) {
        if (use_atomic) { (void)m_atomic_inc(&g_counter, &a); retries += a - 1u; }
        else            { (void)m_plain_inc(&g_counter); }
    }
    SYST_CSR = 0u;
    uint32_t expect = STRESS_N + g_isr_hits;
    puts_(use_atomic ? "stress LDREX/STREX : counter=" : "stress ldr/add/str : counter=");
    put_u(g_counter); puts_(" expected="); put_u(expect);
    puts_(" isr_hits="); put_u(g_isr_hits);
    puts_(" lost="); put_u(expect - g_counter);
    if (use_atomic) { puts_(" strex_retries="); put_u(retries); }
    puts_("\n");
    if (use_atomic) CHECK(g_counter == expect);
}

/* gdb 가 여기 멈춘 뒤 inc_after_ldrex / inc_after_strex 에 조건부 브레이크를 건다 */
__attribute__((noinline)) void scenario_begin(void) { __asm__ volatile("" ::: "memory"); }

static void scenario(void) {
    static const char *const name[5] = {
        "R1 PendSV, ISR incs counter ",
        "R2 IRQ30,  ISR touches nothing",
        "R3 no exc, gdb writes +100   ",
        "R4 no exc, gdb writes same   ",
        "R5 nothing (control)         ",
    };
    NVIC_ISER0 = 1u << 30;
    scenario_begin();
    for (unsigned r = 0; r < 5u; r++) {
        uint32_t a = 0, hits0 = g_isr_hits, before = g_counter;
        g_isr_mode = (r == 1u) ? 1u : 0u;
        uint32_t v = m_atomic_inc(&g_counter, &a);
        puts_(name[r]); puts_(": attempts="); put_u(a);
        puts_(" before="); put_u(before); puts_(" after="); put_u(v);
        puts_(" isr_hits+="); put_u(g_isr_hits - hits0);
        puts_(" ipsr_in_isr="); put_u(g_isr_last_ipsr); puts_("\n");
    }
}

int main(void) {
    puts_("ARM-M11 Cortex-M3 LDREX/STREX demo\n");
    unit_tests();
    stress(1);
    stress(0);
    scenario();
    put_u(g_pass); puts_("/"); put_u(g_total); puts_(" passed\n");
    exit_(g_pass == g_total);
    return 0;
}

/* ---------------- 시작 코드 + 벡터 테이블 ---------------- */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
void Reset_Handler(void) {
    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;
    for (d = &_sbss; d < &_ebss; ) *d++ = 0u;
    (void)main();
    exit_(0);
}

#define D ((uintptr_t)Default_Handler)
__attribute__((section(".vectors"), used))
const uintptr_t g_vectors[16 + 32] = {
    (uintptr_t)&_estack, (uintptr_t)Reset_Handler, D /*NMI*/, (uintptr_t)HardFault_Handler,
    D, D, D, 0, 0, 0, 0, D /*SVC*/, D, 0, (uintptr_t)PendSV_Handler, (uintptr_t)SysTick_Handler,
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D,      /* IRQ0..15  */
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, (uintptr_t)IRQ30_Handler, D, /* IRQ16..31 */
};
