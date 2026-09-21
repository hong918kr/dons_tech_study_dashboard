/*
7. TLV(Type-Length-Value) 인코더/디코더 - 실제 인터뷰 스타일 상세 설명

상황 설명:
당신은 산업용 IoT 게이트웨이의 펌웨어를 개발 중입니다.  
이 게이트웨이는 다양한 센서와 액추에이터로부터 데이터를 수집하고,  
네트워크를 통해 서버 또는 다른 장치로 데이터를 전송합니다.

이때, 센서 데이터, 명령, 설정 등 다양한 종류의 메시지를  
확장성과 유연성을 위해 TLV(Type-Length-Value) 포맷으로 직렬화합니다.  
TLV는 각 필드가 "타입(1바이트), 길이(2바이트, big-endian), 값(가변)" 구조로 되어 있어  
새로운 타입을 추가하거나, 알 수 없는 타입을 무시하는 것이 쉽고  
네트워크 프로토콜, 바이너리 설정 파일, 펌웨어 메시지 등에서 널리 사용됩니다.

면접관이 평가하고자 하는 포인트:
- 바이트 오더(big-endian) 처리의 정확성
- 경계/오버런 체크, 안전한 버퍼 접근
- 알 수 없는 타입 무시, 여러 TLV 연속 파싱 등 확장성
- API 설계 및 테스트 케이스 작성 능력

문제:
아래 시그니처에 따라 TLV 인코더/디코더를 구현하세요.
- size_t tlv_encode(uint8_t* out, const tlv_t* tlv);
- int tlv_decode(const uint8_t* in, size_t in_len, tlv_t* tlv);

제약:
- out/in 버퍼는 충분히 크다고 가정(테스트에서는 경계 체크)
- 디코드 시 in_len이 부족하면 -1 반환
- 디코드 시 value는 in 버퍼 내 포인터(복사 X)
- 여러 TLV 연속 파싱을 위해 디코드 함수는 실제 읽은 바이트 수를 반환

아래는 구현 예시와 테스트 코드입니다.
*/

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

typedef struct {
    uint8_t type;
    uint16_t length;
    const uint8_t* value;
} tlv_t;

// TLV 인코딩: out에 [type][length(2B, BE)][value] 순서로 기록, 총 길이 반환
size_t tlv_encode(uint8_t* out, const tlv_t* tlv) {
    if (!out || !tlv || (tlv->length > 0 && !tlv->value)) return 0;
    out[0] = tlv->type;
    out[1] = (uint8_t)(tlv->length >> 8);
    out[2] = (uint8_t)(tlv->length & 0xFF);
    if (tlv->length > 0) {
        memcpy(out + 3, tlv->value, tlv->length);
    }
    return 3 + tlv->length;
}

// TLV 디코딩: in에서 TLV 하나를 파싱, 부족하면 -1, 성공시 읽은 바이트 수 반환
int tlv_decode(const uint8_t* in, size_t in_len, tlv_t* tlv) {
    if (!in || !tlv || in_len < 3) return -1;
    tlv->type = in[0];
    tlv->length = ((uint16_t)in[1] << 8) | in[2];
    if (in_len < 3 + tlv->length) return -1;
    tlv->value = in + 3;
    return 3 + tlv->length;
}

// 테스트 코드
int main() {
    uint8_t buf[256];

    // 1. 기본 인코딩/디코딩
    uint8_t val1[] = {0xDE, 0xAD, 0xBE, 0xEF};
    tlv_t tlv1 = { .type = 0x01, .length = sizeof(val1), .value = val1 };
    size_t enc_len = tlv_encode(buf, &tlv1);
    assert(enc_len == 7);
    assert(buf[0] == 0x01 && buf[1] == 0x00 && buf[2] == 0x04);

    tlv_t parsed;
    int dec_len = tlv_decode(buf, enc_len, &parsed);
    assert(dec_len == 7);
    assert(parsed.type == 0x01);
    assert(parsed.length == 4);
    assert(memcmp(parsed.value, val1, 4) == 0);

    // 2. 길이 0 TLV
    tlv_t tlv2 = { .type = 0xAA, .length = 0, .value = NULL };
    enc_len = tlv_encode(buf, &tlv2);
    assert(enc_len == 3);
    dec_len = tlv_decode(buf, enc_len, &parsed);
    assert(dec_len == 3);
    assert(parsed.type == 0xAA && parsed.length == 0);

    // 3. 연속 TLV 파싱
    uint8_t val3[] = {1,2,3};
    tlv_t tlv3 = { .type = 0x10, .length = 3, .value = val3 };
    size_t off = 0;
    enc_len = tlv_encode(buf+off, &tlv1); off += enc_len;
    enc_len = tlv_encode(buf+off, &tlv3); off += enc_len;
    // 첫 번째 TLV
    dec_len = tlv_decode(buf, off, &parsed);
    assert(dec_len == 7 && parsed.type == 0x01);
    // 두 번째 TLV
    dec_len = tlv_decode(buf+7, off-7, &parsed);
    assert(dec_len == 6 && parsed.type == 0x10 && parsed.length == 3);

    // 4. 경계/오버런 체크
    assert(tlv_decode(buf, 2, &parsed) == -1); // 헤더 부족
    assert(tlv_decode(buf, 6, &parsed) == -1); // value 부족

    printf("All TLV encode/decode tests passed!\n");
    return 0;
}