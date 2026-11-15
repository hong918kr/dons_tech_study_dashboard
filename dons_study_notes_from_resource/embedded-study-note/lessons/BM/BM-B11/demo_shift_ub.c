/* BM-B11 demo: gpio_write(port, 15, 0) 의 BSRR 값을 signed int 로 만들면 — 1 << 31 은 UB
 * cc -std=c11 -Wall -Wextra -Werror -O1 -fsanitize=undefined -fno-sanitize-recover=undefined demo_shift_ub.c -o /tmp/BM-B11_ub && /tmp/BM-B11_ub
 */
#include <stdint.h>
#include <stdio.h>

static uint32_t bsrr_reset_bad(unsigned pin)  { return (uint32_t)(1 << (pin + 16)); }  /* int 1 */
static uint32_t bsrr_reset_good(unsigned pin) { return 1u << (pin + 16u); }            /* unsigned 1 */

int main(int argc, char **argv) {
    (void)argv;
    unsigned pin = 14u + (unsigned)argc;            /* = 15, 컴파일러가 상수로 못 접게 */
    printf("good: pin %u -> BSRR 0x%08X\n", pin, (unsigned)bsrr_reset_good(pin));
    printf("bad : pin %u -> BSRR 0x%08X\n", pin, (unsigned)bsrr_reset_bad(pin));
    return 0;
}
