/* TOOL-B04 anatomy.c — one of everything that ends up in an object file.
 * Not a program (no main): only ever compiled with -c and inspected with
 * nm / objdump / otool / readelf.
 *   Mach-O : cc -O2 -c anatomy.c -o anatomy_macho.o
 *   ELF    : clang --target=aarch64-linux-gnu -O2 -c anatomy.c -o anatomy_elf.o
 *   Cortex-M4 ELF : clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -c anatomy.c
 */
extern int ext_counter;                      /* extern variable: used, defined elsewhere */
extern void ext_log(const char *msg, int v); /* extern function: called, defined elsewhere */

int g_init = 42;                             /* global, initialized               */
int g_zero = 0;                              /* global, explicitly zero           */
int g_tent;                                  /* tentative definition (no "= ...") */
static int s_file = 7;                       /* file-scope static, initialized    */
static int s_file_zero;                      /* file-scope static, zero           */
const int g_const = 100;                     /* const global                      */
const char *g_msg = "hello";                 /* pointer variable -> string literal */

__attribute__((noinline)) static int helper(int x) { return x * 3 + s_file; }

static int unused_static(int x) { return x + 1; }                  /* never called */
__attribute__((used)) static int kept_static(int x) { return x + 2; } /* never called */

__attribute__((weak)) int board_init(void) { return 0; }  /* default, overridable */

int counter_next(void)
{
    static int s_local;                      /* static local */
    s_local += 1;
    return s_local;
}

int api_entry(int v)
{
    ext_log("api_entry", v);                 /* extern call + string literal */
    ext_counter += helper(v) + s_file_zero + g_zero + g_tent;
    return g_init + g_const + board_init();
}
