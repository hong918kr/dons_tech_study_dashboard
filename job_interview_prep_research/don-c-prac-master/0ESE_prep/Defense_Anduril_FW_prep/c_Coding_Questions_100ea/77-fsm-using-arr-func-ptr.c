/*
Implement an FSM using an array of function pointers.
typedef function pointer, array.
Designing scalable and flexible state machines.
*/

/*

이 코드는 함수 포인터 배열을 이용한
유한 상태 기계(FSM, Finite State Machine) 구현 예제입니다.
아래와 같이 동작합니다.


요약
상태별 동작을 함수 포인터 배열로 관리하여, 코드가 간결하고 확장성이 높아집니다.
입력값에 따라 상태를 전이하고, 해당 상태의 동작을 실행합니다.
새로운 상태가 추가될 때 함수만 추가하면 되므로 유지보수가 쉽습니다.
실제 임베디드, 통신, UI 등 다양한 FSM 설계에 활용되는 패턴입니다.
*/

#include <stdio.h>

// Define FSM states 
/*
1. 상태(enum) 정의
FSM의 상태를 IDLE, RUN, STOP 3가지로 정의합니다.
*/
typedef enum {
    STATE_IDLE,
    STATE_RUN,
    STATE_STOP,
    STATE_MAX
} fsm_state_t;



// State handler functions
/*
2. 상태별 처리 함수
각 상태에서 실행될 함수를 만듭니다.
*/
void state_idle(void) {
    printf("State: IDLE\n");
}
void state_run(void) {
    printf("State: RUN\n");
}
void state_stop(void) {
    printf("State: STOP\n");
}

// Define function pointer type for state handlers
/*
3. 함수 포인터 타입 및 FSM 구조체
*/
typedef void (*state_func_t)(void);


// FSM structure
/*
state_func_t는 상태 처리 함수의 포인터 타입입니다.
fsm_t는 현재 상태와 상태별 함수 포인터 배열을 가집니다.
*/
typedef struct {
    fsm_state_t state;
    state_func_t state_table[STATE_MAX];
} fsm_t;

// Initialize FSM
/*
4. FSM 초기화
각 상태에 맞는 함수 포인터를 배열에 할당합니다.
*/
void fsm_init(fsm_t *fsm) {
    fsm->state = STATE_IDLE;
    fsm->state_table[STATE_IDLE] = state_idle;
    fsm->state_table[STATE_RUN]  = state_run;
    fsm->state_table[STATE_STOP] = state_stop;
}

// Simulate FSM transitions based on input
/*
5. 상태 전이 및 실행
입력값에 따라 상태를 전이하고, 해당 상태의 함수를 호출합니다.
유효하지 않은 입력은 에러 메시지를 출력합니다.
*/
void fsm_update(fsm_t *fsm, int input) {
    // Example: 0 = idle, 1 = run, 2 = stop
    if (input >= 0 && input < STATE_MAX) {
        fsm->state = (fsm_state_t)input;
        fsm->state_table[fsm->state]();
    } else {
        printf("Invalid input: %d\n", input);
    }
}

// --- Test code ---
/*
6. 테스트 코드(main)

입력 시퀀스를 순서대로 FSM에 전달하여 상태 전이와 출력을 확인합니다.
*/
int main(void) {
    fsm_t fsm;
    fsm_init(&fsm);

    int input_sequence[] = {0, 1, 2, 1, 0, 2, 3};
    int len = sizeof(input_sequence) / sizeof(input_sequence[0]);

    printf("Input | State Output\n");
    printf("-------------------\n");
    for (int i = 0; i < len; ++i) {
        printf("  %d   | ", input_sequence[i]);
        fsm_update(&fsm, input_sequence[i]);
    }
    return 0;
}

/*
------------------ Example Input/Output ------------------

Input | State Output
-------------------
  0   | State: IDLE
  1   | State: RUN
  2   | State: STOP
  1   | State: RUN
  0   | State: IDLE
  2   | State: STOP
  3   | Invalid input: 3

----------------------------------------------------------
*/