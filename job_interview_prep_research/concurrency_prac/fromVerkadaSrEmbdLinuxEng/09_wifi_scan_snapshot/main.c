/* main.c — test harness + fake GW31-E radio for 09_wifi_scan_snapshot.
 *
 * GIVEN. Do not modify: it is used unchanged against ap_table.c and
 * ap_table_solution.c.
 *
 *   ./main.sh        main.c + ap_table.c           (your code)
 *   ./main.sh sol    main.c + ap_table_solution.c  (reference)
 *   ./main.sh tsan   reference under -fsanitize=thread
 *
 * How the harness knows the right answer (no hard-coded expected values —
 * those break the moment a timing detail changes):
 *
 *  1. GROUND TRUTH. wifi_scan() logs every scan it ever returns: the
 *     generation number, the tag, the timestamp and the full entry array.
 *     Everything below is checked against that log.
 *
 *  2. TEAR DETECTION. Every AP in one scan carries the same generation tag
 *     in its `channel` field (a real radio reports the real channel; this
 *     fake abuses the field as a tag). So a snapshot is verified by:
 *     take entry[0]'s tag -> find that scan in the log -> memcmp the WHOLE
 *     array. A snapshot stitched from two scans, or one entry short, or one
 *     stale entry, fails the memcmp. The BSSIDs stay stable across scans so
 *     Part 2 still has a real key to aggregate on.
 *
 *  3. VIRTUAL CLOCK. wifi_now_us() = gettimeofday + a skew the harness can
 *     bump. After the scanner is stopped the harness jumps the clock 5 min
 *     forward to test time-based eviction without waiting 5 minutes.
 *
 *  4. Randomness is a fixed-seed xorshift (never rand(): the sequence
 *     differs between glibc and macOS libc). The scan CONTENT is a pure
 *     function of the generation number, so it is identical everywhere;
 *     only the scan durations come from the RNG.
 */
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "ap_table.h"
#include "wifi.h"

/* ------------------------------------------------------------------ */
/* knobs                                                               */
/* ------------------------------------------------------------------ */

#define AP_POOL       40      /* distinct APs in the fake RF environment  */
#define SCAN_MS_MIN   40      /* real radio: 100 ms                      */
#define SCAN_MS_MAX  120      /* real radio: 300 ms                      */
#define BUSY_EVERY     7      /* every 7th call returns -1 (radio busy)  */
#define SCAN_LOG_MAX 256
#define PHASE_A_US  2000000ull
#define STRESS_US   2000000ull
#define STRESS_THREADS 4
#define SLOW_US 2000             /* a getter over 2 ms is an outlier, not normal */

/* ------------------------------------------------------------------ */
/* virtual clock                                                       */
/* ------------------------------------------------------------------ */

static _Atomic uint64_t g_clock_skew_us;

uint64_t wifi_now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000ull * (uint64_t)tv.tv_sec + (uint64_t)tv.tv_usec
         + atomic_load_explicit(&g_clock_skew_us, memory_order_relaxed);
}

/* ------------------------------------------------------------------ */
/* the fake RF environment                                             */
/* ------------------------------------------------------------------ */

/* Stable identity per AP. bssid[4] counts DOWN so that AP #1 sorts before
 * AP #0 by memcmp — the tie-break rule gets exercised for real. */
static void pool_bssid(int i, uint8_t out[6])
{
    out[0] = 0x02;  /* locally administered */
    out[1] = 0x1A;
    out[2] = 0x2B;
    out[3] = 0x3C;
    out[4] = (uint8_t)(0xFFu - (unsigned)i);
    out[5] = (uint8_t)((unsigned)i * 37u);
}

/* APs #0 and #1 deliberately share a base AND a jitter phase, so their
 * smoothed RSSI is bit-identical: an exact tie at the top of the ranking.
 * Everyone else is 2 dBm apart, and the jitter is only 0 or -1 dBm, so no
 * two other APs can ever land on the same smoothed value. */
static int pool_base_rssi(int i) { return (i <= 1) ? -30 : (-30 - 2 * i); }

static int pool_rssi(int i, unsigned gen)
{
    unsigned phase = (i <= 1) ? (gen % 2u) : ((gen + (unsigned)i) % 2u);
    return pool_base_rssi(i) - (int)phase;
}

/* How many APs generation `gen` reports. Hits both edges on purpose:
 *   gen % 7 == 4  -> 0 APs  (empty scan is legal, not an error)
 *   gen % 5 == 3  -> WIFI_MAX_APS (the full buffer) */
static size_t found_for_gen(unsigned gen)
{
    if (gen % 7u == 4u) return 0u;
    if (gen % 5u == 3u) return (size_t)WIFI_MAX_APS;
    return (size_t)(5u + (gen * 7u) % 24u);      /* 5..28 */
}

/* Which pool AP sits at slot j of generation `gen`. Slots 0,1 are always
 * the tie pair; the rest is a rotating window so every AP gets seen. */
static int scan_member(unsigned gen, size_t j)
{
    if (j < 2) return (int)j;
    return (int)(2u + (unsigned)((gen * 5u + (unsigned)(j - 2u)) % (AP_POOL - 2u)));
}

/* ------------------------------------------------------------------ */
/* ground truth: every scan the radio ever returned                    */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned      gen;
    uint8_t       tag;
    size_t        n;
    uint64_t      t_us;
    struct ApInfo ap[WIFI_MAX_APS];
} scanrec_t;

static scanrec_t       g_log[SCAN_LOG_MAX];
static size_t          g_log_n;
static bool            g_log_overflow;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

static void log_record(unsigned gen, uint8_t tag, const struct ApInfo *ap, size_t n)
{
    pthread_mutex_lock(&g_log_lock);
    if (g_log_n < SCAN_LOG_MAX) {
        scanrec_t *r = &g_log[g_log_n++];
        r->gen = gen;
        r->tag = tag;
        r->n = n;
        r->t_us = wifi_now_us();
        memset(r->ap, 0, sizeof r->ap);
        if (n) memcpy(r->ap, ap, n * sizeof *ap);
    } else {
        g_log_overflow = true;
    }
    pthread_mutex_unlock(&g_log_lock);
}

static size_t log_count(void)
{
    pthread_mutex_lock(&g_log_lock);
    size_t n = g_log_n;
    pthread_mutex_unlock(&g_log_lock);
    return n;
}

/* Verify a snapshot against the log. Returns the generation it came from,
 * or -1 if it matches no recorded scan byte for byte. */
static long log_verify(const struct ApInfo *snap, size_t n)
{
    if (n == 0) return -1;                  /* callers handle empty separately */
    uint8_t tag = snap[0].channel;
    long gen = -1;
    pthread_mutex_lock(&g_log_lock);
    for (size_t i = g_log_n; i-- > 0;) {
        if (g_log[i].tag != tag) continue;
        if (g_log[i].n == n && memcmp(g_log[i].ap, snap, n * sizeof *snap) == 0)
            gen = (long)g_log[i].gen;
        break;                              /* newest scan with that tag */
    }
    pthread_mutex_unlock(&g_log_lock);
    return gen;
}

/* ------------------------------------------------------------------ */
/* the fake radio (the "vendor API")                                   */
/* ------------------------------------------------------------------ */

static uint32_t g_rng = 0xC0FFEE11u;        /* touched only by wifi_scan */
static uint32_t xs32(void)
{
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g_rng = x;
    return x;
}

int wifi_scan(struct ApInfo *out, size_t max, size_t *found)
{
    static unsigned calls;                  /* single caller: plain is fine */
    static unsigned gen;

    if (++calls % BUSY_EVERY == 0u)
        return -1;                          /* busy: returns immediately */

    unsigned ms = SCAN_MS_MIN + xs32() % (SCAN_MS_MAX - SCAN_MS_MIN + 1u);
    usleep(ms * 1000u);

    gen++;
    size_t n = found_for_gen(gen);
    uint8_t tag = (uint8_t)(1u + gen % 250u);

    struct ApInfo tmp[WIFI_MAX_APS];
    memset(tmp, 0, sizeof tmp);
    for (size_t j = 0; j < n; j++) {
        int i = scan_member(gen, j);
        pool_bssid(i, tmp[j].bssid);
        snprintf(tmp[j].ssid, sizeof tmp[j].ssid, "GW31-AP-%02d", i);
        tmp[j].rssi_dbm = (int8_t)pool_rssi(i, gen);
        tmp[j].channel  = tag;
    }

    log_record(gen, tag, tmp, n);

    if (found) *found = n;
    size_t c = n < max ? n : max;
    if (out && c) memcpy(out, tmp, c * sizeof *out);
    return 0;
}

/* ------------------------------------------------------------------ */
/* check plumbing                                                      */
/* ------------------------------------------------------------------ */

static int fails;

static void ok(const char *label, bool cond)
{
    printf("  %-46s %s\n", label, cond ? "PASS" : "FAIL");
    if (!cond) fails++;
}

static void ok_sz(const char *label, size_t got, size_t want)
{
    bool c = (got == want);
    printf("  %-46s got %-6zu want %-6zu %s\n", label, got, want, c ? "PASS" : "FAIL");
    if (!c) fails++;
}

static bool close_enough(float a, float b) { return fabsf(a - b) <= 1e-3f; }

/* ------------------------------------------------------------------ */
/* reference AP table (brute force, rebuilt from the scan log)          */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  bssid[6];
    char     ssid[33];
    float    rssi;
    uint32_t seen;
    uint64_t last;
} ref_t;

static ref_t  g_ref[AP_POOL + 8];
static size_t g_ref_n;
static size_t g_order[AP_POOL + 8];         /* indices, strongest first */

static void ref_build(void)
{
    g_ref_n = 0;
    pthread_mutex_lock(&g_log_lock);
    for (size_t s = 0; s < g_log_n; s++) {
        for (size_t j = 0; j < g_log[s].n; j++) {
            const struct ApInfo *a = &g_log[s].ap[j];
            size_t k;
            for (k = 0; k < g_ref_n; k++)
                if (memcmp(g_ref[k].bssid, a->bssid, 6) == 0) break;
            if (k == g_ref_n) {
                if (g_ref_n == sizeof g_ref / sizeof g_ref[0]) continue;
                g_ref_n++;
                memcpy(g_ref[k].bssid, a->bssid, 6);
                snprintf(g_ref[k].ssid, sizeof g_ref[k].ssid, "%s", a->ssid);
                g_ref[k].rssi = (float)a->rssi_dbm;   /* first sighting seeds */
                g_ref[k].seen = 1;
            } else {
                /* identical statements to the implementation's EWMA */
                float d = (float)a->rssi_dbm - g_ref[k].rssi;
                d *= AP_EWMA_ALPHA;
                g_ref[k].rssi += d;
                g_ref[k].seen++;
            }
            g_ref[k].last = g_log[s].t_us;
        }
    }
    pthread_mutex_unlock(&g_log_lock);
}

/* strongest first: rssi desc, exact ties by BSSID ascending */
static bool ref_stronger(const ref_t *a, const ref_t *b)
{
    if (a->rssi != b->rssi) return a->rssi > b->rssi;
    return memcmp(a->bssid, b->bssid, 6) < 0;
}

static void ref_sort(void)
{
    for (size_t i = 0; i < g_ref_n; i++) g_order[i] = i;
    for (size_t i = 1; i < g_ref_n; i++) {      /* insertion sort: N <= 48 */
        size_t v = g_order[i], j = i;
        while (j > 0 && ref_stronger(&g_ref[v], &g_ref[g_order[j - 1]])) {
            g_order[j] = g_order[j - 1];
            j--;
        }
        g_order[j] = v;
    }
}

static bool is_pool_bssid(const uint8_t b[6])
{
    uint8_t want[6];
    for (int i = 0; i < AP_POOL; i++) {
        pool_bssid(i, want);
        if (memcmp(want, b, 6) == 0) return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* stress: thread safety + "the getters never block" evidence           */
/* ------------------------------------------------------------------ */

static _Atomic bool g_stress_stop;

typedef struct {
    int      id;
    uint64_t calls, bad, hits;
    uint64_t max_snap, max_lookup, max_topk;
} sstat_t;

static void *stress_reader(void *arg)
{
    sstat_t *st = arg;
    struct ApInfo  snap[WIFI_MAX_APS];
    struct ApEntry top[8];
    int probe = st->id;

    while (!atomic_load_explicit(&g_stress_stop, memory_order_relaxed)) {
        uint64_t t0, dt;

        /* Part 1: the snapshot must be exactly one recorded scan */
        t0 = wifi_now_us();
        size_t n = wifi_get_snapshot(snap, WIFI_MAX_APS);
        dt = wifi_now_us() - t0;
        if (dt > st->max_snap) st->max_snap = dt;
        if (n > WIFI_MAX_APS)            st->bad++;
        else if (n > 0 && log_verify(snap, n) < 0) st->bad++;

        /* Part 2: lookup */
        uint8_t key[6];
        pool_bssid(probe % AP_POOL, key);
        probe += STRESS_THREADS;
        struct ApEntry e;
        memset(&e, 0, sizeof e);
        t0 = wifi_now_us();
        int r = wifi_lookup(key, &e);
        dt = wifi_now_us() - t0;
        if (dt > st->max_lookup) st->max_lookup = dt;
        if (r == 0) {
            st->hits++;
            if (memcmp(e.bssid, key, 6) != 0) st->bad++;
            if (!(e.rssi_smoothed <= -20.0f && e.rssi_smoothed >= -120.0f)) st->bad++;
            if (e.times_seen == 0) st->bad++;
        }

        /* Part 2: top-k */
        t0 = wifi_now_us();
        size_t m = wifi_top_k(top, 8);
        dt = wifi_now_us() - t0;
        if (dt > st->max_topk) st->max_topk = dt;
        if (m > 8) { st->bad++; m = 0; }
        for (size_t i = 0; i < m; i++) {
            if (!is_pool_bssid(top[i].bssid)) st->bad++;
            if (top[i].times_seen == 0) st->bad++;
            if (i > 0) {
                if (top[i - 1].rssi_smoothed < top[i].rssi_smoothed) st->bad++;
                if (top[i - 1].rssi_smoothed == top[i].rssi_smoothed &&
                    memcmp(top[i - 1].bssid, top[i].bssid, 6) >= 0) st->bad++;
            }
        }
        st->calls++;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct ApInfo  snap[WIFI_MAX_APS];
    struct ApEntry got[72];

    printf("== 09_wifi_scan_snapshot harness ==\n");
    printf("   fake radio: %d APs in range, scan blocks %d-%d ms "
           "(real hw: 100-300 ms), 1 call in %d returns BUSY\n",
           AP_POOL, SCAN_MS_MIN, SCAN_MS_MAX, BUSY_EVERY);
    printf("   window AP_STALE_US = %llu us, table cap = %u, alpha = %g\n",
           (unsigned long long)AP_STALE_US, AP_TABLE_CAP, (double)AP_EWMA_ALPHA);

    if (wifi_table_init() != 0) {
        printf("wifi_table_init() failed\n");
        return 1;
    }

    /* ---------------- Part 1: right after init -------------------- */
    printf("\n== Part 1: snapshot right after init ==\n");
    size_t n0 = wifi_get_snapshot(snap, WIFI_MAX_APS);
    long g0 = log_verify(snap, n0);
    printf("   snapshot has %zu AP(s), from scan generation %ld\n", n0, g0);
    ok("init took one scan synchronously (n > 0)", n0 > 0);
    ok("snapshot is byte-identical to a logged scan", g0 > 0);
    ok("that scan is generation 1 or 2", g0 == 1 || g0 == 2);
    ok_sz("count matches that generation's AP count",
          n0, g0 > 0 ? found_for_gen((unsigned)g0) : (size_t)-1);

    size_t nz = wifi_get_snapshot(snap, 0);
    ok_sz("max == 0 copies nothing", nz, 0);

    struct ApInfo small[4];
    memset(small, 0, sizeof small);
    size_t ns = wifi_get_snapshot(small, 3);
    ok_sz("small buffer is clamped to max", ns, 3);
    ok("clamped entries all carry one generation tag",
       ns == 3 && small[0].channel == small[1].channel &&
       small[1].channel == small[2].channel && small[0].channel != 0);

    /* ---------------- Part 1: variable length --------------------- */
    printf("\n== Part 1: variable-length snapshots over ~2 s ==\n");
    size_t iters = 0, saw_empty = 0, saw_full = 0, mism = 0, distinct = 0;
    long last_gen = -1;
    uint64_t t_end = wifi_now_us() + PHASE_A_US;
    while (wifi_now_us() < t_end) {
        size_t m = wifi_get_snapshot(snap, WIFI_MAX_APS);
        iters++;
        if (m == 0) { saw_empty++; continue; }
        if (m == (size_t)WIFI_MAX_APS) saw_full++;
        long g = log_verify(snap, m);
        if (g < 0) { mism++; continue; }
        if (g != last_gen) { distinct++; last_gen = g; }
    }
    printf("   %zu reads, %zu distinct generations, %zu empty, %zu full, %zu mismatched\n",
           iters, distinct, saw_empty, saw_full, mism);
    ok_sz("every non-empty snapshot matched a scan", mism, 0);
    ok("the radio really produced empty scans", found_for_gen(4) == 0);
    ok("observed an empty snapshot (0 APs)", saw_empty > 0);
    ok("observed a full snapshot (WIFI_MAX_APS)", saw_full > 0);
    ok("the scanner kept publishing (>= 10 generations)", distinct >= 10);

    /* ---------------- stress -------------------------------------- */
    printf("\n== Thread safety: %d readers for ~2 s while the radio scans ==\n",
           STRESS_THREADS);
    pthread_t th[STRESS_THREADS];
    sstat_t   st[STRESS_THREADS];
    memset(st, 0, sizeof st);
    atomic_store(&g_stress_stop, false);
    for (int i = 0; i < STRESS_THREADS; i++) {
        st[i].id = i;
        pthread_create(&th[i], NULL, stress_reader, &st[i]);
    }
    for (uint64_t slept = 0; slept < STRESS_US; slept += 200000ull)
        usleep(200 * 1000);                 /* usleep(>= 1 s) is unspecified */
    atomic_store(&g_stress_stop, true);
    uint64_t calls = 0, bad = 0, hits = 0, ms_snap = 0, ms_lk = 0, ms_tk = 0;
    for (int i = 0; i < STRESS_THREADS; i++) {
        pthread_join(th[i], NULL);
        calls += st[i].calls;
        bad   += st[i].bad;
        hits  += st[i].hits;
        if (st[i].max_snap   > ms_snap) ms_snap = st[i].max_snap;
        if (st[i].max_lookup > ms_lk)   ms_lk   = st[i].max_lookup;
        if (st[i].max_topk   > ms_tk)   ms_tk   = st[i].max_topk;
    }
    printf("   %llu rounds, %llu lookup hits, %llu invariant violations\n",
           (unsigned long long)calls, (unsigned long long)hits,
           (unsigned long long)bad);
    printf("   worst latency under 4-way contention: snapshot %llu us, "
           "lookup %llu us, top_k %llu us (info only)\n",
           (unsigned long long)ms_snap, (unsigned long long)ms_lk,
           (unsigned long long)ms_tk);
    ok("readers made progress", calls > 0);
    ok_sz("no torn / invented / misordered values", (size_t)bad, 0);
    ok("lookups hit while scans were running", hits > 0);
    /* No verdict on latency here: lookup/top_k share one mutex by design, so
     * their worst case under four-way contention is a queueing number, not
     * evidence about the radio. That is measured on its own below. */

    /* ---------------- getter latency vs the radio ------------------ */
    printf("\n== Getter latency: one reader, radio still scanning ==\n");
    {
        uint64_t w[3] = {0, 0, 0};          /* worst per getter          */
        uint64_t slow[3] = {0, 0, 0};       /* calls over SLOW_US        */
        uint64_t rounds = 0;
        uint8_t key[6];
        pool_bssid(0, key);
        struct ApEntry top[8], e;
        static const char *name[3] = {"wifi_get_snapshot", "wifi_lookup", "wifi_top_k"};
        uint64_t stop = wifi_now_us() + 500000ull;

        while (wifi_now_us() < stop) {
            for (int which = 0; which < 3; which++) {
                uint64_t t0 = wifi_now_us();
                switch (which) {
                case 0: (void)wifi_get_snapshot(snap, WIFI_MAX_APS); break;
                case 1: (void)wifi_lookup(key, &e); break;
                default: (void)wifi_top_k(top, 8); break;
                }
                uint64_t d = wifi_now_us() - t0;
                if (d > w[which]) w[which] = d;
                if (d > SLOW_US) slow[which]++;
            }
            rounds++;
        }
        printf("   %llu rounds; worst / calls over %d us:\n",
               (unsigned long long)rounds, SLOW_US);
        ok("rounds happened", rounds > 100);
        for (int which = 0; which < 3; which++) {
            printf("     %-18s worst %6llu us, %llu slow\n", name[which],
                   (unsigned long long)w[which], (unsigned long long)slow[which]);
            char label[80];
            /* Two bounds. (1) no single call ever sat through a scan — if a
             * getter touched the radio it would block >= SCAN_MS_MIN ms.
             * (2) slow calls are scheduler noise, not the norm: under 0.5%.
             * A one-off 20 ms outlier on a loaded machine is a descheduled
             * thread, not a design bug, so the worst case alone is not a
             * fair verdict. */
            snprintf(label, sizeof label, "%s never sat through a scan", name[which]);
            ok(label, w[which] < (uint64_t)SCAN_MS_MIN * 1000ull);
            snprintf(label, sizeof label, "%s is fast in the common case", name[which]);
            ok(label, slow[which] * 200ull <= rounds);
        }
    }

    /* ---------------- shutdown ------------------------------------ */
    printf("\n== Shutdown ==\n");
    size_t scans_before = log_count();
    wifi_table_deinit();
    size_t scans_at_deinit = log_count();
    usleep(400 * 1000);
    size_t scans_after = log_count();
    printf("   scans: %zu before deinit, %zu at deinit, %zu 400 ms later\n",
           scans_before, scans_at_deinit, scans_after);
    ok("radio was actually scanned", scans_after > 0);
    ok("scan log did not overflow", !g_log_overflow);
    ok("no scan after deinit (thread joined)", scans_after == scans_at_deinit);
    wifi_table_deinit();                    /* idempotent */
    ok("deinit is idempotent", log_count() == scans_after);

    /* ---------------- Part 2: vs brute-force reference ------------ */
    printf("\n== Part 2: AP table vs brute-force replay of every scan ==\n");
    ref_build();
    ref_sort();
    printf("   reference holds %zu distinct BSSIDs from %zu scans\n",
           g_ref_n, scans_after);
    ok("reference is non-empty", g_ref_n > 0);
    ok_sz("wifi_table_count == live reference entries", wifi_table_count(), g_ref_n);
    ok_sz("nothing was dropped (table big enough)", wifi_table_dropped(), 0);

    size_t lk_bad = 0, lk_hit = 0;
    for (size_t i = 0; i < g_ref_n; i++) {
        struct ApEntry e;
        memset(&e, 0, sizeof e);
        if (wifi_lookup(g_ref[i].bssid, &e) != 0) { lk_bad++; continue; }
        lk_hit++;
        if (memcmp(e.bssid, g_ref[i].bssid, 6) != 0)      lk_bad++;
        if (strcmp(e.ssid, g_ref[i].ssid) != 0)           lk_bad++;
        if (!close_enough(e.rssi_smoothed, g_ref[i].rssi)) {
            lk_bad++;
            printf("    rssi mismatch %s: got %f want %f\n",
                   g_ref[i].ssid, (double)e.rssi_smoothed, (double)g_ref[i].rssi);
        }
        if (e.times_seen != g_ref[i].seen) {
            lk_bad++;
            printf("    times_seen mismatch %s: got %u want %u\n",
                   g_ref[i].ssid, e.times_seen, g_ref[i].seen);
        }
        uint64_t d = e.last_seen_us > g_ref[i].last
                   ? e.last_seen_us - g_ref[i].last : g_ref[i].last - e.last_seen_us;
        if (d > 400000ull) lk_bad++;
    }
    ok_sz("every reference BSSID looked up", lk_hit, g_ref_n);
    ok_sz("all looked-up fields match the reference", lk_bad, 0);

    uint8_t nokey[6] = {0x0E, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    struct ApEntry dummy;
    memset(&dummy, 0, sizeof dummy);
    ok("unknown BSSID misses", wifi_lookup(nokey, &dummy) == -1);

    /* top-k for several k, strict order comparison */
    printf("\n== Part 2: top-k vs the sorted reference ==\n");
    ok_sz("k == 0 returns nothing", wifi_top_k(got, 0), 0);
    const size_t ks[] = {1, 3, 8, (size_t)AP_POOL, 72};
    for (size_t x = 0; x < sizeof ks / sizeof ks[0]; x++) {
        size_t k = ks[x];
        memset(got, 0, sizeof got);
        size_t m = wifi_top_k(got, k);
        size_t want = k < g_ref_n ? k : g_ref_n;
        char label[64];
        snprintf(label, sizeof label, "top_k(%zu) count", k);
        ok_sz(label, m, want);
        size_t bad_j = 0;
        for (size_t j = 0; j < m && j < want; j++) {
            const ref_t *r = &g_ref[g_order[j]];
            if (memcmp(got[j].bssid, r->bssid, 6) != 0) bad_j++;
            else if (!close_enough(got[j].rssi_smoothed, r->rssi)) bad_j++;
        }
        snprintf(label, sizeof label, "top_k(%zu) order matches reference", k);
        ok_sz(label, bad_j, 0);
    }

    /* the deliberate exact tie at the top of the ranking */
    printf("\n== Part 2: deterministic tie-break (rssi equal -> BSSID asc) ==\n");
    {
        uint8_t b0[6], b1[6];
        pool_bssid(0, b0);
        pool_bssid(1, b1);
        struct ApEntry e0, e1;
        bool have = wifi_lookup(b0, &e0) == 0 && wifi_lookup(b1, &e1) == 0;
        ok("both tie APs are in the table", have);
        if (have) {
            printf("   AP-00 rssi %f, AP-01 rssi %f (equal: %s)\n",
                   (double)e0.rssi_smoothed, (double)e1.rssi_smoothed,
                   e0.rssi_smoothed == e1.rssi_smoothed ? "yes" : "no");
            ok("the two strongest APs tie exactly",
               e0.rssi_smoothed == e1.rssi_smoothed);
            memset(got, 0, sizeof got);
            size_t m = wifi_top_k(got, 2);
            ok_sz("top_k(2) count", m, 2);
            ok("tie resolved by smaller BSSID first",
               m == 2 && memcmp(got[0].bssid, b1, 6) == 0 &&
               memcmp(got[1].bssid, b0, 6) == 0);
        }
    }

    /* ---------------- Part 2: time eviction ----------------------- */
    printf("\n== Part 2: eviction after the %llu s window "
           "(clock jumped, radio stopped) ==\n",
           (unsigned long long)(AP_STALE_US / 1000000ull));
    atomic_fetch_add(&g_clock_skew_us, AP_STALE_US + 2000000ull);
    ok_sz("live entries after the window", wifi_table_count(), 0);
    ok("lookup of a known BSSID now misses", wifi_lookup(g_ref[0].bssid, &dummy) == -1);
    ok_sz("top_k returns nothing", wifi_top_k(got, 8), 0);

    /* ---------------- Part 2: table full ------------------------- */
    printf("\n== Part 2: hash map full (%u slots, %u distinct APs offered) ==\n",
           AP_TABLE_CAP, 4u * AP_TABLE_CAP);
    {
        size_t offered = 4u * AP_TABLE_CAP;
        size_t dropped_before = wifi_table_dropped();
        for (size_t i = 0; i < offered; i++) {
            struct ApInfo a;
            memset(&a, 0, sizeof a);
            a.bssid[0] = 0x06;
            a.bssid[1] = 0x11;
            a.bssid[2] = (uint8_t)(i >> 8);
            a.bssid[3] = (uint8_t)i;
            a.bssid[4] = (uint8_t)(i * 13u);
            a.bssid[5] = (uint8_t)(i * 7u + 3u);
            snprintf(a.ssid, sizeof a.ssid, "SYNTH-%04zu", i);
            a.rssi_dbm = (int8_t)(-40 - (int)(i % 30));
            a.channel = 6;
            wifi_table_observe(&a, 1);
        }
        size_t dropped = wifi_table_dropped() - dropped_before;
        printf("   stored %zu, dropped %zu\n", wifi_table_count(), dropped);
        ok_sz("table holds exactly AP_TABLE_CAP entries",
              wifi_table_count(), AP_TABLE_CAP);
        ok_sz("every AP past capacity was dropped, not lost silently",
              dropped, offered - AP_TABLE_CAP);

        size_t miss_early = 0, hit_late = 0;
        for (size_t i = 0; i < offered; i++) {
            uint8_t b[6] = {0x06, 0x11, (uint8_t)(i >> 8), (uint8_t)i,
                            (uint8_t)(i * 13u), (uint8_t)(i * 7u + 3u)};
            int r = wifi_lookup(b, &dummy);
            if (i < AP_TABLE_CAP) { if (r != 0) miss_early++; }
            else                  { if (r == 0) hit_late++; }
        }
        ok_sz("the first CAP inserts are all still found", miss_early, 0);
        ok_sz("the dropped ones are absent (no phantom hits)", hit_late, 0);
    }

    printf("\n");
    if (fails) printf("%d CHECK(S) FAILED\n", fails);
    else       printf("ALL CHECKS PASSED (0 failed)\n");
    return fails ? 1 : 0;
}
