/* H6 · MODEL ANSWER — the 5 bugs fixed (diff vs H6_dma_buggy.c, marked FIX n). */
#ifndef TARGET
#include <assert.h>
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "H6_sim.h"
#ifndef TARGET
#include <stdio.h>
#endif

#define CTRL_START    (1u << 0)
#define CTRL_IRQ_EN   (1u << 1)
#define ST_DONE0      (1u << 0)
#define ST_BUS_ERR    (1u << 31)                    /* FIX 2: unsigned shift   */
#define DMA_TIMEOUT   100000u                       /* spins; derive from max len */

static volatile bool dma_done, dma_err;             /* FIX 1: ISR-shared       */

void dma0_isr(void) {
    uint32_t st   = REG(DMA_STATUS);
    uint32_t mine = st & (ST_DONE0 | ST_BUS_ERR);
    if (mine & ST_BUS_ERR) dma_err = true;
    if (mine & ST_DONE0)   dma_done = true;
    REG(DMA_STATUS) = mine;                         /* FIX 3: W1C = plain write of my bits */
}

int dma0_copy(void *dst, const void *src, size_t nbytes) {
    dma_done = dma_err = false;
    REG(DMA_SRC)  = virt_to_bus(src);
    REG(DMA_DST)  = virt_to_bus(dst);
    REG(DMA_LEN)  = (uint32_t)nbytes;
    REG(DMA_CTRL) = CTRL_START | CTRL_IRQ_EN;
    for (uint32_t spins = 0; !dma_done; spins++) {  /* FIX 4: bounded wait     */
        if (spins >= DMA_TIMEOUT) {
            REG(DMA_CTRL) = 0;                      /* abort the channel       */
            return -2;                              /* timeout                 */
        }
        cpu_relax();
    }
    return dma_err ? -1 : 0;
}

int send_frame(uint8_t *tx_buf, const uint8_t *frame, size_t len) {
    return dma0_copy(tx_buf, frame, len);           /* FIX 5: caller passes length */
}

#ifndef TARGET
int main(void) {
    uint8_t *frame = &sim_sram[0], *tx = &sim_sram[512];

    sim_reset();                                    /* normal frame + ch1 pending */
    for (unsigned i = 0; i < FRAME_LEN; i++) frame[i] = (uint8_t)(i + 1);
    sim_hw_status = 1u << 1;
    assert(send_frame(tx, frame, FRAME_LEN) == 0);
    assert(sim_last_len == FRAME_LEN && tx[0] == 1 && tx[63] == 64);
    assert(sim_hw_status == (1u << 1));             /* ch1 DONE survived the ack */

    sim_reset(); sim_bus_err = 1;                   /* bus error reported      */
    assert(send_frame(tx, frame, 16) == -1);
    assert(sim_hw_status == 0);                     /* both of my bits acked   */

    sim_reset(); sim_never_complete = 1;            /* hardware never finishes */
    assert(send_frame(tx, frame, 16) == -2);
    assert((sim_regs[DMA_CTRL / 4u] & CTRL_START) == 0);

    sim_reset();                                    /* minimal 1-byte transfer */
    frame[0] = 0xA5; tx[0] = 0;
    assert(send_frame(tx, frame, 1) == 0 && tx[0] == 0xA5 && sim_last_len == 1);

    printf("normal/bus-err/timeout/1-byte OK, ch1 pending preserved\n");
    puts("H6 PASS");
    return 0;
}
#endif
