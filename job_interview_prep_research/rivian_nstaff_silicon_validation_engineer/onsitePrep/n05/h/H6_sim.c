/* H6 · host model of a 4-channel DMA block with a shared W1C STATUS register.
 * cpu_relax() is the "clock": after 3 calls a started transfer completes,
 * the model copies the bytes, sets DONE0 (+BUS_ERR if asked) and calls the ISR.
 * W1C emulation: the ISR's write to STATUS is treated as "clear these 1s". */
#include <string.h>
#include "H6_sim.h"

volatile uint32_t sim_regs[8];
uint8_t  sim_sram[1024];
int      sim_never_complete, sim_bus_err;
uint32_t sim_hw_status, sim_last_len;
static int countdown = -1;

void sim_reset(void) {
    memset((void *)sim_regs, 0, sizeof sim_regs);
    sim_never_complete = sim_bus_err = 0;
    sim_hw_status = sim_last_len = 0;
    countdown = -1;
}

uint32_t virt_to_bus(const void *p) { return (uint32_t)((const uint8_t *)p - sim_sram); }

void cpu_relax(void) {
    uint32_t ctrl = sim_regs[DMA_CTRL / 4u];
    if ((ctrl & 1u) && countdown < 0) countdown = 3;       /* START seen   */
    if (countdown < 0 || sim_never_complete || --countdown > 0) return;
    countdown = -1;
    sim_regs[DMA_CTRL / 4u] = ctrl & ~1u;                  /* START self-clears */
    sim_last_len = sim_regs[DMA_LEN / 4u];
    if (sim_last_len <= sizeof sim_sram)
        memcpy(&sim_sram[sim_regs[DMA_DST / 4u]], &sim_sram[sim_regs[DMA_SRC / 4u]], sim_last_len);
    sim_hw_status |= 1u | (sim_bus_err ? 0x80000000u : 0u);
    if (ctrl & 2u) {                                       /* IRQ_EN -> "interrupt" */
        sim_regs[DMA_STATUS / 4u] = sim_hw_status;         /* what a read returns */
        dma0_isr();
        sim_hw_status &= ~sim_regs[DMA_STATUS / 4u];       /* W1C: 1s written clear */
        sim_regs[DMA_STATUS / 4u] = sim_hw_status;
    }
}
