/* C4 - 64-bit free-running counter over two 32-bit registers: latch vs hi-lo-hi, ticks->ns. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ---- HW model (test only): counter advances `m_step` ticks on every bus read.
 * latch mode: reading LO copies the live HI into a shadow; reading HI returns the shadow. */
#define CNT_LO 0x00u
#define CNT_HI 0x04u
static uint64_t m_cnt, m_step;
static uint32_t m_shadow_hi, m_reads;
static bool m_latch;
uint32_t reg_read(uint32_t off)
{
    uint32_t v;
    if (off == CNT_LO) { v = (uint32_t)m_cnt; m_shadow_hi = (uint32_t)(m_cnt >> 32); }
    else               { v = m_latch ? m_shadow_hi : (uint32_t)(m_cnt >> 32); }
    m_cnt += m_step; m_reads++;
    return v;
}
static void model_reset(uint64_t cnt, uint64_t step, bool latch)
{
    m_cnt = cnt; m_step = step; m_latch = latch; m_shadow_hi = 0; m_reads = 0;
}

/* ==== ANSWER BEGIN ==== */
uint32_t reg_read(uint32_t off);

/* (a) HW latches HI when LO is read: LO first, then HI. Order is the whole trick. */
uint64_t cnt_read_latched(void)
{
    uint32_t lo = reg_read(CNT_LO);          /* snapshot: HI copied to shadow */
    uint32_t hi = reg_read(CNT_HI);          /* shadow, consistent with lo    */
    return ((uint64_t)hi << 32) | lo;
}

/* (b) No latch: hi-lo-hi, retry if LO wrapped in between (at most 1 retry). */
uint64_t cnt_read_hlh(void)
{
    uint32_t hi, lo, hi2;
    do {
        hi  = reg_read(CNT_HI);
        lo  = reg_read(CNT_LO);
        hi2 = reg_read(CNT_HI);
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}

/* Overflow-safe ticks -> ns: split into whole seconds + remainder. */
uint64_t ticks_to_ns(uint64_t ticks, uint32_t hz)
{
    const uint64_t NS = 1000000000ull;
    return (ticks / hz) * NS + (ticks % hz) * NS / hz;  /* rem < hz <= 2^32: no overflow */
}

/* 64-bit unsigned subtraction is wrap-safe too (wrap = 584 years @1 GHz). */
uint64_t elapsed_ns(uint64_t start, uint64_t end, uint32_t hz) { return ticks_to_ns(end - start, hz); }
/* ==== ANSWER END ==== */

static uint64_t naive_hi_then_lo(void)
{
    uint32_t hi = reg_read(CNT_HI);
    uint32_t lo = reg_read(CNT_LO);
    return ((uint64_t)hi << 32) | lo;
}

int main(void)
{
    /* torn read demo: live HI, counter at 0x1_FFFF_FFFF, +1 per read */
    model_reset(0x1FFFFFFFFull, 1, false);
    assert(naive_hi_then_lo() == 0x100000000ull);        /* 2^32 ticks in the past! */

    /* hi-lo-hi at the same spot: first try hi=1,lo=0,hi2=2 -> retry -> hi=2,lo=3,hi2=2 */
    model_reset(0x1FFFFFFFFull, 1, false);
    assert(cnt_read_hlh() == 0x200000003ull);
    assert(m_reads == 6);                                 /* exactly one retry */
    model_reset(0x123456789ull, 1, false);                /* no wrap: 3 reads */
    assert(cnt_read_hlh() == 0x123456789ull + 1 && m_reads == 3);

    /* latched HW: LO then HI is consistent even across the wrap */
    model_reset(0x1FFFFFFFFull, 1, true);
    assert(cnt_read_latched() == 0x1FFFFFFFFull);
    /* wrong order on latched HW: HI returns a stale shadow (0 here) */
    model_reset(0x1FFFFFFFFull, 1, true);
    assert(naive_hi_then_lo() == 0x00000000ull);          /* shadow=0, lo wrapped to 0 */

    /* ticks_to_ns */
    assert(ticks_to_ns(0, 24000000u) == 0);
    assert(ticks_to_ns(24000000u, 24000000u) == 1000000000ull);   /* 1 s @24 MHz */
    assert(ticks_to_ns(1, 24000000u) == 41);                      /* 41.67 ns truncated */
    assert(ticks_to_ns(UINT64_MAX, 1000000000u) == UINT64_MAX);   /* 1 GHz: identity, no overflow */
    /* naive ticks*1e9/hz would overflow here: 2^40 ticks * 1e9 > 2^64 */
    assert(ticks_to_ns(1ull << 40, 24000000u) == 45812984490666ull);

    /* elapsed across a 32-bit LO wrap is just 64-bit subtraction */
    assert(elapsed_ns(0xFFFFFFF0ull, 0x100000010ull, 1000000000u) == 32);

    printf("C4 PASS\n");
    return 0;
}
