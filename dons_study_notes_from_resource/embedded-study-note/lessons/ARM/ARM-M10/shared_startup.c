/* ENV: qemu */
/* ARM-M10 공용 스타트업 — starter / solution / practice 모두에 링크된다 (TODO 없음).
 * ARM-B14 shared_startup.c 축약판 + 외부 IRQ 0..31 벡터(QEMU mps2-an385 num-irq = 32).
 * 벡터 테이블(C 배열) + Reset_Handler(.data 복사, .bss 0) + 세미호스팅 출력·종료 + CHECK 카운터.
 * 테스트 파일이 강한 정의로 덮어쓰는 핸들러: NMI, HardFault(shared_rmw.S), SysTick, IRQ6, IRQ7.
 */
#include <stdint.h>

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
int main(void);

/* ---------------- 세미호스팅 (BKPT 0xAB → QEMU 가 호스트에서 처리) ---------------- */
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
void sh_dec(uint32_t v) {
    char b[11];
    int i = 10;
    b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    sh_puts(&b[i]);
}
__attribute__((noreturn)) void sh_exit(int code) {
    uint32_t blk[2] = { 0x20026u, (uint32_t)code };                  /* ADP_Stopped_ApplicationExit */
    semi(0x20, blk);                                                 /* SYS_EXIT_EXTENDED → qemu exit = code */
    for (;;) { }
}

/* ---------------- 테스트 카운터 (CHECK 매크로는 각 테스트 파일에) ---------------- */
unsigned g_pass, g_total;
void check(int ok, const char *expr, int line) {
    g_total++;
    if (ok) g_pass++;
    sh_puts(ok ? "PASS " : "FAIL ");
    sh_puts("line "); sh_dec((uint32_t)line); sh_puts(": "); sh_puts(expr); sh_puts("\n");
}

/* ---------------- 기본 핸들러 ---------------- */
void Default_Handler(void) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    sh_puts("unexpected exception, IPSR="); sh_dec(ipsr); sh_puts("\n");
    sh_exit(3);
}
#define WEAK_DEFAULT __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK_DEFAULT;
void HardFault_Handler(void) WEAK_DEFAULT;
void MemManage_Handler(void) WEAK_DEFAULT;
void BusFault_Handler(void) WEAK_DEFAULT;
void UsageFault_Handler(void) WEAK_DEFAULT;
void SVC_Handler(void) WEAK_DEFAULT;
void DebugMon_Handler(void) WEAK_DEFAULT;
void PendSV_Handler(void) WEAK_DEFAULT;
void SysTick_Handler(void) WEAK_DEFAULT;
void IRQ6_Handler(void) WEAK_DEFAULT;
void IRQ7_Handler(void) WEAK_DEFAULT;
__attribute__((noreturn)) void Reset_Handler(void);

typedef void (*vector_t)(void);
__attribute__((section(".vectors"), used))
const vector_t g_vectors[16 + 32] = {
    (vector_t)_estack,      /* [0]  초기 MSP 값 (코드 주소 아님) */
    Reset_Handler,          /* [1] */
    NMI_Handler,            /* [2]  우선순위 -2 고정 */
    HardFault_Handler,      /* [3]  우선순위 -1 고정 */
    MemManage_Handler, BusFault_Handler, UsageFault_Handler,     /* [4..6] */
    0, 0, 0, 0,             /* [7..10] reserved */
    SVC_Handler, DebugMon_Handler, 0, PendSV_Handler,            /* [11..14] */
    SysTick_Handler,        /* [15] */
#define D Default_Handler
    D, D, D, D, D, D,       /* IRQ0..5   (예외 16..21) */
    IRQ6_Handler,           /* IRQ6      (예외 22) — 장치가 안 붙은 줄 (BM-M02 확인) */
    IRQ7_Handler,           /* IRQ7      (예외 23) */
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D,              /* IRQ8..23 */
    D, D, D, D, D, D, D, D,                                      /* IRQ24..31 */
#undef D
};

__attribute__((noreturn)) void Reset_Handler(void) {
    for (uint32_t *s = _sidata, *d = _sdata; d < _edata; ) *d++ = *s++;
    for (uint32_t *d = _sbss; d < _ebss; ) *d++ = 0u;
    sh_exit(main());        /* main 의 반환값 = QEMU 종료 코드 */
}
