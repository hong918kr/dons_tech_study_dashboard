/* ringbuf.h — PUBLIC header of the ringbuf library (byte FIFO, 16 slots).
 * RB_HAVE_CRC (0/1) must be the SAME for the library and every consumer:
 * it changes the layout of rb_t, so it is a PUBLIC compile definition. */
#ifndef RINGBUF_H
#define RINGBUF_H

#include <stddef.h>
#include <stdint.h>

#define RB_CAP 16u                  /* power of two */

typedef struct {
    uint8_t  buf[RB_CAP];
    uint32_t head;                  /* free-running write count */
    uint32_t tail;                  /* free-running read count  */
#if RB_HAVE_CRC
    uint16_t crc;                   /* running CRC-16 of every byte ever put */
#endif
} rb_t;

void     rb_init(rb_t *rb);
int      rb_put(rb_t *rb, uint8_t b);      /* 0 = ok, -1 = full  */
int      rb_get(rb_t *rb, uint8_t *out);   /* 0 = ok, -1 = empty */
uint32_t rb_count(const rb_t *rb);
size_t   rb_struct_size(void);             /* sizeof(rb_t) as the LIBRARY saw it */

#if RB_HAVE_CRC
uint16_t rb_crc16(const uint8_t *p, size_t n);   /* CRC-16/CCITT-FALSE */
uint16_t rb_running_crc(const rb_t *rb);
#endif

#endif
