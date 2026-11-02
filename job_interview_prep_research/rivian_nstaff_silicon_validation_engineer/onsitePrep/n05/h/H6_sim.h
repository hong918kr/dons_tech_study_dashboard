/* H6 · register access layer. TARGET = real MCU; default = host simulation. */
#ifndef H6_SIM_H
#define H6_SIM_H
#include <stdint.h>

#define DMA_SRC      0x00u   /* bus address of source                     */
#define DMA_DST      0x04u   /* bus address of destination                */
#define DMA_LEN      0x08u   /* transfer length in BYTES                  */
#define DMA_CTRL     0x0Cu   /* bit0 START, bit1 IRQ_EN                   */
#define DMA_STATUS   0x10u   /* W1C. bit n = channel n DONE (n=0..3),     */
                             /*      bit 31 = BUS_ERR. Shared by all chans */
#define FRAME_LEN    64u

#ifdef TARGET
#define REG(off)     (*(volatile uint32_t *)(0x40020000u + (off)))
#ifndef cpu_relax                      /* -D"cpu_relax()=((void)0)" = busy-poll build */
#define cpu_relax()  __asm__ volatile("wfi")
#endif
static inline uint32_t virt_to_bus(const void *p) { return (uint32_t)(uintptr_t)p; }
#else
extern volatile uint32_t sim_regs[8];
#define REG(off)     (sim_regs[(off) / 4u])
void     cpu_relax(void);               /* host: advances the DMA model   */
uint32_t virt_to_bus(const void *p);    /* host: offset into sim_sram[]   */
/* knobs for tests */
extern uint8_t  sim_sram[1024];
extern int      sim_never_complete, sim_bus_err;
extern uint32_t sim_hw_status, sim_last_len;
void sim_reset(void);
#endif
void dma0_isr(void);
#endif
