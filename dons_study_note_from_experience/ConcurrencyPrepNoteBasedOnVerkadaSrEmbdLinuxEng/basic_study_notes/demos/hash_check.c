/* hash_check.c — verify the open-addressing map from note C6 against brute force.
 *   cc -std=c11 -O2 -Wall -Wextra -o hash_check hash_check.c && ./hash_check
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- the map under test (same shape as 07의 해답) ---------------- */
#define CAP     1024u
#define MASK    (CAP - 1u)
#define WINDOW  16u
#define RETENTION 30000000ull      /* 30 s in us */

typedef struct {
    uint32_t id;
    uint64_t last_seen;
    bool     used;
} slot_t;

static slot_t map[CAP];
static uint64_t g_live, g_evict, g_reuse;

static uint32_t home_fib(uint32_t id) { return (uint32_t)(id * 2654435761u) & MASK; }
static uint32_t home_mask(uint32_t id) { return id & MASK; }

static bool stale(const slot_t *s, uint64_t now)
{
    return s->last_seen + RETENTION <= now;
}

static void map_note(uint32_t id, uint64_t ts)
{
    uint32_t h = home_fib(id);
    long first_stale = -1, first_empty = -1, oldest = -1;
    for (uint32_t i = 0; i < WINDOW; i++) {
        uint32_t s = (h + i) & MASK;
        slot_t *e = &map[s];
        if (!e->used) { if (first_empty < 0) first_empty = (long)s; continue; }
        if (e->id == id) { if (ts > e->last_seen) e->last_seen = ts; return; }
        if (first_stale < 0 && stale(e, ts)) first_stale = (long)s;
        if (oldest < 0 || e->last_seen < map[oldest].last_seen) oldest = (long)s;
    }
    long v;
    if (first_stale >= 0)      { v = first_stale; g_reuse++; }
    else if (first_empty >= 0) { v = first_empty; g_live++; }
    else                       { v = oldest;      g_evict++; }
    map[v].id = id; map[v].last_seen = ts; map[v].used = true;
}

static int map_is_dup(uint32_t id, uint64_t now, uint64_t win)
{
    uint32_t h = home_fib(id);
    for (uint32_t i = 0; i < WINDOW; i++) {          /* no early exit: no tombstones */
        const slot_t *e = &map[(h + i) & MASK];
        if (e->used && e->id == id) {
            uint64_t t = e->last_seen;
            return (t <= now && now - t <= win) ? 1 : 0;
        }
    }
    return 0;
}

/* ---------------- brute force reference ---------------- */
#define NIDS 600
static uint64_t ref_seen[NIDS];       /* 0 = never */
static bool     ref_has[NIDS];

static int ref_is_dup(uint32_t id, uint64_t now, uint64_t win)
{
    if (!ref_has[id]) return 0;
    uint64_t t = ref_seen[id];
    return (t <= now && now - t <= win) ? 1 : 0;
}

/* ---------------- rng ---------------- */
static uint64_t rs = 88172645463325252ull;
static uint32_t rnd(void)
{
    rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
    return (uint32_t)(rs >> 11);
}

/* ---------------- test 1: 10k random ops vs brute force ---------------- */
static int test_random_ops(void)
{
    memset(map, 0, sizeof map);
    memset(ref_seen, 0, sizeof ref_seen);
    memset(ref_has, 0, sizeof ref_has);
    g_live = g_evict = g_reuse = 0;

    uint64_t now = 1000000ull;
    long ops = 0, dups = 0, bad = 0;

    for (int i = 0; i < 10000; i++) {
        now += 1 + rnd() % 900;                 /* ~0.45 ms avg, 10k ops < 5 s */
        uint32_t id = rnd() % NIDS;
        if (rnd() % 2) {                        /* insert */
            map_note(id, now);
            ref_seen[id] = now; ref_has[id] = true;
        } else {                                /* lookup, 3 s anti-passback window */
            int a = map_is_dup(id, now, 3000000ull);
            int b = ref_is_dup(id, now, 3000000ull);
            dups += b;
            if (a != b) { bad++; if (bad < 4) printf("  MISMATCH id=%u got=%d want=%d\n", id, a, b); }
        }
        ops++;
    }
    printf("test1 random ops: ops=%ld dup-hits=%ld mismatches=%ld live=%llu evict=%llu reuse=%llu\n",
           ops, dups, bad, (unsigned long long)g_live,
           (unsigned long long)g_evict, (unsigned long long)g_reuse);
    return bad == 0;
}

/* ---------------- test 2: 하위 비트만 쓰면 어떻게 되는가 ---------------- */
static int test_hash_quality(void)
{
    /* 600 badges from one printing batch: ids step by 1024 (site prefix in the
     * high bits). id & MASK sends every one of them to the same slot. */
    uint32_t ids[600];
    for (int i = 0; i < 600; i++) ids[i] = 0x10000u + 1024u * (uint32_t)i;

    for (int which = 0; which < 2; which++) {
        memset(map, 0, sizeof map);
        g_live = g_evict = g_reuse = 0;
        uint32_t distinct_home[CAP]; memset(distinct_home, 0, sizeof distinct_home);
        for (int i = 0; i < 600; i++) {
            uint32_t h = which ? home_fib(ids[i]) : home_mask(ids[i]);
            distinct_home[h]++;
        }
        uint32_t buckets = 0, worst = 0;
        for (uint32_t i = 0; i < CAP; i++) {
            if (distinct_home[i]) buckets++;
            if (distinct_home[i] > worst) worst = distinct_home[i];
        }
        /* now actually insert with the real map (Fibonacci) or a masked clone */
        uint64_t t = 1000000ull;
        uint32_t lost = 0;
        memset(map, 0, sizeof map);
        for (int i = 0; i < 600; i++) {
            uint32_t h = which ? home_fib(ids[i]) : home_mask(ids[i]);
            long empty = -1;
            bool placed = false;
            for (uint32_t p = 0; p < WINDOW; p++) {
                uint32_t s = (h + p) & MASK;
                if (!map[s].used) { empty = (long)s; break; }
                if (map[s].id == ids[i]) { placed = true; break; }
            }
            if (placed) continue;
            if (empty < 0) { lost++; continue; }      /* window full -> evict a live one */
            map[empty].id = ids[i]; map[empty].last_seen = t; map[empty].used = true;
        }
        printf("test2 %-10s: distinct home slots=%4u  worst home load=%3u  "
               "inserts that had to evict a LIVE entry=%u/600\n",
               which ? "fibonacci" : "id & MASK", buckets, worst, lost);
    }

    /* the MAC-address version: one vendor, BSSIDs stepping by 16 */
    uint8_t mac[300][6];
    for (int i = 0; i < 300; i++) {
        mac[i][0] = 0x00; mac[i][1] = 0x1a; mac[i][2] = 0x2b;
        mac[i][3] = 0x00; mac[i][4] = (uint8_t)(i / 16); mac[i][5] = (uint8_t)((i * 16) & 0xff);
    }
    for (int which = 0; which < 2; which++) {
        uint32_t cnt[256]; memset(cnt, 0, sizeof cnt);
        for (int i = 0; i < 300; i++) {
            uint32_t h;
            if (which == 0) {
                h = mac[i][5];                              /* "just use the last byte" */
            } else {
                h = 2166136261u;
                for (int b = 0; b < 6; b++) { h ^= mac[i][b]; h *= 16777619u; }
                h ^= h >> 16;                               /* xor fold */
            }
            cnt[h & 255u]++;
        }
        uint32_t used = 0, worst = 0;
        for (int i = 0; i < 256; i++) { if (cnt[i]) used++; if (cnt[i] > worst) worst = cnt[i]; }
        printf("test2 %-10s: 300 BSSIDs (stride 16) -> %3u/256 buckets used, worst bucket=%u\n",
               which ? "FNV-1a+fold" : "last byte", used, worst);
    }
    return 1;
}

/* ---------------- test 3: lazy expiry ---------------- */
static int test_lazy_expiry(void)
{
    memset(map, 0, sizeof map);
    g_live = g_evict = g_reuse = 0;

    uint64_t now = 1000000ull;
    /* 900 distinct badges over 5 minutes: more than CAP*0.75, but they age out,
     * so the table should recycle stale slots and never overwrite a live one. */
    for (uint32_t i = 0; i < 900; i++) {
        map_note(0xABCD0000u + i, now);
        now += 400000ull;                       /* 0.4 s apart -> 6 min total */
    }
    /* everything older than 30 s must be invisible */
    int visible = 0;
    for (uint32_t i = 0; i < 900; i++)
        visible += map_is_dup(0xABCD0000u + i, now, 25000000ull);

    printf("test3 lazy expiry: 900 badges over %llus  live=%llu  stale_reuse=%llu  "
           "evictions=%llu  visible within 25s=%d\n",
           (unsigned long long)((now - 1000000ull) / 1000000ull),
           (unsigned long long)g_live, (unsigned long long)g_reuse,
           (unsigned long long)g_evict, visible);
    return g_evict == 0;
}

int main(void)
{
    int ok = 1;
    ok &= test_random_ops();
    ok &= test_hash_quality();
    ok &= test_lazy_expiry();
    printf("%s\n", ok ? "ALL OK" : "FAILED");
    return ok ? 0 : 1;
}
