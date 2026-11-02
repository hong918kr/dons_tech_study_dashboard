/* E2 - arbitrary-size ring: count-based vs one-slot-empty */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>

/* ==== MODEL ANSWER BEGIN ==== */
#define QN 5u                                    /* any size, NOT a power of 2 */

/* (a) count-based: storage N, capacity N. `count` is written by BOTH sides. */
typedef struct { uint8_t buf[QN]; uint32_t head, tail, count; } cq_t;

bool cq_push(cq_t *q, uint8_t v)
{
    if (q->count == QN) return false;            /* full */
    q->buf[q->head] = v;
    q->head = (q->head + 1u) % QN;
    q->count++;
    return true;
}

bool cq_pop(cq_t *q, uint8_t *out)
{
    if (q->count == 0) return false;             /* empty */
    *out = q->buf[q->tail];
    q->tail = (q->tail + 1u) % QN;
    q->count--;
    return true;
}

/* (b) one-slot-empty: storage N, capacity N-1. head: producer only, tail: consumer only. */
typedef struct { uint8_t buf[QN]; uint32_t head, tail; } sq_t;

static uint32_t sq_next(uint32_t i) { return (i + 1u == QN) ? 0u : i + 1u; } /* no % */

bool sq_push(sq_t *q, uint8_t v)
{
    uint32_t n = sq_next(q->head);
    if (n == q->tail) return false;              /* full: one slot always kept empty */
    q->buf[q->head] = v;
    q->head = n;
    return true;
}

bool sq_pop(sq_t *q, uint8_t *out)
{
    if (q->head == q->tail) return false;        /* empty */
    *out = q->buf[q->tail];
    q->tail = sq_next(q->tail);
    return true;
}

uint32_t sq_count(const sq_t *q) { return (q->head + QN - q->tail) % QN; }
/* ==== MODEL ANSWER END ==== */

int main(void)
{
    cq_t c = {0}; sq_t s = {0}; uint8_t v;
    assert(!cq_pop(&c, &v) && !sq_pop(&s, &v));          /* empty */
    for (uint8_t i = 0; i < QN; i++) assert(cq_push(&c, i));
    assert(!cq_push(&c, 99));                            /* count-based holds N */
    for (uint8_t i = 0; i < QN - 1; i++) assert(sq_push(&s, i));
    assert(!sq_push(&s, 99));                            /* one-slot-empty holds N-1 */
    assert(sq_count(&s) == QN - 1);
    for (uint8_t i = 0; i < QN; i++) { assert(cq_pop(&c, &v) && v == i); }
    for (uint8_t i = 0; i < QN - 1; i++) { assert(sq_pop(&s, &v) && v == i); }
    assert(!cq_pop(&c, &v) && !sq_pop(&s, &v) && sq_count(&s) == 0);
    /* interleave across many wraps vs reference */
    uint32_t seed = 7, cw = 0, cr = 0, sw = 0, sr = 0;
    for (int k = 0; k < 100000; k++) {
        seed = seed * 1103515245u + 12345u;
        if ((seed >> 16) & 1) {
            if (cq_push(&c, (uint8_t)cw)) cw++; else assert(cw - cr == QN);
            if (sq_push(&s, (uint8_t)sw)) sw++; else assert(sw - sr == QN - 1);
        } else {
            if (cq_pop(&c, &v)) { assert(v == (uint8_t)cr); cr++; } else assert(cw == cr);
            if (sq_pop(&s, &v)) { assert(v == (uint8_t)sr); sr++; } else assert(sw == sr);
        }
        assert(c.count == cw - cr && sq_count(&s) == sw - sr);
        assert(c.head < QN && c.tail < QN && s.head < QN && s.tail < QN);
    }
    /* why free-running needs 2^k: 2^32 % 5 != 0 -> slot order breaks at wrap */
    uint32_t h = 0xFFFFFFFFu;
    assert(h % QN == 0u && (uint32_t)(h + 1u) % QN == 0u);   /* 2^32-1 = 5*858993459; 0 -> slot 0 twice */
    puts("E2 PASS");
    return 0;
}
