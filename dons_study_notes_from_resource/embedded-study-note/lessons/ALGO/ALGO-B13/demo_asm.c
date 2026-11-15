/* ALGO-B13 demo: 같은 C 가 arm64 / Cortex-M4 / Cortex-M0 에서 어떤 명령이 되나.
 * <stdint.h> 만 쓴다 (thumb 타깃은 libc 헤더 없이 컴파일).
 *   clang -O2 -S demo_asm.c -o - | less                                           # arm64 (이 맥)
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S demo_asm.c -o -     # Cortex-M4 (ARMv7E-M)
 *   clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -O2 -S demo_asm.c -o -     # Cortex-M0 (ARMv6-M)
 */
#include <stdint.h>

uint32_t rev32_loop(uint32_t x) {
    uint32_t r = 0;
    for (int i = 0; i < 32; i++) { r = (r << 1) | (x & 1u); x >>= 1; }
    return r;
}
uint32_t rev32_dc(uint32_t x) {
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    x = ((x >> 8) & 0x00FF00FFu) | ((x & 0x00FF00FFu) << 8);
    return (x >> 16) | (x << 16);
}
extern const uint8_t REV8_EXT[256];        /* 표는 다른 곳에 있다고 치고 접근 코드만 본다 */
uint32_t rev32_lut(uint32_t x) {
    return ((uint32_t)REV8_EXT[x & 0xFFu] << 24) | ((uint32_t)REV8_EXT[(x >> 8) & 0xFFu] << 16) |
           ((uint32_t)REV8_EXT[(x >> 16) & 0xFFu] << 8) | (uint32_t)REV8_EXT[x >> 24];
}
uint8_t rev8_dc(uint8_t b) {
    b = (uint8_t)((b >> 4) | (b << 4));
    b = (uint8_t)(((b >> 2) & 0x33u) | ((b & 0x33u) << 2));
    b = (uint8_t)(((b >> 1) & 0x55u) | ((b & 0x55u) << 1));
    return b;
}
uint64_t rev_nibbles64_dc(uint64_t x) {
    x = ((x >> 4) & 0x0F0F0F0F0F0F0F0Full) | ((x & 0x0F0F0F0F0F0F0F0Full) << 4);
    x = ((x >> 8) & 0x00FF00FF00FF00FFull) | ((x & 0x00FF00FF00FF00FFull) << 8);
    x = ((x >> 16) & 0x0000FFFF0000FFFFull) | ((x & 0x0000FFFF0000FFFFull) << 16);
    return (x >> 32) | (x << 32);
}
uint32_t swap_odd_even32(uint32_t x) {
    return ((x & 0xAAAAAAAAu) >> 1) | ((x & 0x55555555u) << 1);
}
uint32_t parity32_fold(uint32_t x) {
    x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
    return x & 1u;
}
uint32_t parity32_6996(uint32_t x) {        /* 마지막 4비트는 16비트 상수 0x6996 을 표로 쓴다 */
    x ^= x >> 16; x ^= x >> 8; x ^= x >> 4;
    return (0x6996u >> (x & 0xFu)) & 1u;
}
