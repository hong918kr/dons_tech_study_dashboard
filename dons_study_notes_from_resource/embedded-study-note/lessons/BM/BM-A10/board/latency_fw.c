/* ENV: board */
/* BM-A10 인터럽트 지연 측정 펌웨어 — STM32F407 (예: STM32F4-Discovery), 리셋 클럭 HSI 16 MHz 그대로.
 *
 * 이 파일은 Lima VM 에서 arm-none-eabi-gcc -mcpu=cortex-m4 로 **빌드만** 확인했다 (build_vm.sh fw).
 * 보드에서 돌린 적은 없다 → 이 레슨의 어떤 숫자도 이 펌웨어의 실측이 아니다.
 * 레지스터 주소·비트·IRQ 번호는 RM0090 / ARMv7-M ARM 기준 대표값 — 보드에 올리기 전에 문서 확인 권장.
 *
 * 측정 원리
 *   main:  r1 = &DWT_CYCCNT 를 둔 채  [ldr t0,[r1]] → [트리거 store] → dsb/isb
 *   ISR :  첫 명령이 [ldr r0,[r1]] — 예외 진입은 r0–r3 를 스택에 "복사" 할 뿐 값을 지우지 않으므로 (ARMv7-M,
 *          확인 권장) r1 은 여전히 CYCCNT 주소다. 그래서 ISR 안에서 주소를 다시 읽는 명령이 필요 없다.
 *   raw = t_isr − t0 = (t0 표본 LDR 이후) + 트리거 경로 + 진입 지연.  calib = back-to-back LDR 두 번의 차.
 *   트리거 경로(store → NVIC 펜딩)는 모른다 → 절대값보다 **시나리오 간 차이**를 믿는다 (WS·FP·ART 효과).
 *
 * 배선: PA8(출력) ──점퍼── PB0(EXTI0 입력). PB1 = ISR 표시 핀(로직 애널라이저용). USART2 TX = PA2, 115200 8N1.
 * 출력: solution.c 의 분석기가 읽는 덤프 형식 v1 (H/C/S/D/E 줄).
 */
#include <stddef.h>
#include <stdint.h>
#include "dwt_cyccnt.h"

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];

#define REG(a)          (*(volatile uint32_t *)(uintptr_t)(a))
#define REG8(a)         (*(volatile uint8_t *)(uintptr_t)(a))
/* RCC (RM0090) */
#define RCC_AHB1ENR     REG(0x40023830u)
#define RCC_APB1ENR     REG(0x40023840u)
#define RCC_APB2ENR     REG(0x40023844u)
/* GPIO */
#define GPIOA           0x40020000u
#define GPIOB           0x40020400u
#define GPIO_MODER(p)   REG((p) + 0x00u)
#define GPIO_BSRR(p)    REG((p) + 0x18u)
#define GPIO_AFRL(p)    REG((p) + 0x20u)
/* USART2 */
#define USART2_SR       REG(0x40004400u)
#define USART2_DR       REG(0x40004404u)
#define USART2_BRR      REG(0x40004408u)
#define USART2_CR1      REG(0x4000440Cu)
/* FLASH_ACR: LATENCY[2:0], PRFTEN 8, ICEN 9, DCEN 10, ICRST 11, DCRST 12 */
#define FLASH_ACR       REG(0x40023C00u)
#define ACR_PRFTEN      (1u << 8)
#define ACR_ICEN        (1u << 9)
#define ACR_DCEN        (1u << 10)
#define ACR_ICRST       (1u << 11)
#define ACR_DCRST       (1u << 12)
/* EXTI / SYSCFG */
#define EXTI_IMR        REG(0x40013C00u)
#define EXTI_RTSR       REG(0x40013C08u)
#define EXTI_SWIER      REG(0x40013C10u)
#define EXTI_PR         REG(0x40013C14u)
#define SYSCFG_EXTICR1  REG(0x40013808u)
/* TIM2 / TIM5 (32 비트, APB1) */
#define TIM2            0x40000000u
#define TIM5            0x40000C00u
#define TIM_CR1(t)      REG((t) + 0x00u)
#define TIM_DIER(t)     REG((t) + 0x0Cu)
#define TIM_SR(t)       REG((t) + 0x10u)
#define TIM_EGR(t)      REG((t) + 0x14u)
#define TIM_CNT(t)      REG((t) + 0x24u)
#define TIM_PSC(t)      REG((t) + 0x28u)
#define TIM_ARR(t)      REG((t) + 0x2Cu)
#define TIM_CCR1(t)     REG((t) + 0x34u)
#define TIM_CC1IF       (1u << 1)   /* SR bit1 (rc_w0) */
#define TIM_CC1IE       (1u << 1)   /* DIER bit1 */
/* NVIC / SCB / FPU */
#define NVIC_ISER(n)    REG(0xE000E100u + 4u * (n))
#define NVIC_ICER(n)    REG(0xE000E180u + 4u * (n))
#define NVIC_ISPR(n)    REG(0xE000E200u + 4u * (n))
#define NVIC_ICPR(n)    REG(0xE000E280u + 4u * (n))
#define NVIC_IPR8(i)    REG8(0xE000E400u + (i))
#define NVIC_STIR       REG(0xE000EF00u)
#define SCB_CPACR       REG(0xE000ED88u)
#define FPU_FPCCR       REG(0xE000EF34u)
#define FPCCR_ASPEN     (1u << 31)
#define FPCCR_LSPEN     (1u << 30)
/* IRQ 번호 (STM32F407 벡터 표 — 확인 권장) */
#define IRQ_EXTI0       6
#define IRQ_EXTI1       7
#define IRQ_EXTI2       8
#define IRQ_TIM2        28
#define IRQ_TIM5        50
#define N_IRQ           82

#define CORE_HZ         16000000u
#define N_SAMP          1000u
#define N_LATE          100u
#define WAIT_LOOPS      100000u

/* ISR 가 채우는 표본. 오프셋은 아래 naked asm 이 그대로 쓴다 → _Static_assert 로 고정 */
typedef struct {
    volatile uint32_t t_isr0, t_end0, t_isr1, t_end0_reg, t_low, t_high, seq, low_seq, high_seq, t_fp0, t_fp1, done;
} samples_t;
_Static_assert(offsetof(samples_t, t_end0) == 4 && offsetof(samples_t, t_isr1) == 8, "asm offsets");
_Static_assert(offsetof(samples_t, t_end0_reg) == 12 && offsetof(samples_t, t_low) == 16, "asm offsets");
_Static_assert(offsetof(samples_t, t_high) == 20 && offsetof(samples_t, seq) == 24, "asm offsets");
_Static_assert(offsetof(samples_t, t_fp0) == 36 && offsetof(samples_t, done) == 44, "asm offsets");
samples_t g_s;

static uint32_t s_t0[N_SAMP], s_t1[N_SAMP];
static uint32_t g_sum, g_total, g_timeouts;

/* ---------------- ISR (naked: 첫 명령이 표본) ---------------- */
/* EXTI0 (IRQ 6): 진입 표본 → PR 지우기 → 끝 표본(r3 에도 남김: 꼬리 물기면 다음 ISR 이 그대로 본다) */
__attribute__((naked)) void EXTI0_IRQHandler(void)
{
    __asm__ volatile(
        "ldr   r0, [r1]          \n"   /* (1) 진입 표본: r1 = &CYCCNT (main 이 남김) */
        "ldr   r2, =g_s          \n"
        "str   r0, [r2, #0]      \n"   /* t_isr0 */
        "ldr   r0, =0x40020418   \n"   /* GPIOB_BSRR */
        "movs  r3, #2            \n"
        "str   r3, [r0]          \n"   /* PB1 = 1 (LA 표시) */
        "ldr   r0, =0x40013C14   \n"
        "movs  r3, #3            \n"
        "str   r3, [r0]          \n"   /* EXTI_PR = PR0|PR1 (W1C) */
        "ldr   r0, [r2, #44]     \n"
        "adds  r0, r0, #1        \n"
        "str   r0, [r2, #44]     \n"   /* done++ */
        "dsb                     \n"   /* 지우기가 복귀 전에 도착 (BM-M01 late clear) */
        "ldr   r3, [r1]          \n"   /* (2) 끝 표본 */
        "str   r3, [r2, #4]      \n"   /* t_end0 — 이 STR 1 개가 갭 측정의 오프셋 */
        "bx    lr                \n"
        ".ltorg                  \n");
}
/* EXTI1 (IRQ 7): 꼬리 물기로 들어오면 r1 과 r3 는 EXTI0 이 남긴 값 그대로 */
__attribute__((naked)) void EXTI1_IRQHandler(void)
{
    __asm__ volatile(
        "ldr   r0, [r1]          \n"
        "ldr   r2, =g_s          \n"
        "str   r0, [r2, #8]      \n"   /* t_isr1 */
        "str   r3, [r2, #12]     \n"   /* t_end0_reg */
        "ldr   r0, [r2, #44]     \n"
        "adds  r0, r0, #1        \n"
        "str   r0, [r2, #44]     \n"
        "bx    lr                \n"
        ".ltorg                  \n");
}
/* EXTI2 (IRQ 8): lazy FP 멈춤 — 표본, VADD (첫 FP 명령), 표본 */
__attribute__((naked)) void EXTI2_IRQHandler(void)
{
    __asm__ volatile(
        "ldr   r0, [r1]          \n"
        "vadd.f32 s0, s0, s0     \n"   /* LSPEN=1 이고 FPCA=1 이었으면 여기서 S0–S15+FPSCR 저장으로 멈춤 */
        "ldr   r3, [r1]          \n"
        "ldr   r2, =g_s          \n"
        "str   r0, [r2, #36]     \n"
        "str   r3, [r2, #40]     \n"
        "ldr   r0, [r2, #44]     \n"
        "adds  r0, r0, #1        \n"
        "str   r0, [r2, #44]     \n"
        "bx    lr                \n"
        ".ltorg                  \n");
}
/* TIM2 (낮음) / TIM5 (높음): late arrival. 표본 후 C 로 꼬리 호출 (LR = EXC_RETURN 유지) */
void tim2_rest(void);
void tim5_rest(void);
__attribute__((naked)) void TIM2_IRQHandler(void)
{
    __asm__ volatile("ldr r0, =0xE0001004\n ldr r0, [r0]\n ldr r2, =g_s\n str r0, [r2, #16]\n b tim2_rest\n .ltorg\n");
}
__attribute__((naked)) void TIM5_IRQHandler(void)
{
    __asm__ volatile("ldr r0, =0xE0001004\n ldr r0, [r0]\n ldr r2, =g_s\n str r0, [r2, #20]\n b tim5_rest\n .ltorg\n");
}
void tim2_rest(void) { TIM_SR(TIM2) = ~TIM_CC1IF; TIM_DIER(TIM2) = 0; g_s.low_seq = ++g_s.seq; ++g_s.done; __asm__ volatile("dsb"); }
void tim5_rest(void) { TIM_SR(TIM5) = ~TIM_CC1IF; TIM_DIER(TIM5) = 0; g_s.high_seq = ++g_s.seq; ++g_s.done; __asm__ volatile("dsb"); }

/* ---------------- startup ---------------- */
void Reset_Handler(void);
static void Default_Handler(void) { for (;;) { } }
__attribute__((section(".isr_vector"), used))
static void (*const g_vectors[16 + N_IRQ])(void) = {
    [0] = (void (*)(void))(uintptr_t)_estack, [1] = Reset_Handler,
    [2 ... 15] = Default_Handler,
    [16 ... 16 + IRQ_EXTI0 - 1] = Default_Handler,
    [16 + IRQ_EXTI0] = EXTI0_IRQHandler, [16 + IRQ_EXTI1] = EXTI1_IRQHandler, [16 + IRQ_EXTI2] = EXTI2_IRQHandler,
    [16 + IRQ_EXTI2 + 1 ... 16 + IRQ_TIM2 - 1] = Default_Handler,
    [16 + IRQ_TIM2] = TIM2_IRQHandler,
    [16 + IRQ_TIM2 + 1 ... 16 + IRQ_TIM5 - 1] = Default_Handler,
    [16 + IRQ_TIM5] = TIM5_IRQHandler,
    [16 + IRQ_TIM5 + 1 ... 16 + N_IRQ - 1] = Default_Handler,
};

/* ---------------- UART 출력 (폴링) ---------------- */
static void uart_init(void)
{
    RCC_AHB1ENR |= (1u << 0) | (1u << 1);               /* GPIOA, GPIOB */
    RCC_APB1ENR |= (1u << 17) | (1u << 0) | (1u << 3);  /* USART2, TIM2, TIM5 */
    RCC_APB2ENR |= (1u << 14);                          /* SYSCFG */
    (void)RCC_APB2ENR;                                  /* 클럭 켠 뒤 더미 읽기 (BM-B03) */
    GPIO_MODER(GPIOA) = (GPIO_MODER(GPIOA) & ~(3u << 4)) | (2u << 4);   /* PA2 = AF */
    GPIO_AFRL(GPIOA) = (GPIO_AFRL(GPIOA) & ~(0xFu << 8)) | (7u << 8);   /* AF7 = USART2 */
    USART2_BRR = (CORE_HZ + 115200u / 2u) / 115200u;    /* 139 = 0x8B: 16 MHz / 115200, OVER8=0 */
    USART2_CR1 = (1u << 13) | (1u << 3);                /* UE | TE */
}
static void putc_(char c) { while (!(USART2_SR & (1u << 7))) { } USART2_DR = (uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void putdec(uint32_t v) { char b[11]; int i = 10; b[i] = 0; do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v); puts_(&b[i]); }
static void puthex8(uint32_t v) { for (int i = 28; i >= 0; i -= 4) putc_("0123456789abcdef"[(v >> i) & 0xFu]); }

/* ---------------- 측정 원시 연산 ---------------- */
/* back-to-back 두 번 읽기: 같은 LDR 모양 */
static uint32_t calib_once(void)
{
    register volatile uint32_t *r1 __asm__("r1") = &DWT_CYCCNT;
    uint32_t a, b;
    __asm__ volatile("ldr %0, [%2]\n ldr %1, [%2]\n" : "=&r"(a), "=&r"(b) : "r"(r1) : "memory");
    return b - a;
}
/* t0 표본 → store(트리거) → dsb/isb.  r1 은 ISR 이 쓰도록 &CYCCNT 로 고정 */
static uint32_t trig_store(volatile uint32_t *reg, uint32_t val)
{
    register volatile uint32_t *r1 __asm__("r1") = &DWT_CYCCNT;
    uint32_t t0;
    __asm__ volatile("ldr %0, [%1]\n str %2, [%3]\n dsb\n isb\n"
                     : "=&r"(t0) : "r"(r1), "r"(val), "r"(reg) : "memory");
    return t0;
}
/* 스레드로 돌아온 직후 표본 (exit 갭) — 트리거와 같은 asm 블록 안에서 */
static uint32_t trig_store_resume(volatile uint32_t *reg, uint32_t val, uint32_t *t_resume)
{
    register volatile uint32_t *r1 __asm__("r1") = &DWT_CYCCNT;
    uint32_t t0, t2;
    __asm__ volatile("ldr %0, [%2]\n str %3, [%4]\n dsb\n isb\n ldr %1, [%2]\n"
                     : "=&r"(t0), "=&r"(t2) : "r"(r1), "r"(val), "r"(reg) : "memory");
    *t_resume = t2;
    return t0;
}
static int wait_done(uint32_t want)
{
    for (uint32_t i = 0; i < WAIT_LOOPS; ++i) if (g_s.done >= want) return 0;
    ++g_timeouts;
    return -1;
}
static void flash_cfg(uint32_t ws, uint32_t art)
{
    uint32_t v = (FLASH_ACR & ~(7u | ACR_PRFTEN | ACR_ICEN | ACR_DCEN)) | ws | (art ? (ACR_PRFTEN | ACR_ICEN | ACR_DCEN) : 0u);
    FLASH_ACR = v;
    while ((FLASH_ACR & 7u) != ws) { }                  /* RM: LATENCY 는 읽어서 확인 */
}
static void art_flush(void)
{
    uint32_t v = FLASH_ACR;
    FLASH_ACR = v & ~(ACR_ICEN | ACR_DCEN);             /* 끈 상태에서만 리셋 가능 (확인 권장) */
    FLASH_ACR = (v & ~(ACR_ICEN | ACR_DCEN)) | ACR_ICRST | ACR_DCRST;
    FLASH_ACR = v & ~(ACR_ICEN | ACR_DCEN);
    FLASH_ACR = v;
}
static void fpca_set(int on)
{
    if (on) { __asm__ volatile("vmov.f32 s1, s0" ::: "memory"); return; }   /* FP 명령 하나 → CONTROL.FPCA = 1 */
    __asm__ volatile("mrs r0, control\n bic r0, r0, #4\n msr control, r0\n isb\n" ::: "r0", "memory");
}

/* ---------------- 시나리오 ---------------- */
enum { K_STIR, K_SWIER, K_GPIO, K_TAIL, K_EXIT, K_LAZY };
static void emit_scn(uint32_t id, const char *name, uint32_t n)
{
    puts_("S,"); putdec(id); putc_(','); puts_(name); putc_(','); putdec(n); puts_("\n");
    for (uint32_t i = 0; i < n; ++i) {
        puts_("D,"); putdec(id); putc_(','); putdec(i); putc_(','); puthex8(s_t0[i]); putc_(','); puthex8(s_t1[i]); puts_("\n");
        g_sum += s_t1[i] - s_t0[i]; ++g_total;
    }
}
static void one_sample(int kind, uint32_t i, int cold)
{
    uint32_t t0 = 0, t1 = 0, base = g_s.done;
    int to = 0;
    if (cold) art_flush();
    switch (kind) {
    case K_STIR:  t0 = trig_store(&NVIC_STIR, IRQ_EXTI0); to = wait_done(base + 1); t1 = g_s.t_isr0; break;
    case K_SWIER: t0 = trig_store(&EXTI_SWIER, 1u);       to = wait_done(base + 1); t1 = g_s.t_isr0; break;
    case K_GPIO:  t0 = trig_store(&GPIO_BSRR(GPIOA), 1u << 8); to = wait_done(base + 1); t1 = g_s.t_isr0;
                  GPIO_BSRR(GPIOA) = 1u << (8 + 16); break;
    case K_TAIL:  (void)trig_store(&NVIC_ISPR(0), (1u << IRQ_EXTI0) | (1u << IRQ_EXTI1)); to = wait_done(base + 2);
                  t0 = g_s.t_end0_reg; t1 = g_s.t_isr1; break;
    case K_EXIT:  (void)trig_store_resume(&NVIC_STIR, IRQ_EXTI0, &t1); to = wait_done(base + 1); t0 = g_s.t_end0; break;
    case K_LAZY:  (void)trig_store(&NVIC_STIR, IRQ_EXTI2); to = wait_done(base + 1); t0 = g_s.t_fp0; t1 = g_s.t_fp1; break;
    }
    GPIO_BSRR(GPIOB) = 1u << (1 + 16);
    if (to) t1 = t0;                                   /* 타임아웃 표본은 raw 0 → 분석기에서 눈에 띈다 */
    s_t0[i] = t0; s_t1[i] = t1;
}
static void run_scn(uint32_t id, const char *name, int kind, uint32_t ws, int art, int cold, int fpca, int lspen)
{
    flash_cfg(ws, (uint32_t)art);
    FPU_FPCCR = FPCCR_ASPEN | (lspen ? FPCCR_LSPEN : 0u);
    for (uint32_t i = 0; i < N_SAMP; ++i) { fpca_set(fpca); one_sample(kind, i, cold); }
    emit_scn(id, name, N_SAMP);
}
/* late arrival: TIM2 (낮음) CCR1 = 2000, TIM5 (높음) CCR1 = 2000 + d. 둘을 연달아 켜서 생기는 시작 차(skew)는
 * 상수 → d 축이 그만큼 밀릴 뿐. t0 = 시작 직전 CYCCNT, t1 = 높은 ISR 첫 표본. 순서는 주석 줄로. */
static void run_late(uint32_t id, uint32_t d)
{
    char name[12] = "late_d00";
    uint32_t high_first = 0;
    name[6] = (char)('0' + d / 10u); name[7] = (char)('0' + d % 10u);
    flash_cfg(0, 0);
    for (uint32_t i = 0; i < N_LATE; ++i) {
        uint32_t base = g_s.done;
        TIM_CR1(TIM2) = 0; TIM_CR1(TIM5) = 0;
        TIM_PSC(TIM2) = 0; TIM_PSC(TIM5) = 0; TIM_ARR(TIM2) = 0xFFFFFFFFu; TIM_ARR(TIM5) = 0xFFFFFFFFu;
        TIM_EGR(TIM2) = 1; TIM_EGR(TIM5) = 1;            /* PSC 적용 (BM-B14) */
        TIM_CNT(TIM2) = 0; TIM_CNT(TIM5) = 0;
        TIM_CCR1(TIM2) = 2000u; TIM_CCR1(TIM5) = 2000u + d;
        TIM_SR(TIM2) = 0; TIM_SR(TIM5) = 0;              /* UG 가 세운 UIF 등 정리 */
        TIM_DIER(TIM2) = TIM_CC1IE; TIM_DIER(TIM5) = TIM_CC1IE;
        uint32_t t0 = DWT_CYCCNT;
        TIM_CR1(TIM2) = 1u; TIM_CR1(TIM5) = 1u;
        wait_done(base + 2);
        TIM_CR1(TIM2) = 0; TIM_CR1(TIM5) = 0;
        if (g_s.high_seq < g_s.low_seq) ++high_first;
        s_t0[i] = t0; s_t1[i] = g_s.t_high;
    }
    puts_("# late "); puts_(name); puts_(" high_first="); putdec(high_first); puts_("/"); putdec(N_LATE); puts_("\n");
    emit_scn(id, name, N_LATE);
}

int main(void)
{
    uart_init();
    GPIO_MODER(GPIOA) = (GPIO_MODER(GPIOA) & ~(3u << 16)) | (1u << 16);   /* PA8 출력 (트리거) */
    GPIO_MODER(GPIOB) = (GPIO_MODER(GPIOB) & ~(3u << 2)) | (1u << 2);     /* PB1 출력 (LA 표시), PB0 입력 */
    SYSCFG_EXTICR1 = (SYSCFG_EXTICR1 & ~0xFu) | 1u;     /* EXTI0 ← 포트 B */
    EXTI_RTSR |= 1u; EXTI_IMR |= 1u;
    NVIC_IPR8(IRQ_EXTI0) = 0x40; NVIC_IPR8(IRQ_EXTI1) = 0x40; NVIC_IPR8(IRQ_EXTI2) = 0x40;
    NVIC_IPR8(IRQ_TIM2) = 0x80;                         /* 낮음 */
    NVIC_IPR8(IRQ_TIM5) = 0x00;                         /* 높음 */
    NVIC_ISER(0) = (1u << IRQ_EXTI0) | (1u << IRQ_EXTI1) | (1u << IRQ_EXTI2) | (1u << IRQ_TIM2);
    NVIC_ISER(1) = 1u << (IRQ_TIM5 - 32);

    int rc = dwt_cyccnt_init();
    puts_("# BM-A10 latency dump v1, STM32F407 @HSI 16 MHz, dwt_init="); putdec((uint32_t)rc); puts_("\n");
    uint32_t calib = 0xFFFFFFFFu;
    for (int i = 0; i < 64; ++i) { uint32_t c = calib_once(); if (c < calib) calib = c; }
    puts_("H,"); putdec(CORE_HZ); puts_(",23\nC,"); putdec(calib); puts_("\n");
    g_sum = CORE_HZ + calib;

    uint32_t id = 0;
    run_scn(id++, "entry_stir_ws0",   K_STIR,  0, 0, 0, 0, 1);
    run_scn(id++, "entry_swier_ws0",  K_SWIER, 0, 0, 0, 0, 1);
    run_scn(id++, "entry_gpio_ws0",   K_GPIO,  0, 0, 0, 0, 1);
    run_scn(id++, "entry_stir_ws1",   K_STIR,  1, 0, 0, 0, 1);
    run_scn(id++, "entry_stir_ws3",   K_STIR,  3, 0, 0, 0, 1);
    run_scn(id++, "entry_stir_ws5",   K_STIR,  5, 0, 0, 0, 1);
    run_scn(id++, "entry_ws5_art",    K_STIR,  5, 1, 0, 0, 1);
    run_scn(id++, "entry_ws5_artcold",K_STIR,  5, 1, 1, 0, 1);
    run_scn(id++, "tail_gap_ws0",     K_TAIL,  0, 0, 0, 0, 1);
    run_scn(id++, "exit_ws0",         K_EXIT,  0, 0, 0, 0, 1);
    run_scn(id++, "entry_fp_full",    K_STIR,  0, 0, 0, 1, 0);
    run_scn(id++, "entry_fp_lazy",    K_STIR,  0, 0, 0, 1, 1);
    run_scn(id++, "entry_nofp_lazy",  K_STIR,  0, 0, 0, 0, 1);
    run_scn(id++, "lazy_stall",       K_LAZY,  0, 0, 0, 1, 1);
    run_scn(id++, "lazy_nostall",     K_LAZY,  0, 0, 0, 0, 1);
    for (uint32_t d = 0; d < 16; d += 2) run_late(id++, d);      /* 15 + 8 = 23 시나리오 */

    puts_("E,"); putdec(g_total); putc_(','); puthex8(g_sum); puts_("\n");
    puts_("# timeouts="); putdec(g_timeouts); puts_("\n");
    for (;;) { __asm__ volatile("wfi"); }
}

void Reset_Handler(void)
{
    uint32_t *s = _sidata, *d = _sdata;
    while (d < _edata) *d++ = *s++;
    for (d = _sbss; d < _ebss; ) *d++ = 0;
    SCB_CPACR |= 0xFu << 20;                            /* CP10/CP11 전체 접근 — FP 명령 전에 (BM-A01) */
    __asm__ volatile("dsb\n isb" ::: "memory");
    (void)main();
    for (;;) { }
}
