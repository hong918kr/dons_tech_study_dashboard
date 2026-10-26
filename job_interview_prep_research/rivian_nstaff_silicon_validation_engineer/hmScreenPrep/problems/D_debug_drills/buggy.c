/*
 * D  Debug Drills (D1-D10) — silicon validation / bring-up flavor
 *
 * Ten short bare-metal functions of the kind a silicon-validation engineer writes
 * on first silicon: poll a status bit, ack an interrupt, run a DMA loopback, hand a
 * descriptor to hardware, wait for a PLL lock, decode a register dump, map a register
 * block, extract a bit field, report bandwidth, run a memory test.
 * Each one compiles and "looks right", but each has exactly ONE realistic bug.
 * For every drill:
 *   1. read the "Intended" comment,
 *   2. say the bug out loud in under a minute (and which input triggers it),
 *   3. fix it with a one-line (or few-line) change.
 *
 * There is no real hardware here. Each drill has a tiny mock HW model
 * (a counter of MMIO reads, a W1C status register, a D-cache modelled as a separate
 * copy array, a DMA engine that snoops descriptors, a fake timer, a fake RAM with
 * canary words). The test main() exposes every bug WITHOUT crashing or hanging.
 *
 * Buggy version.  Build & run:  ./run.sh D buggy     (copy to my.c, fix, ./run.sh D my)
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* ------------------------------------------------------------------------------------
 * D1  Poll a DMA status register until DONE.
 * Intended: poll DMA_STATUS until bit DONE is 1. Return the number of polls that saw
 *           DONE == 0 (0 if it was already done), or -1 after max_polls polls.
 *           Every poll must be a real bus read of the register.
 *
 * Source as written on the bench:
 *     #define DMA_STATUS (*(uint32_t *)0x40001004u)        // no volatile
 *     while (!(DMA_STATUS & DMA_DONE)) { ... }
 * At -O2 the compiler may load DMA_STATUS once and spin on the register copy.
 * The body below is that compiled behavior, written out in C so the host test can see it.
 * Mock HW: dma_status_read() is one bus read; DONE reads 1 from the Nth read on.
 * ---------------------------------------------------------------------------------- */
#define DMA_DONE (1u << 0)
static uint32_t g_d1_reads, g_d1_done_on_read;

static uint32_t dma_status_read(void)          /* models one MMIO load */
{
    g_d1_reads++;
    return g_d1_reads >= g_d1_done_on_read ? DMA_DONE : 0u;
}

int wait_dma_done(uint32_t max_polls)
{
    uint32_t st = dma_status_read();
    for (uint32_t i = 0; i < max_polls; i++) {
        if (st & DMA_DONE) return (int)i;
    }
    return -1;
}

/* ------------------------------------------------------------------------------------
 * D2  Acknowledge one interrupt source.
 * Intended: INT_STATUS is write-1-to-clear (W1C): writing 1 to a bit clears it,
 *           writing 0 leaves it alone. irq_ack(bit) must clear ONLY that bit and
 *           leave every other pending source pending.
 * Mock HW: intc_read()/intc_write() are the bus accessors of a W1C register.
 * ---------------------------------------------------------------------------------- */
#define IRQ_TX_DONE (1u << 0)
#define IRQ_RX_DONE (1u << 1)
#define IRQ_ERR     (1u << 4)
static uint32_t g_int_status;                     /* hardware latch */

static uint32_t intc_read(void)        { return g_int_status; }
static void     intc_write(uint32_t v) { g_int_status &= ~v; }   /* W1C semantics */

void irq_ack(uint32_t bit)
{
    intc_write(intc_read() | bit);
}

/* ------------------------------------------------------------------------------------
 * D3  DMA memory-to-memory loopback test with a write-back D-cache.
 * Intended: CPU writes a pattern into src, DMA copies src -> dst, CPU checks dst.
 *           Return the number of mismatching words (0 = pass). Must pass on every
 *           run, not only the first one.
 * Mock HW: ddr[] is DRAM (what DMA sees). dc_*[] is the CPU D-cache: one word per
 *          "line", write-back. The DMA engine reads/writes ddr[] only (not coherent).
 * ---------------------------------------------------------------------------------- */
#define RAM_WORDS 64
static uint32_t ddr[RAM_WORDS];
static uint32_t dc_data[RAM_WORDS];
static bool     dc_valid[RAM_WORDS], dc_dirty[RAM_WORDS];

static uint32_t cpu_rd(uint32_t a)
{
    if (!dc_valid[a]) { dc_data[a] = ddr[a]; dc_valid[a] = true; }   /* line fill */
    return dc_data[a];
}
static void cpu_wr(uint32_t a, uint32_t v) { dc_data[a] = v; dc_valid[a] = dc_dirty[a] = true; }
static void dcache_clean(uint32_t a, uint32_t n)          /* write dirty lines back to DRAM */
{
    for (uint32_t i = a; i < a + n; i++)
        if (dc_valid[i] && dc_dirty[i]) { ddr[i] = dc_data[i]; dc_dirty[i] = false; }
}
static void dcache_invalidate(uint32_t a, uint32_t n)     /* drop lines; next read refills */
{
    for (uint32_t i = a; i < a + n; i++) dc_valid[i] = dc_dirty[i] = false;
}
static void dma_copy(uint32_t src, uint32_t dst, uint32_t n)   /* start + wait for DONE */
{
    for (uint32_t i = 0; i < n; i++) ddr[dst + i] = ddr[src + i];
}

uint32_t dma_loopback_test(uint32_t src, uint32_t dst, uint32_t n, uint32_t seed)
{
    for (uint32_t i = 0; i < n; i++) cpu_wr(src + i, seed + i * 0x01010101u);
    dcache_clean(src, n);                       /* make the pattern visible to DMA */
    dma_copy(src, dst, n);
    uint32_t bad = 0;
    for (uint32_t i = 0; i < n; i++)
        if (cpu_rd(dst + i) != seed + i * 0x01010101u) bad++;
    return bad;
}

/* ------------------------------------------------------------------------------------
 * D4  Hand a TX descriptor to the DMA engine.
 * Intended: fill buf_addr and length, then give the descriptor to HW by setting OWN.
 *           The engine may fetch the descriptor at ANY moment once OWN = 1, so it
 *           must see the new buf_addr and length together with OWN.
 * Layout:   ctrl[31] = OWN (1 = owned by HW), ctrl[30] = IOC (irq on complete),
 *           ctrl[15:0] = length in bytes.
 * Mock HW:  DESC_WR() is one store to descriptor memory; after every store the engine
 *           snoops the descriptor and, the first time it sees OWN = 1, latches a copy.
 * ---------------------------------------------------------------------------------- */
#define DESC_OWN (1u << 31)
#define DESC_IOC (1u << 30)
typedef struct { uint32_t buf_addr; uint32_t ctrl; } DmaDesc;
static DmaDesc g_hw_seen;                       /* what the engine fetched */
static bool    g_hw_took;

static void hw_snoop(const DmaDesc *d)
{
    if (!g_hw_took && (d->ctrl & DESC_OWN)) { g_hw_seen = *d; g_hw_took = true; }
}
#define DESC_WR(d, field, v) do { (d)->field = (v); hw_snoop(d); } while (0)
static inline void dma_wmb(void) { /* real HW: __DSB() / wmb(); no-op in the model */ }

void desc_submit(DmaDesc *d, uint32_t buf, uint16_t len)
{
    DESC_WR(d, ctrl, DESC_OWN | DESC_IOC | len);
    dma_wmb();
    DESC_WR(d, buf_addr, buf);
}

/* ------------------------------------------------------------------------------------
 * D5  Wait for PLL lock with a timeout on a free-running 32-bit timer.
 * Intended: poll PLL_STATUS.LOCK. Return the number of polls that saw LOCK == 0, or
 *           -1 once at least `timeout` ticks have elapsed since the start.
 *           The timer wraps from 0xFFFFFFFF to 0, and that must not matter.
 *           d5_elapsed records the ticks elapsed when the timeout fired.
 * Mock HW:  timer_now() advances 1000 ticks per call. LOCK reads 1 from the Nth read on
 *           (N = 0 means the PLL never locks: fault injection).
 * ---------------------------------------------------------------------------------- */
#define PLL_LOCK (1u << 0)
static uint32_t g_tick, g_pll_reads, g_pll_lock_on_read, d5_elapsed;

static uint32_t timer_now(void) { uint32_t t = g_tick; g_tick += 1000u; return t; }
static uint32_t pll_status_read(void)
{
    g_pll_reads++;
    return (g_pll_lock_on_read && g_pll_reads >= g_pll_lock_on_read) ? PLL_LOCK : 0u;
}

int wait_pll_lock(uint32_t timeout)
{
    uint32_t start = timer_now();
    int polls = 0;
    while (!(pll_status_read() & PLL_LOCK)) {
        uint32_t now = timer_now();
        if (now > start + timeout) { d5_elapsed = now - start; return -1; }
        polls++;
        if (polls > 100000) return -2;        /* TEST GUARD (not part of the drill) */
    }
    return polls;
}

/* ------------------------------------------------------------------------------------
 * D6  Decode a 32-bit register value from a debug-UART dump.
 * Intended: the on-chip debug monitor dumps each 32-bit register as 4 bytes,
 *           LITTLE-endian on the wire: b[0] = bits 7..0, ..., b[3] = bits 31..24.
 *           Return the register value.
 * ---------------------------------------------------------------------------------- */
uint32_t reg_from_dump(const uint8_t b[4])
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
}

/* ------------------------------------------------------------------------------------
 * D7  Register map of a UART block (datasheet):
 *       0x00 CTRL   0x04 STATUS   0x08-0x0F reserved   0x10 INT_EN   0x14 DATA
 *     All registers are 32-bit. Block size 0x18.
 * Intended: uart_enable_rx_irq() sets INT_EN.RX (bit 0) at offset 0x10.
 * Mock HW:  uart_block[] is the register block; the fake UART decodes it by offset.
 * ---------------------------------------------------------------------------------- */
#define UART_INT_RX (1u << 0)
typedef struct {
    uint32_t CTRL;          /* 0x00 */
    uint32_t STATUS;        /* 0x04 */
    uint16_t RESERVED[2];   /* 0x08 - 0x0F */
    uint32_t INT_EN;        /* 0x10 */
    uint32_t DATA;          /* 0x14 */
} UartRegs;
static uint32_t uart_block[8];

void uart_enable_rx_irq(volatile UartRegs *u)
{
    u->INT_EN |= UART_INT_RX;
}

/* ------------------------------------------------------------------------------------
 * D8  Extract a bit field from a register value.
 * Intended: return bits [lsb + width - 1 : lsb] of reg, right-aligned.
 *           Valid for 1 <= width <= 32 and lsb + width <= 32 (width 32 = whole register).
 * ---------------------------------------------------------------------------------- */
uint32_t field_get(uint32_t reg, unsigned lsb, unsigned width)
{
    uint32_t mask = (1u << width) - 1u;
    return (reg >> lsb) & mask;
}

/* ------------------------------------------------------------------------------------
 * D9  Report DMA bandwidth from a cycle-counter measurement.
 * Intended: `bytes` were moved in `cycles` cycles of a `clk_mhz` MHz counter.
 *           Return MB/s (10^6 bytes per second), truncated:
 *             MB/s = bytes / (cycles / clk_mhz [us]) = bytes * clk_mhz / cycles.
 *           Must be exact for bytes up to 4 GiB - 1 and clk_mhz up to 2000.
 * ---------------------------------------------------------------------------------- */
uint32_t bw_mbps(uint32_t bytes, uint32_t cycles, uint32_t clk_mhz)
{
    uint64_t bw = bytes * clk_mhz / cycles;
    return (uint32_t)bw;
}

/* ------------------------------------------------------------------------------------
 * D10 Write/read-back memory test over a region given in BYTES.
 * Intended: for every 32-bit word in [base, base + len_bytes) write a pattern, then
 *           read all words back. Return the number of mismatching words.
 *           Must not touch a single byte outside the region.
 * Mock HW:  fake_ram[0..15] is the region under test (64 bytes); the words after it
 *           are canaries standing in for "whatever lives next to the region".
 * ---------------------------------------------------------------------------------- */
#define D10_REGION_WORDS 16
#define D10_RAM_WORDS    (D10_REGION_WORDS * 4 + 16)   /* big enough that a 4x overrun stays in-bounds */
#define CANARY(i)        (0xC0FFEE00u | (uint32_t)(i))
static uint32_t fake_ram[D10_RAM_WORDS];

uint32_t mem_test(uint32_t *base, size_t len_bytes)
{
    uint32_t *end = base + len_bytes;
    for (uint32_t *p = base; p < end; p++) *p = 0xA5A50000u ^ (uint32_t)(p - base);
    uint32_t bad = 0;
    for (uint32_t *p = base; p < end; p++)
        if (*p != (0xA5A50000u ^ (uint32_t)(p - base))) bad++;
    return bad;
}

/* ================================== tests ======================================== */
static int g_sec_fail;
static void begin(void) { g_sec_fail = g_fail; }
static void end(const char *name) { printf("%-30s %s\n", name, g_fail == g_sec_fail ? "ok" : "<-- BUG"); }

int main(void)
{
    /* D1 */
    begin();
    g_d1_reads = 0; g_d1_done_on_read = 1;
    CHECK(wait_dma_done(100) == 0);                        /* already done */
    g_d1_reads = 0; g_d1_done_on_read = 4;                 /* DONE appears on the 4th read */
    CHECK(wait_dma_done(100) == 3);
    CHECK(g_d1_reads == 4);                                /* one bus read per poll */
    g_d1_reads = 0; g_d1_done_on_read = 1000;              /* never within budget */
    CHECK(wait_dma_done(10) == -1);
    CHECK(g_d1_reads == 10);
    end("D1 poll without volatile");

    /* D2 */
    begin();
    g_int_status = IRQ_TX_DONE;
    irq_ack(IRQ_TX_DONE);
    CHECK(intc_read() == 0);                              /* normal: one source */
    g_int_status = IRQ_TX_DONE | IRQ_RX_DONE | IRQ_ERR;    /* three sources pending */
    irq_ack(IRQ_TX_DONE);
    CHECK(intc_read() == (IRQ_RX_DONE | IRQ_ERR));        /* RX and ERR must survive */
    end("D2 W1C read-modify-write");

    /* D3 */
    begin();
    memset(ddr, 0, sizeof ddr); dcache_invalidate(0, RAM_WORDS);          /* cold start */
    CHECK(dma_loopback_test(0, 32, 16, 0x11110000u) == 0); /* run 1: dst not in cache yet */
    CHECK(dma_loopback_test(0, 32, 16, 0x22220000u) == 0); /* run 2: dst lines cached by run 1 */
    CHECK(ddr[32 + 5] == 0x22220000u + 5u * 0x01010101u);  /* DMA itself did write DRAM */
    end("D3 DMA cache coherency");

    /* D4 */
    begin();
    DmaDesc d = { 0xDEAD0000u, 0 };                        /* stale buf_addr from the last use */
    g_hw_took = false;
    desc_submit(&d, 0x80001000u, 256);
    CHECK(g_hw_took);
    CHECK(g_hw_seen.buf_addr == 0x80001000u);              /* engine must fetch the NEW buffer */
    CHECK((g_hw_seen.ctrl & 0xFFFFu) == 256u);
    CHECK(d.buf_addr == 0x80001000u && (d.ctrl & DESC_OWN));
    end("D4 descriptor OWN ordering");

    /* D5 */
    begin();
    g_tick = 1000; g_pll_reads = 0; g_pll_lock_on_read = 6;
    CHECK(wait_pll_lock(50000) == 5);                      /* normal: locks on the 6th read */
    g_tick = 1000; g_pll_reads = 0; g_pll_lock_on_read = 0; d5_elapsed = 0;
    CHECK(wait_pll_lock(50000) == -1);                     /* never locks, no wrap */
    CHECK(d5_elapsed >= 50000 && d5_elapsed <= 51000);
    g_tick = 0xFFFFF000u; g_pll_reads = 0; d5_elapsed = 0; /* start 4096 ticks before wrap */
    CHECK(wait_pll_lock(50000) == -1);
    CHECK(d5_elapsed >= 50000 && d5_elapsed <= 51000);     /* must not give up early */
    end("D5 timeout across wrap");

    /* D6 */
    begin();
    const uint8_t id[4]  = { 0x78, 0x56, 0x34, 0x12 };    /* CHIP_ID = 0x12345678 */
    const uint8_t one[4] = { 0x01, 0x00, 0x00, 0x00 };
    const uint8_t top[4] = { 0x00, 0x00, 0x00, 0x80 };
    CHECK(reg_from_dump(id)  == 0x12345678u);
    CHECK(reg_from_dump(one) == 1u);
    CHECK(reg_from_dump(top) == 0x80000000u);
    end("D6 endianness");

    /* D7 */
    begin();
    memset(uart_block, 0, sizeof uart_block);
    uart_enable_rx_irq((volatile UartRegs *)(void *)uart_block);
    CHECK(uart_block[0x10 / 4] & UART_INT_RX);             /* INT_EN lives at 0x10 */
    CHECK(uart_block[0x0C / 4] == 0);                      /* reserved word untouched */
    CHECK(offsetof(UartRegs, INT_EN) == 0x10);
    CHECK(offsetof(UartRegs, DATA) == 0x14);
    CHECK(sizeof(UartRegs) == 0x18);
    end("D7 register map layout");

    /* D8 */
    begin();
    CHECK(field_get(0xABCD1234u, 8, 8) == 0x12u);          /* normal */
    CHECK(field_get(0xABCD1234u, 28, 4) == 0xAu);          /* top nibble */
    CHECK(field_get(0xABCD1234u, 0, 1) == 0u);
    CHECK(field_get(0xABCD1234u, 0, 32) == 0xABCD1234u);   /* whole register */
    end("D8 mask shift UB");

    /* D9 */
    begin();
    CHECK(bw_mbps(4096, 1000, 1000) == 4096u);             /* normal: 4 KiB in 1 us */
    CHECK(bw_mbps(16777216u, 4194304u, 1000) == 4000u);    /* 16 MiB at 4 GB/s */
    CHECK(bw_mbps(0xFFFFFFFFu, 0xFFFFFFFFu, 2000) == 2000u);
    end("D9 bandwidth overflow");

    /* D10 */
    begin();
    for (uint32_t i = 0; i < D10_RAM_WORDS; i++) fake_ram[i] = CANARY(i);
    CHECK(mem_test(fake_ram, D10_REGION_WORDS * sizeof(uint32_t)) == 0);
    uint32_t smashed = 0;
    for (uint32_t i = D10_REGION_WORDS; i < D10_RAM_WORDS; i++) smashed += fake_ram[i] != CANARY(i);
    CHECK(smashed == 0);                                   /* nothing outside the region touched */
    CHECK(fake_ram[D10_REGION_WORDS - 1] == (0xA5A50000u ^ 15u));   /* last word was tested */
    end("D10 memtest pointer scale");

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
