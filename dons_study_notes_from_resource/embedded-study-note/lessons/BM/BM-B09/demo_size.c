/* ENV: qemu */
/* BM-B09 코드 크기 실험 — 같은 "x=42 y=-7 s=ok" 한 줄을 여러 방법으로 찍고 arm-none-eabi-size 로 잰다.
 * run_qemu.sh size 가 아래 조합으로 빌드하고 표를 만든다 (모두 -O2 -Wl,--gc-sections):
 *   -DRAW            printf 없이 uart_puts + 손으로 쓴 10진 변환
 *   -DPF=printf      / -DPF=iprintf (정수 전용 printf)    × --specs=nano.specs 유무 (newlib-nano vs full newlib)
 *   -DUSE_FLOAT      %f 도 찍는다 (nano 는 -u _printf_float 를 줘야 실제로 찍힌다)
 *   -DSEMIHOST       _write 를 안 쓰고 --specs=rdimon.specs (세미호스팅 syscall 라이브러리)
 *   -DNO_SYSCALLS    syscall 을 하나도 안 줌 → 링크 에러 목록 / --specs=nosys.specs 로 "링크는 되는데 조용함" (run_qemu.sh link)
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define REG32(a)     (*(volatile uint32_t *)(uintptr_t)(a))
#define UART_DATA    REG32(0x40004000u)
#define UART_STATE   REG32(0x40004004u)
#define UART_CTRL    REG32(0x40004008u)
#define UART_BAUDDIV REG32(0x40004010u)
#define SCB_AIRCR    REG32(0xE000ED0Cu)
#ifndef PF
#define PF printf
#endif
int iprintf(const char *, ...);   /* newlib 확장: -std=c11 (엄격) 에서는 <stdio.h> 가 선언을 숨긴다 */

static void uart_putc(char c) {
    for (uint32_t n = 0; (UART_STATE & 1u) && n < 100000u; n++) { }
    UART_DATA = (uint8_t)c;
}
__attribute__((unused)) static void uart_puts(const char *s) { for (; *s; s++) { if (*s == '\n') uart_putc('\r'); uart_putc(*s); } }

#if !defined(SEMIHOST) && !defined(NO_SYSCALLS)   /* ---- UART 판 최소 syscall ---- */
extern char __heap_start[], __HeapLimit[];
int _write(int fd, const char *buf, int len) {
    if (fd != 1 && fd != 2) { errno = EBADF; return -1; }
    for (int i = 0; i < len; i++) { if (buf[i] == '\n') uart_putc('\r'); uart_putc(buf[i]); }
    return len;
}
void *_sbrk(ptrdiff_t incr) {
    static char *brk = __heap_start;
    char *prev = brk;
    if (incr > __HeapLimit - brk) { errno = ENOMEM; return (void *)-1; }
    brk += incr;
    return prev;
}
int _fstat(int fd, struct stat *st) { (void)fd; memset(st, 0, sizeof *st); st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { return fd >= 0 && fd <= 2; }
int _close(int fd) { (void)fd; errno = EBADF; return -1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; errno = ESPIPE; return -1; }
int _read(int fd, char *buf, int len) { (void)fd; (void)buf; (void)len; return 0; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
__attribute__((noreturn)) void _exit(int code) {
    (void)code;
    SCB_AIRCR = 0x05FA0004u;
    for (;;) { }
}
#elif defined(NO_SYSCALLS)
__attribute__((noreturn)) void _exit(int code) { (void)code; SCB_AIRCR = 0x05FA0004u; for (;;) { } }
#else
extern void initialise_monitor_handles(void);
#endif

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
extern void __libc_init_array(void);
int main(void);
void _init(void) { }
void _fini(void) { }
void Reset_Handler(void) {
    for (uint32_t *s = _sidata, *d = _sdata; d < _edata;) *d++ = *s++;
    for (uint32_t *d = _sbss; d < _ebss;) *d++ = 0u;
#ifdef SEMIHOST
    initialise_monitor_handles();
#endif
    __libc_init_array();
    exit(main());
}
void Default_Handler(void) { for (;;) { } }
__attribute__((section(".isr_vector"), used)) const uintptr_t g_vectors[16] = {
    (uintptr_t)_estack, (uintptr_t)Reset_Handler, (uintptr_t)Default_Handler, (uintptr_t)Default_Handler,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, 0, 0, 0, 0,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, 0, (uintptr_t)Default_Handler, (uintptr_t)Default_Handler,
};

static volatile int g_x = 42, g_y = -7;
static volatile float g_f = 3.25f;

int main(void) {
    UART_BAUDDIV = 217u;
    UART_CTRL = 1u;
#ifdef RAW
    char b[12];
    int v = g_y, i = 11;
    unsigned u = (unsigned)(v < 0 ? -v : v);
    b[i] = '\0';
    do { b[--i] = (char)('0' + u % 10u); u /= 10u; } while (u);
    if (v < 0) b[--i] = '-';
    uart_puts("x=42 y=");
    uart_puts(&b[i]);
    uart_puts(" s=ok\n");
#else
    PF("x=%d y=%d s=%s\n", g_x, g_y, "ok");
#ifdef USE_FLOAT
    PF("f=%f g=%.2f\n", (double)g_f, (double)g_f);
#endif
#endif
    return 0;
}
