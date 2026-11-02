/* G8 - parse a fixed-format log line "T=123 V=0x1F" into a struct, no sscanf */
/* --- model answer --- */
#include <stdint.h>
#include <stdbool.h>

typedef struct { uint32_t t; uint32_t v; } rec_t;
enum { LP_OK = 0, LP_FORMAT = -1, LP_RANGE = -2 };

/* Parse digits at *pp in base 10 or 16, advance *pp. Returns LP_* code. */
static int parse_num(const char **pp, unsigned base, uint32_t *out)
{
    const char *p = *pp;
    uint32_t v = 0;
    int nd = 0;
    for (;; p++, nd++) {
        unsigned d;
        char c = *p;
        if (c >= '0' && c <= '9')                    d = (unsigned)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
        else break;
        if (v > (UINT32_MAX - d) / base) return LP_RANGE;   /* v*base + d would overflow */
        v = v * base + d;
    }
    if (nd == 0) return LP_FORMAT;
    *pp = p; *out = v;
    return LP_OK;
}

/* Format: "T=<dec>" one-or-more spaces "V=0x<hex>" then optional "\r\n" / "\n". */
int parse_line(const char *s, rec_t *r)
{
    int rc;
    if (s[0] != 'T' || s[1] != '=') return LP_FORMAT;
    s += 2;
    if ((rc = parse_num(&s, 10, &r->t)) != LP_OK) return rc;
    if (*s != ' ') return LP_FORMAT;
    while (*s == ' ') s++;
    if (s[0] != 'V' || s[1] != '=' || s[2] != '0' || (s[3] != 'x' && s[3] != 'X')) return LP_FORMAT;
    s += 4;
    if ((rc = parse_num(&s, 16, &r->v)) != LP_OK) return rc;
    if (*s == '\r') s++;
    if (*s == '\n') s++;
    return (*s == '\0') ? LP_OK : LP_FORMAT;            /* reject trailing junk */
}
/* --- tests --- */
#include <assert.h>
#include <stdio.h>

int main(void)
{
    rec_t r;

    assert(parse_line("T=123 V=0x1F", &r) == LP_OK && r.t == 123 && r.v == 0x1F);
    assert(parse_line("T=0 V=0x0\n", &r) == LP_OK && r.t == 0 && r.v == 0);
    assert(parse_line("T=42   V=0XdeadBEEF\r\n", &r) == LP_OK && r.t == 42 && r.v == 0xDEADBEEFu);
    assert(parse_line("T=4294967295 V=0xFFFFFFFF", &r) == LP_OK &&
           r.t == UINT32_MAX && r.v == UINT32_MAX);                        /* max values */

    assert(parse_line("T=4294967296 V=0x1", &r) == LP_RANGE);              /* dec overflow */
    assert(parse_line("T=1 V=0x100000000", &r) == LP_RANGE);               /* hex overflow */
    assert(parse_line("", &r) == LP_FORMAT);
    assert(parse_line("T= V=0x1", &r) == LP_FORMAT);                       /* no digits */
    assert(parse_line("T=12V=0x1", &r) == LP_FORMAT);                      /* missing space */
    assert(parse_line("T=12 V=1F", &r) == LP_FORMAT);                      /* missing 0x */
    assert(parse_line("T=12 V=0x", &r) == LP_FORMAT);
    assert(parse_line("T=12 V=0x1G", &r) == LP_FORMAT);                    /* trailing junk */
    assert(parse_line("V=0x1 T=12", &r) == LP_FORMAT);                     /* wrong order */
    assert(parse_line("T=-5 V=0x1", &r) == LP_FORMAT);
    assert(parse_line("T=1 V=0x2\n\n", &r) == LP_FORMAT);

    printf("G8 PASS\n");
    return 0;
}
