/* C5 - ISR: read status, dispatch a handler per set bit (lowest first), W1C ack. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ---- HW model (test only): INT_STATUS (W1C) at 0x04, INT_EN (RW) at 0x08 ---- */
#define STAT_OFF 0x04u
#define EN_OFF   0x08u
static uint32_t m_stat, m_en, m_writes;
uint32_t reg_read(uint32_t off) { return off == STAT_OFF ? m_stat : m_en; }
void reg_write(uint32_t off, uint32_t val)
{
    if (off == STAT_OFF) m_stat &= ~val; else m_en = val;
    m_writes++;
}

/* ==== ANSWER BEGIN ==== */
uint32_t reg_read(uint32_t off);
void     reg_write(uint32_t off, uint32_t val);

typedef void (*irq_fn_t)(unsigned bit, void *ctx);
static struct { irq_fn_t fn; void *ctx; } irq_tbl[32];
static uint32_t spurious;                       /* debug counter */

int irq_register(unsigned bit, irq_fn_t fn, void *ctx)
{
    if (bit >= 32u || fn == NULL)
        return -1;
    irq_tbl[bit].fn  = fn;
    irq_tbl[bit].ctx = ctx;
    return 0;
}

/* Returns number of handlers called. */
unsigned irq_dispatch(void)
{
    uint32_t pending = reg_read(STAT_OFF) & reg_read(EN_OFF);  /* one snapshot */
    unsigned n = 0;
    if (pending == 0)
        return 0;
    reg_write(STAT_OFF, pending);              /* W1C: ack only what we took, before handling */
    while (pending) {
        unsigned bit = (unsigned)__builtin_ctz(pending);       /* lowest set bit = priority */
        pending &= pending - 1u;                               /* clear lowest set bit */
        if (irq_tbl[bit].fn) {
            irq_tbl[bit].fn(bit, irq_tbl[bit].ctx);
            n++;
        } else {                                /* no handler: mask it, or it storms forever */
            reg_write(EN_OFF, reg_read(EN_OFF) & ~(1u << bit));
            spurious++;
        }
    }
    return n;
}
/* ==== ANSWER END ==== */

static unsigned order[32], norder;
static void rec(unsigned bit, void *ctx) { order[norder++] = bit; (*(int *)ctx)++; }
static void rec_and_refire(unsigned bit, void *ctx)          /* event fires again mid-handler */
{
    rec(bit, ctx);
    m_stat |= 1u << bit;
}
static void reset_all(void) { memset(irq_tbl, 0, sizeof irq_tbl); spurious = 0; norder = 0; m_writes = 0; }

int main(void)
{
    int cnt = 0;
    reset_all();
    assert(irq_register(32, rec, &cnt) == -1 && irq_register(0, NULL, NULL) == -1);
    assert(irq_register(0, rec, &cnt) == 0 && irq_register(3, rec, &cnt) == 0);
    assert(irq_register(31, rec, &cnt) == 0);

    /* bits 31,3,0 pending + enabled; bit 4 pending but disabled */
    m_stat = 0x80000019u; m_en = 0x80000009u;
    assert(irq_dispatch() == 3);
    assert(norder == 3 && order[0] == 0 && order[1] == 3 && order[2] == 31);  /* low bit first */
    assert(cnt == 3);
    assert(m_stat == 0x10u);                    /* disabled bit 4 still pending, not acked */

    /* unregistered bit 7 enabled+pending: acked, masked, counted */
    m_stat = 1u << 7; m_en = (1u << 7) | 1u;
    assert(irq_dispatch() == 0);
    assert(spurious == 1 && m_en == 1u && m_stat == 0);

    /* nothing pending: no bus writes */
    m_writes = 0; m_stat = 0; m_en = ~0u;
    assert(irq_dispatch() == 0 && m_writes == 0);

    /* event re-fires during its handler: because we acked first, it is not lost */
    reset_all(); cnt = 0;
    assert(irq_register(2, rec_and_refire, &cnt) == 0);
    m_stat = 1u << 2; m_en = 1u << 2;
    assert(irq_dispatch() == 1 && m_stat == (1u << 2));   /* still pending -> next IRQ */

    printf("C5 PASS\n");
    return 0;
}
