/* ENV: cross — demo_size.sh 가 thumbv7em(Mac clang) / arm-none-eabi-gcc(VM) 로 빌드한다. 호스트에서 실행 금지. */
/* TOOL-B06 최소 시작 코드 (BM-B08 을 줄인 것). 설명은 BM-B08, 링커 스크립트는 TOOL-M01. */
#include <stdint.h>

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
int main(void);

void Reset_Handler(void) {
    uint32_t *src = _sidata, *dst = _sdata;
    while (dst < _edata) *dst++ = *src++;      /* .data 초기값: Flash(LMA) → RAM(VMA) 복사 */
    for (dst = _sbss; dst < _ebss;) *dst++ = 0; /* .bss 0 채우기 (Flash 에는 아무것도 없다)  */
    main();
    for (;;) {
    }
}

void Default_Handler(void) {
    for (;;) {
    }
}

/* Cortex-M 벡터 표 앞 16칸: [0] 초기 SP, [1] Reset, 나머지는 기본 핸들러 */
__attribute__((section(".isr_vector"), used))
const uintptr_t vectors[16] = {
    (uintptr_t)_estack, (uintptr_t)Reset_Handler,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, (uintptr_t)Default_Handler,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, 0, 0, 0, 0,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, 0,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler,
};
