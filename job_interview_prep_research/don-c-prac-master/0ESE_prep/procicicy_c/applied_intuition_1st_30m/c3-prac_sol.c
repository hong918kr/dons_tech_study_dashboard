/*
Applied Intuition 코딩 챌린지 대비 50제


카테고리 3: 데이터 파싱 및 상태 머신 (Data Parsing & FSM)
23. [0x7E, 길이, 데이터..., 체크섬] 패킷 파서 함수 (체크섬=합)
    Follow-up: 유효하지 않은 패킷 처리법
24. enum/switch로 LED 3상태(OFF, ON, BLINKING) FSM 함수
    Follow-up: 상태 변수 static 선언 이유
25. 쉼표로 구분된 숫자 문자열 파싱 함수
    Follow-up: strtok의 위험성과 대안
26. 16진수 문자열을 정수로 변환하는 함수 int hex_to_int(const char* hex_str)
    Follow-up: 유효하지 않은 문자 처리법
27. 12비트 ADC값(0-4095)을 0.0~3.3V로 변환하는 함수
    Follow-up: 정수 연산만으로 변환하는 방법


*/

/*
카테고리 3: 데이터 파싱 및 상태 머신 (Data Parsing & FSM)
각 문제는 함수 시그니처, 설명, 예제 변수, follow-up 답변, 테스트 코드(PASS/FAIL)를 포함합니다.
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// 23. [0x7E, 길이, 데이터..., 체크섬] 패킷 파서 함수 (체크섬=합)
// 설명: 0x7E로 시작, 길이, 데이터, 체크섬(데이터 합) 구조의 패킷을 파싱하는 함수
// Follow-up: 유효하지 않은 패킷(길이 불일치, 체크섬 오류 등)은 -1을 반환
int parse_packet(const uint8_t* pkt, int pkt_len, uint8_t* data, int* data_len) {
    if (pkt_len < 4) return -1; // 최소 길이
    if (pkt[0] != 0x7E) return -1;
    int len = pkt[1];
    if (len + 3 != pkt_len) return -1; // 7E len data... cksum
    uint8_t sum = 0;
    for (int i = 0; i < len; ++i) sum += pkt[2 + i];
    if (sum != pkt[2 + len]) return -1;
    memcpy(data, pkt + 2, len);
    *data_len = len;
    return 0;
}
// Follow-up 답변: 길이 불일치, 체크섬 오류, 시작 바이트 오류 등에서 -1 반환

// 24. enum/switch로 LED 3상태 FSM 함수
// 설명: LED 상태(OFF, ON, BLINKING)를 enum과 switch문으로 관리하는 FSM 함수
// Follow-up: 상태 변수 static 선언 이유는 함수 호출 간 상태 유지(메모리 유지) 때문
typedef enum { LED_OFF, LED_ON, LED_BLINKING } LedState;
const char* led_state_str[] = {"OFF", "ON", "BLINKING"};
LedState led_fsm(LedState input) {
    static LedState state = LED_OFF;
    switch (input) {
        case LED_ON: state = LED_ON; break;
        case LED_OFF: state = LED_OFF; break;
        case LED_BLINKING: state = LED_BLINKING; break;
        default: break;
    }
    return state;
}
// Follow-up 답변: static 변수로 상태를 함수 호출 간에 유지

// 25. 쉼표로 구분된 숫자 문자열 파싱 함수
// 설명: "1,2,3" 형태의 문자열을 int 배열로 파싱
// Follow-up: strtok은 thread-unsafe, 원본 문자열을 파괴. 대안은 strtok_r 또는 직접 파싱.
int parse_csv(const char* str, int* arr, int max_cnt) {
    int cnt = 0;
    const char* p = str;
    while (*p && cnt < max_cnt) {
        arr[cnt++] = atoi(p);
        while (*p && *p != ',') ++p;
        if (*p == ',') ++p;
    }
    return cnt;
}
// Follow-up 답변: strtok은 원본 문자열을 변경하므로, 복사본 사용 또는 직접 파싱 권장

// 26. 16진수 문자열을 정수로 변환하는 함수
// 설명: "1A3F" → 0x1A3F 변환
// Follow-up: 유효하지 않은 문자가 있으면 -1 반환
int hex_to_int(const char* hex_str) {
    int val = 0;
    if (!hex_str || !*hex_str) return -1;
    while (*hex_str) {
        char c = toupper((unsigned char)*hex_str);
        if ('0' <= c && c <= '9') val = val * 16 + (c - '0');
        else if ('A' <= c && c <= 'F') val = val * 16 + (c - 'A' + 10);
        else return -1;
        ++hex_str;
    }
    return val;
}
// Follow-up 답변: 유효하지 않은 문자가 있으면 -1 반환

// 27. 12비트 ADC값(0-4095)을 0.0~3.3V로 변환하는 함수
// 설명: 12비트 ADC값을 실수(부동소수점) 전압으로 변환
// Follow-up: 정수 연산만으로 변환하려면 (adc * 3300) / 4095 (mV 단위) 사용
float adc_to_voltage(int adc) {
    return (adc / 4095.0f) * 3.3f;
}
int adc_to_mv(int adc) {
    return (adc * 3300) / 4095;
}
// Follow-up 답변: 정수 연산만으로 (adc * 3300) / 4095 사용

// ------------------- 테스트 코드 -------------------
#define TEST_LABEL_WIDTH 45
void test_result(const char* label, int ok) {
    printf("%-*s %s\n", TEST_LABEL_WIDTH, label, ok ? "[PASS]" : "[FAIL]");
}

int main(void) {
    // 23. 패킷 파서 테스트
    uint8_t pkt1[] = {0x7E, 3, 0x11, 0x22, 0x33, 0x66}; // 0x11+0x22+0x33=0x66
    uint8_t data[10]; int data_len = 0;
    test_result("23. parse_packet valid", parse_packet(pkt1, 6, data, &data_len) == 0 && data_len == 3 && data[0] == 0x11 && data[2] == 0x33);
    uint8_t pkt2[] = {0x7E, 2, 0x10, 0x20, 0x31}; // wrong checksum
    test_result("23. parse_packet invalid", parse_packet(pkt2, 5, data, &data_len) == -1);

    // 24. LED FSM 테스트
    test_result("24. led_fsm ON", led_fsm(LED_ON) == LED_ON);
    test_result("24. led_fsm BLINKING", led_fsm(LED_BLINKING) == LED_BLINKING);
    test_result("24. led_fsm OFF", led_fsm(LED_OFF) == LED_OFF);

    // 25. CSV 파싱 테스트
    int arr[5];
    int cnt = parse_csv("10,20,30", arr, 5);
    test_result("25. parse_csv count", cnt == 3);
    test_result("25. parse_csv values", arr[0] == 10 && arr[1] == 20 && arr[2] == 30);

    // 26. hex_to_int 테스트
    test_result("26. hex_to_int valid", hex_to_int("1A3F") == 0x1A3F);
    test_result("26. hex_to_int invalid", hex_to_int("1G3F") == -1);

    // 27. ADC 변환 테스트
    float v = adc_to_voltage(4095);
    test_result("27. adc_to_voltage(4095)", v > 3.29f && v < 3.31f);
    test_result("27. adc_to_mv(4095)", adc_to_mv(4095) == 3300);

    return 0;
}