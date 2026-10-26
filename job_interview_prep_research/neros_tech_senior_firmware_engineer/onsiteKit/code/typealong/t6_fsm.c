/* T6 arm / failsafe state machine -- "arm only if throttle low and link alive; 100 ms no radio -> safe; no flicker" */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RX_TIMEOUT_MS 100u
#define RECOVER_FRAMES 5u
#define LAND_AFTER_MS 3000u
#define THROTTLE_LOW 50u
#define LOG_MAX 16u

typedef enum { DISARMED, ARMED, FAILSAFE } state_t;

typedef struct { uint32_t t; state_t from, to; const char *why; } transition_t;

typedef struct {
    state_t state;
    bool have_rx;
    uint32_t last_rx, fs_since;
    unsigned good;
    transition_t log[LOG_MAX];   /* the test artifact */
    unsigned nlog;
} mode_t_;

static void go(mode_t_ *m, uint32_t t, state_t to, const char *why)
{
    if (m->nlog < LOG_MAX)
        m->log[m->nlog++] = (transition_t){t, m->state, to, why};
    m->state = to;
}

static bool link_ok(const mode_t_ *m, uint32_t t)
{
    return m->have_rx && t - m->last_rx < RX_TIMEOUT_MS;
}

void on_rx(mode_t_ *m, uint32_t t)
{
    if (m->state == FAILSAFE) {
        if (!link_ok(m, t))
            m->good = 0;                       /* gap breaks the "in a row" count */
        if (++m->good >= RECOVER_FRAMES)
            go(m, t, ARMED, "link back");
    }
    m->have_rx = true;
    m->last_rx = t;
}

bool request_arm(mode_t_ *m, uint32_t t, unsigned throttle)
{
    if (m->state == DISARMED && throttle <= THROTTLE_LOW && link_ok(m, t))
        go(m, t, ARMED, "arm");
    return m->state == ARMED;
}

void tick(mode_t_ *m, uint32_t t)
{
    if (m->state == ARMED && !link_ok(m, t)) {
        m->good = 0;
        m->fs_since = t;
        go(m, t, FAILSAFE, "rx timeout");
    } else if (m->state == FAILSAFE && t - m->fs_since >= LAND_AFTER_MS) {
        go(m, t, DISARMED, "landed");
    }
}

int main(void)
{
    mode_t_ m = {0};
    assert(!request_arm(&m, 0, 0));                         /* no link yet */
    on_rx(&m, 0);
    assert(!request_arm(&m, 10, 900));                      /* throttle high */
    assert(request_arm(&m, 10, 0));
    tick(&m, 99);  assert(m.state == ARMED);                /* 99 ms: still fine */
    tick(&m, 100); assert(m.state == FAILSAFE);             /* exactly 100 ms -> failsafe */
    for (uint32_t t = 110; t <= 140; t += 10)
        on_rx(&m, t);
    assert(m.state == FAILSAFE);                            /* 4 frames: not yet */
    on_rx(&m, 150); assert(m.state == ARMED);               /* 5 in a row -> back */
    tick(&m, 400); tick(&m, 3400);
    assert(m.state == DISARMED && strcmp(m.log[m.nlog - 1].why, "landed") == 0);
    puts("t6 OK");
    return 0;
}
