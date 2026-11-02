/* C2 - RMW helper + W1C interrupt-status acknowledge. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ---- HW model (test only) ----
 * 0x00 CTRL       RW
 * 0x04 INT_STATUS W1C: HW sets bits; writing 1 clears that bit, writing 0 does nothing
 * 0x08 INT_EN     RW
 * Optional "late event": bits OR-ed into INT_STATUS right after the Nth bus access,
 * i.e. an interrupt that fires while the ISR is running.                          */
#define CTRL_OFF   0x00u
#define STAT_OFF   0x04u
#define EN_OFF     0x08u
static uint32_t m_ctrl, m_stat, m_en, m_reads, m_writes, m_late_bits, m_late_at;
static void model_tick(void)
{
    if (m_late_bits && m_reads + m_writes == m_late_at) { m_stat |= m_late_bits; m_late_bits = 0; }
}
uint32_t reg_read(uint32_t off)
{
    uint32_t v = (off == CTRL_OFF) ? m_ctrl : (off == STAT_OFF) ? m_stat : m_en;
    m_reads++; model_tick();
    return v;
}
void reg_write(uint32_t off, uint32_t val)
{
    if (off == CTRL_OFF) m_ctrl = val;
    else if (off == STAT_OFF) m_stat &= ~val;     /* W1C */
    else m_en = val;
    m_writes++; model_tick();
}
static void model_reset(uint32_t ctrl, uint32_t stat, uint32_t en)
{
    m_ctrl = ctrl; m_stat = stat; m_en = en; m_reads = m_writes = 0; m_late_bits = 0; m_late_at = 0;
}

/* ==== ANSWER BEGIN ==== */
uint32_t reg_read(uint32_t off);
void     reg_write(uint32_t off, uint32_t val);

/* RMW for normal RW registers: exactly one read, one write. Not atomic vs ISRs. */
void reg_rmw(uint32_t off, uint32_t clr, uint32_t set)
{
    uint32_t v = reg_read(off);
    v = (v & ~clr) | set;
    reg_write(off, v);
}

/* W1C ack: write ONLY the bits to clear. 0-bits are no-ops, so no read needed. */
void irq_ack(uint32_t bits) { reg_write(STAT_OFF, bits); }

/* Snapshot enabled pending bits, ack exactly those, return them for handling.
 * Ack BEFORE handling: an event that re-fires during handling sets the bit again. */
uint32_t irq_take_pending(void)
{
    uint32_t pending = reg_read(STAT_OFF) & reg_read(EN_OFF);
    if (pending)
        irq_ack(pending);
    return pending;
}
/* ==== ANSWER END ==== */

int main(void)
{
    /* RMW on RW register: change field [7:4] to 0x3, keep others */
    model_reset(0xFFFF00A5u, 0, 0);
    reg_rmw(CTRL_OFF, 0xF0u, 0x30u);
    assert(m_ctrl == 0xFFFF0035u && m_reads == 1 && m_writes == 1);
    reg_rmw(CTRL_OFF, 0, 0);                     /* no-op RMW still 1R+1W, value kept */
    assert(m_ctrl == 0xFFFF0035u);

    /* THE BUG: RMW on W1C to "clear bit 0" clears every pending bit */
    model_reset(0, 0x15u, 0xFFu);                /* bits 0,2,4 pending */
    reg_rmw(STAT_OFF, 0, 1u << 0);               /* writes back 0x15 -> all cleared */
    assert(m_stat == 0);
    /* correct: write only bit 0 */
    model_reset(0, 0x15u, 0xFFu);
    irq_ack(1u << 0);
    assert(m_stat == 0x14u && m_reads == 0);

    /* take_pending: masked bits stay pending, enabled ones acked exactly */
    model_reset(0, 0x15u, 0x05u);                /* bit 4 pending but disabled */
    assert(irq_take_pending() == 0x05u);
    assert(m_stat == 0x10u);

    /* late event (bit 5) arrives after the two reads: must survive our ack */
    model_reset(0, 0x01u, 0xFFu);
    m_late_bits = 1u << 5; m_late_at = 2;
    assert(irq_take_pending() == 0x01u);
    assert(m_stat == (1u << 5));                 /* next ISR will see it */
    assert(irq_take_pending() == (1u << 5) && m_stat == 0);

    /* nothing pending: no write at all */
    model_reset(0, 0, 0xFFu);
    assert(irq_take_pending() == 0 && m_writes == 0);

    /* bit 31 */
    model_reset(0, 0x80000000u, 0x80000000u);
    assert(irq_take_pending() == 0x80000000u && m_stat == 0);

    printf("C2 PASS\n");
    return 0;
}
