
//58. // I2C 레지스터 쓰기 함수 구현
// 설명: I2C 슬레이브의 특정 레지스터에 데이터를 쓰는 함수 구현
// 개념: 시리얼 통신, 프로토콜
// 함수 시그니처:
int i2c_write_register(uint8_t dev_addr, uint8_t reg_addr, uint8_t data);
// 샘플 입력: i2c_write_register(0x50, 0x10, 0xAB);
// 샘플 출력: 0 (성공), -1 (실패)
