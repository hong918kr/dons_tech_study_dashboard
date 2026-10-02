/* Classic byte ring buffer: power-of-2 capacity, free-running head/tail, bulk read/write. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RB_SIZE 16u                      /* must be a power of 2 */
#define RB_MASK (RB_SIZE - 1u)
_Static_assert((RB_SIZE & RB_MASK) == 0, "RB_SIZE must be a power of 2");

typedef struct {
    uint8_t  buf[RB_SIZE];
    uint32_t head;                       /* next write, never masked -> wraps at 2^32 */
    uint32_t tail;                       /* next read */
} rb_t;

static void rb_init(rb_t *rb) { rb->head = rb->tail = 0; }

/* unsigned subtraction is correct even after head wraps past 2^32 */
static uint32_t rb_count(const rb_t *rb) { return rb->head - rb->tail; }
static uint32_t rb_space(const rb_t *rb) { return RB_SIZE - rb_count(rb); }
static bool rb_empty(const rb_t *rb) { return rb_count(rb) == 0; }
static bool rb_full(const rb_t *rb)  { return rb_count(rb) == RB_SIZE; }

static bool rb_push(rb_t *rb, uint8_t b)
{
    if (rb_full(rb))
        return false;                    /* drop-new policy: caller decides */
    rb->buf[rb->head & RB_MASK] = b;
    rb->head++;
    return true;
}

static bool rb_pop(rb_t *rb, uint8_t *out)
{
    if (rb_empty(rb))
        return false;
    *out = rb->buf[rb->tail & RB_MASK];
    rb->tail++;
    return true;
}

static bool rb_peek(const rb_t *rb, uint8_t *out)
{
    if (rb_empty(rb))
        return false;
    *out = rb->buf[rb->tail & RB_MASK];
    return true;
}

/* Bulk write: at most two memcpy calls (before and after the wrap point). */
static size_t rb_write(rb_t *rb, const uint8_t *src, size_t len)
{
    uint32_t n = rb_space(rb);
    if (len < n)
        n = (uint32_t)len;
    uint32_t idx = rb->head & RB_MASK;
    uint32_t first = RB_SIZE - idx;      /* contiguous room until end of array */
    if (first > n)
        first = n;
    memcpy(&rb->buf[idx], src, first);
    memcpy(&rb->buf[0], src + first, n - first);
    rb->head += n;
    return n;
}

static size_t rb_read(rb_t *rb, uint8_t *dst, size_t len)
{
    uint32_t n = rb_count(rb);
    if (len < n)
        n = (uint32_t)len;
    uint32_t idx = rb->tail & RB_MASK;
    uint32_t first = RB_SIZE - idx;
    if (first > n)
        first = n;
    memcpy(dst, &rb->buf[idx], first);
    memcpy(dst + first, &rb->buf[0], n - first);
    rb->tail += n;
    return n;
}

int main(void)
{
    rb_t rb;
    uint8_t v;

    rb_init(&rb);
    assert(rb_empty(&rb) && !rb_pop(&rb, &v) && !rb_peek(&rb, &v));

    for (uint8_t i = 0; i < RB_SIZE; i++)
        assert(rb_push(&rb, i));
    assert(rb_full(&rb) && !rb_push(&rb, 99));          /* full: all 16 slots usable */
    assert(rb_peek(&rb, &v) && v == 0);

    for (uint8_t i = 0; i < RB_SIZE; i++)
        assert(rb_pop(&rb, &v) && v == i);
    assert(rb_empty(&rb));

    /* wraparound with bulk ops, many laps */
    uint8_t in[11], out[11], next_in = 0, next_out = 0;
    for (int lap = 0; lap < 1000; lap++) {
        for (size_t i = 0; i < sizeof in; i++)
            in[i] = next_in++;
        size_t w = rb_write(&rb, in, sizeof in);
        next_in = (uint8_t)(next_in - (sizeof in - w));  /* rewind what didn't fit */
        size_t r = rb_read(&rb, out, 7);
        for (size_t i = 0; i < r; i++)
            assert(out[i] == next_out++);
        assert(rb_count(&rb) <= RB_SIZE);
    }

    /* index overflow: start near UINT32_MAX so head wraps to 0 */
    rb.head = rb.tail = UINT32_MAX - 3;
    for (uint8_t i = 0; i < 10; i++)
        assert(rb_push(&rb, i));
    assert(rb_count(&rb) == 10);
    for (uint8_t i = 0; i < 10; i++)
        assert(rb_pop(&rb, &v) && v == i);
    assert(rb_empty(&rb));

    puts("ring_buffer: PASS");
    return 0;
}
