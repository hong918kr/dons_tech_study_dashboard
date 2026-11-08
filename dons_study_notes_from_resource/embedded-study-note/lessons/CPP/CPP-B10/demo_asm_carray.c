/* CPP-B10 demo: C 배열 버전 (asm 비교용)
 * cc -std=c11 -O2 -S -o - demo_asm_carray.c
 */
#include <stddef.h>
#include <stdint.h>

/* C 는 컴파일 타임에 표를 "계산" 할 수 없다 → ① 스크립트로 256개 리터럴 생성 또는 ② 부팅 때 계산(RAM) */
static uint8_t crc8_table[256];                 /* ② 방식: .bss (RAM 256B) + init 코드 */
void crc8_init(void) {
    for (unsigned i = 0; i < 256; i++) {
        uint8_t c = (uint8_t)i;
        for (int b = 0; b < 8; b++) c = (c & 0x80u) ? (uint8_t)((c << 1) ^ 0x07u) : (uint8_t)(c << 1);
        crc8_table[i] = c;
    }
}

uint8_t crc8_frame(const uint8_t f[8]) {        /* [8] 은 장식 — 실제로는 const uint8_t* */
    uint8_t c = 0;
    for (size_t i = 0; i < 8; i++) c = crc8_table[c ^ f[i]];
    return c;
}

uint32_t sum64(const uint16_t buf[64]) {
    uint32_t s = 0;
    for (size_t i = 0; i < 64; i++) s += buf[i];
    return s;
}

uint16_t get3(const uint16_t buf[64]) { return buf[3]; }
