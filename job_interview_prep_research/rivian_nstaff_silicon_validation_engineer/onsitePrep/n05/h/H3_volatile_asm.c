/* H3 · volatile — compile with:  clang -O2 -S H3_volatile_asm.c  and read the asm.
 * Not a runnable program (no main); run.sh greps the interesting functions. */
#include <stdint.h>

int flag;                       /* set by an ISR (not volatile!)   */
volatile int vflag;             /* same, done right                */
extern void wfi_stub(void);     /* opaque call (e.g. __WFI wrapper) */

/* L1: spin on ISR flag */
void L1_wait_flag(void)        { while (!flag) { } }
void L1_wait_vflag(void)       { while (!vflag) { } }

/* L2: poll a status register through a plain pointer */
void L2_poll(uint32_t *st)               { while ((*st & 1u) == 0) { } }
void L2_poll_v(volatile uint32_t *st)    { while ((*st & 1u) == 0) { } }

/* L3: busy-wait delay loop */
void L3_delay(void)            { for (uint32_t i = 0; i < 1000000u; i++) { } }
void L3_delay_v(void)          { for (volatile uint32_t i = 0; i < 1000000u; i++) { } }

/* L4: NOT broken by the compiler: the opaque call forces a reload of flag */
void L4_wait_call(void)        { while (!flag) { wfi_stub(); } }

/* X1 (bonus, not a loop): push two words into a TX FIFO register */
void X1_tx2(uint32_t *fifo)              { *fifo = 0xA; *fifo = 0xB; }
void X1_tx2_v(volatile uint32_t *fifo)   { *fifo = 0xA; *fifo = 0xB; }

/* V1: volatile does NOT make ++ atomic: still load / add / store */
volatile uint32_t isr_count;
void V1_count(void)            { isr_count++; }

/* V2: volatile does NOT order normal-memory stores vs the doorbell on the CPU;
 * you need a barrier (dmb/dsb on Arm; Linux wmb()/writel()). */
void V2_kick(uint32_t *desc, volatile uint32_t *doorbell) {
    desc[0] = 0x1000; desc[1] = 64;
    __atomic_thread_fence(__ATOMIC_RELEASE);   /* -> dmb ish on arm64 */
    *doorbell = 1;
}
