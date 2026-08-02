/*
8. COBS(Consistent Overhead Byte Stuffing) 프레이밍 - 실제 인터뷰 스타일 상세 설명

상황 설명:
당신은 산업용 센서 네트워크의 시리얼 통신 프로토콜을 개발 중입니다.
UART, RS-485, CAN 등에서 패킷 경계를 명확히 구분하기 위해 "프레임 구분자"로 0x00 바이트를 사용합니다.
하지만, 실제 데이터에도 0x00 바이트가 포함될 수 있으므로,  
데이터 내의 모든 0x00을 안전하게 치환하여 전송하고, 수신 측에서 원래 데이터로 복원해야 합니다.

COBS(Consistent Overhead Byte Stuffing)는 이런 상황에서 널리 쓰이는 알고리즘입니다.
- 입력 데이터에 0x00이 몇 개 있든, 출력 프레임에는 0x00이 오직 프레임 구분자로만 등장합니다.
- 인코딩 시, 0x00을 "길이 코드"로 치환하고, 복원 시 다시 0x00으로 되돌립니다.
- 오버헤드는 데이터 길이에 비례하며, 구현이 간단하고 실시간성이 뛰어납니다.

면접관이 평가하고자 하는 포인트:
- 바이트 단위 인코딩/디코딩 로직의 정확성
- 경계/오버런 처리, 입력/출력 버퍼 관리
- 0x00이 없는 경우, 연속된 0x00이 많은 경우 등 다양한 케이스 처리
- API 설계 및 테스트 케이스 작성 능력

문제:
아래 시그니처에 따라 COBS 인코더/디코더를 구현하세요.
- size_t cobs_encode(const uint8_t* input, size_t length, uint8_t* output);
- size_t cobs_decode(const uint8_t* input, size_t length, uint8_t* output);

제약:
- output 버퍼는 input보다 1바이트 이상 크게 할당되어 있다고 가정
- 디코드 시 잘못된 입력(코드 바이트가 0이거나, 길이 초과 등)은 0을 반환
- 테스트 코드에서 다양한 입력(0x00 없음, 0x00만, 앞/뒤/중간 0x00, 최대 길이 등) 검증

아래는 구현 예시와 테스트 코드입니다.
*/

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

// COBS 인코딩
size_t cobs_encode(const uint8_t* input, size_t length, uint8_t* output) {
    if (!input || !output) return 0;
    size_t read = 0, write = 1, code_pos = 0;
    uint8_t code = 1;
    while (read < length) {
        if (input[read] == 0) {
            output[code_pos] = code;
            code_pos = write++;
            code = 1;
            read++;
        } else {
            output[write++] = input[read++];
            code++;
            if (code == 0xFF) {
                output[code_pos] = code;
                code_pos = write++;
                code = 1;
            }
        }
    }
    output[code_pos] = code;
    return write;
}

// COBS 디코딩
size_t cobs_decode(const uint8_t* input, size_t length, uint8_t* output) {
    if (!input || !output || length == 0) return 0;
    size_t read = 0, write = 0;
    while (read < length) {
        uint8_t code = input[read++];
        if (code == 0 || read + code - 1 > length + 1) return 0;
        for (uint8_t i = 1; i < code; ++i) {
            if (read >= length) return 0;
            output[write++] = input[read++];
        }
        if (code != 0xFF && read < length) {
            output[write++] = 0;
        }
    }
    return write;
}

// 테스트 코드
int main() {
    uint8_t input[256], encoded[300], decoded[256];
    size_t enc_len, dec_len;

    // 1. 0x00 없는 데이터
    uint8_t test1[] = {1,2,3,4,5};
    enc_len = cobs_encode(test1, 5, encoded);
    dec_len = cobs_decode(encoded, enc_len, decoded);
    assert(dec_len == 5 && memcmp(test1, decoded, 5) == 0);

    // 2. 0x00만 있는 데이터
    uint8_t test2[] = {0,0,0};
    enc_len = cobs_encode(test2, 3, encoded);
    dec_len = cobs_decode(encoded, enc_len, decoded);
    assert(dec_len == 3 && memcmp(test2, decoded, 3) == 0);

    // 3. 앞/뒤/중간 0x00
    uint8_t test3[] = {0,1,0,2,3,0};
    enc_len = cobs_encode(test3, 6, encoded);
    dec_len = cobs_decode(encoded, enc_len, decoded);
    assert(dec_len == 6 && memcmp(test3, decoded, 6) == 0);

    // 4. 최대 길이(0xFF-1 연속)
    for (int i = 0; i < 254; ++i) input[i] = i+1;
    enc_len = cobs_encode(input, 254, encoded);
    dec_len = cobs_decode(encoded, enc_len, decoded);
    assert(dec_len == 254 && memcmp(input, decoded, 254) == 0);

    // 5. 잘못된 입력(코드 바이트 0)
    encoded[0] = 0;
    assert(cobs_decode(encoded, 5, decoded) == 0);

    printf("All COBS encode/decode tests passed!\n");
    return 0;
}