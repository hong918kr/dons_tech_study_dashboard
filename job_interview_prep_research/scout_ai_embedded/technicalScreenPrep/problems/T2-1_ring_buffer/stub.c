/*
 * T2-1  SPSC Ring Buffer of Temperature Samples
 *
 * A temperature sensor raises an interrupt every 10 ms. The ISR reads the sample
 * (int16_t, 0.1 degC) and pushes it into a ring buffer; the control task pops samples
 * later. Implement the ring buffer.
 *
 * Definitions (agreed with the interviewer):
 *   - Capacity RB_SIZE is a power of two. ALL RB_SIZE slots are usable.
 *   - head and tail are FREE-RUNNING uint32_t counters (never reduced modulo RB_SIZE).
 *     Slot index = counter & RB_MASK.  count = head - tail (unsigned subtraction).
 *     Empty: count == 0.  Full: count == RB_SIZE.
 *   - Single producer (ISR) writes only head; single consumer (task) writes only tail.
 *   - rb_push on a full buffer returns false and drops the NEW sample.
 *   - rb_pop / rb_peek on an empty buffer return false and leave *out untouched.
 *   - No malloc, no locks.
 *
 * Build & run:  ./run.sh T2-1
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define RB_SIZE 8u                        /* must be a power of two */
#define RB_MASK (RB_SIZE - 1u)

typedef struct {
    int16_t buf[RB_SIZE];
    volatile uint32_t head;               /* written only by the producer (ISR)  */
    volatile uint32_t tail;               /* written only by the consumer (task) */
} TempRing;

void rb_init(TempRing *rb)
{
    /* TODO: your code here */
    (void)rb;
}

uint32_t rb_count(const TempRing *rb)
{
    /* TODO: your code here */
    (void)rb;
    return 0;
}

bool rb_push(TempRing *rb, int16_t v)     /* producer side */
{
    /* TODO: your code here */
    (void)rb; (void)v;
    return false;
}

bool rb_peek(const TempRing *rb, int16_t *out)
{
    /* TODO: your code here */
    (void)rb; (void)out;
    return false;
}

bool rb_pop(TempRing *rb, int16_t *out)   /* consumer side */
{
    /* TODO: your code here */
    (void)rb; (void)out;
    return false;
}

int main(void)
{
    TempRing rb = {0};
    int16_t v;

    /* 1. basic FIFO order + count */
    rb_init(&rb);
    CHECK(rb_push(&rb, 801) && rb_push(&rb, 802) && rb_push(&rb, 803));
    CHECK(rb_count(&rb) == 3);
    v = 0; CHECK(rb_pop(&rb, &v) && v == 801);
    v = 0; CHECK(rb_pop(&rb, &v) && v == 802);
    CHECK(rb_count(&rb) == 1);

    /* 2. empty: pop/peek fail and do not touch *out */
    rb_init(&rb);
    v = 1234;
    CHECK(!rb_pop(&rb, &v) && v == 1234);
    CHECK(!rb_peek(&rb, &v) && v == 1234);
    CHECK(rb_count(&rb) == 0);

    /* 3. full: all RB_SIZE slots usable, the next push is rejected */
    rb_init(&rb);
    int ok = 1;
    for (int i = 0; i < (int)RB_SIZE; i++) ok &= rb_push(&rb, (int16_t)(801 + i));
    CHECK(ok && rb_count(&rb) == RB_SIZE);
    CHECK(!rb_push(&rb, 809));
    v = 0; CHECK(rb_pop(&rb, &v) && v == 801);      /* oldest survives, 809 was dropped */
    CHECK(rb_push(&rb, 809));                       /* one slot freed -> push works again */
    CHECK(rb_count(&rb) == RB_SIZE && rb.head == 9 && rb.tail == 1);

    /* 4. peek does not remove */
    rb_init(&rb);
    CHECK(rb_push(&rb, -55));
    v = 0; CHECK(rb_peek(&rb, &v) && v == -55);
    CHECK(rb_count(&rb) == 1);
    v = 0; CHECK(rb_pop(&rb, &v) && v == -55);
    CHECK(rb_count(&rb) == 0);

    /* 5. slot index wraps: 100 rounds of push 5 / pop 5 keep FIFO order */
    rb_init(&rb);
    ok = 1;
    for (int r = 0; r < 100; r++) {
        for (int i = 0; i < 5; i++) ok &= rb_push(&rb, (int16_t)(r * 5 + i));
        for (int i = 0; i < 5; i++) { v = -1; ok &= rb_pop(&rb, &v) && v == r * 5 + i; }
    }
    CHECK(ok && rb_count(&rb) == 0 && rb.head == 500);

    /* 6. the uint32_t counters themselves wrap past UINT32_MAX */
    rb_init(&rb);
    rb.head = rb.tail = UINT32_MAX - 2u;
    ok = 1;
    for (int i = 0; i < 6; i++) ok &= rb_push(&rb, (int16_t)(900 + i));
    CHECK(ok && rb_count(&rb) == 6 && rb.head == 3u);   /* head wrapped to 3, tail did not */
    for (int i = 0; i < 6; i++) { v = 0; ok &= rb_pop(&rb, &v) && v == 900 + i; }
    CHECK(ok && rb_count(&rb) == 0);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
