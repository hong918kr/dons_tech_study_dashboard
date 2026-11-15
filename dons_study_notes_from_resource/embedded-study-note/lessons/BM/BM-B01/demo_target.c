/* BM-B01 demo_target.c — 같은 드라이버 소스를 UNIT_TEST 없이 Cortex-M4 용으로 컴파일 (실행하지 않는다)
 *
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -ffreestanding -S demo_target.c -o -
 *   cc -std=c11 -Wall -Wextra -Werror -O2 -c demo_target.c -o /tmp/BM-B01_dt.o          (호스트: 경고 0 확인만)
 *   cc -std=c11 -Wall -Wextra -Werror -DDEMO_WRITE_RO -fsyntax-only demo_target.c       (읽기 전용 쓰기 → 에러)
 *
 * GPIOA 는 헤더 그대로 ((GPIO_TypeDef *)0x40020000UL) → asm 에 주소 상수가 그대로 보인다.
 * 호스트에서 이 함수들을 실행하면 0x40020000 을 역참조하므로 죽는다 — 그래서 테스트는 RAM 구조체로 바꿔 돌린다.
 */
#include <stdbool.h>
#include <stdint.h>
#include "device_excerpt.h"

#define VAL2FLD(field, value)   (((uint32_t)(value) << field ## _Pos) & field ## _Msk)
#define MODIFY_REG(REG, CLEARMASK, SETMASK) WRITE_REG((REG), (READ_REG(REG) & ~(CLEARMASK)) | (SETMASK))

enum { MODE_OUT = 1u };

/* 타깃에서 unsigned long 은 4바이트 (AAPCS ILP32), 맥 호스트에서는 8바이트 (LP64) */
const unsigned msk_size = sizeof(GPIO_MODER_MODER5_Msk);

void led_pa5_init(void) {
    SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPIOAEN);
    (void)READ_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPIOAEN);
    MODIFY_REG(GPIOA->MODER, GPIO_MODER_MODER5_Msk, VAL2FLD(GPIO_MODER_MODER5, MODE_OUT));
}

void led_pa5_set(bool on) {
    WRITE_REG(GPIOA->BSRR, on ? GPIO_BSRR_BS5_Msk : GPIO_BSRR_BR5_Msk);
}

#ifdef DEMO_WRITE_RO
/* nRF 계열 MDK 헤더 모양 (확인 권장): 입력 레지스터를 __IM (= volatile const) 로 선언 */
typedef struct {
    __IOM uint32_t OUT;
    __IOM uint32_t OUTSET;
    __IOM uint32_t OUTCLR;
    __IM  uint32_t IN;
} NRF_GPIO_Like;
#define P0 ((NRF_GPIO_Like *) 0x50000504UL)
void bad_write_input(void) { P0->IN = 1u; }   /* 읽기 전용 레지스터에 쓰기 → 컴파일 에러 */
#endif
