/*
FOTA vs. SOTA: 무엇이 다른가?
OTA는 크게 두 가지 유형으로 나뉩니다.
FOTA (Firmware-Over-the-Air): 펌웨어 전체를 업데이트하는 것으로, 마이크로컨트롤러의 운영체제(OS), 드라이버, 부트로더 등 저수준(low-level) 소프트웨어를 대상으로 합니다. FOTA는 ECU의 핵심 동작을 변경하므로 실패 시 "벽돌(bricking)" 현상을 유발할 수 있어 매우 높은 수준의 안정성과 보안이 요구됩니다.
SOTA (Software-Over-the-Air): 특정 애플리케이션이나 기능 소프트웨어만 업데이트합니다. 인포테인먼트 시스템의 내비게이션 앱이나 자율주행 알고리즘의 일부를 개선하는 경우가 해당됩니다. FOTA에 비해 상대적으로 위험 부담이 적고 더 빈번하게 수행될 수 있습니다.
본 보고서는 이 두 가지 개념을 모두 아우르며, OTA 시스템을 구축하는 데 필요한 핵심 로직을 C언어로 구현하는 능력을 기를 수 있도록 단계별 문제들을 제시합니다.

난이도: Easy (기초 개념 및 기본 블록 구현)
이 단계는 OTA 프로세스의 각 단계를 구성하는 기본적인 데이터 처리 및 검증 로직을 구현하는 데 중점을 둡니다. C언어의 기본기와 데이터 무결성의 중요성을 이해하는 것이 핵심입니다.

1. CRC32 체크섬 계산: 주어진 데이터 블록의 CRC32 체크섬을 계산하는 함수를 작성하세요. 이는 다운로드된 데이터의 무결성을 검증하는 가장 기본적인 방법입니다.
2. SHA-256 해시 계산 (API 호출): 암호화 라이브러리(mbedTLS, OpenSSL 등)의 API를 호출하여 데이터 블록의 SHA-256 해시를 계산하는 래퍼(wrapper) 함수를 작성하세요.
3. 펌웨어 버전 비교: "1.2.3"과 같은 버전 문자열 두 개를 비교하여 어느 쪽이 더 최신 버전인지 판별하는 함수를 구현하세요.
4. 간단한 패킷 파서: [길이:2바이트][데이터:N바이트] 구조를 가진 데이터 패킷 스트림에서 유효한 패킷을 추출하는 함수를 작성하세요.
5. 메모리 복사 및 비교: memcpy와 memcmp를 사용하지 않고, 주어진 두 메모리 영역을 복사하거나 비교하는 함수를 직접 구현하세요.
6. 16진수 문자열을 바이트 배열로 변환: "4A FF 01 C3"와 같은 16진수 문자열을 uint8_t 배열로 변환하는 함수를 작성하세요.
7. 바이트 배열을 16진수 문자열로 변환: uint8_t 배열을 "4AFF01C3" 형태의 16진수 문자열로 변환하는 함수를 작성하세요.
8. 업데이트 상태 플래그 관리: enum과 static 변수를 사용하여 OTA 클라이언트의 현재 상태(IDLE, DOWNLOADING, VERIFYING, READY_TO_INSTALL)를 관리하는 간단한 상태 머신(FSM)을 구현하세요.
9. 파일 크기 및 블록 수 계산: 전체 펌웨어 크기와 블록당 크기가 주어졌을 때, 총 몇 개의 블록으로 나누어 전송해야 하는지 계산하는 함수를 작성하세요.
10. 데이터 블록 유효성 검사: 펌웨어 데이터 블록 구조체(struct FirmwareBlock { uint32_t block_index; uint16_t data_length; uint8_t data; uint32_t crc; };)를 정의하고, 수신된 블록의 CRC가 유효한지 검사하는 함수를 작성하세요.
11. 부트로더 진입 플래그 설정: 특정 메모리 주소(예: 0x2000_FF00)에 매직 넘버(Magic Number)를 써서, 다음 부팅 시 부트로더 모드로 진입하도록 요청하는 함수를 작성하세요.
12. 엔디안 변환 매크로: 32비트 정수의 엔디안을 변환하는 SWAP_ENDIAN32 매크로를 작성하세요.
13. 간단한 Manifest 파일 파싱: "Version:1.2.0\nSize:1048576\nCRC:0xABCD1234" 형식의 간단한 텍스트 기반 Manifest 파일에서 각 필드의 값을 파싱하는 함수를 작성하세요.
14. 플래시 메모리 페이지 정렬 확인: 주어진 주소와 길이가 플래시 메모리의 페이지 크기(예: 256바이트)에 맞게 정렬되어 있는지 확인하는 함수를 작성하세요.
15. 타임아웃 카운터: 특정 시간(밀리초 단위)이 경과했는지 확인하는 간단한 소프트웨어 타임아웃 로직을 구현하세요. (예: is_timeout(uint32_t start_time, uint32_t duration))
*/

/*
FOTA vs. SOTA: 무엇이 다른가?
OTA는 크게 두 가지 유형으로 나뉩니다.
FOTA (Firmware-Over-the-Air): 펌웨어 전체를 업데이트하는 것으로, 마이크로컨트롤러의 운영체제(OS), 드라이버, 부트로더 등 저수준(low-level) 소프트웨어를 대상으로 합니다. FOTA는 ECU의 핵심 동작을 변경하므로 실패 시 "벽돌(bricking)" 현상을 유발할 수 있어 매우 높은 수준의 안정성과 보안이 요구됩니다.
SOTA (Software-Over-the-Air): 특정 애플리케이션이나 기능 소프트웨어만 업데이트합니다. 인포테인먼트 시스템의 내비게이션 앱이나 자율주행 알고리즘의 일부를 개선하는 경우가 해당됩니다. FOTA에 비해 상대적으로 위험 부담이 적고 더 빈번하게 수행될 수 있습니다.
*/

/*
FOTA vs. SOTA: 무엇이 다른가?
OTA는 크게 두 가지 유형으로 나뉩니다.
FOTA (Firmware-Over-the-Air): 펌웨어 전체를 업데이트하는 것으로, 마이크로컨트롤러의 운영체제(OS), 드라이버, 부트로더 등 저수준(low-level) 소프트웨어를 대상으로 합니다. FOTA는 ECU의 핵심 동작을 변경하므로 실패 시 "벽돌(bricking)" 현상을 유발할 수 있어 매우 높은 수준의 안정성과 보안이 요구됩니다.
SOTA (Software-Over-the-Air): 특정 애플리케이션이나 기능 소프트웨어만 업데이트합니다. 인포테인먼트 시스템의 내비게이션 앱이나 자율주행 알고리즘의 일부를 개선하는 경우가 해당됩니다. FOTA에 비해 상대적으로 위험 부담이 적고 더 빈번하게 수행될 수 있습니다.
본 보고서는 이 두 가지 개념을 모두 아우르며, OTA 시스템을 구축하는 데 필요한 핵심 로직을 C언어로 구현하는 능력을 기를 수 있도록 단계별 문제들을 제시합니다.

난이도: Easy (기초 개념 및 기본 블록 구현)
이 단계는 OTA 프로세스의 각 단계를 구성하는 기본적인 데이터 처리 및 검증 로직을 구현하는 데 중점을 둡니다. C언어의 기본기와 데이터 무결성의 중요성을 이해하는 것이 핵심입니다.
*/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/*
1. CRC32 체크섬 계산 함수
   설명: 데이터 블록의 무결성을 검증하기 위해 CRC32 체크섬을 계산합니다.
   개념: CRC는 데이터 전송/저장 시 오류 검출에 널리 사용되는 해시 함수입니다.
   함수 시그니처: uint32_t calc_crc32(const uint8_t* data, size_t len);
   예시 입력: data = {0x01, 0x02, 0x03, 0x04}, len = 4
   예시 출력: 0xB63CFBCD
*/
uint32_t calc_crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

/*
2. SHA-256 해시 계산 (API 호출)
   설명: 암호화 라이브러리 API를 사용해 데이터 블록의 SHA-256 해시를 계산합니다.
   개념: SHA-256은 데이터 위변조 방지, 인증 등에 사용되는 해시 함수입니다.
   함수 시그니처: int calc_sha256(const uint8_t* data, size_t len, uint8_t* hash_out);
   예시 입력: data = "hello", len = 5
   예시 출력: hash_out = {0x2C, 0xF2, ...} (32바이트)
*/
int calc_sha256(const uint8_t* data, size_t len, uint8_t* hash_out) {
    // 실제 환경에서는 라이브러리 함수 호출 필요
    // 예시: mbedtls_sha256_ret(data, len, hash_out, 0);
    // 여기선 더미값
    for (int i = 0; i < 32; ++i) hash_out[i] = (uint8_t)(i + len);
    return 0;
}

/*
3. 펌웨어 버전 비교 함수
   설명: 버전 문자열("1.2.3") 두 개를 비교해 어느 쪽이 최신인지 판별합니다.
   개념: 버전 관리는 OTA에서 업데이트 필요성 판단에 필수입니다.
   함수 시그니처: int compare_version(const char* v1, const char* v2);
   예시 입력: v1 = "1.2.3", v2 = "1.3.0"
   예시 출력: -1 (v2가 더 최신)
*/
int compare_version(const char* v1, const char* v2) {
    int a1, a2, a3, b1, b2, b3;
    a2 = a3 = b2 = b3 = 0;
    sscanf(v1, "%d.%d.%d", &a1, &a2, &a3);
    sscanf(v2, "%d.%d.%d", &b1, &b2, &b3);
    if (a1 != b1) return (a1 > b1) ? 1 : -1;
    if (a2 != b2) return (a2 > b2) ? 1 : -1;
    if (a3 != b3) return (a3 > b3) ? 1 : -1;
    return 0;
}

/*
4. 간단한 패킷 파서
   설명: [길이:2바이트][데이터:N바이트] 구조의 스트림에서 유효한 패킷을 추출합니다.
   개념: OTA 데이터는 패킷 단위로 전송되므로 파싱이 필요합니다.
   함수 시그니처: int parse_packet(const uint8_t* stream, size_t stream_len, uint8_t* out_data, size_t* out_len);
   예시 입력: stream = {0x00, 0x03, 0xAA, 0xBB, 0xCC}, stream_len = 5
   예시 출력: out_data = {0xAA, 0xBB, 0xCC}, out_len = 3
*/
int parse_packet(const uint8_t* stream, size_t stream_len, uint8_t* out_data, size_t* out_len) {
    if (stream_len < 2) return -1;
    uint16_t len = (stream[0] << 8) | stream[1];
    if (stream_len < 2 + len) return -2;
    memcpy(out_data, stream + 2, len);
    *out_len = len;
    return 0;
}

/*
5. memcpy/memcmp 없이 메모리 복사 및 비교
   설명: 표준 라이브러리 없이 메모리 복사/비교 함수를 직접 구현합니다.
   개념: 임베디드 환경에서 라이브러리 사용이 제한될 수 있습니다.
   함수 시그니처: void my_memcpy(void* dst, const void* src, size_t len);
                 int my_memcmp(const void* a, const void* b, size_t len);
   예시 입력: src = {0x01, 0x02}, dst = {0x00, 0x00}, len = 2
   예시 출력: dst = {0x01, 0x02}
*/
void my_memcpy(void* dst, const void* src, size_t len) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < len; ++i) d[i] = s[i];
}
int my_memcmp(const void* a, const void* b, size_t len) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    for (size_t i = 0; i < len; ++i) {
        if (pa[i] != pb[i]) return pa[i] - pb[i];
    }
    return 0;
}

/*
6. 16진수 문자열을 바이트 배열로 변환
   설명: "4A FF 01 C3"와 같은 문자열을 uint8_t 배열로 변환합니다.
   개념: 펌웨어 정보나 인증값을 사람이 읽기 쉬운 형태로 주고받을 때 사용합니다.
   함수 시그니처: int hexstr_to_bytes(const char* hexstr, uint8_t* out_bytes, size_t* out_len);
   예시 입력: hexstr = "4A FF 01 C3"
   예시 출력: out_bytes = {0x4A, 0xFF, 0x01, 0xC3}, out_len = 4
*/
int hexchar_to_val(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    return -1;
}
int hexstr_to_bytes(const char* hexstr, uint8_t* out_bytes, size_t* out_len) {
    size_t len = 0;
    while (*hexstr) {
        while (*hexstr == ' ') ++hexstr;
        if (!*hexstr) break;
        int hi = hexchar_to_val(*hexstr++);
        if (*hexstr == 0) return -1;
        int lo = hexchar_to_val(*hexstr++);
        if (hi < 0 || lo < 0) return -1;
        out_bytes[len++] = (hi << 4) | lo;
    }
    *out_len = len;
    return 0;
}

/*
7. 바이트 배열을 16진수 문자열로 변환
   설명: uint8_t 배열을 "4AFF01C3" 형태의 문자열로 변환합니다.
   개념: 바이너리 데이터를 로그, 전송, 저장 시 사람이 읽기 쉽게 표현합니다.
   함수 시그니처: void bytes_to_hexstr(const uint8_t* bytes, size_t len, char* out_str);
   예시 입력: bytes = {0x4A, 0xFF, 0x01, 0xC3}, len = 4
   예시 출력: out_str = "4AFF01C3"
*/
void bytes_to_hexstr(const uint8_t* bytes, size_t len, char* out_str) {
    static const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < len; ++i) {
        out_str[2*i] = hex[(bytes[i] >> 4) & 0xF];
        out_str[2*i+1] = hex[bytes[i] & 0xF];
    }
    out_str[2*len] = 0;
}

/*
8. OTA 상태 플래그 관리 (FSM)
   설명: enum과 static 변수를 사용해 OTA 클라이언트의 상태를 관리합니다.
   개념: 상태 머신은 OTA 프로세스의 각 단계를 명확히 구분하는 데 필수입니다.
   함수 시그니처: void ota_set_state(int state); int ota_get_state(void);
   예시 입력: ota_set_state(VERIFYING); ota_get_state();
   예시 출력: VERIFYING
*/
typedef enum { IDLE, DOWNLOADING, VERIFYING, READY_TO_INSTALL } ota_state_t;
static ota_state_t g_ota_state = IDLE;
void ota_set_state(ota_state_t state) { g_ota_state = state; }
ota_state_t ota_get_state(void) { return g_ota_state; }

/*
9. 파일 크기 및 블록 수 계산
   설명: 전체 펌웨어 크기와 블록당 크기가 주어졌을 때, 필요한 블록 수를 계산합니다.
   개념: OTA 전송 최적화, 전송 재개 등에 활용됩니다.
   함수 시그니처: size_t calc_block_count(size_t total_size, size_t block_size);
   예시 입력: total_size = 1025, block_size = 256
   예시 출력: 5
*/
size_t calc_block_count(size_t total_size, size_t block_size) {
    return (total_size + block_size - 1) / block_size;
}

/*
10. 데이터 블록 유효성 검사
    설명: 펌웨어 데이터 블록 구조체를 정의하고, 수신된 블록의 CRC가 유효한지 검사합니다.
    개념: OTA 데이터 무결성 보장에 필수적입니다.
    함수 시그니처:
        typedef struct {
            uint32_t block_index;
            uint16_t data_length;
            uint8_t data[256];
            uint32_t crc;
        } FirmwareBlock;
        bool check_block_crc(const FirmwareBlock* block);
    예시 입력: block.data = {0x01, 0x02, 0x03}, block.data_length = 3, block.crc = CRC32({0x01,0x02,0x03})
    예시 출력: true
*/
typedef struct {
    uint32_t block_index;
    uint16_t data_length;
    uint8_t data[256];
    uint32_t crc;
} FirmwareBlock;
bool check_block_crc(const FirmwareBlock* block) {
    uint32_t crc = calc_crc32(block->data, block->data_length);
    return crc == block->crc;
}

/*
11. 부트로더 진입 플래그 설정
    설명: 특정 메모리 주소에 매직 넘버를 써서, 다음 부팅 시 부트로더 모드로 진입하도록 요청합니다.
    개념: 안전한 펌웨어 교체를 위해 부트로더 진입이 필요합니다.
    함수 시그니처: void set_bootloader_flag(void);
    예시 입력: (함수 호출)
    예시 출력: 0x2000FF00 주소에 0xDEADBEEF 기록
*/
void set_bootloader_flag(void) {
    volatile uint32_t* flag_addr = (uint32_t*)0x2000FF00;
    *flag_addr = 0xDEADBEEF;
}

/*
12. 엔디안 변환 매크로
    설명: 32비트 정수의 엔디안을 변환하는 매크로를 작성합니다.
    개념: 네트워크/저장 장치와의 호환성을 위해 엔디안 변환이 필요합니다.
    매크로: #define SWAP_ENDIAN32(x) ...
    예시 입력: 0x12345678
    예시 출력: 0x78563412
*/
//#define SWAP_ENDIAN32(x) ( ((x)>>24) | (((x)>>8)&0xFF00) | (((x)<<8)&0xFF0000) | ((x)<<24) )
// 경고 해결: (x)를 uint32_t로 캐스팅해서 오버플로 방지
/*
설명:

왼쪽 시프트 연산에서 오버플로 경고가 발생하는 이유는,
32비트 정수에서 24비트 이상 시프트하면 int(32비트) 범위를 넘을 수 있기 때문입니다.
모든 시프트 연산에서 (uint32_t)로 캐스팅하여 경고를 방지합니다.
각 비트 필드를 명확히 마스킹하여 안전하게 엔디안 변환이 됩니다.
이제 경고 없이 동작합니다!
*/
#define SWAP_ENDIAN32(x) ( \
    (((uint32_t)(x) >> 24) & 0x000000FF) | \
    (((uint32_t)(x) >> 8)  & 0x0000FF00) | \
    (((uint32_t)(x) << 8)  & 0x00FF0000) | \
    (((uint32_t)(x) << 24) & 0xFF000000) )


/*
13. 간단한 Manifest 파일 파싱
    설명: "Version:1.2.0\nSize:1048576\nCRC:0xABCD1234" 형식의 텍스트에서 각 필드 값을 파싱합니다.
    개념: OTA 메타데이터 관리에 필수적입니다.
    함수 시그니처: int parse_manifest(const char* manifest, char* version, size_t* size, uint32_t* crc);
    예시 입력: manifest = "Version:1.2.0\nSize:1048576\nCRC:0xABCD1234"
    예시 출력: version="1.2.0", size=1048576, crc=0xABCD1234
*/
int parse_manifest(const char* manifest, char* version, size_t* size, uint32_t* crc) {
    int ret = sscanf(manifest, "Version:%[^\n]\nSize:%zu\nCRC:0x%X", version, size, crc);
    return (ret == 3) ? 0 : -1;
}

/*
14. 플래시 메모리 페이지 정렬 확인
    설명: 주소와 길이가 플래시 페이지 크기(예: 256바이트)에 맞게 정렬되어 있는지 확인합니다.
    개념: 플래시 쓰기/지우기 동작의 안정성 보장에 필요합니다.
    함수 시그니처: bool is_flash_aligned(uint32_t addr, size_t len, size_t page_size);
    예시 입력: addr=0x1000, len=512, page_size=256
    예시 출력: true
*/
bool is_flash_aligned(uint32_t addr, size_t len, size_t page_size) {
    return (addr % page_size == 0) && (len % page_size == 0);
}

/*
15. 타임아웃 카운터
    설명: 특정 시간(밀리초)이 경과했는지 확인하는 소프트웨어 타임아웃 로직입니다.
    개념: OTA 통신, 다운로드 등에서 시간 초과 처리가 필요합니다.
    함수 시그니처: bool is_timeout(uint32_t start_time, uint32_t duration, uint32_t now);
    예시 입력: start_time=1000, duration=500, now=1600
    예시 출력: true
*/
bool is_timeout(uint32_t start_time, uint32_t duration, uint32_t now) {
    return (now - start_time) >= duration;
}

// ------------------- Test code -------------------
#define LABEL_WIDTH 40
void test_result(const char* label, int ok) {
    printf("%-*s %s\n", LABEL_WIDTH, label, ok ? "[PASS]" : "[FAIL]");
}

int main(void) {
    // 1. CRC32
    uint8_t d1[] = {0x01, 0x02, 0x03, 0x04};
    test_result("1. CRC32", calc_crc32(d1, 4) == 0xB63CFBCD);

    // 2. SHA-256 (더미)
    uint8_t hash[32];
    calc_sha256((const uint8_t*)"hello", 5, hash);
    test_result("2. SHA-256 (dummy)", hash[0] == 5 && hash[31] == 36);

    // 3. 버전 비교
    test_result("3. compare_version", compare_version("1.2.3", "1.3.0") == -1);

    // 4. 패킷 파서
    uint8_t stream[] = {0x00, 0x03, 0xAA, 0xBB, 0xCC};
    uint8_t out_data[10]; size_t out_len = 0;
    test_result("4. parse_packet", parse_packet(stream, 5, out_data, &out_len) == 0 && out_len == 3 && out_data[0] == 0xAA);

    // 5. memcpy/memcmp
    uint8_t src[] = {0x01, 0x02}, dst[] = {0x00, 0x00};
    my_memcpy(dst, src, 2);
    test_result("5. my_memcpy", dst[0] == 0x01 && dst[1] == 0x02);
    test_result("5. my_memcmp", my_memcmp(src, dst, 2) == 0);

    // 6. hexstr_to_bytes
    uint8_t bytes[4]; size_t blen = 0;
    test_result("6. hexstr_to_bytes", hexstr_to_bytes("4A FF 01 C3", bytes, &blen) == 0 && blen == 4 && bytes[1] == 0xFF);

    // 7. bytes_to_hexstr
    char hexstr[9];
    bytes_to_hexstr(bytes, 4, hexstr);
    test_result("7. bytes_to_hexstr", strcmp(hexstr, "4AFF01C3") == 0);

    // 8. OTA 상태 플래그
    ota_set_state(VERIFYING);
    test_result("8. ota_set_state/get_state", ota_get_state() == VERIFYING);

    // 9. 블록 수 계산
    test_result("9. calc_block_count", calc_block_count(1025, 256) == 5);

    // 10. 데이터 블록 유효성 검사
    FirmwareBlock block = {0, 3, {0x01, 0x02, 0x03}, 0};
    block.crc = calc_crc32(block.data, block.data_length);
    test_result("10. check_block_crc", check_block_crc(&block));

    // 11. 부트로더 플래그 (실제 메모리 쓰기이므로 테스트 생략)
    // set_bootloader_flag();

    // 12. 엔디안 변환
    test_result("12. SWAP_ENDIAN32", SWAP_ENDIAN32(0x12345678) == 0x78563412);

    // 13. Manifest 파싱
    char version[16]; size_t size = 0; uint32_t crc = 0;
    test_result("13. parse_manifest", parse_manifest("Version:1.2.0\nSize:1048576\nCRC:0xABCD1234", version, &size, &crc) == 0 && strcmp(version, "1.2.0") == 0 && size == 1048576 && crc == 0xABCD1234);

    // 14. 플래시 정렬
    test_result("14. is_flash_aligned", is_flash_aligned(0x1000, 512, 256));

    // 15. 타임아웃
    test_result("15. is_timeout", is_timeout(1000, 500, 1600));

    return 0;
}