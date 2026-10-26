/*
 * V1  Memory Test Engine for SRAM/DDR Bring-up (fake memory + fault injection)
 *
 * You get a word-addressed memory behind two accessors (implemented at the bottom of
 * this file as a FAKE memory with injectable faults -- do not edit the fake):
 *
 *   void     mem_wr(uint32_t addr, uint32_t val);   addr = word index, 0 .. MEM_WORDS-1
 *   uint32_t mem_rd(uint32_t addr);
 *
 * Implement three tests. Each returns 0 = pass, -EIO = fault found (and fills *f with
 * the FIRST failure), -EINVAL = bad arguments (f == NULL, nwords == 0, range outside memory).
 *
 *   memtest_databus(addr, f)
 *       Walking-ones on ONE word: for bit = 0..31 write (1u << bit), read it back.
 *       On mismatch: f = { addr, expected, actual, element = bit }.
 *
 *   memtest_addrbus(base, nwords, bad_bit, f)
 *       Power-of-two offsets: base and base + (1u << k) for every (1u << k) < nwords.
 *       1) write PATTERN (0xAAAAAAAA) to all of them
 *       2) write ANTI (0x55555555) to base; every base+(1<<k) must still read PATTERN.
 *          If not, address bit k is stuck (that address aliases base).
 *       3) restore base; for each k write ANTI to base+(1<<k) and check base and every
 *          other offset j != k still read PATTERN (bit j shorted to bit k); then restore.
 *       On failure: *bad_bit = k (absolute address bit), f = { addr read, PATTERN,
 *       actual, element = k }. On pass *bad_bit = -1.
 *       Assume base is aligned to the next power of two >= nwords (tests use 0 and 64).
 *
 *   memtest_march_cminus(base, nwords, f)
 *       March C- over [base, base + nwords) with backgrounds 0x00000000 / 0xFFFFFFFF:
 *         M0 up(w0)  M1 up(r0,w1)  M2 up(r1,w0)  M3 down(r0,w1)  M4 down(r1,w0)  M5 up(r0)
 *       ("up" = ascending addresses, "down" = descending; M0/M5 are "either order", use up.)
 *       Stop at the first read mismatch: f = { addr, expected, actual, element = 0..5 }.
 *       Exactly 10*N accesses on a clean run (5N reads + 5N writes). Touch nothing
 *       outside the range. No malloc.
 *
 * Build & run:  ./run.sh V1
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* ---------------- memory accessors (provided) ---------------- */
#define MEM_WORDS 256u
void     mem_wr(uint32_t addr, uint32_t val);
uint32_t mem_rd(uint32_t addr);

#define PATTERN 0xAAAAAAAAu
#define ANTI    0x55555555u

typedef struct {
    uint32_t addr;       /* word address of the failing read */
    uint32_t expected;
    uint32_t actual;
    int      element;    /* march element 0..5 | bit index (databus) | address bit (addrbus) */
} MemFail;

int memtest_databus(uint32_t addr, MemFail *f)
{
    /* TODO: walking-ones on one word */
    (void)addr; (void)f;
    return 0;
}

int memtest_addrbus(uint32_t base, uint32_t nwords, int *bad_bit, MemFail *f)
{
    /* TODO: power-of-two address test; report the faulty address bit */
    (void)base; (void)nwords; (void)bad_bit; (void)f;
    return 0;
}

int memtest_march_cminus(uint32_t base, uint32_t nwords, MemFail *f)
{
    /* TODO: M0 up(w0) M1 up(r0,w1) M2 up(r1,w0) M3 down(r0,w1) M4 down(r1,w0) M5 up(r0) */
    (void)base; (void)nwords; (void)f;
    return 0;
}

/* =====================================================================
 * Test harness: fake memory with fault injection (do not edit)
 * ===================================================================== */
static struct {
    uint32_t cell[MEM_WORDS];     /* physical storage */
    uint32_t rd, wr, oob;         /* access counters; oob = out-of-range accesses */
    /* stuck-at: bits in saf_mask of word saf_addr always read as saf_val bits */
    int32_t  saf_addr; uint32_t saf_mask, saf_val;
    /* coupling: a write that raises any bit (0->1) of word cf_aggr inverts cf_mask in cf_victim */
    int32_t  cf_aggr, cf_victim; uint32_t cf_mask;
    /* address line: bit addr_bit of every address is forced to addr_val (0 or 1) */
    int32_t  addr_bit; uint32_t addr_val;
} F;

static void fake_reset(void)
{
    memset(&F, 0, sizeof F);
    for (uint32_t i = 0; i < MEM_WORDS; i++) F.cell[i] = 0xA5A5A5A5u;   /* power-up garbage */
    F.saf_addr = F.cf_aggr = F.cf_victim = F.addr_bit = -1;
}

static uint32_t phys(uint32_t addr)
{
    if (F.addr_bit >= 0) {
        uint32_t m = 1u << F.addr_bit;
        addr = F.addr_val ? (addr | m) : (addr & ~m);
    }
    return addr;
}

static uint32_t apply_saf(uint32_t p, uint32_t v)
{
    if ((int32_t)p == F.saf_addr) v = (v & ~F.saf_mask) | (F.saf_val & F.saf_mask);
    return v;
}

void mem_wr(uint32_t addr, uint32_t val)
{
    F.wr++;
    if (addr >= MEM_WORDS) { F.oob++; return; }
    uint32_t p = phys(addr);
    uint32_t old = F.cell[p];
    F.cell[p] = apply_saf(p, val);
    if ((int32_t)p == F.cf_aggr && (~old & F.cell[p]) != 0)
        F.cell[F.cf_victim] = apply_saf((uint32_t)F.cf_victim, F.cell[F.cf_victim] ^ F.cf_mask);
}

uint32_t mem_rd(uint32_t addr)
{
    F.rd++;
    if (addr >= MEM_WORDS) { F.oob++; return 0; }
    uint32_t p = phys(addr);
    return apply_saf(p, F.cell[p]);
}

int main(void)
{
    MemFail f = { 0, 0, 0, 0 };
    int bb = 0;

    /* 1. clean memory: all three tests pass; March C- is exactly 10N accesses */
    fake_reset();
    CHECK(memtest_databus(17, &f) == 0);
    CHECK(memtest_addrbus(0, 256, &bb, &f) == 0 && bb == -1);
    fake_reset();
    CHECK(memtest_march_cminus(0, 64, &f) == 0);
    CHECK(F.rd == 5u * 64 && F.wr == 5u * 64 && F.oob == 0);

    /* 2. data bus: bit 5 stuck-at-0 at word 17 -> walking-1 fails at bit 5 */
    fake_reset(); F.saf_addr = 17; F.saf_mask = 1u << 5; F.saf_val = 0;
    CHECK(memtest_databus(17, &f) == -EIO);
    CHECK(f.addr == 17 && f.expected == 0x20u && f.actual == 0 && f.element == 5);
    /*    bit 3 stuck-at-1 -> fails on the very first pattern (bit 0) */
    fake_reset(); F.saf_addr = 17; F.saf_mask = 1u << 3; F.saf_val = 0xFFFFFFFFu;
    CHECK(memtest_databus(17, &f) == -EIO);
    CHECK(f.expected == 0x1u && f.actual == 0x9u && f.element == 0);

    /* 3. address bus: bit 3 stuck-at-0 (8 aliases 0) and bit 2 stuck-at-1 (0 aliases 4) */
    fake_reset(); F.addr_bit = 3; F.addr_val = 0;
    CHECK(memtest_addrbus(0, 256, &bb, &f) == -EIO);
    CHECK(bb == 3 && f.addr == 8 && f.expected == PATTERN && f.actual == ANTI && f.element == 3);
    fake_reset(); F.addr_bit = 2; F.addr_val = 1;
    CHECK(memtest_addrbus(0, 32, &bb, &f) == -EIO && bb == 2 && f.addr == 4);
    /*    non-zero base: bit 4 stuck-at-0 inside [64, 128) -> 80 aliases 64 */
    fake_reset(); F.addr_bit = 4; F.addr_val = 0;
    CHECK(memtest_addrbus(64, 64, &bb, &f) == -EIO && bb == 4 && f.addr == 80);

    /* 4. March C-: stuck-at faults */
    fake_reset(); F.saf_addr = 10; F.saf_mask = 1u << 7; F.saf_val = 0xFFFFFFFFu;   /* SA1 */
    CHECK(memtest_march_cminus(0, 32, &f) == -EIO);
    CHECK(f.addr == 10 && f.expected == 0 && f.actual == 0x80u && f.element == 1);
    fake_reset(); F.saf_addr = 20; F.saf_mask = 1u << 0; F.saf_val = 0;             /* SA0 */
    CHECK(memtest_march_cminus(0, 32, &f) == -EIO);
    CHECK(f.addr == 20 && f.expected == 0xFFFFFFFFu && f.actual == 0xFFFFFFFEu && f.element == 2);

    /* 5. March C-: coupling fault, aggressor below and above the victim */
    fake_reset(); F.cf_aggr = 5; F.cf_victim = 12; F.cf_mask = 1u << 3;
    CHECK(memtest_march_cminus(0, 32, &f) == -EIO);
    CHECK(f.addr == 12 && f.expected == 0 && f.actual == 0x8u && f.element == 1);
    fake_reset(); F.cf_aggr = 20; F.cf_victim = 7; F.cf_mask = 1u << 3;
    CHECK(memtest_march_cminus(0, 32, &f) == -EIO);
    CHECK(f.addr == 7 && f.expected == 0xFFFFFFFFu && f.actual == 0xFFFFFFF7u && f.element == 2);

    /* 6. March C- also catches the address-line fault (8 aliases 0) */
    fake_reset(); F.addr_bit = 3; F.addr_val = 0;
    CHECK(memtest_march_cminus(0, 32, &f) == -EIO);
    CHECK(f.addr == 8 && f.expected == 0 && f.actual == 0xFFFFFFFFu && f.element == 1);

    /* 7. range length 1, and range boundaries */
    fake_reset();
    CHECK(memtest_march_cminus(5, 1, &f) == 0 && F.rd == 5 && F.wr == 5);
    fake_reset(); F.saf_addr = 5; F.saf_mask = 1u; F.saf_val = 1u;
    CHECK(memtest_march_cminus(5, 1, &f) == -EIO && f.addr == 5 && f.element == 1);
    fake_reset(); F.saf_addr = 255; F.saf_mask = 1u << 31; F.saf_val = 0;           /* last word */
    CHECK(memtest_march_cminus(192, 64, &f) == -EIO && f.addr == 255 && f.element == 2);
    fake_reset(); F.saf_addr = 64; F.saf_mask = 1u; F.saf_val = 1u;                 /* just outside */
    CHECK(memtest_march_cminus(0, 64, &f) == 0);
    CHECK(F.cell[64] == 0xA5A5A5A5u && F.cell[65] == 0xA5A5A5A5u && F.oob == 0);    /* untouched */

    /* 8. bad arguments */
    CHECK(memtest_march_cminus(0, 0, &f) == -EINVAL);
    CHECK(memtest_march_cminus(200, 57, &f) == -EINVAL);
    CHECK(memtest_march_cminus(0, 8, NULL) == -EINVAL);
    CHECK(memtest_addrbus(0, 0, &bb, &f) == -EINVAL);
    CHECK(memtest_databus(MEM_WORDS, &f) == -EINVAL);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
