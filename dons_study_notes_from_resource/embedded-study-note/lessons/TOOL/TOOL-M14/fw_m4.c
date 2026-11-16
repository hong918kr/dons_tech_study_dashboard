/* TOOL-M14 Cortex-M4 image for the ELF half (Mac arm-none-eabi-gcc, no newlib -> -nostdlib).
 *   arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -g -O1 -ffreestanding -nostdlib \
 *       -Wl,--build-id=sha1 -T fw_m4.ld fw_m4.c -o fw.elf
 * Never run here: the "crash" PCs are taken from the image itself (objdump), like a HardFault
 * handler would have stored them (ARM-M12 / BM-A09).  "@T" lines = ground truth for check.sh.
 */
#include <stdint.h>

struct frame { uint8_t hdr; const uint8_t *body; };
volatile uint32_t g_seen, g_crc;
extern uint32_t _estack;

static inline __attribute__((always_inline)) uint16_t rd_be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);                    // @T m4rd
}

__attribute__((noreturn, noinline)) void fw_fault(uint32_t code)
{
    *(volatile uint32_t *)0xE000ED0Cu = 0x05FA0004u | code;   // @T m4reset (AIRCR write)
    for (;;) { }
}

__attribute__((noinline)) void check_crc(const struct frame *f)
{
    if (f->hdr != 0x7E)
        fw_fault(0x10);                                       // @T m4chk
    g_crc += f->hdr;
}

__attribute__((noinline)) uint32_t parse_frame(const struct frame *f)
{
    uint32_t len = rd_be16(f->body);                          // @T m4parse
    return len + g_seen;
}

__attribute__((noinline)) uint32_t sensor_task(const struct frame *f)
{
    check_crc(f);                                             // @T m4task_chk
    uint32_t n = parse_frame(f);                              // @T m4task_parse
    g_seen++;
    return n;
}

void Reset_Handler(void)
{
    static const struct frame f = { 0x7E, 0 };
    for (;;)
        g_seen += sensor_task(&f);                            // @T m4reset_call
}

void Default_Handler(void) { for (;;) { } }

__attribute__((section(".isr_vector"), used))
const void *const g_vectors[16] = {
    &_estack, (void *)Reset_Handler, (void *)Default_Handler, (void *)Default_Handler,
};
