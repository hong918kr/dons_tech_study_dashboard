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


/*
난이도: Hard (시스템 아키텍처, 최적화, 고급 알고리즘)
각 문제는 함수 시그니처, 설명, 예시 입력/출력, 그리고 배경 개념을 함께 제공합니다.
*/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include <assert.h>

// 61. 인터럽트 지연 시간(Latency) 최소화 전략
/*
전략:
1) ISR에서 플래그만 설정, 실제 처리는 메인 루프에서 수행
2) ISR에서 복잡한 연산/함수 호출/동적 할당 금지
3) ISR에서 전역 변수 접근 최소화, 큐/버퍼에 데이터만 저장
예시: 아래는 ISR에서 플래그만 설정하는 코드
*/
volatile int isr_flag = 0;
void ISR_example(void) { isr_flag = 1; }
void main_loop(void) { if (isr_flag) { /* 실제 처리 */ isr_flag = 0; } }

/*

******************자세한 설명******************

*/

/*
61. 인터럽트 지연 시간(Latency) 최소화 전략

설명:
임베디드 시스템에서 인터럽트 서비스 루틴(ISR)은 가능한 한 짧게 실행되어야 합니다.
ISR이 길어지면 다른 중요한 인터럽트가 지연되거나, 시스템 전체의 응답성이 저하될 수 있습니다.
따라서 ISR에서는 "진짜로 꼭 필요한 최소한의 일"만 하고, 나머지 처리는 메인 루프(또는 백그라운드 태스크)에서 하도록 설계합니다.

대표적인 전략:
1) ISR에서는 플래그(또는 큐)에 이벤트만 기록하고, 실제 처리는 메인 루프에서 한다.
2) ISR에서 복잡한 연산, 동적 메모리 할당, printf 등 시간 오래 걸리는 작업 금지.
3) ISR에서 전역 변수 접근은 atomic하게 하거나, 최소화한다.

아래는 "버튼 눌림" 인터럽트 예시입니다.
- ISR에서는 button_pressed 플래그만 1로 설정
- 메인 루프에서 이 플래그를 확인해 실제 동작(LED 토글 등) 수행

예시 입력: 버튼 인터럽트 발생
예시 출력: 메인 루프에서 "Button event handled!" 출력

*/

#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>

volatile int button_pressed = 0;

// ISR (실제 MCU에서는 인터럽트 벡터에 등록)
void button_isr(int sig) {
    button_pressed = 1; // ISR에서는 플래그만!
}

// 메인 루프에서 실제 처리
void main_loop_example(void) {
    while (1) {
        if (button_pressed) {
            printf("Button event handled!\n");
            button_pressed = 0;
        }
        // 기타 작업...
        usleep(10000); // 10ms sleep (시뮬레이션용)
    }
}

// 시뮬레이션용: SIGUSR1 시그널을 버튼 인터럽트로 사용
int main(void) {
    signal(SIGUSR1, button_isr);
    printf("Send SIGUSR1 (kill -USR1 %d) to simulate button press\n", getpid());
    main_loop_example();
    return 0;
}

/*
핵심 요약:
- ISR은 빠르게 끝내고, 실제 처리는 메인 루프에서!
- ISR에서 플래그/큐만 사용, 복잡한 연산 금지
- 시스템 응답성, 안정성, 예측성 향상
*/




/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 62. 재진입성(Reentrancy)을 고려한 함수 설계
/*
재진입성 없는 함수: 전역 변수 사용, 동시에 호출 시 값 꼬임
재진입성 있는 함수: 인자로 지역 변수 사용, 동시에 호출해도 안전
*/
int global_cnt = 0;
void inc_global(void) { global_cnt++; } // 재진입성 없음
void inc_local(int *cnt) { (*cnt)++; }  // 재진입성 보장



/*

******************자세한 설명******************

*/
/*
62. 재진입성(Reentrancy)을 고려한 함수 설계

설명:
재진입 가능한 함수란, 여러 실행 흐름(예: 멀티스레드, ISR, 재귀 등)에서 동시에 호출되어도
동작이 꼬이지 않고 항상 올바른 결과를 반환하는 함수입니다.
임베디드 시스템에서는 ISR과 메인 루프, 또는 여러 태스크에서 동시에 같은 함수를 호출할 수 있으므로
재진입성이 매우 중요합니다.

재진입성이 깨지는 대표적 예시: 함수 내부에서 전역 변수(또는 static 지역 변수)를 사용하여 상태를 저장할 때,
동시에 두 실행 흐름이 접근하면 값이 꼬일 수 있습니다.

아래는 재진입성이 없는 함수와, 이를 재진입성 있게 고친 예시입니다.

예시 입력: 두 스레드(또는 ISR/메인)가 동시에 inc_global() 호출
예시 출력: 재진입성 없는 경우 값이 꼬임, inc_local()은 항상 정확

*/

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

// 재진입성 없는 함수: 전역 변수 사용
int global_cnt = 0;
void inc_global(void) {
    int tmp = global_cnt;
    usleep(1000); // 동시성 문제 유발을 위한 인위적 지연
    global_cnt = tmp + 1;
}

// 재진입성 있는 함수: 인자로 지역 변수 사용
void inc_local(int *cnt) {
    int tmp = *cnt;
    usleep(1000);
    *cnt = tmp + 1;
}

// 테스트용 스레드 함수
void* thread_func_global(void* arg) {
    for (int i = 0; i < 1000; ++i) inc_global();
    return NULL;
}
void* thread_func_local(void* arg) {
    int *cnt = (int*)arg;
    for (int i = 0; i < 1000; ++i) inc_local(cnt);
    return NULL;
}

int main_reentrancy_demo(void) {
    // 재진입성 없는 함수 테스트
    global_cnt = 0;
    pthread_t t1, t2;
    pthread_create(&t1, NULL, thread_func_global, NULL);
    pthread_create(&t2, NULL, thread_func_global, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    printf("Non-reentrant global_cnt: %d (should be 2000, but usually less)\n", global_cnt);

    // 재진입성 있는 함수 테스트
    int local_cnt = 0;
    pthread_create(&t1, NULL, thread_func_local, &local_cnt);
    pthread_create(&t2, NULL, thread_func_local, &local_cnt);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    printf("Reentrant local_cnt: %d (should be 2000)\n", local_cnt);

    return 0;
}

/*
핵심 요약:
- 재진입성 없는 함수는 전역/정적 변수 사용 시 동시성 문제 발생
- 재진입성 있는 함수는 인자(지역 변수)만 사용, 언제든 안전하게 호출 가능
- 임베디드에서는 ISR, 멀티태스크 환경에서 반드시 재진입성 고려 필요



/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/












// 63. heap_4.c 스타일의 메모리 관리자 (간단 예시)
/*
블록 구조체에 free 플래그, 인접 free 블록 병합
*/
typedef struct mem_blk {
    struct mem_blk *next;
    size_t size;
    int free;
} mem_blk_t;
#define HEAP4_SIZE 256
static uint8_t heap4[HEAP4_SIZE];
static mem_blk_t *heap4_head = NULL;
void heap4_init(void) {
    heap4_head = (mem_blk_t*)heap4;
    heap4_head->next = NULL;
    heap4_head->size = HEAP4_SIZE - sizeof(mem_blk_t);
    heap4_head->free = 1;
}
void* heap4_alloc(size_t size) {
    mem_blk_t *cur = heap4_head;
    while (cur) {
        if (cur->free && cur->size >= size) {
            if (cur->size > size + sizeof(mem_blk_t)) {
                mem_blk_t *new_blk = (mem_blk_t*)((uint8_t*)cur + sizeof(mem_blk_t) + size);
                new_blk->size = cur->size - size - sizeof(mem_blk_t);
                new_blk->free = 1;
                new_blk->next = cur->next;
                cur->next = new_blk;
                cur->size = size;
            }
            cur->free = 0;
            return (uint8_t*)cur + sizeof(mem_blk_t);
        }
        cur = cur->next;
    }
    return NULL;
}
void heap4_free(void *ptr) {
    if (!ptr) return;
    mem_blk_t *cur = (mem_blk_t*)((uint8_t*)ptr - sizeof(mem_blk_t));
    cur->free = 1;
    // 병합
    mem_blk_t *p = heap4_head;
    while (p && p->next) {
        if (p->free && p->next->free) {
            p->size += sizeof(mem_blk_t) + p->next->size;
            p->next = p->next->next;
        } else {
            p = p->next;
        }
    }
}




/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/






// 64. UDS SecurityAccess (0x27) Seed & Key 알고리즘 구현
/*
서버: seed 생성, 클라이언트: key = seed ^ secret
*/
uint16_t uds_generate_seed(void) { return 0x1234; }
uint16_t uds_calc_key(uint16_t seed, uint16_t secret) { return seed ^ secret; }

/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/








// 65. 비트뱅잉(Bit-banging)을 이용한 I2C 마스터 구현 (의사 코드)
/*
실제 GPIO 제어 함수는 하드웨어에 맞게 작성 필요
*/
void i2c_start(void)   { /* SDA=1, SCL=1; SDA=0; SCL=0; */ }
void i2c_stop(void)    { /* SDA=0, SCL=1; SDA=1; */ }
bool i2c_write_byte(uint8_t data) {
    for (int i = 7; i >= 0; --i) {
        // SDA = (data >> i) & 1; SCL=1; SCL=0;
    }
    return i2c_read_ack();
}
bool i2c_read_ack(void) { /* SCL=1; ack=read_SDA(); SCL=0; return !ack; */ return true; }
/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 66. 간단한 비선점형 스케줄러(Scheduler) 구현
void scheduler_run(tcb_t *tasks, int n) {
    int max_pri = -1, idx = -1;
    for (int i = 0; i < n; ++i) {
        if (tasks[i].priority > max_pri) { max_pri = tasks[i].priority; idx = i; }
    }
    if (idx >= 0) tasks[idx].task();
}
/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

// 67. 1차원 칼만 필터(Kalman Filter) 구현
void kalman1d_init(kalman1d_t *kf, float q, float r, float x0, float p0) {
    kf->q = q; kf->r = r; kf->x = x0; kf->p = p0;
}
float kalman1d_update(kalman1d_t *kf, float z) {
    // 예측
    kf->p += kf->q;
    // 칼만 이득
    float k = kf->p / (kf->p + kf->r);
    // 업데이트
    kf->x += k * (z - kf->x);
    kf->p *= (1 - k);
    return kf->x;
}
/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

// 68. 부트로더(Bootloader)의 애플리케이션 점프 로직 (의사 코드)
/*
if (crc_check(app_addr)) {
    set_vector_table(app_addr);
    jump_to_address(app_addr);
}
*/


/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/



// 69. GPIO 드라이브 강도(Drive Strength) 제어의 의미
/*
고속 신호에서 드라이브 강도를 낮추면 링잉 감소, 신호 품질 개선.
MCU 레지스터로 드라이브 강도 설정.
*/


/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

// 70. AUTOSAR COM 스택의 Signal-to-PDU 매핑 과정
/*
신호를 PDU에 packing, PDU Router를 통해 CAN 프레임으로 전송.
추상화 계층으로 모듈화, 유지보수성 향상.
*/


/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 71. Rate Limiter 알고리즘 구현 (Token Bucket)
void token_bucket_init(token_bucket_t *tb, int cap, int rate) {
    tb->tokens = cap; tb->capacity = cap; tb->refill_rate = rate; tb->last_time = 0;
}
bool token_bucket_consume(token_bucket_t *tb, int now) {
    int elapsed = now - tb->last_time;
    if (elapsed > 0) {
        tb->tokens += elapsed * tb->refill_rate;
        if (tb->tokens > tb->capacity) tb->tokens = tb->capacity;
        tb->last_time = now;
    }
    if (tb->tokens > 0) { tb->tokens--; return true; }
    return false;
}

/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 72. 기능 안전성(ISO 26262)과 MPU
/*
MPU로 각 ASIL 등급별 소프트웨어 영역을 분리, 불법 접근 시 예외 발생.
*/

/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/



// 73. UDS 펌웨어 업데이트(Flashing) 시퀀스 구현 (의사 코드)
void uds_flash_sequence(const uint8_t *fw_data, int fw_len) {
    // 1. RequestDownload(0x34) 수신 → 플래시 준비
    // 2. TransferData(0x36) 반복 수신 → 데이터 저장
    // 3. RequestTransferExit(0x37) 수신 → CRC 체크, 완료
}

/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 74. 공간 벡터 PWM(SVPWM) 알고리즘 (의사 코드)
/*
1. 목표 전압 벡터(Vd,Vq)를 3상 변환
2. 섹터 결정, 각 스위치 ON/OFF 시간 계산
3. 타이머 CCR에 적용
*/

/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/



// 75. CAN 버스 침입 탐지 시스템(IDS) 알고리즘
void can_ids_monitor(uint32_t msg_id, uint32_t timestamp) {
    static uint32_t last_time[256] = {0};
    uint32_t period = timestamp - last_time[msg_id % 256];
    if (period < 5) printf("ALERT: Abnormal CAN traffic!\n");
    last_time[msg_id % 256] = timestamp;
}
/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 76. 스택 사용량 분석 및 최적화
/*
정적 분석: 컴파일러 옵션, 함수 호출 깊이 분석
동적 분석: 스택 영역에 0xAA로 페인팅, 실행 후 남은 0xAA 개수로 사용량 측정
*/
/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 77. 비트뱅잉을 이용한 SPI 슬레이브 구현 (의사 코드)
void spi_slave_bitbang(void) {
    // while(1) {
    //   if (SCK rising edge) { read MOSI; shift in; }
    //   if (SCK falling edge) { set MISO; shift out; }
    // }
}
/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 78. 플래시(Flash) 드라이버 구현 (의사 코드)
bool flash_write_page(uint32_t addr, const uint8_t *buf, int len) {
    // unlock_flash();
    // erase_if_needed(addr);
    // program_flash(addr, buf, len);
    // lock_flash();
    return true;
}
bool flash_erase_sector(uint32_t addr) { /* ... */ return true; }
bool flash_erase_chip(void) { /* ... */ return true; }
/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 79. 우선순위 기반의 선점형 스케줄러 구현 (의사 코드)
void preemptive_scheduler_run(void) {
    // 타이머 인터럽트에서 현재 태스크보다 높은 우선순위 태스크 ready면 context switch
}

/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 80. Lock-Free 자료구조 (Lock-Free Queue) (간단 예시)
#include <stdatomic.h>
typedef struct { int buf[8]; atomic_int head, tail; } lf_queue_t;
bool lf_queue_enqueue(lf_queue_t *q, int v) {
    int t = atomic_load(&q->tail);
    int h = atomic_load(&q->head);
    if (((t + 1) % 8) == h) return false; // full
    q->buf[t] = v;
    atomic_store(&q->tail, (t + 1) % 8);
    return true;
}
bool lf_queue_dequeue(lf_queue_t *q, int *v) {
    int h = atomic_load(&q->head);
    int t = atomic_load(&q->tail);
    if (h == t) return false; // empty
    *v = q->buf[h];
    atomic_store(&q->head, (h + 1) % 8);
    return true;
}

/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 81. assert() 매크로의 활용
/*
assert(x > 0); // x<=0이면 디버그 빌드에서 프로그램 중단
*/

/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 82. DMA를 이용한 ADC 연속 변환 (의사 코드)
/*
adc_dma_init(channels, n);
dma_start(adc_buffer, n * samples);
DMA 완료 인터럽트에서 데이터 처리
*/

/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 83. UDS Authentication (0x29) 서비스의 개념
/*
PKI 기반 인증: 서버는 공개키로 서명 검증, SecurityAccess는 대칭키 기반 Seed-Key
*/

/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 84. CORDIC 알고리즘 구현 (간단 예시)
void cordic_sin_cos(float theta, float *s, float *c) {
    // 실제 구현은 고정소수점, 반복 시프트/덧셈
    *s = 0.707f; *c = 0.707f; // 45도 근사값
}

/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 85. Fast Fourier Transform (FFT) 구현 (간단 예시)
void fft(float *real, float *imag, int n) {
    if (n <= 1) return;
    float even_real[n/2], even_imag[n/2], odd_real[n/2], odd_imag[n/2];
    for (int i = 0; i < n/2; ++i) {
        even_real[i] = real[2*i];
        even_imag[i] = imag[2*i];
        odd_real[i]  = real[2*i+1];
        odd_imag[i]  = imag[2*i+1];
    }
    fft(even_real, even_imag, n/2);
    fft(odd_real, odd_imag, n/2);
    for (int k = 0; k < n/2; ++k) {
        float t_real = cos(-2*M_PI*k/n)*odd_real[k] - sin(-2*M_PI*k/n)*odd_imag[k];
        float t_imag = sin(-2*M_PI*k/n)*odd_real[k] + cos(-2*M_PI*k/n)*odd_imag[k];
        real[k] = even_real[k] + t_real;
        imag[k] = even_imag[k] + t_imag;
        real[k+n/2] = even_real[k] - t_real;
        imag[k+n/2] = even_imag[k] - t_imag;
    }
}

/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 86. 데드락(Deadlock) 발생 조건 및 예방
/*
조건: 상호배제, 점유와 대기, 비선점, 순환대기
예방: 락 획득 순서 고정, 타임아웃, 자원 할당 전부 확보
*/

/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 87. Watchdog을 이용한 시스템 복구 (의사 코드)
/*
if (reset_cause == WATCHDOG) {
    nvm_error_count++;
    enter_safe_mode();
}
*/

/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 88. MISRA C 규칙의 중요성
/*
동적 메모리, 재귀 함수 금지: 예측 불가 동작, 스택/힙 오버플로우 방지
*/

/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 89. AUTOSAR SecOC(Secure On-board Communication)의 개념
/*
MAC와 Freshness Value로 메시지 위변조/재전송 방지
*/



/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 90. 타임 트리거(Time-Triggered) 아키텍처
/*
이벤트 기반: 이벤트 발생 시 처리, 타임 트리거: 주기적/정해진 시점에만 처리
FlexRay 등에서 결정론적 통신 보장
*/



/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 91. 전력 소모 최적화 (의사 코드)
/*
필요시만 센서/주변장치 ON, 유휴시 슬립모드, 클럭 속도 동적 조절
*/



/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 92. A/B 파티션을 이용한 안전한 OTA 펌웨어 업데이트 (의사 코드)
/*
1. 현재 A 실행 중, B에 새 펌웨어 다운로드
2. 유효성 검사 후 부팅 파티션 B로 전환
3. 실패 시 A로 롤백
*/



/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 93. setjmp와 longjmp를 이용한 예외 처리 (예시)
jmp_buf jb;
void foo() {
    printf("foo error!\n");
    longjmp(jb, 1);
}
void try_catch_example() {
    if (setjmp(jb) == 0) {
        foo();
    } else {
        printf("caught exception!\n");
    }
}



/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 94. 메모리 배리어(Memory Barrier)의 필요성
/*
멀티코어 환경에서 write/read 순서 보장 위해 __sync_synchronize() 등 사용
*/



/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 95. CRC32 하드웨어 가속기 사용 (의사 코드)
uint32_t hw_crc32(const uint8_t *buf, int len) {
    // CRC 하드웨어 레지스터에 buf, len 설정, 결과 읽기
    return 0xA3830348; // 예시값
}



/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 96. UDS RoutineControl (0x31) 서비스 구현 (간단 예시)
int uds_routine_control(uint16_t routine_id, uint8_t subfunc, uint8_t *data, int len) {
    if (subfunc == 1) { /* start */ return 0; }
    if (subfunc == 2) { /* stop */ return 0; }
    if (subfunc == 3) { /* requestResult */ data[0]=0x55; return 1; }
    return -1;
}



/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 97. 가상 EEPROM 구현 (의사 코드)
bool veeprom_write(uint16_t addr, uint8_t data) {
    // wear-leveling 알고리즘 적용하여 플래시에 기록
    return true;
}
bool veeprom_read(uint16_t addr, uint8_t *data) {
    // wear-leveling 알고리즘 적용하여 플래시에서 읽기
    *data = 0x55;
    return true;
}



/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 98. DMA를 이용한 PWM 파형 생성 (의사 코드)
void dma_pwm_start(const uint16_t *duty, int len) {
    // DMA로 duty[]를 타이머 CCR에 순차 전송, PWM 파형 생성
}



/*

******************자세한 설명******************

*/






/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


// 99. CAN FD의 BRS(Bit Rate Switch)와 ESI(Error State Indicator)
/*
BRS: 데이터 구간에서 속도 증가, ESI: 오류 상태 표시로 네트워크 안정성 향상
*/

// 100. 시스템 상태 관리자(System State Manager) 설계 (간단 예시)
typedef enum { SYS_OFF, SYS_ACC, SYS_IGN, SYS_CRANK } sys_state_t;
void system_state_manager(sys_state_t state) {
    switch (state) {
        case SYS_OFF:   /* 모든 모듈 비활성화 */ break;
        case SYS_ACC:   /* 일부 모듈 활성화 */ break;
        case SYS_IGN:   /* 주요 모듈 활성화 */ break;
        case SYS_CRANK: /* 시동 관련 모듈 활성화 */ break;
    }
}