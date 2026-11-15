/* ENV: qemu */
/* ARM-B13 공용 startup — qemu-system-arm -M mps2-an385 (Cortex-M3).
 * BM-B08 레시피의 압축 사본 (레슨마다 자기완결): 벡터 테이블 + Reset_Handler + 세미호스팅 출력/종료
 * + HardFault 보고. 모든 변형(starter/solution/practice/demo_bugs)에 같이 링크된다 (shared_*).
 * main() 은 harness.c 또는 practice.c 가 제공하고, 그 반환값이 QEMU 종료 코드가 된다.
 */
#include <stdint.h>

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
int main(void);

/* ---------------- 세미호스팅: bkpt 0xab → QEMU 가 호스트에서 처리 ---------------- */
static int semi(int op, const void *arg) {
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
void sh_puts(const char *s) { semi(0x04, s); }                       /* SYS_WRITE0 */
void sh_hex(uint32_t v) {                                            /* "0x%08x" */
    char b[11];
    b[0] = '0'; b[1] = 'x'; b[10] = 0;
    for (int i = 9; i >= 2; --i, v >>= 4) b[i] = "0123456789abcdef"[v & 0xFu];
    sh_puts(b);
}
void sh_dec(int32_t sv) {                                            /* 부호 있는 10진수 */
    char b[12];
    int i = 11;
    uint32_t v = sv < 0 ? 0u - (uint32_t)sv : (uint32_t)sv;          /* INT32_MIN 도 안전 */
    b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    if (sv < 0) b[--i] = '-';
    sh_puts(&b[i]);
}
__attribute__((noreturn)) void sh_exit(int code) {
    uint32_t blk[2] = { 0x20026u, (uint32_t)code };                  /* ADP_Stopped_ApplicationExit + 코드 */
    semi(0x20, blk);                                                 /* SYS_EXIT_EXTENDED → qemu exit = code */
    semi(0x18, (const void *)(uintptr_t)(code ? 0x20024u : 0x20026u)); /* 구형 호스트용 SYS_EXIT */
    for (;;) { }
}

/* ---------------- 리셋: .data 복사, .bss 0, main ---------------- */
__attribute__((noreturn)) void Reset_Handler(void) {
    const uint32_t *src = _sidata;
    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;
    for (uint32_t *dst = _sbss; dst < _ebss;) *dst++ = 0u;
    sh_exit(main());
}

/* ---------------- 예외: 주인 없는 예외 / HardFault 보고 ---------------- */
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
void Default_Handler(void) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    sh_puts("Default_Handler: IPSR="); sh_dec((int32_t)ipsr); sh_puts("\n");
    sh_exit(2);
}
/* 예외 프레임: r0 r1 r2 r3 r12 lr pc xpsr (하드웨어 자동 push). 해독은 ARM-M09/M12 몫 — 여기선 출력만 */
__attribute__((used, noreturn)) void hardfault_report(const uint32_t *frame) {
    sh_puts("HardFault! CFSR="); sh_hex(REG32(0xE000ED28u));
    sh_puts(" HFSR=");           sh_hex(REG32(0xE000ED2Cu));
    sh_puts(" BFAR=");           sh_hex(REG32(0xE000ED38u));    /* CFSR bit15 BFARVALID 일 때만 의미 */
    sh_puts("\n ");
    sh_puts(" stacked pc=");     sh_hex(frame[6]);
    sh_puts(" lr=");             sh_hex(frame[5]);
    sh_puts(" xpsr=");           sh_hex(frame[7]);
    sh_puts("\n");
    sh_exit(3);
}
__attribute__((naked)) void HardFault_Handler(void) {
    __asm__ volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n b hardfault_report\n");
}

typedef void (*vector_t)(void);
#define D Default_Handler
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16] = {
    (vector_t)_estack,  Reset_Handler, D /* NMI */, HardFault_Handler,
    D, D, D, 0, 0, 0, 0, D /* SVC */, D, 0, D /* PendSV */, D /* SysTick */,
};   /* 외부 IRQ 는 이 레슨에서 켜지 않는다 (벡터 테이블 자세히 = ARM-M08) */
