/* ENV: qemu */
/* BM-B09 추가 연습 — 한 이미지로 4개 연습을 검증한다 (qemu mps2-an385, Cortex-M3, newlib-nano)
 *   연습 1: fd 2(stderr) → 직접 쓴 세미호스팅 SYS_WRITE0 (rdimon 없이, 32바이트 청크 + NUL)
 *   연습 2: 세미호스팅-안전 HardFault: BKPT 0xAB 에서 죽으면 r0=-1, PC+=2 로 건너뛰고 stderr 를 UART 로 돌린다
 *   연습 3: setvbuf 로 stdout/stdin 에 정적 버퍼 → printf 가 힙을 1바이트도 안 쓴다
 *   연습 4: _read — UART RX(RXFULL 폴링, 에코, '\n' 에서 반환) → fgets 로 명령 한 줄 읽기
 * run_qemu.sh practice 가 두 번 돌린다: 세미호스팅 켬 / 끔(디버거 없는 보드 흉내). 둘 다 PASS 여야 한다.
 */
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---------- CMSDK APB UART (QEMU hw/char/cmsdk-apb-uart.c v10.2.0 에서 확인) ---------- */
#define REG32(a)          (*(volatile uint32_t *)(uintptr_t)(a))
#define UART0_BASE        0x40004000u        /* mps2-an385 UART0 = QEMU 의 첫 번째 -serial */
#define UART_DATA(b)      REG32((b) + 0x00u) /* 쓰기 = TX 1바이트, 읽기 = RX 1바이트       */
#define UART_STATE(b)     REG32((b) + 0x04u) /* bit0 TXFULL(r) bit1 RXFULL(r) bit2/3 OVR(W1C) */
#define UART_CTRL(b)      REG32((b) + 0x08u) /* bit0 TX_EN bit1 RX_EN bit2~5 IRQ enable     */
#define UART_BAUDDIV(b)   REG32((b) + 0x10u) /* 20비트, 최소 16. baud = PCLK / BAUDDIV      */
#define UART_STATE_TXFULL (1u << 0)
#define UART_STATE_RXFULL (1u << 1)
#define UART_CTRL_TX_EN   (1u << 0)
#define UART_CTRL_RX_EN   (1u << 1)
#define SCB_AIRCR         REG32(0xE000ED0Cu)
#define SCB_CFSR          REG32(0xE000ED28u)
#define SCB_HFSR          REG32(0xE000ED2Cu)

#define SYSCLK_HZ    25000000u   /* mps2.c SYSCLK_FRQ — UART 의 PCLK */
#define CONSOLE_BAUD 115200u
#define UART_TIMEOUT 100000u     /* TXFULL 폴링 최대 횟수: 고장 난 UART 에서 무한 대기 금지 */
#define RX_TIMEOUT   20000000u   /* RX 첫 바이트 대기 한도 (QEMU 의 입력 스레드가 늦게 넣어 줄 수 있어 넉넉히) */
#define CHAR_EOT     0x04        /* Ctrl-D: 입력 끝 (터미널 관례) */

static uint32_t g_console = UART0_BASE;   /* _write 가 쓰는 UART */
static volatile uint32_t g_uart_timeouts; /* uart_putc 타임아웃 횟수 */
static volatile uint32_t g_write_calls;   /* newlib 이 _write 를 부른 횟수 (버퍼링 관찰용) */
extern char __heap_start[], __HeapLimit[];  /* mps2_an385.ld */

/* ======================= 본문 UART 드라이버 (solution.c 와 같음) ======================= */

/* Part A-1: BAUDDIV = round(pclk / baud). 결과가 16 미만이거나 20비트(0xFFFFF) 초과, baud == 0 이면
 * 레지스터를 건드리지 않고 -1. 성공하면 BAUDDIV 를 먼저 쓰고 CTRL = TX_EN | RX_EN, 0 반환. */
int uart_init(uint32_t base, uint32_t pclk, uint32_t baud) {
    if (baud == 0u) return -1;
    uint32_t div = pclk / baud;
    if (pclk % baud >= (baud + 1u) / 2u) div++;           /* 반올림 (오버플로 없는 꼴) */
    if (div < 16u || div > 0xFFFFFu) return -1;
    UART_BAUDDIV(base) = div;
    UART_CTRL(base) = UART_CTRL_TX_EN | UART_CTRL_RX_EN;
    return 0;
}

/* Part A-2: TXFULL 이 0 이 될 때까지 최대 UART_TIMEOUT 번 폴링한 뒤 DATA 에 1바이트.
 * 타임아웃이면 g_uart_timeouts++ 하고 -1 (DATA 는 쓰지 않음). */
int uart_putc(uint32_t base, char c) {
    uint32_t n = 0;
    while (UART_STATE(base) & UART_STATE_TXFULL) {
        if (++n >= UART_TIMEOUT) { g_uart_timeouts++; return -1; }
    }
    UART_DATA(base) = (uint8_t)c;
    return 0;
}

/* ---------- 연습 1·2: 세미호스팅 (rdimon 없이 직접) ---------- */
static volatile int g_semihost_alive = 1;      /* 연습 2 의 HardFault 가 0 으로 내린다 */
static volatile uint32_t g_bkpt_skipped;

static int semihost_call(int op, const void *arg) {          /* ARM semihosting: r0 = op, r1 = arg, BKPT 0xAB */
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
/* SYS_WRITE0(0x04): NUL 로 끝나는 문자열 1개. 버퍼에 NUL 이 있을 수 있으니 빼고, 32바이트씩 끊어 보낸다 */
static int semihost_write0(const char *buf, int len) {
    char chunk[33];
    int i = 0;
    while (i < len) {
        int n = 0;
        for (; i < len && n < 32; i++)
            if (buf[i] != '\0') chunk[n++] = buf[i];
        chunk[n] = '\0';
        (void)semihost_call(0x04, chunk);
        if (!g_semihost_alive) return -1;                  /* 첫 BKPT 가 HardFault 로 → 건너뛰어짐 */
    }
    return len;
}
static int uart_write_buf(const char *buf, int len) {
    for (int i = 0; i < len; i++) {
        if ((buf[i] == '\n' && uart_putc(g_console, '\r') != 0) || uart_putc(g_console, buf[i]) != 0) {
            if (i > 0) return i;
            errno = EIO;
            return -1;
        }
    }
    return len;
}
int _write(int fd, const char *buf, int len) {
    static int err_line_start = 1;                          /* 폴백 표시는 줄 처음에만 */
    g_write_calls++;
    if (fd == 1) return uart_write_buf(buf, len);
    if (fd != 2) { errno = EBADF; return -1; }
    if (g_semihost_alive && semihost_write0(buf, len) == len) return len;
    for (int i = 0; i < len; i++) {                         /* 폴백: 디버거가 없으면 UART 로, 줄마다 표시 */
        if (buf[i] == '\0') continue;                       /* 콘솔은 텍스트: NUL 은 두 경로 모두 버린다 */
        if (err_line_start && uart_write_buf("[stderr] ", 9) != 9) return -1;
        if (uart_write_buf(&buf[i], 1) != 1) return -1;
        err_line_start = (buf[i] == '\n');
    }
    return len;
}

/* ---------- 연습 4: _read — UART RX 폴링 ---------- */
int _read(int fd, char *buf, int len) {
    if (fd != 0) { errno = EBADF; return -1; }
    int n = 0;
    while (n < len) {
        uint32_t t = 0;
        while (!(UART_STATE(UART0_BASE) & UART_STATE_RXFULL))
            if (++t >= RX_TIMEOUT) return n;                   /* 더 안 오면 지금까지 (0 = EOF) */
        char c = (char)UART_DATA(UART0_BASE);                  /* DATA 를 읽으면 RXFULL 이 내려간다 */
        if (c == CHAR_EOT) return n;                           /* 줄 처음의 Ctrl-D → 0 → newlib 이 EOF 로 */
        if (c == '\r') c = '\n';
        buf[n++] = c;
        if (c == '\n') { (void)uart_putc(UART0_BASE, '\r'); (void)uart_putc(UART0_BASE, '\n'); break; }
        (void)uart_putc(UART0_BASE, c);                        /* 에코 (터미널처럼) */
    }
    return n;
}

void *_sbrk(ptrdiff_t incr) {
    static char *brk = __heap_start;
    char *prev = brk;
    if ((incr > 0 && incr > __HeapLimit - brk) || (incr < 0 && -incr > brk - __heap_start)) {
        errno = ENOMEM;
        return (void *)-1;
    }
    brk += incr;
    return prev;
}
int _fstat(int fd, struct stat *st) {
    if (fd < 0 || fd > 2) { errno = EBADF; return -1; }
    memset(st, 0, sizeof *st);
    st->st_mode = S_IFCHR;
    return 0;
}
int _isatty(int fd) {
    if (fd >= 0 && fd <= 2) return 1;
    errno = EBADF;
    return 0;
}
int _close(int fd) { (void)fd; errno = EBADF; return -1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; errno = ESPIPE; return -1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }

static void raw_puts(const char *s) {
    for (; *s; s++) {
        if (*s == '\n') (void)uart_putc(UART0_BASE, '\r');
        (void)uart_putc(UART0_BASE, *s);
    }
}
static void raw_puthex(uint32_t v) {
    char b[11] = "0x";
    for (int i = 0; i < 8; i++) b[2 + i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 0xFu];
    b[10] = '\0';
    raw_puts(b);
}
__attribute__((noreturn)) void _exit(int code) {
    raw_puts("[exit ");
    raw_puthex((uint32_t)code);
    raw_puts("]\n");
    SCB_AIRCR = 0x05FA0004u;
    for (;;) { }
}

/* ---------- 연습 2: 세미호스팅-안전 HardFault ---------- */
extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
extern void __libc_init_array(void);
int main(void);
void _init(void) { }
void _fini(void) { }
void Reset_Handler(void) {
    for (uint32_t *s = _sidata, *d = _sdata; d < _edata;) *d++ = *s++;
    for (uint32_t *d = _sbss; d < _ebss;) *d++ = 0u;
    __libc_init_array();
    exit(main());
}
void hardfault_c(uint32_t *frame) {     /* frame = r0 r1 r2 r3 r12 lr pc xpsr */
    uint32_t pc = frame[6];
    if (*(const volatile uint16_t *)(uintptr_t)pc == 0xBEABu) { /* BKPT 0xAB: 세미호스팅 호출이 응답 없이 폴트 */
        g_semihost_alive = 0;
        g_bkpt_skipped++;
        frame[0] = (uint32_t)-1;          /* 호출 결과 = -1 (실패) */
        frame[6] = pc + 2u;               /* 16비트 BKPT 다음 명령으로 */
        SCB_HFSR = SCB_HFSR;              /* HFSR 은 W1C: 읽은 값을 다시 써서 FORCED/DEBUGEVT 지움 */
        return;                           /* EXC_RETURN 으로 복귀 → 스레드 코드가 계속 돈다 */
    }
    (void)uart_init(UART0_BASE, SYSCLK_HZ, CONSOLE_BAUD);
    raw_puts("\n!! HardFault HFSR=");
    raw_puthex(SCB_HFSR);
    raw_puts(" CFSR=");
    raw_puthex(SCB_CFSR);
    raw_puts(" PC=");
    raw_puthex(pc);
    raw_puts("\n");
    _exit(0xDEAD);
}
__attribute__((naked)) void HardFault_Handler(void) {
    __asm__ volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n b hardfault_c\n");
}
void Default_Handler(void) { raw_puts("\n!! unexpected exception\n"); _exit(0xBAD); }
__attribute__((section(".isr_vector"), used)) const uintptr_t g_vectors[16] = {
    (uintptr_t)_estack, (uintptr_t)Reset_Handler, (uintptr_t)Default_Handler, (uintptr_t)HardFault_Handler,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, 0, 0, 0, 0,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, 0, (uintptr_t)Default_Handler, (uintptr_t)Default_Handler,
};

/* ======================= 테스트 ======================= */
static unsigned g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) g_pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

static char g_stdout_buf[128];          /* 연습 3: 힙 1032 B 대신 정적 128 B */
static char g_stdin_buf[64];

int main(void) {
    CHECK(uart_init(UART0_BASE, SYSCLK_HZ, CONSOLE_BAUD) == 0);
    /* 연습 3: 첫 입출력 '전에' setvbuf */
    CHECK(setvbuf(stdout, g_stdout_buf, _IOLBF, sizeof g_stdout_buf) == 0);
    CHECK(setvbuf(stdin, g_stdin_buf, _IOLBF, sizeof g_stdin_buf) == 0);
    printf("practice: stdout -> UART0, stderr -> semihosting (or UART fallback)\n");
    uint32_t w = g_write_calls;
    printf("line-buffered in a static %u-byte buffer\n", (unsigned)sizeof g_stdout_buf);
    CHECK(g_write_calls - w == 1u);

    /* 연습 1·2: stderr */
    int n = fprintf(stderr, "E: sensor timeout on bus %d (this line is longer than one 32-byte chunk)\n", 2);
    CHECK(n == 72);
    CHECK(_write(2, "nul\0skipped\n", 12) == 12);           /* SYS_WRITE0 는 NUL 을 못 실어 나른다 → 빼고 보냄 */
    printf("[info] semihosting %s, BKPT skipped %u time(s)\n",
           g_semihost_alive ? "answered by host" : "absent -> stderr fell back to UART", (unsigned)g_bkpt_skipped);
    CHECK(g_semihost_alive ? g_bkpt_skipped == 0u : g_bkpt_skipped == 1u);

    /* 연습 4: 명령 두 줄 + EOF */
    char line[32];
    printf("> ");
    fflush(stdout);
    CHECK(fgets(line, sizeof line, stdin) != NULL && strcmp(line, "led on\n") == 0);
    printf("> ");
    fflush(stdout);
    CHECK(fgets(line, sizeof line, stdin) != NULL && strtol(line, NULL, 0) == 0x2A);
    CHECK(fgets(line, sizeof line, stdin) == NULL && feof(stdin));

    /* 연습 3 확인: 지금까지 malloc 이 한 번도 안 불렸다 */
    CHECK((char *)_sbrk(0) == __heap_start);
    printf("[info] heap used by stdio = %u bytes\n", (unsigned)((char *)_sbrk(0) - __heap_start));
    printf("%u/%u passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
