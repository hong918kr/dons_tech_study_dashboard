/* Software timers on a 32-bit tick: one-shot/periodic, wrap-safe comparisons, run callbacks from the main loop (not the ISR). */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_TIMERS 4u

typedef void (*timer_cb)(void *ctx);

typedef struct {
    bool     active;
    uint32_t deadline, period;                  /* period 0 = one-shot */
    timer_cb cb;
    void    *ctx;
} soft_timer_t;

static soft_timer_t timers[MAX_TIMERS];
static volatile uint32_t g_tick;                /* incremented by the SysTick ISR on target */

static bool tick_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;      /* wrap-safe while intervals < 2^31 ticks */
}

static int timer_start(uint32_t delay, uint32_t period, timer_cb cb, void *ctx)
{
    for (size_t i = 0; i < MAX_TIMERS; i++) {
        if (!timers[i].active) {
            timers[i] = (soft_timer_t){true, g_tick + delay, period, cb, ctx};
            return (int)i;
        }
    }
    return -1;                                  /* table full */
}

static void timer_stop(int id) { if (id >= 0 && (size_t)id < MAX_TIMERS) timers[id].active = false; }

static void timers_poll(void)                   /* call from the main loop */
{
    uint32_t now = g_tick;
    for (size_t i = 0; i < MAX_TIMERS; i++) {
        soft_timer_t *t = &timers[i];
        if (!t->active || !tick_reached(now, t->deadline))
            continue;
        if (t->period)
            t->deadline += t->period;           /* from the deadline, not from now → no drift */
        else
            t->active = false;
        t->cb(t->ctx);
    }
}

static void count_cb(void *ctx) { (*(int *)ctx)++; }

static void run(uint32_t ticks)
{
    for (uint32_t i = 0; i < ticks; i++) {
        g_tick++;                               /* simulated SysTick */
        timers_poll();
    }
}

int main(void)
{
    int once = 0, periodic = 0;
    g_tick = UINT32_MAX - 5;                    /* start just before the 32-bit wrap */
    int a = timer_start(10, 0, count_cb, &once);
    int b = timer_start(3, 3, count_cb, &periodic);
    assert(a >= 0 && b >= 0);

    run(9);
    assert(once == 0 && periodic == 3);         /* fired at +3, +6, +9 across the wrap */
    run(1);
    assert(once == 1);                          /* one-shot at +10 */
    run(20);
    assert(once == 1 && periodic == 10);        /* +12 … +30 */
    timer_stop(b);
    run(10);
    assert(periodic == 10);                     /* stopped */

    for (int i = 0; i < (int)MAX_TIMERS; i++)
        timers[i].active = false;
    for (int i = 0; i < (int)MAX_TIMERS; i++)
        assert(timer_start(1, 0, count_cb, &once) >= 0);
    assert(timer_start(1, 0, count_cb, &once) == -1);                  /* table full is reported */

    puts("soft_timer: PASS");
    return 0;
}
