/* G1 - button debounce FSM (sampled every 1 ms, N consecutive samples to confirm) */
/* --- model answer --- */
#include <stdint.h>
#include <stdbool.h>

typedef enum { DB_LOW, DB_MAYBE_HIGH, DB_HIGH, DB_MAYBE_LOW } db_state_t;
typedef enum { EV_NONE, EV_PRESS, EV_RELEASE } db_event_t;

typedef struct {
    db_state_t st;
    uint8_t    cnt;   /* consecutive samples that disagree with the stable level */
    uint8_t    n;     /* samples needed to confirm a change (n >= 1) */
} debounce_t;

void db_init(debounce_t *d, bool level, uint8_t n)
{
    d->st  = level ? DB_HIGH : DB_LOW;
    d->cnt = 0;
    d->n   = n ? n : 1;
}

/* Call once per 1 ms tick with the raw pin level. Returns an event on a confirmed edge. */
db_event_t db_sample(debounce_t *d, bool raw)
{
    switch (d->st) {
    case DB_LOW:
        if (!raw) return EV_NONE;
        d->cnt = 0;
        d->st  = DB_MAYBE_HIGH;
        /* fall through: this sample counts as the first high one */
    case DB_MAYBE_HIGH:
        if (!raw) { d->st = DB_LOW; return EV_NONE; }      /* bounce: back to stable */
        if (++d->cnt < d->n) return EV_NONE;
        d->st = DB_HIGH;
        return EV_PRESS;
    case DB_HIGH:
        if (raw) return EV_NONE;
        d->cnt = 0;
        d->st  = DB_MAYBE_LOW;
        /* fall through */
    case DB_MAYBE_LOW:
        if (raw) { d->st = DB_HIGH; return EV_NONE; }
        if (++d->cnt < d->n) return EV_NONE;
        d->st = DB_LOW;
        return EV_RELEASE;
    }
    return EV_NONE;
}
/* --- tests --- */
#include <assert.h>
#include <stdio.h>

static int feed(debounce_t *d, const char *s, db_event_t *last)
{
    int events = 0;
    for (; *s; s++) {
        db_event_t e = db_sample(d, *s == '1');
        if (e != EV_NONE) { events++; *last = e; }
    }
    return events;
}

int main(void)
{
    debounce_t d;
    db_event_t last = EV_NONE;

    /* clean press with n=3: event exactly on the 3rd high sample */
    db_init(&d, false, 3);
    assert(db_sample(&d, 1) == EV_NONE);
    assert(db_sample(&d, 1) == EV_NONE);
    assert(db_sample(&d, 1) == EV_PRESS);
    assert(d.st == DB_HIGH);
    assert(db_sample(&d, 1) == EV_NONE);           /* holding: no repeat event */

    /* bouncy press: 1 0 1 1 0 1 1 1 -> only one PRESS, at the last sample */
    db_init(&d, false, 3);
    assert(feed(&d, "1011011", &last) == 0);
    assert(db_sample(&d, 1) == EV_PRESS);

    /* bouncy release then clean release */
    assert(feed(&d, "0010001000", &last) == 1 && last == EV_RELEASE);
    assert(d.st == DB_LOW);

    /* glitch shorter than n never produces an event */
    db_init(&d, false, 4);
    assert(feed(&d, "0111011101110", &last) == 0);

    /* n = 1: every level change is an event; n = 0 treated as 1 */
    db_init(&d, false, 0);
    assert(feed(&d, "1010", &last) == 4);

    /* start high */
    db_init(&d, true, 2);
    assert(feed(&d, "1101", &last) == 0);
    assert(feed(&d, "00", &last) == 1 && last == EV_RELEASE);

    /* n = 255 boundary: 254 highs no event, 255th -> PRESS */
    db_init(&d, false, 255);
    for (int i = 0; i < 254; i++) assert(db_sample(&d, 1) == EV_NONE);
    assert(db_sample(&d, 1) == EV_PRESS);

    printf("G1 PASS\n");
    return 0;
}
