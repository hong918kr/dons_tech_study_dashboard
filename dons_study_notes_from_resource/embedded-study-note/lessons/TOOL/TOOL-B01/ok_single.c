/* expect: ok */
#include <stdint.h>
static uint32_t popcount32(uint32_t x) { uint32_t n = 0; while (x) { x &= x - 1u; n++; } return n; }
int main(void) { return popcount32(0xF0u) == 4u ? 0 : 1; }
