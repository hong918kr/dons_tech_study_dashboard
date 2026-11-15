/* ARCH-B06 demo_asm — 펌웨어에서 LFSR 한 스텝은 몇 명령인가 (freestanding: stdint.h 만)
 * Cortex-M4: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -
 * arm64    : cc -O2 -S -o - demo_asm.c
 */
#include <stdint.h>

uint16_t lfsr16_galois(uint16_t s) {          /* x^16 + x^14 + x^13 + x^11 + 1 */
    unsigned out = s & 1u;
    s = (uint16_t)(s >> 1);
    if (out) s ^= 0xB400u;
    return s;
}

uint16_t lfsr16_galois_branchless(uint16_t s) {
    return (uint16_t)((s >> 1) ^ (-(unsigned)(s & 1u) & 0xB400u));
}
