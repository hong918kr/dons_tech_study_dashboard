/* ENV: cross — arm-none-eabi-gcc only (CMake target build). Never built for the host. */
/* startup.c — minimal Cortex-M4 startup (BM-B08 condensed; linker symbols from TOOL-M01). */
#include <stdint.h>

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
int main(void);

void Reset_Handler(void)
{
    uint32_t *src = _sidata, *dst = _sdata;
    while (dst < _edata)                 /* .data initial values: Flash (LMA) -> RAM (VMA) */
        *dst++ = *src++;
    for (dst = _sbss; dst < _ebss;)      /* .bss = 0 (costs no Flash) */
        *dst++ = 0;
    main();
    for (;;) {
    }
}

void Default_Handler(void)
{
    for (;;) {
    }
}

/* vector table: [0] initial SP, [1] Reset, then the Cortex-M system exceptions */
__attribute__((section(".isr_vector"), used))
const uintptr_t vectors[16] = {
    (uintptr_t)_estack, (uintptr_t)Reset_Handler,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, (uintptr_t)Default_Handler,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, 0, 0, 0, 0,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, 0,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler,
};
