#ifndef WIFI_H
#define WIFI_H

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ *
 * Vendor Wi-Fi driver header for the GW31-E gateway radio.
 * GIVEN — do not modify. The fake implementation lives in main.c.
 * ------------------------------------------------------------------ */

/* The radio never reports more than this many APs in one scan. */
#define WIFI_MAX_APS 40

/* One access point as the radio reports it.
 * All fields are 1-byte aligned, so sizeof == 41 with no padding:
 * a whole ApInfo can be memcmp'd / memcpy'd safely. */
struct ApInfo {
    uint8_t bssid[6];   /* MAC of the AP — the only stable identity   */
    char    ssid[33];   /* NUL-terminated, may be empty (hidden SSID) */
    int8_t  rssi_dbm;   /* signal strength, e.g. -42                  */
    uint8_t channel;    /* channel the AP was heard on                */
};

/* Run one full scan.
 *
 *   out    caller's buffer, at least `max` entries
 *   max    capacity of `out`
 *   found  out-param: how many APs were SEEN (0..WIFI_MAX_APS).
 *          NOTE: *found may be LARGER than max — the radio saw more APs
 *          than fit in your buffer. Only min(*found, max) entries are
 *          written. Never index past that.
 *
 * BLOCKS for the whole scan: 100-300 ms on real hardware.
 * (The fake radio in main.c blocks 40-120 ms so the harness finishes fast.)
 *
 * Returns 0 on success, -1 if the radio is BUSY. On -1 nothing is written:
 * `out` and `*found` are unspecified — do not read them.
 *
 * NOT thread-safe (static internal state). Exactly ONE caller, ever.
 */
int wifi_scan(struct ApInfo *out, size_t max, size_t *found);

/* Wall clock in microseconds. Same clock the rest of the system uses.
 * gettimeofday-based, so it is NOT monotonic: it can step backwards. */
uint64_t wifi_now_us(void);

#endif /* WIFI_H */
