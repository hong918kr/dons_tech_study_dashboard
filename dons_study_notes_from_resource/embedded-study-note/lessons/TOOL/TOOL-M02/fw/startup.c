/* TOOL-M02 demo firmware: vector table + Reset_Handler (see TOOL-M01, BM-B08). */
#include "fw.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);
void Reset_Handler(void);

void Default_Handler(void)
{
	for (;;) {
	}
}
void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

__attribute__((section(".isr_vector"), used)) void (*const g_vectors[16 + 38])(void) = {
	[0] = (void (*)(void))&_estack,
	[1] = Reset_Handler,
	[2] = NMI_Handler,
	[3] = HardFault_Handler,
	[15] = SysTick_Handler,
	[16 + 37] = USART1_IRQHandler,
};

void Reset_Handler(void)
{
	memcpy(&_sdata, &_sidata, (size_t)((char *)&_edata - (char *)&_sdata));
	memset(&_sbss, 0, (size_t)((char *)&_ebss - (char *)&_sbss));
	main();
	for (;;) {
	}
}
