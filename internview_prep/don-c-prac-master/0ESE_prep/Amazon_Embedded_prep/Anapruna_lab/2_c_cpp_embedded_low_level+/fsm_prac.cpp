
#include <iostream>
#include <cassert>
using namespace std;

// 복잡한 임베디드용 State Machine 예제
// 예: 장치가 IDLE → INIT → RUN → ERROR → (RESET) → IDLE 등 여러 상태를 가짐
enum State { IDLE, INIT, RUN, PAUSE, ERROR };

class DeviceStateMachine {
public:
    DeviceStateMachine();

    void event_power_on();    // IDLE → INIT
    void event_init_done();   // INIT → RUN
    void event_pause();       // RUN → PAUSE
    void event_resume();      // PAUSE → RUN
    void event_error();       // (어디서든) → ERROR
    void event_reset();       // ERROR → IDLE

    State get_state() const;

private:
    State state;
};

// TODO: 각 함수 구현

// 테스트 코드
int main() {
    DeviceStateMachine sm;
    assert(sm.get_state() == IDLE);

    sm.event_power_on();
    assert(sm.get_state() == INIT);

    sm.event_init_done();
    assert(sm.get_state() == RUN);

    sm.event_pause();
    assert(sm.get_state() == PAUSE);

    sm.event_resume();
    assert(sm.get_state() == RUN);

    sm.event_error();
    assert(sm.get_state() == ERROR);

    sm.event_reset();
    assert(sm.get_state() == IDLE);

    // ERROR에서 reset 없이 다른 이벤트는 무시되어야 함
    sm.event_power_on();
    sm.event_init_done();
    sm.event_error();
    assert(sm.get_state() == INIT); // power_on만 허용

    cout << "All tests PASS" << endl;
    return 0;
}

/*
문제 예시:
- 임베디드 장치의 상태 전이(State Machine)를 클래스로 구현하라.
- 상태: IDLE, INIT, RUN, PAUSE, ERROR
- 이벤트: power_on, init_done, pause, resume, error, reset
- 각 이벤트에 따라 상태가 올바르게 전이되도록 구현하라.
- main에서 상태 전이와 예외 상황을 테스트하는 코드를 작성하라.
*/