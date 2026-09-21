/*
100. **종합 문제:** 위에서 만든 `GpioPin`, `Timer`, `State Machine` 개념을 종합하여, 버튼을 누를 때마다 LED가 OFF -> ON -> BLINKING 상태로 순환하는 전체 시스템의 핵심 로직을 C++ 클래스들로 구조화하여 구현하세요.

*/

/*
100. **종합 문제:** 위에서 만든 `GpioPin`, `Timer`, `State Machine` 개념을 종합하여,
버튼을 누를 때마다 LED가 OFF -> ON -> BLINKING 상태로 순환하는 전체 시스템의 핵심 로직을
C++ 클래스들로 구조화하여 구현하세요.
*/

#include <iostream>
#include <chrono>
#include <thread>

// --- GPIO Pin abstraction ---
class GpioPin {
public:
    enum class Direction { In, Out };
    GpioPin(int pin) : pin_(pin), dir_(Direction::In), value_(false) {}
    void set_direction(Direction dir) { dir_ = dir; }
    void write(bool value) { if (dir_ == Direction::Out) value_ = value; }
    bool read() const { return value_; }
    void toggle() { if (dir_ == Direction::Out) value_ = !value_; }
    int pin_number() const { return pin_; }
private:
    int pin_;
    Direction dir_;
    bool value_;
};

// --- Timer abstraction (software simulation) ---
class Timer {
public:
    Timer() : running_(false), interval_ms_(0), last_tick_(std::chrono::steady_clock::now()) {}
    void start(uint32_t ms) {
        interval_ms_ = ms;
        running_ = true;
        last_tick_ = std::chrono::steady_clock::now();
    }
    void stop() { running_ = false; }
    bool expired() {
        if (!running_) return false;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick_).count();
        if (elapsed >= interval_ms_) {
            last_tick_ = now;
            return true;
        }
        return false;
    }
    bool running() const { return running_; }
private:
    bool running_;
    uint32_t interval_ms_;
    std::chrono::steady_clock::time_point last_tick_;
};

// --- LED State Machine ---
class LedState {
public:
    virtual ~LedState() = default;
    virtual void handle(GpioPin& led, Timer& blink_timer) = 0;
    virtual LedState* next() = 0;
    virtual const char* name() const = 0;
};

class LedOff : public LedState {
public:
    void handle(GpioPin& led, Timer&) override {
        led.write(false);
        std::cout << "[LED] OFF\n";
    }
    LedState* next() override { return new LedOn(); }
    const char* name() const override { return "OFF"; }
};

class LedOn : public LedState {
public:
    void handle(GpioPin& led, Timer&) override {
        led.write(true);
        std::cout << "[LED] ON\n";
    }
    LedState* next() override { return new LedBlinking(); }
    const char* name() const override { return "ON"; }
};

class LedBlinking : public LedState {
public:
    void handle(GpioPin& led, Timer& blink_timer) override {
        if (!blink_timer.running()) blink_timer.start(300);
        if (blink_timer.expired()) {
            led.toggle();
            std::cout << "[LED] BLINK: " << (led.read() ? "ON" : "OFF") << "\n";
        }
    }
    LedState* next() override { return new LedOff(); }
    const char* name() const override { return "BLINKING"; }
};

// --- Controller that ties everything together ---
class LedSystem {
public:
    LedSystem(GpioPin& led, GpioPin& button)
        : led_(led), button_(button), state_(new LedOff()), blink_timer_(), prev_button_state_(false)
    {
        led_.set_direction(GpioPin::Direction::Out);
        button_.set_direction(GpioPin::Direction::In);
    }
    ~LedSystem() { delete state_; }

    void poll(bool button_pressed) {
        // Simulate button input (in real system, button_.read())
        if (button_pressed && !prev_button_state_) {
            // Button rising edge: change state
            LedState* next_state = state_->next();
            std::cout << "[STATE] " << state_->name() << " -> " << next_state->name() << "\n";
            delete state_;
            state_ = next_state;
            // Reset blink timer if entering BLINKING
            if (std::string(state_->name()) == "BLINKING") {
                blink_timer_.start(300);
            } else {
                blink_timer_.stop();
            }
        }
        state_->handle(led_, blink_timer_);
        prev_button_state_ = button_pressed;
    }

    const char* current_state() const { return state_->name(); }
    bool led_output() const { return led_.read(); }
private:
    GpioPin& led_;
    GpioPin& button_;
    LedState* state_;
    Timer blink_timer_;
    bool prev_button_state_;
};

// --- Test code ---
int main() {
    std::cout << "=== 100. LED State Machine System Test ===\n";
    GpioPin led(1);
    GpioPin button(2);

    LedSystem system(led, button);

    // Simulate: button press sequence and time passing
    // OFF -> ON -> BLINKING -> OFF -> ...
    bool button_sequence[] = {
        false, false, true, true, false, // press (OFF->ON)
        false, false, true, false,       // press (ON->BLINKING)
        false, false, false, false,      // let it blink
        true, false,                     // press (BLINKING->OFF)
        false
    };

    int seq_len = sizeof(button_sequence)/sizeof(button_sequence[0]);
    for (int i = 0; i < seq_len; ++i) {
        std::cout << "[Loop " << i << "] Button: " << button_sequence[i] << " | State: " << system.current_state() << "\n";
        system.poll(button_sequence[i]);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "=== End of Test ===\n";
}
