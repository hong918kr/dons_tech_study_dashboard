/* C-M05 demo: 시프트로 쓴 get/put 헬퍼가 어떤 명령으로 컴파일되는가 (main 없음, 헤더는 stdint 만)
 *   clang -std=c11 -O2 -S demo_asm.c -o -                                                   # 호스트 arm64
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -O2 -S demo_asm.c -o -
 *   clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2 -S demo_asm.c -o -          # 비정렬 금지 코어
 */
#include <stdint.h>

uint16_t get_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint16_t get_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t get_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint32_t get_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
uint64_t get_le64(const uint8_t *p) { return (uint64_t)get_le32(p) | ((uint64_t)get_le32(p + 4) << 32); }
uint64_t get_be64(const uint8_t *p) { return ((uint64_t)get_be32(p) << 32) | (uint64_t)get_be32(p + 4); }

void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
void put_be64(uint8_t *p, uint64_t v) { put_be32(p, (uint32_t)(v >> 32)); put_be32(p + 4, (uint32_t)v); }

int16_t s16_from_u16(uint16_t u) { return u <= 0x7FFFu ? (int16_t)u : (int16_t)((int32_t)u - 65536); }

/* float: memcpy 로 비트 이동 (freestanding 이라 <string.h> 대신 __builtin_memcpy — 데모 전용) */
void put_le_f32(uint8_t *p, float f) { uint32_t u; __builtin_memcpy(&u, &f, 4); put_le32(p, u); }
float get_le_f32(const uint8_t *p) { uint32_t u = get_le32(p); float f; __builtin_memcpy(&f, &u, 4); return f; }

/* 헤더 3필드를 연달아 쓰면? (sync BE16, type u8, seq LE16 — 오프셋 0,2,3) */
void put_hdr(uint8_t *p, uint16_t seq) {
    p[0] = 0xEB; p[1] = 0x90; p[2] = 0x21;
    p[3] = (uint8_t)seq; p[4] = (uint8_t)(seq >> 8);
}
