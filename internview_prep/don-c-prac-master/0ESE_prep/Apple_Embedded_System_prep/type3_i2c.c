/*
Type 3: Low-Level Driver Logic (I2C)
Core Skill: Translating a datasheet into code.

Problem: Given functions i2c_start(), i2c_write(byte), i2c_read(ack), i2c_stop(),
write a function to read a 2-byte temperature value from a sensor.

Key Points: Explain I2C start/stop, sending device address and command byte,
reading two bytes (MSB/LSB), sending ACK/NACK, and combining into a 16-bit integer.
*/

#include <stdint.h>
#include <stdio.h>

// I2C 기본 함수 시그니처 (구현은 비워둠)
void i2c_start(void) {
    // TODO: implement
}
void i2c_stop(void) {
    // TODO: implement
}
void i2c_write(uint8_t byte) {
    // TODO: implement
}
uint8_t i2c_read(int ack) {
    // TODO: implement
    return 0;
}

// 센서 주소와 명령 (예시)
#define SENSOR_ADDR 0x48
#define TEMP_CMD    0x01

// 2바이트 온도값 읽기 함수
uint16_t read_temperature_i2c(void) {
    // TODO: implement
    return 0;
}

// 테스트 코드
int main() {
    uint16_t temp = read_temperature_i2c();
    printf("Read temperature (I2C): %u\n", temp);
    return 0;
}

/*
부연설명:
- I2C는 start/stop 신호로 통신을 시작/종료합니다.
- 먼저 start, 센서 주소(쓰기), 명령 바이트(TEMP_CMD) 전송.
- 다시 start, 센서 주소(읽기) 전송.
- i2c_read(1)로 첫 바이트(MSB) 읽고 ACK, i2c_read(0)로 두 번째 바이트(LSB) 읽고 NACK.
- stop으로 통신 종료.
- 두 바이트를 합쳐 16비트 값으로 반환.
*/