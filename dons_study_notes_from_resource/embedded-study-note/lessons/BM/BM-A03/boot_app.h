/* ENV: qemu */
/* BM-A03 공유 헤더 — 부트로더(starter.c / solution.c)와 앱(app.c)이 "같이 합의해야 하는 것" 만 담는다.
 *   1) 플래시 배치 (부트로더 0x00000000 16 KB, 앱 슬롯 0x00004000) — boot.ld / app.ld 와 같은 숫자
 *   2) 메일박스: 두 이미지가 같은 고정 RAM 주소(0x2000F800)를 보는 .noinit 구조체 (어느 링커 스크립트도 할당 안 함)
 *   3) 레지스터 주소 (ARMv7-M ARM / CMSDK 기준 — 확인 권장, QEMU 10.2.1 mps2-an385 에서 probe 로 확인)
 *   4) 세미호스팅 출력 (BM-B08 축약판)
 * 대상: qemu-system-arm -M mps2-an385 (Cortex-M3). 같은 lesson 디렉터리 안에서만 공유한다.
 */
#ifndef BOOT_APP_H
#define BOOT_APP_H
#include <stdint.h>

/* ---------------- 1) 배치 ---------------- */
#define BOOT_BASE      0x00000000u
#define BOOT_SIZE      0x00004000u      /* 16 KB = STM32F4 섹터 0 하나 (크기 확인 권장) */
#define APP_BASE       0x00004000u      /* 앱 벡터 테이블 = 슬롯 맨 앞 */
#define APP_LIMIT      0x00040000u      /* 앱 슬롯 끝 (exclusive): 240 KB */
#define APP_NVEC       48u              /* 16 시스템 + 32 외부 IRQ (mps2-an385 num-irq = 32) */
#define RAM_LO         0x20000000u
#define APP_RAM_HI     0x2000F800u      /* 앱 스택 꼭대기 = 메일박스 바로 아래 */
#define MB_ADDR        0x2000F800u      /* 메일박스 256 B */
#define PLAN_ADDR      0x2000F900u      /* 부트로더 전용 시험 계획 1.75 KB */

/* ---------------- 2) 메일박스 (부트로더 ↔ 앱, 리셋을 건너 살아남는 RAM) ---------------- */
#define MB_MAGIC       0x4A554D50u      /* "JUMP" */
enum { STAGE_NOT_STARTED = 0, STAGE_ENTERED = 1, STAGE_READY = 2, STAGE_DONE = 3, STAGE_FAULT = 4 };

typedef struct {
    uint32_t magic;          /* 앱(또는 앱의 fault 핸들러)이 관찰을 다 쓴 뒤 마지막에 MB_MAGIC */
    uint32_t step;           /* 부트로더가 점프 직전에 쓴다: 이번 시험 단계 번호 */
    uint32_t stage;          /* 앱이 어디까지 왔나 (STAGE_*) */
    uint32_t boot_ticks;     /* "부트로더의" SysTick 핸들러가 올린다 — 누가 불려도 여기로 */
    uint32_t boot_irq_hits;  /* 부트로더의 IRQ7 핸들러 */
    uint32_t early_ticks;    /* 앱 SysTick 핸들러가 stage < READY 에서 불린 횟수 (= 준비 전 인터럽트) */
    uint32_t early_irqs;     /* 앱 외부 IRQ 핸들러가 stage < READY 에서 불린 횟수 */
    uint32_t early_irq_num;  /* 마지막 early IRQ 번호 */
    uint32_t early_stage;    /* early 인터럽트가 왔을 때의 stage (0 = 앱 첫 명령보다 먼저) */
    uint32_t app_ticks;      /* 앱 SysTick 핸들러 (READY 이후) */
    /* 앱 진입 순간의 스냅샷 (naked Reset_Handler 가 레지스터를 건드리기 전에 읽음) */
    uint32_t e_msp, e_primask, e_control, e_vtor, e_syst_csr, e_icsr, e_iser0, e_ispr0, e_uart_ctrl;
    uint32_t boot_ticks_in_app;  /* 앱이 SysTick 을 켠 동안 boot_ticks 가 늘어난 양 (VTOR 를 잊으면 > 0) */
    uint32_t f_exc, f_cfsr, f_hfsr, f_pc;  /* 앱의 fault 핸들러가 쓴 기록 */
    uint32_t f_stage;        /* fault 가 났을 때의 stage (0 = 앱의 첫 명령도 못 돌았다) */
} mailbox_t;
#define MB ((volatile mailbox_t *)MB_ADDR)

/* ---------------- 3) 레지스터 ---------------- */
#define REG32(a)        (*(volatile uint32_t *)(uintptr_t)(a))
#define SCB_ICSR        REG32(0xE000ED04u)
#define SCB_VTOR        REG32(0xE000ED08u)
#define SCB_AIRCR       REG32(0xE000ED0Cu)
#define SCB_SHCSR       REG32(0xE000ED24u)
#define SCB_CFSR        REG32(0xE000ED28u)
#define SCB_HFSR        REG32(0xE000ED2Cu)
#define NVIC_ICTR       REG32(0xE000E004u)     /* INTLINESNUM [3:0]: (n+1)·32 줄 */
#define NVIC_ISER(n)    REG32(0xE000E100u + 4u * (n))
#define NVIC_ICER(n)    REG32(0xE000E180u + 4u * (n))
#define NVIC_ISPR(n)    REG32(0xE000E200u + 4u * (n))
#define NVIC_ICPR(n)    REG32(0xE000E280u + 4u * (n))
#define SYST_CSR        REG32(0xE000E010u)     /* ENABLE 0, TICKINT 1, CLKSOURCE 2, COUNTFLAG 16 */
#define SYST_RVR        REG32(0xE000E014u)
#define SYST_CVR        REG32(0xE000E018u)
#define UART0_DATA      REG32(0x40004000u)     /* CMSDK APB UART0 */
#define UART0_STATE     REG32(0x40004004u)
#define UART0_CTRL      REG32(0x40004008u)     /* TX_EN 0, RX_EN 1, TX_INTEN 2, RX_INTEN 3 */
#define UART0_INTSTATUS REG32(0x4000400Cu)     /* 읽기: TX 0, RX 1 / 쓰기 1 = 지움 */
#define UART0_BAUDDIV   REG32(0x40004010u)

#define ICSR_PENDSTSET  (1u << 26)
#define ICSR_PENDSTCLR  (1u << 25)
#define SYST_ENABLE     (1u << 0)
#define SYST_TICKINT    (1u << 1)
#define SYST_CLKSOURCE  (1u << 2)
#define SYST_COUNTFLAG  (1u << 16)
#define AIRCR_VECTKEY   (0x05FAu << 16)
#define AIRCR_SYSRESETREQ (1u << 2)
#define CFSR_INVSTATE   (1u << 17)
#define HFSR_FORCED     (1u << 30)
#define BOOT_IRQ        7u                     /* mps2-an385 에서 장치가 안 붙은 줄 (QEMU mps2.c 확인) — "플래시 컨트롤러 IRQ" 역할 */
#define TICK_RELOAD     2499u                  /* 25 MHz 기준 100 µs (QEMU mps2 SYSCLK 25 MHz) */

#define dsb() __asm__ volatile("dsb" ::: "memory")
#define isb() __asm__ volatile("isb" ::: "memory")

/* ---------------- 4) 세미호스팅 (BM-B08 축약판) ---------------- */
static inline int semi(int op, const void *arg) {
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
static inline void sh_puts(const char *s) { semi(0x04, s); }              /* SYS_WRITE0 */
static inline void sh_hex(uint32_t v) {
    char b[11];
    b[0] = '0'; b[1] = 'x'; b[10] = 0;
    for (int i = 9; i >= 2; --i, v >>= 4) b[i] = "0123456789abcdef"[v & 0xFu];
    sh_puts(b);
}
static inline void sh_dec(uint32_t v) {
    char b[11];
    int i = 10;
    b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    sh_puts(&b[i]);
}
__attribute__((noreturn)) static inline void sh_exit(int code) {
    uint32_t blk[2] = { 0x20026u, (uint32_t)code };                       /* SYS_EXIT_EXTENDED */
    semi(0x20, blk);
    for (;;) { }
}
__attribute__((noreturn)) static inline void system_reset(void) {         /* CMSIS NVIC_SystemReset 모양 */
    dsb();
    SCB_AIRCR = AIRCR_VECTKEY | (SCB_AIRCR & (7u << 8)) | AIRCR_SYSRESETREQ;
    dsb();
    for (;;) { __asm__ volatile("nop"); }
}
#endif
