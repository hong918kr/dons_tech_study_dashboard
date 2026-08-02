/*
3. 엔디안-안전 직렬화 유틸리티 (실제 인터뷰 스타일 상세 설명)

상황 설명:
두 임베디드 시스템(예: 센서와 마이크로컨트롤러, 차량 내 ECU, IoT 디바이스 등)이 네트워크(이더넷, CAN, UART 등)나 저장장치(EEPROM, 플래시)에 데이터를 주고받거나 저장하는 상황을 가정합니다.
이때, 시스템마다 CPU의 엔디안(바이트 순서)이 다를 수 있습니다. 예를 들어, 한쪽은 Little Endian(LE), 다른 한쪽은 Big Endian(BE)일 수 있습니다.
네트워크 프로토콜(예: TCP/IP, CAN, Modbus 등)이나 바이너리 파일 포맷(예: WAV, BMP, 바이너리 로그 등)은 일반적으로 "네트워크 바이트 오더"(Big Endian)나 "리틀 엔디안" 등 명확한 바이트 순서를 요구합니다.

따라서, 임베디드/펌웨어 개발자는 다음을 정확히 구현할 수 있어야 합니다:
- 정수/실수형 데이터를 지정된 엔디안으로 직렬화(메모리→버퍼) 및 역직렬화(버퍼→메모리)
- 미스얼라인드 접근 없이 안전하게 처리
- 이식성, 경계 체크, API 명확성

면접관이 평가하고자 하는 포인트:
- 비트/바이트 연산의 정확성
- 포인터/메모리 접근 안전성
- 실수형의 비트 패턴 처리(IEEE754)
- API 설계 및 테스트 케이스 작성 능력

문제:
아래 함수들을 구현하세요.
- void write_u32_le(uint8_t* buf, uint32_t val);
- void write_u32_be(uint8_t* buf, uint32_t val);
- uint32_t read_u32_le(const uint8_t* buf);
- uint32_t read_u32_be(const uint8_t* buf);
- void write_f64_le(uint8_t* buf, double val);
- double read_f64_le(const uint8_t* buf);

제약:
- buf는 최소 4바이트(u32) 또는 8바이트(f64) 이상 할당되어 있다고 가정
- 미스얼라인드 접근 없이 바이트 단위로 처리
- 실수형은 IEEE754 64비트(double) 비트 패턴을 그대로 복사
- 테스트 코드에서 다양한 값(0, 1, 최대/최소, 음수, NaN, Inf 등)을 검증

아래는 구현 예시와 테스트 코드입니다.
*/

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>

// 32비트 정수 Little Endian 쓰기
void write_u32_le(uint8_t* buf, uint32_t val) {
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
    buf[2] = (uint8_t)((val >> 16) & 0xFF);
    buf[3] = (uint8_t)((val >> 24) & 0xFF);
}

// 32비트 정수 Big Endian 쓰기
void write_u32_be(uint8_t* buf, uint32_t val) {
    buf[0] = (uint8_t)((val >> 24) & 0xFF);
    buf[1] = (uint8_t)((val >> 16) & 0xFF);
    buf[2] = (uint8_t)((val >> 8) & 0xFF);
    buf[3] = (uint8_t)(val & 0xFF);
}

// 32비트 정수 Little Endian 읽기
uint32_t read_u32_le(const uint8_t* buf) {
    return ((uint32_t)buf[0]) |
           ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) |
           ((uint32_t)buf[3] << 24);
}

// 32비트 정수 Big Endian 읽기
uint32_t read_u32_be(const uint8_t* buf) {
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) |
           ((uint32_t)buf[3]);
}

// 64비트 실수 Little Endian 쓰기
void write_f64_le(uint8_t* buf, double val) {
    uint64_t u;
    memcpy(&u, &val, sizeof(u));
    for (int i = 0; i < 8; ++i) {
        buf[i] = (uint8_t)((u >> (8 * i)) & 0xFF);
    }
}

// 64비트 실수 Little Endian 읽기
double read_f64_le(const uint8_t* buf) {
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) {
        u |= ((uint64_t)buf[i]) << (8 * i);
    }
    double val;
    memcpy(&val, &u, sizeof(val));
    return val;
}

// 테스트 코드
int main() {
    uint8_t buf[8];

    // u32 LE/BE roundtrip
    uint32_t u32_vals[] = {0, 1, 0x12345678, 0xFFFFFFFF, 0x80000000};
    for (int i = 0; i < 5; ++i) {
        uint32_t v = u32_vals[i];
        write_u32_le(buf, v);
        assert(read_u32_le(buf) == v);
        write_u32_be(buf, v);
        assert(read_u32_be(buf) == v);
    }
    printf("u32 LE/BE roundtrip tests passed\n");

    // f64 LE roundtrip
    double f64_vals[] = {0.0, -0.0, 1.0, -1.0, 123456.789, -98765.4321, INFINITY, -INFINITY, NAN};
    for (int i = 0; i < 9; ++i) {
        double v = f64_vals[i];
        write_f64_le(buf, v);
        double r = read_f64_le(buf);
        if (isnan(v)) {
            assert(isnan(r));
        } else {
            assert(memcmp(&v, &r, sizeof(double)) == 0);
        }
    }
    printf("f64 LE roundtrip tests passed\n");

    // f64 LE/BE cross check (endianness effect)
    double d = 3.141592653589793;
    write_f64_le(buf, d);
    // 수동으로 BE로 읽으면 값이 달라야 함
    uint64_t u_le = 0, u_be = 0;
    for (int i = 0; i < 8; ++i) {
        u_le |= ((uint64_t)buf[i]) << (8 * i);
        u_be |= ((uint64_t)buf[i]) << (8 * (7 - i));
    }
    assert(u_le != u_be);
    printf("f64 LE/BE cross check passed\n");

    printf("All tests passed!\n");
    return 0;
}