/*
Type 3: Low-Level Driver Logic (SPI/I2C)
Core Skill: Translating a datasheet into code.


Problem: Given a function spi_transfer(byte),
 write a function to read a 2-byte temperature value from a sensor.

 
Key Points: Explain Chip Select (CS) control, sending a command byte, 
sending dummy bytes to receive data (due to SPI's full-duplex nature), 
and combining the two received bytes into a 16-bit integer.

*/
#include <stdio.h>
#include <stdint.h>

// Chip Select 제어 함수 (가정)
void cs_low(void) {
    // TODO: 구현
}
void cs_high(void) {
    // TODO: 구현
}

// SPI 전송 함수 (가정: 1바이트 전송, 1바이트 수신)
uint8_t spi_transfer(uint8_t byte) {
    // TODO: 구현
    return 0;
}

// 센서에서 2바이트 온도값 읽기
uint16_t read_temperature(void) {
    // TODO: 구현
    return 0;
}

// 테스트 코드
int main() {
    // TODO: spi_transfer, cs_low/high를 적절히 mock하거나 직접 구현 후 테스트
    uint16_t temp = read_temperature();
    printf("Read temperature: %u\n", temp);
    return 0;
}