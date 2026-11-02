/* E5 - software timer delta list: add(ticks) / tick() / expiry callback */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>

/* ==== MODEL ANSWER BEGIN ==== */
typedef void (*timer_cb_t)(void *arg);

typedef struct sw_timer {
    struct sw_timer *next;
    uint32_t   delta;          /* ticks AFTER the previous node expires */
    timer_cb_t cb;
    void      *arg;
} sw_timer_t;                  /* caller owns the node: no malloc */

static sw_timer_t *g_head;     /* sorted by expiry; head->delta = ticks until first */

void timer_add(sw_timer_t *t, uint32_t ticks, timer_cb_t cb, void *arg)
{
    sw_timer_t **pp = &g_head;
    if (ticks == 0)
        ticks = 1;                               /* fire on next tick, never "now" */
    while (*pp && (*pp)->delta <= ticks) {       /* <= : equal expiry stays FIFO */
        ticks -= (*pp)->delta;
        pp = &(*pp)->next;
    }
    t->delta = ticks; t->cb = cb; t->arg = arg;
    t->next = *pp;
    if (t->next)
        t->next->delta -= ticks;                 /* successor now relative to t */
    *pp = t;
}

void timer_tick(void)                            /* O(1) per tick + expired ones */
{
    if (!g_head)
        return;
    g_head->delta--;                             /* only the head counts down */
    while (g_head && g_head->delta == 0) {
        sw_timer_t *t = g_head;
        g_head = t->next;                        /* unlink BEFORE cb: cb may re-add */
        t->cb(t->arg);
    }
}
/* ==== MODEL ANSWER END ==== */

/* follow-up Q2 (shown in md): cancel */
bool timer_cancel(sw_timer_t *t) {
    for (sw_timer_t **pp = &g_head; *pp; pp = &(*pp)->next)
        if (*pp == t) { if (t->next) t->next->delta += t->delta; *pp = t->next; return true; }
    return false;
}

static uint32_t g_now;
static int  g_log[64], g_nlog;
static uint32_t g_when[64];
static void cb_log(void *arg) { g_log[g_nlog] = (int)(intptr_t)arg; g_when[g_nlog++] = g_now; }

static sw_timer_t g_periodic; static int g_period_left;
static void cb_periodic(void *arg)
{
    cb_log(arg);
    if (--g_period_left > 0) timer_add(&g_periodic, 3, cb_periodic, arg);   /* re-add from cb */
}

static void run(uint32_t n) { while (n--) { g_now++; timer_tick(); } }

int main(void)
{
    sw_timer_t a, b, c, d, e;
    run(5);                                       /* tick on empty list is a no-op */
    assert(g_nlog == 0);
    g_now = 0;
    timer_add(&a, 3, cb_log, (void *)1);
    timer_add(&b, 1, cb_log, (void *)2);
    timer_add(&c, 3, cb_log, (void *)3);          /* same expiry as a -> after a */
    timer_add(&d, 5, cb_log, (void *)4);
    timer_add(&e, 0, cb_log, (void *)5);          /* 0 -> next tick, after b */
    /* delta list: b(1) e(0) a(2) c(0) d(2) */
    assert(g_head == &b && b.delta == 1 && e.delta == 0 && a.delta == 2 && c.delta == 0 && d.delta == 2);
    run(10);
    int want[] = { 2, 5, 1, 3, 4 }; uint32_t at[] = { 1, 1, 3, 3, 5 };
    assert(g_nlog == 5);
    for (int i = 0; i < 5; i++) assert(g_log[i] == want[i] && g_when[i] == at[i]);
    assert(g_head == NULL);
    /* add in the middle of a running list */
    g_nlog = 0; g_now = 0;
    timer_add(&a, 10, cb_log, (void *)1);
    run(4);
    timer_add(&b, 2, cb_log, (void *)2);          /* expires at 6, before a (10) */
    timer_add(&c, 6, cb_log, (void *)3);          /* expires at 10, same as a -> after a */
    run(10);
    assert(g_nlog == 3 && g_log[0] == 2 && g_when[0] == 6 && g_log[1] == 1 && g_when[1] == 10
           && g_log[2] == 3 && g_when[2] == 10);
    /* periodic re-add from inside the callback */
    g_nlog = 0; g_now = 0; g_period_left = 4;
    timer_add(&g_periodic, 3, cb_periodic, (void *)9);
    run(20);
    assert(g_nlog == 4 && g_when[0] == 3 && g_when[1] == 6 && g_when[2] == 9 && g_when[3] == 12);
    /* follow-up: cancel keeps later timers' absolute times */
    g_nlog = 0; g_now = 0;
    timer_add(&a, 2, cb_log, (void *)1);
    timer_add(&b, 5, cb_log, (void *)2);
    timer_add(&c, 7, cb_log, (void *)3);
    assert(timer_cancel(&b) && c.delta == 5);
    assert(!timer_cancel(&b));
    run(10);
    assert(g_nlog == 2 && g_when[0] == 2 && g_log[1] == 3 && g_when[1] == 7);
    assert(!timer_cancel(&a));                    /* already fired */
    puts("E5 PASS");
    return 0;
}
