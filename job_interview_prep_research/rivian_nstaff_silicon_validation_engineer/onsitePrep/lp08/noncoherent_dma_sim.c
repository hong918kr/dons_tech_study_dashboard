// noncoherent_dma_sim.c - tiny model of a NON-coherent SoC:
//   CPU  <-> write-back data cache (64 B lines) <-> DRAM <-> DMA engine
// The DMA engine reads/writes DRAM directly and never looks in the CPU cache
// (this is what "no hardware coherency / no snooping" means).
// We replay 4 classic bugs and the fix for each. What Linux arm64 does
// (arch/arm64/mm/dma-mapping.c, v6.18):
//   map / sync_for_device : dcache_clean_poc  (clean = write back dirty lines) for EVERY direction
//   unmap / sync_for_cpu  : dcache_inval_poc  (invalidate) unless dir == DMA_TO_DEVICE
// Cases:
//   TX  (DMA_TO_DEVICE)   : without the clean, the device reads stale DRAM
//   RX  (DMA_FROM_DEVICE) : without the invalidate, the CPU reads stale cached lines
//   RX + speculative fill : CPU may pull a line in DURING the DMA -> invalidate must be at unmap
//   RX buffer sharing a cache line with a CPU variable : eviction of that dirty line overwrites DMA data
//
// macOS build (spec flags):
//   cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 \
//      -Wall -Wextra -Werror -fsanitize=address,undefined noncoherent_dma_sim.c -o /tmp/nc && /tmp/nc
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LINE     64u
#define MEMSZ    2048u
#define NLINES   (MEMSZ / LINE)

static uint8_t dram[MEMSZ];
static struct { int valid, dirty; uint8_t d[LINE]; } cache[NLINES];  /* fully "mapped": 1 slot per line */

/* ---- CPU side: every access goes through the cache ---- */
static void fill(unsigned l) { if (!cache[l].valid) { memcpy(cache[l].d, dram + l * LINE, LINE); cache[l].valid = 1; cache[l].dirty = 0; } }
static uint8_t cpu_rd(unsigned a)            { fill(a / LINE); return cache[a / LINE].d[a % LINE]; }
static void    cpu_wr(unsigned a, uint8_t v) { fill(a / LINE); cache[a / LINE].d[a % LINE] = v; cache[a / LINE].dirty = 1; }
static void evict(unsigned l) { if (cache[l].valid && cache[l].dirty) memcpy(dram + l * LINE, cache[l].d, LINE); cache[l].valid = cache[l].dirty = 0; }
static void evict_all(void) { for (unsigned l = 0; l < NLINES; l++) evict(l); }   /* cache pressure / natural eviction */

/* ---- cache maintenance by address range (like arm64 dcache_clean_poc / dcache_inval_poc) ---- */
static void clean(unsigned a, unsigned n)      /* write dirty lines back, keep them valid */
{ for (unsigned l = a / LINE; l <= (a + n - 1) / LINE; l++) if (cache[l].valid && cache[l].dirty) { memcpy(dram + l * LINE, cache[l].d, LINE); cache[l].dirty = 0; } }
static void invalidate(unsigned a, unsigned n) /* drop lines WITHOUT writing back */
{ for (unsigned l = a / LINE; l <= (a + n - 1) / LINE; l++) cache[l].valid = cache[l].dirty = 0; }

/* ---- device side: DRAM only ---- */
static void dma_read(uint8_t *dst, unsigned a, unsigned n)        { memcpy(dst, dram + a, n); }
static void dma_write(unsigned a, uint8_t v, unsigned n)          { memset(dram + a, v, n); }

static void reset(void) { memset(dram, 0, sizeof dram); memset(cache, 0, sizeof cache); }
static int fails;
static void report(const char *name, int ok, int expect_ok)
{
    printf("  %-58s %s%s\n", name, ok ? "data OK " : "CORRUPT ", ok == expect_ok ? "" : "  <-- UNEXPECTED");
    if (ok != expect_ok) fails++;
}

/* 1. TX: CPU fills a buffer, device reads it */
static void tx(int do_clean)
{
    reset();
    unsigned buf = 0x100, n = 128; uint8_t seen[128];
    for (unsigned i = 0; i < n; i++) cpu_wr(buf + i, 0xA5);   /* data sits DIRTY in cache */
    if (do_clean) clean(buf, n);                              /* dma_map_single(.., DMA_TO_DEVICE) */
    dma_read(seen, buf, n);
    int ok = 1; for (unsigned i = 0; i < n; i++) ok &= seen[i] == 0xA5;
    report(do_clean ? "TX with clean before DMA" : "TX without clean (device reads stale DRAM)", ok, do_clean);
}

/* 2. RX: device writes, CPU reads — but the CPU had the lines cached from earlier */
static void rx(int do_inval)
{
    reset();
    unsigned buf = 0x200, n = 128;
    for (unsigned i = 0; i < n; i++) (void)cpu_rd(buf + i);   /* earlier use left CLEAN copies in cache */
    dma_write(buf, 0x5A, n);
    if (do_inval) invalidate(buf, n);                         /* dma_unmap / dma_sync_single_for_cpu */
    int ok = 1; for (unsigned i = 0; i < n; i++) ok &= cpu_rd(buf + i) == 0x5A;
    report(do_inval ? "RX with invalidate before CPU reads" : "RX without invalidate (CPU hits stale lines)", ok, do_inval);
}

/* 3. RX + speculative prefetch DURING the DMA: maintenance only at map time is not enough */
static void rx_spec(int inval_at_unmap)
{
    reset();
    unsigned buf = 0x300, n = 64;
    clean(buf, n);                              /* map time: arm64 cleans (nothing dirty here) */
    dma_write(buf, 0x11, n / 2);                /* device writes first half ... */
    (void)cpu_rd(buf);                          /* ... CPU speculatively pulls the line in (half-new) */
    dma_write(buf + n / 2, 0x22, n / 2);        /* device finishes */
    if (inval_at_unmap) invalidate(buf, n);     /* unmap time */
    int ok = cpu_rd(buf + n - 1) == 0x22;
    report(inval_at_unmap ? "RX + speculative fill, invalidate at unmap (Linux way)" :
                            "RX + speculative fill, maintenance only at map", ok, inval_at_unmap);
}

/* 4. RX buffer shares a cache line with a CPU-owned variable (misaligned kmalloc-style buffer) */
static void rx_shared_line(int aligned)
{
    reset();
    unsigned flag = 0x400;                       /* CPU variable at the start of a line */
    unsigned buf  = aligned ? 0x440 : 0x408;     /* DMA buffer: own line vs. same line as flag */
    unsigned n = 48;
    clean(buf, n);                               /* map(FROM_DEVICE): arm64 cleans */
    cpu_wr(flag, 0x77);                          /* CPU touches its variable while DMA is in flight */
    dma_write(buf, 0x33, n);                     /* device writes the buffer in DRAM */
    evict_all();                                 /* normal cache eviction writes the DIRTY line back */
    invalidate(buf, n);                          /* unmap(FROM_DEVICE) */
    int ok = cpu_rd(buf) == 0x33 && cpu_rd(buf + n - 1) == 0x33 && cpu_rd(flag) == 0x77;
    report(aligned ? "RX buffer cache-line aligned (own lines)" :
                     "RX buffer shares a line with a CPU variable", ok, aligned);
}

int main(void)
{
    printf("non-coherent DMA model: %u-byte lines, write-back cache, DMA sees DRAM only\n", LINE);
    tx(0);  tx(1);
    rx(0);  rx(1);
    rx_spec(0); rx_spec(1);
    rx_shared_line(0); rx_shared_line(1);
    printf(fails ? "SOME CASES DID NOT BEHAVE AS EXPECTED\n" : "ALL PASS (each bug reproduced, each fix works)\n");
    return fails != 0;
}
