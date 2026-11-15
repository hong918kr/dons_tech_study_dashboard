/* ring.h — C 링 버퍼 API. C 와 C++ 양쪽에서 include 된다. */
#ifndef RING_H
#define RING_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ring {
    uint8_t *buf;
    size_t cap, head, tail, count;
} ring_t;

void ring_init(ring_t *r, uint8_t *buf, size_t cap);
bool ring_push(ring_t *r, uint8_t b);
bool ring_pop(ring_t *r, uint8_t *out);
void ring_overflow_hook(ring_t *r, uint8_t b);   /* 앱이 정의 */

#ifdef __cplusplus
}
#endif
#endif
