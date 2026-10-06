/* Byte ring buffer: power-of-2 capacity, free-running head/tail (code under test for all C examples). */
#ifndef RB_H
#define RB_H
#include <stdbool.h>
#include <stdint.h>

#define RB_SIZE 16u

typedef struct {
    uint8_t  buf[RB_SIZE];
    uint32_t head, tail;
} rb_t;

void     rb_init(rb_t *rb);
bool     rb_push(rb_t *rb, uint8_t b);
bool     rb_pop(rb_t *rb, uint8_t *out);
uint32_t rb_count(const rb_t *rb);
#endif
