/* C-M11 demo: 큰 본문을 여러 곳에서 쓸 때 — 매크로는 무조건 복사, 함수는 "복사할지 말지" 선택할 수 있다
 *   -DUSE_MACRO : CRC8 한 바이트 처리를 문장 매크로로 (호출할 때마다 루프 통째 복사)
 *   (기본)      : static inline 함수 — 컴파일러가 인라인 여부를 정한다
 *   -DNOINLINE  : 같은 함수에 __attribute__((noinline)) — 본문 1벌 + bl 호출
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -Os [-D...] -c demo_crc_size.c -o crc.o
 * xcrun llvm-size -A crc.o ; xcrun llvm-nm -S crc.o
 */
#include <stdint.h>

#ifdef NOINLINE
#define NOINL __attribute__((noinline))
#else
#define NOINL
#endif

#ifdef USE_MACRO
#define CRC8_STEP(crc, b) do { (crc) ^= (uint8_t)(b); \
    for (int k_ = 0; k_ < 8; k_++) (crc) = ((crc) & 0x80u) ? (uint8_t)(((crc) << 1) ^ 0x07u) : (uint8_t)((crc) << 1); } while (0)
#else
NOINL static inline uint8_t crc8_step(uint8_t crc, uint8_t b) {      /* CRC-8 (poly 0x07), 비트 단위 */
    crc ^= b;
    for (int k = 0; k < 8; k++) crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
    return crc;
}
#define CRC8_STEP(crc, b) ((crc) = crc8_step((crc), (b)))
#endif

/* 패킷 헤더 4바이트, 명령 2바이트, 블록 16바이트 — 호출 지점 22곳 */
uint8_t hdr_crc(const uint8_t *h) {
    uint8_t c = 0; CRC8_STEP(c, h[0]); CRC8_STEP(c, h[1]); CRC8_STEP(c, h[2]); CRC8_STEP(c, h[3]); return c;
}
uint8_t cmd_crc(uint8_t a, uint8_t b) { uint8_t c = 0xFF; CRC8_STEP(c, a); CRC8_STEP(c, b); return c; }
uint8_t blk_crc(const uint8_t *p) {
    uint8_t c = 0;
    CRC8_STEP(c, p[0]);  CRC8_STEP(c, p[1]);  CRC8_STEP(c, p[2]);  CRC8_STEP(c, p[3]);
    CRC8_STEP(c, p[4]);  CRC8_STEP(c, p[5]);  CRC8_STEP(c, p[6]);  CRC8_STEP(c, p[7]);
    CRC8_STEP(c, p[8]);  CRC8_STEP(c, p[9]);  CRC8_STEP(c, p[10]); CRC8_STEP(c, p[11]);
    CRC8_STEP(c, p[12]); CRC8_STEP(c, p[13]); CRC8_STEP(c, p[14]); CRC8_STEP(c, p[15]);
    return c;
}
