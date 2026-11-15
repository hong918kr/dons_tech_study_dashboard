/* ALGO-A06 demo: Cortex-M4 에서 zigzag / varint / 델타 한 단계가 몇 명령인지 (freestanding — 라이브러리 헤더 없음)
 * build: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4.c -o -
 */
#include <stddef.h>
#include <stdint.h>

uint16_t zz16_enc(uint16_t d) { return (uint16_t)((uint16_t)(d << 1) ^ (uint16_t)(0u - (d >> 15))); }
uint16_t zz16_dec(uint16_t z) { return (uint16_t)((z >> 1) ^ (uint16_t)(0u - (z & 1u))); }

/* 델타 + zigzag + varint 를 한 샘플씩: 펌웨어 로거의 내부 루프 */
size_t dzv_enc(const int16_t *x, size_t n, uint8_t *out) {
    uint16_t prev = 0;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        uint16_t u = (uint16_t)x[i];
        uint16_t d = (uint16_t)(u - prev);
        prev = u;
        uint16_t z = zz16_enc(d);
        while (z >= 0x80u) { out[o++] = (uint8_t)(z | 0x80u); z = (uint16_t)(z >> 7); }
        out[o++] = (uint8_t)z;
    }
    return o;
}
