/* fake_mem.h -- test harness only (NOT part of any model answer).
 * A 256-word "SRAM" behind two accessors with injectable faults:
 *   data-bus : stuck-high / stuck-low lines, wired-OR / wired-AND shorts (all addresses)
 *   address  : stuck-high / stuck-low lines, wired-OR / wired-AND shorts (aliasing)
 *   one cell : stuck bits in a single word
 * mem_wr/mem_rd take a WORD offset from the base of the region under test.
 */
#ifndef FAKE_MEM_H
#define FAKE_MEM_H
#include <stdint.h>
#include <string.h>

#define MEM_WORDS 256u

static uint32_t cells[MEM_WORDS];
static struct {
    uint32_t d_hi, d_lo;      /* data lines stuck-high / stuck-low            */
    uint32_t d_or, d_and;     /* 2-bit masks: data lines shorted (OR / AND)   */
    uint32_t a_hi, a_lo;      /* address lines stuck-high / stuck-low          */
    uint32_t a_or, a_and;     /* 2-bit masks: address lines shorted (OR / AND) */
    int      cell;            /* -1 = none; else word index with stuck bits    */
    uint32_t c_hi, c_lo;
    unsigned rd, wr;          /* access counters                               */
} F;

static inline void fake_reset(void)
{
    memset(&F, 0, sizeof F);
    F.cell = -1;
    for (unsigned i = 0; i < MEM_WORDS; i++) cells[i] = 0xA5A5A5A5u;  /* power-up garbage */
}

static inline uint32_t short_or(uint32_t v, uint32_t m)  { return (m && (v & m)) ? (v | m) : v; }
static inline uint32_t short_and(uint32_t v, uint32_t m) { return (m && (v & m) != m) ? (v & ~m) : v; }

static inline uint32_t fake_map(uint32_t a)
{
    a = short_and(short_or(a, F.a_or), F.a_and);
    a = (a | F.a_hi) & ~F.a_lo;
    return a % MEM_WORDS;
}

static inline uint32_t fake_bus(uint32_t v)
{
    v = short_and(short_or(v, F.d_or), F.d_and);
    return (v | F.d_hi) & ~F.d_lo;
}

static inline void mem_wr(uint32_t off, uint32_t v)
{
    F.wr++;
    cells[fake_map(off)] = fake_bus(v);
}

static inline uint32_t mem_rd(uint32_t off)
{
    F.rd++;
    uint32_t i = fake_map(off), v = cells[i];
    if ((int)i == F.cell) v = (v | F.c_hi) & ~F.c_lo;
    return fake_bus(v);
}
#endif
