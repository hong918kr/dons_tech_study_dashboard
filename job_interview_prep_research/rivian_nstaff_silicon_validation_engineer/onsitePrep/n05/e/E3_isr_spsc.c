/* E3 - UART RX ISR (producer) -> main loop (consumer) SPSC ring.
 * Host build: C11 acquire/release atomics (data-race-free, so a pthread stress test is legal).
 * Cortex-M bare metal: idx_t = volatile uint32_t, LD_ACQ = load then __DMB(), ST_REL = __DMB() then store. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>

uint8_t uart_read_dr(void);                      /* test stub below */

/* ==== MODEL ANSWER BEGIN ==== */
typedef _Atomic uint32_t idx_t;                  /* bare metal: volatile uint32_t */
#define LD_OWN(x)    atomic_load_explicit(&(x), memory_order_relaxed)       /* own index  */
#define ST_OWN(x, v) atomic_store_explicit(&(x), (v), memory_order_relaxed)
#define LD_ACQ(x)    atomic_load_explicit(&(x), memory_order_acquire)       /* M: v=x; __DMB() */
#define ST_REL(x, v) atomic_store_explicit(&(x), (v), memory_order_release) /* M: __DMB(); x=v */

#define RX_N 16u                                 /* 2^k */
static uint8_t rx_buf[RX_N];
static idx_t   rx_head;                          /* written ONLY by the ISR (producer) */
static idx_t   rx_tail;                          /* written ONLY by main    (consumer) */
static idx_t   rx_overflow;                      /* written ONLY by the ISR */

void uart_rx_isr(void)                           /* producer: never blocks, never spins */
{
    uint8_t  b = uart_read_dr();                 /* read DR even if full: clears RXNE */
    uint32_t h = LD_OWN(rx_head);
    uint32_t t = LD_ACQ(rx_tail);                /* consumer is done with the slot we reuse */
    if (h - t == RX_N) {
        ST_OWN(rx_overflow, LD_OWN(rx_overflow) + 1u);   /* drop newest, count it */
        return;
    }
    rx_buf[h & (RX_N - 1u)] = b;                 /* 1. write data                 */
    ST_REL(rx_head, h + 1u);                     /* 2. barrier, then publish head */
}

int rx_getc(void)                                /* consumer: byte, or -1 if empty */
{
    uint32_t t = LD_OWN(rx_tail);
    uint32_t h = LD_ACQ(rx_head);                /* see head first, THEN read data */
    if (h == t)
        return -1;
    uint8_t b = rx_buf[t & (RX_N - 1u)];
    ST_REL(rx_tail, t + 1u);                     /* finish reading, then free slot */
    return b;
}
/* ==== MODEL ANSWER END ==== */

/* ---- simulated UART data register ---- */
static uint8_t g_dr;                             /* deterministic tests */
static int     g_dr_reads;
uint8_t uart_read_dr(void) { g_dr_reads++; return g_dr; }

static void reset(uint32_t start) { ST_OWN(rx_head, start); ST_OWN(rx_tail, start); ST_OWN(rx_overflow, 0u); }

#define STRESS_N 2000000u
static void *producer(void *arg)
{
    (void)arg;
    for (uint32_t i = 0; i < STRESS_N; i++) {
        while (LD_OWN(rx_head) - LD_OWN(rx_tail) == RX_N) { }   /* "flow control": wait for space */
        g_dr = (uint8_t)(i * 7u + 3u);           /* only producer thread touches g_dr here */
        uart_rx_isr();
    }
    return NULL;
}

int main(void)
{
    /* deterministic: empty, fill, overflow, FIFO order, wrap */
    uint32_t starts[] = { 0u, 0xFFFFFFF8u };
    for (int s = 0; s < 2; s++) {
        reset(starts[s]);
        g_dr_reads = 0;
        assert(rx_getc() == -1);
        for (uint32_t i = 0; i < RX_N + 3; i++) { g_dr = (uint8_t)(0x40 + i); uart_rx_isr(); }
        assert(g_dr_reads == (int)(RX_N + 3));  /* DR read (IRQ acked) even when full */
        assert(LD_OWN(rx_overflow) == 3u);
        for (uint32_t i = 0; i < RX_N; i++) assert(rx_getc() == (int)(0x40 + i));
        assert(rx_getc() == -1);
        g_dr = 0xFF; uart_rx_isr();
        assert(rx_getc() == 0xFF);               /* 0xFF byte must not look like -1 */
        assert(LD_OWN(rx_head) == starts[s] + RX_N + 1u);
    }
    /* 2-thread stress: producer thread = ISR, main = consumer */
    reset(0xFFFFF000u);
    pthread_t th;
    assert(pthread_create(&th, NULL, producer, NULL) == 0);
    for (uint32_t i = 0; i < STRESS_N; ) {
        int c = rx_getc();
        if (c < 0) continue;
        assert(c == (uint8_t)(i * 7u + 3u));     /* order + visibility of data */
        i++;
    }
    pthread_join(th, NULL);
    assert(rx_getc() == -1 && LD_OWN(rx_overflow) == 0u);
    puts("E3 PASS");
    return 0;
}
