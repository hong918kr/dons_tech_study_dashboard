#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ---- model answer ---- */
typedef void (*putc_fn)(char c);

static void put_hex(putc_fn out, uint32_t v, int digits)
{
    static const char hex[] = "0123456789abcdef";
    for (int sh = (digits - 1) * 4; sh >= 0; sh -= 4)
        out(hex[(v >> sh) & 0xFu]);
}

/* Line: "OOOOOOOO: hh hh .. hh  ASCII\n", 16 bytes per line,
 * short last line padded so the ASCII column stays aligned. */
void hexdump(const uint8_t *buf, size_t n, uint32_t base, putc_fn out)
{
    for (size_t line = 0; line < n; line += 16) {
        size_t cnt = (n - line < 16) ? n - line : 16;
        put_hex(out, base + (uint32_t)line, 8);
        out(':');
        out(' ');
        for (size_t i = 0; i < 16; i++) {
            if (i < cnt) {
                put_hex(out, buf[line + i], 2);
                out(' ');
            } else {
                out(' '); out(' '); out(' ');      /* pad missing byte */
            }
        }
        out(' ');
        for (size_t i = 0; i < cnt; i++) {
            uint8_t c = buf[line + i];
            out((c >= 0x20 && c <= 0x7E) ? (char)c : '.');
        }
        out('\n');
    }
}
/* ---- end model answer ---- */

static char cap[1024];
static size_t caplen;
static void cap_putc(char c)
{
    assert(caplen + 1 < sizeof cap);
    cap[caplen++] = c;
    cap[caplen] = '\0';
}

int main(void)
{
    /* page example: 20 bytes "Hello, Rivian!\n" + 00 01 7f 80 ff, base 0x40000000 */
    const uint8_t data[] = "Hello, Rivian!\n\x00\x01\x7f\x80\xff";   /* 21 bytes incl NUL */
    caplen = 0;
    cap[0] = '\0';
    hexdump(data, 20, 0x40000000u, cap_putc);
    const char *exp =
        "40000000: 48 65 6c 6c 6f 2c 20 52 69 76 69 61 6e 21 0a 00  Hello, Rivian!..\n"
        "40000010: 01 7f 80 ff                                      ....\n";
    assert(strcmp(cap, exp) == 0);

    /* n == 0 -> nothing */
    caplen = 0;
    cap[0] = '\0';
    hexdump(data, 0, 0, cap_putc);
    assert(caplen == 0);

    /* exactly 16 bytes -> one full line, no second line */
    caplen = 0;
    hexdump(data, 16, 0xFFFFFFF0u, cap_putc);
    assert(strcmp(cap, "fffffff0: 48 65 6c 6c 6f 2c 20 52 69 76 69 61 6e 21 0a 00  Hello, Rivian!..\n") == 0);

    /* 1 byte -> padding keeps ASCII column at same position */
    caplen = 0;
    hexdump((const uint8_t *)"A", 1, 0x10, cap_putc);
    assert(strcmp(cap, "00000010: 41                                               A\n") == 0);
    assert(strlen("40000010: 01 7f 80 ff                                      ") ==
           strlen("00000010: 41                                               "));

    puts("D3 PASS");
    return 0;
}
