/* E4 - overwrite-oldest trace log ring + oldest-first dump */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>

/* ==== MODEL ANSWER BEGIN ==== */
#define TL_N 4u                                  /* any size */

typedef struct { uint32_t ts; uint16_t code; uint16_t arg; } trace_ev_t;

typedef struct {
    trace_ev_t ev[TL_N];
    uint32_t   next;          /* slot written next, 0..N-1 (= oldest once wrapped) */
    bool       wrapped;       /* set on first wrap: buffer is full from then on */
} trace_log_t;

void trace_log(trace_log_t *t, uint32_t ts, uint16_t code, uint16_t arg)
{
    t->ev[t->next] = (trace_ev_t){ ts, code, arg };   /* never fails: overwrite oldest */
    if (++t->next == TL_N) {
        t->next = 0;
        t->wrapped = true;
    }
}

/* copy the NEWEST min(valid, max) events into out[], oldest first */
size_t trace_dump(const trace_log_t *t, trace_ev_t *out, size_t max)
{
    size_t n     = t->wrapped ? TL_N : t->next;       /* valid entries */
    size_t start = t->wrapped ? t->next : 0;          /* index of the oldest */
    size_t skip  = (n > max) ? n - max : 0;           /* drop the oldest ones */
    for (size_t i = 0; i < n - skip; i++)
        out[i] = t->ev[(start + skip + i) % TL_N];
    return n - skip;
}
/* ==== MODEL ANSWER END ==== */

int main(void)
{
    trace_log_t t = {0}; trace_ev_t o[8];
    assert(trace_dump(&t, o, 8) == 0);                       /* empty */
    trace_log(&t, 10, 1, 0); trace_log(&t, 11, 2, 0);
    assert(trace_dump(&t, o, 8) == 2 && o[0].ts == 10 && o[1].ts == 11);   /* not wrapped */
    trace_log(&t, 12, 3, 0); trace_log(&t, 13, 4, 0);        /* exactly full */
    assert(t.wrapped && t.next == 0);
    assert(trace_dump(&t, o, 8) == 4 && o[0].ts == 10 && o[3].ts == 13);
    trace_log(&t, 14, 5, 0); trace_log(&t, 15, 6, 0);        /* overwrote 10, 11 */
    assert(trace_dump(&t, o, 8) == 4);
    assert(o[0].ts == 12 && o[1].ts == 13 && o[2].ts == 14 && o[3].ts == 15);
    assert(trace_dump(&t, o, 2) == 2 && o[0].ts == 14 && o[1].ts == 15);   /* newest 2 */
    assert(trace_dump(&t, o, 0) == 0);
    /* long run: dump always = last N in order */
    for (uint32_t ts = 16; ts < 1000; ts++) {
        trace_log(&t, ts, (uint16_t)ts, (uint16_t)~ts);
        size_t n = trace_dump(&t, o, 8);
        assert(n == TL_N);
        for (size_t i = 0; i < n; i++) assert(o[i].ts == ts - (TL_N - 1) + i && (o[i].ts < 16 || o[i].code == (uint16_t)o[i].ts));
    }
    puts("E4 PASS");
    return 0;
}
