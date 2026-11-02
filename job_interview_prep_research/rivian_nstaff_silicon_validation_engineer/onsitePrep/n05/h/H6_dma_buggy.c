/* H6 · THE PROBLEM CODE — 5 planted bugs. Build without -Werror (it warns).
 * main() runs it on the host model and shows two of the bugs observably. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "H6_sim.h"
#ifndef TARGET
#include <stdio.h>
#endif

#define CTRL_START   (1u << 0)
#define CTRL_IRQ_EN  (1u << 1)
#define ST_DONE0     (1u << 0)
#define ST_BUS_ERR   (1 << 31)

static bool dma_done, dma_err;

void dma0_isr(void) {
    uint32_t st = REG(DMA_STATUS);
    if (st & ST_BUS_ERR) dma_err = true;
    if (st & ST_DONE0)   dma_done = true;
    REG(DMA_STATUS) |= ST_DONE0 | ST_BUS_ERR;         /* ack */
}

int dma0_copy(void *dst, const void *src, size_t nbytes) {
    dma_done = dma_err = false;
    REG(DMA_SRC)  = virt_to_bus(src);
    REG(DMA_DST)  = virt_to_bus(dst);
    REG(DMA_LEN)  = nbytes;
    REG(DMA_CTRL) = CTRL_START | CTRL_IRQ_EN;
    while (!dma_done) { cpu_relax(); }
    return dma_err ? -1 : 0;
}

int send_frame(uint8_t *tx_buf, const uint8_t frame[FRAME_LEN]) {
    return dma0_copy(tx_buf, frame, sizeof(frame));
}

#ifndef TARGET
#include <string.h>
int main(int argc, char **argv) {
    sim_reset();
    if (argc > 1 && strcmp(argv[1], "hang") == 0) {
        sim_never_complete = 1;           /* DMA never finishes (clock gated, bad addr...) */
        puts("hang mode: DMA never completes ...");
        fflush(stdout);
        dma0_copy(&sim_sram[512], &sim_sram[0], 4);
        puts("returned (never printed)");
        return 0;
    }
    uint8_t *frame = &sim_sram[0], *tx = &sim_sram[512];
    for (unsigned i = 0; i < FRAME_LEN; i++) frame[i] = (uint8_t)(i + 1);
    sim_hw_status = 1u << 1;              /* channel 1 DONE is already pending */
    int rc = send_frame(tx, frame);
    printf("rc=%d  LEN programmed=%u (want %u)  tx[63]=%u (want 64)\n",
           rc, (unsigned)sim_last_len, FRAME_LEN, tx[63]);
    printf("channel-1 DONE still pending after ch0 ISR? %s\n",
           (sim_hw_status & (1u << 1)) ? "yes" : "NO - lost");
    return 0;
}
#endif
