/*
You're working on an STM32H7 (Cortex-M7) system with D-cache enabled.
You configure DMA to fill a buffer from a high-speed ADC peripheral.

But 𝗨𝘀𝗲𝗕𝘂𝗳𝗳𝗲𝗿() 𝘀𝗼𝗺𝗲𝘁𝗶𝗺𝗲𝘀 𝗽𝗿𝗼𝗰𝗲𝘀𝘀𝗲𝘀 𝘀𝘁𝗮𝗹𝗲 𝗼𝗿 𝗴𝗮𝗿𝗯𝗮𝗴𝗲 𝘃𝗮𝗹𝘂𝗲𝘀, 
𝗲𝘃𝗲𝗻 𝘁𝗵𝗼𝘂𝗴𝗵 𝘁𝗵𝗲 𝗗𝗠𝗔 𝗰𝗼𝗺𝗽𝗹𝗲𝘁𝗲𝘀 𝘀𝘂𝗰𝗰𝗲𝘀𝘀𝗳𝘂𝗹𝗹𝘆. 𝗪𝗵𝗮𝘁 𝗶𝘀 𝘁𝗵𝗲 𝗺𝗼𝘀𝘁 𝗮𝗰𝗰𝘂𝗿𝗮𝘁𝗲 𝗿𝗲𝗮𝘀𝗼𝗻?

A) The DMA is not aligned to a 4-byte boundary, 
which violates the STM32 memory alignment requirements.

B) adc_buffer is in SRAM2, which is inaccessible by the DMA controller.

C) The D-cache is not coherent with the DMA, 
so the CPU sees stale data unless explicitly cleaned/invalidated.

D) volatile is missing from adc_buffer, 
causing stale reads due to compiler optimizations.
*/

#define BUF_SIZE 1024
uint16_t adc_buffer[BUF_SIZE] __attribute__((aligned(32)));

void StartADC_DMA(void)
{
    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buffer, BUF_SIZE);
}

// Later in the main loop
void ProcessADC(void) {
    if (dma_done) {
        UseBuffer(adc_buffer);   // Process the filled buffer
    }
}

