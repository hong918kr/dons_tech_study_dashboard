/* 한 바이트 손으로 따라가기: CRC-8, 입력 '1' (0x31) — 비트 방식 8단계와 테이블 조회 한 번이 같은 값인지 */
#include <stdint.h>
#include <stdio.h>

static void bin8(uint8_t v) { for (int i = 7; i >= 0; i--) putchar((v >> i) & 1 ? '1' : '0'); }

int main(void)
{
    uint8_t crc = 0x00, in = 0x31;
    crc ^= in;
    printf("시작: crc = 0x00 ^ 입력 0x31 = 0x%02X  (", crc); bin8(crc); puts(")");
    for (int i = 1; i <= 8; i++) {
        int top = crc & 0x80;
        uint8_t shifted = (uint8_t)(crc << 1);
        crc = top ? (uint8_t)(shifted ^ 0x07) : shifted;
        printf("단계 %d: 맨 위 비트 %d → %s → 0x%02X  (", i, top ? 1 : 0,
               top ? "<<1 후 ^0x07" : "<<1 만     ", crc);
        bin8(crc);
        puts(")");
    }
    printf("결과: CRC8(0x31) = 0x%02X  = 테이블[0x31] (0x30~0x3F 줄의 2번째 칸)\n", crc);
    return 0;
}
