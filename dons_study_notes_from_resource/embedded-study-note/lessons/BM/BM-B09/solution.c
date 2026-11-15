/* ENV: qemu */
/* BM-B09 printf 리타겟팅 — newlib-nano printf → _write → CMSDK APB UART0 (qemu mps2-an385, Cortex-M3)
 *
 * 빌드·실행·출력 비교는 run_qemu.sh 가 한다 (Lima VM `elinux` 안):
 *   arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror \
 *       --specs=nano.specs -nostartfiles -T mps2_an385.ld solution.c -o fw.elf
 *   qemu-system-arm -M mps2-an385 -nographic -monitor none -no-reboot -serial file:uart0.txt -kernel fw.elf
 * 펌웨어의 자체 검사 결과도 printf 로 나간다 → _write 가 비어 있으면 화면에 아무것도 안 나오고,
 * run_qemu.sh 가 캡처한 UART 출력을 expected_solution.txt 와 비교해 FAIL 을 낸다.
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
#define UART_CTRL_TX_EN   (1u << 0)
#define UART_CTRL_RX_EN   (1u << 1)
#define SCB_AIRCR         REG32(0xE000ED0Cu)
#define SCB_CFSR          REG32(0xE000ED28u)
#define SCB_HFSR          REG32(0xE000ED2Cu)

#define SYSCLK_HZ    25000000u   /* mps2.c SYSCLK_FRQ — UART 의 PCLK */
#define CONSOLE_BAUD 115200u
#define UART_TIMEOUT 100000u     /* TXFULL 폴링 최대 횟수: 고장 난 UART 에서 무한 대기 금지 */

static uint32_t g_console = UART0_BASE;   /* _write 가 쓰는 UART. 테스트가 가짜 UART 로 바꿔 끼운다 */
static volatile uint32_t g_uart_timeouts; /* uart_putc 타임아웃 횟수 */
static volatile uint32_t g_write_calls;   /* newlib 이 _write 를 부른 횟수 (버퍼링 관찰용) */
extern char __heap_start[], __HeapLimit[];  /* mps2_an385.ld */

/* ======================= TODO 영역 ======================= */

/* Part A-1: BAUDDIV = round(pclk / baud). 결과가 16 미만이거나 20비트(0xFFFFF) 초과, baud == 0 이면
 * 레지스터를 건드리지 않고 -1. 성공하면 BAUDDIV 를 먼저 쓰고 CTRL = TX_EN | RX_EN, 0 반환. */
int uart_init(uint32_t base, uint32_t pclk, uint32_t baud) {
    if (baud == 0u) return -1;
    uint32_t div = pclk / baud;
    if (pclk % baud >= (baud + 1u) / 2u) div++;           /* 반올림 (오버플로 없는 꼴) */
    if (div < 16u || div > 0xFFFFFu) return -1;
    UART_BAUDDIV(base) = div;
#ifndef BUG_NO_TXEN
    UART_CTRL(base) = UART_CTRL_TX_EN | UART_CTRL_RX_EN;
#endif
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

/* Part A-3: newlib 이 stdout/stderr 버퍼를 비울 때 부르는 syscall.
 * fd 1,2 만 받는다 (그 외 errno=EBADF, -1). '\n' 앞에 '\r' 을 넣는다 (터미널용 CRLF).
 * 반환 = 소비한 바이트 수 (len). UART 가 중간에 멈추면 그때까지 보낸 바이트 수, 하나도 못 보냈으면 errno=EIO, -1. */
int _write(int fd, const char *buf, int len) {
    g_write_calls++;
    if (fd != 1 && fd != 2) { errno = EBADF; return -1; }
    for (int i = 0; i < len; i++) {
        if ((buf[i] == '\n' && uart_putc(g_console, '\r') != 0) || uart_putc(g_console, buf[i]) != 0) {
            if (i > 0) return i;
            errno = EIO;
            return -1;
        }
    }
    return len;
}

/* Part B-1: 힙 = [__heap_start, __HeapLimit). 이전 break 를 돌려주고 incr 만큼 옮긴다.
 * 범위를 벗어나면 errno=ENOMEM, (void *)-1. (malloc 이 부른다 — stdio 버퍼도 여기서 나온다) */
void *_sbrk(ptrdiff_t incr) {
    static char *brk = __heap_start;
    char *prev = brk;
#ifdef BUG_SBRK
    incr = PTRDIFF_MAX;                                    /* 고장 주입: 힙이 늘 부족 */
#endif
    if ((incr > 0 && incr > __HeapLimit - brk) || (incr < 0 && -incr > brk - __heap_start)) {
        errno = ENOMEM;
        return (void *)-1;
    }
    brk += incr;
    return prev;
}

/* Part B-2: newlib 은 fstat 의 st_mode 가 S_IFCHR 이고 isatty(fd) == 1 일 때만 stdout 을 '줄 버퍼'로 만든다.
 * fd 0~2 = 콘솔(문자 장치). 그 외 fd 는 errno=EBADF 로 실패. struct stat 은 0 으로 채운 뒤 st_mode 만 설정. */
int _fstat(int fd, struct stat *st) {
    if (fd < 0 || fd > 2) { errno = EBADF; return -1; }
    memset(st, 0, sizeof *st);
#ifndef BUG_FSTAT
    st->st_mode = S_IFCHR;
#endif
    return 0;
}
int _isatty(int fd) {
    if (fd >= 0 && fd <= 2) return 1;
    errno = EBADF;
    return 0;
}

/* ======================= 나머지 syscall (주어짐) ======================= */
int _close(int fd) { (void)fd; errno = EBADF; return -1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; errno = ESPIPE; return -1; }
int _read(int fd, char *buf, int len) { (void)fd; (void)buf; (void)len; return 0; } /* 입력 없음 = EOF */
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }

/* printf 를 거치지 않는 직접 출력 — _exit / HardFault 용 (stdio 가 망가져도 써야 하므로) */
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
    SCB_AIRCR = 0x05FA0004u;    /* VECTKEY | SYSRESETREQ — QEMU 를 -no-reboot 로 띄웠으므로 QEMU 가 끝난다 */
    for (;;) { }
}

/* ======================= startup (BM-B08 축약판) ======================= */
extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
extern void __libc_init_array(void);
int main(void);
void _init(void) { }            /* -nostartfiles 라 crti.o 가 없다 → __libc_init_array 가 부르는 _init 을 직접 */
void _fini(void) { }            /* full newlib 의 exit → __libc_fini_array → _fini (nano 는 안 부름) */

void Reset_Handler(void) {
    for (uint32_t *s = _sidata, *d = _sdata; d < _edata;) *d++ = *s++;
    for (uint32_t *d = _sbss; d < _ebss;) *d++ = 0u;
    __libc_init_array();
    exit(main());               /* exit → stdio flush → _exit */
}
void hardfault_c(const uint32_t *frame) {   /* frame = 예외 스택 프레임 r0 r1 r2 r3 r12 lr pc xpsr */
    (void)uart_init(UART0_BASE, SYSCLK_HZ, CONSOLE_BAUD);
    raw_puts("\n!! HardFault HFSR=");
    raw_puthex(SCB_HFSR);
    raw_puts(" CFSR=");
    raw_puthex(SCB_CFSR);
    raw_puts(" PC=");
    raw_puthex(frame[6]);
    raw_puts("\n");
    _exit(0xDEAD);
}
__attribute__((naked)) void HardFault_Handler(void) {
    __asm__ volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n b hardfault_c\n");
}
void Default_Handler(void) { raw_puts("\n!! unexpected exception\n"); _exit(0xBAD); }

__attribute__((section(".isr_vector"), used)) const uintptr_t g_vectors[16] = {
    (uintptr_t)_estack,            /* [0] MSP 초기값 */
    (uintptr_t)Reset_Handler,      /* [1] Thumb 비트(LSB=1)는 링커가 붙인다 */
    (uintptr_t)Default_Handler,    /* [2] NMI */
    (uintptr_t)HardFault_Handler,  /* [3] HardFault */
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, /* MemManage BusFault UsageFault */
    0, 0, 0, 0,
    (uintptr_t)Default_Handler,    /* [11] SVCall */
    (uintptr_t)Default_Handler,    /* [12] DebugMonitor */
    0,
    (uintptr_t)Default_Handler,    /* [14] PendSV */
    (uintptr_t)Default_Handler,    /* [15] SysTick */
};

/* ======================= 테스트 하네스 ======================= */
static unsigned g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) g_pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

static volatile uint32_t fake_uart[5];      /* RAM 에 만든 가짜 UART (DATA STATE CTRL INTSTATUS BAUDDIV) */
#define FAKE_UART ((uint32_t)(uintptr_t)fake_uart)

static unsigned g_log_seq;
static void log_msg(char level, const char *tag, const char *fmt, ...) {   /* 펌웨어식 로그 한 줄 */
    va_list ap;
    va_start(ap, fmt);
    printf("[%4u] %c %s: ", ++g_log_seq, level, tag);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

int main(void) {
    /* ---------- Part A: 콘솔 올리기 → printf 가 UART 로 ---------- */
    int rc = uart_init(UART0_BASE, SYSCLK_HZ, CONSOLE_BAUD);
    printf("Hello from printf -> _write -> UART0 @ 0x%08lX\n", (unsigned long)UART0_BASE);
    CHECK(rc == 0);
    CHECK(UART_BAUDDIV(UART0_BASE) == 217u);                     /* 25e6 / 115200 = 217.01 */
    CHECK((UART_CTRL(UART0_BASE) & UART_CTRL_TX_EN) != 0u);
    fake_uart[4] = 0xAAAAu;
    CHECK(uart_init(FAKE_UART, SYSCLK_HZ, 1700000u) == -1 && fake_uart[4] == 0xAAAAu); /* 14.7 → 15 < 16 */
    CHECK(uart_init(FAKE_UART, SYSCLK_HZ, 1600000u) == 0 && fake_uart[4] == 16u);      /* 15.6 → 16 */
    CHECK(uart_init(FAKE_UART, SYSCLK_HZ, 9600u) == 0 && fake_uart[4] == 2604u);       /* 2604.17 */
    CHECK(uart_init(FAKE_UART, SYSCLK_HZ, 23u) == -1 && uart_init(FAKE_UART, SYSCLK_HZ, 0u) == -1);
    CHECK(fake_uart[2] == (UART_CTRL_TX_EN | UART_CTRL_RX_EN));
    printf("dec=%d neg=%d hex=0x%08lX str=%s chr=%c pct=%%\n", 42, -7, 0xDEADBEEFul, "ok", 'Z');
    CHECK(_write(1, "W1 via _write\n", 14) == 14);
    errno = 0;
    CHECK(_write(5, "x", 1) == -1 && errno == EBADF);
    CHECK(_write(1, "", 0) == 0);

    /* ---------- Part B: 버퍼링 · 힙 · 고장 주입 ---------- */
    struct stat st;
    CHECK(_fstat(1, &st) == 0 && S_ISCHR(st.st_mode) && _isatty(1) == 1 && _isatty(7) == 0);
    printf("[info] heap used by stdio buffers = %u bytes\n", (unsigned)((char *)_sbrk(0) - __heap_start));
    uint32_t w = g_write_calls;
    printf("x=%d y=%s\n", 42, "ok");                             /* 줄 버퍼: 한 줄 = _write 1번 */
    CHECK(g_write_calls - w == 1u);
    w = g_write_calls;
    printf("A1 ");                                               /* '\n' 이 없으니 아직 버퍼 안 */
    CHECK(g_write_calls == w);
    raw_puts("R ");                                              /* 버퍼를 거치지 않는 출력이 먼저 나간다 */
    printf("A2\n");                                              /* → 화면: "R A1 A2" */
    printf("P1 ");
    w = g_write_calls;
    fprintf(stderr, "E1 (stderr is unbuffered)\n");             /* → 화면: "E1 ..." 이 "P1" 보다 먼저 */
    uint32_t ew = g_write_calls - w;
    printf("P2\n");
    printf("[info] that 26-byte stderr line took %u _write calls\n", (unsigned)ew);
    void *p = malloc(100);
    CHECK(p != NULL && (char *)p >= __heap_start && (char *)p + 100 <= __HeapLimit);
    errno = 0;
    CHECK(malloc(8u << 20) == NULL && errno == ENOMEM);          /* 8 MB > 힙 */
    CHECK(_sbrk(-(ptrdiff_t)(64u << 20)) == (void *)-1);
    free(p);
    g_console = FAKE_UART;                                       /* 고장 주입: TXFULL 이 1 로 붙은 UART */
    fake_uart[1] = UART_STATE_TXFULL;
    uint32_t t = g_uart_timeouts;
    errno = 0;
    CHECK(_write(1, "ab", 2) == -1 && errno == EIO && g_uart_timeouts == t + 1u);
    fake_uart[1] = 0u;                                           /* 정상 UART (가짜): DATA 에 마지막 바이트 */
    CHECK(_write(1, "ok\n", 3) == 3 && fake_uart[0] == '\n');
    g_console = UART0_BASE;

    /* ---------- Part C: 펌웨어처럼 쓰기 ---------- */
    printf("[BOOT] BM-B09 fw, SYSCLK=%lu Hz, UART0 %lu baud (BAUDDIV=%lu)\n",
           (unsigned long)SYSCLK_HZ, (unsigned long)CONSOLE_BAUD, (unsigned long)UART_BAUDDIV(UART0_BASE));
    w = g_write_calls;
    log_msg('I', "adc", "ch%d=%d mV", 3, 1234);
    log_msg('W', "pwr", "vbat=%u.%02u V low", 3u, 41u);
    CHECK(g_write_calls - w == 2u);                              /* 로그 한 줄 = UART 버스트 1번 */
    static const uint8_t blob[12] = {0xDE, 0xAD, 0xBE, 0xEF, 0, 1, 2, 3, 0x7F, 0x80, 0xFE, 0xFF};
    for (unsigned i = 0; i < sizeof blob; i++)                   /* 8바이트씩 hex dump */
        printf("%02X%c", blob[i], (i % 8u == 7u || i + 1u == sizeof blob) ? '\n' : ' ');
    printf("%u/%u passed\n", g_pass, g_total);
    printf("bye (no newline: exit() must flush this)");          /* exit() 의 stdio 정리가 내보낸다 */
    return g_pass == g_total ? 0 : 1;
}
