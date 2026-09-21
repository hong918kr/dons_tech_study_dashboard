/*
2. 헤더 비트-패킹/언패킹 (실제 인터뷰 스타일 상세 설명)

상황 설명:
두 임베디드 시스템(예: 차량 내 ECU, 센서-게이트웨이, IoT 디바이스 등)이 네트워크 또는 저장 매체를 통해 데이터를 주고받는 상황을 가정합니다.
이때, 여러 개의 필드(버전, 타입, 플래그, 페이로드 길이, 세션 ID 등)를 하나의 64비트 워드로 압축해서 전송하거나 저장하면, 
대역폭과 저장 공간을 크게 절약할 수 있습니다.

실제 CAN, LIN, UDS, 산업용 프로토콜, 또는 플래시/EEPROM에 저장하는 로그/이벤트 헤더 등에서 이런 비트 패킹이 매우 흔하게 사용됩니다.
면접관은 아래와 같은 능력을 평가하고자 합니다:
- 비트 마스크/시프트 연산의 정확성
- 경계 조건(overflow, underflow, 범위 초과) 처리
- 구조체와 바이너리 레이아웃의 차이 이해
- API 설계 및 테스트 케이스 작성 능력

문제:
아래와 같은 필드로 구성된 헤더를 하나의 64비트 워드에 패킹/언패킹하는 함수를 작성하세요.

필드 정의:
- version:      3비트 (0~7)
- type:         5비트 (0~31)
- flags:       10비트 (0~1023)
- payload_len: 20비트 (0~1048575)
- session_id:  26비트 (0~67108863)

함수 시그니처:
    uint64_t pack_header(const header_fields_t* fields);
    void unpack_header(uint64_t packed, header_fields_t* fields);

제약:
- 각 필드는 반드시 지정된 비트폭을 넘지 않아야 하며, 오버플로가 발생하지 않도록 주의해야 합니다.
- 구조체와 패킹된 워드의 엔디안(비트 순서)은 동일하다고 가정합니다(LSB-first).
- 테스트 코드에서 다양한 경계값(최소/최대/중간값 등)을 검증하세요.

아래는 구현 예시와 테스트 코드입니다.
*/

#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

typedef struct {
    uint8_t version;      // 3 bits
    uint8_t type;         // 5 bits
    uint16_t flags;       // 10 bits
    uint32_t payload_len; // 20 bits
    uint32_t session_id;  // 26 bits
} header_fields_t;

// 비트폭 상수
#define VERSION_BITS      3
#define TYPE_BITS         5
#define FLAGS_BITS       10
#define PAYLOAD_LEN_BITS 20
#define SESSION_ID_BITS  26

// 비트 위치 상수 (LSB-first)
#define VERSION_SHIFT      0
#define TYPE_SHIFT         (VERSION_SHIFT + VERSION_BITS)
#define FLAGS_SHIFT        (TYPE_SHIFT + TYPE_BITS)
#define PAYLOAD_LEN_SHIFT  (FLAGS_SHIFT + FLAGS_BITS)
#define SESSION_ID_SHIFT   (PAYLOAD_LEN_SHIFT + PAYLOAD_LEN_BITS)

// 마스크 상수
#define VERSION_MASK      ((1U << VERSION_BITS) - 1)
#define TYPE_MASK         ((1U << TYPE_BITS) - 1)
#define FLAGS_MASK        ((1U << FLAGS_BITS) - 1)
#define PAYLOAD_LEN_MASK  ((1U << PAYLOAD_LEN_BITS) - 1)
#define SESSION_ID_MASK   ((1U << SESSION_ID_BITS) - 1)

// 패킹 함수
uint64_t pack_header(const header_fields_t* fields) {
    // 각 필드가 비트폭을 넘지 않는지 검증 (디버깅/테스트용)
    assert(fields->version     <= VERSION_MASK);
    assert(fields->type        <= TYPE_MASK);
    assert(fields->flags       <= FLAGS_MASK);
    assert(fields->payload_len <= PAYLOAD_LEN_MASK);
    assert(fields->session_id  <= SESSION_ID_MASK);

    uint64_t packed = 0;
    packed |= ((uint64_t)(fields->version    & VERSION_MASK))     << VERSION_SHIFT;
    packed |= ((uint64_t)(fields->type       & TYPE_MASK))        << TYPE_SHIFT;
    packed |= ((uint64_t)(fields->flags      & FLAGS_MASK))       << FLAGS_SHIFT;
    packed |= ((uint64_t)(fields->payload_len & PAYLOAD_LEN_MASK))<< PAYLOAD_LEN_SHIFT;
    packed |= ((uint64_t)(fields->session_id & SESSION_ID_MASK))  << SESSION_ID_SHIFT;
    return packed;
}

// 언패킹 함수
void unpack_header(uint64_t packed, header_fields_t* fields) {
    fields->version     = (packed >> VERSION_SHIFT)     & VERSION_MASK;
    fields->type        = (packed >> TYPE_SHIFT)        & TYPE_MASK;
    fields->flags       = (packed >> FLAGS_SHIFT)       & FLAGS_MASK;
    fields->payload_len = (packed >> PAYLOAD_LEN_SHIFT) & PAYLOAD_LEN_MASK;
    fields->session_id  = (packed >> SESSION_ID_SHIFT)  & SESSION_ID_MASK;
}

// 테스트 코드
int main() {
    header_fields_t fields, unpacked;
    uint64_t packed;

    // Test 1: All zeros
    memset(&fields, 0, sizeof(fields));
    packed = pack_header(&fields);
    unpack_header(packed, &unpacked);
    assert(memcmp(&fields, &unpacked, sizeof(fields)) == 0);
    printf("Test 1 (all zeros) passed\n");

    // Test 2: All max values
    fields.version     = VERSION_MASK;
    fields.type        = TYPE_MASK;
    fields.flags       = FLAGS_MASK;
    fields.payload_len = PAYLOAD_LEN_MASK;
    fields.session_id  = SESSION_ID_MASK;
    packed = pack_header(&fields);
    unpack_header(packed, &unpacked);
    assert(memcmp(&fields, &unpacked, sizeof(fields)) == 0);
    printf("Test 2 (all max) passed\n");

    // Test 3: Alternating bits
    fields.version     = 0b101;
    fields.type        = 0b11010;
    fields.flags       = 0b1010101010;
    fields.payload_len = 0xABCDE;
    fields.session_id  = 0x1234567;
    packed = pack_header(&fields);
    unpack_header(packed, &unpacked);
    assert(memcmp(&fields, &unpacked, sizeof(fields)) == 0);
    printf("Test 3 (alternating) passed\n");

    // Test 4: Edge values
    fields.version     = 0;
    fields.type        = TYPE_MASK;
    fields.flags       = 0;
    fields.payload_len = PAYLOAD_LEN_MASK;
    fields.session_id  = 0;
    packed = pack_header(&fields);
    unpack_header(packed, &unpacked);
    assert(memcmp(&fields, &unpacked, sizeof(fields)) == 0);
    printf("Test 4 (edge) passed\n");

    // Test 5: Random values
    fields.version     = 2;
    fields.type        = 17;
    fields.flags       = 512;
    fields.payload_len = 123456;
    fields.session_id  = 654321;
    packed = pack_header(&fields);
    unpack_header(packed, &unpacked);
    assert(memcmp(&fields, &unpacked, sizeof(fields)) == 0);
    printf("Test 5 (random) passed\n");

    printf("All test cases passed!\n");
    return 0;
}