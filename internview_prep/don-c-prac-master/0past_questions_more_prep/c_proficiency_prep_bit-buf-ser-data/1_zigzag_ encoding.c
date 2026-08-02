/*
1. ZigZag + Base-128 Varint (signed 64-bit)
설명:
음/양수 모두를 효율적으로 직렬화하기 위해 ZigZag 인코딩(음수→양수 매핑) 후, Base-128 varint로 인코딩/디코딩합니다.
Google Protocol Buffers 등에서 사용되는 방식입니다.

헤더/시그니처:

*/

/*


/*
1. ZigZag + Base-128 Varint (signed 64-bit)

문제 상세 설명:
임베디드 시스템이나 네트워크 프로토콜에서 signed 64비트 정수(int64_t)를 효율적으로 직렬화(serialize)해야 할 때가 많습니다.  
특히, 값의 대부분이 0에 가깝거나 작은 음수/양수일 때, 고정 8바이트로 전송하는 것은 비효율적입니다.

이를 해결하기 위해 두 가지 기법을 결합합니다:

1) **ZigZag 인코딩**  
- 음수와 양수를 모두 양수로 변환하여, 부호 비트로 인한 비효율을 없앱니다.  
- 예시:  
  - 0 → 0  
  - -1 → 1  
  - 1 → 2  
  - -2 → 3  
  - 2 → 4  
  - ...  
- 이렇게 하면, 작은 절댓값의 음수도 작은 값으로 변환되어 이후 단계에서 효율적으로 인코딩됩니다.

2) **Base-128 Varint 인코딩**  
- 7비트씩 잘라서, 각 바이트의 MSB(최상위 비트)를 "계속됨" 플래그로 사용합니다.  
- 값이 작으면 1바이트, 값이 커질수록 바이트 수가 늘어납니다.
- 예시:  
  - 0 → [0x00]  
  - 1 → [0x01]  
  - 300 → [0xac, 0x02]  
  - 2^63-1 → 10바이트

이 방식은 Google Protocol Buffers, gRPC 등에서 널리 사용되며,  
특히 대다수 값이 작고 드물게 큰 값이 나오는 센서 데이터, 로그, 메시지 등에 매우 적합합니다.

**구현 목표:**  
- int64_t 값을 ZigZag 인코딩 후, Base-128 Varint로 직렬화/역직렬화하는 함수를 작성하세요.
- 함수 시그니처는 아래와 같습니다:
    int encode_zigzag_varint(int64_t value, uint8_t* out); // returns bytes written
    int decode_zigzag_varint(const uint8_t* in, int64_t* value); // returns bytes read
- out/in 버퍼는 충분히 크다고 가정합니다.
- 반환값은 실제로 사용/읽은 바이트 수입니다.
*/

#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

// ZigZag 인코딩: 음수→양수 매핑
static uint64_t zigzag_encode(int64_t value) {
    return (uint64_t)((value << 1) ^ (value >> 63));
}

// ZigZag 디코딩: 양수→원래 부호
static int64_t zigzag_decode(uint64_t value) {
    return (int64_t)((value >> 1) ^ (~(value & 1) + 1));
}

// Base-128 Varint 인코딩
int encode_zigzag_varint(int64_t value, uint8_t* out) {
    uint64_t uval = zigzag_encode(value);
    int i = 0;
    do {
        uint8_t byte = uval & 0x7F;
        uval >>= 7;
        if (uval != 0) byte |= 0x80;
        out[i++] = byte;
    } while (uval != 0);
    return i;
}

// Base-128 Varint 디코딩
int decode_zigzag_varint(const uint8_t* in, int64_t* value) {
    uint64_t result = 0;
    int shift = 0;
    int i = 0;
    while (1) {
        uint8_t byte = in[i];
        result |= ((uint64_t)(byte & 0x7F)) << shift;
        if ((byte & 0x80) == 0) break;
        shift += 7;
        i++;
    }
    *value = zigzag_decode(result);
    return i + 1;
}

// 테스트 코드
int main() {
    uint8_t buffer[16];
    int64_t test_values[] = {0, -1, 1, -123456789, 123456789, INT64_MIN, INT64_MAX};
    int num_tests = sizeof(test_values) / sizeof(test_values[0]);
    for (int t = 0; t < num_tests; ++t) {
        int64_t val = test_values[t];
        memset(buffer, 0, sizeof(buffer));
        int enc_len = encode_zigzag_varint(val, buffer);

        int64_t decoded = 0;
        int dec_len = decode_zigzag_varint(buffer, &decoded);

        printf("Test %d: value = %lld\n", t+1, (long long)val);
        printf("  Encoded bytes: ");
        for (int i = 0; i < enc_len; ++i) printf("%02x ", buffer[i]);
        printf("\n  Encoded length: %d, Decoded length: %d\n", enc_len, dec_len);
        printf("  Decoded value: %lld\n", (long long)decoded);
        assert(val == decoded);
        assert(enc_len == dec_len);
        printf("  Test Passed\n\n");
    }
    printf("All tests passed!\n");
    return 0;
}