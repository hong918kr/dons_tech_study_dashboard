/* C-M01 demo_real_addr.c — 타깃 주소를 호스트(맥)에서 그대로 역참조하면?
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_real_addr.c -o demo_real_addr && ./demo_real_addr
 * 0x40011000 은 STM32F4 USART1 이지만, 맥 프로세스의 가상 주소 공간에선 매핑되지 않은 페이지다. */
#include <stdint.h>
#include <stdio.h>

#define USART1_SR (*(volatile uint32_t *)0x40011000u)

int main(void) {
    printf("reading 0x40011000 ...\n");
    fflush(stdout);
    uint32_t sr = USART1_SR;          /* 정수 → 포인터는 구현 정의, 매핑 안 된 주소 역참조는 UB */
    printf("SR = %08X\n", (unsigned)sr);
    return 0;
}
