/* ALGO-M02 크로스 타깃 관찰: 같은 CRC-32 루프가 Cortex-M4 / M0 에서 몇 개의 명령이 되나.
 * 테이블은 demo_gentable.c 가 만든 const 배열(플래시)이라고 보고 extern 으로만 선언한다.
 * 이 파일은 <stdint.h>/<stddef.h> 만 쓰므로 베어메탈 타깃으로 바로 컴파일된다:
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4.c -o -
 *   clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_m4.c -o -
 * (호스트 확인용 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 -c demo_m4.c -o /tmp/ALGO-M02_m4.o)
 */
#include <stddef.h>
#include <stdint.h>

#define POLY_R 0xEDB88320u
extern const uint32_t crc32_tab[256];
extern const uint32_t crc32_tab8[8][256];

uint32_t m_bitwise(uint32_t c, const uint8_t *p, size_t n) {
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (POLY_R & (0u - (c & 1u)));
    }
    return c;
}

uint32_t m_table(uint32_t c, const uint8_t *p, size_t n) {
    while (n--) c = crc32_tab[(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return c;
}

static inline uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint32_t m_slice8(uint32_t c, const uint8_t *p, size_t n) {
    const uint32_t (*T)[256] = crc32_tab8;
    for (; n >= 8; p += 8, n -= 8) {
        uint32_t lo = c ^ le32(p), hi = le32(p + 4);
        c = T[7][lo & 0xFFu] ^ T[6][(lo >> 8) & 0xFFu] ^ T[5][(lo >> 16) & 0xFFu] ^ T[4][lo >> 24]
          ^ T[3][hi & 0xFFu] ^ T[2][(hi >> 8) & 0xFFu] ^ T[1][(hi >> 16) & 0xFFu] ^ T[0][hi >> 24];
    }
    while (n--) c = T[0][(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return c;
}
