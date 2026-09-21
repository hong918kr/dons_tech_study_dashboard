#include <stdio.h>

// 1. 함수 포인터 콜백 (Callback)
// 설명: LED를 켜거나 끄는 함수를 콜백으로 받아 동작시키는 함수


typedef void (*led_cb_t) (void);

void led_action(led_cb_t cb) {
    // TODO: implement
    if(cb!=NULL) cb();
}

// 테스트용 LED 상태 변수
static int led_state = 0;
void led_on(void)  { led_state = 1; }
void led_off(void) { led_state = 0; }

int test_led_action() {
    led_state = 0;
    led_action(led_on);
    if (led_state != 1) { printf("1-1 FAIL\n"); return 0; }
    led_action(led_off);
    if (led_state != 0) { printf("1-2 FAIL\n"); return 0; }
    printf("1 PASS\n");
    return 1;
}

// 2. 테이블 기반 분기 (Function Pointer Table)
// 설명: 명령 번호(0~2)에 따라 다른 동작을 수행하는 함수 포인터 테이블
typedef void (*cmd_func_t)(void);

static int cmd_log[3] = {0, 0, 0};
void cmd0(void) { cmd_log[0]++; }
void cmd1(void) { cmd_log[1]++; }
void cmd2(void) { cmd_log[2]++; }

void exec_command(int cmd_id) {
    // TODO: implement
    static cmd_func_t table[3] = {cmd0, cmd1, cmd2};
    if (cmd_id >=0 && cmd_id <3)
    {
        table[cmd_id]();
    }
}

int test_exec_command() {
    cmd_log[0] = cmd_log[1] = cmd_log[2] = 0;
    exec_command(0);
    exec_command(1);
    exec_command(2);
    exec_command(1);
    if (cmd_log[0] != 1) { printf("2-1 FAIL\n"); return 0; }
    if (cmd_log[1] != 2) { printf("2-2 FAIL\n"); return 0; }
    if (cmd_log[2] != 1) { printf("2-3 FAIL\n"); return 0; }
    printf("2 PASS\n");
    return 1;
}

// 3. 이벤트 핸들러 (Event Handler)
// 설명: 이벤트 타입에 따라 등록된 핸들러를 호출하는 구조
typedef void (*event_handler_t)(int event_data);

#define MAX_EVENT 2
static event_handler_t handlers[MAX_EVENT] = {0, 0};
static int event_log[MAX_EVENT] = {0, 0};

void register_event_handler(int event_type, event_handler_t handler) {
    // TODO: implement
    if (event_type >= 0 && event_type < MAX_EVENT)
    {
        handlers[event_type] = handler;
    }
}
void handle_event(int event_type, int event_data) {
    // TODO: implement
    if (event_type >= 0 && event_type < MAX_EVENT)
    {
        handlers[event_type](event_data);
    }

}

// 테스트용 이벤트 핸들러
void on_button(int data) { event_log[0] = data; }
void on_sensor(int data) { event_log[1] = data; }

int test_event_handler() {
    event_log[0] = event_log[1] = 0;
    register_event_handler(0, on_button);
    register_event_handler(1, on_sensor);
    handle_event(0, 123);
    handle_event(1, 456);
    if (event_log[0] != 123) { printf("3-1 FAIL\n"); return 0; }
    if (event_log[1] != 456) { printf("3-2 FAIL\n"); return 0; }
    printf("3 PASS\n");
    return 1;
}

int main() {
    int total = 0;
    total += test_led_action();
    total += test_exec_command();
    total += test_event_handler();
    printf("Total Passed: %d/3\n", total);
    return 0;
}