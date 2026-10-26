/* T5 log check -- "how many lost?" ... "seq is 16-bit" ... "corrupted lines" ... "out of order" */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bool started;
    uint16_t expected;
    long lost;
    unsigned dup, reordered, bad;
    uint8_t missing[65536 / 8];   /* 8 KB bitmap: one bit per seq */
} seqlog_t;

static bool bit_get(const uint8_t *m, uint16_t s) { return m[s >> 3] & (1u << (s & 7)); }
static void bit_set(uint8_t *m, uint16_t s)       { m[s >> 3] |= (uint8_t)(1u << (s & 7)); }
static void bit_clr(uint8_t *m, uint16_t s)       { m[s >> 3] &= (uint8_t)~(1u << (s & 7)); }

/* "... seq=123 ..." -> true and *out, false for anything malformed */
static bool parse_seq(const char *line, uint16_t *out)
{
    const char *p = strstr(line, "seq=");
    if (!p || p[4] < '0' || p[4] > '9')
        return false;
    char *end;
    unsigned long v = strtoul(p + 4, &end, 10);
    if (v > 0xFFFF || (*end != '\0' && *end != ' '))
        return false;
    *out = (uint16_t)v;
    return true;
}

void seqlog_line(seqlog_t *t, const char *line)
{
    uint16_t seq;
    if (!parse_seq(line, &seq)) {
        t->bad++;
        return;
    }
    if (!t->started) {
        t->started = true;
        t->expected = (uint16_t)(seq + 1);
        return;
    }
    uint16_t diff = (uint16_t)(seq - t->expected);   /* uint16_t math = mod 65536 */
    if (diff == 0) {
        t->expected = (uint16_t)(seq + 1);
    } else if (diff < 0x8000) {                       /* forward jump: diff missing */
        for (uint16_t k = 0; k < diff; k++)
            bit_set(t->missing, (uint16_t)(t->expected + k));
        t->lost += diff;
        t->expected = (uint16_t)(seq + 1);
    } else if (bit_get(t->missing, seq)) {            /* late arrival */
        bit_clr(t->missing, seq);
        t->lost--;
        t->reordered++;
    } else {
        t->dup++;
    }
}

static seqlog_t *run(const char **lines, size_t n)
{
    static seqlog_t t;
    memset(&t, 0, sizeof t);
    for (size_t i = 0; i < n; i++)
        seqlog_line(&t, lines[i]);
    return &t;
}

int main(void)
{
    const char *a[] = {"seq=1", "seq=2", "seq=3"};
    assert(run(a, 3)->lost == 0);
    const char *w[] = {"seq=65534", "seq=65535", "seq=0", "seq=1"};
    assert(run(w, 4)->lost == 0);                           /* wrap is not a gap */
    const char *g[] = {"seq=65535", "seq=2"};
    assert(run(g, 2)->lost == 2);                           /* 0, 1 lost across wrap */
    const char *r[] = {"seq=1", "seq=4", "seq=2"};
    seqlog_t *t = run(r, 3);
    assert(t->lost == 1 && t->reordered == 1);              /* 2 came late */
    const char *d[] = {"seq=1", "seq=2", "seq=2"};
    assert(run(d, 3)->dup == 1);
    const char *b[] = {"t=0 seq=1", "garbage", "seq=x", "seq=70000", "seq=2"};
    t = run(b, 5);
    assert(t->bad == 3 && t->lost == 0);
    puts("t5 OK");
    return 0;
}
