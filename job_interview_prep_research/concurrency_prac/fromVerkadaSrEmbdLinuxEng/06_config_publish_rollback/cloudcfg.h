#ifndef CLOUDCFG_H
#define CLOUDCFG_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Vendor / platform header — GIVEN.  Do not modify.
 *
 * The cloud SDK hands the gateway its provisioning record.  The call is
 * synchronous and slow: it does DNS, TLS and an HTTP round trip inside.
 * ------------------------------------------------------------------------- */

struct RawConfig {
    char     apn[32];            /* cellular access point name, NUL terminated */
    uint32_t upload_period_ms;   /* how often to push telemetry              */
    uint8_t  wan_priority;       /* 0 = LTE first, 1 = Wi-Fi first, ...       */
    uint32_t version;            /* monotonically increasing on the server    */
};

/* Fetch the current configuration from the cloud.
 *
 *   returns  0  *out is filled in.  NOTE: the server often answers with the
 *               SAME version it gave last time — nothing changed.
 *   returns -1  the WAN link is down; *out is left untouched.  Failures come
 *               in BURSTS (a tunnel, a dead cell, a rebooting head-end), not
 *               one at a time.
 *
 * BLOCKS for 0-200 ms in production.  The practice harness scales that down
 * (see question_note) so the whole test finishes in a few seconds.
 *
 * NOT thread-safe: it keeps static session state internally.  Exactly one
 * thread may ever call it, and only one call may be in flight at a time.
 */
int cloudcfg_fetch(struct RawConfig *out);

/* Monotonic-ish microsecond clock provided by the platform (main.c). */
uint64_t cloudcfg_now_us(void);

#endif /* CLOUDCFG_H */
