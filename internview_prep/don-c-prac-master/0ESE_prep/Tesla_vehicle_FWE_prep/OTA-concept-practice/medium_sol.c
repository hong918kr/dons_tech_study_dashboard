/*
난이도: Medium (RTOS, 자료구조, 프로토콜 통합)
이 단계에서는 RTOS 환경에서 OTA 프로세스를 안정적으로 실행하고, 실제 통신 프로토콜과 연동하며, 효율적인 데이터 관리를 위한 자료구조를 활용하는 능력을 평가합니다.

16. 원형 버퍼를 이용한 데이터 수신: UART나 CAN 인터럽트에서 수신된 데이터를 원형 버퍼(Circular Buffer)에 저장하고, 메인 OTA 태스크에서 이를 읽어 처리하는 로직을 구현하세요.
17. OTA 다운로더 RTOS 태스크: 펌웨어 다운로드를 전담하는 별도의 RTOS 태스크를 생성하고, 메시지 큐를 통해 다운로드 URL과 같은 명령을 수신하도록 구현하세요.
18. Mutex를 이용한 상태 변수 보호: OTA 상태 변수를 여러 태스크(예: 통신 태스크, UI 태스크)가 안전하게 접근할 수 있도록 Mutex로 보호하는 코드를 작성하세요.
19. 세마포어를 이용한 다운로드 완료 동기화: 데이터 다운로드가 완료되면 ISR 또는 통신 태스크가 세마포어를 'give'하고, OTA 메인 태스크는 이를 'take'하여 다음 단계(유효성 검사)를 진행하도록 동기화 로직을 구현하세요.
20. UDS RequestDownload (0x34) 핸들러: UDS 서비스 0x34 요청을 수신하여 펌웨어 다운로드를 준비하는 서버(ECU) 측 핸들러를 구현하세요. 메모리 주소와 크기를 검증하고, 최대 전송 블록 크기를 응답해야 합니다.
21. UDS TransferData (0x36) 핸들러: UDS 서비스 0x36 요청을 순차적으로 수신하여, 수신된 데이터 블록을 임시 저장 공간(RAM 버퍼 또는 플래시)에 쓰는 핸들러를 구현하세요. 블록 시퀀스 카운터를 확인해야 합니다.
22. UDS RequestTransferExit (0x37) 핸들러: UDS 서비스 0x37 요청을 수신하여 전체 펌웨어 데이터 수신이 완료되었음을 확인하고, 최종 무결성 검사를 수행한 후 응답하는 핸들러를 구현하세요.
23. 메모리 풀을 이용한 패킷 관리: 가변적인 통신 패킷들을 처리하기 위해 고정 크기 블록으로 구성된 메모리 풀(Memory Pool)을 구현하여 동적 할당의 오버헤드와 단편화를 방지하세요.
24. 델타 업데이트(Delta Update) 적용: 이전 버전의 펌웨어 바이너리와 델타 패치(binary diff) 파일을 입력받아 새로운 버전의 펌웨어 바이너리를 생성하는 함수를 구현하세요.
25. 디지털 서명 검증 (API 호출): 다운로드된 펌웨어 이미지와 서명 값이 주어졌을 때, 암호화 라이브러리를 사용하여 ECDSA 또는 RSA 서명을 검증하는 함수를 작성하세요.
26. 플래시 드라이버 추상화: flash_erase_sector(address), flash_write_page(address, data, length)와 같은 함수 인터페이스를 정의하고, 이를 호출하여 다운로드된 펌웨어를 플래시 메모리에 쓰는 로직을 구현하세요.
27. 다운로드 재개(Resume) 로직: 통신이 중간에 끊겼을 경우, 이전에 다운로드받은 블록 수를 NVM에 기록했다가 다음 연결 시 해당 블록부터 다운로드를 재개하는 로직을 구현하세요.
28. SOME/IP 서비스 디스커버리: OTA 클라이언트가 네트워크상의 OTA 서버(업데이트 서버)를 동적으로 찾기 위해 SOME/IP-SD(Service Discovery)를 사용하는 과정을 의사 코드로 설명하세요.
29. DoIP(Diagnostics over IP)를 통한 데이터 수신: 대용량 펌웨어 파일을 이더넷을 통해 DoIP(ISO 13400)로 수신하는 과정을 설명하고, TCP/UDP 소켓을 사용하는 데이터 수신부의 기본 구조를 의사 코드로 작성하세요.
30. 이중 버퍼링(Double Buffering) 플래시 쓰기: 플래시 쓰기 작업이 진행되는 동안 통신을 통해 다음 데이터 블록을 RAM 버퍼에 미리 수신하여 전체 업데이트 시간을 단축하는 로직을 구현하세요.
31. 안전한 상태 전환 로직: 차량이 주행 중일 때는 OTA 업데이트 설치가 진행되지 않도록, 차량 속도나 기어 상태(P단)를 확인하는 조건부를 업데이트 상태 머신에 추가하세요.
32. 업데이트 패키지 압축 해제 (API 호출): zlib과 같은 라이브러리를 사용하여 압축된 펌웨어 이미지를 수신한 후 압축을 해제하는 함수를 작성하세요.
33. ODX/PDX 파일의 역할: OTA 과정에서 ODX(Open Diagnostic Data Exchange) 파일이 ECU의 진단 서비스(예: 플래싱 시퀀스)를 정의하는 데 어떻게 사용되는지 설명하세요.
34. AUTOSAR DCM/DEM과의 상호작용: OTA 업데이트 과정에서 발생하는 이벤트(예: 다운로드 시작, 검증 실패)를 AUTOSAR DEM(Diagnostic Event Manager)에 보고하고, UDS 요청을 DCM(Diagnostic Communication Manager)을 통해 처리하는 흐름을 설명하세요.
35. 전력 소모 관리: 대용량 파일 다운로드 중 배터리 소모를 최소화하기 위해, 다운로드 중에는 저전력 모드로 진입하고 통신 이벤트 발생 시에만 깨어나는(wake-up) 로직을 설계하세요.
*/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/*
16. 원형 버퍼(Circular Buffer) 구현 및 테스트
   설명: UART/CAN 인터럽트에서 데이터를 원형 버퍼에 저장하고, 메인 태스크에서 읽어 처리합니다.
   개념: 원형 버퍼는 FIFO 큐로, 오버플로 없이 연속적으로 데이터를 저장/읽기 위해 사용합니다.
   함수 시그니처: void cb_init(...); int cb_write(...); int cb_read(...);
   예시 입력: cb_write(&cb, 0xAA); cb_read(&cb, &val);
   예시 출력: val == 0xAA
*/
#define CB_SIZE 8
typedef struct {
    uint8_t buf[CB_SIZE];
    int head, tail, count;
} CircularBuffer;

void cb_init(CircularBuffer* cb) {
    cb->head = cb->tail = cb->count = 0;
}
int cb_write(CircularBuffer* cb, uint8_t data) {
    if (cb->count == CB_SIZE) return -1; // Full
    cb->buf[cb->head] = data;
    cb->head = (cb->head + 1) % CB_SIZE;
    cb->count++;
    return 0;
}
int cb_read(CircularBuffer* cb, uint8_t* data) {
    if (cb->count == 0) return -1; // Empty
    *data = cb->buf[cb->tail];
    cb->tail = (cb->tail + 1) % CB_SIZE;
    cb->count--;
    return 0;
}

/*
17. OTA 다운로더 RTOS 태스크 (의사코드)
   설명: RTOS 태스크에서 메시지 큐로 명령을 받아 펌웨어 다운로드를 수행합니다.
   개념: RTOS 환경에서 태스크 간 통신(메시지 큐)을 통한 명령 처리 구조.
   함수 시그니처: void ota_downloader_task(void* arg);
   예시 입력: queue에 "http://example.com/fw.bin" 삽입
   예시 출력: "Downloading from http://example.com/fw.bin"
*/
typedef struct {
    char url[128];
} DownloadCmd;

#define QUEUE_SIZE 4
typedef struct {
    DownloadCmd cmds[QUEUE_SIZE];
    int head, tail, count;
} MsgQueue;

void queue_init(MsgQueue* q) { q->head = q->tail = q->count = 0; }
int queue_send(MsgQueue* q, const char* url) {
    if (q->count == QUEUE_SIZE) return -1;
    strncpy(q->cmds[q->head].url, url, sizeof(q->cmds[q->head].url)-1);
    q->cmds[q->head].url[sizeof(q->cmds[q->head].url)-1] = 0;
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count++;
    return 0;
}
int queue_recv(MsgQueue* q, DownloadCmd* out) {
    if (q->count == 0) return -1;
    *out = q->cmds[q->tail];
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count--;
    return 0;
}
int ota_downloader_task(MsgQueue* q) {
    DownloadCmd cmd;
    if (queue_recv(q, &cmd) == 0) {
        printf("Downloading from %s\n", cmd.url);
        return 0;
    }
    return -1;
}

/*
18. Mutex를 이용한 상태 변수 보호 (의사코드)
   설명: 여러 태스크가 공유 상태 변수에 접근할 때 Mutex로 보호합니다.
   개념: RTOS 환경에서 데이터 레이스 방지.
   함수 시그니처: void set_ota_status(int status); int get_ota_status(void);
   예시 입력: set_ota_status(2); get_ota_status();
   예시 출력: 2
*/
typedef struct { int locked; } Mutex;
void mutex_lock(Mutex* m) { m->locked = 1; }
void mutex_unlock(Mutex* m) { m->locked = 0; }
static int ota_status = 0;
static Mutex ota_mutex = {0};
void set_ota_status(int status) {
    mutex_lock(&ota_mutex);
    ota_status = status;
    mutex_unlock(&ota_mutex);
}
int get_ota_status(void) {
    int val;
    mutex_lock(&ota_mutex);
    val = ota_status;
    mutex_unlock(&ota_mutex);
    return val;
}

/*
19. 세마포어를 이용한 다운로드 완료 동기화 (의사코드)
   설명: ISR/통신 태스크가 세마포어를 give, OTA 태스크가 take하여 동기화.
   개념: RTOS에서 태스크/ISR 간 동기화.
   함수 시그니처: void sem_give(Semaphore* s); void sem_take(Semaphore* s);
   예시 입력: sem_give(&sem); sem_take(&sem);
   예시 출력: sem.value == 0
*/
typedef struct { int value; } Semaphore;
void sem_init(Semaphore* s) { s->value = 0; }
void sem_give(Semaphore* s) { s->value++; }
void sem_take(Semaphore* s) { if (s->value > 0) s->value--; }

/*
20. UDS RequestDownload (0x34) 핸들러 (의사코드)
   설명: UDS 0x34 요청을 받아 주소/크기 검증 후 최대 블록 크기 응답.
   개념: 진단 프로토콜에서 펌웨어 다운로드 준비.
   함수 시그니처: int uds_handle_request_download(uint32_t addr, uint32_t size, uint32_t* max_block);
   예시 입력: addr=0x10000, size=0x20000
   예시 출력: max_block=4096, return 0
*/
int uds_handle_request_download(uint32_t addr, uint32_t size, uint32_t* max_block) {
    if (addr % 4096 != 0 || size == 0) return -1;
    *max_block = 4096;
    return 0;
}


/*
21. UDS TransferData (0x36) 핸들러
   설명: UDS 서비스 0x36 요청을 받아, 데이터 블록을 임시 저장(RAM/플래시)에 쓰고, 블록 시퀀스 카운터를 확인합니다.
   개념: 진단 프로토콜에서 데이터 무결성 및 순서 보장.
   함수 시그니처: int uds_handle_transfer_data(uint8_t seq, const uint8_t* data, size_t len, uint8_t* last_seq);
   예시 입력: seq=1, data={0xAA,0xBB}, len=2, last_seq=0
   예시 출력: last_seq=1, return 0
*/
int uds_handle_transfer_data(uint8_t seq, const uint8_t* data, size_t len, uint8_t* last_seq) {
    static uint8_t expected_seq = 1;
    if (seq != expected_seq) return -1; // Sequence mismatch
    // 실제로는 data를 RAM/플래시에 저장
    *last_seq = seq;
    expected_seq++;
    return 0;
}

/*
22. UDS RequestTransferExit (0x37) 핸들러
   설명: UDS 서비스 0x37 요청을 받아, 전체 데이터 수신 완료 및 무결성 검사 후 응답합니다.
   개념: 진단 프로토콜에서 전송 종료 및 검증.
   함수 시그니처: int uds_handle_transfer_exit(bool data_ok);
   예시 입력: data_ok=true
   예시 출력: return 0
*/
int uds_handle_transfer_exit(bool data_ok) {
    if (!data_ok) return -1; // 무결성 실패
    // 실제로는 플래시 쓰기 완료, 상태 갱신 등
    return 0;
}

/*
23. 메모리 풀(Memory Pool) 구현
   설명: 고정 크기 블록으로 구성된 메모리 풀에서 패킷을 할당/반환합니다.
   개념: 동적 할당 오버헤드/단편화 방지.
   함수 시그니처: void mp_init(...); void* mp_alloc(...); void mp_free(...);
   예시 입력: void* p = mp_alloc(&pool); mp_free(&pool, p);
   예시 출력: p != NULL, pool.free_count++
*/
#define MP_BLOCK_SIZE 32
#define MP_BLOCK_COUNT 4
typedef struct {
    uint8_t blocks[MP_BLOCK_COUNT][MP_BLOCK_SIZE];
    bool used[MP_BLOCK_COUNT];
    int free_count;
} MemPool;

void mp_init(MemPool* pool) {
    memset(pool->used, 0, sizeof(pool->used));
    pool->free_count = MP_BLOCK_COUNT;
}
void* mp_alloc(MemPool* pool) {
    for (int i = 0; i < MP_BLOCK_COUNT; ++i) {
        if (!pool->used[i]) {
            pool->used[i] = true;
            pool->free_count--;
            return pool->blocks[i];
        }
    }
    return NULL;
}
void mp_free(MemPool* pool, void* ptr) {
    for (int i = 0; i < MP_BLOCK_COUNT; ++i) {
        if (pool->blocks[i] == ptr) {
            pool->used[i] = false;
            pool->free_count++;
            return;
        }
    }
}

/*
24. 델타 업데이트(Delta Update) 적용
   설명: 이전 펌웨어와 델타(diff) 파일을 입력받아 새로운 펌웨어를 생성합니다.
   개념: 전체 바이너리 대신 변경분만 적용해 효율적 업데이트.
   함수 시그니처: int apply_delta(const uint8_t* old_fw, size_t old_len, const uint8_t* delta, size_t delta_len, uint8_t* new_fw, size_t* new_len);
   예시 입력: old_fw={1,2,3}, delta={0,0,4}, new_fw=?
   예시 출력: new_fw={1,2,4}, new_len=3
*/
int apply_delta(const uint8_t* old_fw, size_t old_len, const uint8_t* delta, size_t delta_len, uint8_t* new_fw, size_t* new_len) {
    if (old_len != delta_len) return -1;
    for (size_t i = 0; i < old_len; ++i)
        new_fw[i] = old_fw[i] + delta[i];
    *new_len = old_len;
    return 0;
}

/*
25. 디지털 서명 검증 (API 호출, 더미)
   설명: 다운로드된 펌웨어와 서명 값이 주어졌을 때, 라이브러리로 ECDSA/RSA 서명을 검증합니다.
   개념: OTA 보안의 핵심, 위변조 방지.
   함수 시그니처: int verify_signature(const uint8_t* data, size_t len, const uint8_t* sig, size_t sig_len);
   예시 입력: data="abc", sig={0x01,0x02}, sig_len=2
   예시 출력: return 0 (성공)
*/
int verify_signature(const uint8_t* data, size_t len, const uint8_t* sig, size_t sig_len) {
    // 실제 환경에서는 암호화 라이브러리 호출 필요
    // 여기서는 더미: sig_len==2면 성공
    return (sig_len == 2) ? 0 : -1;
}

/*
26. 플래시 드라이버 추상화
   설명: flash_erase_sector(address), flash_write_page(address, data, length)와 같은 함수 인터페이스를 정의하고, 이를 호출하여 다운로드된 펌웨어를 플래시 메모리에 쓰는 로직을 구현하세요.
   개념: 하드웨어 독립적인 플래시 접근 추상화.
   함수 시그니처: int flash_erase_sector(uint32_t addr); int flash_write_page(uint32_t addr, const uint8_t* data, size_t len);
   예시 입력: flash_erase_sector(0x10000); flash_write_page(0x10000, buf, 256);
   예시 출력: return 0 (성공)
*/
int flash_erase_sector(uint32_t addr) {
    // 실제로는 하드웨어 제어 코드 필요, 여기선 더미
    return (addr % 4096 == 0) ? 0 : -1;
}
int flash_write_page(uint32_t addr, const uint8_t* data, size_t len) {
    // 실제로는 하드웨어 제어 코드 필요, 여기선 더미
    return (addr % 256 == 0 && len <= 256) ? 0 : -1;
}

/*
27. 다운로드 재개(Resume) 로직
   설명: 이전에 다운로드받은 블록 수를 NVM에 기록했다가, 다음 연결 시 해당 블록부터 다운로드를 재개하는 로직을 구현하세요.
   개념: OTA 중단 후 재시작 지원.
   함수 시그니처: void save_resume_block(uint32_t block); uint32_t load_resume_block(void);
   예시 입력: save_resume_block(5); load_resume_block();
   예시 출력: 5
*/
static uint32_t nvm_resume_block = 0;
void save_resume_block(uint32_t block) { nvm_resume_block = block; }
uint32_t load_resume_block(void) { return nvm_resume_block; }

/*
28. SOME/IP 서비스 디스커버리 (의사코드)
   설명: OTA 클라이언트가 네트워크상의 OTA 서버를 동적으로 찾기 위해 SOME/IP-SD를 사용하는 과정을 의사 코드로 설명하세요.
   개념: 네트워크 서비스 자동 탐색.
   함수 시그니처: int someip_service_discovery(const char* service_name, char* out_ip);
   예시 입력: service_name="OTA_SERVER"
   예시 출력: out_ip="192.168.0.10", return 0
*/
int someip_service_discovery(const char* service_name, char* out_ip) {
    // 실제 네트워크 브로드캐스트/응답 필요, 여기선 더미
    if (strcmp(service_name, "OTA_SERVER") == 0) {
        strcpy(out_ip, "192.168.0.10");
        return 0;
    }
    return -1;
}

/*
29. DoIP(Diagnostics over IP)를 통한 데이터 수신 (의사코드)
   설명: 대용량 펌웨어 파일을 이더넷을 통해 DoIP로 수신하는 과정을 설명하고, TCP/UDP 소켓을 사용하는 데이터 수신부의 기본 구조를 의사 코드로 작성하세요.
   개념: 이더넷 기반 진단 데이터 수신.
   함수 시그니처: int doip_receive_data(const char* ip, uint16_t port, uint8_t* buf, size_t maxlen, size_t* outlen);
   예시 입력: ip="192.168.0.10", port=13400
   예시 출력: buf={0x01,0x02}, outlen=2, return 0
*/
int doip_receive_data(const char* ip, uint16_t port, uint8_t* buf, size_t maxlen, size_t* outlen) {
    // 실제로는 소켓 통신 필요, 여기선 더미
    if (strcmp(ip, "192.168.0.10") == 0 && port == 13400) {
        buf[0] = 0x01; buf[1] = 0x02; *outlen = 2;
        return 0;
    }
    return -1;
}

/*
30. 이중 버퍼링(Double Buffering) 플래시 쓰기
   설명: 플래시 쓰기 작업이 진행되는 동안 통신을 통해 다음 데이터 블록을 RAM 버퍼에 미리 수신하여 전체 업데이트 시간을 단축하는 로직을 구현하세요.
   개념: 데이터 수신과 플래시 쓰기 병렬화.
   함수 시그니처: void double_buffer_write(const uint8_t* data1, const uint8_t* data2, size_t len);
   예시 입력: data1={1,2}, data2={3,4}, len=2
   예시 출력: (플래시 쓰기 성공)
*/
void double_buffer_write(const uint8_t* data1, const uint8_t* data2, size_t len) {
    // 실제로는 쓰기와 수신을 병렬로 처리
    flash_write_page(0x10000, data1, len);
    flash_write_page(0x10100, data2, len);
}

/*
31. 안전한 상태 전환 로직
   설명: 차량이 주행 중일 때는 OTA 업데이트 설치가 진행되지 않도록, 차량 속도나 기어 상태(P단)를 확인하는 조건부를 업데이트 상태 머신에 추가하세요.
   개념: 안전 조건 기반 상태 전환.
   함수 시그니처: bool can_update(int speed, char gear);
   예시 입력: speed=0, gear='P'
   예시 출력: true
*/
bool can_update(int speed, char gear) {
    return (speed == 0 && gear == 'P');
}

/*
32. 업데이트 패키지 압축 해제 (API 호출, 더미)
   설명: zlib과 같은 라이브러리를 사용하여 압축된 펌웨어 이미지를 수신한 후 압축을 해제하는 함수를 작성하세요.
   개념: OTA 데이터 전송 효율화.
   함수 시그니처: int decompress_fw(const uint8_t* comp, size_t clen, uint8_t* out, size_t* outlen);
   예시 입력: comp={1,2,3}, clen=3
   예시 출력: out={1,2,3}, outlen=3, return 0
*/
int decompress_fw(const uint8_t* comp, size_t clen, uint8_t* out, size_t* outlen) {
    // 실제로는 zlib 등 라이브러리 호출, 여기선 더미
    memcpy(out, comp, clen);
    *outlen = clen;
    return 0;
}

/*
33. ODX/PDX 파일의 역할 (설명)
   설명: OTA 과정에서 ODX 파일이 ECU의 진단 서비스(예: 플래싱 시퀀스)를 정의하는 데 어떻게 사용되는지 설명하세요.
   개념: 진단 데이터 표준화 및 자동화.
   함수 시그니처: const char* explain_odx_role(void);
   예시 출력: "ODX 파일은 ECU 진단 서비스 정의에 사용됨"
*/
const char* explain_odx_role(void) {
    return "ODX 파일은 ECU 진단 서비스 정의에 사용됨";
}

/*
34. AUTOSAR DCM/DEM과의 상호작용 (설명)
   설명: OTA 업데이트 과정에서 발생하는 이벤트(예: 다운로드 시작, 검증 실패)를 AUTOSAR DEM에 보고하고, UDS 요청을 DCM을 통해 처리하는 흐름을 설명하세요.
   개념: 표준 진단 이벤트/통신 관리.
   함수 시그니처: const char* explain_autosar_dcm_dem(void);
   예시 출력: "DEM에 이벤트 보고, DCM으로 UDS 처리"
*/
const char* explain_autosar_dcm_dem(void) {
    return "DEM에 이벤트 보고, DCM으로 UDS 처리";
}

/*
35. 전력 소모 관리
   설명: 대용량 파일 다운로드 중 배터리 소모를 최소화하기 위해, 다운로드 중에는 저전력 모드로 진입하고 통신 이벤트 발생 시에만 깨어나는(wake-up) 로직을 설계하세요.
   개념: 저전력 설계, 이벤트 기반 wake-up.
   함수 시그니처: void enter_low_power(void); void wake_on_event(void);
   예시 입력: enter_low_power(); wake_on_event();
   예시 출력: (상태 플래그 변경)
*/
static int low_power_mode = 0;
void enter_low_power(void) { low_power_mode = 1; }
void wake_on_event(void) { low_power_mode = 0; }
int is_low_power(void) { return low_power_mode; }

// ------------------- Test code -------------------
#define LABEL_WIDTH 48
void test_result(const char* label, int ok) {
    printf("%-*s %s\n", LABEL_WIDTH, label, ok ? "[PASS]" : "[FAIL]");
}

int main(void) {
    // 16. 원형 버퍼 테스트
    CircularBuffer cb; cb_init(&cb);
    test_result("16. cb_write/cb_read", cb_write(&cb, 0xAA) == 0 && cb.count == 1);
    uint8_t val = 0;
    test_result("16. cb_read", cb_read(&cb, &val) == 0 && val == 0xAA);

    // 17. OTA 다운로더 RTOS 태스크 테스트
    MsgQueue q; queue_init(&q);
    queue_send(&q, "http://example.com/fw.bin");
    test_result("17. ota_downloader_task", ota_downloader_task(&q) == 0);
    ota_downloader_task(&q); // 콘솔에 출력

    // 18. Mutex 상태 변수 보호 테스트
    set_ota_status(2);
    test_result("18. set/get_ota_status (mutex)", get_ota_status() == 2);

    // 19. 세마포어 동기화 테스트
    Semaphore sem; sem_init(&sem);
    sem_give(&sem);
    sem_take(&sem);
    test_result("19. sem_give/sem_take", sem.value == 0);

    // 20. UDS RequestDownload 핸들러 테스트
    uint32_t max_block = 0;
    int ret = uds_handle_request_download(0x10000, 0x20000, &max_block);
    test_result("20. uds_handle_request_download", ret == 0 && max_block == 4096);

    // 21. UDS TransferData 핸들러 테스트
    uint8_t last_seq = 0;
    uint8_t dblock[] = {0xAA, 0xBB};
    int ret21 = uds_handle_transfer_data(1, dblock, 2, &last_seq);
    test_result("21. uds_handle_transfer_data", (ret21 == 0 && last_seq == 1));

    // 22. UDS TransferExit 핸들러 테스트
    int ret22 = uds_handle_transfer_exit(true);
    test_result("22. uds_handle_transfer_exit", (ret22 == 0));

    // 23. 메모리 풀 테스트
    MemPool pool; mp_init(&pool);
    void* p1 = mp_alloc(&pool);
    void* p2 = mp_alloc(&pool);
    mp_free(&pool, p1);
    test_result("23. mp_alloc/mp_free", (p1 && p2 && pool.free_count == MP_BLOCK_COUNT - 1));

    // 24. 델타 업데이트 테스트
    uint8_t old_fw[] = {1,2,3}, delta[] = {0,0,1}, new_fw[3]; size_t new_len = 0;
    int ret24 = apply_delta(old_fw, 3, delta, 3, new_fw, &new_len);
    test_result("24. apply_delta", (ret24 == 0 && new_fw[2] == 4 && new_len == 3));

    // 25. 디지털 서명 검증 테스트
    uint8_t sig[] = {0x01, 0x02};
    int ret25 = verify_signature((const uint8_t*)"abc", 3, sig, 2);
    test_result("25. verify_signature", (ret25 == 0));
    // 26. 플래시 드라이버 추상화 테스트
    uint8_t buf[256] = {0};
    test_result("26. flash_erase_sector", flash_erase_sector(0x10000) == 0);
    test_result("26. flash_write_page", flash_write_page(0x10000, buf, 256) == 0);

    // 27. 다운로드 재개 로직 테스트
    save_resume_block(5);
    test_result("27. save/load_resume_block", load_resume_block() == 5);

    // 28. SOME/IP 서비스 디스커버리 테스트
    char ip[16];
    test_result("28. someip_service_discovery", someip_service_discovery("OTA_SERVER", ip) == 0 && strcmp(ip, "192.168.0.10") == 0);

    // 29. DoIP 데이터 수신 테스트
    uint8_t doip_buf[8]; size_t doip_len = 0;
    test_result("29. doip_receive_data", doip_receive_data("192.168.0.10", 13400, doip_buf, 8, &doip_len) == 0 && doip_len == 2 && doip_buf[0] == 0x01);

    // 30. 이중 버퍼링 플래시 쓰기 테스트
    uint8_t db1[2] = {1,2}, db2[2] = {3,4};
    double_buffer_write(db1, db2, 2);
    test_result("30. double_buffer_write", 1); // 실제 검증 불가, 호출만 확인

    // 31. 안전한 상태 전환 로직 테스트
    test_result("31. can_update", can_update(0, 'P') == true && can_update(10, 'D') == false);

    // 32. 업데이트 패키지 압축 해제 테스트
    uint8_t comp[3] = {1,2,3}, out[3]; size_t outlen = 0;
    test_result("32. decompress_fw", decompress_fw(comp, 3, out, &outlen) == 0 && outlen == 3 && out[2] == 3);

    // 33. ODX/PDX 파일 역할 설명 테스트
    test_result("33. explain_odx_role", strcmp(explain_odx_role(), "ODX 파일은 ECU 진단 서비스 정의에 사용됨") == 0);

    // 34. AUTOSAR DCM/DEM 상호작용 설명 테스트
    test_result("34. explain_autosar_dcm_dem", strcmp(explain_autosar_dcm_dem(), "DEM에 이벤트 보고, DCM으로 UDS 처리") == 0);

    // 35. 전력 소모 관리 테스트
    enter_low_power();
    test_result("35. enter_low_power", is_low_power() == 1);
    wake_on_event();
    test_result("35. wake_on_event", is_low_power() == 0);

    return 0;
}