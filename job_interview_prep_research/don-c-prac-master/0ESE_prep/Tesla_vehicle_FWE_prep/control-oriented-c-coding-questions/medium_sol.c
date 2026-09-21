/*
난이도: Medium (자료구조, 알고리즘, RTOS 기초)
각 문제는 함수 시그니처, 설명, 예시 입력/출력, 그리고 배경 개념을 함께 제공합니다.
*/

/*
난이도: Medium (자료구조, 알고리즘, RTOS 기초)
이 단계는 원형 버퍼와 같은 효율적인 자료구조, PID 제어와 같은 핵심 제어 알고리즘, 그리고 RTOS의 기본 동기화 메커니즘(Mutex 등)에 대한 이해를 요구합니다. 실제 하드웨어 드라이버와 통신 프로토콜의 세부 사항을 다루는 문제들이 포함됩니다.

26. 원형 버퍼(Circular Buffer) 구현: 지정된 크기의 원형 버퍼에 데이터를 추가(enqueue)하고 제거(dequeue)하는 함수들을 구현하세요. 버퍼가 가득 찼을 때와 비었을 때를 정확히 처리해야 합니다.
27. 상태 머신(FSM)을 이용한 프로토콜 파서: [Header][Length][Payload][Checksum] 구조를 갖는 통신 프로토콜의 패킷을 파싱하는 상태 머신을 구현하세요.
28. 고정 소수점(Fixed-Point) 곱셈/나눗셈: Q15.16 형식의 두 고정 소수점 숫자를 곱하고 나누는 함수를 구현하세요. 오버플로우와 정밀도 손실을 고려해야 합니다.
29. FIR 필터(Finite Impulse Response Filter) 구현: 주어진 계수(coefficients) 배열을 사용하여 FIR 필터를 구현하는 함수를 작성하세요.
30. 간단한 PID 제어기(PID Controller) 구현: P, I, D 게인 값과 목표값(setpoint), 현재값(process variable)을 입력받아 제어 출력값을 계산하는 PID 제어기 함수를 구현하세요. 누적 오차(Integral windup) 방지 로직을 포함해야 합니다.
31. ISR과 메인 루프 간 공유 자원 보호: 인터럽트 서비스 루틴(ISR)과 메인 루프에서 공유 변수(예: 카운터)를 안전하게 접근하기 위해 인터럽트를 비활성화/활성화하는 방법을 사용하여 코드를 작성하세요.
32. Mutex를 사용한 임계 구역(Critical Section) 보호: 두 개의 태스크(스레드)가 공유 버퍼에 접근할 때 데이터 손상을 방지하기 위해 Mutex를 사용하여 임계 구역을 보호하는 코드를 작성하세요. (RTOS API 사용 가정)
33. 타이머 기반 버튼 디바운싱: 딜레이 함수를 사용하지 않고, 주기적인 타이머 인터럽트와 상태 카운터를 이용하여 버튼 입력을 디바운싱하는 로직을 구현하세요.
34. CAN 메시지 패킹(Packing) 함수: 여러 개의 작은 신호(예: 4비트 상태, 12비트 센서 값)를 하나의 64비트 CAN 메시지 데이터 필드에 패킹하는 함수를 작성하세요.
35. CAN 메시지 언패킹(Unpacking) 함수: 64비트 CAN 메시지 데이터 필드에서 특정 위치와 길이의 신호 값을 추출하여 언패킹하는 함수를 작성하세요.
36. 메모리 풀(Memory Pool) 할당자 구현: 고정된 크기의 블록 여러 개로 구성된 메모리 풀에서 메모리를 할당(alloc)하고 해제(free)하는 간단한 메모리 풀 관리자를 구현하세요.
37. 소프트웨어 워치독 구현: 여러 태스크가 주기적으로 자신의 "생존 신호"를 알려야 하며, 만약 특정 시간 내에 모든 태스크의 신호가 감지되지 않으면 시스템 리셋을 시뮬레이션하는 소프트웨어 워치독을 구현하세요.
38. UDS ReadDataByIdentifier (0x22) 서비스 핸들러: UDS 요청 22 F1 90 (VIN 읽기)을 수신했을 때, 미리 정의된 DID(Data Identifier) 테이블을 조회하여 해당 데이터를 포함한 긍정 응답 62 F1 90 또는 부정 응답을 생성하는 함수를 구현하세요.
39. 비트 필드를 이용한 상태 플래그 관리: 구조체(struct) 내 비트 필드(bit-field)를 사용하여 여러 개의 상태 플래그(예: FLAG_ERROR, FLAG_READY, FLAG_BUSY)를 하나의 바이트(byte)로 관리하는 코드를 작성하세요.
40. 1차 IIR 필터(Infinite Impulse Response Filter) 구현: 1차 저역 통과(Low-pass) IIR 필터를 y[n] = a*x[n] + (1-a)*y[n-1] 공식에 따라 구현하세요. 고정 소수점 연산을 사용하세요.
41. 우선순위 역전(Priority Inversion) 현상 설명 및 해결책 제시: 우선순위 역전이 무엇인지 설명하고, 이를 해결하기 위한 방법(예: 우선순위 상속)을 Mutex를 사용하는 코드 예시로 설명하세요.
42. 룩업 테이블(Look-Up Table)과 선형 보간: 비선형적인 특성을 가진 센서의 ADC 출력 값을 실제 물리량(예: 온도)으로 변환하기 위해, 선형 보간(linear interpolation) 기능을 포함한 룩업 테이블 기반 변환 함수를 구현하세요.
43. 데드 타임(Dead-Time)을 고려한 PWM 생성: H-브리지 모터 드라이버에서 상단/하단 스위치가 동시에 켜지는 것을 방지하기 위해, 두 개의 상보적인(complementary) PWM 신호 사이에 데드 타임을 삽입하는 로직을 설명하고 의사 코드로 작성하세요.
44. heap_1.c 스타일의 간단한 힙 관리자: FreeRTOS의 heap_1.c와 같이, 메모리 해제를 지원하지 않고 주어진 정적 배열을 요청된 크기로 잘라 할당해주는 가장 간단한 형태의 힙 관리자를 구현하세요.
45. AUTOSAR E2E Profile 1의 개념: AUTOSAR의 E2E(End-to-End) 보호가 왜 필요한지 설명하고, Profile 1에서 사용되는 CRC와 카운터의 기본 개념을 설명하세요.
46. 세마포어(Semaphore)를 이용한 생산자-소비자 문제: 하나의 생산자 태스크가 데이터를 버퍼에 채우고, 하나의 소비자 태스크가 데이터를 버퍼에서 가져가는 시나리오를 카운팅 세마포어를 사용하여 동기화하는 코드를 작성하세요.
47. 메시지 큐(Message Queue)를 이용한 데이터 전달: 센서 값을 읽는 태스크가 측정된 데이터를 메시지 큐를 통해 로깅(logging) 태스크로 전달하는 코드를 구현하세요. (RTOS API 사용 가정)
48. 함수 포인터(Function Pointer)를 이용한 콜백(Callback) 구현: 버튼 누름 이벤트가 발생했을 때, 미리 등록된 콜백 함수를 호출하는 간단한 이벤트 처리기를 구현하세요.
49. 타이머 입력 캡처(Input Capture) 모드: 외부 신호의 주파수와 듀티 사이클을 측정하기 위해 타이머의 입력 캡처 모드를 어떻게 설정하고 사용하는지 의사 코드로 설명하세요.
50. static 키워드의 두 가지 의미: C언어에서 static 키워드가 변수와 함수에 사용될 때 각각 어떤 의미를 가지는지 코드 예시와 함께 설명하세요.
51. 재귀 함수(Recursive Function)의 위험성: 임베디드 시스템에서 재귀 함수를 사용할 때 발생할 수 있는 스택 오버플로우(Stack Overflow) 문제에 대해 설명하세요.
52. UDS WriteDataByIdentifier (0x2E) 서비스 핸들러: UDS 서비스 0x2E 요청을 처리하여 특정 DID에 해당하는 값을 EEPROM에 쓰는 것과 같은 비휘발성 메모리에 저장하는 함수를 구현하세요.
53. DMA를 이용한 UART 데이터 송신: CPU 개입 없이 버퍼에 있는 데이터를 UART로 전송하기 위해 DMA(Direct Memory Access) 컨트롤러를 설정하는 과정을 의사 코드로 설명하세요.
54. 저전력 모드(Low Power Mode) 진입/탈출: MCU를 저전력 모드(예: STOP 모드)로 전환하고, 특정 외부 인터럽트(예: 버튼 입력)에 의해 깨어나는(wake-up) 과정을 구현하세요.
55. CRC-16 (Modbus) 테이블 기반 계산: 미리 계산된 CRC 테이블을 사용하여 주어진 데이터 버퍼의 16비트 CRC(Modbus) 체크섬을 계산하는 함수를 구현하세요.
56. 링크드 리스트(Linked List)를 이용한 동적 데이터 관리: 크기가 변동될 수 있는 이벤트 로그를 관리하기 위해 단일 링크드 리스트를 구현하세요. (동적 할당 함수 사용 가정)
57. 해시 테이블(Hash Table)을 이용한 빠른 데이터 조회: 문자열 키(예: 파라미터 이름)와 정수 값(예: 파라미터 값)을 저장하고 빠르게 조회할 수 있는 간단한 해시 테이블을 구현하세요.
58. #pragma pack의 사용: 구조체 패딩(padding)을 제어하기 위해 #pragma pack 지시어를 사용하는 이유와 예시를 보여주세요.
59. union을 이용한 데이터 타입 변환: union을 사용하여 4개의 uint8_t 배열을 하나의 uint32_t 변수로, 또는 그 반대로 타입 변환(type-punning)하는 방법을 보여주고, 이것이 안전한지에 대해 논하세요.
60. 간단한 이벤트 플래그(Event Flags) 구현: 여러 이벤트 비트를 설정하고, 특정 비트 조합이 만족될 때까지 대기하는 간단한 이벤트 플래그 그룹을 구현하세요.
*/


#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

// 26. 원형 버퍼(Circular Buffer) 구현
/*
원형 버퍼는 FIFO 큐의 일종으로, head/tail 포인터와 count로 가득/비었음을 구분합니다.
임베디드에서 UART, 센서 데이터 버퍼 등에 자주 사용됩니다.
예시 입력: enqueue 1,2,3, dequeue
예시 출력: 1
*/
typedef struct { int buf[8]; int head, tail, count; } circbuf_t;
void circbuf_init(circbuf_t *cb) {
    cb->head = cb->tail = cb->count = 0;
}
bool circbuf_enqueue(circbuf_t *cb, int v) {
    if (cb->count == 8) return false; // full
    cb->buf[cb->tail] = v;
    cb->tail = (cb->tail + 1) % 8;
    cb->count++;
    return true;
}
bool circbuf_dequeue(circbuf_t *cb, int *v) {
    if (cb->count == 0) return false; // empty
    *v = cb->buf[cb->head];
    cb->head = (cb->head + 1) % 8;
    cb->count--;
    return true;
}

// 27. FSM을 이용한 프로토콜 파서
/*
상태 머신(FSM)으로 [Header][Length][Payload][Checksum] 구조의 패킷을 파싱합니다.
각 상태에서 입력 바이트에 따라 상태 전이와 데이터 저장을 수행합니다.
예시 입력: 0xAA, 0x03, 0x11, 0x22, 0x33, 0x66
예시 출력: payload={0x11,0x22,0x33}, return true
*/

/*

******************자세한 설명******************

왜 0x11, 0x22, 0x33만 payload에 들어가는지 자세히 설명드리겠습니다.

문제 구조 및 예시 입력
입력 시퀀스:
0xAA, 0x03, 0x11, 0x22, 0x33, 0x66

프로토콜 구조:
[Header][Length][Payload][Checksum]

Header: 0xAA (패킷 시작 알림)
Length: 다음에 올 Payload 바이트 개수 (여기선 0x03 → 3바이트)
Payload: 실제 데이터 (여기선 0x11, 0x22, 0x33)
Checksum: Payload의 모든 바이트를 더한 값의 하위 8비트 (0x11+0x22+0x33=0x66)
FSM 동작 과정
S_HEADER 상태:

0xAA가 들어오면 S_LENGTH로 전이
S_LENGTH 상태:

0x03이 들어오면, payload 길이를 3으로 설정, S_PAYLOAD로 전이
S_PAYLOAD 상태:

3바이트(0x11, 0x22, 0x33)를 순서대로 payload[]에 저장
동시에 checksum에 누적 (checksum += byte)
3바이트를 모두 받으면 S_CHECKSUM으로 전이
S_CHECKSUM 상태:

들어온 바이트(0x66)가 누적된 checksum(0x66)과 같으면 패킷 완성, true 반환
왜 payload에 0x11, 0x22, 0x33만 들어가는가?
Length 바이트(0x03)가 "payload의 개수"를 지정합니다.
FSM은 S_PAYLOAD 상태에서 딱 3바이트만 payload[]에 저장합니다.
그 다음 바이트(0x66)는 payload가 아니라 checksum이므로 payload에 저장하지 않습니다.
즉,

payload에는 Length에서 지정한 개수만큼의 바이트만 들어가고,
그 뒤의 바이트(Checksum)는 payload에 포함되지 않습니다.
정리
0xAA(헤더) → 0x03(길이) → 0x11,0x22,0x33(payload) → 0x66(체크섬)
payload에는 0x11, 0x22, 0x33만 들어가고, 0x66은 payload에 들어가지 않음
추가 개념
이런 구조는 실제 통신 프로토콜(예: CAN, UART, SPI 등)에서 매우 흔하게 사용됩니다.
FSM을 쓰면 데이터 손실, 오동작, 노이즈에 강한 robust한 파서를 만들 수 있습니다.
*/
typedef struct { int state, len, idx; uint8_t payload[32], checksum; } proto_fsm_t;
enum { S_HEADER, S_LENGTH, S_PAYLOAD, S_CHECKSUM };
void proto_fsm_init(proto_fsm_t *fsm) {
    fsm->state = S_HEADER; fsm->len = fsm->idx = fsm->checksum = 0;
}
bool proto_fsm_parse(proto_fsm_t *fsm, uint8_t byte) {
    switch (fsm->state) {
        case S_HEADER:
            if (byte == 0xAA) { fsm->state = S_LENGTH; }
            break;
        case S_LENGTH:
            fsm->len = byte;
            fsm->idx = 0;
            fsm->checksum = 0;
            if (fsm->len > 0 && fsm->len <= 32) fsm->state = S_PAYLOAD;
            else fsm->state = S_HEADER;
            break;
        case S_PAYLOAD:
            fsm->payload[fsm->idx++] = byte;
            fsm->checksum += byte;
            if (fsm->idx >= fsm->len) fsm->state = S_CHECKSUM;
            break;
        case S_CHECKSUM:
            if ((fsm->checksum & 0xFF) == byte) {
                fsm->state = S_HEADER;
                return true; // 패킷 완성
            } else {
                fsm->state = S_HEADER;
            }
            break;
    }
    return false;
}




// 28. 고정 소수점 곱셈/나눗셈
/*
Q15.16 고정 소수점에서 곱셈은 (a*b)>>16, 나눗셈은 ((a<<16)/b)로 구현합니다.
실수 연산이 느린 MCU에서 빠르고 정밀하게 소수를 다루기 위해 사용합니다.
예시 입력: a=0x00010000(1.0), b=0x00008000(0.5)
예시 출력: q15_16_mul(a,b)=0x00008000(0.5), q15_16_div(a,b)=0x00020000(2.0)
*/
int32_t q15_16_mul(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * b) >> 16);
}
int32_t q15_16_div(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a << 16) / b);
}

// 29. FIR 필터 구현
/*
FIR 필터는 입력 신호와 계수의 곱을 모두 더해 출력합니다.
노이즈 제거, 신호 평활화 등에 사용됩니다.
예시 입력: coeff={0.2,0.2,0.2,0.2,0.2}, input={1,2,3,4,5}
예시 출력: 3.0
*/
float fir_filter(const float *coeff, int n, const float *input) {
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) sum += coeff[i] * input[i];
    return sum;
}

// 30. PID 제어기 구현
/*
PID는 오차에 비례(P), 적분(I), 미분(D) 항을 더해 제어 출력을 만듭니다.
Integral windup(적분항 과도 누적)을 i_max로 제한해 방지합니다.
예시 입력: setpoint=10, measured=8, kp=2, ki=0.5, kd=1, i_max=10
예시 출력: (출력값, 예: 2*2 + 0.5*2 + 1*2 = 6)
*/
typedef struct { float kp, ki, kd, prev_err, integral, i_max; } pid_t;
void pid_init(pid_t *pid, float kp, float ki, float kd, float i_max) {
    pid->kp = kp; pid->ki = ki; pid->kd = kd; pid->prev_err = 0; pid->integral = 0; pid->i_max = i_max;
}
float pid_update(pid_t *pid, float setpoint, float measured) {
    float err = setpoint - measured;
    pid->integral += err;
    if (pid->integral > pid->i_max) pid->integral = pid->i_max;
    if (pid->integral < -pid->i_max) pid->integral = -pid->i_max;
    float deriv = err - pid->prev_err;
    pid->prev_err = err;
    return pid->kp * err + pid->ki * pid->integral + pid->kd * deriv;
}

// 31. ISR과 메인 루프 간 공유 자원 보호
/*
공유 변수는 ISR에서 변경, 메인에서 읽을 때 인터럽트 비활성화로 경쟁 조건을 방지합니다.
임베디드에서 매우 중요한 패턴입니다.
예시: ISR에서 isr_increment() 여러 번 호출, main에서 main_read_counter()로 안전하게 읽기
예시 출력: ISR에서 5번 증가 후 main에서 읽으면 5
*/
volatile int shared_counter = 0;
void isr_increment(void) { shared_counter++; }
int main_read_counter(void) {
    int val;
    __disable_irq(); // 가상 함수, 실제 MCU에 맞게 구현 필요
    val = shared_counter;
    __enable_irq();
    return val;
}

// 32. Mutex를 사용한 임계 구역 보호
/*
RTOS 환경에서 여러 태스크가 공유 자원에 접근할 때 Mutex로 임계 구역을 보호합니다.
데이터 손상(race condition) 방지에 필수입니다.
예시: 두 태스크가 동시에 shared_buf[0]에 접근해도 값이 꼬이지 않음
*/
pthread_mutex_t buf_mutex = PTHREAD_MUTEX_INITIALIZER;
int shared_buf[8];
void critical_section_example(void *arg) {
    pthread_mutex_lock(&buf_mutex);
    // 공유 버퍼 접근
    shared_buf[0] = 42;
    pthread_mutex_unlock(&buf_mutex);
}

// 33. 타이머 기반 버튼 디바운싱
/*
딜레이 없이 타이머 인터럽트마다 raw_input을 읽고, 일정 횟수 연속 동일하면 상태를 확정합니다.
실시간성 보장, CPU 낭비 방지에 효과적입니다.
예시 입력: raw_input이 5번 연속 true면 debounced=true
예시 출력: get_debounced_state() -> true
*/
static int debounce_cnt = 0;
static bool debounced = false;
void debounce_timer_tick(bool raw_input) {
    static bool last = false;
    if (raw_input == last) {
        if (debounce_cnt < 5) debounce_cnt++;
    } else {
        debounce_cnt = 0;
    }
    if (debounce_cnt >= 5) debounced = raw_input;
    last = raw_input;
}
bool get_debounced_state(void) { return debounced; }

// 34. CAN 메시지 패킹 함수
/*
여러 신호를 비트 연산으로 64비트 데이터에 패킹합니다.
CAN 통신에서 데이터 효율적 전송을 위해 자주 사용됩니다.
예시 입력: status=0xA, sensor=0x123
예시 출력: 0x00000000000A0123
*/
uint64_t can_pack(uint8_t status, uint16_t sensor) {
    return ((uint64_t)status << 16) | sensor;
}

// 35. CAN 메시지 언패킹 함수
/*
비트 연산으로 64비트 데이터에서 원하는 신호를 추출합니다.
예시 입력: msg=0x00000000000A0123
예시 출력: status=0xA, sensor=0x123
*/
void can_unpack(uint64_t msg, uint8_t *status, uint16_t *sensor) {
    *sensor = msg & 0xFFFF;
    *status = (msg >> 16) & 0xFF;
}

// 36. 메모리 풀 할당자 구현
/*
고정 크기 블록의 메모리 풀에서 할당/해제. free_list로 미사용 블록 관리.
동적 할당이 제한된 임베디드 환경에서 메모리 관리에 유용합니다.
예시 입력: mpool_alloc() 여러 번 호출, mpool_free()로 해제
예시 출력: 최대 8개까지 할당, 그 이상은 NULL 반환
*/
#define MPOOL_SIZE 8
#define MPOOL_BLK_SIZE 32
static uint8_t mpool[MPOOL_SIZE][MPOOL_BLK_SIZE];
static bool mpool_used[MPOOL_SIZE] = {0};
void* mpool_alloc(void) {
    for (int i = 0; i < MPOOL_SIZE; ++i) {
        if (!mpool_used[i]) {
            mpool_used[i] = true;
            return mpool[i];
        }
    }
    return NULL;
}
void mpool_free(void *ptr) {
    for (int i = 0; i < MPOOL_SIZE; ++i) {
        if (mpool[i] == ptr) {
            mpool_used[i] = false;
            return;
        }
    }
}

// 37. 소프트웨어 워치독 구현
/*
여러 태스크가 주기적으로 pat을 보내지 않으면 시스템 리셋.
각 태스크별 타임아웃 카운터를 관리합니다.
예시 입력: sw_watchdog_pat(0), sw_watchdog_tick() 11번 호출
예시 출력: "RESET: Task 0 missed watchdog"
*/
#define WD_TASKS 4
static int wd_counter[WD_TASKS] = {0};
void sw_watchdog_pat(int task_id) { wd_counter[task_id] = 0; }
void sw_watchdog_tick(void) {
    for (int i = 0; i < WD_TASKS; ++i) {
        wd_counter[i]++;
        if (wd_counter[i] > 10) {
            printf("RESET: Task %d missed watchdog\n", i);
            wd_counter[i] = 0;
        }
    }
}

// 38. UDS ReadDataByIdentifier (0x22) 서비스 핸들러
/*
DID 테이블에서 데이터 조회, 응답 생성. 실제 차량에서는 테이블/메모리에서 데이터 읽음.
예시 입력: did=0xF190
예시 출력: out={VIN 데이터}, out_len=17, return 0(성공)
*/
typedef struct { uint16_t did; uint8_t *data; int len; } did_entry_t;
static uint8_t vin[17] = "1ABCDEFGH12345678";
static did_entry_t did_table[] = {
    {0xF190, vin, 17},
    // ... 추가 DID
};
int uds_read_did(uint16_t did, uint8_t *out, int *out_len) {
    for (int i = 0; i < sizeof(did_table)/sizeof(did_table[0]); ++i) {
        if (did_table[i].did == did) {
            memcpy(out, did_table[i].data, did_table[i].len);
            *out_len = did_table[i].len;
            return 0;
        }
    }
    return -1;
}

// 39. 비트 필드를 이용한 상태 플래그 관리
/*
비트 필드로 여러 플래그를 한 바이트에 효율적으로 저장합니다.
예시 입력: flags.error = 1; flags.ready = 0; flags.busy = 1;
예시 출력: flags의 메모리 사용은 1바이트
*/
typedef struct {
    uint8_t error:1;
    uint8_t ready:1;
    uint8_t busy:1;
    uint8_t reserved:5;
} status_flags_t;

// 40. 1차 IIR 필터 구현
/*
고정 소수점(Q15)으로 1차 저역통과 IIR 필터 구현.
y[n] = a*x[n] + (1-a)*y[n-1], a_q15는 0~32767(0~1.0)
예시 입력: x=1000, prev_y=900, a_q15=16384(0.5)
예시 출력: 950
*/
int32_t iir_filter(int32_t x, int32_t prev_y, int32_t a_q15) {
    return ((a_q15 * x + (32767 - a_q15) * prev_y) >> 15);
}

// 41. 우선순위 역전 현상 설명 및 해결책
/*
낮은 우선순위 태스크가 Mutex로 자원을 점유한 상태에서 높은 우선순위 태스크가 대기하면,
중간 우선순위 태스크가 계속 실행되어 높은 우선순위 태스크가 무한 대기하는 현상.
해결: Mutex에 Priority Inheritance 옵션을 사용하면, 낮은 우선순위 태스크가 자원을 점유 중일 때
더 높은 우선순위 태스크가 대기하면, 낮은 우선순위 태스크의 우선순위를 일시적으로 올려줌.
예시: TaskL(낮음) -> Mutex lock -> TaskH(높음) 대기 -> TaskM(중간) 계속 실행 -> 우선순위 역전 발생
      Priority Inheritance 사용 시 TaskL의 우선순위가 TaskH로 상승하여 빠르게 Mutex 해제
*/

// 42. 룩업 테이블과 선형 보간
/*
adc_table, phy_table에서 adc_val이 위치한 구간을 찾아 선형 보간.
센서 캘리브레이션, 비선형 변환에 필수.
예시 입력: adc_table={0,1000,2000}, phy_table={0.0,50.0,100.0}, adc_val=1500
예시 출력: 75.0
*/
float lut_linear_interp(const int *adc_table, const float *phy_table, int n, int adc_val) {
    for (int i = 0; i < n-1; ++i) {
        if (adc_val >= adc_table[i] && adc_val <= adc_table[i+1]) {
            float ratio = (adc_val - adc_table[i]) / (float)(adc_table[i+1] - adc_table[i]);
            return phy_table[i] + ratio * (phy_table[i+1] - phy_table[i]);
        }
    }
    return phy_table[n-1];
}

// 43. 데드 타임을 고려한 PWM 생성
/*
의사 코드:
if (pwm_high) {
    set_high();
    wait(dead_time);
    set_low();
    set_complementary_high();
    wait(pulse_width - dead_time);
    set_complementary_low();
}
예시 입력: dead_time=10us, pulse_width=100us
예시 출력: 상보 PWM 신호 사이에 10us 데드타임이 삽입됨
*/
// 실제 MCU에서는 하드웨어 dead-time 삽입 기능을 사용

// 44. heap_1.c 스타일의 간단한 힙 관리자
/*
정적 배열에서 할당만 지원, 해제 없음. 포인터를 증가시키며 메모리 할당.
예시 입력: simple_heap_alloc(32) 8번 호출
예시 출력: 8번째까지는 유효 포인터, 그 이후는 NULL
*/
#define HEAP_SIZE 256
static uint8_t heap[HEAP_SIZE];
static int heap_idx = 0;
void* simple_heap_alloc(int size) {
    if (heap_idx + size > HEAP_SIZE) return NULL;
    void *ptr = &heap[heap_idx];
    heap_idx += size;
    return ptr;
}

// 45. AUTOSAR E2E Profile 1의 개념
/*
E2E 보호는 통신 데이터의 무결성, 순서, 신뢰성을 보장하기 위해 필요.
Profile 1은 CRC(오류 검출)와 카운터(순서 검증)를 사용.
예시 입력: 송신 시 CRC와 카운터 추가, 수신 시 CRC/카운터 확인
예시 출력: CRC 오류나 카운터 불일치 시 오류 처리
*/

// 46. 세마포어를 이용한 생산자-소비자 문제
/*
카운팅 세마포어로 버퍼의 사용 가능 공간/데이터 개수를 관리.
생산자는 공간 세마포어를, 소비자는 데이터 세마포어를 사용.
예시 입력: 생산자 3번 post, 소비자 3번 wait
예시 출력: 소비자는 3번 데이터를 안전하게 소비
*/
// 예시 (RTOS API 가정):
// sem_t space_sem, data_sem;
// producer: wait(space_sem); ...; post(data_sem);
// consumer: wait(data_sem); ...; post(space_sem);

// 47. 메시지 큐를 이용한 데이터 전달
/*
RTOS 메시지 큐로 센서 태스크가 데이터를 로깅 태스크에 전달.
예시 입력: sensor_task에서 100, 200, 300 전송
예시 출력: logger_task에서 100, 200, 300 수신
*/
// 예시 (RTOS API 가정):
// QueueHandle_t q = xQueueCreate(...);
// sensor_task: xQueueSend(q, &data, ...);
// logger_task: xQueueReceive(q, &data, ...);

// 48. 함수 포인터를 이용한 콜백 구현
/*
콜백 함수 포인터를 등록하고, 버튼 이벤트 발생 시 호출.
예시 입력: register_button_callback(my_cb); button_event();
예시 출력: my_cb() 함수가 호출됨
*/
typedef void (*button_callback_t)(void);
static button_callback_t btn_cb = NULL;
void register_button_callback(button_callback_t cb) { btn_cb = cb; }
void button_event(void) { if (btn_cb) btn_cb(); }

// 49. 타이머 입력 캡처 모드
/*
의사 코드:
- 타이머를 입력 캡처 모드로 설정
- 상승/하강 에지에서 타이머 값 캡처
- 두 에지 간 차이로 주기/듀티 계산
예시 입력: 상승 에지 1000, 하강 에지 1500
예시 출력: 주기=500, 듀티=(하강-상승)/주기
*/

// 50. static 키워드의 두 가지 의미
/*
1) 함수 내 static 변수: 함수 호출 간 값 유지(정적 저장 기간)
2) 파일 스코프 static 함수/변수: 해당 파일 내에서만 접근 가능(링크 제한)
예시 입력: static int cnt=0; 함수 여러 번 호출
예시 출력: cnt 값이 함수 호출 간 유지됨
*/

// 51. 재귀 함수의 위험성
/*
임베디드에서 재귀 함수는 스택 오버플로우 위험이 크므로 주의.
반복문으로 대체 권장.
예시 입력: 재귀로 1000번 호출
예시 출력: 스택 오버플로우 발생 가능
*/

// 52. UDS WriteDataByIdentifier (0x2E) 서비스 핸들러
/*
DID에 해당하는 데이터를 EEPROM 등 비휘발성 메모리에 저장.
예시 입력: did=0xF190, data={...}, len=17
예시 출력: return 0(성공), -1(실패)
*/
int uds_write_did(uint16_t did, const uint8_t *data, int len) {
    // 예시: did=0xF190이면 VIN 저장
    if (did == 0xF190 && len == 17) {
        memcpy(vin, data, 17);
        return 0;
    }
    return -1;
}

// 53. DMA를 이용한 UART 데이터 송신
/*
의사 코드:
- DMA 채널에 송신 버퍼 주소, 길이 설정
- DMA 시작
- DMA 완료 인터럽트에서 후처리
예시 입력: buf={0x01,0x02,0x03}, len=3
예시 출력: UART로 0x01,0x02,0x03이 전송됨
*/

// 54. 저전력 모드 진입/탈출
/*
의사 코드:
- 저전력 진입: set_sleep_mode(); sleep_cpu();
- 외부 인터럽트 발생 시 wake-up
예시 입력: sleep 진입, 버튼 인터럽트 발생
예시 출력: sleep에서 깨어남
*/

// 55. CRC-16 (Modbus) 테이블 기반 계산
/*
미리 계산된 CRC 테이블을 사용해 빠르게 CRC-16(Modbus) 체크섬 계산.
예시 입력: buf={0x01,0x02,0x03}, len=3
예시 출력: 0x4B37 (예시값)
*/
static const uint16_t crc16_table[256] = {
    // ... (생략, 표준 Modbus CRC 테이블)
};
uint16_t crc16_modbus(const uint8_t *buf, int len) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; ++i) {
        crc = (crc >> 8) ^ crc16_table[(crc ^ buf[i]) & 0xFF];
    }
    return crc;
}

// 56. 링크드 리스트를 이용한 동적 데이터 관리
/*
단일 링크드 리스트로 이벤트 로그 관리. 동적 할당 사용.
예시 입력: add 1,2,3, remove 2
예시 출력: 리스트에 1,3만 남음
*/
typedef struct node { int data; struct node *next; } node_t;
node_t* list_add(node_t *head, int v) {
    node_t *n = malloc(sizeof(node_t));
    n->data = v; n->next = head;
    return n;
}
node_t* list_remove(node_t *head, int v) {
    node_t *cur = head, *prev = NULL;
    while (cur) {
        if (cur->data == v) {
            if (prev) prev->next = cur->next;
            else head = cur->next;
            free(cur);
            break;
        }
        prev = cur; cur = cur->next;
    }
    return head;
}

// 57. 해시 테이블을 이용한 빠른 데이터 조회
/*
문자열 키와 정수 값 저장/조회 해시 테이블(오픈 어드레싱, 단순 구현).
예시 입력: set("speed",100), get("speed")
예시 출력: 100
*/
#define HT_SIZE 16
typedef struct { char key[16]; int value; bool used; } ht_entry_t;
static ht_entry_t ht[HT_SIZE];
unsigned ht_hash(const char *key) {
    unsigned h = 5381;
    while (*key) h = ((h << 5) + h) + (unsigned char)(*key++);
    return h % HT_SIZE;
}
void hashtable_set(const char *key, int value) {
    unsigned h = ht_hash(key);
    for (int i = 0; i < HT_SIZE; ++i) {
        unsigned idx = (h + i) % HT_SIZE;
        if (!ht[idx].used || strcmp(ht[idx].key, key) == 0) {
            strncpy(ht[idx].key, key, 15); ht[idx].key[15]=0;
            ht[idx].value = value; ht[idx].used = true;
            return;
        }
    }
}
bool hashtable_get(const char *key, int *value) {
    unsigned h = ht_hash(key);
    for (int i = 0; i < HT_SIZE; ++i) {
        unsigned idx = (h + i) % HT_SIZE;
        if (ht[idx].used && strcmp(ht[idx].key, key) == 0) {
            *value = ht[idx].value;
            return true;
        }
    }
    return false;
}

// 58. #pragma pack의 사용
/*
구조체 패딩을 제어해 메모리 절약/호환성을 높임.
예시 입력: packed_t { char a; int b; }
예시 출력: sizeof(packed_t)=5 (패딩 없음)
예시:
#pragma pack(push,1)
typedef struct { char a; int b; } packed_t;
#pragma pack(pop)
*/

// 59. union을 이용한 데이터 타입 변환
/*
union을 사용하면 같은 메모리 공간을 여러 타입으로 해석 가능.
엔디안에 따라 바이트 순서가 달라질 수 있으니 주의.
예시 입력: u32_bytes_t u; u.w=0x12345678;
예시 출력: u.b[0]=0x78 (리틀엔디안)
*/
typedef union { uint8_t b[4]; uint32_t w; } u32_bytes_t;

// 60. 간단한 이벤트 플래그 구현
/*
여러 이벤트 비트를 OR 연산으로 설정, 특정 조합이 만족될 때까지 대기.
RTOS에서는 event group API를 사용.
예시 입력: event_set(&eg, 0x01); event_set(&eg, 0x02); event_wait(&eg, 0x03);
예시 출력: 0x01, 0x02 모두 set될 때까지 대기, 이후 진행
*/
typedef struct { uint32_t flags; } event_group_t;
void event_set(event_group_t *eg, uint32_t mask) { eg->flags |= mask; }
void event_wait(event_group_t *eg, uint32_t mask) {
    while ((eg->flags & mask) != mask) {
        // RTOS에서는 태스크를 블록, baremetal에서는 busy wait
    }
}