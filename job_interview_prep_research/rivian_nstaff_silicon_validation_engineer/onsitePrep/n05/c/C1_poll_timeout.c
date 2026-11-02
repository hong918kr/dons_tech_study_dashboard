/* C1 - poll status bits with a wrap-safe 32-bit tick timeout. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ---- HW model (test only): STATUS becomes ready after N reads; ticks advance per call ---- */
#define STATUS_OFF 0x04u
#define ST_READY   (1u << 0)
#define ST_BUSY    (1u << 1)
static uint32_t m_reads, m_ready_after, m_tick, m_tick_step, m_preempt;
uint32_t reg_read(uint32_t off)
{
    (void)off;
    m_reads++;
    if (m_reads == 1) m_tick += m_preempt;    /* simulate a long ISR/preemption after read #1 */
    return (m_reads >= m_ready_after) ? ST_READY : ST_BUSY;   /* BUSY until READY */
}
uint32_t ticks_now(void) { uint32_t t = m_tick; m_tick += m_tick_step; return t; }
static void model_reset(uint32_t ready_after, uint32_t tick0, uint32_t step)
{
    m_reads = 0; m_ready_after = ready_after; m_tick = tick0; m_tick_step = step; m_preempt = 0;
}

/* ==== ANSWER BEGIN ==== */
#define POLL_OK       0
#define POLL_EINVAL  (-1)
#define POLL_TIMEOUT (-2)

uint32_t reg_read(uint32_t off);   /* 32-bit MMIO read (side effects allowed) */
uint32_t ticks_now(void);          /* free-running 32-bit tick counter, wraps */

/* Wait until (reg & mask) == want, or `timeout` ticks elapse. */
int wait_bits(uint32_t off, uint32_t mask, uint32_t want, uint32_t timeout)
{
    if (want & ~mask)
        return POLL_EINVAL;                 /* can never match */
    uint32_t start = ticks_now();
    for (;;) {
        /* sample time BEFORE the read: after a long preemption we still get one last read */
        bool expired = (uint32_t)(ticks_now() - start) >= timeout;   /* unsigned: wrap-safe */
        if ((reg_read(off) & mask) == want)
            return POLL_OK;
        if (expired)
            return POLL_TIMEOUT;
    }
}
/* ==== ANSWER END ==== */

/* Subtler bug: read first, check time after -> false timeout after a preemption. */
static int wait_bits_check_after(uint32_t off, uint32_t mask, uint32_t want, uint32_t timeout)
{
    uint32_t start = ticks_now();
    while ((reg_read(off) & mask) != want)
        if ((uint32_t)(ticks_now() - start) >= timeout)
            return POLL_TIMEOUT;
    return POLL_OK;
}

/* The classic bug, kept here to prove it fails at wrap. */
static int wait_bits_naive(uint32_t off, uint32_t mask, uint32_t want, uint32_t timeout)
{
    uint32_t deadline = ticks_now() + timeout;    /* wraps to a small number */
    while ((reg_read(off) & mask) != want)
        if (ticks_now() >= deadline)              /* true immediately after wrap */
            return POLL_TIMEOUT;
    return POLL_OK;
}

int main(void)
{
    /* normal: ready on 3rd read */
    model_reset(3, 1000, 1);
    assert(wait_bits(STATUS_OFF, ST_READY, ST_READY, 100) == POLL_OK);
    assert(m_reads == 3);

    /* wait for a bit to CLEAR: mask=BUSY, want=0 */
    model_reset(5, 0, 1);
    assert(wait_bits(STATUS_OFF, ST_BUSY, 0, 100) == POLL_OK);
    assert(m_reads == 5);

    /* wrap-around: start near 0xFFFFFFFF, ready on 20th read, timeout 100 ticks */
    model_reset(20, 0xFFFFFFF0u, 1);
    assert(wait_bits(STATUS_OFF, ST_READY, ST_READY, 100) == POLL_OK);
    model_reset(20, 0xFFFFFFF0u, 1);
    assert(wait_bits_naive(STATUS_OFF, ST_READY, ST_READY, 100) == POLL_TIMEOUT);  /* bug shown */

    /* never ready: bounded number of reads (timeout 10 ticks, 1 tick per ticks_now call) */
    model_reset(UINT32_MAX, 0xFFFFFFFAu, 1);
    assert(wait_bits(STATUS_OFF, ST_READY, ST_READY, 10) == POLL_TIMEOUT);
    assert(m_reads == 10);

    /* timeout 0: still exactly one read (already-ready register passes) */
    model_reset(1, 0, 1);
    assert(wait_bits(STATUS_OFF, ST_READY, ST_READY, 0) == POLL_OK && m_reads == 1);
    model_reset(2, 0, 1);
    assert(wait_bits(STATUS_OFF, ST_READY, ST_READY, 0) == POLL_TIMEOUT && m_reads == 1);

    /* long preemption right after read #1; HW got ready meanwhile (ready on read #2) */
    model_reset(2, 0, 1); m_preempt = 1000;
    assert(wait_bits(STATUS_OFF, ST_READY, ST_READY, 10) == POLL_OK && m_reads == 2);
    model_reset(2, 0, 1); m_preempt = 1000;
    assert(wait_bits_check_after(STATUS_OFF, ST_READY, ST_READY, 10) == POLL_TIMEOUT);  /* false timeout */

    /* bad argument: want has bits outside mask -> no bus access */
    model_reset(1, 0, 1);
    assert(wait_bits(STATUS_OFF, ST_READY, ST_READY | ST_BUSY, 10) == POLL_EINVAL);
    assert(m_reads == 0);

    printf("C1 PASS\n");
    return 0;
}
