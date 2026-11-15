/* ENV: qemu */
/* BM-B08 demo — "주소 0 에 있는 객체" 를 0 과 비교하면 GCC 가 비교를 지운다.
 * C 에서 객체의 주소는 널 포인터와 같을 수 없으므로 (&obj != NULL), -O2 는 아래를 상수로 접는다.
 * 벡터 테이블은 진짜로 주소 0 에 있으니, 검사는 volatile 레지스터(VTOR) 값과 비교해야 한다.
 * build: arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -c demo_addr0.c
 */
#include <stdint.h>

typedef void (*vector_t)(void);
extern const vector_t g_vectors[];

int table_at_zero_naive(void) { return (uintptr_t)g_vectors == 0u; }            /* → movs r0, #0 */

int table_at_zero_vtor(void) {                                                    /* → 진짜 비교 */
    return *(volatile uint32_t *)0xE000ED08u == (uintptr_t)g_vectors;
}
