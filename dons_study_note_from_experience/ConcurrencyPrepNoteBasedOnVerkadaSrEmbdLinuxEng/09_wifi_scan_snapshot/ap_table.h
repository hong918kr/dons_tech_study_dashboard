#ifndef AP_TABLE_H
#define AP_TABLE_H

#include <stddef.h>
#include <stdint.h>

#include "wifi.h"

/* ------------------------------------------------------------------ *
 * The API you implement (ap_table.c).  main.c calls only this.
 * ------------------------------------------------------------------ */

/* Smoothing factor for the EWMA on RSSI.  Fixed so the harness can
 * reproduce your numbers exactly:
 *
 *      float d = (float)rssi_dbm - e->rssi_smoothed;
 *      d *= AP_EWMA_ALPHA;
 *      e->rssi_smoothed += d;
 *
 * Write it as those three statements (not one expression): keeping the
 * multiply and the add in separate statements stops the compiler from
 * contracting them into an FMA, so the harness's reference and your
 * code produce bit-identical floats.  The first sighting of an AP seeds
 * rssi_smoothed with the raw value instead of smoothing it. */
#define AP_EWMA_ALPHA 0.25f

/* "Seen recently" window: an entry older than this is dead. */
#define AP_STALE_US (5ull * 60ull * 1000000ull)   /* 5 minutes */

/* Slots in the hash map. Power of two so the index is a mask, not a %. */
#define AP_TABLE_CAP 256u

/* One AP as the table remembers it, aggregated over many scans. */
struct ApEntry {
    uint8_t  bssid[6];        /* key                                        */
    char     ssid[33];        /* last SSID seen for this BSSID               */
    float    rssi_smoothed;   /* EWMA of rssi_dbm, see AP_EWMA_ALPHA         */
    uint32_t times_seen;      /* how many scans contained this BSSID         */
    uint64_t last_seen_us;    /* wifi_now_us() when it was last folded in    */
};

/* ---- lifecycle ---------------------------------------------------- */

/* Run ONE scan synchronously (so a getter called right after init already
 * has a snapshot — costs one scan time, ~100 ms) and start the scanner
 * thread. Idempotent. Returns 0 on success, -1 on failure. */
int wifi_table_init(void);

/* Stop the scanner thread and join it (may wait one scan, ~300 ms).
 * The published snapshot and the AP table stay readable afterwards —
 * the harness checks Part 2 with the radio stopped. Idempotent. */
void wifi_table_deinit(void);

/* ---- Part 1: concurrency ------------------------------------------ */

/* Copy out the most recent COMPLETE scan.
 *   - never blocks on the radio
 *   - never returns entries from two different scans mixed together
 *   - returns the number of entries written = min(scan size, max)
 *     (a scan can legitimately contain 0 APs — that is not an error)
 *   - before the first scan completes: returns 0
 * Safe to call from any number of threads at once. */
size_t wifi_get_snapshot(struct ApInfo *out, size_t max);

/* ---- Part 2: data structure --------------------------------------- */

/* Fold one scan's worth of APs into the table (insert or update).
 * The scanner thread calls this; it is public so Part 2 can be tested
 * without the radio. Thread-safe. */
void wifi_table_observe(const struct ApInfo *aps, size_t n);

/* The k strongest APs seen within the last AP_STALE_US, strongest first.
 *   `out` must have room for k entries.
 *   Returns how many were written = min(k, live entries).
 * Order: by rssi_smoothed descending; exact ties broken by BSSID
 * ascending (memcmp), so the result is deterministic. */
size_t wifi_top_k(struct ApEntry *out, size_t k);

/* Look up one BSSID. O(1) average.
 * Returns 0 and fills *out on a hit, -1 if unknown or stale. */
int wifi_lookup(const uint8_t bssid[6], struct ApEntry *out);

/* ---- stats (for the harness) -------------------------------------- */

/* Live (non-stale) entries currently in the table. */
size_t wifi_table_count(void);

/* APs that could not be stored because the table was full of live
 * entries. Non-zero means AP_TABLE_CAP is too small. */
size_t wifi_table_dropped(void);

#endif /* AP_TABLE_H */
