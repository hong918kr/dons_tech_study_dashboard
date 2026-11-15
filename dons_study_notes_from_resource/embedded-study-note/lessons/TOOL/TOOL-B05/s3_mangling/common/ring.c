/* ring.c — C 로 쓴 바이트 링 버퍼 (벤더 HAL/레거시 C 모듈 역할) */
#include "ring.h"

void ring_init(ring_t *r, uint8_t *buf, size_t cap)
{
    r->buf = buf;
    r->cap = cap;
    r->head = r->tail = r->count = 0;
}

bool ring_push(ring_t *r, uint8_t b)
{
    if (r->count == r->cap) {
        ring_overflow_hook(r, b);     /* 앱이 구현하는 콜백 */
        return false;
    }
    r->buf[r->head] = b;
    r->head = (r->head + 1) % r->cap;
    r->count++;
    return true;
}

bool ring_pop(ring_t *r, uint8_t *out)
{
    if (r->count == 0)
        return false;
    *out = r->buf[r->tail];
    r->tail = (r->tail + 1) % r->cap;
    r->count--;
    return true;
}
