/* E7 - find the race: 3 ISR/main shared-data snippets, buggy vs fixed (simulated preemption) */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <assert.h>
#include <stdio.h>

/* ---- simulated interrupt controller (same as E6) ---- */
static uint32_t sim_primask;
static void   (*sim_pending)(void);
static void   (*sim_inject)(void);
static int      sim_inject_at = -1, sim_points;
static void sim_raise(void (*isr)(void)) { if (sim_primask) sim_pending = isr; else isr(); }
static void sim_preempt(void) { if (sim_points++ == sim_inject_at) sim_raise(sim_inject); }
static uint32_t irq_save(void) { uint32_t k = sim_primask; sim_primask = 1; return k; }
static void irq_restore(uint32_t k)
{
    sim_primask = k;
    if (!k && sim_pending) { void (*f)(void) = sim_pending; sim_pending = NULL; f(); }
}
#define PREEMPT_POINT() sim_preempt()

/* ==== MODEL ANSWER BEGIN ==== */
/* (1) shared counter: ISR adds, main subtracts. Bug: main's `-=` is load/sub/store. */
static volatile uint32_t g_rx_count;
void rx_isr(void) { g_rx_count++; }              /* ISR side OK: main can't preempt it */
void main_consume(uint32_t n)                    /* FIX: mask IRQs around main's RMW */
{
    uint32_t key = irq_save();
    uint32_t v = g_rx_count;                     /* load */
    PREEMPT_POINT();                             /* sim: IRQ here is now held pending */
    g_rx_count = v - n;                          /* store */
    irq_restore(key);
}

/* (2) completion flag. Bug: `static bool g_done;` -> compiler hoists the load, spins forever,
 *     and may move the g_result read above the flag check. FIX: volatile + compiler barrier. */
static volatile bool g_done;
static uint32_t      g_result;
void dma_isr(void)
{
    g_result = 0xC0FFEEu;                                 /* 1. payload */
    atomic_signal_fence(memory_order_release);            /* 2. compiler barrier */
    g_done = true;                                        /* 3. flag    */
}
bool wait_done(uint32_t spins, uint32_t *out)             /* bounded wait, never hang */
{
    while (!g_done) {
        if (spins-- == 0) return false;
        PREEMPT_POINT();
    }
    atomic_signal_fence(memory_order_acquire);
    *out = g_result;
    g_done = false;
    return true;
}

/* (3) 64-bit tick kept as two 32-bit words on a 32-bit CPU. Bug: read lo, hi -> torn value
 *     when lo wraps in between. FIX: hi-lo-hi retry (no IRQ masking needed). */
static volatile uint32_t g_tick_lo, g_tick_hi;
void tick_isr(void) { if (++g_tick_lo == 0) g_tick_hi++; }
uint64_t ticks64(void)
{
    uint32_t hi, lo;
    do {
        hi = g_tick_hi;
        PREEMPT_POINT();
        lo = g_tick_lo;
    } while (hi != g_tick_hi);                   /* hi changed -> lo may be torn -> retry */
    return ((uint64_t)hi << 32) | lo;
}
/* ==== MODEL ANSWER END ==== */

/* ---- the original buggy versions, for contrast ---- */
static void main_consume_buggy(uint32_t n)
{
    uint32_t v = g_rx_count;
    PREEMPT_POINT();                             /* ISR increment happens here ... */
    g_rx_count = v - n;                          /* ... and is overwritten */
}
static uint64_t ticks64_buggy(void)
{
    uint32_t lo = g_tick_lo;
    PREEMPT_POINT();
    uint32_t hi = g_tick_hi;
    return ((uint64_t)hi << 32) | lo;
}
static void arm(void (*isr)(void), int at) { sim_inject = isr; sim_inject_at = at; sim_points = 0; }

int main(void)
{
    /* (1) */
    g_rx_count = 5; arm(rx_isr, 0); main_consume_buggy(2);
    assert(g_rx_count == 3);                     /* should be 4: ISR's ++ lost */
    g_rx_count = 5; arm(rx_isr, 0); main_consume(2);
    assert(g_rx_count == 4 && sim_primask == 0); /* ISR pended, then ran */
    g_rx_count = 0; arm(NULL, -1); main_consume(0);
    assert(g_rx_count == 0);

    /* (2) */
    uint32_t r = 0;
    g_done = false; arm(dma_isr, 3);
    assert(wait_done(10, &r) && r == 0xC0FFEEu && !g_done);
    g_done = false; arm(NULL, -1);
    assert(!wait_done(5, &r));                   /* timeout path */
    g_done = false; arm(dma_isr, 0);
    assert(!wait_done(0, &r) || r == 0xC0FFEEu); /* spins=0: returns false before ever yielding */

    /* (3) torn read at the 32-bit boundary */
    g_tick_hi = 0; g_tick_lo = 0xFFFFFFFFu; arm(tick_isr, 0);
    uint64_t bad = ticks64_buggy();
    assert(bad == 0x100000000ull + 0xFFFFFFFFull);   /* hi=1 with old lo: ~2^32 ticks too far */
    g_tick_hi = 0; g_tick_lo = 0xFFFFFFFFu; arm(tick_isr, 0);
    uint64_t good = ticks64();
    assert(good == 0x100000000ull);              /* retried and got the consistent value */
    g_tick_hi = 7; g_tick_lo = 41; arm(tick_isr, 0);
    assert(ticks64() == ((7ull << 32) | 42u));   /* no wrap: hi same, lo updated, one pass */
    g_tick_hi = 0xFFFFFFFFu; g_tick_lo = 0xFFFFFFFFu; arm(NULL, -1);
    assert(ticks64() == UINT64_MAX);
    puts("E7 PASS");
    return 0;
}
