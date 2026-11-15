/* ALGO-A07 demo — Cortex-M 코드 생성 보기 (freestanding: <stdint.h> 만)
 * Cortex-M4:  clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -
 * Cortex-M0+: clang --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -mthumb -O2 -S demo_asm.c -o -
 * (실행 파일이 아니다. 이 맥에서 asm 만 뽑는다.)
 */
#include <stdint.h>

uint32_t gray_enc(uint32_t b) { return b ^ (b >> 1); }

uint32_t gray_dec(uint32_t g) {
    g ^= g >> 16; g ^= g >> 8; g ^= g >> 4; g ^= g >> 2; g ^= g >> 1;
    return g;
}

uint32_t haszero32(uint32_t v) { return (v - 0x01010101u) & ~v & 0x80808080u; }

/* 첫 0 바이트 (little-endian), 없으면 4 */
uint32_t first_zero_le(uint32_t v) {
    uint32_t h = haszero32(v);
    return h ? (uint32_t)__builtin_ctz(h) >> 3 : 4u;
}

uint32_t gosper_div(uint32_t x) {
    uint32_t c = x & (0u - x), r = x + c;
    if (x == 0 || r == 0) return 0;
    return (((r ^ x) >> 2) / c) | r;
}

uint32_t gosper_ctz(uint32_t x) {
    uint32_t c = x & (0u - x), r = x + c;
    if (x == 0 || r == 0) return 0;
    return (((r ^ x) >> 2) >> __builtin_ctz(x)) | r;
}
