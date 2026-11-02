/* H1 (UB part) · uint16_t * uint16_t overflows *int* -> undefined behavior.
 * Build WITHOUT -Werror, WITH -fsanitize=undefined (see run.sh). */
#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) uint32_t mul16(uint16_t a, uint16_t b) {
    return a * b;          /* both promoted to (signed) int: 65535*65535 > INT_MAX */
}

int main(void) {
    uint32_t r = mul16(0xFFFF, 0xFFFF);
    printf("mul16(0xFFFF, 0xFFFF) = 0x%08X\n", (unsigned)r);
    return 0;
}
