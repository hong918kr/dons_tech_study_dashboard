/*
 * V3  DMA Descriptor Ring Driver (memcpy / AXI-DMA engine, validation harness)
 *
 * A memcpy DMA engine walks a ring of 32-byte descriptors in memory. Software fills a
 * descriptor and hands it to hardware by setting OWN; hardware copies the data, writes
 * status, clears OWN and moves to the next slot. The engine is a FAKE at the bottom of
 * this file: the test calls dma_hw_step(budget) to let it run (on silicon it runs by itself).
 *
 *   ctrl   b31 OWN (1 = HW owns) | b30 IRQ (interrupt when done) | b29 EOP (last of a transfer)
 *   status b0 DONE | b3:1 ERR (0 = OK, 1 = SLVERR, 2 = DECERR); written by HW before clearing OWN
 *
 * Implement:
 *   ring_init     n must be a power of two in [2, RING_MAX] (-EINVAL). Clear every descriptor,
 *                 reset the SW indices, program the engine with dma_hw_set_ring().
 *   dma_submit    split len into chunks of at most DMA_MAX_LEN bytes, one descriptor each;
 *                 EOP|IRQ only on the last. Fill all fields first, set OWN LAST (dma_wmb()
 *                 before it). Not enough free slots -> -EBUSY and nothing is written.
 *                 len == 0, or more descriptors than the whole ring -> -EINVAL.
 *                 Returns a cookie (0, 1, 2, ...) that identifies the transfer.
 *   dma_reap      walk from tail while the slot was submitted (tail != head) and OWN == 0.
 *                 dma_rmb() before reading status. At each EOP call cb(ctx, cookie, err, bytes):
 *                 err = 0 or -EIO (any descriptor of the transfer had ERR != 0 or no DONE),
 *                 bytes = sum of len of the good descriptors. Returns the number of callbacks.
 *   dma_selftest  copy patterns of sizes {1,3,4,63,64,255,256,257,700,1024} x dst offsets 0..3
 *                 (40 cases), one transfer at a time, and check the destination AND the guard
 *                 bytes before/after it. Fill SelftestReport; return 0 if all pass, else -EIO.
 *
 * head/tail are free-running uint32_t counters; slot = idx & mask. used = head - tail.
 *
 * Build & run:  ./run.sh V3
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* ---------------- descriptor format (from the engine spec) ---------------- */
typedef struct {
    uint64_t src;        /* source bus address */
    uint64_t dst;        /* destination bus address */
    uint32_t len;        /* bytes, 1 .. DMA_MAX_LEN */
    uint32_t ctrl;       /* OWN | IRQ | EOP */
    uint32_t status;     /* DONE | ERR, written by HW */
    uint32_t rsvd;       /* pad to 32 bytes */
} DmaDesc;

#define CTRL_OWN      (1u << 31)
#define CTRL_IRQ      (1u << 30)
#define CTRL_EOP      (1u << 29)
#define ST_DONE       (1u << 0)
#define ST_ERR_SHIFT  1
#define ST_ERR_MASK   (7u << ST_ERR_SHIFT)
#define ERR_SLVERR    1u

#define DMA_MAX_LEN   256u          /* bytes per descriptor */
#define RING_MAX      64u

/* Ordering barriers for memory shared with a DMA master (Linux: dma_wmb / dma_rmb). */
#define dma_wmb() __atomic_thread_fence(__ATOMIC_RELEASE)
#define dma_rmb() __atomic_thread_fence(__ATOMIC_ACQUIRE)

/* ---------------- HAL (provided, implemented by the fake) ---------------- */
void dma_hw_set_ring(volatile DmaDesc *base, uint32_t n);  /* RING_BASE/RING_SIZE, HW ptr = 0 */
int  dma_hw_step(int budget);   /* harness only: engine processes up to budget descriptors */

typedef void (*dma_cb)(void *ctx, int32_t cookie, int err, uint32_t bytes);

typedef struct {
    volatile DmaDesc *desc;     /* ring memory (shared with HW) */
    uint32_t n, mask;
    uint32_t head;              /* free-running: next slot SW fills */
    uint32_t tail;              /* free-running: next slot SW reaps */
    int32_t  next_cookie;
    int32_t  cookie[RING_MAX];  /* SW-only shadow, per slot */
    uint32_t acc_bytes;         /* transfer being reaped: good bytes so far */
    int      acc_err;           /* transfer being reaped: 0 or -EIO */
} DmaRing;

typedef struct { uint32_t cases, pass, data_err, guard_err, dma_err, timeout; } SelftestReport;

int     ring_init(DmaRing *r, volatile DmaDesc *mem, uint32_t n);
int32_t dma_submit(DmaRing *r, const void *src, void *dst, uint32_t len);
int     dma_reap(DmaRing *r, dma_cb cb, void *ctx);
int     dma_selftest(DmaRing *r, SelftestReport *rep);

/* ---------------- your code ---------------- */
int ring_init(DmaRing *r, volatile DmaDesc *mem, uint32_t n)
{
    /* TODO: validate n, clear descriptors, reset indices, dma_hw_set_ring() */
    (void)r; (void)mem; (void)n;
    return 0;
}

int32_t dma_submit(DmaRing *r, const void *src, void *dst, uint32_t len)
{
    /* TODO: split into <= DMA_MAX_LEN chunks, fill fields, dma_wmb(), set OWN last */
    (void)r; (void)src; (void)dst; (void)len;
    return 0;
}

int dma_reap(DmaRing *r, dma_cb cb, void *ctx)
{
    /* TODO: walk from tail while tail != head and OWN == 0; callback at each EOP */
    (void)r; (void)cb; (void)ctx;
    return 0;
}

int dma_selftest(DmaRing *r, SelftestReport *rep)
{
    /* TODO: 10 sizes x 4 dst offsets; check data and guard bytes; fill rep */
    (void)r; (void)rep;
    return -1;
}

/* =====================================================================
 * Test harness: fake DMA engine (do not edit)
 * ===================================================================== */
static struct {
    volatile DmaDesc *ring;
    uint32_t n;
    uint32_t hw;            /* free-running HW pointer (slot = hw & (n-1)) */
    uint32_t seq;           /* descriptors processed since fake_reset (1-based) */
    uint32_t err_at;        /* seq that fails with SLVERR (no copy); 0 = off */
    uint32_t overrun_at;    /* seq that copies len + 1 bytes (HW bug); 0 = off */
    bool     overrun_eop;   /* every EOP descriptor copies len + 1 */
    uint32_t short_at;      /* seq that copies len - 1 bytes (HW bug); 0 = off */
} F;

static void fake_reset(void) { memset(&F, 0, sizeof F); }

void dma_hw_set_ring(volatile DmaDesc *base, uint32_t n) { F.ring = base; F.n = n; F.hw = 0; }

int dma_hw_step(int budget)
{
    int k = 0;
    if (F.ring == NULL) return 0;
    while (k < budget) {
        volatile DmaDesc *d = &F.ring[F.hw & (F.n - 1)];
        if (!(d->ctrl & CTRL_OWN)) break;            /* nothing more for HW */
        F.seq++;
        uint32_t len = d->len, st = ST_DONE;
        if (F.seq == F.err_at || len == 0 || len > DMA_MAX_LEN) {
            st |= ERR_SLVERR << ST_ERR_SHIFT;
        } else {
            uint32_t n = len;
            if (F.seq == F.overrun_at || (F.overrun_eop && (d->ctrl & CTRL_EOP))) n = len + 1;
            if (F.seq == F.short_at) n = len - 1;
            memcpy((void *)(uintptr_t)d->dst, (const void *)(uintptr_t)d->src, n);
        }
        d->status = st;
        dma_wmb();
        d->ctrl &= ~CTRL_OWN;                         /* hand back to SW */
        F.hw++;
        k++;
    }
    return k;
}

/* ---- test helpers ---- */
typedef struct { int n; int32_t ck[16]; int err[16]; uint32_t bytes[16]; } Log;

static void log_cb(void *ctx, int32_t ck, int err, uint32_t bytes)
{
    Log *l = ctx;
    if (l->n < 16) { l->ck[l->n] = ck; l->err[l->n] = err; l->bytes[l->n] = bytes; }
    l->n++;
}

/* wrap test: in-order check + data check per transfer */
#define WR_SLOT 512u
static uint8_t wr_src[256 + 300];
static uint8_t wr_dst[8][WR_SLOT];
typedef struct { int32_t expect; uint32_t len[8], soff[8]; int order_err, data_err, done; } WrapCtx;

static void wrap_cb(void *ctx, int32_t ck, int err, uint32_t bytes)
{
    WrapCtx *w = ctx;
    uint32_t s = (uint32_t)ck & 7u;
    if (ck != w->expect || err != 0) w->order_err++;
    else if (bytes != w->len[s] || memcmp(wr_dst[s], wr_src + w->soff[s], bytes) != 0) w->data_err++;
    w->expect = ck + 1;
    w->done++;
}

static uint32_t lcg(uint32_t *x) { *x = *x * 1664525u + 1013904223u; return *x >> 8; }

static DmaDesc ring_mem[RING_MAX];
static DmaRing R;

int main(void)
{
    static uint8_t a[1024], b[1024];
    for (int i = 0; i < 1024; i++) a[i] = (uint8_t)(i * 13 + 5);
    Log lg;

    /* 1. basic: one 100-byte transfer, descriptor fields, OWN handoff, completion */
    fake_reset();
    CHECK(ring_init(&R, ring_mem, 6) == -EINVAL);          /* not a power of two */
    CHECK(ring_init(&R, ring_mem, 0) == -EINVAL);
    CHECK(ring_init(&R, ring_mem, 8) == 0);
    memset(b, 0, sizeof b);
    CHECK(dma_submit(&R, a, b, 100) == 0);                 /* first cookie is 0 */
    CHECK(ring_mem[0].len == 100 && ring_mem[0].src == (uint64_t)(uintptr_t)a
          && ring_mem[0].dst == (uint64_t)(uintptr_t)b);
    CHECK(ring_mem[0].ctrl == (CTRL_OWN | CTRL_IRQ | CTRL_EOP) && ring_mem[0].status == 0);
    memset(&lg, 0, sizeof lg);
    CHECK(dma_reap(&R, log_cb, &lg) == 0);                 /* HW has not run yet */
    CHECK(dma_hw_step(8) == 1);
    CHECK((ring_mem[0].ctrl & CTRL_OWN) == 0 && ring_mem[0].status == ST_DONE);
    CHECK(dma_reap(&R, log_cb, &lg) == 1);
    CHECK(lg.n == 1 && lg.ck[0] == 0 && lg.err[0] == 0 && lg.bytes[0] == 100);
    CHECK(memcmp(a, b, 100) == 0 && b[100] == 0);
    CHECK(dma_reap(&R, log_cb, &lg) == 0);                 /* OWN==0 but never submitted */
    CHECK(dma_submit(&R, a, b, 0) == -EINVAL);

    /* 2. full ring: 8 fit, 9th -> -EBUSY; reaping 3 frees exactly 3 */
    fake_reset();
    CHECK(ring_init(&R, ring_mem, 8) == 0);
    int ok = 0;
    for (int i = 0; i < 8; i++) ok += dma_submit(&R, a, b, 16) == i;
    CHECK(ok == 8);
    CHECK(dma_submit(&R, a, b, 16) == -EBUSY);
    memset(&lg, 0, sizeof lg);
    CHECK(dma_reap(&R, log_cb, &lg) == 0);                 /* all OWN = 1 */
    CHECK(dma_hw_step(3) == 3);
    CHECK(dma_reap(&R, log_cb, &lg) == 3 && lg.ck[2] == 2);
    ok = 0;
    for (int i = 0; i < 3; i++) ok += dma_submit(&R, a, b, 16) == 8 + i;
    CHECK(ok == 3);
    CHECK(dma_submit(&R, a, b, 16) == -EBUSY);
    CHECK(R.head - R.tail == 8);

    /* 3. wrap-around many times, indices also crossing 2^32 */
    for (int phase = 0; phase < 2; phase++) {
        fake_reset();
        CHECK(ring_init(&R, ring_mem, 8) == 0);
        if (phase == 1) { R.head = R.tail = 0xFFFFFFF0u; F.hw = 0xFFFFFFF0u; }
        for (uint32_t i = 0; i < sizeof wr_src; i++) wr_src[i] = (uint8_t)(i * 29 + 3);
        WrapCtx w; memset(&w, 0, sizeof w);
        uint32_t x = 12345u, submitted = 0, busy = 0;
        for (int round = 0; round < 500; round++) {
            int k = 1 + (int)(lcg(&x) % 3);
            for (int j = 0; j < k; j++) {
                uint32_t len = 1 + lcg(&x) % 300, soff = lcg(&x) % 256;
                uint32_t s = (uint32_t)R.next_cookie & 7u;
                int32_t ck = dma_submit(&R, wr_src + soff, wr_dst[s], len);
                if (ck == -EBUSY) { busy++; break; }
                if (ck < 0 || ck != (int32_t)submitted) { w.order_err++; break; }
                w.len[s] = len; w.soff[s] = soff; submitted++;
            }
            dma_hw_step(1 + (int)(lcg(&x) % 4));
            if (lcg(&x) % 2) dma_reap(&R, wrap_cb, &w);
        }
        for (int spin = 0; spin < 100 && w.done < (int)submitted; spin++) {
            dma_hw_step(8);
            dma_reap(&R, wrap_cb, &w);
        }
        CHECK(submitted > 300 && busy > 0);
        CHECK(w.done == (int)submitted && w.order_err == 0 && w.data_err == 0);
        CHECK(R.head == R.tail && R.head - (phase ? 0xFFFFFFF0u : 0u) > 400u);
    }

    /* 4. split: 700 bytes -> 256 + 256 + 188, EOP|IRQ only on the last */
    fake_reset();
    CHECK(ring_init(&R, ring_mem, 8) == 0);
    memset(b, 0, sizeof b);
    CHECK(dma_submit(&R, a, b, 700) == 0);
    CHECK(R.head == 3);
    CHECK(ring_mem[0].len == 256 && ring_mem[1].len == 256 && ring_mem[2].len == 188);
    CHECK(ring_mem[1].src == (uint64_t)(uintptr_t)(a + 256) && ring_mem[2].dst == (uint64_t)(uintptr_t)(b + 512));
    CHECK(ring_mem[0].ctrl == CTRL_OWN && ring_mem[1].ctrl == CTRL_OWN
          && ring_mem[2].ctrl == (CTRL_OWN | CTRL_IRQ | CTRL_EOP));
    memset(&lg, 0, sizeof lg);
    CHECK(dma_hw_step(2) == 2);
    CHECK(dma_reap(&R, log_cb, &lg) == 0);                 /* chain not finished: no callback */
    CHECK(dma_hw_step(8) == 1);
    CHECK(dma_reap(&R, log_cb, &lg) == 1 && lg.bytes[0] == 700 && lg.err[0] == 0);
    CHECK(memcmp(a, b, 700) == 0);
    /* 6 slots used -> a 3-descriptor transfer does not fit and writes nothing */
    for (int i = 0; i < 6; i++) dma_submit(&R, a, b, 8);
    uint32_t head_before = R.head;
    CHECK(dma_submit(&R, a, b, 600) == -EBUSY && R.head == head_before);
    CHECK(dma_submit(&R, a, b, 9 * 256) == -EINVAL);        /* 9 descriptors > ring size 8 */

    /* 5. error descriptor: SLVERR on the 2nd descriptor, and inside a split transfer */
    fake_reset();
    CHECK(ring_init(&R, ring_mem, 8) == 0);
    F.err_at = 2;
    for (int i = 0; i < 3; i++) dma_submit(&R, a, b, 64);
    memset(&lg, 0, sizeof lg);
    CHECK(dma_hw_step(8) == 3);
    CHECK(dma_reap(&R, log_cb, &lg) == 3);
    CHECK(lg.err[0] == 0 && lg.err[1] == -EIO && lg.bytes[1] == 0 && lg.err[2] == 0 && lg.bytes[2] == 64);
    fake_reset();
    CHECK(ring_init(&R, ring_mem, 8) == 0);
    F.err_at = 2;                                           /* middle chunk of 700 */
    dma_submit(&R, a, b, 700);
    CHECK(dma_submit(&R, a, b, 10) == 1);
    memset(&lg, 0, sizeof lg);
    dma_hw_step(8);
    CHECK(dma_reap(&R, log_cb, &lg) == 2);
    CHECK(lg.err[0] == -EIO && lg.bytes[0] == 444 && lg.err[1] == 0 && lg.bytes[1] == 10);

    /* 6. self-test: clean engine passes all 40 cases */
    SelftestReport rep = { 0 };
    fake_reset();
    CHECK(ring_init(&R, ring_mem, 8) == 0);
    CHECK(dma_selftest(&R, &rep) == 0);
    CHECK(rep.cases == 40 && rep.pass == 40);

    /* 7. self-test catches HW bugs: overrun by 1 byte, short copy, bus error */
    fake_reset(); ring_init(&R, ring_mem, 8); F.overrun_at = 1;
    CHECK(dma_selftest(&R, &rep) == -EIO);
    CHECK(rep.guard_err == 1 && rep.pass == 39);
    fake_reset(); ring_init(&R, ring_mem, 8); F.overrun_eop = true;
    CHECK(dma_selftest(&R, &rep) == -EIO);
    CHECK(rep.guard_err == 40 && rep.pass == 0);
    fake_reset(); ring_init(&R, ring_mem, 8); F.short_at = 3;
    CHECK(dma_selftest(&R, &rep) == -EIO);
    CHECK(rep.data_err == 1 && rep.pass == 39);
    fake_reset(); ring_init(&R, ring_mem, 8); F.err_at = 5;
    CHECK(dma_selftest(&R, &rep) == -EIO);
    CHECK(rep.dma_err == 1 && rep.pass == 39 && rep.timeout == 0);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
