/* ENV: qemu */
/* ARM-A07 M4 레인 스타트업 — 벡터 테이블(C 배열) + Reset_Handler + 세미호스팅 + 테스트 카운터.
 * (ARM-M09 shared_startup.c 축약판 — FPU 는 안 쓴다: -mfloat-abi=soft)
 */
#include <stdint.h>

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
int main(void);

static int semi(int op, const void *arg) {                 /* BKPT 0xAB → QEMU 가 호스트에서 처리 */
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
void sh_puts(const char *s) { semi(0x04, s); }             /* SYS_WRITE0 */
void sh_hex(uint32_t v) {
    char b[11];
    b[0] = '0'; b[1] = 'x'; b[10] = 0;
    for (int i = 9; i >= 2; --i, v >>= 4) b[i] = "0123456789abcdef"[v & 0xFu];
    sh_puts(b);
}
void sh_dec(int32_t sv) {
    char b[12];
    int i = 11;
    uint32_t v = sv < 0 ? 0u - (uint32_t)sv : (uint32_t)sv;
    b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    if (sv < 0) b[--i] = '-';
    sh_puts(&b[i]);
}
__attribute__((noreturn)) void sh_exit(int code) {
    uint32_t blk[2] = { 0x20026u, (uint32_t)code };         /* ADP_Stopped_ApplicationExit, 종료 코드 */
    semi(0x20, blk);                                        /* SYS_EXIT_EXTENDED → qemu exit = code */
    for (;;) { }
}

unsigned g_pass, g_total;
void check(int ok, const char *expr, int line) {
    g_total++;
    if (ok) g_pass++;
    sh_puts(ok ? "PASS " : "FAIL ");
    sh_puts("line "); sh_dec(line); sh_puts(": "); sh_puts(expr); sh_puts("\n");
}

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
void Default_Handler(void) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    sh_puts("unexpected exception, IPSR="); sh_dec((int32_t)ipsr);
    sh_puts(" CFSR="); sh_hex(REG32(0xE000ED28u)); sh_puts("\n");
    sh_exit(3);
}
#define WEAK_DEFAULT __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK_DEFAULT;
void HardFault_Handler(void) WEAK_DEFAULT;      /* m3probe 는 강한 정의로 덮어쓴다 */
void MemManage_Handler(void) WEAK_DEFAULT;
void BusFault_Handler(void) WEAK_DEFAULT;
void UsageFault_Handler(void) WEAK_DEFAULT;
void SVC_Handler(void) WEAK_DEFAULT;
void PendSV_Handler(void) WEAK_DEFAULT;
void SysTick_Handler(void) WEAK_DEFAULT;
__attribute__((noreturn)) void Reset_Handler(void);

typedef void (*vector_t)(void);
__attribute__((section(".vectors"), used))
const vector_t g_vectors[16] = {
    (vector_t)_estack, Reset_Handler, NMI_Handler, HardFault_Handler,
    MemManage_Handler, BusFault_Handler, UsageFault_Handler, 0, 0, 0, 0,
    SVC_Handler, 0, 0, PendSV_Handler, SysTick_Handler,
};

__attribute__((noreturn)) void Reset_Handler(void) {
    for (uint32_t *s = _sidata, *d = _sdata; d < _edata; ) *d++ = *s++;
    for (uint32_t *d = _sbss; d < _ebss; ) *d++ = 0u;
    sh_exit(main());                                    /* main 의 반환값 = QEMU 종료 코드 */
}
