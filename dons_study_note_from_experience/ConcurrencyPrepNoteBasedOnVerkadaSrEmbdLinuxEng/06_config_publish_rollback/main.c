/* main.c — test harness + fake cloud SDK.  GIVEN; do not modify.
 *
 * Used unchanged by both config_store.c (your stub) and
 * config_store_solution.c, so every check has to go through the public API.
 *
 * What it proves:
 *   - a reader that holds a config across many publishes never reads freed or
 *     garbage memory (every config carries a checksum, and the harness also
 *     keeps ground truth for every version the fake cloud ever produced)
 *   - allocation accounting: every config is eventually freed after deinit,
 *     nothing is freed while still referenced, live objects stay bounded
 *   - history / eviction / rollback correctness, including lookups of evicted
 *     versions and of versions that never existed
 *   - a burst of link-down failures leaves the last good config in place
 *   - 4 readers + 1 publisher for ~2 s with no torn or invented values
 *   - the getters never block anywhere near as long as the vendor call does
 *
 * All randomness is a fixed-seed xorshift, so the run is the same on macOS and
 * Linux; nothing is compared against a hard-coded expected value.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "cloudcfg.h"
#include "config_store.h"

/* The real SDK blocks 0-200 ms.  Scaled down here so the whole test finishes
 * in a few seconds (SPEC 3.1); everything else about it is unchanged. */
#define FETCH_MIN_US   (5 * 1000)
#define FETCH_MAX_US  (50 * 1000)

/* ------------------------------------------------------------------ */
/* ground truth (harness only)                                        */
/* ------------------------------------------------------------------ */

#define TRUTH_MAX 16384
static struct {
    bool             have;
    struct RawConfig raw;
} g_truth[TRUTH_MAX];
static pthread_mutex_t g_truth_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic uint32_t g_truth_top;      /* highest version produced */

static void truth_record(const struct RawConfig *raw)
{
    pthread_mutex_lock(&g_truth_lock);
    if (raw->version < TRUTH_MAX) {
        g_truth[raw->version].have = true;
        g_truth[raw->version].raw  = *raw;
    }
    pthread_mutex_unlock(&g_truth_lock);
    atomic_store(&g_truth_top, raw->version);
}

/* Rebuild the struct Config the implementation should have produced for this
 * version, so we can compare field by field AND by checksum. */
static bool truth_expect(uint32_t version, struct Config *out)
{
    bool ok = false;
    pthread_mutex_lock(&g_truth_lock);
    if (version < TRUTH_MAX && g_truth[version].have) {
        const struct RawConfig *r = &g_truth[version].raw;
        memset(out, 0, sizeof *out);
        memcpy(out->apn, r->apn, sizeof out->apn);
        out->apn[sizeof out->apn - 1] = '\0';
        out->upload_period_ms = r->upload_period_ms;
        out->wan_priority     = r->wan_priority;
        out->version          = r->version;
        out->checksum         = config_checksum(out);
        ok = true;
    }
    pthread_mutex_unlock(&g_truth_lock);
    return ok;
}

static bool cfg_equal(const struct Config *a, const struct Config *b)
{
    return memcmp(a->apn, b->apn, sizeof a->apn) == 0
        && a->upload_period_ms == b->upload_period_ms
        && a->wan_priority     == b->wan_priority
        && a->version          == b->version
        && a->checksum         == b->checksum;
}

/* A config is "real" if it checksums AND matches what the cloud handed out for
 * that version.  Freed/recycled memory fails the first test; a torn copy or a
 * value nobody ever published fails the second. */
static bool cfg_is_real(const struct Config *c)
{
    struct Config want;
    if (c->checksum != config_checksum(c)) return false;
    if (!truth_expect(c->version, &want))  return false;
    return cfg_equal(c, &want);
}

/* ------------------------------------------------------------------ */
/* check plumbing                                                     */
/* ------------------------------------------------------------------ */

static int fails;

static void check(const char *label, bool ok)
{
    if (!ok) fails++;
    printf("  %-46s %s\n", label, ok ? "PASS" : "FAIL");
}

static void check_u32(const char *label, uint32_t got, uint32_t want)
{
    bool ok = (got == want);
    if (!ok) fails++;
    printf("  %-46s got %-10u want %-10u %s\n", label, got, want, ok ? "PASS" : "FAIL");
}

/* ------------------------------------------------------------------ */
/* fake cloud SDK (the "vendor" side)                                 */
/* ------------------------------------------------------------------ */

/* VMODE_STORM is still legal vendor behaviour — "blocks 0-200 ms" includes 0.
 * It models a fleet-wide config push, where a new version lands on every poll.
 * It is also the only way a test can make the publish/acquire race (loading the
 * current pointer, then claiming a reference on it) fire often enough to see. */
enum { VMODE_NORMAL = 0, VMODE_FREEZE = 1, VMODE_LINKDOWN = 2, VMODE_STORM = 3 };
static _Atomic int      g_vmode = VMODE_NORMAL;
static _Atomic uint64_t g_fetch_fails;
static _Atomic uint64_t g_fetch_nochange;
static _Atomic uint64_t g_fetch_new;
static _Atomic uint64_t g_fetch_max_block_us;

/* xorshift32, fixed seed: only cloudcfg_fetch() touches it, and only one
 * thread may ever call cloudcfg_fetch(). */
static uint32_t g_vrng = 0x1BADB002u;
static uint32_t vrng(void)
{
    uint32_t x = g_vrng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_vrng = x;
    return x;
}

uint64_t cloudcfg_now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000ull * (uint64_t)tv.tv_sec + (uint64_t)tv.tv_usec;
}

static const char *const APN_TABLE[] = {
    "iot.vzw.gw", "m2m.att.net", "broadband.tmo", "lpwa.telus.iot",
    "vk.private.apn", "gw.backup.apn",
};
static const uint32_t PERIOD_TABLE[] = { 1000, 2000, 5000, 10000, 30000, 60000 };

static void vendor_mutate(struct RawConfig *cur)
{
    memset(cur->apn, 0, sizeof cur->apn);
    snprintf(cur->apn, sizeof cur->apn, "%s",
             APN_TABLE[vrng() % (sizeof APN_TABLE / sizeof APN_TABLE[0])]);
    cur->upload_period_ms =
        PERIOD_TABLE[vrng() % (sizeof PERIOD_TABLE / sizeof PERIOD_TABLE[0])];
    cur->wan_priority = (uint8_t)(vrng() % 4u);
}

static void vendor_block(uint32_t lo_us, uint32_t hi_us)
{
    uint32_t us = lo_us + vrng() % (hi_us - lo_us + 1u);
    uint64_t t0 = cloudcfg_now_us();
    usleep(us);
    uint64_t t1 = cloudcfg_now_us();
    uint64_t dt = (t1 > t0) ? t1 - t0 : 0;      /* gettimeofday is not monotonic */
    if (dt > atomic_load(&g_fetch_max_block_us))
        atomic_store(&g_fetch_max_block_us, dt);
}

int cloudcfg_fetch(struct RawConfig *out)
{
    static bool first = true;
    static struct RawConfig cur;          /* static session state: ONE caller */
    int mode = atomic_load(&g_vmode);     /* harness knob, not part of the API */

    if (first) {
        /* The SDK has the provisioning record cached from the boot handshake,
         * so the very first call answers immediately. */
        first = false;
        cur.version = 1;
        vendor_mutate(&cur);
        truth_record(&cur);
        atomic_fetch_add(&g_fetch_new, 1);
        *out = cur;
        return 0;
    }

    if (mode == VMODE_LINKDOWN) {
        /* Failures arrive in bursts; the caller must keep its last good
         * config.  *out is deliberately left untouched. */
        vendor_block(FETCH_MIN_US, FETCH_MIN_US * 3);
        atomic_fetch_add(&g_fetch_fails, 1);
        return -1;
    }

    if (mode == VMODE_STORM) {
        usleep(100);                      /* answers immediately */
    } else {
        vendor_block(FETCH_MIN_US, FETCH_MAX_US);
    }

    /* Never run off the end of the ground-truth table. */
    if (mode == VMODE_FREEZE || cur.version + 16u >= TRUTH_MAX ||
        (mode != VMODE_STORM && (vrng() % 100u) < 25u)) {
        atomic_fetch_add(&g_fetch_nochange, 1);
        *out = cur;                       /* SAME version as last time */
        return 0;
    }

    cur.version++;
    vendor_mutate(&cur);
    truth_record(&cur);
    atomic_fetch_add(&g_fetch_new, 1);
    *out = cur;
    return 0;
}

/* ------------------------------------------------------------------ */
/* reader stress                                                      */
/* ------------------------------------------------------------------ */

static _Atomic bool g_stress_stop;

#define SLOW_ROUND_US 5000          /* a getter round this slow is suspicious */

typedef struct {
    uint32_t seed;
    uint64_t calls, bad, held, slow, max_us;
} stress_stat_t;

static void *stress_reader(void *arg)
{
    stress_stat_t *st = arg;
    uint32_t rng = st->seed;
    uint64_t iter = 0;

    while (!atomic_load(&g_stress_stop)) {
        struct Config a, b;
        bool have_a = false, have_b = false;
        uint32_t versions[CONFIG_HISTORY_MAX];
        size_t nv;
        uint32_t cur_ver;

        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;

        /* ---- timed region: nothing but the API under test ---- */
        uint64_t t0 = cloudcfg_now_us();

        const struct Config *c = config_acquire();
        if (c) { a = *c; have_a = true; }
        cur_ver = config_version();
        nv = config_history(versions, CONFIG_HISTORY_MAX);
        if (nv) {
            const struct Config *c2 = config_get_version(versions[rng % nv]);
            if (c2) { b = *c2; have_b = true; config_release(c2); }
        }
        if (c) config_release(c);

        uint64_t t1 = cloudcfg_now_us();
        /* ---- end timed region ---- */
        /* gettimeofday can step backwards (NTP); never let that underflow. */
        uint64_t dt = (t1 > t0) ? t1 - t0 : 0;

        if (dt > st->max_us) st->max_us = dt;
        if (dt > SLOW_ROUND_US) st->slow++;
        if (have_a && !cfg_is_real(&a)) st->bad++;
        if (have_b && !cfg_is_real(&b)) st->bad++;
        if (nv > CONFIG_HISTORY_MAX)    st->bad++;
        if (cur_ver == 0 && have_a)     st->bad++;   /* version must agree */

        /* Every so often hold a reference across a delay: that is when the
         * publisher is most likely to swap and free underneath us. */
        if ((iter++ & 63u) == 0u) {
            const struct Config *h = config_acquire();
            if (h) {
                struct Config snap = *h;
                usleep(300);
                if (!cfg_is_real(h) || !cfg_equal(h, &snap)) st->bad++;
                config_release(h);
                st->held++;
            }
        }
        st->calls++;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static size_t alive_objects(void)
{
    size_t a = config_alloc_count();
    size_t f = config_free_count();
    return (a >= f) ? (a - f) : 0;
}

/* Wait until the publisher has produced `want` distinct versions, or give up.
 * While waiting, keep checking that the config we are holding is intact. */
static bool wait_for_versions(uint32_t want, const struct Config *held,
                              const struct Config *snap, uint64_t budget_us,
                              uint64_t *checks_out, bool *intact_out)
{
    uint64_t t0 = cloudcfg_now_us();
    uint64_t checks = 0;
    bool intact = true;

    while (cloudcfg_now_us() - t0 < budget_us) {
        if (held) {
            if (!cfg_is_real(held) || !cfg_equal(held, snap)) intact = false;
            checks++;
        }
        if (atomic_load(&g_truth_top) >= want && config_version() >= want)
            break;
        usleep(2000);
    }
    *checks_out = checks;
    *intact_out = intact;
    return atomic_load(&g_truth_top) >= want;
}

/* ------------------------------------------------------------------ */
/* test                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    uint32_t hist[CONFIG_HISTORY_MAX + 4];
    uint32_t main_rng = 0x9E3779B9u;

    /* Freeze the cloud at version 1 first, so the checks right after init are
     * deterministic: the fetch thread keeps getting "no change". */
    atomic_store(&g_vmode, VMODE_FREEZE);

    printf("== Part 1: init and publish ==\n");
    if (config_store_init() != 0) {
        printf("  config_store_init() failed\n");
        return 1;
    }
    check("config_store_init() == 0", true);
    check("cloud was actually read during init", atomic_load(&g_truth_top) == 1);
    check_u32("config_version() after init", config_version(), 1);

    {
        const struct Config *c = config_acquire();
        check("config_acquire() != NULL right after init", c != NULL);
        if (c) {
            struct Config want;
            check("acquired config checksums",
                  c->checksum == config_checksum(c));
            check("acquired config matches the cloud record",
                  truth_expect(c->version, &want) && cfg_equal(c, &want));
            check_u32("config_version() == acquired version",
                      config_version(), c->version);
            config_release(c);
        }
        check("history after init == [1]",
              config_history(hist, CONFIG_HISTORY_MAX) == 1 && hist[0] == 1);
        const struct Config *v1 = config_get_version(1);
        check("config_get_version(1) != NULL", v1 != NULL);
        if (v1) { check("...and is version 1", v1->version == 1); config_release(v1); }
        check("config_get_version(never published) == NULL",
              config_get_version(0xC0FFEEu) == NULL);
        check("config_get_version(0) == NULL", config_get_version(0) == NULL);
    }

    /* ------------------------------------------------------------------ */
    printf("\n== Part 1: a reader holds a config across many publishes ==\n");
    {
        const struct Config *held = config_acquire();
        struct Config snap;
        uint32_t held_ver = 0;
        uint64_t checks = 0;
        bool intact = true, got_enough;

        if (held) { snap = *held; held_ver = held->version; }
        atomic_store(&g_vmode, VMODE_NORMAL);           /* let it publish */

        /* Enough versions that the ring wraps and the held one is evicted. */
        got_enough = wait_for_versions(CONFIG_HISTORY_MAX + 3, held,
                                       held ? &snap : NULL, 3000000ull,
                                       &checks, &intact);

        printf("  published %llu versions, %llu no-change replies, "
               "%llu re-reads of the held config\n",
               (unsigned long long)atomic_load(&g_fetch_new),
               (unsigned long long)atomic_load(&g_fetch_nochange),
               (unsigned long long)checks);

        check("publisher produced > CONFIG_HISTORY_MAX versions", got_enough);
        check("held config never changed under the reader",
              held != NULL && intact && checks > 0);
        check("held config still checksums after the publishes",
              held != NULL && held->checksum == config_checksum(held));
        check("current version moved on while we held the old one",
              held != NULL && config_version() > held_ver);
        check("evicted version is no longer findable",
              held != NULL && config_get_version(held_ver) == NULL);
        check("allocator hooks report allocations",
              config_alloc_count() > CONFIG_HISTORY_MAX);
        check("eviction actually freed older configs",
              config_free_count() > 0);
        config_release(held);
    }

    /* ------------------------------------------------------------------ */
    printf("\n== Part 2: history ring ==\n");
    atomic_store(&g_vmode, VMODE_FREEZE);
    usleep(FETCH_MAX_US * 3);          /* let any in-flight publish land */
    {
        size_t n = config_history(hist, CONFIG_HISTORY_MAX + 4);
        bool ordered = (n > 0), all_found = (n > 0), all_real = (n > 0);

        printf("  history (newest first):");
        for (size_t i = 0; i < n; i++) printf(" %u", hist[i]);
        printf("\n");

        for (size_t i = 1; i < n; i++)
            if (hist[i] >= hist[i - 1]) ordered = false;

        for (size_t i = 0; i < n; i++) {
            const struct Config *c = config_get_version(hist[i]);
            if (!c) { all_found = false; continue; }
            struct Config want;
            if (c->version != hist[i] || c->checksum != config_checksum(c) ||
                !truth_expect(c->version, &want) || !cfg_equal(c, &want))
                all_real = false;
            config_release(c);
        }

        check_u32("history holds exactly CONFIG_HISTORY_MAX entries",
                  (uint32_t)n, (uint32_t)CONFIG_HISTORY_MAX);
        check("history is newest-first, strictly decreasing", ordered);
        check("newest history entry == config_version()",
              n > 0 && hist[0] == config_version());
        check("every listed version can be looked up", all_found);
        check("every looked-up version matches the cloud record", all_real);
        check("config_history(buf, 3) returns 3",
              config_history(hist, 3) == 3);
        check("config_history(NULL, 4) == 0 (no crash)",
              config_history(NULL, 4) == 0);
        check("config_history(buf, 0) == 0",
              config_history(hist, 0) == 0);

        printf("  live configs: %zu (alloc %zu / free %zu)\n",
               alive_objects(), config_alloc_count(), config_free_count());
        check("live configs bounded by the ring size",
              alive_objects() <= CONFIG_HISTORY_MAX + 2);
    }

    /* ------------------------------------------------------------------ */
    printf("\n== Part 2: rollback ==\n");
    {
        size_t n = config_history(hist, CONFIG_HISTORY_MAX + 4);
        uint32_t newest = n ? hist[0] : 0;
        uint32_t oldest = n ? hist[n - 1] : 0;
        uint32_t gone   = (oldest > 1) ? oldest - 1 : 0;   /* already evicted */

        check("rollback to the oldest remembered version",
              n > 0 && config_rollback_to(oldest) == 0);
        check_u32("config_version() after rollback", config_version(), oldest);
        {
            const struct Config *c = config_acquire();
            struct Config want;
            check("acquire() after rollback returns that version",
                  c != NULL && c->version == oldest);
            check("rolled-back config matches the cloud record",
                  c != NULL && c->checksum == config_checksum(c) &&
                  truth_expect(oldest, &want) && cfg_equal(c, &want));
            if (c) config_release(c);
        }
        check("rollback to an evicted version fails",
              config_rollback_to(gone) == -1);
        check("rollback to a version that never existed fails",
              config_rollback_to(0xC0FFEEu) == -1);
        check_u32("a failed rollback leaves the version alone",
                  config_version(), oldest);
        check("history is unchanged by a rollback",
              config_history(hist, CONFIG_HISTORY_MAX + 4) == n);
        check("roll forward again to the newest version",
              config_rollback_to(newest) == 0);
        check_u32("config_version() after rolling forward",
                  config_version(), newest);
    }

    /* ------------------------------------------------------------------ */
    printf("\n== Error path: a burst of link-down fetches ==\n");
    {
        struct Config snap;
        bool have_snap = false;
        uint32_t before = config_version();
        uint64_t fails0 = atomic_load(&g_fetch_fails);
        const struct Config *c = config_acquire();

        if (c) { snap = *c; have_snap = true; config_release(c); }

        atomic_store(&g_vmode, VMODE_LINKDOWN);
        usleep(300 * 1000);
        uint64_t burst = atomic_load(&g_fetch_fails) - fails0;
        printf("  cloudcfg_fetch() returned -1 %llu times\n",
               (unsigned long long)burst);

        check("the link-down path was really exercised", burst >= 3);
        check_u32("version unchanged while the link is down",
                  config_version(), before);
        c = config_acquire();
        check("last good config still served during the outage",
              have_snap && c != NULL && cfg_equal(c, &snap) &&
              c->checksum == config_checksum(c));
        if (c) config_release(c);
        atomic_store(&g_vmode, VMODE_NORMAL);
    }

    /* ------------------------------------------------------------------ */
    printf("\n== Getters must not wait for the vendor call ==\n");
    {
        /* One measuring thread, publisher running normally, so the numbers are
         * lock-and-copy cost and not scheduler noise from four hot readers.
         * Percentiles rather than the single worst sample: under a sanitizer
         * the OS can steal tens of ms from any thread, which says nothing
         * about whether the getter waited on the network. */
        static uint64_t lat[4096];
        size_t nlat = 0;
        uint64_t news0 = atomic_load(&g_fetch_new) + atomic_load(&g_fetch_nochange);
        uint64_t t_end = cloudcfg_now_us() + 400000ull;
        bool got_cfg = false;

        while (nlat < sizeof lat / sizeof lat[0] && cloudcfg_now_us() < t_end) {
            uint32_t vs[CONFIG_HISTORY_MAX];
            uint64_t t0 = cloudcfg_now_us();
            const struct Config *c = config_acquire();
            (void)config_version();
            size_t k = config_history(vs, CONFIG_HISTORY_MAX);
            if (k) {
                const struct Config *c2 = config_get_version(vs[k - 1]);
                if (c2) config_release(c2);
            }
            if (c) { got_cfg = true; config_release(c); }
            uint64_t t1 = cloudcfg_now_us();
            lat[nlat++] = (t1 > t0) ? t1 - t0 : 0;
            usleep(100);              /* spread samples over many vendor calls */
        }
        qsort(lat, nlat, sizeof lat[0], cmp_u64);
        uint64_t p50 = nlat ? lat[nlat / 2] : 0;
        uint64_t p99 = nlat ? lat[(nlat * 99) / 100] : 0;
        uint64_t worst = nlat ? lat[nlat - 1] : 0;
        uint64_t vendor_calls =
            atomic_load(&g_fetch_new) + atomic_load(&g_fetch_nochange) - news0;

        printf("  %zu getter rounds over %llu vendor calls: "
               "p50 %llu us, p99 %llu us, worst %llu us "
               "(one vendor call blocks up to %d us)\n",
               nlat, (unsigned long long)vendor_calls,
               (unsigned long long)p50, (unsigned long long)p99,
               (unsigned long long)worst, FETCH_MAX_US);

        check("the getters returned a config at all", got_cfg);
        check("the vendor really was blocking during the measurement",
              vendor_calls >= 3);
        check("p50 getter round under 200 us", nlat > 50 && p50 < 200);
        check("p99 getter round nowhere near a vendor call",
              nlat > 50 && p99 < 2000);
    }

    /* ------------------------------------------------------------------ */
    printf("\n== Thread safety: 4 readers + 1 publisher for 2 s ==\n");
    {
        pthread_t th[4];
        stress_stat_t st[4];
        uint64_t calls = 0, bad = 0, held = 0, slow = 0, max_us = 0;
        uint64_t t_end;

        memset(st, 0, sizeof st);
        atomic_store(&g_stress_stop, false);
        for (int i = 0; i < 4; i++) {
            st[i].seed = 0x2545F491u + (uint32_t)i * 2654435761u;
            pthread_create(&th[i], NULL, stress_reader, &st[i]);
        }

        /* Mix rollbacks into the stress so publish and rollback race. */
        t_end = cloudcfg_now_us() + 2000000ull;
        while (cloudcfg_now_us() < t_end) {
            usleep(200 * 1000);
            size_t n = config_history(hist, CONFIG_HISTORY_MAX);
            if (n) {
                main_rng ^= main_rng << 13; main_rng ^= main_rng >> 17;
                main_rng ^= main_rng << 5;
                (void)config_rollback_to(hist[main_rng % n]);  /* may fail: fine */
            }
        }
        atomic_store(&g_stress_stop, true);

        for (int i = 0; i < 4; i++) {
            pthread_join(th[i], NULL);
            calls  += st[i].calls;
            bad    += st[i].bad;
            held   += st[i].held;
            slow   += st[i].slow;
            if (st[i].max_us > max_us) max_us = st[i].max_us;
        }

        printf("  %llu getter rounds, %llu long holds, %llu bad reads\n",
               (unsigned long long)calls, (unsigned long long)held,
               (unsigned long long)bad);
        check("readers made progress", calls > 1000);
        check("no torn, stale-freed or invented config was ever read", bad == 0);

        printf("  rounds slower than %d us: %llu of %llu "
               "(worst %llu us, worst vendor block %llu us)\n",
               SLOW_ROUND_US, (unsigned long long)slow,
               (unsigned long long)calls, (unsigned long long)max_us,
               (unsigned long long)atomic_load(&g_fetch_max_block_us));
        check("virtually no getter round waited on anything",
              calls > 1000 && slow * 100 < calls);

        printf("  live configs after stress: %zu (alloc %zu / free %zu)\n",
               alive_objects(), config_alloc_count(), config_free_count());
        check("live configs still bounded after the stress",
              alive_objects() <= CONFIG_HISTORY_MAX + 8);
    }

    /* ------------------------------------------------------------------ */
    printf("\n== Config storm: publish as fast as the readers can look ==\n");
    {
        /* The use-after-free window is only a few instructions wide, so it
         * needs a high publish rate to show up at all: every publish frees the
         * previous config the instant its last reference goes away, which is
         * exactly when a reader may be between "load the pointer" and "claim a
         * reference".  Run this under -fsanitize=address (./main.sh asan). */
        pthread_t th[4];
        stress_stat_t st[4];
        uint64_t calls = 0, bad = 0, v0, v1;

        memset(st, 0, sizeof st);
        v0 = config_version();
        atomic_store(&g_stress_stop, false);
        for (int i = 0; i < 4; i++) {
            st[i].seed = 0x85EBCA6Bu + (uint32_t)i * 374761393u;
            pthread_create(&th[i], NULL, stress_reader, &st[i]);
        }
        atomic_store(&g_vmode, VMODE_STORM);

        /* Rolling back to the OLDEST remembered version is what makes the race
         * reachable.  Normally the current config is also sitting in the
         * history ring, so a publish drops it from two owners to one and never
         * frees it.  After a rollback the ring evicts that old version within a
         * few publishes and the `current` slot becomes its LAST owner — so the
         * next publish frees it the moment it is swapped out, which is exactly
         * when a reader may be between loading the pointer and claiming a
         * reference on it. */
        {
            uint64_t t_end = cloudcfg_now_us() + 600000ull;
            while (cloudcfg_now_us() < t_end) {
                size_t n = config_history(hist, CONFIG_HISTORY_MAX);
                if (n) (void)config_rollback_to(hist[n - 1]);
                usleep(200);
            }
        }
        atomic_store(&g_stress_stop, true);
        for (int i = 0; i < 4; i++) {
            pthread_join(th[i], NULL);
            calls += st[i].calls;
            bad   += st[i].bad;
        }
        atomic_store(&g_vmode, VMODE_NORMAL);
        v1 = config_version();

        printf("  %llu versions published during %llu getter rounds "
               "(alloc %zu / free %zu / live %zu)\n",
               (unsigned long long)(v1 - v0), (unsigned long long)calls,
               config_alloc_count(), config_free_count(), alive_objects());
        check("the storm really published hundreds of versions", v1 - v0 > 200);
        check("no reader ever touched a freed or recycled config", bad == 0);
        check("configs are being reclaimed, not accumulated",
              alive_objects() <= CONFIG_HISTORY_MAX + 8);
    }

    /* ------------------------------------------------------------------ */
    printf("\n== Shutdown: reference counting vs deinit ==\n");
    {
        const struct Config *kept = config_acquire();
        struct Config snap;
        bool have = (kept != NULL);
        size_t allocs;

        if (have) snap = *kept;

        config_store_deinit();
        allocs = config_alloc_count();

        printf("  after deinit: alloc %zu / free %zu / live %zu\n",
               allocs, config_free_count(), alive_objects());
        check("configs were allocated during the run", allocs > 0);
        check_u32("exactly the reader's config is still alive",
                  (uint32_t)alive_objects(), 1u);
        check("a config borrowed before deinit is still readable",
              have && kept->checksum == config_checksum(kept) &&
              cfg_equal(kept, &snap));

        config_release(kept);
        printf("  after the last release: alloc %zu / free %zu\n",
               config_alloc_count(), config_free_count());
        check("no leaks: every config was freed",
              config_free_count() == config_alloc_count() && config_alloc_count() > 0);
        check("no double frees: frees never exceeded allocs",
              config_free_count() <= config_alloc_count());
    }

    printf("\nvendor calls: %llu new, %llu no-change, %llu link-down\n",
           (unsigned long long)atomic_load(&g_fetch_new),
           (unsigned long long)atomic_load(&g_fetch_nochange),
           (unsigned long long)atomic_load(&g_fetch_fails));

    if (fails) printf("\n%d CHECK(S) FAILED\n", fails);
    else       printf("\nALL CHECKS PASSED (0 failed)\n");
    return fails ? 1 : 0;
}
