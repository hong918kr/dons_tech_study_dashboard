/* Fixed-size element ring for message structs (memcpy of elem_size), statically allocated. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t *storage;                    /* capacity * elem_size bytes, caller-owned */
    size_t   elem_size;
    uint32_t capacity;                   /* power of 2 */
    uint32_t head, tail;
} ring_t;

/* No malloc: the macro creates the backing array and the control block together. */
#define RING_DEFINE(name, type, cap)                                         \
    _Static_assert(((cap) & ((cap) - 1)) == 0, #name ": cap must be 2^n");  \
    static uint8_t name##_storage[(cap) * sizeof(type)];                    \
    static ring_t name = { name##_storage, sizeof(type), (cap), 0, 0 }

static uint32_t ring_count(const ring_t *r) { return r->head - r->tail; }

static bool ring_put(ring_t *r, const void *elem)
{
    if (ring_count(r) == r->capacity)
        return false;
    uint32_t idx = r->head & (r->capacity - 1);
    memcpy(r->storage + (size_t)idx * r->elem_size, elem, r->elem_size);
    r->head++;
    return true;
}

static bool ring_get(ring_t *r, void *elem)
{
    if (ring_count(r) == 0)
        return false;
    uint32_t idx = r->tail & (r->capacity - 1);
    memcpy(elem, r->storage + (size_t)idx * r->elem_size, r->elem_size);
    r->tail++;
    return true;
}

/* zero-copy peek: pointer into the slot, valid until the next ring_get */
static const void *ring_front(const ring_t *r)
{
    if (ring_count(r) == 0)
        return NULL;
    return r->storage + (size_t)(r->tail & (r->capacity - 1)) * r->elem_size;
}

typedef struct {
    uint32_t timestamp_us;
    uint16_t msg_id;
    int16_t  gyro[3];
} imu_msg_t;

RING_DEFINE(g_imu_q, imu_msg_t, 4);

int main(void)
{
    imu_msg_t m, out;

    for (uint16_t i = 0; i < 4; i++) {
        m = (imu_msg_t){ .timestamp_us = 1000u * i, .msg_id = i, .gyro = { (int16_t)i, (int16_t)-i, 7 } };
        assert(ring_put(&g_imu_q, &m));
    }
    assert(!ring_put(&g_imu_q, &m));                     /* full */

    const imu_msg_t *front = ring_front(&g_imu_q);
    assert(front && front->msg_id == 0);

    for (int lap = 0; lap < 50; lap++) {                 /* wraparound with struct payloads */
        assert(ring_get(&g_imu_q, &out));
        m.msg_id = (uint16_t)(4 + lap);
        m.timestamp_us = 1000u * m.msg_id;
        assert(ring_put(&g_imu_q, &m));
    }
    uint16_t expect = 50;
    while (ring_get(&g_imu_q, &out)) {
        assert(out.msg_id == expect && out.timestamp_us == 1000u * expect && out.gyro[2] == 7);
        expect++;
    }
    assert(expect == 54 && ring_front(&g_imu_q) == NULL);

    puts("ring_buffer_generic: PASS");
    return 0;
}
