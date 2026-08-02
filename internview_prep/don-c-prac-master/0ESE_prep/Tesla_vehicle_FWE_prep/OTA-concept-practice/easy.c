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
uint32_t calc_crc32(const uint8_t* data, size_t len);

/*
2. SHA-256 해시 계산 (API 호출)
   설명: 암호화 라이브러리 API를 사용해 데이터 블록의 SHA-256 해시를 계산합니다.
   개념: SHA-256은 데이터 위변조 방지, 인증 등에 사용되는 해시 함수입니다.
   함수 시그니처: int calc_sha256(const uint8_t* data, size_t len, uint8_t* hash_out);
   예시 입력: data = "hello", len = 5
   예시 출력: hash_out = {0x2C, 0xF2, ...} (32바이트)
*/
int calc_sha256(const uint8_t* data, size_t len, uint8_t* hash_out);

/*
3. 펌웨어 버전 비교 함수
   설명: 버전 문자열("1.2.3") 두 개를 비교해 어느 쪽이 최신인지 판별합니다.
   개념: 버전 관리는 OTA에서 업데이트 필요성 판단에 필수입니다.
   함수 시그니처: int compare_version(const char* v1, const char* v2);
   예시 입력: v1 = "1.2.3", v2 = "1.3.0"
   예시 출력: -1 (v2가 더 최신)
*/
int compare_version(const char* v1, const char* v2);

/*
4. 간단한 패킷 파서
   설명: [길이:2바이트][데이터:N바이트] 구조의 스트림에서 유효한 패킷을 추출합니다.
   개념: OTA 데이터는 패킷 단위로 전송되므로 파싱이 필요합니다.
   함수 시그니처: int parse_packet(const uint8_t* stream, size_t stream_len, uint8_t* out_data, size_t* out_len);
   예시 입력: stream = {0x00, 0x03, 0xAA, 0xBB, 0xCC}, stream_len = 5
   예시 출력: out_data = {0xAA, 0xBB, 0xCC}, out_len = 3
*/
int parse_packet(const uint8_t* stream, size_t stream_len, uint8_t* out_data, size_t* out_len);

/*
5. memcpy/memcmp 없이 메모리 복사 및 비교
   설명: 표준 라이브러리 없이 메모리 복사/비교 함수를 직접 구현합니다.
   개념: 임베디드 환경에서 라이브러리 사용이 제한될 수 있습니다.
   함수 시그니처: void my_memcpy(void* dst, const void* src, size_t len);
                 int my_memcmp(const void* a, const void* b, size_t len);
   예시 입력: src = {0x01, 0x02}, dst = {0x00, 0x00}, len = 2
   예시 출력: dst = {0x01, 0x02}
*/
void my_memcpy(void* dst, const void* src, size_t len);
int my_memcmp(const void* a, const void* b, size_t len);

/*
6. 16진수 문자열을 바이트 배열로 변환
   설명: "4A FF 01 C3"와 같은 문자열을 uint8_t 배열로 변환합니다.
   개념: 펌웨어 정보나 인증값을 사람이 읽기 쉬운 형태로 주고받을 때 사용합니다.
   함수 시그니처: int hexstr_to_bytes(const char* hexstr, uint8_t* out_bytes, size_t* out_len);
   예시 입력: hexstr = "4A FF 01 C3"
   예시 출력: out_bytes = {0x4A, 0xFF, 0x01, 0xC3}, out_len = 4
*/
int hexstr_to_bytes(const char* hexstr, uint8_t* out_bytes, size_t* out_len);

/*
7. 바이트 배열을 16진수 문자열로 변환
   설명: uint8_t 배열을 "4AFF01C3" 형태의 문자열로 변환합니다.
   개념: 바이너리 데이터를 로그, 전송, 저장 시 사람이 읽기 쉽게 표현합니다.
   함수 시그니처: void bytes_to_hexstr(const uint8_t* bytes, size_t len, char* out_str);
   예시 입력: bytes = {0x4A, 0xFF, 0x01, 0xC3}, len = 4
   예시 출력: out_str = "4AFF01C3"
*/
void bytes_to_hexstr(const uint8_t* bytes, size_t len, char* out_str);

/*
8. OTA 상태 플래그 관리 (FSM)
   설명: enum과 static 변수를 사용해 OTA 클라이언트의 상태를 관리합니다.
   개념: 상태 머신은 OTA 프로세스의 각 단계를 명확히 구분하는 데 필수입니다.
   함수 시그니처: void ota_set_state(int state); int ota_get_state(void);
   예시 입력: ota_set_state(VERIFYING); ota_get_state();
   예시 출력: VERIFYING
*/
typedef enum { IDLE, DOWNLOADING, VERIFYING, READY_TO_INSTALL } ota_state_t;
void ota_set_state(ota_state_t state);
ota_state_t ota_get_state(void);

/*
9. 파일 크기 및 블록 수 계산
   설명: 전체 펌웨어 크기와 블록당 크기가 주어졌을 때, 필요한 블록 수를 계산합니다.
   개념: OTA 전송 최적화, 전송 재개 등에 활용됩니다.
   함수 시그니처: size_t calc_block_count(size_t total_size, size_t block_size);
   예시 입력: total_size = 1025, block_size = 256
   예시 출력: 5
*/
size_t calc_block_count(size_t total_size, size_t block_size);

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
bool check_block_crc(const FirmwareBlock* block);

/*
11. 부트로더 진입 플래그 설정
    설명: 특정 메모리 주소에 매직 넘버를 써서, 다음 부팅 시 부트로더 모드로 진입하도록 요청합니다.
    개념: 안전한 펌웨어 교체를 위해 부트로더 진입이 필요합니다.
    함수 시그니처: void set_bootloader_flag(void);
    예시 입력: (함수 호출)
    예시 출력: 0x2000FF00 주소에 0xDEADBEEF 기록
*/
void set_bootloader_flag(void);

/*
12. 엔디안 변환 매크로
    설명: 32비트 정수의 엔디안을 변환하는 매크로를 작성합니다.
    개념: 네트워크/저장 장치와의 호환성을 위해 엔디안 변환이 필요합니다.
    매크로: #define SWAP_ENDIAN32(x) ...
    예시 입력: 0x12345678
    예시 출력: 0x78563412
*/
#define SWAP_ENDIAN32(x) ( ((x)>>24) | (((x)>>8)&0xFF00) | (((x)<<8)&0xFF0000) | ((x)<<24) )

/*
13. 간단한 Manifest 파일 파싱
    설명: "Version:1.2.0\nSize:1048576\nCRC:0xABCD1234" 형식의 텍스트에서 각 필드 값을 파싱합니다.
    개념: OTA 메타데이터 관리에 필수적입니다.
    함수 시그니처: int parse_manifest(const char* manifest, char* version, size_t* size, uint32_t* crc);
    예시 입력: manifest = "Version:1.2.0\nSize:1048576\nCRC:0xABCD1234"
    예시 출력: version="1.2.0", size=1048576, crc=0xABCD1234
*/
int parse_manifest(const char* manifest, char* version, size_t* size, uint32_t* crc);

/*
14. 플래시 메모리 페이지 정렬 확인
    설명: 주소와 길이가 플래시 페이지 크기(예: 256바이트)에 맞게 정렬되어 있는지 확인합니다.
    개념: 플래시 쓰기/지우기 동작의 안정성 보장에 필요합니다.
    함수 시그니처: bool is_flash_aligned(uint32_t addr, size_t len, size_t page_size);
    예시 입력: addr=0x1000, len=512, page_size=256
    예시 출력: true
*/
bool is_flash_aligned(uint32_t addr, size_t len, size_t page_size);

/*
15. 타임아웃 카운터
    설명: 특정 시간(밀리초)이 경과했는지 확인하는 소프트웨어 타임아웃 로직입니다.
    개념: OTA 통신, 다운로드 등에서 시간 초과 처리가 필요합니다.
    함수 시그니처: bool is_timeout(uint32_t start_time, uint32_t duration, uint32_t now);
    예시 입력: start_time=1000, duration=500, now=1600
    예시 출력: true
*/
bool is_timeout(uint32_t start_time, uint32_t duration, uint32_t);
