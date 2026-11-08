/* C-M01 demo_target.c — 실제 타깃(Cortex-M4)에서 두 방식이 만드는 코드 비교. main 없음.
 * 빌드: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -
 * 타깃 전용: 진짜 주소 0x40011000 (STM32F4 USART1). 호스트에서 실행하는 파일이 아니다. */
#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(void *) == 4, "ILP32 target");

/* ① #define 주소 방식 */
#define USART1_BASE   0x40011000u
#define REG32(a)      (*(volatile uint32_t *)(a))
#define USART1_SR     REG32(USART1_BASE + 0x00u)
#define USART1_DR     REG32(USART1_BASE + 0x04u)
#define USART1_BRR    REG32(USART1_BASE + 0x08u)
#define USART1_CR1    REG32(USART1_BASE + 0x0Cu)

/* ② 구조체 오버레이 방식 (CMSIS 스타일) */
typedef struct {
    volatile uint32_t SR, DR, BRR, CR1;
    uint32_t RESERVED0[4];
    volatile const uint32_t ID;
} USART_TypeDef;
_Static_assert(offsetof(USART_TypeDef, ID) == 0x20, "ID at 0x20");
#define USART1 ((USART_TypeDef *)USART1_BASE)

/* ③ 멤버에 volatile 이 없는 구조체 (잘못된 정의) */
typedef struct { uint32_t SR, DR, BRR, CR1; } USART_NV;
#define USART1_NV ((USART_NV *)USART1_BASE)

/* ④ 16비트 멤버 (예전 SPL 스타일) */
typedef struct {
    volatile uint16_t SR;  uint16_t R0;
    volatile uint16_t DR;  uint16_t R1;
} USART16_TypeDef;
#define USART1_16 ((USART16_TypeDef *)USART1_BASE)

void putc_define(uint8_t c)  { while (!(USART1_SR & 0x80u)) { } USART1_DR = c; }
void putc_struct(uint8_t c)  { while (!(USART1->SR & 0x80u)) { } USART1->DR = c; }
void putc_param(USART_TypeDef *u, uint8_t c) { while (!(u->SR & 0x80u)) { } u->DR = c; }

void init_define(void) { USART1_CR1 = 0; USART1_BRR = 139; USART1_CR1 = 0xC; USART1_CR1 = 0x200C; }
void init_struct(void) { USART1->CR1 = 0; USART1->BRR = 139; USART1->CR1 = 0xC; USART1->CR1 = 0x200C; }
void init_nv(void)     { USART1_NV->CR1 = 0; USART1_NV->BRR = 139; USART1_NV->CR1 = 0xC; USART1_NV->CR1 = 0x200C; }

void write16(uint8_t c)      { USART1_16->DR = c; }
uint32_t read_id(void)       { return USART1->ID; }
