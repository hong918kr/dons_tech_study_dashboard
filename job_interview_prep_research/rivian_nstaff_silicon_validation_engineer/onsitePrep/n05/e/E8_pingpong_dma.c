/* E8 - ping-pong (double) DMA buffer handoff + overrun detection.
 * DMA is simulated: the test writes into buf[dma_idx] then calls the "transfer complete" ISR. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>

/* ==== MODEL ANSWER BEGIN ==== */
#define PP_LEN 4u
enum { OWN_DMA = 0, OWN_CPU = 1 };

typedef struct {
    uint16_t          buf[2][PP_LEN];
    volatile uint8_t  owner[2];   /* who may touch buf[i] right now */
    volatile uint8_t  dma_idx;    /* buffer the DMA is filling */
    volatile uint32_t overrun;    /* DMA finished but the CPU still held the other buffer */
} pingpong_t;

void pp_init(pingpong_t *p)
{
    p->owner[0] = p->owner[1] = OWN_DMA;
    p->dma_idx = 0;
    p->overrun = 0;
}

void pp_dma_done_isr(pingpong_t *p)             /* DMA transfer-complete IRQ */
{
    uint8_t done = p->dma_idx, nxt = done ^ 1u;
    if (p->owner[nxt] == OWN_CPU) {             /* CPU is late: nowhere to put next block */
        p->overrun++;                           /* policy: re-fill `done`, its data is dropped */
        return;                                 /* dma_idx unchanged (re-arm same buffer) */
    }
    p->owner[done] = OWN_CPU;                   /* hand the full buffer to the CPU */
    p->dma_idx = nxt;                           /* hw: program DMA address = buf[nxt] */
}

int pp_acquire(const pingpong_t *p)             /* main: ready buffer index, or -1 */
{
    int i = p->dma_idx ^ 1u;                    /* ready one is never the DMA target */
    return (p->owner[i] == OWN_CPU) ? i : -1;   /* M7: invalidate D-cache for buf[i] here */
}

void pp_release(pingpong_t *p, int i)           /* main: done processing buf[i] */
{
    p->owner[i] = OWN_DMA;                      /* last access: give it back */
}
/* ==== MODEL ANSWER END ==== */

static uint16_t g_seq;
static void dma_fill(pingpong_t *p)              /* simulated DMA writes a frame */
{
    for (uint32_t k = 0; k < PP_LEN; k++) p->buf[p->dma_idx][k] = (uint16_t)(g_seq * 16u + k);
    g_seq++;
    pp_dma_done_isr(p);
}

int main(void)
{
    pingpong_t p; pp_init(&p);
    assert(pp_acquire(&p) == -1);               /* nothing ready */
    dma_fill(&p);                               /* frame 0 -> buf0 */
    int i = pp_acquire(&p);
    assert(i == 0 && p.dma_idx == 1 && p.buf[0][0] == 0 && p.buf[0][3] == 3);
    dma_fill(&p);                               /* frame 1 -> buf1, CPU still holds buf0 */
    assert(p.overrun == 1 && p.dma_idx == 1 && p.owner[1] == OWN_DMA);
    pp_release(&p, i);
    dma_fill(&p);                               /* frame 2 -> buf1 (re-armed) */
    i = pp_acquire(&p);
    assert(i == 1 && p.buf[1][0] == 2 * 16 && p.overrun == 1);   /* frame 1 lost, counted */
    pp_release(&p, i);
    assert(pp_acquire(&p) == -1);
    /* steady state: CPU keeps up -> every frame seen in order, alternating buffers */
    pp_init(&p); g_seq = 0;
    for (uint16_t f = 0; f < 1000; f++) {
        dma_fill(&p);
        i = pp_acquire(&p);
        assert(i == (f & 1) && p.buf[i][0] == (uint16_t)(f * 16u));
        pp_release(&p, i);
    }
    assert(p.overrun == 0);
    /* CPU stalls for 5 frames -> 5 overruns, then recovers */
    dma_fill(&p); i = pp_acquire(&p); assert(i >= 0);
    for (int k = 0; k < 5; k++) dma_fill(&p);
    assert(p.overrun == 5);
    pp_release(&p, i);
    dma_fill(&p); assert(pp_acquire(&p) >= 0);
    puts("E8 PASS");
    return 0;
}
