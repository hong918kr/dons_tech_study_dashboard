/*
Applied Intuition 코딩 챌린지 대비 50제

카테고리 3: 데이터 파싱 및 상태 머신 (Data Parsing & FSM)

23. [0x7E, 길이, 데이터..., 체크섬] 패킷 파서 함수 (체크섬=합)
    - 설명: 0x7E로 시작, 길이, 데이터, 체크섬(데이터 합) 구조의 패킷을 파싱하는 함수
    - Follow-up: 유효하지 않은 패킷(길이 불일치, 체크섬 오류 등)은 -1을 반환

24. enum/switch로 LED 3상태(OFF, ON, BLINKING) FSM 함수
    - 설명: LED 상태(OFF, ON, BLINKING)를 enum과 switch문으로 관리하는 FSM 함수
    - Follow-up: 상태 변수 static 선언 이유는 함수 호출 간 상태 유지(메모리 유지) 때문

25. 쉼표로 구분된 숫자 문자열 파싱 함수
    - 설명: "1,2,3" 형태의 문자열을 int 배열로 파싱
    - Follow-up: strtok은 thread-unsafe, 원본 문자열을 파괴. 대안은 strtok_r 또는 직접 파싱.

26. 16진수 문자열을 정수로 변환하는 함수 int hex_to_int(const char* hex_str)
    - 설명: "1A3F" → 0x1A3F 변환
    - Follow-up: 유효하지 않은 문자가 있으면 -1 반환

27. 12비트 ADC값(0-4095)을 0.0~3.3V로 변환하는 함수
    - 설명: 12비트 ADC값을 실수(부동소수점) 전압으로 변환
    - Follow-up: 정수 연산만으로 변환하려면 (adc * 3300) / 4095 (mV 단위) 사용

*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// 23. 패킷 파서 함수 원형
int parse_packet(const uint8_t* pkt, int pkt_len, uint8_t* data, int* data_len) {}

// 24. LED 3상태 FSM 함수 원형
typedef enum { LED_OFF, LED_ON, LED_BLINKING } LedState;
const char* led_state_str[] = {"OFF", "ON", "BLINKING"};
LedState led_fsm(LedState input) {}

// 25. 쉼표로 구분된 숫자 문자열 파싱 함수 원형
int parse_csv(const char* str, int* arr, int max_cnt) {}

// 26. 16진수 문자열을 정수로 변환하는 함수 원형
int hex_to_int(const char* hex_str) {}

// 27. 12비트 ADC값을 전압으로 변환하는 함수 원형
float adc_to_voltage(int adc) {}
int adc_to_mv(int adc) {}

#define TEST_LABEL_WIDTH 45
void test_result(const char* label, int ok) {
    printf("%-*s %s\n", TEST_LABEL_WIDTH, label, ok ? "[PASS]" : "[FAIL]");
}

int main(void) {
    // 테스트 코드는 남겨둡니다.
    return 0;
}