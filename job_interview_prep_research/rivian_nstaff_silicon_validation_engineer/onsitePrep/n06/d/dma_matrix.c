/*
 * dma_matrix.c - N06D: DMA engine model + test-matrix runner + fault injection.
 *
 * Purpose (interview story): "Here is my DMA test matrix. How do I know the
 * matrix itself is good?  I inject known bugs into the engine model and check
 * that at least one row of the matrix fails for each bug (test of the test)."
 *
 * Model: 4 channels, 8-slot descriptor ring per channel, OWN-bit handoff,
 * 256 B bursts on a 16 B/cycle bus, bursts split at 4 KiB (AXI rule),
 * strict priority + aging arbiter (or round robin), IRQ coalescing
 * (count threshold + timer), DECERR for addresses outside "DDR",
 * channel reset, and a CPU write-back cache model for the non-coherent flow.
 *
 * Rows (R01..R11) = rows of the N06D D1 test matrix that can run in a model.
 * Bugs (B1..B11)  = realistic engine / driver bugs, injected one at a time.
 *
 * Build: cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined dma_matrix.c
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { MEM_SIZE = 64 * 1024, NCH = 4, RING_N = 8, MASK = RING_N - 1,
       BURST = 256, BUS_B = 16, DESC_FETCH = 16, AGE_LIMIT = 64, GUARD = 16,
       LINE = 64 };

enum { B_NONE, B_OVERRUN_TAIL, B_4K_WRAP, B_MISALIGN_FLUSH, B_RING_WRAP_SKIP,
       B_ARB_STARVE, B_ERR_SILENT, B_COALESCE_TIMER, B_CHAIN_EARLY_DONE,
       B_RESET_RESUME, B_SKIP_CLEAN, B_SKIP_INVAL, B_COUNT };
static const char *bug_name[B_COUNT] = {
    "none (baseline)",
    "B1  tail overrun: +1 byte when len%4!=0",
    "B2  burst crosses 4KiB, addr wraps in page",
    "B3  src/dst misaligned: last byte dropped",
    "B4  ring wrap skips slot 0",
    "B5  strict priority w/o aging (starvation)",
    "B6  DECERR reported as OK",
    "B7  coalescing timer never fires",
    "B8  chain: EOP marked done one desc early",
    "B9  channel reset keeps in-flight desc",
    "B10 driver: no cache clean before DMA",
    "B11 driver: no invalidate after DMA",
};
static int g_bug;

static uint8_t mem[MEM_SIZE];               /* "DDR", bus address = offset */

/* ------------------------------------------------------------ engine --- */
#define C_OWN 1u
#define C_EOP 2u
#define S_DONE 1u
#define S_DECERR 4u

typedef struct { uint32_t src, dst, len, ctrl, status; } Desc;

typedef struct {
    Desc ring[RING_N];
    uint32_t hw_idx, off;                   /* HW: next slot (free running), bytes done */
    Desc cur; int fetched, force, early;    /* latched descriptor; B8 helpers */
    int prio; uint64_t wait, max_wait;
    uint32_t head, tail;                    /* SW producer / consumer (free running) */
    uint64_t bytes;                         /* bytes moved (bandwidth share) */
} Chan;

typedef struct {
    Chan ch[NCH];
    int strict, rr_last;
    uint64_t cycles;
    uint32_t coal_thresh, coal_timeout, coal_cnt; uint64_t coal_age;
    uint32_t irqs, signaled, events;
} Eng;
static Eng E;

static void eng_init(void)
{
    memset(&E, 0, sizeof E);
    E.strict = 1; E.coal_thresh = 1; E.coal_timeout = 300; E.rr_last = NCH - 1;
}

static int in_mem(uint32_t a, uint32_t n) { return a < MEM_SIZE && n <= MEM_SIZE - a; }

static void irq_fire(void) { E.irqs++; E.signaled += E.coal_cnt; E.coal_cnt = 0; E.coal_age = 0; }
static void coal_event(void) { E.events++; if (++E.coal_cnt >= E.coal_thresh) irq_fire(); }
static void coal_tick(uint64_t cost)
{
    if (!E.coal_cnt) return;
    E.coal_age += cost;
    if (E.coal_age >= E.coal_timeout && g_bug != B_COALESCE_TIMER) irq_fire();
}

static int ch_pending(const Chan *c)
{
    return c->fetched || c->force || (c->ring[c->hw_idx & MASK].ctrl & C_OWN);
}

static void finish_desc(Chan *c, uint32_t status)
{
    uint32_t slot = c->hw_idx & MASK;
    Desc *d = &c->ring[slot];
    if (g_bug == B_OVERRUN_TAIL && !(status & S_DECERR) && (c->cur.len % 4) &&
        in_mem(c->cur.dst + c->cur.len, 1) && in_mem(c->cur.src + c->cur.len, 1))
        mem[c->cur.dst + c->cur.len] = mem[c->cur.src + c->cur.len];
    d->status = status;
    d->ctrl &= ~C_OWN;
    if (d->ctrl & C_EOP) { if (c->early) c->early = 0; else coal_event(); }
    if (g_bug == B_CHAIN_EARLY_DONE && !(d->ctrl & C_EOP)) {
        Desc *n = &c->ring[(c->hw_idx + 1) & MASK];
        if ((n->ctrl & C_OWN) && (n->ctrl & C_EOP)) {        /* off-by-one EOP */
            n->status = S_DONE; n->ctrl &= ~C_OWN; c->force = 1; c->early = 1; coal_event();
        }
    }
    c->hw_idx += (g_bug == B_RING_WRAP_SKIP && slot == RING_N - 1) ? 2u : 1u;
    c->fetched = 0; c->off = 0;
}

static int arb_pick(const int *pend)
{
    int pick = -1;
    if (E.strict) {
        if (g_bug != B_ARB_STARVE) {               /* aging: oldest starving channel wins */
            uint64_t best = 0;
            for (int i = 0; i < NCH; i++)
                if (pend[i] && E.ch[i].wait >= AGE_LIMIT && E.ch[i].wait > best) { best = E.ch[i].wait; pick = i; }
        }
        if (pick < 0)
            for (int i = 0; i < NCH; i++)
                if (pend[i] && (pick < 0 || E.ch[i].prio > E.ch[pick].prio)) pick = i;
    } else {
        for (int k = 1; k <= NCH; k++) {
            int i = (E.rr_last + k) % NCH;
            if (pend[i]) { pick = i; break; }
        }
        if (pick >= 0) E.rr_last = pick;
    }
    return pick;
}

/* one arbitration slot: a descriptor fetch or one burst; returns 0 if idle */
static int eng_step(void)
{
    int pend[NCH];
    for (int i = 0; i < NCH; i++) pend[i] = ch_pending(&E.ch[i]);
    int pick = arb_pick(pend);
    if (pick < 0) { E.cycles++; coal_tick(1); return 0; }
    Chan *c = &E.ch[pick];
    uint64_t cost;
    if (!c->fetched) {
        c->cur = c->ring[c->hw_idx & MASK];
        c->fetched = 1; c->force = 0; c->off = 0;
        cost = DESC_FETCH;
    } else {
        uint32_t rem = c->cur.len - c->off;
        uint32_t s = c->cur.src + c->off, d = c->cur.dst + c->off;
        uint32_t n = rem < BURST ? rem : BURST;
        if (g_bug != B_4K_WRAP) {                  /* AXI: a burst must not cross 4 KiB */
            uint32_t ls = 0x1000 - (s & 0xFFFu), ld = 0x1000 - (d & 0xFFFu);
            if (n > ls) n = ls;
            if (n > ld) n = ld;
        }
        if (!in_mem(s, n) || !in_mem(d, n)) {
            cost = 2;
            finish_desc(c, S_DONE | (g_bug == B_ERR_SILENT ? 0u : S_DECERR));
        } else {
            for (uint32_t i = 0; i < n; i++) {
                uint32_t sa = s + i, da = d + i;
                if (g_bug == B_4K_WRAP) {          /* only low 12 bits increment */
                    sa = (s & ~0xFFFu) | ((s + i) & 0xFFFu);
                    da = (d & ~0xFFFu) | ((d + i) & 0xFFFu);
                }
                if (g_bug == B_MISALIGN_FLUSH && ((c->cur.src ^ c->cur.dst) & 3u) &&
                    c->cur.len > 4 && c->off + i == c->cur.len - 1)
                    continue;
                mem[da] = mem[sa];
            }
            c->off += n; c->bytes += n;
            cost = 1 + (n + BUS_B - 1) / BUS_B;
            if (c->off == c->cur.len) finish_desc(c, S_DONE);
        }
    }
    E.cycles += cost; coal_tick(cost);
    for (int i = 0; i < NCH; i++) {
        if (i == pick) { E.ch[i].wait = 0; continue; }
        if (pend[i]) { E.ch[i].wait += cost; if (E.ch[i].wait > E.ch[i].max_wait) E.ch[i].max_wait = E.ch[i].wait; }
    }
    return 1;
}

static void ch_reset(int ch)                 /* HW channel reset + SW ring re-init */
{
    Chan *c = &E.ch[ch];
    memset(c->ring, 0, sizeof c->ring);
    c->hw_idx = 0; c->head = c->tail = 0;
    if (g_bug != B_RESET_RESUME) { c->fetched = 0; c->off = 0; c->force = 0; c->early = 0; }
}

/* ------------------------------------------------------------ driver --- */
static int submit_chain(int ch, const uint32_t *src, const uint32_t *dst, const uint32_t *len, int nseg)
{
    Chan *c = &E.ch[ch];
    if (nseg < 1 || nseg > RING_N) return -22;               /* -EINVAL */
    if (c->head - c->tail + (uint32_t)nseg > RING_N) return -16; /* -EBUSY */
    for (int k = 0; k < nseg; k++) {
        Desc *d = &c->ring[(c->head + (uint32_t)k) & MASK];
        d->src = src[k]; d->dst = dst[k]; d->len = len[k]; d->status = 0;
        d->ctrl = (k == nseg - 1) ? C_EOP : 0;
    }
    /* (dma_wmb) then hand over: OWN last */
    for (int k = nseg - 1; k >= 0; k--) c->ring[(c->head + (uint32_t)k) & MASK].ctrl |= C_OWN;
    c->head += (uint32_t)nseg;
    return 0;
}
static int submit1(int ch, uint32_t src, uint32_t dst, uint32_t len)
{
    return submit_chain(ch, &src, &dst, &len, 1);
}

/* reap one whole transfer (up to EOP) if every descriptor of it is back */
static int reap_one(int ch, uint32_t *st)
{
    Chan *c = &E.ch[ch];
    uint32_t acc = 0;
    for (uint32_t t = c->tail; t != c->head; t++) {
        const Desc *d = &c->ring[t & MASK];
        if (d->ctrl & C_OWN) return 0;
        acc |= d->status;
        if (d->ctrl & C_EOP) { c->tail = t + 1; *st = acc; return 1; }
    }
    return 0;
}

static int run_until_done(int ch, uint32_t *st, long max_steps)
{
    for (long i = 0; i < max_steps; i++) {
        if (reap_one(ch, st)) return 0;
        eng_step();
    }
    return -1;                                               /* timeout */
}

/* ------------------------------------------------------- cache model --- */
static uint8_t cdata[MEM_SIZE], cvalid[MEM_SIZE / LINE], cdirty[MEM_SIZE / LINE];
static void cache_reset(void) { memset(cvalid, 0, sizeof cvalid); memset(cdirty, 0, sizeof cdirty); }
static void cache_fill(uint32_t a)
{
    uint32_t l = a / LINE;
    if (!cvalid[l]) { memcpy(&cdata[l * LINE], &mem[l * LINE], LINE); cvalid[l] = 1; cdirty[l] = 0; }
}
static void cpu_write(uint32_t a, uint8_t v) { cache_fill(a); cdata[a] = v; cdirty[a / LINE] = 1; }
static uint8_t cpu_read(uint32_t a) { cache_fill(a); return cdata[a]; }
static void cache_clean(uint32_t a, uint32_t n)
{
    for (uint32_t l = a / LINE; l <= (a + n - 1) / LINE; l++)
        if (cvalid[l] && cdirty[l]) { memcpy(&mem[l * LINE], &cdata[l * LINE], LINE); cdirty[l] = 0; }
}
static void cache_inval(uint32_t a, uint32_t n)
{
    for (uint32_t l = a / LINE; l <= (a + n - 1) / LINE; l++) { cvalid[l] = 0; cdirty[l] = 0; }
}

/* ------------------------------------------------------------ helpers --- */
static uint32_t rng_state = 1;
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }
static uint8_t pat(uint32_t a, uint32_t seed) { uint32_t x = a * 2654435761u ^ seed * 40503u; return (uint8_t)(x ^ (x >> 13) ^ (x >> 24)); }
static void fill_src(uint32_t a, uint32_t n, uint32_t seed) { for (uint32_t i = 0; i < n; i++) mem[a + i] = pat(a + i, seed); }
static void prep_dst(uint32_t a, uint32_t n) { memset(&mem[a - GUARD], 0xA5, n + 2 * GUARD); }
/* 0 = ok; checks data and both 16 B guards (dmatest-style out-of-bounds check) */
static int check_copy(uint32_t src, uint32_t dst, uint32_t n)
{
    for (uint32_t i = 0; i < GUARD; i++)
        if (mem[dst - GUARD + i] != 0xA5 || mem[dst + n + i] != 0xA5) return 1;
    return memcmp(&mem[src], &mem[dst], n) ? 2 : 0;
}

typedef struct { int cases, fails; } Row;
static void tally(Row *r, int bad) { r->cases++; if (bad) r->fails++; }

/* ---------------------------------------------------------------- rows --- */
static Row r01_size_align(void)      /* sizes x src offset x dst offset */
{
    static const uint32_t sizes[] = {1, 2, 3, 4, 5, 7, 8, 15, 16, 63, 64, 65, 255, 256, 257,
                                     1023, 1024, 4095, 4096, 4097, 8192};
    static const uint32_t soff[] = {0, 1, 3, 7}, doff[] = {0, 1, 2, 3, 63};
    Row r = {0, 0}; eng_init();
    for (size_t a = 0; a < sizeof sizes / sizeof *sizes; a++)
        for (size_t b = 0; b < 4; b++)
            for (size_t c = 0; c < 5; c++) {
                uint32_t s = 0x100 + soff[b], d = 0x8100 + doff[c], n = sizes[a], st = 0;
                fill_src(s, n, (uint32_t)(a * 31 + b * 7 + c)); prep_dst(d, n);
                int bad = submit1(0, s, d, n) || run_until_done(0, &st, 100000) || st != S_DONE || check_copy(s, d, n);
                if (bad) eng_init();
                tally(&r, bad);
            }
    return r;
}

static Row r02_4k_boundary(void)     /* start just below a 4 KiB page boundary */
{
    static const uint32_t ks[] = {1, 2, 4, 15, 16, 17, 63, 64, 255, 256};
    static const uint32_t lens[] = {2, 64, 256, 300, 4097};
    Row r = {0, 0}; eng_init();
    for (size_t i = 0; i < 10; i++)
        for (size_t j = 0; j < 5; j++) {
            uint32_t s = 0x2000 - ks[i], d = 0xA000 - ((ks[i] + 5) % 64) - 1, n = lens[j], st = 0;
            fill_src(s, n, (uint32_t)(i * 5 + j)); prep_dst(d, n);
            int bad = submit1(0, s, d, n) || run_until_done(0, &st, 100000) || st != S_DONE || check_copy(s, d, n);
            if (bad) eng_init();
            tally(&r, bad);
        }
    return r;
}

static Row r03_chain_sg(void)        /* gather: n scattered sources -> one contiguous dst */
{
    static uint8_t expect[8 * 700];
    Row r = {0, 0}; eng_init(); rng_state = 0x5EED03;
    for (int nseg = 2; nseg <= RING_N; nseg++)
        for (int rep = 0; rep < 20; rep++) {
            uint32_t src[RING_N], dst[RING_N], len[RING_N], tot = 0, st = 0, d0 = 0x8100;
            for (int k = 0; k < nseg; k++) {
                len[k] = 1 + rnd() % 700; src[k] = 0x100 + (uint32_t)k * 0xE00 + rnd() % 64;
                dst[k] = d0 + tot; tot += len[k];
                fill_src(src[k], len[k], (uint32_t)(nseg * 100 + rep * 8 + k));
            }
            for (int k = 0, o = 0; k < nseg; o += (int)len[k], k++) memcpy(&expect[o], &mem[src[k]], len[k]);
            prep_dst(d0, tot);
            int bad = submit_chain(0, src, dst, len, nseg) || run_until_done(0, &st, 200000) || st != S_DONE;
            if (!bad) {
                for (uint32_t i = 0; i < GUARD; i++) bad |= mem[d0 - GUARD + i] != 0xA5 || mem[d0 + tot + i] != 0xA5;
                bad |= memcmp(expect, &mem[d0], tot) != 0;
            }
            if (bad) eng_init();
            tally(&r, bad);
        }
    return r;
}

static Row r04_ring_wrap(void)       /* 27 single submits, then 4 rounds of full ring */
{
    Row r = {0, 0}; eng_init();
    for (uint32_t i = 0; i < 3 * RING_N + 3; i++) {
        uint32_t s = 0x100 + i * 16, d = 0x8100 + (i % 4) * 0x400, n = 100 + i, st = 0;
        fill_src(s, n, i); prep_dst(d, n);
        int bad = submit1(0, s, d, n) || run_until_done(0, &st, 100000) || st != S_DONE || check_copy(s, d, n);
        if (bad) eng_init();
        tally(&r, bad);
    }
    for (uint32_t round = 0; round < 4; round++) {
        int bad = 0;
        for (uint32_t k = 0; k < RING_N; k++) {
            fill_src(0x1000 + k * 0x200, 200, round * 8 + k); prep_dst(0x8100 + k * 0x200, 200);
            bad |= submit1(0, 0x1000 + k * 0x200, 0x8100 + k * 0x200, 200) != 0;
        }
        bad |= submit1(0, 0x100, 0x8100, 4) != -16;             /* ring full -> -EBUSY */
        for (uint32_t k = 0; k < RING_N && !bad; k++) {
            uint32_t st = 0;
            bad |= run_until_done(0, &st, 100000) || st != S_DONE;
        }
        for (uint32_t k = 0; k < RING_N && !bad; k++) bad |= check_copy(0x1000 + k * 0x200, 0x8100 + k * 0x200, 200) != 0;
        if (bad) eng_init();
        tally(&r, bad);
    }
    return r;
}

/* strict priority: ch0 (prio 3) saturates, ch1..3 must still finish (aging);
 * then round robin: equal load -> equal bandwidth share */
static uint64_t g_lowprio_max_wait, g_share_min_pct, g_share_max_pct;
static Row r05_concurrency(void)
{
    Row r = {0, 0}; eng_init();
    for (int i = 0; i < NCH; i++) E.ch[i].prio = 3 - i;
    uint32_t sub0 = 0, done0 = 0, low_done[NCH] = {0}, low_sub[NCH] = {0};
    int bad = 0; long steps = 0;
    for (uint32_t k = 0; k < 7; k++) fill_src(k * 0x400, 1024, 900 + k);   /* 7 buffers, <= 6 in flight */
    for (int ch = 1; ch < NCH; ch++) fill_src((uint32_t)ch * 0x2000 + 0x100, 4 * 1024, 700 + (uint32_t)ch);
    while (steps++ < 400000) {
        int low_all = 1;
        for (int ch = 1; ch < NCH; ch++) low_all &= low_done[ch] == 4;
        if (!low_all || done0 != sub0) {
            if (!low_all && E.ch[0].head - E.ch[0].tail < 6) {        /* keep ch0 saturated */
                uint32_t k = sub0 % 7;
                prep_dst(0x8100 + k * 0x440, 1024);
                if (!submit1(0, k * 0x400, 0x8100 + k * 0x440, 1024)) sub0++;
            }
        } else break;
        for (int ch = 1; ch < NCH; ch++)
            if (low_sub[ch] < 4 && E.ch[ch].head - E.ch[ch].tail < RING_N) {
                uint32_t j = low_sub[ch], s = (uint32_t)ch * 0x2000 + 0x100 + j * 1024, d = 0x8000 + (uint32_t)ch * 0x2000 + 0x100 + j * 1024;
                prep_dst(d, 512);
                if (!submit1(ch, s, d, 512)) low_sub[ch]++;
            }
        eng_step();
        uint32_t st;
        while (reap_one(0, &st)) { uint32_t k = done0 % 7; bad |= st != S_DONE || check_copy(k * 0x400, 0x8100 + k * 0x440, 1024); done0++; }
        for (int ch = 1; ch < NCH; ch++)
            while (reap_one(ch, &st)) {
                uint32_t j = low_done[ch];
                bad |= st != S_DONE || check_copy((uint32_t)ch * 0x2000 + 0x100 + j * 1024, 0x8000 + (uint32_t)ch * 0x2000 + 0x100 + j * 1024, 512);
                low_done[ch]++;
            }
    }
    g_lowprio_max_wait = 0;
    for (int ch = 1; ch < NCH; ch++) {
        bad |= low_done[ch] != 4;
        if (E.ch[ch].max_wait > g_lowprio_max_wait) g_lowprio_max_wait = E.ch[ch].max_wait;
    }
    bad |= g_lowprio_max_wait > 4 * AGE_LIMIT;                  /* bounded latency */
    bad |= done0 != sub0;                                      /* ch0 fully drained */
    tally(&r, bad);

    /* round robin fairness */
    eng_init(); E.strict = 0; bad = 0;
    uint32_t sub[NCH] = {0}, dn[NCH] = {0};
    for (int ch = 0; ch < NCH; ch++) fill_src((uint32_t)ch * 0x2000, 2048, 50 + (uint32_t)ch);
    for (long t = 0; t < 20000; t++) {
        for (int ch = 0; ch < NCH; ch++)
            if (E.ch[ch].head - E.ch[ch].tail < 2) {
                uint32_t d = 0x8000 + (uint32_t)ch * 0x2000 + 0x100 + (sub[ch] % 2) * 0x1000;
                prep_dst(d, 2048);
                if (!submit1(ch, (uint32_t)ch * 0x2000, d, 2048)) sub[ch]++;
            }
        eng_step();
        for (int ch = 0; ch < NCH; ch++) {
            uint32_t st;
            while (reap_one(ch, &st)) {
                uint32_t d = 0x8000 + (uint32_t)ch * 0x2000 + 0x100 + (dn[ch] % 2) * 0x1000;
                bad |= st != S_DONE || check_copy((uint32_t)ch * 0x2000, d, 2048); dn[ch]++;
            }
        }
    }
    uint64_t tot = 0, mn = UINT64_MAX, mx = 0;
    for (int ch = 0; ch < NCH; ch++) { tot += E.ch[ch].bytes; if (E.ch[ch].bytes < mn) mn = E.ch[ch].bytes; if (E.ch[ch].bytes > mx) mx = E.ch[ch].bytes; }
    g_share_min_pct = tot ? mn * 1000 / tot : 0; g_share_max_pct = tot ? mx * 1000 / tot : 0;
    bad |= g_share_min_pct < 225 || g_share_max_pct > 275;     /* 25% +- 2.5% */
    tally(&r, bad);
    return r;
}

static Row r06_error_inject(void)    /* bad src / bad dst / straddle end / bad seg in chain */
{
    Row r = {0, 0}; eng_init();
    const uint32_t bad_src[] = {0x00F00000u, 0x200, MEM_SIZE - 100};
    const uint32_t bad_dst[] = {0x8100, 0x00F00000u, 0x8100};
    for (int i = 0; i < 3; i++) {
        uint32_t st = 0;
        prep_dst(0x8100, 256);
        int bad = submit1(0, bad_src[i], bad_dst[i], 256) || run_until_done(0, &st, 100000);
        bad |= !(st & S_DECERR);                                  /* error must be reported */
        if (i == 0) for (uint32_t k = 0; k < 256 + GUARD; k++) bad |= mem[0x8100 - GUARD + k] != 0xA5;
        /* engine must keep going: next good transfer on the same channel */
        fill_src(0x300, 128, 77u + (uint32_t)i); prep_dst(0x9100, 128);
        bad |= submit1(0, 0x300, 0x9100, 128) || run_until_done(0, &st, 100000) || st != S_DONE || check_copy(0x300, 0x9100, 128);
        if (bad) eng_init();
        tally(&r, bad);
    }
    {   /* chain with a bad middle segment: transfer status must carry the error */
        uint32_t src[3] = {0x100, 0x00F00000u, 0x500}, dst[3] = {0x8100, 0x8200, 0x8300}, len[3] = {64, 64, 64}, st = 0;
        int bad = submit_chain(0, src, dst, len, 3) || run_until_done(0, &st, 100000) || !(st & S_DECERR);
        tally(&r, bad);
    }
    return r;
}

static Row r07_irq_coalesce(void)
{
    Row r = {0, 0};
    for (int mode = 0; mode < 2; mode++) {
        eng_init(); E.coal_thresh = mode ? 4 : 1; E.coal_timeout = 300;
        int bad = 0; uint32_t st;
        for (int batch = 0; batch < 2; batch++) {
            int n = batch ? 2 : 8;
            for (int k = 0; k < n; k++) bad |= submit1(0, 0x100 + (uint32_t)k * 64, 0x8100 + (uint32_t)k * 64, 64) != 0;
            for (int k = 0; k < n; k++) bad |= run_until_done(0, &st, 100000) != 0;
        }
        for (int t = 0; t < 400; t++) eng_step();                 /* > coalescing timeout */
        uint32_t want_irqs = mode ? 3 : 10;                       /* 4 + 4 + timer(2) */
        bad |= E.events != 10 || E.signaled != 10 || E.irqs != want_irqs;
        tally(&r, bad);
    }
    return r;
}

static Row r08_cache_flow(void)      /* non-coherent master: clean src, invalidate dst */
{
    static const uint32_t sizes[] = {64, 100, 4096}, doffs[] = {0, 8};
    Row r = {0, 0}; eng_init();
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 2; b++) {
            uint32_t s = 0x1000, d = 0x9000 + doffs[b], n = sizes[a], st = 0;
            cache_reset();
            memset(&mem[s], 0, n);                                  /* stale value in DDR */
            prep_dst(d, n);
            for (uint32_t i = 0; i < n; i++) (void)cpu_read(d + i); /* CPU touched dst before */
            for (uint32_t i = 0; i < n; i++) cpu_write(s + i, pat(s + i, 4242u + (uint32_t)a));
            if (g_bug != B_SKIP_CLEAN) cache_clean(s, n);           /* dma_map(TO_DEVICE) */
            if (g_bug != B_SKIP_INVAL) cache_inval(d, n);           /* dma_map(FROM_DEVICE) */
            int bad = submit1(0, s, d, n) || run_until_done(0, &st, 100000) || st != S_DONE;
            if (g_bug != B_SKIP_INVAL) cache_inval(d, n);           /* dma_unmap(FROM_DEVICE) */
            for (uint32_t i = 0; i < n && !bad; i++) bad |= cpu_read(d + i) != pat(s + i, 4242u + (uint32_t)a);
            if (bad) eng_init();
            tally(&r, bad);
        }
    return r;
}

static Row r09_reset_mid(void)       /* reset during a transfer: no writes after reset */
{
    static uint8_t snap[8192];
    static const uint32_t stop_at[] = {0, 2048, 7936};               /* after fetch / mid / last burst */
    Row r = {0, 0};
    for (int i = 0; i < 3; i++) {
        eng_init();
        uint32_t s = 0x100, d = 0x8100, st = 0;
        fill_src(s, 8192, 9u + (uint32_t)i); prep_dst(d, 8192);
        int bad = submit1(0, s, d, 8192) != 0;
        for (int guard = 0; guard < 100000 && (!E.ch[0].fetched || E.ch[0].off < stop_at[i]); guard++) eng_step();
        ch_reset(0);
        memcpy(snap, &mem[d], 8192);
        for (int t = 0; t < 2000; t++) eng_step();
        bad |= memcmp(snap, &mem[d], 8192) != 0;                       /* no writes after reset */
        fill_src(0x3000, 512, 99); prep_dst(0xC000, 512);
        bad |= submit1(0, 0x3000, 0xC000, 512) || run_until_done(0, &st, 100000) || st != S_DONE || check_copy(0x3000, 0xC000, 512);
        tally(&r, bad);
    }
    return r;
}

static uint32_t perf_len[5] = {64, 256, 1024, 4096, 16384};
static uint64_t perf_cyc[5];
static Row r10_perf(void)            /* efficiency = ideal bus cycles / measured cycles */
{
    Row r = {0, 0}; eng_init();
    for (int i = 0; i < 5; i++) {
        uint32_t n = perf_len[i], st = 0;
        uint64_t t0 = E.cycles;
        int bad = submit1(0, 0x0, 0x8000, n) || run_until_done(0, &st, 100000) || st != S_DONE;
        perf_cyc[i] = E.cycles - t0;
        if (n >= 4096) bad |= (n / BUS_B) * 100 < perf_cyc[i] * 85;    /* >= 85% for >= 4 KiB */
        tally(&r, bad);
    }
    return r;
}

static Row r11_random_soak(void)     /* 4 channels, RR, random size/align/chain, seed fixed */
{
    static uint8_t expect[NCH][4 * 512];
    uint32_t tot[NCH] = {0}, dbase[NCH] = {0}; int busy[NCH] = {0};
    Row r = {0, 0}; eng_init(); E.strict = 0; rng_state = 0xC0FFEE;
    long idle = 0; int done = 0;
    while (done < 1200) {
        for (int ch = 0; ch < NCH; ch++) {
            if (busy[ch]) continue;
            uint32_t src[4], dst[4], len[4]; int nseg = 1 + (int)(rnd() % 4);
            uint32_t win = (uint32_t)ch * 0x2000, t = 0;
            dbase[ch] = 0x8000 + win + GUARD + rnd() % 64;
            for (int k = 0; k < nseg; k++) {
                len[k] = 1 + rnd() % 512; src[k] = win + rnd() % (0x2000 - 512);
                dst[k] = dbase[ch] + t; t += len[k];
                fill_src(src[k], len[k], rnd());
            }
            for (int k = 0, o = 0; k < nseg; o += (int)len[k], k++) memcpy(&expect[ch][o], &mem[src[k]], len[k]);
            tot[ch] = t; prep_dst(dbase[ch], t);
            if (!submit_chain(ch, src, dst, len, nseg)) busy[ch] = 1;
        }
        int prog = 0;
        eng_step();
        for (int ch = 0; ch < NCH; ch++) {
            uint32_t st;
            if (busy[ch] && reap_one(ch, &st)) {
                int bad = st != S_DONE || memcmp(expect[ch], &mem[dbase[ch]], tot[ch]) != 0;
                for (uint32_t i = 0; i < GUARD; i++) bad |= mem[dbase[ch] - GUARD + i] != 0xA5 || mem[dbase[ch] + tot[ch] + i] != 0xA5;
                tally(&r, bad); busy[ch] = 0; done++; prog = 1;
            }
        }
        idle = prog ? 0 : idle + 1;
        if (idle > 50000) {                                       /* hang = failure, recover */
            tally(&r, 1); done++; idle = 0; eng_init(); E.strict = 0;
            for (int ch = 0; ch < NCH; ch++) busy[ch] = 0;
        }
    }
    return r;
}

/* --------------------------------------------------------------- main --- */
typedef Row (*RowFn)(void);
static const struct { const char *id, *name; RowFn fn; } rows[] = {
    {"R01", "size x src/dst alignment sweep", r01_size_align},
    {"R02", "4 KiB boundary crossing", r02_4k_boundary},
    {"R03", "descriptor chain / gather", r03_chain_sg},
    {"R04", "ring wrap + ring full (-EBUSY)", r04_ring_wrap},
    {"R05", "4-ch priority/aging + RR fairness", r05_concurrency},
    {"R06", "error injection (DECERR)", r06_error_inject},
    {"R07", "IRQ coalescing (count + timer)", r07_irq_coalesce},
    {"R08", "non-coherent cache maintenance", r08_cache_flow},
    {"R09", "channel reset mid-transfer", r09_reset_mid},
    {"R10", "throughput efficiency", r10_perf},
    {"R11", "random soak 1200 xfers, seed", r11_random_soak},
};
enum { NROWS = sizeof rows / sizeof rows[0] };

int main(void)
{
    Row base[NROWS];
    g_bug = B_NONE;
    printf("== baseline (no injected bug) ==\n");
    int base_ok = 1;
    for (int i = 0; i < NROWS; i++) {
        base[i] = rows[i].fn();
        printf("%s %-36s cases=%4d fail=%d  %s\n", rows[i].id, rows[i].name, base[i].cases, base[i].fails,
               base[i].fails ? "FAIL" : "PASS");
        base_ok &= base[i].fails == 0;
    }
    printf("R05 detail: low-prio max wait=%llu cycles (limit %d); RR share min/max = %llu.%llu%% / %llu.%llu%%\n",
           (unsigned long long)g_lowprio_max_wait, 4 * AGE_LIMIT,
           (unsigned long long)(g_share_min_pct / 10), (unsigned long long)(g_share_min_pct % 10),
           (unsigned long long)(g_share_max_pct / 10), (unsigned long long)(g_share_max_pct % 10));
    printf("R10 detail (model: %d B/cycle bus, %d-cycle desc fetch, %d B bursts):\n", BUS_B, DESC_FETCH, BURST);
    for (int i = 0; i < 5; i++)
        printf("  len=%5u  cycles=%5llu  efficiency=%3llu%%\n", perf_len[i], (unsigned long long)perf_cyc[i],
               (unsigned long long)((perf_len[i] / BUS_B) * 100 / perf_cyc[i]));

    printf("\n== mutation matrix: x = row FAILED (bug caught), . = row passed ==\n");
    printf("%-44s", "injected bug");
    for (int i = 0; i < NROWS; i++) printf(" %s", rows[i].id + 1);
    printf("  caught\n");
    int caught_all = 0, caught_by_soak = 0, nbugs = B_COUNT - 1;
    for (int b = 1; b < B_COUNT; b++) {
        g_bug = b;
        int any = 0, soak = 0;
        printf("%-44s", bug_name[b]);
        for (int i = 0; i < NROWS; i++) {
            Row r = rows[i].fn();
            printf("  %c", r.fails ? 'x' : '.');
            if (r.fails) { any = 1; if (i == NROWS - 1) soak = 1; }
        }
        printf("  %s\n", any ? "YES" : "NO");
        caught_all += any; caught_by_soak += soak;
    }
    g_bug = B_NONE;
    printf("\nbaseline: %s\n", base_ok ? "ALL PASS" : "FAIL");
    printf("bugs caught by full matrix : %d/%d\n", caught_all, nbugs);
    printf("bugs caught by random soak : %d/%d  (directed rows needed for the rest)\n", caught_by_soak, nbugs);
    printf("RESULT dma_matrix %s\n", (base_ok && caught_all == nbugs) ? "PASS" : "FAIL");
    return (base_ok && caught_all == nbugs) ? 0 : 1;
}
