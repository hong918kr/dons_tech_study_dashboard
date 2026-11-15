/* ENV: qemu */
/* ARM-M08 공용 런타임 — starter / solution / practice 모두에 링크된다 (TODO 없음).
 * 세미호스팅 출력·종료 + CHECK 카운터 + "예상 못 한 예외" 보고. (ARM-B14 shared_startup.c 축약판)
 * 이번 레슨은 벡터 테이블과 Reset_Handler 를 C 가 아니라 <variant>.S 의 어셈블리로 만든다.
 */
#include <stdint.h>

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
__attribute__((noreturn)) void sh_exit(int code) {                   /* Reset_Handler 가 main 뒤에 b sh_exit */
    uint32_t blk[2] = { 0x20026u, (uint32_t)code };                  /* ADP_Stopped_ApplicationExit, 종료 코드 */
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
void sh_kv(const char *k, uint32_t v) { sh_puts("  "); sh_puts(k); sh_puts(" = "); sh_hex(v); sh_puts("\n"); }

/* Default_Handler(.S) 가 r0 = IPSR 를 담아 b 로 온다. 테스트가 예상하지 않은 예외 = 즉시 종료(3). */
__attribute__((noreturn)) void unexpected_exception(uint32_t ipsr) {
    sh_puts("unexpected exception, IPSR="); sh_dec(ipsr);
    sh_puts(" CFSR="); sh_hex(*(volatile uint32_t *)0xE000ED28u);
    sh_puts(" HFSR="); sh_hex(*(volatile uint32_t *)0xE000ED2Cu);
    sh_puts("\n");
    sh_exit(3);
}
