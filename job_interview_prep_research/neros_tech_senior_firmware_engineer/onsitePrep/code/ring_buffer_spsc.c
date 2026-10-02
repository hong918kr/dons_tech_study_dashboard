/* Lock-free SPSC ring (ISR producer -> main-loop consumer) with C11 acquire/release atomics. */
/*
 * Why it works without a lock:
 *   - Only the producer writes head, only the consumer writes tail (single writer per index).
 *   - Producer: store data, THEN store head with release -> data is visible before the new head.
 *   - Consumer: load head with acquire, THEN read data -> it never reads a slot before it's written.
 *   - On a single-core Cortex-M the ISR and main loop never run truly in parallel, but the
 *     compiler can still reorder or cache values; the atomics stop that (and emit DMB where needed
 *     on multi-core / weakly ordered CPUs). `volatile` alone does not order the data writes.
 *   - 32-bit aligned loads/stores are single-copy atomic on Cortex-M, so this is lock-free there.
 */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define Q_SIZE 1024u
#define Q_MASK (Q_SIZE - 1u)
_Static_assert((Q_SIZE & Q_MASK) == 0, "power of 2");

typedef struct {
    uint32_t buf[Q_SIZE];
    _Atomic uint32_t head;               /* written only by producer */
    _Atomic uint32_t tail;               /* written only by consumer */
} spsc_t;

/* producer side (e.g. UART RX ISR) */
static bool spsc_push(spsc_t *q, uint32_t v)
{
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);   /* own index */
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);   /* slot freed? */
    if (h - t == Q_SIZE)
        return false;                    /* full: drop new, count it in real code */
    q->buf[h & Q_MASK] = v;
    atomic_store_explicit(&q->head, h + 1, memory_order_release);        /* publish */
    return true;
}

/* consumer side (main loop / task) */
static bool spsc_pop(spsc_t *q, uint32_t *out)
{
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);   /* see published data */
    if (h == t)
        return false;
    *out = q->buf[t & Q_MASK];
    atomic_store_explicit(&q->tail, t + 1, memory_order_release);        /* give slot back */
    return true;
}

#define N_ITEMS 10000000u
static spsc_t g_q;

static void *producer(void *arg)
{
    (void)arg;
    for (uint32_t i = 0; i < N_ITEMS;) {
        if (spsc_push(&g_q, i))
            i++;
    }
    return NULL;
}

static void *consumer(void *arg)
{
    uint32_t expect = 0, v;
    while (expect < N_ITEMS) {
        if (spsc_pop(&g_q, &v)) {
            if (v != expect) {
                *(int *)arg = 1;         /* sequence broken: lost / duplicated / torn */
                return NULL;
            }
            expect++;
        }
    }
    return NULL;
}

int main(void)
{
    uint32_t v;
    atomic_init(&g_q.head, 0);
    atomic_init(&g_q.tail, 0);

    /* single-thread sanity: full / empty */
    for (uint32_t i = 0; i < Q_SIZE; i++)
        assert(spsc_push(&g_q, i));
    assert(!spsc_push(&g_q, 0));
    for (uint32_t i = 0; i < Q_SIZE; i++)
        assert(spsc_pop(&g_q, &v) && v == i);
    assert(!spsc_pop(&g_q, &v));

    /* two threads hammering: every value must arrive exactly once, in order */
    int bad = 0;
    pthread_t p, c;
    pthread_create(&c, NULL, consumer, &bad);
    pthread_create(&p, NULL, producer, NULL);
    pthread_join(p, NULL);
    pthread_join(c, NULL);
    assert(!bad);
    assert(!spsc_pop(&g_q, &v));

    printf("ring_buffer_spsc: PASS (%u items across 2 threads)\n", N_ITEMS);
    return 0;
}
