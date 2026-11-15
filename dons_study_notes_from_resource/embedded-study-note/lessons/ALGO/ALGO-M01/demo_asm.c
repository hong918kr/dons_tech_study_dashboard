/* ALGO-M01 demo_asm.c — 고정 파라미터 비트 단위 CRC-16/IBM-3740 의 안쪽 루프가 Cortex-M 에서 어떻게 나오나.
 * 빌드(asm 만 본다, 링크 없음):
 *   clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_asm.c -o -
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -
 * <stdint.h>/<stddef.h> 만 쓴다 (베어메탈 타깃에 libc 헤더 없음).
 */
#include <stddef.h>
#include <stdint.h>

/* 분기형: 나가는 비트가 1 이면 poly XOR */
uint16_t crc16_3740_branch(const uint8_t *d, size_t n) {
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= (uint16_t)(*d++ << 8);
        for (int k = 0; k < 8; k++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* 마스크형: -(top bit) & poly — 데이터와 무관하게 같은 명령 열 (분기 없음) */
uint16_t crc16_3740_mask(const uint8_t *d, size_t n) {
    uint32_t crc = 0xFFFF;
    while (n--) {
        crc ^= (uint32_t)*d++ << 8;
        for (int k = 0; k < 8; k++)
            crc = ((crc << 1) ^ (0x1021u & (0u - ((crc >> 15) & 1u)))) & 0xFFFFu;
    }
    return (uint16_t)crc;
}
