/* E1 - power-of-2 ring buffer with free-running indices (uint16_t ADC samples) */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>

/* ==== MODEL ANSWER BEGIN ==== */
#define RB_N    8u                               /* capacity, must be 2^k */
#define RB_MASK (RB_N - 1u)
_Static_assert(RB_N != 0 && (RB_N & (RB_N - 1u)) == 0, "RB_N must be a power of 2");

typedef struct {
    uint16_t buf[RB_N];
    uint32_t head;              /* total items ever written (free-running) */
    uint32_t tail;              /* total items ever read    (free-running) */
} rb_t;

static uint32_t rb_count(const rb_t *r) { return r->head - r->tail; } /* wrap-safe */
static uint32_t rb_space(const rb_t *r) { return RB_N - rb_count(r); }

bool rb_push(rb_t *r, uint16_t v)
{
    if (rb_count(r) == RB_N)
        return false;                            /* full: drop newest, caller counts */
    r->buf[r->head & RB_MASK] = v;               /* mask only when indexing */
    r->head++;
    return true;
}

bool rb_pop(rb_t *r, uint16_t *out)
{
    if (r->head == r->tail)
        return false;                            /* empty */
    *out = r->buf[r->tail & RB_MASK];
    r->tail++;
    return true;
}

size_t rb_read(rb_t *r, uint16_t *out, size_t max)   /* bulk pop, oldest first */
{
    size_t n = rb_count(r);
    if (n > max)
        n = max;
    for (size_t i = 0; i < n; i++)
        out[i] = r->buf[(r->tail + (uint32_t)i) & RB_MASK];
    r->tail += (uint32_t)n;                      /* release all slots at once */
    return n;
}
/* ==== MODEL ANSWER END ==== */

static void reset(rb_t *r, uint32_t start) { r->head = r->tail = start; }

int main(void)
{
    rb_t r; uint16_t v, out[16];
    for (int s = 0; s < 3; s++) {
        uint32_t start = (s == 0) ? 0u : (s == 1) ? 0xFFFFFFFCu : 0xFFFFFFFFu;
        reset(&r, start);
        assert(!rb_pop(&r, &v));                         /* empty */
        assert(rb_count(&r) == 0 && rb_space(&r) == RB_N);
        for (uint16_t i = 0; i < RB_N; i++) assert(rb_push(&r, (uint16_t)(100 + i)));
        assert(rb_count(&r) == RB_N && rb_space(&r) == 0);
        assert(!rb_push(&r, 999));                       /* N+1 -> full */
        for (uint16_t i = 0; i < RB_N; i++) { assert(rb_pop(&r, &v)); assert(v == 100 + i); }
        assert(!rb_pop(&r, &v));
        /* bulk read: max 0, partial, rest */
        for (uint16_t i = 0; i < 5; i++) assert(rb_push(&r, (uint16_t)(0xA000 + i)));
        assert(rb_read(&r, out, 0) == 0 && rb_count(&r) == 5);
        assert(rb_read(&r, out, 2) == 2 && out[0] == 0xA000 && out[1] == 0xA001);
        assert(rb_read(&r, out, 16) == 3 && out[0] == 0xA002 && out[2] == 0xA004);
        assert(rb_read(&r, out, 16) == 0);
    }
    /* wrap check: head crosses 2^32 */
    reset(&r, 0xFFFFFFFEu);
    for (uint16_t i = 0; i < 5; i++) assert(rb_push(&r, i));
    assert(r.head == 3u && rb_count(&r) == 5);
    assert(r.buf[6] == 0 && r.buf[7] == 1 && r.buf[0] == 2);
    /* randomized interleave vs reference model */
    reset(&r, 0xFFFFFF00u);
    uint32_t seed = 12345, wr = 0, rd = 0;
    for (int k = 0; k < 100000; k++) {
        seed = seed * 1103515245u + 12345u;
        if ((seed >> 16) & 1) { if (rb_push(&r, (uint16_t)wr)) wr++; else assert(wr - rd == RB_N); }
        else if (rb_pop(&r, &v)) { assert(v == (uint16_t)rd); rd++; } else assert(wr == rd);
        assert(rb_count(&r) == wr - rd);
    }
    puts("E1 PASS");
    return 0;
}
