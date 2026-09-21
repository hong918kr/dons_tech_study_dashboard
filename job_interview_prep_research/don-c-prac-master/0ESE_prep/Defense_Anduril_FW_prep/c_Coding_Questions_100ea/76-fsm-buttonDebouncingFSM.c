/*
Implement a button debouncing FSM using a switch statement.	
enum (states), switch (state handling).
Basic pattern for GPIO input processing.
*/

/*

이 코드는 버튼 디바운싱(Button Debouncing) FSM(유한 상태 기계, Finite State Machine) 예제입니다. 
실제 하드웨어 버튼은 누르거나 뗄 때 접점이 튀어서(채터링) 짧은 시간 동안 신호가 불안정하게 변할 수 있는데,
이를 소프트웨어적으로 안정화하는 대표적인 패턴입니다.
*/

#include <stdio.h>
#include <stdbool.h>

// Example button states for FSM
/*
(상태) enum 
BTN_RELEASED: 버튼이 눌리지 않은 상태
BTN_BOUNCE_PRESS: 버튼이 눌렸을 때 디바운싱 중(확정 대기)
BTN_PRESSED: 버튼이 눌린 상태
BTN_BOUNCE_RELEASE: 버튼이 떼졌을 때 디바운싱 중(확정 대기)
*/
typedef enum {
    BTN_RELEASED,
    BTN_BOUNCE_PRESS,
    BTN_PRESSED,
    BTN_BOUNCE_RELEASE
} btn_state_t;

// Simulated GPIO input (0: released, 1: pressed)
/*
2. FSM 구조체
state: 현재 FSM 상태
debounce_counter: 디바운싱 카운터
debounce_ticks: 디바운싱에 필요한 틱 수(샘플 수)
output: 버튼이 눌렸다고 확정된 경우 true
*/
typedef struct {
    btn_state_t state;
    int debounce_counter;
    int debounce_ticks; // Number of ticks to debounce
    bool output; // true if button is considered pressed
} button_fsm_t;

// Initialize FSM
/*
3. 초기화 함수
FSM 상태와 카운터, 디바운스 틱 수를 초기화합니다.
*/
void button_fsm_init(button_fsm_t *fsm, int debounce_ticks) {
    fsm->state = BTN_RELEASED;
    fsm->debounce_counter = 0;
    fsm->debounce_ticks = debounce_ticks;
    fsm->output = false;
}

// Call this function every tick with the current GPIO input
/*
4. 상태 업데이트 함수

매 틱마다(주기적으로) 현재 버튼 입력값(gpio_input: 0 또는 1)을 받아 FSM 상태를 전이시킵니다.
각 상태에서 입력에 따라 상태 전이와 카운터 증가, output 설정을 처리합니다.
*/
void button_fsm_update(button_fsm_t *fsm, int gpio_input) {
    switch (fsm->state) {
        case BTN_RELEASED:
            if (gpio_input) {
                fsm->state = BTN_BOUNCE_PRESS;
                fsm->debounce_counter = 0;
            }
            break;
        case BTN_BOUNCE_PRESS:
            if (gpio_input) {
                if (++fsm->debounce_counter >= fsm->debounce_ticks) {
                    fsm->state = BTN_PRESSED;
                    fsm->output = true;
                }
            } else {
                fsm->state = BTN_RELEASED;
            }
            break;
        case BTN_PRESSED:
            if (!gpio_input) {
                fsm->state = BTN_BOUNCE_RELEASE;
                fsm->debounce_counter = 0;
            }
            break;
        case BTN_BOUNCE_RELEASE:
            if (!gpio_input) {
                if (++fsm->debounce_counter >= fsm->debounce_ticks) {
                    fsm->state = BTN_RELEASED;
                    fsm->output = false;
                }
            } else {
                fsm->state = BTN_PRESSED;
            }
            break;
    }
}

// --- Test code ---
/*
5. 테스트 코드(main)
input_sequence 배열로 버튼 입력 시퀀스를 시뮬레이션합니다.
각 틱마다 FSM을 업데이트하고, 상태와 output을 출력합니다.


*/
int main(void) {
    button_fsm_t fsm;
    button_fsm_init(&fsm, 3); // 3 ticks debounce

    // Simulated GPIO input sequence (0: released, 1: pressed)
    int input_sequence[] = {0,0,1,0,1,1,1,1,0,0,0,1,1,0,0,0};
    int len = sizeof(input_sequence)/sizeof(input_sequence[0]);

    printf("Tick | GPIO | State            | Output\n");
    printf("----------------------------------------\n");
    for (int i = 0; i < len; ++i) {
        button_fsm_update(&fsm, input_sequence[i]);
        const char *state_str[] = {"RELEASED", "BOUNCE_PRESS", "PRESSED", "BOUNCE_RELEASE"};
        printf("%4d |  %d   | %-14s | %d\n", i, input_sequence[i], state_str[fsm.state], fsm.output);
    }
    return 0;
}

/*
예시 출력
버튼이 연속적으로 눌리거나 떼질 때, 디바운싱 카운터가 충분히 쌓여야 상태가 확정됩니다.
output이 1이 되는 시점이 실제로 버튼이 "확실히 눌렸음"을 의미합니다.
------------------ Example Input/Output ------------------

Tick | GPIO | State            | Output
----------------------------------------
   0 |  0   | RELEASED        | 0
   1 |  0   | RELEASED        | 0
   2 |  1   | BOUNCE_PRESS    | 0
   3 |  0   | RELEASED        | 0
   4 |  1   | BOUNCE_PRESS    | 0
   5 |  1   | BOUNCE_PRESS    | 0
   6 |  1   | PRESSED         | 1
   7 |  1   | PRESSED         | 1
   8 |  0   | BOUNCE_RELEASE  | 1
   9 |  0   | BOUNCE_RELEASE  | 1
  10 |  0   | RELEASED        | 0
  11 |  1   | BOUNCE_PRESS    | 0
  12 |  1   | BOUNCE_PRESS    | 0
  13 |  0   | RELEASED        | 0
  14 |  0   | RELEASED        | 0
  15 |  0   | RELEASED        | 0
*/

