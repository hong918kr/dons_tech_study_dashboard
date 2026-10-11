/* CRC 테스트 — 표준 check 값 · 비트 방식 == 테이블 방식 · 리터럴 테이블 == 생성 테이블 · 잔여(residue) · 비트 오류 검출 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "crc.h"
#include "out/values.h"

// --- tests ---
static const uint8_t CHECK[] = "123456789";            /* 모든 CRC 카탈로그가 쓰는 표준 입력 */
#define N 9

static void test_check_values(void)
{
    uint16_t t16[256];
    uint32_t t32[256];
    crc16_ccitt_make_table(t16);
    crc32_make_table(t32);
    assert(crc8(CHECK, N) == 0xF4);                          /* CRC-8/SMBUS */
    assert(crc8_bitwise(CHECK, N) == 0xF4);
    assert(crc16_ccitt_bitwise(0xFFFF, CHECK, N) == 0x29B1); /* CRC-16/CCITT-FALSE */
    assert(crc16_ccitt_table(t16, 0xFFFF, CHECK, N) == 0x29B1);
    assert(crc16_ccitt_nibble(CHECK, N) == 0x29B1);
    assert(crc16_ccitt_bitwise(0x0000, CHECK, N) == 0x31C3); /* CRC-16/XMODEM: init 만 다르다 */
    assert(crc32_bitwise(CHECK, N) == 0xCBF43926u);          /* CRC-32 (zlib) */
    assert(crc32_table(t32, CHECK, N) == 0xCBF43926u);
}

static void test_literal_table_matches_generator(void)
{
    uint8_t gen[256];
    crc8_make_table(gen);
    for (int i = 0; i < 256; i++) {
        uint8_t one = (uint8_t)i;
        assert(crc8(&one, 1) == gen[i]);                     /* 바이트 하나의 CRC = 테이블 값 */
    }
}

static void test_table_equals_bitwise_random(void)
{
    uint16_t t16[256];
    uint32_t t32[256];
    crc16_ccitt_make_table(t16);
    crc32_make_table(t32);
    srand(1234);
    uint8_t buf[300];
    for (int trial = 0; trial < 200; trial++) {
        size_t n = (size_t)(rand() % 300);
        for (size_t i = 0; i < n; i++)
            buf[i] = (uint8_t)rand();
        assert(crc8(buf, n) == crc8_bitwise(buf, n));
        assert(crc16_ccitt_table(t16, 0xFFFF, buf, n) == crc16_ccitt_bitwise(0xFFFF, buf, n));
        assert(crc16_ccitt_nibble(buf, n) == crc16_ccitt_bitwise(0xFFFF, buf, n));
        assert(crc32_table(t32, buf, n) == crc32_bitwise(buf, n));
    }
}

static void test_residue_zero(void)
{
    /* 반사 없음 + xorout 0 이면: 데이터 뒤에 CRC 를 (MSB 먼저) 붙여 다시 돌리면 0 → 수신 측 검사를 '0 인가'로 할 수 있다 */
    uint8_t f[12];
    memcpy(f, CHECK, N);
    f[N] = crc8(CHECK, N);
    assert(crc8(f, N + 1) == 0);
    uint16_t c = crc16_ccitt_bitwise(0xFFFF, CHECK, N);
    f[N] = (uint8_t)(c >> 8);
    f[N + 1] = (uint8_t)c;
    assert(crc16_ccitt_bitwise(0xFFFF, f, N + 2) == 0);
}

static void test_detects_every_single_bit_flip(void)
{
    uint8_t buf[32];
    for (int i = 0; i < 32; i++)
        buf[i] = (uint8_t)(i * 37);
    uint8_t good = crc8(buf, 32);
    for (int bit = 0; bit < 32 * 8; bit++) {                  /* 1비트 오류는 전부 잡혀야 한다 */
        buf[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        assert(crc8(buf, 32) != good);
        buf[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    }
}

static void test_lecture_worked_examples(void)       /* L10 페이지에 나온 숫자를 C 구현으로 다시 확인 */
{
    uint16_t t16[256];
    crc16_ccitt_make_table(t16);
    assert(crc8((const uint8_t *)"1", 1) == 0x97);
    assert(crc8((const uint8_t *)"12", 2) == EX_CRC8_12);
    assert(crc16_ccitt_table(t16, 0xFFFF, (const uint8_t *)"1", 1) == EX_CRC16_ONE);
    const uint8_t body[] = {3, 0x10, 0x20, 0x30};
    assert(crc8(body, sizeof body) == EX_FRAME_CRC);
}

int main(void)
{
    test_lecture_worked_examples();
    test_check_values();
    test_literal_table_matches_generator();
    test_table_equals_bitwise_random();
    test_residue_zero();
    test_detects_every_single_bit_flip();
    puts("test_crc: PASS");
    return 0;
}
// --- end ---
