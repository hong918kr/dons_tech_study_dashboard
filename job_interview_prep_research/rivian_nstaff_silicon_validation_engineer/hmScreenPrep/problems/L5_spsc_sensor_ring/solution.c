/*
 * L5  Lock-free SPSC Ring for a Sensor Capture -> Logger Pipeline
 * Reference solution.
 *
 * A capture thread (stands in for an ISR / DMA-completion handler) pushes fixed-size
 * 64-byte sensor records into a ring. A logger thread drains them in batches to storage.
 * Traffic is bursty and the capture side must NEVER block.
 *
 * Implement (C11 <stdatomic.h>, no locks, no malloc, caller-provided buffer):
 *   ring_init       cap must be a non-zero power of two, else -EINVAL. Reset indices + stats.
 *   ring_push       PRODUCER ONLY. Copy the record into the next slot and publish it.
 *                   Ring full -> drop the NEW record, count it in `dropped`, return false.
 *                   Track high_water = max occupancy ever seen (after a push).
 *   ring_pop_batch  CONSUMER ONLY. Copy up to `max` records (oldest first) into out[],
 *                   free those slots, return how many were copied (0 if empty).
 *   ring_stats      pushed (accepted), dropped, high_water. Safe to call from any thread.
 *
 * head/tail are free-running uint32_t counters (they wrap past 2^32);
 * slot = idx & (cap - 1); occupancy = head - tail (unsigned arithmetic).
 * head is written only by the producer, tail only by the consumer.
 * Place acquire/release so a consumer never sees a slot before its record is written,
 * and the producer never overwrites a slot the consumer is still reading.
 *
 * Build & run:  ./run.sh L5 sol      (also try: cc -std=c11 -O1 -fsanitize=thread ... )
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdatomic.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* ---------------- record + ring layout (given) ---------------- */
#define PAYLOAD_N   48u
#define CACHE_LINE  64            /* Cortex-A53/A72 (Zynq MPSoC). Apple M-series: 128 */

typedef struct {
    uint32_t seq;                 /* capture sequence number */
    uint32_t len;                 /* valid payload bytes, 1 .. PAYLOAD_N */
    uint64_t ts_ns;               /* capture timestamp */
    uint8_t  payload[PAYLOAD_N];
} Record;                         /* exactly one 64-byte cache line */
_Static_assert(sizeof(Record) == 64, "Record must be one cache line");

typedef struct {
    /* read-only after init: shared by both sides, never written -> no false sharing */
    Record  *buf;
    uint32_t cap, mask;
    /* producer-written line */
    _Alignas(CACHE_LINE) _Atomic uint32_t head;   /* next slot to write (free-running) */
    _Atomic uint32_t pushed, dropped, high_water; /* stats: written by producer only */
    /* consumer-written line */
    _Alignas(CACHE_LINE) _Atomic uint32_t tail;   /* next slot to read (free-running) */
} Ring;

typedef struct { uint32_t pushed, dropped, high_water; } RingStats;

int      ring_init(Ring *r, Record *buf, uint32_t cap);
bool     ring_push(Ring *r, const Record *rec);
uint32_t ring_pop_batch(Ring *r, Record *out, uint32_t max);
void     ring_stats(Ring *r, RingStats *out);

/* Test hook (provided): start both indices at `start` on an EMPTY ring, before any
 * thread runs, so a test can cross 2^32 without pushing 4 billion records. */
static void ring_test_set_index(Ring *r, uint32_t start)
{
    atomic_store_explicit(&r->head, start, memory_order_relaxed);
    atomic_store_explicit(&r->tail, start, memory_order_relaxed);
}

/* ---------------- your code ---------------- */
int ring_init(Ring *r, Record *buf, uint32_t cap)
{
    if (r == NULL || buf == NULL || cap == 0 || (cap & (cap - 1)) != 0)
        return -EINVAL;
    r->buf  = buf;
    r->cap  = cap;
    r->mask = cap - 1;
    atomic_store_explicit(&r->head, 0, memory_order_relaxed);
    atomic_store_explicit(&r->tail, 0, memory_order_relaxed);
    atomic_store_explicit(&r->pushed, 0, memory_order_relaxed);
    atomic_store_explicit(&r->dropped, 0, memory_order_relaxed);
    atomic_store_explicit(&r->high_water, 0, memory_order_relaxed);
    return 0;   /* threads started after this (pthread_create) see the init */
}

/* Single writer: a plain load + store is enough (no RMW needed). Relaxed: stats only. */
static inline void stat_inc(_Atomic uint32_t *c)
{
    atomic_store_explicit(c, atomic_load_explicit(c, memory_order_relaxed) + 1u,
                          memory_order_relaxed);
}

bool ring_push(Ring *r, const Record *rec)
{
    uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);  /* own index */
    /* acquire pairs with the consumer's release of tail: its reads of the slot
     * are finished before we overwrite that slot */
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    uint32_t used = h - t;                         /* wrap-safe: 0 .. cap */

    if (used == r->cap) {                          /* full: drop newest, never block */
        stat_inc(&r->dropped);
        return false;
    }
    r->buf[h & r->mask] = *rec;                    /* 1. write the record */
    /* 2. publish: release makes the record visible before the new head */
    atomic_store_explicit(&r->head, h + 1u, memory_order_release);

    stat_inc(&r->pushed);
    if (used + 1u > atomic_load_explicit(&r->high_water, memory_order_relaxed))
        atomic_store_explicit(&r->high_water, used + 1u, memory_order_relaxed);
    return true;
}

uint32_t ring_pop_batch(Ring *r, Record *out, uint32_t max)
{
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);  /* own index */
    /* acquire pairs with the producer's release of head: records up to head are visible */
    uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    uint32_t n = h - t;
    if (n > max) n = max;
    for (uint32_t i = 0; i < n; i++)
        out[i] = r->buf[(t + i) & r->mask];
    /* release: our reads of these slots happen before the producer may reuse them.
     * One store for the whole batch = one cache-line transfer, not n. */
    if (n != 0)
        atomic_store_explicit(&r->tail, t + n, memory_order_release);
    return n;
}

void ring_stats(Ring *r, RingStats *out)
{
    out->pushed     = atomic_load_explicit(&r->pushed, memory_order_relaxed);
    out->dropped    = atomic_load_explicit(&r->dropped, memory_order_relaxed);
    out->high_water = atomic_load_explicit(&r->high_water, memory_order_relaxed);
}

/* ---------------- tests ---------------- */
static void on_timeout(int sig)
{
    (void)sig;
    static const char msg[] = "FAIL: threaded test timed out (push must never block)\n";
    ssize_t w = write(1, msg, sizeof msg - 1);
    (void)w;
    _exit(1);
}

/* Deterministic record content derived from seq, so the consumer can verify every byte. */
static void make_record(Record *rec, uint32_t seq)
{
    memset(rec, 0, sizeof *rec);
    rec->seq   = seq;
    rec->len   = 1u + seq % PAYLOAD_N;
    rec->ts_ns = (uint64_t)seq * 1000u + 7u;
    for (uint32_t i = 0; i < rec->len; i++)
        rec->payload[i] = (uint8_t)(seq * 131u + i * 7u + 1u);
}

static bool record_ok(const Record *rec)
{
    if (rec->len != 1u + rec->seq % PAYLOAD_N) return false;
    if (rec->ts_ns != (uint64_t)rec->seq * 1000u + 7u) return false;
    for (uint32_t i = 0; i < PAYLOAD_N; i++) {
        uint8_t want = i < rec->len ? (uint8_t)(rec->seq * 131u + i * 7u + 1u) : 0;
        if (rec->payload[i] != want) return false;
    }
    return true;
}

static uint32_t payload_sum(const Record *rec)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < PAYLOAD_N; i++) s += rec->payload[i];
    return s;
}

/* ---- 2-thread stress harness ---- */
typedef struct {
    Ring    *r;
    uint32_t n;            /* records to produce: seq 0 .. n-1 */
    uint32_t burst;        /* back-to-back pushes before a short pause */
    uint32_t pause;        /* spin iterations between bursts */
    _Atomic bool done;
    uint32_t accepted;     /* push() returned true */
    uint64_t sum;          /* payload checksum of accepted records */
} Producer;

static void *producer_main(void *arg)
{
    Producer *p = arg;
    Record rec;
    for (uint32_t seq = 0; seq < p->n; seq++) {
        make_record(&rec, seq);
        if (ring_push(p->r, &rec)) { p->accepted++; p->sum += payload_sum(&rec); }
        if (p->burst && seq % p->burst == p->burst - 1)
            for (volatile uint32_t k = 0; k < p->pause; k++) { }
    }
    atomic_store_explicit(&p->done, true, memory_order_release);
    return NULL;
}

typedef struct {
    uint32_t popped, bad, nonmono, gaps, batches;
    uint64_t sum;
} Consumer;

static void consume(Ring *r, Producer *p, uint32_t max_batch, Consumer *c)
{
    Record batch[32];
    bool have_last = false;
    uint32_t last = 0;
    memset(c, 0, sizeof *c);
    for (;;) {
        uint32_t got = ring_pop_batch(r, batch, max_batch);
        if (got == 0) {
            if (!atomic_load_explicit(&p->done, memory_order_acquire)) continue;
            got = ring_pop_batch(r, batch, max_batch);   /* final drain after done */
            if (got == 0) break;
        }
        if (got > max_batch) { c->bad++; break; }
        c->batches++;
        for (uint32_t i = 0; i < got; i++) {
            const Record *rec = &batch[i];
            if (!record_ok(rec)) c->bad++;
            if (have_last && rec->seq <= last) c->nonmono++;
            c->gaps += have_last ? rec->seq - last - 1u : rec->seq;   /* missing seqs */
            last = rec->seq;
            have_last = true;
            c->sum += payload_sum(rec);
            c->popped++;
        }
        if (c->popped > p->n) { c->bad++; break; }        /* more out than in: broken */
    }
    if (have_last && last < p->n - 1u) c->gaps += p->n - 1u - last;   /* dropped at the end */
    if (!have_last) c->gaps = p->n;
}

static Record buf8[8], buf_big[1024], out[16];
static Ring R;

int main(void)
{
    RingStats s;
    Record rec;

    /* 1. init: cap must be a non-zero power of two */
    CHECK(ring_init(&R, buf8, 0) == -EINVAL);
    CHECK(ring_init(&R, buf8, 6) == -EINVAL);
    CHECK(ring_init(&R, NULL, 8) == -EINVAL);
    CHECK(ring_init(&R, buf8, 8) == 0);
    CHECK(R.mask == 7u);
    CHECK(ring_pop_batch(&R, out, 16) == 0);                   /* empty */

    /* 2. basic FIFO: push 3, pop all 3 in order with intact content */
    for (uint32_t i = 0; i < 3; i++) { make_record(&rec, i); CHECK(ring_push(&R, &rec)); }
    memset(out, 0, sizeof out);
    CHECK(ring_pop_batch(&R, out, 16) == 3);
    CHECK(out[0].seq == 0 && out[1].seq == 1 && out[2].seq == 2);
    CHECK(record_ok(&out[0]) && record_ok(&out[2]) && out[2].len == 3);
    CHECK(ring_pop_batch(&R, out, 16) == 0);
    s = (RingStats){ 0 }; ring_stats(&R, &s);
    CHECK(s.pushed == 3 && s.dropped == 0 && s.high_water == 3);

    /* 3. full ring: drop-NEWEST, count drops, high-water = cap */
    CHECK(ring_init(&R, buf8, 8) == 0);
    int ok = 0;
    for (uint32_t i = 0; i < 10; i++) { make_record(&rec, i); ok += ring_push(&R, &rec); }
    CHECK(ok == 8);
    s = (RingStats){ 0 }; ring_stats(&R, &s);
    CHECK(s.pushed == 8 && s.dropped == 2 && s.high_water == 8);
    CHECK(ring_pop_batch(&R, out, 16) == 8);
    CHECK(out[0].seq == 0 && out[7].seq == 7);                 /* 8, 9 were dropped */
    make_record(&rec, 10);
    CHECK(ring_push(&R, &rec));                                /* space again after pop */
    CHECK(ring_pop_batch(&R, out, 16) == 1 && out[0].seq == 10);

    /* 4. batch limits: max smaller than available, max == 0 */
    CHECK(ring_init(&R, buf8, 8) == 0);
    for (uint32_t i = 0; i < 5; i++) { make_record(&rec, i); ring_push(&R, &rec); }
    CHECK(ring_pop_batch(&R, out, 2) == 2 && out[0].seq == 0 && out[1].seq == 1);
    CHECK(ring_pop_batch(&R, out, 0) == 0);
    CHECK(ring_pop_batch(&R, out, 16) == 3 && out[0].seq == 2 && out[2].seq == 4);
    CHECK(ring_pop_batch(&R, out, 16) == 0);
    s = (RingStats){ 0 }; ring_stats(&R, &s);
    CHECK(s.high_water == 5);                                  /* not reset by popping */

    /* 5. wrap past 2^32: indices start at 0xFFFFFFFE (slot 6) */
    CHECK(ring_init(&R, buf8, 8) == 0);
    ring_test_set_index(&R, 0xFFFFFFFEu);
    ok = 0;
    for (uint32_t i = 0; i < 5; i++) { make_record(&rec, i); ok += ring_push(&R, &rec); }
    CHECK(ok == 5 && atomic_load(&R.head) == 3u);              /* 0xFFFFFFFE + 5 wraps to 3 */
    CHECK(buf8[6].seq == 0 && buf8[7].seq == 1 && buf8[0].seq == 2 && buf8[2].seq == 4);
    ok = 0;
    for (uint32_t i = 5; i < 9; i++) { make_record(&rec, i); ok += ring_push(&R, &rec); }
    CHECK(ok == 3);                                            /* seq 8 dropped: full */
    CHECK(ring_pop_batch(&R, out, 3) == 3 && out[0].seq == 0 && out[2].seq == 2);
    CHECK(atomic_load(&R.tail) == 1u);
    ok = 0;
    for (uint32_t i = 9; i < 12; i++) { make_record(&rec, i); ok += ring_push(&R, &rec); }
    CHECK(ok == 3);
    CHECK(ring_pop_batch(&R, out, 16) == 8);
    CHECK(out[0].seq == 3 && out[4].seq == 7 && out[5].seq == 9 && out[7].seq == 11);
    CHECK(record_ok(&out[7]));
    CHECK(atomic_load(&R.head) == 9u && atomic_load(&R.tail) == 9u);
    s = (RingStats){ 0 }; ring_stats(&R, &s);
    CHECK(s.pushed == 11 && s.dropped == 1 && s.high_water == 8);

    /* 6. two threads, 1,000,000 records, big ring, bursty producer.
     *    Indices start near 2^32 so they wrap mid-run. */
    signal(SIGALRM, on_timeout);
    alarm(20);
    struct { uint32_t cap, burst, pause, max_batch; Record *buf; } run[2] = {
        { 1024, 256, 40000, 32, buf_big },    /* mostly keeps up */
        {    8,  64,  200,  4, buf8    },    /* tiny ring: drops are expected */
    };
    for (int k = 0; k < 2; k++) {
        CHECK(ring_init(&R, run[k].buf, run[k].cap) == 0);
        ring_test_set_index(&R, 0xFFFFFFFFu - 300000u);
        Producer p = { .r = &R, .n = 1000000u, .burst = run[k].burst, .pause = run[k].pause };
        atomic_init(&p.done, false);
        Consumer c;
        pthread_t th;
        CHECK(pthread_create(&th, NULL, producer_main, &p) == 0);
        consume(&R, &p, run[k].max_batch, &c);
        pthread_join(th, NULL);
        s = (RingStats){ 0 }; ring_stats(&R, &s);
        printf("  run %d: cap %4u  pushed %7u  dropped %6u  high_water %4u  batches %u\n",
               k, run[k].cap, s.pushed, s.dropped, s.high_water, c.batches);
        CHECK(c.bad == 0 && c.nonmono == 0);                   /* no torn / stale records */
        CHECK(s.pushed + s.dropped == p.n);                    /* every capture accounted */
        CHECK(c.popped == s.pushed && s.pushed == p.accepted);
        CHECK(c.gaps == s.dropped);                            /* seq gaps == drop counter */
        CHECK(c.sum == p.sum);                                 /* payload checksum */
        CHECK(s.high_water >= 1 && s.high_water <= run[k].cap);
        if (k == 1) CHECK(s.dropped > 0 && s.high_water == 8);
    }
    alarm(0);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
