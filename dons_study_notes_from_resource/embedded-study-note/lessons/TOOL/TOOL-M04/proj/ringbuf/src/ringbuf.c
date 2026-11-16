/* ringbuf.c — 16-slot byte FIFO with free-running head/tail counters. */
#include <stdio.h>
#include <stdlib.h>
#include "rb_internal.h"   /* private: found via the library's PRIVATE include dir */

#if defined(RB_ASSERTS) && RB_ASSERTS
void rb_assert_fail(const char *file, int line)
{
    fprintf(stderr, "RB_ASSERT failed at %s:%d\n", file, line);
    abort();
}
#endif

void rb_init(rb_t *rb)
{
    rb->head = 0;
    rb->tail = 0;
#if RB_HAVE_CRC
    rb->crc = 0xFFFFu;
#endif
}

uint32_t rb_count(const rb_t *rb)
{
    return rb->head - rb->tail;            /* unsigned wrap is fine */
}

int rb_put(rb_t *rb, uint8_t b)
{
    if (rb_count(rb) == RB_CAP)
        return -1;
    rb->buf[rb_idx(rb->head)] = b;
    rb->head++;
#if RB_HAVE_CRC
    rb->crc = rb_crc_step(rb->crc, b);
#endif
    RB_ASSERT(rb_count(rb) <= RB_CAP);
    return 0;
}

int rb_get(rb_t *rb, uint8_t *out)
{
    if (rb_count(rb) == 0)
        return -1;
    *out = rb->buf[rb_idx(rb->tail)];
    rb->tail++;
    return 0;
}

size_t rb_struct_size(void)
{
    return sizeof(rb_t);
}
