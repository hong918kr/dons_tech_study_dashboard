/* ENV: qemu */
/* BM-M13 demo_probe — 정책을 짜기 "전에" QEMU mps2-an385 가 실제로 어떻게 하는지 확인한다.
 *   P1 주소별로 읽기 → BusFault 가 나나? (핸들러가 fault 명령을 건너뛰고 복귀하는 "탐침" 기법)
 *   P2 저장(store)도 PRECISERR 인가?  P3 트랩 off/on 에서 UDIV/0, 비정렬 LDR/LDRH/LDRD
 *   P4 CFSR 를 지운 뒤에도 BFAR 에 옛 값이 남나?  P5 MPU 영역 수, CCR 리셋값
 *   P7 IRQ 안의 UDF: IRQ 우선순위 0 vs 0x80 → HardFault(FORCED) vs UsageFault
 *   P6 SYSRESETREQ 뒤 무엇이 남나 (.data/.bss/.noinit/RAM/FLASH 영역/레지스터) — 부팅 2 에서 보고
 * 빌드: run_qemu.sh probe (m13.ld 사용)
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
    char b[11];
    b[0] = '0'; b[1] = 'x'; b[10] = 0;
    for (int i = 9; i >= 2; --i, v >>= 4) b[i] = "0123456789abcdef"[v & 0xFu];
    sh_puts(b);
}
__attribute__((noreturn)) static void sh_exit(int code) {
    uint32_t blk[2] = { 0x20026u, (uint32_t)code };
    semi(0x20, blk);
    for (;;) { }
}

#define REG32(a)  (*(volatile uint32_t *)(uintptr_t)(a))
#define SCB_AIRCR REG32(0xE000ED0Cu)
#define SCB_CCR   REG32(0xE000ED14u)
#define SCB_SHCSR REG32(0xE000ED24u)
#define SCB_CFSR  REG32(0xE000ED28u)
#define SCB_HFSR  REG32(0xE000ED2Cu)
#define SCB_MMFAR REG32(0xE000ED34u)
#define SCB_BFAR  REG32(0xE000ED38u)
#define MPU_TYPE  REG32(0xE000ED90u)
#define LED0      REG32(0x40028000u)
#define REG8(a)   (*(volatile uint8_t *)(uintptr_t)(a))

/* ---- 탐침용 핸들러: 기록하고, CFSR/HFSR 를 W1C 로 지우고, 쌓인 PC 를 다음 명령으로 옮겨 복귀 ---- */
volatile uint32_t g_exc, g_cfsr, g_hfsr, g_r0;
__attribute__((used)) void probe_fault(uint32_t *frame) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    g_exc = ipsr; g_cfsr = SCB_CFSR; g_hfsr = SCB_HFSR; g_r0 = frame[0];
    SCB_CFSR = g_cfsr;                                    /* W1C */
    SCB_HFSR = g_hfsr;
    uint16_t hw = *(const uint16_t *)(uintptr_t)frame[6];
    frame[6] += ((hw & 0xF800u) >= 0xE800u) ? 4u : 2u;   /* 32비트 Thumb-2 명령이면 4 */
}
__attribute__((naked)) void Probe_Handler(void) {
    __asm__ volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n"
                     "push {r4, lr}\n bl probe_fault\n pop {r4, pc}\n");
}
void Reset_Handler(void);
void Irq7_Udf(void) { __asm__ volatile("udf #7"); }      /* IRQ 안에서 UsageFault 를 일으킨다 (P7) */
typedef void (*vector_t)(void);
__attribute__((section(".isr_vector"), used)) const vector_t g_vectors[16 + 8] = {
    (vector_t)_estack, Reset_Handler, Probe_Handler, Probe_Handler, Probe_Handler, Probe_Handler, Probe_Handler,
    [16 + 7] = Irq7_Udf,
};

static void clr(void) { g_exc = 0; g_cfsr = 0; g_hfsr = 0; g_r0 = 0; }
static void row(const char *what, uint32_t val) {
    sh_puts(what); sh_puts(" exc="); sh_hex(g_exc); sh_puts(" CFSR="); sh_hex(g_cfsr);
    sh_puts(" HFSR="); sh_hex(g_hfsr); sh_puts(" BFAR="); sh_hex(SCB_BFAR); sh_puts(" val="); sh_hex(val); sh_puts("\n");
}
__attribute__((noinline)) static uint32_t ld(uint32_t a)  { uint32_t v = 0xEEEEEEEEu; __asm__ volatile("ldr %0, [%1]"  : "+r"(v) : "r"(a) : "memory"); return v; }
__attribute__((noinline)) static uint32_t ldh(uint32_t a) { uint32_t v = 0xEEEEEEEEu; __asm__ volatile("ldrh %0, [%1]" : "+r"(v) : "r"(a) : "memory"); return v; }
__attribute__((noinline)) static uint32_t ldd(uint32_t a) { uint32_t x = 0xEEEEEEEEu, y = 0; __asm__ volatile("ldrd %0, %1, [%2]" : "+r"(x), "+r"(y) : "r"(a) : "memory"); return x; }
__attribute__((noinline)) static void st(uint32_t a, uint32_t v) { __asm__ volatile("str %1, [%0]" :: "r"(a), "r"(v) : "memory"); }
__attribute__((noinline)) static uint32_t dv(uint32_t a, uint32_t b) { uint32_t q = 0xEEEEEEEEu; __asm__ volatile("udiv %0, %1, %2" : "+r"(q) : "r"(a), "r"(b)); return q; }

/* ---- P6 용 표식들 ---- */
uint32_t g_data = 0x11111111u;                            /* .data */
uint32_t g_bss;                                           /* .bss  */
__attribute__((section(".noinit"))) uint32_t g_noinit[3]; /* [0] 부팅 표식, [1] 리셋 요청 뒤 루프 횟수 */
const uint32_t g_rodata = 0x0DA7A000u;                    /* .rodata = "FLASH" (QEMU 에선 RAM) */
#define RAW_RAM (*(volatile uint32_t *)0x2000F000u)       /* 어느 섹션에도 속하지 않는 RAM */
#define BOOT2_MARK 0xB007CAFEu
static uint32_t s_words[4] = { 0x33221100u, 0x77665544u, 0xBBAA9988u, 0xFFEEDDCCu };

static int boot1(void) {
    static const struct { uint32_t a; const char *what; } k[] = {
        { 0x30000000u, "0x30000000 (mtree: 아무것도 없음)  " }, { 0x60000000u, "0x60000000 (mtree: 아무것도 없음)  " },
        { 0x41200000u, "0x41200000 (VGA 뒤, 없음)          " }, { 0xE0100000u, "0xE0100000 (PPB 뒤, 없음)          " },
        { 0x00800000u, "0x00800000 (RESERVED 1, unimp dev) " }, { 0x40030000u, "0x40030000 (RESERVED 4, unimp dev) " },
        { 0x40003000u, "0x40003000 (APB 빈 슬롯, unimp dev)" }, { 0x20400000u, "0x20400000 (SSRAM2&3 미러)         " },
    };
    sh_puts("P1 load32 with BUSFAULTENA=1 (handler skips the faulting LDR):\n");
    SCB_SHCSR |= 7u << 16;
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; ++i) { clr(); uint32_t v = ld(k[i].a); row(k[i].what, v); }
    sh_puts("P2 store32 to 0x30000000:\n");
    clr(); st(0x30000000u, 1u); row("  str                               ", 0);
    sh_puts("P3 traps OFF (CCR=");  sh_hex(SCB_CCR); sh_puts("):\n");
    uint32_t odd = (uint32_t)(uintptr_t)s_words + 1u;
    clr(); row("  udiv 7/0                          ", dv(7u, 0u));
    clr(); row("  ldr  [odd]                        ", ld(odd));
    clr(); row("  ldrh [odd]                        ", ldh(odd));
    clr(); row("  ldrd [odd]                        ", ldd(odd));
    SCB_CCR |= (1u << 4) | (1u << 3);
    __asm__ volatile("dsb\n isb" ::: "memory");
    sh_puts("   traps ON (CCR="); sh_hex(SCB_CCR); sh_puts("):\n");
    clr(); row("  udiv 7/0                          ", dv(7u, 0u));
    sh_puts("     stacked r0 for that udiv = "); sh_hex(g_r0); sh_puts(" (the mov before it loaded 0xeeeeeeee)\n");
    clr(); row("  ldr  [odd]                        ", ld(odd));
    clr(); row("  ldr  [+2]                         ", ld(odd + 1u));
    clr(); row("  ldrh [odd]                        ", ldh(odd));
    sh_puts("P4 after W1C clear: CFSR="); sh_hex(SCB_CFSR); sh_puts(" BFAR="); sh_hex(SCB_BFAR);
    sh_puts(" MMFAR="); sh_hex(SCB_MMFAR); sh_puts("\n");
    sh_puts("P5 MPU_TYPE="); sh_hex(MPU_TYPE); sh_puts(" (DREGION=bits[15:8])\n");
    sh_puts("P7 UDF inside IRQ7, USGFAULTENA=1, UsageFault priority 0 (SHPR1 reset):\n");
    for (uint32_t prio = 0; prio <= 0x80u; prio += 0x80u) {
        REG8(0xE000E407u) = (uint8_t)prio;                /* NVIC_IPR1 byte 3 = IRQ7 */
        clr();
        REG32(0xE000E100u) = 1u << 7;                     /* ISER0 */
        REG32(0xE000E200u) = 1u << 7;                     /* ISPR0 → 바로 IRQ7 */
        __asm__ volatile("dsb\n isb" ::: "memory");
        sh_puts(prio ? "  IRQ7 prio 0x80 (IPR read back " : "  IRQ7 prio 0x00 (IPR read back ");
        sh_hex(REG8(0xE000E407u)); row(")", 0);
    }

    sh_puts("P6 boot 1: write markers, LED0=3, then SYSRESETREQ\n");
    g_noinit[0] = BOOT2_MARK; g_noinit[1] = 0u;
    g_data = 0x22222222u; g_bss = 0x33333333u; RAW_RAM = 0x44444444u;
    *(volatile uint32_t *)(uintptr_t)&g_rodata = 0x55555555u;     /* QEMU 에선 쓰기가 된다 (진짜 플래시는 아님) */
    sh_puts("   .rodata(FLASH) after write = "); sh_hex(*(volatile const uint32_t *)&g_rodata); sh_puts("\n");
    LED0 = 0xFFu;
    sh_puts("   LED0 after writing 0xff = "); sh_hex(LED0); sh_puts("\n");
    LED0 = 3u;
    __asm__ volatile("dsb" ::: "memory");
    SCB_AIRCR = (0x05FAu << 16) | (SCB_AIRCR & 0x700u) | (1u << 2);
    __asm__ volatile("dsb" ::: "memory");
    for (uint32_t i = 0; i < 50000000u; ++i) ((volatile uint32_t *)g_noinit)[1] = i + 1u;
    sh_puts("   still running after SYSRESETREQ?!\n");
    return 5;
}

__attribute__((noreturn)) void Reset_Handler(void) {
    volatile uint32_t *n = g_noinit;
    if (n[0] == BOOT2_MARK) {                             /* 부팅 2: reset_init 전에 값을 본다 */
        sh_puts("P6 boot 2 (before .data/.bss init):\n   .data="); sh_hex(*(volatile uint32_t *)&g_data);
        sh_puts(" .bss="); sh_hex(*(volatile uint32_t *)&g_bss);
        sh_puts(" .noinit="); sh_hex(n[0]);
        sh_puts(" raw RAM="); sh_hex(RAW_RAM);
        sh_puts("\n   .rodata(FLASH)="); sh_hex(*(volatile const uint32_t *)&g_rodata);
        sh_puts(" loop iterations after SYSRESETREQ="); sh_hex(n[1]);
        sh_puts("\n   CFSR="); sh_hex(SCB_CFSR); sh_puts(" SHCSR="); sh_hex(SCB_SHCSR); sh_puts(" CCR="); sh_hex(SCB_CCR);
        sh_puts(" LED0="); sh_hex(LED0); sh_puts(" AIRCR="); sh_hex(SCB_AIRCR); sh_puts("\n");
        n[0] = 0u;
        sh_exit(0);
    }
    for (uint32_t *s = _sidata, *p = _sdata; p < _edata;) *p++ = *s++;
    for (uint32_t *p = _sbss; p < _ebss;) *p++ = 0u;
    sh_exit(boot1());
}
