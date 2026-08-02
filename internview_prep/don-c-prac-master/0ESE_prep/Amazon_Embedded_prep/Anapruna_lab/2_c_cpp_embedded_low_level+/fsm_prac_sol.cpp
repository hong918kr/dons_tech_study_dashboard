#include <iostream>
#include <cassert>
using namespace std;

// 복잡한 임베디드용 State Machine 예제
enum State { IDLE, INIT, RUN, PAUSE, ERROR };

class DeviceStateMachine {
public:
    DeviceStateMachine() : state(IDLE) {}

    void event_power_on() {
        if (state == IDLE) state = INIT;
        // ERROR 상태에서만 reset만 허용, power_on은 무시
        else if (state == ERROR) state = INIT;
    }

    void event_init_done() {
        if (state == INIT) state = RUN;
    }

    void event_pause() {
        if (state == RUN) state = PAUSE;
    }

    void event_resume() {
        if (state == PAUSE) state = RUN;
    }

    void event_error() {
        if (state != ERROR) state = ERROR;
    }

    void event_reset() {
        if (state == ERROR) state = IDLE;
    }

    State get_state() const {
        return state;
    }

private:
    State state;
};

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