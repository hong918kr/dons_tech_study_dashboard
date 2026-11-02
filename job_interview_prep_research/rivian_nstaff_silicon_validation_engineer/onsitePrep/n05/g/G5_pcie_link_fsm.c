/* G5 - SIMPLIFIED PCIe link FSM: Detect -> Polling -> Config -> L0, timeout/retry/Recovery.
 * Not the real LTSSM (no substates, L0s/L1, EQ, HotReset...). See LP03 for the real one. */
/* --- model answer --- */
#include <stdint.h>
#include <stdbool.h>

typedef enum { L_DETECT, L_POLLING, L_CONFIG, L_L0, L_RECOVERY, L_FAIL } lstate_t;

typedef struct {               /* inputs sampled each 1 ms tick (from PHY/MAC status) */
    bool rx_detected;          /* far-end receiver termination seen        */
    bool ts_locked;            /* Polling: enough TS1/TS2 received         */
    bool cfg_done;             /* Config: link/lane numbers agreed         */
    bool link_err;             /* in L0: error or speed-change request     */
    bool rcv_done;             /* Recovery: retrained OK                   */
} link_in_t;

typedef struct {
    lstate_t st;
    uint32_t ms_in_state;
    uint8_t  retries;          /* fallbacks to Detect since last L0 */
    uint32_t recoveries;       /* L0 -> Recovery count (link health metric) */
} link_t;

#define T_STATE_MS 24u         /* simplified: one timeout for Polling/Config/Recovery */
#define MAX_RETRY  3u

static void go(link_t *l, lstate_t s) { l->st = s; l->ms_in_state = 0; }

static void fall_back(link_t *l)      /* timeout: back to Detect, or give up */
{
    if (++l->retries > MAX_RETRY) go(l, L_FAIL);
    else                          go(l, L_DETECT);
}

void link_init(link_t *l) { *l = (link_t){0}; go(l, L_DETECT); }

lstate_t link_step(link_t *l, const link_in_t *in)    /* call every 1 ms */
{
    l->ms_in_state++;
    bool tmo = l->ms_in_state >= T_STATE_MS;
    switch (l->st) {
    case L_DETECT:                                    /* no timeout: keep looking for a card */
        if (in->rx_detected) go(l, L_POLLING);
        break;
    case L_POLLING:
        if (in->ts_locked) go(l, L_CONFIG);
        else if (tmo)      fall_back(l);
        break;
    case L_CONFIG:
        if (in->cfg_done) { go(l, L_L0); l->retries = 0; }
        else if (tmo)       fall_back(l);
        break;
    case L_L0:
        if (in->link_err) { go(l, L_RECOVERY); l->recoveries++; }
        break;
    case L_RECOVERY:
        if (in->rcv_done) go(l, L_L0);
        else if (tmo)     fall_back(l);
        break;
    case L_FAIL:                                      /* sticky until link_init() */
        break;
    }
    return l->st;
}
/* --- tests --- */
#include <assert.h>
#include <stdio.h>

static lstate_t steps(link_t *l, const link_in_t *in, int n)
{
    lstate_t s = l->st;
    while (n-- > 0) s = link_step(l, in);
    return s;
}

int main(void)
{
    link_t l;
    const link_in_t none = {0};
    link_in_t in;

    /* happy path: one tick per state */
    link_init(&l);
    assert(steps(&l, &none, 100) == L_DETECT);                    /* empty slot: never fails */
    in = (link_in_t){ .rx_detected = true };
    assert(link_step(&l, &in) == L_POLLING);
    in = (link_in_t){ .ts_locked = true };
    assert(link_step(&l, &in) == L_CONFIG);
    in = (link_in_t){ .cfg_done = true };
    assert(link_step(&l, &in) == L_L0 && l.retries == 0);
    assert(steps(&l, &none, 1000) == L_L0);                       /* stays up */

    /* L0 error -> Recovery -> back to L0, counted */
    in = (link_in_t){ .link_err = true };
    assert(link_step(&l, &in) == L_RECOVERY && l.recoveries == 1);
    in = (link_in_t){ .rcv_done = true };
    assert(link_step(&l, &in) == L_L0);

    /* Polling timeout boundary: 23 ticks still Polling, 24th -> Detect, retries=1 */
    link_init(&l);
    in = (link_in_t){ .rx_detected = true };
    link_step(&l, &in);
    assert(l.st == L_POLLING);
    assert(steps(&l, &none, 23) == L_POLLING);
    assert(link_step(&l, &none) == L_DETECT && l.retries == 1);

    /* repeated Polling failures: retries 1..3 go to Detect, 4th -> FAIL (sticky) */
    for (int k = 2; k <= 4; k++) {
        in = (link_in_t){ .rx_detected = true };
        link_step(&l, &in);
        lstate_t s = steps(&l, &none, (int)T_STATE_MS);
        if (k <= (int)MAX_RETRY) assert(s == L_DETECT && l.retries == k);
        else                     assert(s == L_FAIL);
    }
    in = (link_in_t){ .rx_detected = true, .ts_locked = true, .cfg_done = true };
    assert(steps(&l, &in, 10) == L_FAIL);

    /* Recovery timeout falls back to Detect; reaching L0 again resets retries */
    link_init(&l);
    in = (link_in_t){ .rx_detected = true, .ts_locked = true, .cfg_done = true };
    assert(steps(&l, &in, 3) == L_L0);
    in = (link_in_t){ .link_err = true };
    link_step(&l, &in);
    assert(steps(&l, &none, (int)T_STATE_MS) == L_DETECT && l.retries == 1);
    in = (link_in_t){ .rx_detected = true, .ts_locked = true, .cfg_done = true };
    assert(steps(&l, &in, 3) == L_L0 && l.retries == 0);

    /* Config timeout */
    link_init(&l);
    in = (link_in_t){ .rx_detected = true, .ts_locked = true };
    assert(steps(&l, &in, 2) == L_CONFIG);
    assert(steps(&l, &in, (int)T_STATE_MS) == L_DETECT);         /* ts_locked ignored in Config */

    printf("G5 PASS\n");
    return 0;
}
