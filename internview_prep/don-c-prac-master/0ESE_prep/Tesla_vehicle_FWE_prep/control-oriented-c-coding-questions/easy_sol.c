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
#include <string.h>
#include <stdlib.h>

// 1. 특정 비트 설정(Set)
uint8_t set_bit3(uint8_t value) {
    return value | (1 << 3);
}

/*
예시 입력: 0x00
예시 출력: 0x08
*/

// 2. 특정 비트 해제(Clear)
uint16_t clear_bit5(uint16_t value) {
    return value & ~(1 << 5);
}

/*
예시 입력: 0xFFFF
예시 출력: 0xFFDF
*/

// 3. 특정 비트 토글(Toggle)
uint8_t toggle_bit(uint8_t value, uint8_t n) {
    return value ^ (1 << n);
}

/*
예시 입력: value=0x10, n=4
예시 출력: 0x00
*/

// 4. 특정 비트 확인
bool is_bit_set(uint32_t value, uint8_t n) {
    return (value & (1 << n)) != 0;
}

/*
예시 입력: value=0x10, n=4
예시 출력: true
*/

// 5. XOR 체크섬 계산
uint8_t calc_xor_checksum(const uint8_t *buffer, int length) {
    uint8_t checksum = 0;
    for (int i = 0; i < length; ++i) {
        checksum ^= buffer[i];
    }
    return checksum;
}


/*
6. 간단한 UART 패킷 파서 FSM
기본 개념:

UART 통신에서는 데이터의 시작(STX, 0x02)과 끝(ETX, 0x03)을 구분해 패킷을 파싱해야 합니다.
FSM(유한 상태 기계)은 입력에 따라 상태를 전이하며, 각 상태에서의 동작을 정의합니다.
왜 이렇게 구현?

UART_IDLE 상태에서 STX(0x02)가 오면 데이터 수신 시작.
UART_DATA 상태에서 ETX(0x03)가 오면 패킷 종료, 아니면 데이터를 버퍼에 저장.
FSM을 쓰면 노이즈나 잘못된 입력에도 견고하게 동작합니다.
추가 개념:

FSM은 임베디드 통신, 프로토콜 파싱 등에서 매우 널리 쓰이는 패턴입니다.
*/

// 6. 간단한 UART 패킷 파서 FSM
/*
예시 입력: {0x12, 0x34, 0x56}
예시 출력: 0x12 ^ 0x34 ^ 0x56 = 0x70
*/

typedef enum { UART_IDLE, UART_DATA } uart_state_e;
typedef struct { int state; int idx; uint8_t data[16]; } uart_fsm_t;
void uart_fsm_init(uart_fsm_t *fsm) {
    fsm->state = UART_IDLE;
    fsm->idx = 0;
}
bool uart_fsm_parse(uart_fsm_t *fsm, uint8_t byte) {
    // STX: 0x02, ETX: 0x03
    switch (fsm->state) {
        case UART_IDLE:
            if (byte == 0x02) {
                fsm->idx = 0;
                fsm->state = UART_DATA;
            }
            break;
        case UART_DATA:
            if (byte == 0x03) {
                fsm->state = UART_IDLE;
                return true; // Packet complete
            } else if (fsm->idx < 16) {
                fsm->data[fsm->idx++] = byte;
            }
            break;
    }
    return false;
}



/*------------------------------------------------------*/
/*

*/
/*
7. 이동 평균 필터(Moving Average Filter)
기본 개념:

이동 평균 필터는 최근 N개의 샘플의 평균을 내어 노이즈를 줄입니다.
순환 버퍼(원형 배열)를 사용하면 오래된 데이터가 자동으로 덮어써집니다.
왜 이렇게 구현?

static 배열과 인덱스를 사용해 최근 5개 값만 저장.
새 샘플이 들어오면 인덱스를 순환시키며 값을 갱신.
평균을 구할 때는 현재 저장된 값만 합산.
추가 개념:

이동 평균은 센서 노이즈 제거, 신호 평활화 등에 자주 사용됩니다.
*/


// 7. 이동 평균 필터(Moving Average Filter)

/*
예시 입력: 0x02, 0x11, 0x22, 0x03
예시 출력: data={0x11, 0x22}, return true
*/

float moving_average_filter(int new_sample) {
    #define MA_SIZE 5
    static int buf[MA_SIZE] = {0};
    static int idx = 0, count = 0;
    buf[idx] = new_sample;
    idx = (idx + 1) % MA_SIZE;
    if (count < MA_SIZE) count++;
    int sum = 0;
    for (int i = 0; i < count; ++i) sum += buf[i];
    return (float)sum / count;
}



/*------------------------------------------------------*/
/*
8. 워치독 타이머 "Pat" 시뮬레이션
기본 개념:

워치독 타이머는 시스템이 정상 동작 중임을 주기적으로 "pat"해야 리셋이 발생하지 않습니다.
만약 일정 시간 동안 pat이 없으면 시스템이 멈췄다고 판단, 리셋을 발생시킵니다.
왜 이렇게 구현?

wdt_pat()이 호출되면 카운터를 0으로 초기화.
wdt_tick()이 주기적으로 호출되어 카운터 증가, 임계값(3) 이상이면 리셋 메시지 출력.
추가 개념:

임베디드 시스템에서 소프트웨어 오류로 인한 멈춤을 자동으로 복구하는 안전장치입니다.
*/

// 8. 워치독 타이머 "Pat" 시뮬레이션
/*
예시 입력: 1,2,3,4,5,6
예시 출력: 3.0 (for last 5 samples: 2,3,4,5,6)
*/
/*
예시: wdt_pat()이 3초 이상 호출되지 않으면 "SYSTEM RESET" 출력
*/

static int wdt_counter = 0;
void wdt_pat(void) {
    wdt_counter = 0;
}
void wdt_tick(void) {
    wdt_counter++;
    if (wdt_counter >= 3) {
        printf("SYSTEM RESET\n");
        wdt_counter = 0;
    }
}






// 9. 엔디안(Endianness) 변환
/*
예시 입력: 0x12345678
예시 출력: 0x78563412
*/

uint32_t swap_endian32(uint32_t value) {
    return ((value & 0xFF) << 24) |
           ((value & 0xFF00) << 8) |
           ((value & 0xFF0000) >> 8) |
           ((value & 0xFF000000) >> 24);
}



/*------------------------------------------------------*/
/*
10. 고정 소수점(Fixed-Point) 덧셈/뺄셈
기본 개념:

고정 소수점(Q15.16)은 정수로 소수점 이하를 표현하는 방식(Qm.n: m비트 정수, n비트 소수).
실수 연산이 느린 MCU에서 빠르고 정밀하게 소수를 다루기 위해 사용.
왜 이렇게 구현?

같은 Q포맷이면 단순히 정수끼리 더하고 빼면 됩니다.
덧셈/뺄셈은 오버플로우만 주의하면 정수 연산과 동일.
추가 개념:

곱셈/나눗셈은 연산 후 비트 시프트로 스케일을 맞춰야 함(덧셈/뺄셈은 그대로).




*/
// 10. 고정 소수점(Fixed-Point) 덧셈/뺄셈
/*
설명: Q15.16 형식의 두 고정 소수점 숫자를 더하고 빼는 매크로.
고정소수점 연산, 비트 시프트 개념.
*/
/*
예시 입력: a=0x00010000(1.0), b=0x00008000(0.5)
예시 출력: Q15_16_ADD(a,b)=0x00018000(1.5)
*/

#define Q15_16_ADD(a, b) ((a)+(b))
#define Q15_16_SUB(a, b) ((a)-(b))




/*------------------------------------------------------*/
/*
11. ADC 값 전압 변환
기본 개념:

ADC는 0~4095(12비트) 범위의 값을 읽어옴.
실제 전압은 (ADC값 / 4095) * 기준전압(예: 3.3V)로 변환.
왜 이렇게 구현?

비례식: ADC값이 0이면 0V, 4095면 3.3V.
부동소수점 연산으로 정확한 전압 계산.
추가 개념:

기준전압이 다르면 해당 값으로 바꿔야 함.
실제 하드웨어에서는 ADC 오차, 기준전압 오차도 고려해야 함.
*/

// 11. ADC 값 전압 변환
/*
예시 입력: 2048
예시 출력: 1.65
*/


float adc_to_voltage(uint16_t adc_value) {
    return (adc_value / 4095.0f) * 3.3f;
}




/*------------------------------------------------------*/
/*
12. PWM 듀티 사이클 계산
기본 개념:

PWM은 주기(ARR)와 듀티(비율)에 따라 비교값(CCR)을 설정.
CCR = (ARR+1) * 듀티비율 - 1
왜 이렇게 구현?

ARR=999면 카운터는 0~999까지 1000번 셉니다.
듀티 75%면 1000 * 0.75 = 750, 인덱스는 0부터 시작이므로 749.
추가 개념:

CCR이 ARR보다 크면 항상 ON, 0이면 항상 OFF.
실전에서는 오버플로우, 분해능도 고려.
*/
// 12. PWM 듀티 사이클 계산

/*
예시 입력: arr=999, duty_cycle=0.75
예시 출력: 749
*/

uint16_t calc_pwm_ccr(uint16_t arr, float duty_cycle) {
    return (uint16_t)((arr + 1) * duty_cycle) - 1;
}




/*------------------------------------------------------*/
/*
13. 딜레이 기반 버튼 디바운싱
기본 개념:

버튼을 누를 때 접점이 튀어서(채터링) 신호가 불안정.
입력 변화 후 일정 시간(20ms) 동안 신호가 유지되어야 진짜 눌림/해제라고 판단.
왜 이렇게 구현?

입력이 바뀌면 시간 기록, 20ms 이상 같은 상태면 그 값을 인정.
소프트웨어적으로 노이즈를 제거하는 대표적 방법.
추가 개념:

하드웨어 디바운스(콘덴서 등)와 병행하면 더 안정적.
*/

// 13. 딜레이 기반 버튼 디바운싱
/*
설명: 버튼 입력 감지 후 20ms 딜레이 후 상태 재확인.
디바운싱, 소프트웨어 딜레이, 신뢰성.
예시 입력: 버튼이 튀는 신호
예시 출력: 안정된 true/false
*/
#include <time.h>
bool debounce_button(bool raw_input) {
    static bool last_state = false;
    static clock_t last_time = 0;
    clock_t now = clock();
    if (raw_input != last_state) {
        last_time = now;
        last_state = raw_input;
    }
    if (((now - last_time) * 1000 / CLOCKS_PER_SEC) > 20) {
        return last_state;
    }
    return !last_state;
}



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
예시 입력: {1,2,3}
예시 출력: 6
*/
uint8_t calc_sum_checksum(const uint8_t *buf, int len) {
    uint8_t sum = 0;
    for (int i = 0; i < len; ++i) sum += buf[i];
    return sum;
}



// 17. 설정된 비트 수 카운트
/*
예시 입력: 0xF0F0F0F0
예시 출력: 16
*/


int count_set_bits(uint32_t value) {
    int count = 0;
    while (value) {
        value &= (value - 1);
        count++;
    }
    return count;
}





/*------------------------------------------------------*/
/*
18. XOR를 이용한 변수 교환
기본 개념:

임시 변수 없이 두 값을 교환하는 고전적인 비트 연산 트릭.
XOR의 성질: a^b^b = a
왜 이렇게 구현?

*a ^= *b; *b ^= *a; *a ^= *b; 순서로 하면 임시 변수 없이 값이 바뀜.
단, a와 b가 같은 주소일 때는 동작하지 않으니 체크 필요.
추가 개념:

실전에서는 가독성, 최적화 때문에 임시 변수 사용이 더 권장됨.
*/
// 18. XOR를 이용한 변수 교환
void xor_swap(int *a, int *b) {
    if (a == b) return;
    *a ^= *b;
    *b ^= *a;
    *a ^= *b;
}

/*
예시 입력: a=5, b=7
예시 출력: a=7, b=5
*/

// 19. 배열을 이용한 간단한 LIFO(Stack) 구현
typedef struct { int data[8]; int top; } stack_t;
void stack_init(stack_t *s) { s->top = 0; }
bool stack_push(stack_t *s, int v) {
    if (s->top >= 8) return false;
    s->data[s->top++] = v;
    return true;
}
bool stack_pop(stack_t *s, int *v) {
    if (s->top == 0) return false;
    *v = s->data[--s->top];
    return true;
}

/*
예시 입력: push 1,2,3, pop
예시 출력: 3
*/

// 20. 배열을 이용한 간단한 FIFO(Queue) 구현
typedef struct { int data[8]; int head, tail, count; } queue_t;
void queue_init(queue_t *q) { q->head = q->tail = q->count = 0; }
bool queue_enqueue(queue_t *q, int v) {
    if (q->count >= 8) return false;
    q->data[q->tail] = v;
    q->tail = (q->tail + 1) % 8;
    q->count++;
    return true;
}
bool queue_dequeue(queue_t *q, int *v) {
    if (q->count == 0) return false;
    *v = q->data[q->head];
    q->head = (q->head + 1) % 8;
    q->count--;
    return true;
}

/*
예시 입력: enqueue 1,2,3, dequeue
예시 출력: 1
*/





/*------------------------------------------------------*/
/*
21. 16진수 문자열을 정수로 변환
기본 개념:

"0x1A"와 같은 문자열을 정수로 바꾸려면 파싱이 필요.
C 표준 함수 strtol은 진수(2~36)를 지정해 문자열을 정수로 변환.
왜 이렇게 구현?

strtol의 세 번째 인자에 0을 주면 "0x" 접두사도 자동 인식.
다양한 진수 입력에 유연하게 대응 가능.
추가 개념:

입력이 유효한지 체크하려면 반환 포인터도 활용.
*/
// 21. 16진수 문자열을 정수로 변환
/*
예시 입력: "0x1A"
예시 출력: 26
*/
int hexstr_to_int(const char *hexstr) {
    return (int)strtol(hexstr, NULL, 0);
}



// 22. 정수를 2진수 문자열로 변환
void int_to_binstr(uint8_t value, char *out) {
    for (int i = 7; i >= 0; --i) {
        out[7 - i] = (value & (1 << i)) ? '1' : '0';
    }
    out[8] = '\0';
}

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







/*------------------------------------------------------*/
/*
24. 간단한 타이머 초기화
기본 개념:

타이머는 분주비(prescaler)와 주기(period)로 원하는 주기 인터럽트를 만듦.
1ms마다 인터럽트: (시스템클럭/1000) - 1
왜 이렇게 구현?

prescaler = (sysclk/1000) - 1로 설정하면 1ms마다 타이머 오버플로우.
period는 0으로 두면 한 번 카운트마다 인터럽트 발생.
추가 개념:

실제 MCU마다 타이머 구조가 다르니 데이터시트 참고.
오차, 클럭 소스, 인터럽트 우선순위 등도 실전에서는 고려.
*/
// 24. 간단한 타이머 초기화
/*
예시 입력: sysclk=16000000
예시 출력: prescaler=15999, period=0
*/

void timer_init_1ms(uint32_t sysclk, uint32_t *prescaler, uint32_t *period) {
    *prescaler = (sysclk / 1000) - 1;
    *period = 0;
}



// 25. LED 점멸 구현
/*
설명: GPIO 핀을 500ms 주기로 토글.
딜레이, 무한 루프, GPIO 제어.
예시: 500ms마다 LED ON/OFF 반복
*/
void led_blink_loop(void) {
    while (1) {
        // gpio_set(LED_PIN, 1);
        printf("LED ON\n");
        // delay_ms(500);
        printf("LED OFF\n");
        // delay_ms(500);
        break; // for demonstration, remove in real code
    }
}

