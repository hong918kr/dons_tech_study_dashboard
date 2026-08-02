// 12. 가변 길이 LEB128 unsigned/signed

/*
상황 설명:
당신은 차량용 ECU 또는 IoT 디바이스의 펌웨어를 개발 중입니다. 이 장치는 다양한 센서 데이터, 이벤트 로그, 진단 정보 등을 외부 플래시, 네트워크, 또는 CAN/LIN 버스 등으로 전송해야 합니다.
이때, 데이터의 크기를 최소화하기 위해 가변 길이 정수 인코딩(LEB128, Little Endian Base 128)을 사용합니다.
예를 들어, 센서 값, 타임스탬프, 카운터, 인덱스 등은 대부분 작은 값이 많으므로, 1~2바이트로 표현할 수 있으면 전체 데이터 전송량과 저장 공간을 크게 줄일 수 있습니다.

LEB128은 unsigned(ULEB128)와 signed(SLEB128) 모두 지원해야 하며, 다음과 같은 요구가 있습니다:
- 값이 작을 때는 1바이트, 값이 커질수록 2~10바이트까지 가변적으로 인코딩
- 디코딩 시 오버플로, 잘못된 입력(바이트 수 초과, 잘못된 continuation bit 등) 검출
- 인코딩/디코딩 모두 최소 바이트 사용 보장
- CAN/LIN, BLE, 플래시 로그 등 다양한 임베디드 환경에서 활용 가능

부연 설명:
LEB128은 Google Protocol Buffers, DWARF 디버그 포맷, WebAssembly 등에서 널리 쓰이는 가변 길이 정수 인코딩 방식입니다.
임베디드에서는 메모리/대역폭이 제한적이므로, 정수값을 최소 바이트로 직렬화하는 것이 매우 중요합니다.
SLEB128은 부호 확장(sign extension)까지 정확히 처리해야 하며, ULEB128은 64비트까지 지원해야 합니다.
테스트 코드는 경계값(0, 1, -1, 최대/최소값), 오버플로, 잘못된 입력 등 다양한 케이스를 포함해야 합니다.
*/

#ifndef LEB128_H
#define LEB128_H
#include <stdint.h>
#include <stddef.h>

// ULEB128 인코딩: val을 out에 인코딩, out_len은 버퍼 크기, 반환값은 사용한 바이트 수(실패시 0)
size_t uleb128_encode(uint64_t val, uint8_t* out, size_t out_len) {
    size_t i = 0;
    do {
        if (i >= out_len) return 0; // 버퍼 초과
        uint8_t byte = val & 0x7F;
        val >>= 7;
        if (val) byte |= 0x80;
        out[i++] = byte;
    } while (val);
    return i;
}

// ULEB128 디코딩: in에서 디코딩, val에 결과 저장, 반환값은 사용한 바이트 수(실패시 -1)
int uleb128_decode(const uint8_t* in, size_t in_len, uint64_t* val) {
    uint64_t result = 0;
    int shift = 0;
    size_t i = 0;
    for (; i < in_len && i < 10; ++i) {
        uint8_t byte = in[i];
        result |= ((uint64_t)(byte & 0x7F)) << shift;
        if (!(byte & 0x80)) {
            *val = result;
            return (int)(i + 1);
        }
        shift += 7;
    }
    return -1; // 오버플로 또는 잘못된 입력
}

// SLEB128 인코딩: val을 out에 인코딩, out_len은 버퍼 크기, 반환값은 사용한 바이트 수(실패시 0)
size_t sleb128_encode(int64_t val, uint8_t* out, size_t out_len) {
    size_t i = 0;
    int more = 1;
    while (more) {
        if (i >= out_len) return 0;
        uint8_t byte = val & 0x7F;
        int sign = (byte & 0x40) != 0;
        val >>= 7;
        if ((val == 0 && !sign) || (val == -1 && sign))
            more = 0;
        else
            byte |= 0x80;
        out[i++] = byte;
    }
    return i;
}

// SLEB128 디코딩: in에서 디코딩, val에 결과 저장, 반환값은 사용한 바이트 수(실패시 -1)
int sleb128_decode(const uint8_t* in, size_t in_len, int64_t* val) {
    int64_t result = 0;
    int shift = 0;
    size_t i = 0;
    uint8_t byte;
    do {
        if (i >= in_len || i >= 10) return -1;
        byte = in[i];
        result |= ((int64_t)(byte & 0x7F)) << shift;
        shift += 7;
    } while (byte & 0x80 && ++i);

    // 부호 확장
    if ((shift < 64) && (byte & 0x40))
        result |= -((int64_t)1 << shift);

    *val = result;
    return (int)(i + 1);
}

#endif

#ifdef LEB128_TEST_MAIN
#include <stdio.h>
#include <string.h>
#include <assert.h>

void test_uleb128(uint64_t val) {
    uint8_t buf[16] = {0};
    uint64_t decoded = 0;
    size_t enc = uleb128_encode(val, buf, sizeof(buf));
    assert(enc > 0 && enc <= 10);
    int dec = uleb128_decode(buf, enc, &decoded);
    assert(dec == (int)enc);
    assert(decoded == val);
    printf("ULEB128: %llu -> [", (unsigned long long)val);
    for (size_t i = 0; i < enc; ++i) printf("%02X ", buf[i]);
    printf("] -> %llu\n", (unsigned long long)decoded);
}

void test_sleb128(int64_t val) {
    uint8_t buf[16] = {0};
    int64_t decoded = 0;
    size_t enc = sleb128_encode(val, buf, sizeof(buf));
    assert(enc > 0 && enc <= 10);
    int dec = sleb128_decode(buf, enc, &decoded);
    assert(dec == (int)enc);
    assert(decoded == val);
    printf("SLEB128: %lld -> [", (long long)val);
    for (size_t i = 0; i < enc; ++i) printf("%02X ", buf[i]);
    printf("] -> %lld\n", (long long)decoded);
}

int main(void) {
    // ULEB128 경계값 테스트
    test_uleb128(0);
    test_uleb128(1);
    test_uleb128(127);
    test_uleb128(128);
    test_uleb128(16383);
    test_uleb128(0xFFFFFFFFULL);
    test_uleb128(0xFFFFFFFFFFFFFFFFULL);

    // SLEB128 경계값 테스트
    test_sleb128(0);
    test_sleb128(1);
    test_sleb128(-1);
    test_sleb128(63);
    test_sleb128(-64);
    test_sleb128(64);
    test_sleb128(-65);
    test_sleb128(0x7FFFFFFFFFFFFFFFLL);
    test_sleb128(-0x8000000000000000LL);

    // 오버플로/잘못된 입력 테스트
    uint8_t bad[11] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint64_t val = 0;
    assert(uleb128_decode(bad, 11, &val) == -1);

    printf("LEB128: 모든 테스트 통과!\n");
    return 0;
}
#endif