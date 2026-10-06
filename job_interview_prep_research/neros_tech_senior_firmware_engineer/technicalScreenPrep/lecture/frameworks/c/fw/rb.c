#include "rb.h"

#define RB_MASK (RB_SIZE - 1u)

void rb_init(rb_t *rb) { rb->head = rb->tail = 0; }

uint32_t rb_count(const rb_t *rb) { return rb->head - rb->tail; }

bool rb_push(rb_t *rb, uint8_t b)
{
    if (rb_count(rb) == RB_SIZE)
        return false;
    rb->buf[rb->head & RB_MASK] = b;
    rb->head++;
    return true;
}

bool rb_pop(rb_t *rb, uint8_t *out)
{
    if (rb_count(rb) == 0)
        return false;
    *out = rb->buf[rb->tail & RB_MASK];
    rb->tail++;
    return true;
}
