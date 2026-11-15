/* ARCH-B11 demo — 같은 C 코드가 코어마다 어떤 버스 접근이 되나 (cross-target asm 관찰용, 실행 안 함)
 *   uart_putc_mmio : STM32 스타일 MMIO — 상수 주소 0x4000_4400 에서 SR 폴링 후 DR 쓰기 (워드 접근)
 *   load_u32_unaligned : 비정렬 포인터에서 4바이트 읽기 (memcpy 관용구)
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_unaligned_asm.c -o -
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -mno-unaligned-access -S demo_unaligned_asm.c -o -
 * clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_unaligned_asm.c -o -
 */
#include <stdint.h>

typedef struct { volatile uint32_t SR, DR, BRR, CR1; } USART_TypeDef;
#define USART2 ((USART_TypeDef *)0x40004400u)

void uart_putc_mmio(char c) {
    while (!(USART2->SR & (1u << 7))) { }       /* TXE */
    USART2->DR = (uint8_t)c;
}

uint32_t load_u32_unaligned(const uint8_t *p) {
    uint32_t v;
    __builtin_memcpy(&v, p, 4);                 /* 정렬을 모르는 포인터: 컴파일러가 코어 능력에 맞춰 고른다 */
    return v;
}
