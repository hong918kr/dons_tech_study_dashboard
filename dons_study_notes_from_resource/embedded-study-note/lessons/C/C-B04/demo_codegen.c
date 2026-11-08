/* C-B04 데모: 마스크 전용 매크로의 "/" "*" 는 상수 마스크면 시프트·비트필드 명령으로 접힌다.
 *   cc -std=c11 -O2 -S -o - demo_codegen.c                                       # Apple arm64
 *   clang -std=c11 -O2 -S -o - --target=thumbv7em-none-eabi -mcpu=cortex-m4 \
 *         -ffreestanding demo_codegen.c                                          # Cortex-M4
 */
#include <stdint.h>

#define FIELD_LSB(mask)       ((uint32_t)(mask) & (~(uint32_t)(mask) + 1u))
#define FIELD_GET(mask, reg)  (((uint32_t)(reg) & (uint32_t)(mask)) / FIELD_LSB(mask))
#define FIELD_PREP(mask, val) (((uint32_t)(val) * FIELD_LSB(mask)) & (uint32_t)(mask))
#define FIELD_REPLACE(mask, reg, val) (((uint32_t)(reg) & ~(uint32_t)(mask)) | FIELD_PREP(mask, val))

#define TIM_CTRL_PRESC_Msk 0x000000F0u   /* bits 7:4 */

uint32_t get_presc(uint32_t ctrl)               { return FIELD_GET(TIM_CTRL_PRESC_Msk, ctrl); }
uint32_t set_presc(uint32_t ctrl, uint32_t v)   { return FIELD_REPLACE(TIM_CTRL_PRESC_Msk, ctrl, v); }
/* 마스크가 런타임 값이면? → 진짜 나눗셈이 남는다 */
uint32_t get_any(uint32_t reg, uint32_t mask)   { return FIELD_GET(mask, reg); }
