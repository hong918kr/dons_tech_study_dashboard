/*

난이도: Hard (시스템 아키텍처, 최적화, 고급 알고리즘)
이 단계의 문제들은 시스템 전체에 대한 깊은 이해, 성능 및 메모리 최적화, 복잡한 알고리즘의 구현 능력, 그리고 기능 안전성과 같은 고급 주제에 대한 지식을 요구합니다. 하드웨어를 직접 제어하는 저수준 프로그래밍 능력이 필수적입니다.

61. 인터럽트 지연 시간(Latency) 최소화 전략: 인터럽트 핸들러의 처리 시간을 최소화하기 위한 C 코드 수준의 전략 3가지 이상을 설명하고, 그중 하나를 코드로 예시를 들어 설명하세요. (예: ISR에서 플래그만 설정하고 메인 루프에서 처리)
62. 재진입성(Reentrancy)을 고려한 함수 설계: 재진입 가능한(reentrant) 함수란 무엇이며, 왜 중요한지 설명하세요. 재진입성이 깨지는 코드와 이를 수정한 코드를 각각 작성하여 비교 설명하세요.
63. heap_4.c 스타일의 메모리 관리자: FreeRTOS의 heap_4.c와 같이, 인접한 빈 블록을 병합(coalescence)하는 기능을 포함하여 메모리 단편화(fragmentation)를 줄이는 동적 메모리 할당/해제 관리자를 구현하세요.
64. UDS SecurityAccess (0x27) Seed & Key 알고리즘 구현: UDS 보안 접근 서비스의 Seed-Key 교환 메커니즘을 구현하세요. 서버는 시드(seed)를 생성하고, 클라이언트는 주어진 시드와 비밀 알고리즘을 사용하여 키(key)를 계산하여 응답해야 합니다. (간단한 XOR/Rotate 알고리즘 사용)
65. 비트뱅잉(Bit-banging)을 이용한 I2C 마스터 구현: MCU의 I2C 하드웨어 모듈을 사용하지 않고, 일반 GPIO 핀 두 개를 직접 제어하여 I2C Start/Stop 조건, 바이트 쓰기, ACK 비트 읽기를 구현하세요.
66. 간단한 비선점형 스케줄러(Scheduler) 구현: 우선순위 기반의 간단한 비선점형(non-preemptive) 스케줄러를 구현하세요. 태스크 제어 블록(TCB) 구조체를 정의하고, 여러 태스크 중 가장 높은 우선순위의 태스크를 실행하는 로직을 작성해야 합니다.
67. 1차원 칼만 필터(Kalman Filter) 구현: 노이즈가 있는 센서 데이터를 평활화(smoothing)하기 위해 간단한 1차원 칼만 필터를 C로 구현하세요. (상태 예측 및 측정 업데이트 단계 포함)
68. 부트로더(Bootloader)의 애플리케이션 점프 로직: 펌웨어 업데이트 후, 부트로더가 애플리케이션의 유효성을 검사(예: CRC 체크)하고, 유효할 경우 애플리케이션의 벡터 테이블을 재배치한 후 시작 주소로 점프하는 핵심 로직을 의사 코드로 작성하세요.
69. GPIO 드라이브 강도(Drive Strength) 제어의 의미: 고속 디지털 신호에서 발생하는 링잉(ringing) 현상의 원인을 설명하고, MCU의 GPIO 출력 드라이브 강도 조절 기능이 이를 완화하는 데 어떻게 사용될 수 있는지 설명하세요.
70. AUTOSAR COM 스택의 Signal-to-PDU 매핑 과정: AUTOSAR COM 모듈이 애플리케이션의 '신호(Signal)'를 PDU 라우터를 통해 CAN '프레임(Frame)'으로 변환하는 과정을 설명하고, 이 추상화 계층의 장점을 논하세요.
71. Rate Limiter 알고리즘 구현: 토큰 버킷(Token Bucket) 알고리즘을 사용하여 특정 이벤트(예: CAN 메시지 전송)가 설정된 비율을 초과하지 않도록 제어하는 함수를 구현하세요.
72. 기능 안전성(ISO 26262)과 MPU: 서로 다른 ASIL 등급을 가진 소프트웨어 컴포넌트들이 하나의 MCU에서 실행될 때, '간섭으로부터의 자유(Freedom from Interference)'를 보장하기 위한 메모리 보호 장치(MPU)의 역할과 설정 방법을 설명하세요.
73. UDS 펌웨어 업데이트(Flashing) 시퀀스 구현: RequestDownload (0x34), TransferData (0x36), RequestTransferExit (0x37) 서비스를 순차적으로 처리하여 펌웨어 데이터를 수신하고 메모리에 쓰는 핵심 로직을 구현하세요.
74. 공간 벡터 PWM(Space Vector PWM, SVPWM) 알고리즘: 3상 모터 제어에 사용되는 SVPWM의 기본 원리를 설명하고, 주어진 목표 전압 벡터에 대해 6개 스위치의 ON/OFF 시간을 계산하는 핵심 부분을 의사 코드로 작성하세요.
75. CAN 버스 침입 탐지 시스템(IDS) 알고리즘: CAN 메시지의 주기(periodicity)를 모니터링하여 정상 범위를 벗어나는 비정상적인 트래픽(예: DoS 공격)을 탐지하는 간단한 IDS 알고리즘을 구현하세요.
76. 스택 사용량 분석 및 최적화: 임베디드 시스템에서 스택 오버플로우가 치명적인 이유를 설명하고, 이를 방지하기 위한 정적 분석(컴파일러 옵션) 및 동적 분석(스택 페인팅) 방법을 설명하세요.
77. 비트뱅잉(Bit-banging)을 이용한 SPI 슬레이브 구현: GPIO 핀들을 사용하여 SPI 슬레이브 장치를 에뮬레이션하는 코드를 작성하세요. 마스터로부터의 클럭과 데이터에 동기화되어 응답해야 합니다.
78. 플래시(Flash) 드라이버 구현: 특정 플래시 메모리 칩의 데이터시트를 참조하여, 페이지 쓰기(Page Write), 섹터 지우기(Sector Erase), 칩 지우기(Chip Erase) 기능을 수행하는 드라이버를 구현하세요.
79. 우선순위 기반의 선점형(Preemptive) 스케줄러 구현: 타이머 인터럽트를 이용하여 현재 실행 중인 태스크보다 우선순위가 높은 태스크가 준비(Ready) 상태가 되면 문맥 전환(Context Switching)을 수행하는 간단한 선점형 스케줄러를 구현하세요.
80. Lock-Free 자료구조: 원자적 연산(Atomic Operation)을 사용하여 Mutex 없이 여러 태스크가 안전하게 접근할 수 있는 간단한 Lock-Free 큐(Queue)를 구현하세요.
81. assert() 매크로의 활용: 디버그 빌드에서만 활성화되는 assert() 매크로의 장점과, 이를 사용하여 계약에 의한 설계(Design by Contract)를 어떻게 구현할 수 있는지 설명하세요.
82. DMA를 이용한 ADC 연속 변환: 여러 ADC 채널을 연속적으로 스캔하고 그 결과를 DMA를 통해 메모리 버퍼로 자동으로 전송하도록 설정하는 과정을 의사 코드로 설명하세요.
83. UDS Authentication (0x29) 서비스의 개념: UDS 서비스 0x29가 기존의 SecurityAccess (0x27)와 어떻게 다르며, PKI(공개 키 기반 구조)를 사용하는 것이 어떤 보안적 이점을 제공하는지 설명하세요.
84. CORDIC 알고리즘 구현: 부동소수점 연산 없이 삼각함수(sin, cos) 값을 계산할 수 있는 CORDIC 알고리즘의 기본 원리를 설명하고 간단히 구현하세요.
85. Fast Fourier Transform (FFT) 구현: 오디오 신호나 진동 센서 데이터의 주파수 성분을 분석하기 위해, 재귀적인 방식으로 FFT 알고리즘을 구현하세요.
86. 데드락(Deadlock) 발생 조건 및 예방: 두 개 이상의 태스크가 서로의 자원을 기다리며 무한 대기 상태에 빠지는 데드락의 4가지 발생 조건을 설명하고, 이를 예방할 수 있는 코딩 전략(예: 락 순서 지정)을 제시하세요.
87. Watchdog을 이용한 시스템 복구: 시스템이 멈췄을 때(hang) Watchdog 타이머 리셋이 발생하면, 부팅 후 리셋의 원인이 Watchdog이었음을 감지하고, 오류 횟수를 NVM에 기록한 후 안전 모드로 진입하는 로직을 구현하세요.
88. MISRA C 규칙의 중요성: MISRA C 코딩 표준의 목적이 무엇이며, '동적 메모리 할당 금지'나 '재귀 함수 호출 금지'와 같은 규칙이 안전 필수(Safety-critical) 시스템에서 왜 중요한지 설명하세요.
89. AUTOSAR SecOC(Secure On-board Communication)의 개념: CAN 통신에서 메시지 인증을 위해 SecOC가 어떻게 MAC(Message Authentication Code)과 Freshness Value를 사용하는지 기본 개념을 설명하세요.
90. 타임 트리거(Time-Triggered) 아키텍처: 이벤트 기반(Event-triggered) 시스템과 타임 트리거 시스템의 차이점을 설명하고, FlexRay와 같은 통신 프로토콜에서 타임 트리거 방식이 어떻게 결정론적(deterministic) 통신을 보장하는지 설명하세요.
91. 전력 소모 최적화: 특정 기능을 수행하는 데 필요한 총 전력 소모(mA)를 최소화하기 위해, CPU 클럭 속도 조절, 불필요한 주변장치(Peripheral) 비활성화, 저전력 모드 활용 등을 종합적으로 고려하는 펌웨어 로직을 설계하세요.
92. A/B 파티션을 이용한 안전한 OTA 펌웨어 업데이트: 펌웨어 업데이트 실패 시 시스템이 "벽돌"이 되는 것을 방지하기 위해, 두 개의 플래시 파티션(A/B)을 사용하여 한쪽에 새 펌웨어를 다운로드하고 유효성 검사 후 부팅 파티션을 전환하는 부트로더 로직을 설계하세요.
93. setjmp와 longjmp를 이용한 예외 처리: C언어에서 try-catch와 유사한 예외 처리 메커니즘을 구현하기 위해 setjmp와 longjmp 함수를 어떻게 사용할 수 있는지 보여주고, 이것이 스택에 미치는 영향과 위험성에 대해 설명하세요.
94. 메모리 배리어(Memory Barrier)의 필요성: 컴파일러의 명령어 재배치(reordering)나 CPU의 비순차적 실행(out-of-order execution)으로 인해 공유 메모리 동기화가 깨질 수 있는 시나리오를 설명하고, 이를 방지하기 위해 메모리 배리어가 왜 필요한지 설명하세요.
95. CRC32 하드웨어 가속기 사용: 소프트웨어로 CRC를 계산하는 대신, MCU에 내장된 CRC 하드웨어 가속기를 사용하여 데이터 블록의 CRC32 값을 계산하는 드라이버 함수를 구현하세요.
96. UDS RoutineControl (0x31) 서비스 구현: 특정 루틴(예: 센서 보정, 시스템 자가 진단)을 시작(start), 중지(stop), 결과 요청(requestResult)하는 UDS 서비스 0x31의 핸들러를 구현하세요.
97. 가상 EEPROM 구현: 플래시 메모리의 일부를 사용하여 마모 균등화(wear-leveling) 알고리즘을 적용한 가상 EEPROM을 구현하여, 데이터의 잦은 쓰기에도 내구성을 보장하는 로직을 작성하세요.
98. DMA를 이용한 PWM 파형 생성: 메모리에 저장된 듀티 사이클 값 배열을 DMA를 통해 타이머의 비교 레지스터로 주기적으로 전송하여, CPU 개입 없이 복잡한 PWM 파형(예: 사인파)을 생성하는 방법을 구현하세요.
99. CAN FD의 BRS(Bit Rate Switch)와 ESI(Error State Indicator): CAN FD가 클래식 CAN과 비교하여 가지는 주요 개선점인 BRS와 ESI의 역할이 무엇인지, 그리고 이것이 데이터 처리량과 네트워크 안정성에 어떻게 기여하는지 설명하세요.
100. 시스템 상태 관리자(System State Manager) 설계: 차량의 상태(예: OFF, ACC, IGNITION, CRANK)에 따라 각 소프트웨어 모듈의 동작(초기화, 실행, 비활성화)을 총괄하는 중앙 상태 관리자를 설계하고 구현하세요.

*/


/*

******************자세한 설명******************

*/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/*

난이도: Hard (시스템 아키텍처, 최적화, 고급 알고리즘)
각 문제는 함수 시그니처, 설명, 예시 입력/출력, 그리고 배경 개념을 함께 제공합니다.
*/

// 61. 인터럽트 지연 시간(Latency) 최소화 전략
/*
설명: ISR(인터럽트 서비스 루틴)에서 처리 시간을 최소화하는 3가지 전략을 설명하고, ISR에서 플래그만 설정하는 예시 코드를 작성하세요.
배경: ISR이 길어지면 시스템 전체 응답성이 저하됨. 빠른 복귀가 중요.
예시 입력: 인터럽트 발생
예시 출력: isr_flag=1, 메인 루프에서 실제 처리
*/
volatile int isr_flag = 0;
void ISR_example(void) { isr_flag = 1; } // ISR에서는 플래그만
void main_loop(void) { if (isr_flag) { /* 실제 처리 */ isr_flag = 0; } }

// 62. 재진입성(Reentrancy)을 고려한 함수 설계
/*
설명: 재진입 가능한 함수란 무엇이며, 왜 중요한지 설명. 재진입성이 깨지는 코드와 수정 예시를 비교.
배경: ISR, 멀티스레드 환경에서 함수가 동시에 호출되어도 안전해야 함.
예시 입력: 두 스레드가 동시에 inc_global() 호출
예시 출력: 재진입성 없는 경우 값 꼬임, 재진입성 보장 시 값 정확
*/
int global_cnt = 0;
// 재진입성 없는 예시
void inc_global(void) { global_cnt++; }
// 재진입성 보장 예시
void inc_local(int *cnt) { (*cnt)++; }

// 63. heap_4.c 스타일의 메모리 관리자
/*
설명: 인접한 빈 블록을 병합(coalescence)하여 단편화를 줄이는 동적 메모리 할당/해제 관리자 구현.
배경: 임베디드에서 메모리 단편화는 시스템 불안정의 원인. 블록 병합으로 효율 향상.
예시 입력: 32바이트 할당, 해제, 인접 블록 병합
예시 출력: 메모리 단편화 없이 연속 공간 재사용
*/
typedef struct mem_blk {
    struct mem_blk *next;
    size_t size;
    int free;
} mem_blk_t;
#define HEAP4_SIZE 256
static uint8_t heap4[HEAP4_SIZE];
static mem_blk_t *heap4_head = NULL;
void heap4_init(void);
void* heap4_alloc(size_t size);
void heap4_free(void *ptr);

// 64. UDS SecurityAccess (0x27) Seed & Key 알고리즘 구현
/*
설명: 서버가 시드(seed)를 생성, 클라이언트가 시드와 비밀 알고리즘(XOR/Rotate 등)으로 키(key)를 계산.
배경: 자동차 진단에서 보안 접근 제어, Seed-Key는 인증의 기초.
예시 입력: seed=0x1234, secret=0xABCD
예시 출력: key=seed ^ secret (예: 0xB9F9)
*/
uint16_t uds_generate_seed(void);
uint16_t uds_calc_key(uint16_t seed, uint16_t secret);

// 65. 비트뱅잉(Bit-banging)을 이용한 I2C 마스터 구현
/*
설명: GPIO로 I2C Start/Stop, 바이트 쓰기, ACK 읽기 구현.
배경: 하드웨어 I2C 없이 소프트웨어로 직접 신호 생성.
예시 입력: i2c_write_byte(0xA5)
예시 출력: SCL/SDA 신호가 I2C 프로토콜에 맞게 출력됨
*/
void i2c_start(void);
void i2c_stop(void);
bool i2c_write_byte(uint8_t data);
bool i2c_read_ack(void);

// 66. 간단한 비선점형 스케줄러(Scheduler) 구현
/*
설명: 우선순위 기반 비선점형 스케줄러. TCB 구조체와 가장 높은 우선순위 태스크 실행.
배경: RTOS의 기본, 태스크 관리의 핵심.
예시 입력: 3개 태스크 등록, 우선순위 2,1,3
예시 출력: 우선순위 3 태스크 실행
*/
typedef struct { void (*task)(void); int priority; } tcb_t;
void scheduler_run(tcb_t *tasks, int n);

// 67. 1차원 칼만 필터(Kalman Filter) 구현
/*
설명: 센서 데이터 평활화. 상태 예측, 측정 업데이트 단계 포함.
배경: 노이즈가 많은 센서 신호의 최적 추정.
예시 입력: 측정값=10, 예측값=9, 오차공분산=1, 측정노이즈=2
예시 출력: 필터링된 값(예: 9.67)
*/
typedef struct { float x, p, q, r; } kalman1d_t;
void kalman1d_init(kalman1d_t *kf, float q, float r, float x0, float p0);
float kalman1d_update(kalman1d_t *kf, float z);

// 68. 부트로더(Bootloader)의 애플리케이션 점프 로직
/*
설명: CRC 체크로 유효성 검사 후, 벡터 테이블 재배치, 애플리케이션 시작 주소로 점프(의사 코드).
배경: 안전한 펌웨어 업데이트, 부트로더 설계의 핵심.
예시 입력: CRC 체크 통과
예시 출력: 애플리케이션 실행 시작
*/
void bootloader_jump_to_app(uint32_t app_addr);

// 69. GPIO 드라이브 강도(Drive Strength) 제어의 의미
/*
설명: 고속 신호에서 링잉(ringing) 현상 원인, 드라이브 강도 조절로 완화.
배경: 하드웨어 신호 무결성, EMI 저감.
예시 입력: 드라이브 강도 High/Low 설정
예시 출력: 신호 품질 개선, 링잉 감소
*/

// 70. AUTOSAR COM 스택의 Signal-to-PDU 매핑 과정
/*
설명: 신호(Signal)를 PDU로 매핑, PDU 라우터를 통해 CAN 프레임으로 변환.
배경: 계층적 추상화로 모듈화, 유지보수성 향상.
예시 입력: 신호 값 0x12
예시 출력: CAN 프레임 데이터에 0x12 포함
*/

// 71. Rate Limiter 알고리즘 구현 (Token Bucket)
/*
설명: 토큰 버킷 알고리즘으로 이벤트 발생률 제한.
배경: 네트워크/버스 과부하 방지, 속도 제어.
예시 입력: 5초간 10회 이벤트 요청, 버킷 용량 5, 리필 1초 1개
예시 출력: 5회만 허용, 나머지는 거부
*/
typedef struct { int tokens, capacity; int refill_rate; int last_time; } token_bucket_t;
void token_bucket_init(token_bucket_t *tb, int cap, int rate);
bool token_bucket_consume(token_bucket_t *tb, int now);

// 72. 기능 안전성(ISO 26262)과 MPU
/*
설명: ASIL 등급별 소프트웨어 분리, MPU로 간섭 방지.
배경: 안전 필수 시스템, 메모리 보호.
예시 입력: ASIL-D/ASIL-A 소프트웨어 분리
예시 출력: MPU 설정으로 상호 간섭 차단
*/

// 73. UDS 펌웨어 업데이트(Flashing) 시퀀스 구현
/*
설명: RequestDownload, TransferData, RequestTransferExit 순서로 펌웨어 수신 및 저장.
배경: 자동차 ECU 펌웨어 업데이트 표준 시퀀스.
예시 입력: 0x34(다운로드 요청), 0x36(데이터), 0x37(종료)
예시 출력: 플래시 메모리에 펌웨어 저장 완료
*/
void uds_flash_sequence(const uint8_t *fw_data, int fw_len);

// 74. 공간 벡터 PWM(SVPWM) 알고리즘
/*
설명: 3상 모터 제어에서 SVPWM의 원리, 6개 스위치 ON/OFF 시간 계산(의사 코드).
배경: 고효율, 저토크 리플 모터 제어.
예시 입력: 목표 전압 벡터 (Vd, Vq)
예시 출력: 각 스위치의 ON/OFF 시간
*/

// 75. CAN 버스 침입 탐지 시스템(IDS) 알고리즘
/*
설명: CAN 메시지 주기 모니터링, 비정상 트래픽 탐지.
배경: 차량 보안, DoS 공격 대응.
예시 입력: 정상 주기 10ms, 실제 2ms 반복 메시지
예시 출력: 이상 탐지(알람 발생)
*/
void can_ids_monitor(uint32_t msg_id, uint32_t timestamp);

// 76. 스택 사용량 분석 및 최적화
/*
설명: 스택 오버플로우 위험, 정적/동적 분석(스택 페인팅) 방법.
배경: 임베디드 시스템 신뢰성, 안전성.
예시 입력: 스택 크기 1024, 함수 호출 깊이 100
예시 출력: 사용량 400, 남은 스택 624
*/

// 77. 비트뱅잉을 이용한 SPI 슬레이브 구현
/*
설명: GPIO로 SPI 슬레이브 에뮬레이션, 클럭/데이터 동기화.
배경: 하드웨어 SPI 없는 MCU에서 통신 구현.
예시 입력: 마스터가 0xA5 전송
예시 출력: 슬레이브가 0xA5 수신
*/
void spi_slave_bitbang(void);

// 78. 플래시(Flash) 드라이버 구현
/*
설명: 페이지 쓰기, 섹터 지우기, 칩 지우기 기능 구현.
배경: 비휘발성 메모리 제어, 데이터 저장.
예시 입력: flash_write_page(0x1000, buf, 256)
예시 출력: 0x1000~0x10FF에 데이터 저장
*/
bool flash_write_page(uint32_t addr, const uint8_t *buf, int len);
bool flash_erase_sector(uint32_t addr);
bool flash_erase_chip(void);

// 79. 우선순위 기반의 선점형 스케줄러 구현
/*
설명: 타이머 인터럽트로 문맥 전환, 높은 우선순위 태스크 실행.
배경: 실시간성 보장, RTOS 핵심.
예시 입력: 태스크 3개, 우선순위 1,2,3
예시 출력: 우선순위 3 태스크가 선점 실행
*/
void preemptive_scheduler_run(void);

// 80. Lock-Free 자료구조 (Lock-Free Queue)
/*
설명: 원자적 연산으로 Mutex 없이 안전하게 접근 가능한 Lock-Free 큐 구현.
배경: 멀티코어, 실시간성, 병목 최소화.
예시 입력: 2개 스레드가 동시에 enqueue/dequeue
예시 출력: 데이터 손실/중복 없이 안전하게 처리
*/
typedef struct { int buf[8]; int head, tail; } lf_queue_t;
bool lf_queue_enqueue(lf_queue_t *q, int v);
bool lf_queue_dequeue(lf_queue_t *q, int *v);

// 81. assert() 매크로의 활용
/*
설명: 디버그 빌드에서만 활성화, 계약에 의한 설계(Design by Contract) 구현.
배경: 버그 조기 발견, 코드 신뢰성 향상.
예시 입력: assert(x > 0)
예시 출력: x<=0이면 프로그램 중단(디버그 빌드)
*/

// 82. DMA를 이용한 ADC 연속 변환
/*
설명: 여러 ADC 채널을 DMA로 연속 스캔, 결과를 메모리 버퍼로 전송(의사 코드).
배경: CPU 부하 감소, 고속 데이터 수집.
예시 입력: 4채널 ADC, 100샘플
예시 출력: 버퍼에 4x100 데이터 저장
*/

// 83. UDS Authentication (0x29) 서비스의 개념
/*
설명: SecurityAccess(0x27)와의 차이, PKI 기반 인증의 장점.
배경: 강력한 보안, 위변조 방지.
예시 입력: 인증 요청, 공개키 기반 서명
예시 출력: 인증 성공/실패
*/

// 84. CORDIC 알고리즘 구현
/*
설명: 부동소수점 없이 sin, cos 계산. 시프트/덧셈만 사용.
배경: FPU 없는 MCU에서 삼각함수 연산.
예시 입력: 각도 45도
예시 출력: sin=0.707, cos=0.707 (근사값)
*/
void cordic_sin_cos(float theta, float *s, float *c);

// 85. Fast Fourier Transform (FFT) 구현
/*
설명: 재귀적 FFT 알고리즘으로 주파수 성분 분석.
배경: 신호처리, 진동/오디오 분석.
예시 입력: 8포인트 신호 배열
예시 출력: 주파수별 복소수 스펙트럼
*/
void fft(float *real, float *imag, int n);

// 86. 데드락(Deadlock) 발생 조건 및 예방
/*
설명: 데드락 4가지 조건, 예방 전략(락 순서 지정 등).
배경: 멀티스레드, 동기화 설계.
예시 입력: 두 스레드가 서로 다른 락을 반대 순서로 획득 시도
예시 출력: 데드락 발생/예방
*/

// 87. Watchdog을 이용한 시스템 복구
/*
설명: Watchdog 리셋 감지, 오류 횟수 NVM 기록, 안전 모드 진입.
배경: 시스템 신뢰성, 장애 복구.
예시 입력: Watchdog 리셋 발생
예시 출력: 오류 카운트 증가, 안전 모드 진입
*/

// 88. MISRA C 규칙의 중요성
/*
설명: MISRA C 표준 목적, 동적 메모리/재귀 금지 등 규칙의 안전성 의미.
배경: 안전 필수 시스템, 코드 품질.
예시 입력: malloc 사용, 재귀 함수 작성
예시 출력: MISRA 위반 경고/오류
*/

// 89. AUTOSAR SecOC(Secure On-board Communication)의 개념
/*
설명: MAC, Freshness Value로 메시지 인증.
배경: CAN 통신 위변조 방지, 보안성 강화.
예시 입력: 메시지+MAC+Freshness
예시 출력: 인증 성공/실패
*/

// 90. 타임 트리거(Time-Triggered) 아키텍처
/*
설명: 이벤트 기반과 타임 트리거 시스템 차이, FlexRay의 결정론적 통신.
배경: 실시간성, 예측 가능성.
예시 입력: 1ms마다 주기적 작업
예시 출력: 항상 1ms마다 정확히 실행
*/

// 91. 전력 소모 최적화
/*
설명: CPU 클럭 조절, 주변장치 비활성화, 저전력 모드 활용 등 종합 설계.
배경: 배터리 수명 연장, 에너지 효율.
예시 입력: 불필요한 센서 OFF, 슬립 모드 진입
예시 출력: 전류 소모 감소
*/

// 92. A/B 파티션을 이용한 안전한 OTA 펌웨어 업데이트
/*
설명: 두 파티션 중 한쪽에 새 펌웨어 다운로드, 유효성 검사 후 부팅 파티션 전환.
배경: OTA 실패 시 벽돌 방지, 신뢰성 향상.
예시 입력: A파티션 실행 중, B에 새 펌웨어 다운로드
예시 출력: 부팅 파티션 B로 전환, 정상 부팅
*/

// 93. setjmp와 longjmp를 이용한 예외 처리
/*
설명: setjmp/longjmp로 try-catch 유사 예외 처리, 스택 영향/위험성.
배경: C에서 예외 처리, 비정상 흐름 제어.
예시 입력: setjmp로 점프 위치 저장, longjmp로 예외 발생
예시 출력: 예외 발생 시 지정 위치로 복귀
*/

// 94. 메모리 배리어(Memory Barrier)의 필요성
/*
설명: 명령어 재배치/비순차 실행으로 인한 동기화 문제, 메모리 배리어로 해결.
배경: 멀티코어, 동기화 신뢰성.
예시 입력: 쓰기-읽기 순서 보장 필요
예시 출력: 메모리 배리어로 순서 보장
*/

// 95. CRC32 하드웨어 가속기 사용
/*
설명: MCU 내장 CRC 하드웨어로 데이터 블록 CRC32 계산.
배경: 소프트웨어보다 빠른 오류 검출.
예시 입력: buf={0x01,0x02,0x03}, len=3
예시 출력: CRC32 값(예: 0xA3830348)
*/
uint32_t hw_crc32(const uint8_t *buf, int len);

// 96. UDS RoutineControl (0x31) 서비스 구현
/*
설명: 루틴 시작, 중지, 결과 요청 기능 구현.
배경: 진단 서비스, ECU 기능 제어.
예시 입력: routine_id=0x1234, start
예시 출력: 루틴 실행, 결과 반환
*/
int uds_routine_control(uint16_t routine_id, uint8_t subfunc, uint8_t *data, int len);

// 97. 가상 EEPROM 구현
/*
설명: 플래시 일부를 마모 균등화(wear-leveling) 적용 가상 EEPROM으로 사용.
배경: 플래시 내구성 향상, 데이터 신뢰성.
예시 입력: veeprom_write(0x10, 0x55)
예시 출력: 0x10 주소에 0x55 저장, wear-leveling 적용
*/
bool veeprom_write(uint16_t addr, uint8_t data);
bool veeprom_read(uint16_t addr, uint8_t *data);

// 98. DMA를 이용한 PWM 파형 생성
/*
설명: 듀티 배열을 DMA로 타이머 CCR에 전송, CPU 개입 없이 PWM 파형 생성.
배경: 복잡한 파형, CPU 부하 감소.
예시 입력: duty[]={10,20,30,40}
예시 출력: PWM 듀티가 순차적으로 변경됨
*/
void dma_pwm_start(const uint16_t *duty, int len);

// 99. CAN FD의 BRS(Bit Rate Switch)와 ESI(Error State Indicator)
/*
설명: CAN FD의 BRS/ESI 역할, 데이터 처리량/네트워크 안정성 기여.
배경: 고속 통신, 오류 상태 표시.
예시 입력: BRS=1, ESI=0
예시 출력: 데이터 구간에서 속도 증가, 정상 상태 표시
*/

// 100. 시스템 상태 관리자(System State Manager) 설계
/*
설명: 차량 상태(OFF, ACC, IGN, CRANK)에 따라 각 모듈의 동작을 총괄하는 중앙 상태 관리자 설계.
배경: 시스템 전체 제어, 모듈 간 연동.
예시 입력: 상태 전이 IGNITION→CRANK
예시 출력: 각 모듈이 상태에 맞게 동작/초기화/비활성화
*/
typedef enum { SYS_OFF, SYS_ACC, SYS_IGN, SYS_CRANK } sys_state_t;
void system_state_manager(sys_state_t state);
