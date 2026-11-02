/* E6 - event flags: ISR sets bits, main does atomic fetch-and-clear.
 * IRQ masking is SIMULATED: irq_save()/irq_restore() model Cortex-M PRIMASK, and
 * PREEMPT_POINT() is where a test may "fire" an interrupt in the middle of main's code. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <assert.h>
#include <stdio.h>

/* ---- simulated interrupt controller (given) ---- */
static uint32_t sim_primask;                     /* 1 = IRQs masked (cpsid i) */
static void   (*sim_pending)(void);              /* IRQ raised while masked */
static void   (*sim_inject)(void);               /* ISR the test wants to fire */
static int      sim_inject_at = -1, sim_points;
static void sim_raise(void (*isr)(void)) { if (sim_primask) sim_pending = isr; else isr(); }
static void sim_preempt(void) { if (sim_points++ == sim_inject_at) sim_raise(sim_inject); }
static uint32_t irq_save(void) { uint32_t k = sim_primask; sim_primask = 1; return k; }
static void irq_restore(uint32_t k)
{
    sim_primask = k;
    if (!k && sim_pending) { void (*f)(void) = sim_pending; sim_pending = NULL; f(); } /* tail-chain */
}
#define PREEMPT_POINT() sim_preempt()

/* ==== MODEL ANSWER BEGIN ==== */
static volatile uint32_t g_events;               /* bit i = event i pending */

void evt_set_from_isr(uint32_t mask)             /* ISR side: only SETS bits */
{
    g_events |= mask;      /* RMW is OK here: main can't preempt an ISR (single core).
                              If a higher-priority ISR also sets bits -> use the atomic version. */
}

uint32_t evt_take(uint32_t mask)                 /* main: fetch-and-clear `mask` bits */
{
    uint32_t key = irq_save();                   /* save PRIMASK, then cpsid i */
    uint32_t got = g_events & mask;
    PREEMPT_POINT();                             /* sim: an IRQ here is held pending */
    g_events &= ~got;                            /* clear ONLY what we return */
    irq_restore(key);                            /* restore, don't blindly enable */
    return got;                                  /* critical section = 3 instructions */
}

/* lock-free alternative: C11 atomics (Cortex-M3+: LDREX/STREX loop; M0: not available) */
static _Atomic uint32_t g_events_a;
void evt_set_a(uint32_t mask)
{
    atomic_fetch_or_explicit(&g_events_a, mask, memory_order_release);
}
uint32_t evt_take_a(uint32_t mask)
{
    return atomic_fetch_and_explicit(&g_events_a, ~mask, memory_order_acquire) & mask;
}
/* ==== MODEL ANSWER END ==== */

/* the classic bug, for contrast: read, (IRQ), then blind clear */
static uint32_t evt_take_buggy(void)
{
    uint32_t got = g_events;
    PREEMPT_POINT();                             /* IRQ runs for real here */
    g_events = 0;                                /* wipes the bit the ISR just set */
    return got;
}

#define EV_RX   (1u << 0)
#define EV_TX   (1u << 1)
#define EV_ERR  (1u << 31)
static void isr_rx(void)  { evt_set_from_isr(EV_RX); }
static void isr_err(void) { evt_set_from_isr(EV_ERR); }
static void arm(void (*isr)(void), int at) { sim_inject = isr; sim_inject_at = at; sim_points = 0; }

int main(void)
{
    /* basic */
    assert(evt_take(0xFFFFFFFFu) == 0);
    evt_set_from_isr(EV_RX | EV_TX);
    evt_set_from_isr(EV_RX);                     /* duplicate set coalesces (it's a flag, not a count) */
    assert(evt_take(EV_TX) == EV_TX);            /* take only TX */
    assert(g_events == EV_RX);                   /* RX untouched */
    assert(evt_take(0xFFFFFFFFu) == EV_RX && g_events == 0);
    evt_set_from_isr(EV_ERR);
    assert(evt_take(EV_ERR) == EV_ERR);          /* bit 31 */

    /* buggy version loses an event that arrives between read and clear */
    g_events = EV_TX;
    arm(isr_rx, 0);
    assert(evt_take_buggy() == EV_TX);
    assert(g_events == 0);                       /* EV_RX LOST */

    /* fixed version: IRQ arriving inside the critical section is pended, then delivered */
    g_events = EV_TX;
    arm(isr_rx, 0);
    assert(evt_take(0xFFFFFFFFu) == EV_TX);
    assert(g_events == EV_RX && sim_primask == 0);   /* delivered after restore, not lost */
    assert(evt_take(EV_RX) == EV_RX);

    /* nested: irq_save inside an already-masked region must not re-enable */
    uint32_t outer = irq_save();
    arm(isr_err, 0);
    evt_set_from_isr(EV_TX);
    assert(evt_take(EV_TX) == EV_TX);
    assert(sim_primask == 1 && sim_pending == isr_err);  /* still masked after inner restore */
    irq_restore(outer);
    assert(g_events == EV_ERR && evt_take(EV_ERR) == EV_ERR);

    /* atomic version */
    assert(evt_take_a(0xFFFFFFFFu) == 0);
    evt_set_a(EV_RX | EV_ERR);
    assert(evt_take_a(EV_RX) == EV_RX && evt_take_a(0xFFFFFFFFu) == EV_ERR && evt_take_a(~0u) == 0);
    puts("E6 PASS");
    return 0;
}
