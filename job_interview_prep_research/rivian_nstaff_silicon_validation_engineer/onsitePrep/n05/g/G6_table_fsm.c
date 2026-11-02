/* G6 - table-driven FSM: (state, event) -> (next, action), device power states */
/* --- model answer --- */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum { S_NONE = 0, S_OFF, S_ACTIVE, S_IDLE, S_SLEEP, S_COUNT } pstate_t;  /* 0 = "no transition" */
typedef enum { E_PWR_ON, E_PWR_OFF, E_NO_ACT, E_ACT, E_TIMER, E_FAULT, E_COUNT } pevent_t;

typedef void (*action_fn)(void);
typedef struct { pstate_t next; action_fn act; } trans_t;

void act_power_up(void);  void act_power_down(void);  void act_dim(void);
void act_wake(void);      void act_suspend(void);     void act_resume(void);
void act_emergency_off(void);

/* Missing entries are zero = {S_NONE, NULL} = event ignored in that state. */
static const trans_t table[S_COUNT][E_COUNT] = {
    [S_OFF]    = { [E_PWR_ON]  = { S_ACTIVE, act_power_up } },
    [S_ACTIVE] = { [E_NO_ACT]  = { S_IDLE,   act_dim },
                   [E_PWR_OFF] = { S_OFF,    act_power_down },
                   [E_FAULT]   = { S_OFF,    act_emergency_off } },
    [S_IDLE]   = { [E_ACT]     = { S_ACTIVE, act_wake },
                   [E_TIMER]   = { S_SLEEP,  act_suspend },
                   [E_PWR_OFF] = { S_OFF,    act_power_down },
                   [E_FAULT]   = { S_OFF,    act_emergency_off } },
    [S_SLEEP]  = { [E_ACT]     = { S_ACTIVE, act_resume },
                   [E_PWR_OFF] = { S_OFF,    act_power_down },
                   [E_FAULT]   = { S_OFF,    act_emergency_off } },
};

typedef struct { pstate_t st; uint32_t ignored; } fsm_t;

/* Returns true if the event caused a transition. */
bool fsm_dispatch(fsm_t *f, pevent_t ev)
{
    if (f->st <= S_NONE || f->st >= S_COUNT || (unsigned)ev >= E_COUNT) {
        f->ignored++;                         /* corrupt state or bad event id */
        return false;
    }
    const trans_t *t = &table[f->st][ev];
    if (t->next == S_NONE) { f->ignored++; return false; }
    if (t->act) t->act();                     /* action runs before state changes */
    f->st = t->next;
    return true;
}
/* --- tests --- */
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char log_buf[32];
static size_t log_n;
static void L(char c) { if (log_n < sizeof log_buf - 1) log_buf[log_n++] = c; log_buf[log_n] = '\0'; }
void act_power_up(void)      { L('U'); }
void act_power_down(void)    { L('D'); }
void act_dim(void)           { L('i'); }
void act_wake(void)          { L('w'); }
void act_suspend(void)       { L('s'); }
void act_resume(void)        { L('r'); }
void act_emergency_off(void) { L('X'); }

int main(void)
{
    fsm_t f = { S_OFF, 0 };

    /* normal life cycle */
    assert(fsm_dispatch(&f, E_PWR_ON) && f.st == S_ACTIVE);
    assert(fsm_dispatch(&f, E_NO_ACT) && f.st == S_IDLE);
    assert(fsm_dispatch(&f, E_TIMER)  && f.st == S_SLEEP);
    assert(fsm_dispatch(&f, E_ACT)    && f.st == S_ACTIVE);
    assert(fsm_dispatch(&f, E_NO_ACT) && f.st == S_IDLE);
    assert(fsm_dispatch(&f, E_ACT)    && f.st == S_ACTIVE);
    assert(fsm_dispatch(&f, E_PWR_OFF) && f.st == S_OFF);
    assert(strcmp(log_buf, "UisriwD") == 0 && f.ignored == 0);

    /* ignored events: no action, no state change, counted */
    log_n = 0; log_buf[0] = '\0';
    assert(!fsm_dispatch(&f, E_TIMER) && f.st == S_OFF);
    assert(!fsm_dispatch(&f, E_FAULT) && f.st == S_OFF);           /* fault while off */
    fsm_dispatch(&f, E_PWR_ON);
    assert(!fsm_dispatch(&f, E_PWR_ON) && f.st == S_ACTIVE);       /* double power-on */
    assert(!fsm_dispatch(&f, E_TIMER) && f.st == S_ACTIVE);
    assert(f.ignored == 4 && strcmp(log_buf, "U") == 0);

    /* fault from SLEEP -> emergency off */
    fsm_dispatch(&f, E_NO_ACT); fsm_dispatch(&f, E_TIMER);
    assert(f.st == S_SLEEP);
    assert(fsm_dispatch(&f, E_FAULT) && f.st == S_OFF && log_buf[log_n - 1] == 'X');

    /* bad event id / corrupted state */
    assert(!fsm_dispatch(&f, (pevent_t)E_COUNT) && f.st == S_OFF);
    f.st = S_NONE;
    assert(!fsm_dispatch(&f, E_PWR_ON) && f.st == S_NONE);
    f.st = (pstate_t)99;
    assert(!fsm_dispatch(&f, E_PWR_ON));
    assert(f.ignored == 7);

    printf("G6 PASS\n");
    return 0;
}
