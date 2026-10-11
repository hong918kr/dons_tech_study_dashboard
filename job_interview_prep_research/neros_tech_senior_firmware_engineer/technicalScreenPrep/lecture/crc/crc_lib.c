#include "crc.h"

// --- crc8_bitwise ---
/* 비트 방식: 바이트를 crc 위쪽에 XOR 하고, 8번 '맨 위 비트가 1이면 시프트 후 poly XOR, 아니면 시프트만' */
uint8_t crc8_bitwise(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}
// --- end ---

// --- crc8_make_table ---
/* 테이블 만들기: table[i] = '바이트 i 하나만 넣었을 때 8번 돌린 결과'. 위 루프의 안쪽을 미리 계산해 둔 것 */
void crc8_make_table(uint8_t table[256])
{
    for (int i = 0; i < 256; i++) {
        uint8_t c = (uint8_t)i;
        for (int b = 0; b < 8; b++)
            c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
        table[i] = c;
    }
}
// --- end ---

// --- crc16 ---
/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, MSB-first, xorout 0 (XMODEM 은 같은 식에 init 0x0000) */
uint16_t crc16_ccitt_bitwise(const uint16_t init, const uint8_t *data, size_t len)
{
    uint16_t crc = init;
    while (len--) {
        crc ^= (uint16_t)(*data++ << 8);              /* 바이트를 crc 의 '위쪽 8비트'에 */
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

void crc16_ccitt_make_table(uint16_t table[256])
{
    for (int i = 0; i < 256; i++) {
        uint16_t c = (uint16_t)(i << 8);              /* 인덱스를 위쪽 바이트에 놓고 8번 */
        for (int b = 0; b < 8; b++)
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
        table[i] = c;
    }
}

uint16_t crc16_ccitt_table(const uint16_t table[256], uint16_t init, const uint8_t *data, size_t len)
{
    uint16_t crc = init;
    while (len--)
        crc = (uint16_t)((crc << 8) ^ table[((crc >> 8) ^ *data++) & 0xFF]);   /* 위 바이트 ^ 입력 → 인덱스 */
    return crc;
}
// --- end ---

// --- crc16_nibble ---
/* 4비트(니블) 테이블 16칸 = 32 바이트. 바이트당 조회 2번. flash 가 아까운 작은 MCU 용 절충안 */
uint16_t crc16_ccitt_nibble(const uint8_t *data, size_t len)
{
    static const uint16_t T[16] = {
        0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
        0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    };
    uint16_t crc = 0xFFFF;
    while (len--) {
        uint8_t b = *data++;
        crc = (uint16_t)((crc << 4) ^ T[((crc >> 12) ^ (b >> 4)) & 0x0F]);   /* 위 니블 */
        crc = (uint16_t)((crc << 4) ^ T[((crc >> 12) ^ (b & 0x0F)) & 0x0F]); /* 아래 니블 */
    }
    return crc;
}
// --- end ---

// --- crc32 ---
/* CRC-32 (Ethernet · zlib · PNG): '반사(reflected)' 방식 — LSB 부터, 반사된 poly 0xEDB88320, init/xorout 0xFFFFFFFF */
uint32_t crc32_bitwise(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        crc ^= *data++;                                /* 반사형: 바이트를 '아래쪽'에 */
        for (int i = 0; i < 8; i++)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

void crc32_make_table(uint32_t table[256])
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int b = 0; b < 8; b++)
            c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
        table[i] = c;
    }
}

uint32_t crc32_table(const uint32_t table[256], const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    while (len--)
        crc = (crc >> 8) ^ table[(crc ^ *data++) & 0xFF];   /* 아래 바이트 ^ 입력 → 인덱스, 오른쪽 시프트 */
    return crc ^ 0xFFFFFFFFu;
}
// --- end ---
