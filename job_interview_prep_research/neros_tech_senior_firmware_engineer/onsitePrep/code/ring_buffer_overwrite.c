/* Overwrite-oldest ring (logger / blackbox style) with dropped counter and a critical section. */
/*
 * Why overwrite is not "pure SPSC":
 *   When full, the producer must advance tail to drop the oldest entry. Now BOTH sides write
 *   tail, so the consumer can read a slot that the producer is overwriting at the same time
 *   (torn / out-of-order record). Two safe designs:
 *     1) Keep the overwrite (tail move + write) and the consumer's pop inside a short critical
 *        section (disable IRQ on MCU) -- simple, what this file does.
 *     2) Never move tail from the producer: stamp each record with a sequence number; the
 *        consumer detects gaps (seq jumped) and counts them as dropped. Lock-free.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define LOG_SIZE 8u
#define LOG_MASK (LOG_SIZE - 1u)

typedef struct {
    uint32_t seq;                        /* lets the reader see gaps */
    uint32_t value;
} rec_t;

typedef struct {
    rec_t    buf[LOG_SIZE];
    uint32_t head, tail;
    uint32_t next_seq;
    uint32_t dropped;                    /* how many oldest records were overwritten */
} olog_t;

/* host stand-ins for __disable_irq()/__enable_irq() (or PRIMASK save/restore on Cortex-M) */
static int g_irq_depth;
static void irq_lock(void)   { g_irq_depth++; }
static void irq_unlock(void) { g_irq_depth--; }

static void olog_init(olog_t *l) { *l = (olog_t){0}; }

static void olog_push(olog_t *l, uint32_t value)          /* never fails: newest data wins */
{
    irq_lock();
    if (l->head - l->tail == LOG_SIZE) {
        l->tail++;                       /* drop oldest */
        l->dropped++;
    }
    l->buf[l->head & LOG_MASK] = (rec_t){ .seq = l->next_seq++, .value = value };
    l->head++;
    irq_unlock();
}

static bool olog_pop(olog_t *l, rec_t *out)
{
    bool ok = false;
    irq_lock();
    if (l->head != l->tail) {
        *out = l->buf[l->tail & LOG_MASK];
        l->tail++;
        ok = true;
    }
    irq_unlock();
    return ok;
}

int main(void)
{
    olog_t l;
    rec_t r;
    olog_init(&l);

    for (uint32_t i = 0; i < 5; i++)
        olog_push(&l, 100 + i);
    assert(l.dropped == 0);
    assert(olog_pop(&l, &r) && r.value == 100 && r.seq == 0);

    /* 4 left; push 10 more -> 14 > 8, so the 6 oldest are overwritten */
    for (uint32_t i = 0; i < 10; i++)
        olog_push(&l, 200 + i);
    assert(l.dropped == 6);
    assert(l.head - l.tail == LOG_SIZE);

    /* reader gets the newest 8, in order; seq gap shows what was lost */
    uint32_t prev_seq = 0, n = 0;
    while (olog_pop(&l, &r)) {
        if (n == 0)
            assert(r.seq == 7);          /* seqs 1..6 were overwritten */
        else
            assert(r.seq == prev_seq + 1);
        prev_seq = r.seq;
        n++;
    }
    assert(n == LOG_SIZE && r.value == 209);
    assert(g_irq_depth == 0);            /* every lock had its unlock */

    puts("ring_buffer_overwrite: PASS");
    return 0;
}
