/* G4 - debug shell: "rd addr" / "wr addr val" / "help" via a command table */
/* --- given --- */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#define MAX_ARGS 4
enum { SH_OK = 0, SH_EMPTY = 1, SH_UNKNOWN = -1, SH_ARGC = -2, SH_BADNUM = -3, SH_BADADDR = -4 };

/* --- fake hardware: 16 registers at 0x40000000 --- */
#define REG_BASE 0x40000000u
static uint32_t fake_regs[16];
uint32_t g_out;                            /* last value "printed" (no printf on target) */

static bool parse_hex(const char *s, uint32_t *v)     /* "0x1F" or "1f"; D4 has the full version */
{
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (*s == '\0') return false;
    uint32_t r = 0;
    for (; *s; s++) {
        unsigned d;
        if (*s >= '0' && *s <= '9')      d = (unsigned)(*s - '0');
        else if (*s >= 'a' && *s <= 'f') d = (unsigned)(*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') d = (unsigned)(*s - 'A' + 10);
        else return false;
        if (r > (UINT32_MAX >> 4)) return false;      /* overflow */
        r = (r << 4) | d;
    }
    *v = r;
    return true;
}

static volatile uint32_t *reg_ptr(uint32_t addr)       /* NULL if outside the window / unaligned */
{
    if (addr < REG_BASE || (addr & 3u) || (addr - REG_BASE) / 4u >= 16u) return NULL;
    return &fake_regs[(addr - REG_BASE) / 4u];
}

/* --- model answer --- */
static int cmd_rd(int argc, char **argv);
static int cmd_wr(int argc, char **argv);
static int cmd_help(int argc, char **argv);

typedef struct {
    const char *name;
    int (*fn)(int argc, char **argv);
    uint8_t argc;                          /* exact argc including the command name */
    const char *usage;
} cmd_t;

static const cmd_t cmds[] = {
    { "rd",   cmd_rd,   2, "rd <addr>"       },
    { "wr",   cmd_wr,   3, "wr <addr> <val>" },
    { "help", cmd_help, 1, "help"            },
};
#define NCMDS (sizeof cmds / sizeof cmds[0])

static int cmd_rd(int argc, char **argv)
{
    (void)argc;
    uint32_t a;
    if (!parse_hex(argv[1], &a)) return SH_BADNUM;
    volatile uint32_t *r = reg_ptr(a);
    if (!r) return SH_BADADDR;
    g_out = *r;
    return SH_OK;
}

static int cmd_wr(int argc, char **argv)
{
    (void)argc;
    uint32_t a, v;
    if (!parse_hex(argv[1], &a) || !parse_hex(argv[2], &v)) return SH_BADNUM;
    volatile uint32_t *r = reg_ptr(a);
    if (!r) return SH_BADADDR;
    *r = v;
    return SH_OK;
}

static int cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    g_out = (uint32_t)NCMDS;               /* real target: uart_puts(cmds[i].usage) per entry */
    return SH_OK;
}

/* Tokenize line in place (spaces/tabs -> '\0'), look up the command, check argc, call it. */
int shell_exec(char *line)
{
    char *argv[MAX_ARGS];
    int argc = 0;
    for (char *p = line; *p; ) {
        while (*p == ' ' || *p == '\t') *p++ = '\0';
        if (*p == '\0') break;
        if (argc == MAX_ARGS) return SH_ARGC;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    if (argc == 0) return SH_EMPTY;
    for (size_t i = 0; i < NCMDS; i++) {
        if (strcmp(argv[0], cmds[i].name) != 0) continue;
        if (argc != cmds[i].argc) return SH_ARGC;
        return cmds[i].fn(argc, argv);
    }
    return SH_UNKNOWN;
}
/* --- tests --- */
#include <assert.h>
#include <stdio.h>

static int run(const char *s)                 /* shell_exec needs a writable copy */
{
    char buf[64];
    strncpy(buf, s, sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';
    return shell_exec(buf);
}

int main(void)
{
    assert(run("wr 0x40000004 0xDEADBEEF") == SH_OK);
    assert(fake_regs[1] == 0xDEADBEEFu);
    g_out = 0;
    assert(run("rd 0x40000004") == SH_OK && g_out == 0xDEADBEEFu);
    assert(run("  \t rd   40000004  ") == SH_OK && g_out == 0xDEADBEEFu);   /* extra blanks, no 0x */
    assert(run("wr 0x4000003C 1") == SH_OK && fake_regs[15] == 1);        /* last register */

    assert(run("") == SH_EMPTY);
    assert(run("    ") == SH_EMPTY);
    assert(run("help") == SH_OK && g_out == 3);
    assert(run("help me") == SH_ARGC);
    assert(run("rd") == SH_ARGC);
    assert(run("wr 0x40000000") == SH_ARGC);
    assert(run("wr 1 2 3 4 5") == SH_ARGC);                                 /* > MAX_ARGS */
    assert(run("peek 0x40000000") == SH_UNKNOWN);
    assert(run("rdx 0x40000000") == SH_UNKNOWN);                            /* prefix must not match */
    assert(run("rd 0x4000000G") == SH_BADNUM);
    assert(run("rd 0x") == SH_BADNUM);
    assert(run("rd 0x100000000") == SH_BADNUM);                             /* 33-bit overflow */
    assert(run("rd 0x40000040") == SH_BADADDR);                             /* one past window */
    assert(run("rd 0x40000002") == SH_BADADDR);                             /* unaligned */
    assert(run("rd 0x3FFFFFFC") == SH_BADADDR);                             /* below base */

    printf("G4 PASS\n");
    return 0;
}
