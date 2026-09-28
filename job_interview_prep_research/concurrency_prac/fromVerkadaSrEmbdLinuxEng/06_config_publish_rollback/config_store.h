#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * The API the harness calls.  You implement it in config_store.c.
 * ------------------------------------------------------------------------- */

/* How many past versions the history must be able to answer for.
 * Older versions are evicted.  Fixed memory: this is an embedded gateway. */
#define CONFIG_HISTORY_MAX 8

/* The config as the rest of the firmware sees it.  Treat an object of this
 * type as IMMUTABLE once it is visible to a reader: never edit one in place. */
struct Config {
    char     apn[32];
    uint32_t upload_period_ms;
    uint8_t  wan_priority;
    uint32_t version;
    uint32_t checksum;           /* == config_checksum(this) — see below */
};

/* FNV-1a over the payload fields, field by field.
 *
 * Never hash the raw struct bytes: the padding between wan_priority and
 * version is indeterminate, so memcmp/hash over the whole object is not a
 * stable answer.  Both the harness and the implementation call this, which
 * lets a reader that holds a config for a long time PROVE the bytes under it
 * are still the bytes it was given (a freed-and-recycled object almost never
 * still checksums). */
static inline uint32_t config_hash_bytes(uint32_t h, const void *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

static inline uint32_t config_checksum(const struct Config *c)
{
    uint32_t h = 2166136261u;
    h = config_hash_bytes(h, c->apn, sizeof c->apn);
    h = config_hash_bytes(h, &c->upload_period_ms, sizeof c->upload_period_ms);
    h = config_hash_bytes(h, &c->wan_priority, sizeof c->wan_priority);
    h = config_hash_bytes(h, &c->version, sizeof c->version);
    return h ? h : 1u;           /* 0 reserved as "obviously wrong" */
}

/* --- lifecycle ---------------------------------------------------------- */

/* Fetch the first config synchronously (retrying a few times if the link is
 * down) and start the background fetch thread.
 * Returns 0 on success, -1 if no config could be obtained at all. */
int config_store_init(void);

/* Stop the fetch thread and drop the store's own references.  Configs that a
 * reader still holds stay alive until that reader releases them. */
void config_store_deinit(void);

/* --- Part 1: readers ---------------------------------------------------- */

/* Borrow the current config.  MUST NOT block on the network.
 * Every non-NULL return must be handed back to config_release() exactly once.
 * The object is guaranteed valid and unchanging until then, even if newer
 * versions are published (and freed) in the meantime.
 * Returns NULL only if the store has no config at all. */
const struct Config *config_acquire(void);

/* Give back a reference from config_acquire() or config_get_version().
 * Safe to call after config_store_deinit(); the object is freed when the last
 * reference goes away.  NULL is ignored. */
void config_release(const struct Config *c);

/* Version of the current config, without borrowing it.  0 if none. */
uint32_t config_version(void);

/* --- Part 2: history and rollback -------------------------------------- */

/* Borrow version `version` from the history.
 * NULL if that version was never seen or has already been evicted.
 * Release it with config_release() like any other reference. */
const struct Config *config_get_version(uint32_t version);

/* Make an older, still-remembered version current again.
 * Returns 0 on success, -1 if the version is unknown or evicted. */
int config_rollback_to(uint32_t version);

/* Write the remembered versions into versions_out, NEWEST FIRST.
 * Writes at most `max` of them and returns how many were written.
 * Size the buffer CONFIG_HISTORY_MAX to see everything. */
size_t config_history(uint32_t *versions_out, size_t max);

/* --- allocation accounting (for the harness) --------------------------- */

/* Number of struct Config objects the implementation has allocated / freed
 * since init.  The harness uses these to prove there are no leaks and no
 * objects freed while still referenced. */
size_t config_alloc_count(void);
size_t config_free_count(void);

#endif /* CONFIG_STORE_H */
