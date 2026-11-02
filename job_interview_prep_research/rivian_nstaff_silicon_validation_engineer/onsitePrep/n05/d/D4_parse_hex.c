#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ---- model answer ---- */
typedef enum { HEX_OK = 0, HEX_ERR_NULL, HEX_ERR_EMPTY, HEX_ERR_CHAR, HEX_ERR_OVERFLOW } hex_err_t;

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Optional "0x"/"0X", then 1+ hex digits, then end of string.
 * *out is written only on HEX_OK. Leading zeros are allowed. */
hex_err_t hex_to_u32(const char *s, uint32_t *out)
{
    if (s == NULL || out == NULL)
        return HEX_ERR_NULL;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    if (*s == '\0')
        return HEX_ERR_EMPTY;
    uint32_t v = 0;
    for (; *s != '\0'; s++) {
        int d = hex_val(*s);
        if (d < 0)
            return HEX_ERR_CHAR;
        if (v > (UINT32_MAX >> 4))          /* v << 4 would drop bits */
            return HEX_ERR_OVERFLOW;
        v = (v << 4) | (uint32_t)d;
    }
    *out = v;
    return HEX_OK;
}
/* ---- end model answer ---- */

int main(void)
{
    uint32_t v = 0;
    assert(hex_to_u32("0x1A2b", &v) == HEX_OK && v == 0x1A2Bu);
    assert(hex_to_u32("deadbeef", &v) == HEX_OK && v == 0xDEADBEEFu);
    assert(hex_to_u32("0XFFFFFFFF", &v) == HEX_OK && v == 0xFFFFFFFFu);
    assert(hex_to_u32("0", &v) == HEX_OK && v == 0);
    assert(hex_to_u32("0x0", &v) == HEX_OK && v == 0);
    assert(hex_to_u32("0x00000000ff", &v) == HEX_OK && v == 0xFFu);   /* leading zeros */

    v = 0x12345678u;
    assert(hex_to_u32("0x100000000", &v) == HEX_ERR_OVERFLOW && v == 0x12345678u);
    assert(hex_to_u32("123456789", &v) == HEX_ERR_OVERFLOW);
    assert(hex_to_u32("", &v) == HEX_ERR_EMPTY);
    assert(hex_to_u32("0x", &v) == HEX_ERR_EMPTY);
    assert(hex_to_u32("0xg1", &v) == HEX_ERR_CHAR);
    assert(hex_to_u32("12 ", &v) == HEX_ERR_CHAR);
    assert(hex_to_u32("-1", &v) == HEX_ERR_CHAR);
    assert(hex_to_u32("0x0x1", &v) == HEX_ERR_CHAR);
    assert(hex_to_u32(NULL, &v) == HEX_ERR_NULL);
    assert(hex_to_u32("1", NULL) == HEX_ERR_NULL);
    assert(v == 0x12345678u);                                         /* untouched on error */

    puts("D4 PASS");
    return 0;
}
