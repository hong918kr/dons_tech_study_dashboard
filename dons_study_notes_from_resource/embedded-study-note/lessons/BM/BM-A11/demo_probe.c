/* ENV: qemu */
/* BM-A11 demo_probe.c — QEMU 10.2.1 mps2-an385 사실 확인 (로거를 짜기 전에 "보드" 를 먼저 잰다)
 *   P1 주변장치 ID (PID0) : timer0 0x40000000, timer1 0x40001000, UART0 0x40004000, watchdog 0x40008000
 *   P2 UART0 TX 인터럽트   : 빈 버퍼에서 TX_INTEN 을 켜면? 바이트를 쓰면? 어느 NVIC 라인(ISPR0 비트)?
 *   P3 timer0             : IRQ 번호, W1C INTSTATUS, ISR 횟수 vs timer1 경과 카운트
 *   P4 watchdog           : 킥(INTCLR) = 재장전? LOCK? 1차 만료 → NMI, 2차 만료 → 리셋? RAM 은 남나?
 * 빌드/실행: bash run_qemu.sh probe   (부팅 2 번, 세미호스팅 출력)
 */
#include <stdint.h>

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];

static int semi(int op, const void *arg) {
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
static void sh_puts(const char *s) { semi(0x04, s); }
static void sh_hex(uint32_t v) {
    char b[11]; b[0] = '0'; b[1] = 'x'; b[10] = 0;
    for (int i = 9; i >= 2; --i, v >>= 4) b[i] = "0123456789abcdef"[v & 0xFu];
    sh_puts(b);
}
static void sh_dec(uint32_t v) {
    char b[11]; int i = 10; b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    sh_puts(&b[i]);
}
__attribute__((noreturn)) static void sh_exit(int code) {
    uint32_t blk[2] = { 0x20026u, (uint32_t)code };
    semi(0x20, blk);
    for (;;) { }
}

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define T0 0x40000000u
#define T1 0x40001000u
#define U0 0x40004000u
#define WD 0x40008000u
#define NVIC_ISER0 REG32(0xE000E100u)
#define NVIC_ICER0 REG32(0xE000E180u)
#define NVIC_ISPR0 REG32(0xE000E200u)
#define NVIC_ICPR0 REG32(0xE000E280u)
#define dsb() __asm__ volatile("dsb" ::: "memory")
#define isb() __asm__ volatile("isb" ::: "memory")

typedef struct { uint32_t magic, stage, nmi_count, nmi_t1, last_kick_t1, nmi_pc, nmi_lr; } probe_rec_t;
__attribute__((section(".noinit"))) probe_rec_t g_rec;
#define PROBE_MAGIC 0x50524F42u

static volatile uint32_t g_tx_irqs, g_t0_irqs;
void Uart0Tx_Handler(void) { REG32(U0 + 0x0C) = 1u; g_tx_irqs++; }
void Timer0_Handler(void) { REG32(T0 + 0x0C) = 1u; g_t0_irqs++; }
void NMI_Handler_C(const uint32_t *frame) {
    g_rec.nmi_count++;
    g_rec.nmi_t1 = REG32(T1 + 0x04);
    g_rec.nmi_pc = frame[6];
    g_rec.nmi_lr = frame[5];
}
__attribute__((naked)) void NMI_Handler(void) {
    __asm__ volatile("mrs r0, msp\n b NMI_Handler_C\n");
}
void Reset_Handler(void);
void Default_Handler(void) {
    uint32_t ipsr; __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    sh_puts("unexpected exception "); sh_dec(ipsr); sh_puts("\n"); sh_exit(2);
}
typedef void (*vector_t)(void);
#define D Default_Handler
__attribute__((section(".isr_vector"), used)) const vector_t g_vectors[16 + 32] = {
    (vector_t)_estack, Reset_Handler, NMI_Handler, D, D, D, D, 0, 0, 0, 0, D, D, 0, D, D,
    D, Uart0Tx_Handler, D, D, D, D, D, D, Timer0_Handler, D, D, D, D, D, D, D,
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D,
};
/* 헤더 자리 (링커 ASSERT 용 64 B; probe 는 검사하지 않음) */
__attribute__((section(".img_hdr"), used)) const uint32_t g_hdr_placeholder[16] = { 1u };

static void line(const char *k, uint32_t v) { sh_puts(k); sh_hex(v); sh_puts("\n"); }

int main(void) {
    if (g_rec.magic == PROBE_MAGIC && g_rec.stage == 1u) {
        sh_puts("== boot 2 (after watchdog) ==\n");
        line("P4 .noinit survived: nmi_count=", g_rec.nmi_count);
        line("P4 timer1 down-count from last kick to NMI  = ", g_rec.last_kick_t1 - g_rec.nmi_t1);
        line("P4 NMI stacked pc (hang loop?) = ", g_rec.nmi_pc);
        extern const char probe_hang[];
        line("P4 &probe_hang                 = ", (uint32_t)(uintptr_t)probe_hang);
        line("P4 WDOGCONTROL after reset = ", REG32(WD + 0x08));
        line("P4 WDOGLOAD after reset    = ", REG32(WD + 0x00));
        line("P4 timer0 CTRL after reset = ", REG32(T0 + 0x00));
        line("P4 UART0 CTRL after reset  = ", REG32(U0 + 0x08));
        g_rec.magic = 0u;
        sh_exit(0);
    }
    sh_puts("== boot 1 ==\n");
    line("P1 PID0 timer0   = ", REG32(T0 + 0xFE0));
    line("P1 PID0 timer1   = ", REG32(T1 + 0xFE0));
    line("P1 PID0 UART0    = ", REG32(U0 + 0xFE0));
    line("P1 PID0 watchdog = ", REG32(WD + 0xFE0));

    /* P2 UART0 */
    REG32(U0 + 0x10) = 217u;                       /* BAUDDIV: 25 MHz / 115200 */
    REG32(U0 + 0x08) = 1u | 4u;                    /* TX_EN | TX_INTEN, 버퍼는 비어 있음 */
    dsb();
    line("P2 after TX_INTEN on empty buffer: INTSTATUS=", REG32(U0 + 0x0C));
    line("P2                                  NVIC ISPR0=", NVIC_ISPR0);
    REG32(U0 + 0x00) = 'x';                        /* 첫 바이트 (NVIC 는 아직 꺼짐) */
    dsb();
    line("P2 after 1 byte written: STATE=", REG32(U0 + 0x04));
    line("P2                       INTSTATUS=", REG32(U0 + 0x0C));
    line("P2                       NVIC ISPR0=", NVIC_ISPR0);
    REG32(U0 + 0x0C) = 1u;                         /* W1C */
    dsb();
    line("P2 after INTSTATUS W1C:  INTSTATUS=", REG32(U0 + 0x0C));
    line("P2                       NVIC ISPR0 (latched?)=", NVIC_ISPR0);
    NVIC_ICPR0 = 0xFFFFFFFFu;
    NVIC_ISER0 = 1u << 1;                          /* UART0 TX = IRQ 1 */
    dsb(); isb();
    for (int i = 0; i < 5; ++i) { REG32(U0 + 0x00) = 'y'; dsb(); isb(); }
    line("P2 5 bytes with IRQ1 enabled -> TX ISR count=", g_tx_irqs);
    REG32(U0 + 0x00) = '\n';
    dsb(); isb();

    /* P3 timer0 (IRQ 8) vs timer1 free-running */
    REG32(T1 + 0x08) = 0xFFFFFFFFu; REG32(T1 + 0x00) = 1u;          /* timer1: 인터럽트 없이 자유 카운트 */
    REG32(T0 + 0x08) = 24999u;                                       /* 25 MHz / 25000 = 1 kHz */
    REG32(T0 + 0x00) = 1u | 8u;                                      /* EN | IRQEN */
    NVIC_ISER0 = 1u << 8;
    uint32_t t_start = REG32(T1 + 0x04);
    while (g_t0_irqs < 50u) { __asm__ volatile("wfi"); }
    uint32_t t_end = REG32(T1 + 0x04);
    line("P3 50 timer0 ISRs took timer1 counts = ", t_start - t_end);
    REG32(T0 + 0x00) = 0u;
    NVIC_ICER0 = 1u << 8;
    line("P3 timer0 INTSTATUS after stop = ", REG32(T0 + 0x0C));

    /* P4 watchdog */
    line("P4 WDOGLOAD reset value = ", REG32(WD + 0x00));
    line("P4 WDOGCONTROL reset    = ", REG32(WD + 0x08));
    REG32(WD + 0xC00) = 0x1ACCE551u;               /* unlock */
    REG32(WD + 0x00) = 250000u;                    /* 10 ms @ 25 MHz */
    REG32(WD + 0x08) = 3u;                         /* INTEN | RESEN */
    REG32(WD + 0xC00) = 0u;                        /* lock */
    REG32(WD + 0x08) = 0u;                         /* 잠긴 상태에서 끄기 시도 */
    line("P4 locked, tried CONTROL=0 -> CONTROL = ", REG32(WD + 0x08));
    /* 킥: 25 ms 동안 2 ms 마다 INTCLR (잠금 풀고 쓰고 다시 잠금) → NMI 없어야 */
    uint32_t t0 = REG32(T1 + 0x04);
    uint32_t last = t0;
    while (t0 - REG32(T1 + 0x04) < 625000u) {
        if (last - REG32(T1 + 0x04) >= 50000u) {
            REG32(WD + 0xC00) = 0x1ACCE551u; REG32(WD + 0x0C) = 1u; REG32(WD + 0xC00) = 0u;
            last = REG32(T1 + 0x04);
        }
    }
    line("P4 25 ms with kicks every 2 ms: NMI count = ", g_rec.nmi_count);
    g_rec.magic = PROBE_MAGIC; g_rec.stage = 1u; g_rec.nmi_count = 0u;
    REG32(WD + 0xC00) = 0x1ACCE551u; REG32(WD + 0x0C) = 1u; REG32(WD + 0xC00) = 0u;
    g_rec.last_kick_t1 = REG32(T1 + 0x04);
    sh_puts("P4 stop kicking -> expect NMI after 10 ms, reset after 20 ms\n");
    __asm__ volatile(".global probe_hang\nprobe_hang: b .");
    return 1;
}

__attribute__((noreturn)) void Reset_Handler(void) {
    const uint32_t *src = _sidata;
    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;
    for (uint32_t *dst = _sbss; dst < _ebss;) *dst++ = 0u;
    sh_exit(main());
}
