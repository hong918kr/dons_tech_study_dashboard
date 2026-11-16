/* TOOL-M07 cm_start.c — buggy.c 를 Cortex-M3(qemu-system-arm -M mps2-an385)에 올리는 최소 시작 코드.
 * 빌드: arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -g -O0 -nostdlib -Dprintf=cm_printf -T cm.ld cm_start.c buggy.c
 * printf 는 버린다(cm_printf) — 이 데모는 gdb 로만 본다. 시작 코드 자체는 BM-B08 참고.
 */
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);

int cm_printf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata)
        *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; )
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

__attribute__((section(".isr_vector"), used))
const void *const vectors[16] = {
    &_estack, (void *)Reset_Handler, (void *)Default_Handler, (void *)Default_Handler,
};
