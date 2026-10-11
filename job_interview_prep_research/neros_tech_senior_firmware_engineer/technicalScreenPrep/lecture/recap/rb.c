#include "rb.h"

// --- impl ---
void rb_init(rb_t *rb, uint8_t *rb_buf, uint8_t capacity)
{
    rb->storage = rb_buf;
    rb->capacity = capacity;
    rb->head = rb->tail = rb->count = 0;
}

bool rb_is_empty(const rb_t *rb) { return rb->count == 0; }
bool rb_is_full(const rb_t *rb)  { return rb->count == rb->capacity; }

bool rb_push_byte(rb_t *rb, uint8_t byte)
{
    if (rb_is_full(rb))
        return false;
    rb->storage[rb->head] = byte;
    rb->head = (rb->head + 1) % rb->capacity;
    rb->count++;
    return true;
}

bool rb_pop_byte(rb_t *rb, uint8_t *out)
{
    if (rb_is_empty(rb))
        return false;
    *out = rb->storage[rb->tail];
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->count--;
    return true;
}

bool rb_push(rb_t *rb, const uint8_t *items, size_t len)
{
    if (len > rb->capacity - rb->count)          /* 남은 자리 < len → 전부 거절 (부분 쓰기 없음) */
        return false;
    for (size_t i = 0; i < len; i++)
        rb_push_byte(rb, items[i]);
    return true;
}

bool rb_pop(rb_t *rb, uint8_t *items, size_t len)
{
    if (len > rb->count)                         /* 들어 있는 것 < len → 전부 거절 */
        return false;
    for (size_t i = 0; i < len; i++)
        rb_pop_byte(rb, &items[i]);
    return true;
}

bool rb_peek(const rb_t *rb, uint8_t offset, uint8_t *out)
{
    if (offset >= rb->count)
        return false;
    *out = rb->storage[(rb->tail + offset) % rb->capacity];
    return true;
}
// --- end ---
