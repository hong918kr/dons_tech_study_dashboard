/* ENV: cross — demo_fw.sh 가 arm-none-eabi-gcc 로 빌드하고 VM 의 qemu-system-arm(mps2-an385)에서 돌린다. */
/* TOOL-M03 펌웨어 데모: 명령 테이블과 initcall 을 Flash 의 커스텀 섹션에 모으고 링커 스크립트 경계로 순회.
 * -nostdlib (Mac arm-none-eabi-gcc 16 에는 newlib 이 없다) → 출력은 semihosting SYS_WRITE0.
 */
#include <stdint.h>
#include <stddef.h>

struct shell_cmd { const char *name; int (*fn)(int argc, const char *const *argv); const char *help; };
typedef int (*initcall_t)(void);

#define SHELL_CMD(name_, fn_, help_)                                                         \
    static const struct shell_cmd shell_cmd_##name_                                          \
        __attribute__((used, aligned(4), section(".shellcmds"))) = { #name_, fn_, help_ }
#define INITCALL(level, fn)                                                                  \
    static const initcall_t initcall_##fn                                                    \
        __attribute__((used, section(".initcall." #level))) = fn

extern const struct shell_cmd __shellcmds_start[], __shellcmds_end[];
extern const initcall_t __initcall_start[], __initcall_end[];
extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];

/* ---- semihosting (QEMU -semihosting-config enable=on) ---- */
static int semi(int op, const void *arg) {
    register int r0 __asm("r0") = op;
    register const void *r1 __asm("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
static void puts_(const char *s) { semi(0x04, s); }
static void put_u(unsigned v) {
    char b[12]; int i = 11; b[i] = '\0';
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v != 0u);
    puts_(&b[i]);
}
static void put_hex(uint32_t v) {
    char b[11] = "0x00000000";
    for (int i = 9; i >= 2; i--) { b[i] = "0123456789abcdef"[v & 0xFu]; v >>= 4; }
    puts_(b);
}
static int str_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* ---- 모듈들 (원래는 각자 다른 .c 파일) ---- */
static int led_state;
static int cmd_led(int argc, const char *const *argv) {
    if (argc != 2) return 2;
    led_state = str_eq(argv[1], "on");
    puts_("  led -> "); puts_(led_state ? "on\n" : "off\n");
    return 0;
}
static int cmd_ver(int argc, const char *const *argv) { (void)argc; (void)argv; puts_("  fw 1.4.2 (TOOL-M03)\n"); return 0; }
static int cmd_help(int argc, const char *const *argv) {
    (void)argc; (void)argv;
    for (const struct shell_cmd *c = __shellcmds_start; c < __shellcmds_end; c++) {
        puts_("  "); puts_(c->name); puts_(" - "); puts_(c->help); puts_("\n");
    }
    return 0;
}
SHELL_CMD(led,     cmd_led,  "led on|off");
SHELL_CMD(version, cmd_ver,  "firmware version");
SHELL_CMD(help,    cmd_help, "list commands");

static int clock_init(void) { puts_("initcall 0 clock\n"); return 0; }
static int uart_init(void)  { puts_("initcall 2 uart\n");  return 0; }
static int gpio_init(void)  { puts_("initcall 1 gpio\n");  return 0; }
static int app_init(void)   { puts_("initcall 3 app\n");   return 0; }
INITCALL(3, app_init);      /* 정의 순서는 일부러 뒤섞음: 3, 0, 2, 1 */
INITCALL(0, clock_init);
INITCALL(2, uart_init);
INITCALL(1, gpio_init);

static int run(const char *const *argv, int argc) {
    for (const struct shell_cmd *c = __shellcmds_start; c < __shellcmds_end; c++)
        if (str_eq(c->name, argv[0])) return c->fn(argc, argv);
    puts_("  unknown command\n");
    return -1;
}

static void sys_exit(int code) {
    uint32_t blk[2] = { 0x20026u /* ADP_Stopped_ApplicationExit */, (uint32_t)code };
    semi(0x20, blk);                                     /* SYS_EXIT_EXTENDED */
    for (;;) { }
}

void Reset_Handler(void) {
    for (uint32_t *s = _sidata, *d = _sdata; d < _edata;) *d++ = *s++;
    for (uint32_t *d = _sbss; d < _ebss;) *d++ = 0u;

    puts_("shellcmds @ "); put_hex((uint32_t)(uintptr_t)__shellcmds_start); puts_(", ");
    put_u((unsigned)(__shellcmds_end - __shellcmds_start)); puts_(" entries\n");
    for (const initcall_t *f = __initcall_start; f < __initcall_end; f++) (*f)();

    static const char *const l1[] = { "help" }, *const l2[] = { "led", "on" }, *const l3[] = { "version" },
                             *const l4[] = { "selftest" };
    int bad = 0;
    puts_("> help\n");     bad |= run(l1, 1) != 0;
    puts_("> led on\n");   bad |= run(l2, 2) != 0 || led_state != 1;
    puts_("> version\n");  bad |= run(l3, 1) != 0;
    puts_("> selftest\n"); bad |= run(l4, 1) != -1;
    puts_(bad ? "RESULT: FAIL\n" : "RESULT: OK\n");
    sys_exit(bad);
}
static void Default_Handler(void) { for (;;) { } }

__attribute__((used, section(".isr_vector")))
static const void *const g_vectors[16] = {
    _estack, (const void *)Reset_Handler,
    (const void *)Default_Handler, (const void *)Default_Handler,   /* NMI, HardFault */
};
