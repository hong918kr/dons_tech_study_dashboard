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
설명: 지정된 크기의 원형 버퍼에 데이터를 추가(enqueue)하고 제거(dequeue)하는 함수 구현.
배경: 원형 버퍼는 FIFO 큐의 일종으로, 메모리 효율이 높고 임베디드에서 자주 사용됨.
*/
typedef struct { int buf[8]; int head, tail, count; } circbuf_t;
void circbuf_init(circbuf_t *cb);
bool circbuf_enqueue(circbuf_t *cb, int v);
bool circbuf_dequeue(circbuf_t *cb, int *v);
/*
예시 입력: enqueue 1,2,3, dequeue
예시 출력: 1
*/

// 27. FSM을 이용한 프로토콜 파서
/*
설명: [Header][Length][Payload][Checksum] 구조의 패킷을 파싱하는 FSM 구현.
배경: 통신 프로토콜 파싱에서 상태 머신은 견고한 처리를 위해 필수적.
*/
typedef struct { int state, len, idx; uint8_t payload[32], checksum; } proto_fsm_t;
void proto_fsm_init(proto_fsm_t *fsm);
bool proto_fsm_parse(proto_fsm_t *fsm, uint8_t byte);
/*
예시 입력: 0xAA, 0x03, 0x11, 0x22, 0x33, 0x66
예시 출력: payload={0x11,0x22,0x33}, return true
*/

// 28. 고정 소수점 곱셈/나눗셈
/*
설명: Q15.16 형식의 두 고정 소수점 숫자를 곱하고 나누는 함수 구현.
배경: 실수 연산이 느린 MCU에서 고정 소수점 연산은 빠르고 효율적.
*/
int32_t q15_16_mul(int32_t a, int32_t b);
int32_t q15_16_div(int32_t a, int32_t b);
/*
예시 입력: a=0x00010000(1.0), b=0x00008000(0.5)
예시 출력: q15_16_mul(a,b)=0x00008000(0.5)
*/

// 29. FIR 필터 구현
/*
설명: 주어진 계수 배열로 FIR 필터를 구현.
배경: FIR 필터는 신호처리에서 잡음 제거, 평활화 등에 사용.
*/
float fir_filter(const float *coeff, int n, const float *input);
/*
예시 입력: coeff={0.2,0.2,0.2,0.2,0.2}, input={1,2,3,4,5}
예시 출력: 3.0
*/

// 30. PID 제어기 구현
/*
설명: P, I, D 게인, 목표값, 현재값을 입력받아 제어 출력 계산. Integral windup 방지 포함.
배경: PID는 온도, 속도 등 다양한 제어 시스템에서 표준적으로 사용.
*/
typedef struct { float kp, ki, kd, prev_err, integral, i_max; } pid_t;
void pid_init(pid_t *pid, float kp, float ki, float kd, float i_max);
float pid_update(pid_t *pid, float setpoint, float measured);
/*
예시 입력: setpoint=10, measured=8
예시 출력: (출력값)
*/

// 31. ISR과 메인 루프 간 공유 자원 보호
/*
설명: ISR과 메인 루프에서 공유 변수 접근 시 인터럽트 비활성화/활성화로 보호.
배경: 경쟁 조건(race condition) 방지, 임베디드 실전에서 매우 중요.
*/
volatile int shared_counter;
void isr_increment(void); // ISR에서 호출
int main_read_counter(void); // 메인 루프에서 안전하게 읽기
/*
예시: ISR에서 증가, 메인에서 읽기
*/

// 32. Mutex를 사용한 임계 구역 보호
/*
설명: 두 태스크가 공유 버퍼에 접근할 때 Mutex로 임계 구역 보호.
배경: RTOS 환경에서 데이터 손상 방지, 동기화의 기본.
*/
void critical_section_example(void *arg); // RTOS 태스크 함수
/*
예시: 두 태스크가 동시에 버퍼 접근 시 데이터 손상 없음
*/

// 33. 타이머 기반 버튼 디바운싱
/*
설명: 딜레이 없이 타이머 인터럽트와 카운터로 버튼 디바운싱.
배경: 실시간성 보장, CPU 낭비 방지.
*/
void debounce_timer_tick(bool raw_input); // 타이머 인터럽트에서 호출
bool get_debounced_state(void);
/*
예시 입력: 버튼 튐 신호
예시 출력: 안정된 true/false
*/

// 34. CAN 메시지 패킹 함수
/*
설명: 여러 신호를 64비트 CAN 데이터에 패킹.
배경: CAN 통신에서 데이터 효율적 전송을 위해 비트 필드 활용.
*/
uint64_t can_pack(uint8_t status, uint16_t sensor);
/*
예시 입력: status=0xA, sensor=0x123
예시 출력: 0x00000000000A0123
*/

// 35. CAN 메시지 언패킹 함수
/*
설명: 64비트 CAN 데이터에서 신호 추출.
배경: 수신 데이터에서 원하는 신호만 추출하는 실전 기술.
*/
void can_unpack(uint64_t msg, uint8_t *status, uint16_t *sensor);
/*
예시 입력: msg=0x00000000000A0123
예시 출력: status=0xA, sensor=0x123
*/

// 36. 메모리 풀 할당자 구현
/*
설명: 고정 크기 블록의 메모리 풀에서 alloc/free 구현.
배경: 동적 할당이 제한된 임베디드 환경에서 메모리 관리.
*/
void* mpool_alloc(void);
void mpool_free(void *ptr);
/*
예시: 여러 번 alloc/free, 중복 할당 방지
*/

// 37. 소프트웨어 워치독 구현
/*
설명: 여러 태스크가 주기적으로 신호를 보내지 않으면 시스템 리셋.
배경: 시스템 전체의 정상 동작 감시, 신뢰성 향상.
*/
void sw_watchdog_pat(int task_id);
void sw_watchdog_tick(void);
/*
예시: 일부 태스크가 pat 안 하면 "RESET"
*/

// 38. UDS ReadDataByIdentifier (0x22) 서비스 핸들러
/*
설명: UDS 0x22 요청 수신 시 DID 테이블에서 데이터 조회, 응답 생성.
배경: 자동차 진단 표준, 서비스 핸들러 설계.
*/
int uds_read_did(uint16_t did, uint8_t *out, int *out_len);
/*
예시 입력: did=0xF190
예시 출력: out={VIN 데이터}, out_len=17
*/

// 39. 비트 필드를 이용한 상태 플래그 관리
/*
설명: 구조체 내 비트 필드로 여러 상태 플래그 관리.
배경: 메모리 절약, 플래그 관리의 효율성.
*/
typedef struct {
    uint8_t error:1;
    uint8_t ready:1;
    uint8_t busy:1;
    uint8_t reserved:5;
} status_flags_t;
/*
예시: flags.error = 1;
*/

// 40. 1차 IIR 필터 구현
/*
설명: y[n] = a*x[n] + (1-a)*y[n-1] 공식의 1차 저역통과 IIR 필터, 고정 소수점 사용.
배경: 신호 평활화, 실시간 필터링.
*/
int32_t iir_filter(int32_t x, int32_t prev_y, int32_t a_q15);
/*
예시 입력: x=1000, prev_y=900, a_q15=16384(0.5)
예시 출력: 950
*/

// 41. 우선순위 역전 현상 설명 및 해결책
/*
설명: 우선순위 역전이란, 낮은 우선순위 태스크가 공유 자원을 점유해 높은 우선순위 태스크가 대기하는 현상.
해결: 우선순위 상속(Priority Inheritance) 등. Mutex 사용 예시 포함.
*/
/*
예시: 코드/주석 설명
*/

// 42. 룩업 테이블과 선형 보간
/*
설명: 비선형 센서 ADC 값을 실제 물리량으로 변환, 선형 보간 포함.
배경: 센서 캘리브레이션, 정확도 향상.
*/
float lut_linear_interp(const int *adc_table, const float *phy_table, int n, int adc_val);
/*
예시 입력: adc_table={0,1000,2000}, phy_table={0.0,50.0,100.0}, adc_val=1500
예시 출력: 75.0
*/

// 43. 데드 타임을 고려한 PWM 생성
/*
설명: 상보 PWM 신호 사이에 데드 타임 삽입 로직(의사 코드).
배경: H-브리지에서 쇼트 방지, 하드웨어 보호.
*/
/*
예시: pseudocode 주석
*/

// 44. heap_1.c 스타일의 간단한 힙 관리자
/*
설명: 해제 없는 단순 할당 힙 관리자, 정적 배열 사용.
배경: FreeRTOS heap_1.c와 유사, 임베디드에서 메모리 관리.
*/
void* simple_heap_alloc(int size);
/*
예시: 여러 번 alloc, 해제 없음
*/

// 45. AUTOSAR E2E Profile 1의 개념
/*
설명: E2E 보호의 필요성, Profile 1의 CRC/카운터 개념.
배경: 자동차 통신 신뢰성, 데이터 무결성.
*/
/*
예시: 설명/주석
*/

// 46. 세마포어를 이용한 생산자-소비자 문제
/*
설명: 카운팅 세마포어로 생산자/소비자 동기화.
배경: RTOS 동기화, 데이터 손실/중복 방지.
*/
void producer_task(void *arg);
void consumer_task(void *arg);
/*
예시: RTOS 태스크, 세마포어 사용
*/

// 47. 메시지 큐를 이용한 데이터 전달
/*
설명: 센서 태스크가 메시지 큐로 데이터를 로깅 태스크에 전달.
배경: 태스크 간 통신, RTOS 메시지 큐.
*/
void sensor_task(void *arg);
void logger_task(void *arg);
/*
예시: RTOS 메시지 큐 사용
*/

// 48. 함수 포인터를 이용한 콜백 구현
/*
설명: 버튼 이벤트 발생 시 등록된 콜백 호출.
배경: 이벤트 기반 프로그래밍, 유연한 구조.
*/
typedef void (*button_callback_t)(void);
void register_button_callback(button_callback_t cb);
void button_event(void);
/*
예시: 콜백 등록 후 버튼 이벤트 발생 시 함수 호출
*/

// 49. 타이머 입력 캡처 모드
/*
설명: 외부 신호의 주파수/듀티 측정 위한 타이머 입력 캡처 설정(의사 코드).
배경: 펄스 측정, 하드웨어 타이머 활용.
*/
/*
예시: pseudocode 주석
*/

// 50. static 키워드의 두 가지 의미
/*
설명: static이 변수/함수에 사용될 때의 의미와 예시.
배경: 파일 스코프, 정적 저장 기간.
*/
/*
예시: 코드/주석
*/

// 51. 재귀 함수의 위험성
/*
설명: 임베디드에서 재귀 함수 사용 시 스택 오버플로우 위험.
배경: 제한된 스택, 함수 호출 깊이.
*/
/*
예시: 설명/주석
*/

// 52. UDS WriteDataByIdentifier (0x2E) 서비스 핸들러
/*
설명: UDS 0x2E 요청 처리, DID에 값 저장(EEPROM 등).
배경: 자동차 진단 표준, 비휘발성 메모리 연동.
*/
int uds_write_did(uint16_t did, const uint8_t *data, int len);
/*
예시 입력: did=0xF190, data={...}, len=17
예시 출력: 0(성공), -1(실패)
*/

// 53. DMA를 이용한 UART 데이터 송신
/*
설명: DMA로 UART 송신, CPU 개입 최소화(의사 코드).
배경: 고속 데이터 송신, CPU 부하 감소.
*/
/*
예시: pseudocode 주석
*/

// 54. 저전력 모드 진입/탈출
/*
설명: MCU 저전력 모드 진입, 외부 인터럽트로 wake-up(의사 코드).
배경: 배터리 절약, 실시간성 유지.
*/
/*
예시: pseudocode 주석
*/

// 55. CRC-16 (Modbus) 테이블 기반 계산
/*
설명: CRC 테이블로 16비트 CRC(Modbus) 체크섬 계산.
배경: 데이터 무결성 검증, 통신 신뢰성.
*/
uint16_t crc16_modbus(const uint8_t *buf, int len);
/*
예시 입력: buf={0x01,0x02,0x03}, len=3
예시 출력: 0x???? (계산값)
*/

// 56. 링크드 리스트를 이용한 동적 데이터 관리
/*
설명: 단일 링크드 리스트로 이벤트 로그 관리, 동적 할당 사용.
배경: 크기 변동 데이터 관리, 메모리 효율.
*/
typedef struct node { int data; struct node *next; } node_t;
node_t* list_add(node_t *head, int v);
node_t* list_remove(node_t *head, int v);
/*
예시: add 1,2,3, remove 2
*/

// 57. 해시 테이블을 이용한 빠른 데이터 조회
/*
설명: 문자열 키와 정수 값 저장/조회 해시 테이블 구현.
배경: 빠른 검색, 파라미터 관리.
*/
void hashtable_set(const char *key, int value);
bool hashtable_get(const char *key, int *value);
/*
예시: set("speed",100), get("speed") -> 100
*/

// 58. #pragma pack의 사용
/*
설명: 구조체 패딩 제어, 메모리 절약/호환성.
배경: 하드웨어 레지스터 매핑, 통신 프로토콜.
*/
/*
예시: 코드/주석
*/

// 59. union을 이용한 데이터 타입 변환
/*
설명: union으로 uint8_t[4] <-> uint32_t 변환, 안전성 논의.
배경: 바이트 레벨 데이터 처리, 엔디안 이슈.
*/
typedef union { uint8_t b[4]; uint32_t w; } u32_bytes_t;
/*
예시: u.w=0x12345678; u.b[0]=?
*/

// 60. 간단한 이벤트 플래그 구현
/*
설명: 여러 이벤트 비트 설정, 특정 조합 만족 시까지 대기.
배경: RTOS 동기화, 태스크 간 신호.
*/
typedef struct { uint32_t flags; } event_group_t;
void event_set(event_group_t *eg, uint32_t mask);
void event_wait(event_group_t *eg, uint32_t mask);
/*
예시: set 0x01, wait 0x03 (0x01,0x02 모두 set될 때까지 대기)
*/