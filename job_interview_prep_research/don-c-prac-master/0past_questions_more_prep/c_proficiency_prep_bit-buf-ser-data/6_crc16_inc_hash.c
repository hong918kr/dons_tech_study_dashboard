/*
6. CRC16-CCITT(0x1021) 인크리멘탈 해시 - 실제 인터뷰 스타일 상세 설명

상황 설명:
당신은 산업용 센서 네트워크의 통신 프로토콜을 개발 중입니다.  
센서 데이터, 제어 명령, 펌웨어 업데이트 등 다양한 바이너리 메시지가 UART, CAN, RS-485 등 신뢰성이 낮은 버스를 통해 전송됩니다.  
이때, 데이터가 손상되었는지 빠르게 검증하기 위해 CRC16-CCITT(폴리노미얼 0x1021) 체크섬이 널리 사용됩니다.

CRC16-CCITT는 ISO/IEC 3309, ITU-T V.41 등에서 표준으로 정의되어 있으며,  
많은 통신 프로토콜(예: XMODEM, PPP, BLE, Modbus 등)에서 채택하고 있습니다.

면접관이 평가하고자 하는 포인트:
- 비트/바이트 연산의 정확성
- 누적(인크리멘탈) 처리: 여러 번에 나눠서 CRC를 계산할 수 있어야 함
- 표준 테스트 벡터와의 일치
- API 설계 및 테스트 케이스 작성 능력

문제:
아래 시그니처에 따라 CRC16-CCITT(0x1021) 해시 함수를 구현하세요.
- uint16_t crc16_ccitt(const uint8_t* data, size_t len, uint16_t crc_init);

제약:
- 초기값(crc_init)을 인자로 받아 누적 계산이 가능해야 함
- 표준 폴리노미얼 0x1021, 입력 바이트당 상위 비트부터 처리
- 최종 XOR 없음, 리플렉션 없음(XMODEM/BLE 스타일)
- 테스트 코드에서 표준 벡터("123456789" 등)와 누적 처리(부분 입력)를 검증

아래는 구현 예시와 테스트 코드입니다.
*/

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

// CRC16-CCITT(0x1021), no reflection, no final XOR
uint16_t crc16_ccitt(const uint8_t* data, size_t len, uint16_t crc_init) {
    uint16_t crc = crc_init;
    for (size_t i = 0; i < len; ++i) {
        crc ^= ((uint16_t)data[i]) << 8;
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

// 테스트 코드
int main() {
    // 표준 테스트 벡터: "123456789" → CRC16-CCITT(0x1021, init=0xFFFF) = 0x29B1
    const uint8_t test1[] = "123456789";
    uint16_t crc = crc16_ccitt(test1, 9, 0xFFFF);
    printf("CRC16-CCITT(\"123456789\", 0xFFFF) = 0x%04X\n", crc);
    assert(crc == 0x29B1);

    // 누적 처리(부분 입력)
    crc = crc16_ccitt(test1, 4, 0xFFFF); // "1234"
    crc = crc16_ccitt(test1+4, 5, crc);  // "56789"
    assert(crc == 0x29B1);

    // 다른 초기값
    crc = crc16_ccitt(test1, 9, 0x0000);
    printf("CRC16-CCITT(\"123456789\", 0x0000) = 0x%04X\n", crc);
    assert(crc == 0x31C3);

    // 빈 입력
    crc = crc16_ccitt(NULL, 0, 0xFFFF);
    assert(crc == 0xFFFF);

    // 임의 데이터
    const uint8_t test2[] = {0xAB, 0xCD, 0xEF, 0x01, 0x23};
    crc = crc16_ccitt(test2, sizeof(test2), 0xFFFF);
    printf("CRC16-CCITT({AB CD EF 01 23}, 0xFFFF) = 0x%04X\n", crc);

    printf("All CRC16-CCITT tests passed!\n");
    return 0;
}