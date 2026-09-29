#ifndef EVSRC_H
#define EVSRC_H

#include <stdint.h>

/* ------------------------------------------------------------------ *
 * Vendor event source (GIVEN — do not modify this header).
 *
 * A third-party access-control / sensor bridge. It hands us events one
 * at a time through a call that blocks with NO TIMEOUT.
 * ------------------------------------------------------------------ */

/* Event types. */
enum {
    EV_MOTION  = 0,
    EV_DOOR    = 1,
    EV_TAMPER  = 2,
    EV_OFFLINE = 3,
    EV_TYPE_COUNT = 4,
};

struct Event {
    int      type;        /* EV_* */
    uint32_t device_id;   /* which camera / sensor / reader */
    uint64_t timestamp;   /* microseconds, same clock as ev_now_us() */
};

/* Block until the next event is available and store it in *out.
 *
 *    0   success, *out is filled in
 *   -1   transient vendor error; *out untouched; the call returned quickly
 *   -2   the call was cut short by evsrc_wake()
 *
 * There is NO timeout argument and no non-blocking mode: if the site is
 * quiet this call can sit there for minutes. Events also arrive in bursts,
 * so several calls in a row may return within the same millisecond.
 *
 * NOT thread-safe (static state inside the vendor library):
 * exactly one thread may ever be inside this function.
 */
int evsrc_read_blocking(struct Event *out);

/* The one escape hatch the vendor gives us.
 *
 * Makes a pending evsrc_read_blocking() return -2 "immediately" (the vendor
 * promises best effort, in practice a couple of milliseconds). The wake is
 * LATCHED: once you have called it, every later evsrc_read_blocking() also
 * returns -2 at once, so there is no window where a wake can be missed.
 *
 * Safe to call from a thread other than the reader. There is no un-wake:
 * it is a one-way trip, meant for shutdown.
 */
void evsrc_wake(void);

/* Monotone-enough microsecond clock (gettimeofday), the same clock the
 * vendor stamps events with. */
uint64_t ev_now_us(void);

#endif /* EVSRC_H */
