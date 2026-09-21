/*
1. 특정 비트 설정(Set): 주어진 8비트 변수에서 3번째 비트를 1로 설정하는 함수를 작성하세요.
2. 특정 비트 해제(Clear): 주어진 16비트 변수에서 5번째 비트를 0으로 해제하는 함수를 작성하세요.
3. 특정 비트 토글(Toggle): 주어진 8비트 변수에서 n번째 비트를 토글하는 함수를 작성하세요.
4. 특정 비트 확인: 주어진 정수의 n번째 비트가 1인지 0인지 확인하는 함수를 작성하세요.
5. XOR 체크섬 계산: 주어진 데이터 배열(uint8_t buffer, int length)에 대한 8비트 XOR 체크섬을 계산하는 함수를 구현하세요.
6. 간단한 UART 패킷 파서 FSM: `` 형식의 간단한 UART 패킷을 파싱하는 상태 머신(State Machine)을 구현하세요. (STX: 0x02, ETX: 0x03)
7. 이동 평균 필터(Moving Average Filter): 가장 최근 5개의 데이터 샘플에 대한 이동 평균을 계산하는 함수를 구현하세요. 정적 배열(static array)을 사용하여 데이터 포인트를 저장하세요.
8. 워치독 타이머 "Pat" 시뮬레이션: void wdt_pat(void) 함수가 주기적으로 호출되지 않으면 "SYSTEM RESET"을 출력하는 간단한 시뮬레이션 코드를 작성하세요.
9. 엔디안(Endianness) 변환: 32비트 정수의 엔디안을 변환하는(Little-endian ↔ Big-endian) 함수를 작성하세요.
10. 고정 소수점(Fixed-Point) 덧셈/뺄셈: Q15.16 형식의 두 고정 소수점 숫자를 더하고 빼는 매크로를 작성하세요.
11. ADC 값 전압 변환: 12비트 ADC(0-4095)에서 읽은 값을 0-3.3V 범위의 전압 값으로 변환하는 함수를 작성하세요.
12. PWM 듀티 사이클 계산: 타이머의 주기 레지스터(ARR) 값이 999일 때, 듀티 사이클을 75%로 설정하기 위한 비교 레지스터(CCR) 값을 계산하는 함수를 작성하세요.
13. 딜레이 기반 버튼 디바운싱(Debouncing): 버튼 입력이 감지되면 20ms 딜레이 후 다시 상태를 확인하여 안정적인 버튼 눌림을 감지하는 간단한 디바운싱 함수를 구현하세요.
14. volatile 키워드의 목적: volatile 키워드가 왜 필요한지 설명하고, 최적화에 의해 코드가 오작동할 수 있는 예시와 volatile을 사용해 해결한 예시를 보여주세요.
15. 메모리 맵 레지스터 제어 매크로: 특정 메모리 주소(예: 0x40010C08)에 값을 쓰거나 읽는 매크로 함수(REG_WRITE, REG_READ)를 작성하세요.
16. 8비트 합계 체크섬(Sum Checksum) 계산: 데이터 버퍼의 모든 바이트를 더하여 8비트 체크섬을 계산하는 함수를 구현하세요. (오버플로우는 자연스럽게 처리)
17. 설정된 비트 수 카운트: 주어진 32비트 정수에서 1로 설정된 비트의 개수를 세는 함수를 작성하세요.
18. XOR를 이용한 변수 교환: 임시 변수를 사용하지 않고 XOR 비트 연산만을 이용하여 두 정수 변수의 값을 교환하는 함수를 작성하세요.
19. 배열을 이용한 간단한 LIFO(Stack) 구현: 정수 배열을 사용하여 push와 pop 기능을 가진 간단한 스택을 구현하세요.
20. 배열을 이용한 간단한 FIFO(Queue) 구현: 정수 배열과 인덱스 변수를 사용하여 enqueue와 dequeue 기능을 가진 간단한 큐를 구현하세요.
21. 16진수 문자열을 정수로 변환: "0x1A"와 같은 16진수 문자열을 입력받아 해당하는 정수(26)로 변환하는 함수를 작성하세요.
22. 정수를 2진수 문자열로 변환: 8비트 정수를 입력받아 "00011010"과 같은 2진수 문자열로 변환하는 함수를 작성하세요.
23. const 키워드의 다양한 사용법: C언어에서 const 키워드가 사용될 수 있는 4가지 다른 위치(예: const int*, int* const)를 설명하고 각각의 의미를 설명하세요.
24. 간단한 타이머 초기화: 시스템 클럭이 16MHz일 때, 1ms마다 타이머 인터럽트가 발생하도록 타이머의 분주비(Prescaler)와 주기(Period) 레지스터 값을 계산하세요.
25. LED 점멸 구현: GPIO 핀을 제어하여 500ms 주기로 LED를 켜고 끄는 무한 루프 코드를 작성하세요. (딜레이 함수 사용)/*

*/
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>



/*
각 문제는 임베디드/펌웨어 코딩 인터뷰에서 자주 나오는 기본 개념을 다룹니다.
함수 시그니처, 설명, 예시 입력/출력, 그리고 배경 개념을 함께 제공합니다.
*/

// 1. 특정 비트 설정(Set)
/*
설명: 주어진 8비트 변수에서 3번째 비트를 1로 설정합니다.
비트 연산의 기본 개념(OR, shift) 이해 필요.
*/
uint8_t set_bit3(uint8_t value);
/*
예시 입력: 0x00
예시 출력: 0x08
*/

// 2. 특정 비트 해제(Clear)
/*
설명: 주어진 16비트 변수에서 5번째 비트를 0으로 해제합니다.
비트 마스크와 AND, NOT 연산 이해 필요.
*/
uint16_t clear_bit5(uint16_t value);
/*
예시 입력: 0xFFFF
예시 출력: 0xFFDF
*/

// 3. 특정 비트 토글(Toggle)
/*
설명: 주어진 8비트 변수에서 n번째 비트를 토글합니다.
XOR 연산의 활용.
*/
uint8_t toggle_bit(uint8_t value, uint8_t n);
/*
예시 입력: value=0x10, n=4
예시 출력: 0x00
*/

// 4. 특정 비트 확인
/*
설명: 주어진 정수의 n번째 비트가 1인지 0인지 확인합니다.
비트 마스크와 shift 연산.
*/
bool is_bit_set(uint32_t value, uint8_t n);
/*
예시 입력: value=0x10, n=4
예시 출력: true
*/

// 5. XOR 체크섬 계산
/*
설명: 데이터 배열의 모든 바이트에 대해 XOR 연산을 누적하여 체크섬을 계산합니다.
XOR의 성질(순서 무관, 자기 자신과 XOR하면 0)을 이해해야 함.
*/
uint8_t calc_xor_checksum(const uint8_t *buffer, int length);
/*
예시 입력: {0x12, 0x34, 0x56}
예시 출력: 0x12 ^ 0x34 ^ 0x56 = 0x70
*/

// 6. 간단한 UART 패킷 파서 FSM
/*
설명: STX(0x02)로 시작, ETX(0x03)로 끝나는 패킷을 파싱하는 상태 머신 구현.
상태 전이, FSM 개념, 시리얼 통신의 기본 구조 이해 필요.
*/
typedef struct { int state; int idx; uint8_t data[16]; } uart_fsm_t;
void uart_fsm_init(uart_fsm_t *fsm);
bool uart_fsm_parse(uart_fsm_t *fsm, uint8_t byte);
/*
예시 입력: 0x02, 0x11, 0x22, 0x03
예시 출력: data={0x11, 0x22}, return true
*/

// 7. 이동 평균 필터(Moving Average Filter)
/*
설명: 최근 5개 샘플의 평균을 반환합니다.
순환 버퍼, 정적 배열, 평균 개념.
*/
float moving_average_filter(int new_sample);
/*
예시 입력: 1,2,3,4,5,6
예시 출력: 3.0 (for last 5 samples: 2,3,4,5,6)
*/

// 8. 워치독 타이머 "Pat" 시뮬레이션
/*
설명: 주기적으로 wdt_pat()이 호출되지 않으면 "SYSTEM RESET" 출력.
타이머, 주기적 호출, 시스템 안정성 개념.
*/
void wdt_pat(void);
void wdt_tick(void);
/*
예시: wdt_pat()이 3초 이상 호출되지 않으면 "SYSTEM RESET" 출력
*/

// 9. 엔디안(Endianness) 변환
/*
설명: 32비트 정수의 엔디안을 변환합니다.
리틀엔디안/빅엔디안 개념, 바이트 단위 접근.
*/
uint32_t swap_endian32(uint32_t value);
/*
예시 입력: 0x12345678
예시 출력: 0x78563412
*/

// 10. 고정 소수점(Fixed-Point) 덧셈/뺄셈
/*
설명: Q15.16 형식의 두 고정 소수점 숫자를 더하고 빼는 매크로.
고정소수점 연산, 비트 시프트 개념.
*/
#define Q15_16_ADD(a, b) ((a)+(b))
#define Q15_16_SUB(a, b) ((a)-(b))
/*
예시 입력: a=0x00010000(1.0), b=0x00008000(0.5)
예시 출력: Q15_16_ADD(a,b)=0x00018000(1.5)
*/

// 11. ADC 값 전압 변환
/*
설명: 12비트 ADC(0-4095) 값을 0~3.3V로 변환.
스케일링, 부동소수점 연산.
*/
float adc_to_voltage(uint16_t adc_value);
/*
예시 입력: 2048
예시 출력: 1.65
*/

// 12. PWM 듀티 사이클 계산
/*
설명: ARR=999일 때, 듀티 75%를 위한 CCR 값 계산.
비례식, 타이머 개념.
*/
uint16_t calc_pwm_ccr(uint16_t arr, float duty_cycle);
/*
예시 입력: arr=999, duty_cycle=0.75
예시 출력: 749
*/

// 13. 딜레이 기반 버튼 디바운싱
/*
설명: 버튼 입력 감지 후 20ms 딜레이 후 상태 재확인.
디바운싱, 소프트웨어 딜레이, 신뢰성.
*/
bool debounce_button(bool raw_input);
/*
예시 입력: 버튼이 튀는 신호
예시 출력: 안정된 true/false
*/

// 14. volatile 키워드의 목적
/*
설명: volatile이 필요한 이유와 예시.
최적화, 하드웨어 레지스터, 인터럽트 변수.
*/
volatile int flag;
/*
예시: while(!flag) {} // 인터럽트에서 flag=1;로 변경
*/

// 15. 메모리 맵 레지스터 제어 매크로
/*
설명: 특정 주소에 값을 쓰고 읽는 매크로.
포인터, 하드웨어 레지스터 접근.
*/
#define REG_WRITE(addr, val) (*(volatile uint32_t *)(addr) = (val))
#define REG_READ(addr) (*(volatile uint32_t *)(addr))
/*
예시: REG_WRITE(0x40010C08, 0x1234);
*/

// 16. 8비트 합계 체크섬(Sum Checksum) 계산
/*
설명: 모든 바이트를 더해 8비트 체크섬 계산.
오버플로우, 누적합.
*/
uint8_t calc_sum_checksum(const uint8_t *buf, int len);
/*
예시 입력: {1,2,3}
예시 출력: 6
*/

// 17. 설정된 비트 수 카운트
/*
설명: 32비트 정수에서 1로 설정된 비트 개수 세기.
비트마스크, Brian Kernighan 알고리즘.
*/
int count_set_bits(uint32_t value);
/*
예시 입력: 0xF0F0F0F0
예시 출력: 16
*/

// 18. XOR를 이용한 변수 교환
/*
설명: 임시 변수 없이 XOR로 두 변수 값 교환.
XOR의 성질, 메모리 절약.
*/
void xor_swap(int *a, int *b);
/*
예시 입력: a=5, b=7
예시 출력: a=7, b=5
*/

// 19. 배열을 이용한 간단한 LIFO(Stack) 구현
/*
설명: 정수 배열로 push/pop 기능 구현.
스택, LIFO 개념.
*/
typedef struct { int data[8]; int top; } stack_t;
void stack_init(stack_t *s);
bool stack_push(stack_t *s, int v);
bool stack_pop(stack_t *s, int *v);
/*
예시 입력: push 1,2,3, pop
예시 출력: 3
*/

// 20. 배열을 이용한 간단한 FIFO(Queue) 구현
/*
설명: 정수 배열과 인덱스로 enqueue/dequeue 구현.
큐, FIFO 개념, 원형 버퍼.
*/
typedef struct { int data[8]; int head, tail, count; } queue_t;
void queue_init(queue_t *q);
bool queue_enqueue(queue_t *q, int v);
bool queue_dequeue(queue_t *q, int *v);
/*
예시 입력: enqueue 1,2,3, dequeue
예시 출력: 1
*/

// 21. 16진수 문자열을 정수로 변환
/*
설명: "0x1A"와 같은 문자열을 정수로 변환.
문자열 파싱, strtol 함수 활용.
*/
int hexstr_to_int(const char *hexstr);
/*
예시 입력: "0x1A"
예시 출력: 26
*/

// 22. 정수를 2진수 문자열로 변환
/*
설명: 8비트 정수를 "00011010"과 같은 문자열로 변환.
문자열, 비트마스크, sprintf.
*/
void int_to_binstr(uint8_t value, char *out);
/*
예시 입력: 26
예시 출력: "00011010"
*/

// 23. const 키워드의 다양한 사용법
/*
설명: const int*, int* const 등 4가지 형태와 의미.
포인터와 const의 결합.
*/
// const int *p; int * const p; const int * const p; int const *p;
/*
예시: 설명 주석
*/

// 24. 간단한 타이머 초기화
/*
설명: 16MHz 클럭에서 1ms마다 인터럽트 발생하도록 타이머 설정.
분주비, 주기 계산, 타이머 레지스터.
*/
void timer_init_1ms(uint32_t sysclk, uint32_t *prescaler, uint32_t *period);
/*
예시 입력: sysclk=16000000
예시 출력: prescaler=15999, period=0
*/

// 25. LED 점멸 구현
/*
설명: GPIO 핀을 500ms 주기로 토글.
딜레이, 무한 루프, GPIO 제어.
*/
void led_blink_loop(void);
/*
예시: 500ms마다 LED ON/OFF 반복
*/