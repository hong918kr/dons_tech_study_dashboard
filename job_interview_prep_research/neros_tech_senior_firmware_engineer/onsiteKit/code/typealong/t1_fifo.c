/* T1 fixed FIFO -- "ISR hands bytes, main loop drains, memory fixed, what if main falls behind?" */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint8_t *buf;            /* caller-owned storage, no malloc */
    size_t cap;
    size_t head, tail, count; /* head: next write, tail: next read */
    bool overwrite;
    uint32_t dropped, overwritten;
} fifo_t;

void fifo_init(fifo_t *f, uint8_t *storage, size_t cap, bool overwrite)
{
    f->buf = storage;
    f->cap = cap;
    f->head = f->tail = f->count = 0;
    f->overwrite = overwrite;
    f->dropped = f->overwritten = 0;
}

bool fifo_put(fifo_t *f, uint8_t x)
{
    if (f->count == f->cap) {                 /* OVERFLOW: decide, then count */
        if (!f->overwrite) {
            f->dropped++;
            return false;
        }
        f->tail = (f->tail + 1) % f->cap;     /* drop oldest: move tail too */
        f->count--;
        f->overwritten++;
    }
    f->buf[f->head] = x;
    f->head = (f->head + 1) % f->cap;
    f->count++;
    return true;
}

bool fifo_get(fifo_t *f, uint8_t *out)
{
    if (f->count == 0)
        return false;
    *out = f->buf[f->tail];
    f->tail = (f->tail + 1) % f->cap;
    f->count--;
    return true;
}

bool fifo_put_all(fifo_t *f, const uint8_t *items, size_t len)
{
    if (len > f->cap - f->count) {            /* subtract, never "count + len" (can wrap) */
        f->dropped += (uint32_t)len;
        return false;
    }
    for (size_t i = 0; i < len; i++)
        fifo_put(f, items[i]);
    return true;
}

int main(void)
{
    uint8_t mem[3], b;
    fifo_t f;
    fifo_init(&f, mem, sizeof mem, false);
    assert(!fifo_get(&f, &b));                               /* empty */
    assert(fifo_put(&f, 1) && fifo_put(&f, 2) && fifo_put(&f, 3));
    assert(!fifo_put(&f, 4) && f.dropped == 1);              /* full -> reject + count */
    for (uint8_t want = 1; want <= 3; want++)
        assert(fifo_get(&f, &b) && b == want);               /* old data and order kept */

    fifo_init(&f, mem, sizeof mem, true);
    for (uint8_t i = 1; i <= 5; i++)
        fifo_put(&f, i);
    assert(f.overwritten == 2);
    for (uint8_t want = 3; want <= 5; want++)
        assert(fifo_get(&f, &b) && b == want);

    uint8_t mem5[5];
    const uint8_t abc[] = {1, 2, 3}, xyz[] = {4, 5, 6};
    fifo_init(&f, mem5, sizeof mem5, false);
    assert(fifo_put_all(&f, abc, 3) && !fifo_put_all(&f, xyz, 3) && f.count == 3);
    assert(!fifo_put_all(&f, abc, SIZE_MAX));                /* huge len must not wrap the check */
    for (uint8_t i = 0; i < 20; i++) {                       /* wrap many laps */
        assert(fifo_get(&f, &b));
        assert(fifo_put(&f, i));
    }
    puts("t1 OK");
    return 0;
}
