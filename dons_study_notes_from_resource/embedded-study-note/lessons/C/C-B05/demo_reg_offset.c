/* C-B05 데모: base + offset 계산에서 포인터 타입을 잘못 고르면 주소가 4배로 뛴다 */
#include <stdint.h>
#include <stdio.h>

int main(void) {
    const uintptr_t BASE = 0x40011000u;          /* STM32F4 USART1 */
    const uint32_t  DR   = 0x04u;                /* USART_DR offset (F4) */

    uintptr_t right = (uintptr_t)((volatile uint8_t *)BASE + DR);
    uintptr_t wrong = (uintptr_t)((volatile uint32_t *)BASE + DR);
    uintptr_t also  = (uintptr_t)((volatile uint32_t *)BASE + DR / 4u);

    printf("(uint8_t *)BASE + 0x%02x        = %#jx\n", (unsigned)DR, (uintmax_t)right);
    printf("(uint32_t*)BASE + 0x%02x        = %#jx   <-- 0x%02x * 4 bytes\n",
           (unsigned)DR, (uintmax_t)wrong, (unsigned)DR);
    printf("(uint32_t*)BASE + 0x%02x / 4    = %#jx\n", (unsigned)DR, (uintmax_t)also);
    return 0;   /* 주소만 계산, 역참조는 하지 않는다 */
}
