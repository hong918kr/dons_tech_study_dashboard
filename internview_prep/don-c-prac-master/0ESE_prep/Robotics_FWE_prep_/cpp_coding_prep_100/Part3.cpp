/*

#### **Part 3: 하드웨어 추상화 및 드라이버 설계 (문제 36-60)**
*C++의 클래스, 상속, 가상 함수 등을 활용하여 하드웨어를 어떻게 구조적으로, 재사용 가능하게, 그리고 안전하게 감싸는지 평가합니다.*

36. **GPIO 드라이버 클래스:** 특정 GPIO 핀을 제어하는 `GpioPin` 클래스를 구현하세요. `set_direction`, `write`, `read`, `toggle` 메소드를 포함해야 합니다.
37. **UART 드라이버 클래스:** UART 주변장치를 위한 `Uart` 클래스를 구현하세요. `init`, `send_byte`, `receive_byte` 메소드를 포함해야 합니다. (Follow-up: 인터럽트 기반의 송수신 로직을 추가하세요.)
38. **RAII 기반 주변장치 전원 관리:** 생성자에서 특정 주변장치의 클럭을 켜고(enable), 소멸자에서 클럭을 끄는(disable) RAII 래퍼(wrapper) 클래스를 구현하세요.
39. **추상 센서 인터페이스:** `read()`라는 순수 가상 함수를 가진 `ISensor` 추상 기반 클래스를 정의하세요. 그리고 이를 상속받아 `TemperatureSensor`와 `HumiditySensor` 클래스를 구현하세요.
40. **SPI 드라이버 클래스:** SPI 통신을 위한 `Spi` 클래스를 구현하세요. `transfer` 메소드를 포함하고, 칩 셀렉트(CS) 핀 관리를 어떻게 할지 설명하세요.
41. **ADC 드라이버 클래스:** ADC 채널에서 아날로그 값을 읽어오는 `AdcChannel` 클래스를 구현하세요. (Follow-up: 여러 채널을 순차적으로 스캔하는 기능을 추가하세요.)
42. **PWM 드라이버 클래스:** 모터나 LED 밝기 제어를 위한 PWM 신호를 생성하는 `PwmChannel` 클래스를 구현하세요. `set_duty_cycle`, `set_frequency` 메소드를 포함해야 합니다.
43. **I2C 디바이스 드라이버:** 특정 I2C 센서(예: MPU-6050)의 데이터시트를 보고, 해당 센서의 특정 레지스터를 읽고 쓰는 메소드를 가진 클래스를 구현하세요.
44. **인터럽트 핸들러 래퍼:** C 스타일의 인터럽트 벡터 테이블과 C++ 멤버 함수(콜백)를 안전하게 연결하는 래퍼 클래스를 구현하세요. (Hint: `static` 멤버 함수와 `this` 포인터 사용)
45. **워치독 타이머 드라이버:** 워치독 타이머를 초기화하고 주기적으로 "먹이를 주는(feed)" `Watchdog` 클래스를 구현하세요.
46. **DMA 전송 관리자:** 메모리-메모리 간 DMA 전송을 설정하고 시작하는 `DmaManager` 클래스를 구현하세요. 전송 완료 시 콜백 함수를 호출하는 기능을 포함하세요.
47. **CAN 메시지 송수신:** CAN 버스로 특정 ID를 가진 메시지를 보내고 받는 `CanBus` 클래스의 일부를 구현하세요.
48. **플래시 메모리 드라이버:** 내부 플래시 메모리의 특정 섹터를 지우고(erase) 쓰는(write) `Flash` 클래스를 구현하세요.
49. **하드웨어 레지스터 맵 구조체:** 데이터시트를 보고 특정 주변장치(예: 타이머)의 모든 레지스터를 `volatile` 멤버로 포함하는 `struct`를 정의하세요.
50. **템플릿 기반 GPIO 드라이버:** 포트(PORTA, PORTB)와 핀 번호를 템플릿 인자로 받아, 컴파일 타임에 최적화된 GPIO 제어 코드를 생성하는 클래스를 구현하세요.
51. **배터리 관리 시스템(BMS) 인터페이스:** 배터리 전압, 전류, 온도를 읽어오는 `Bms` 클래스의 인터페이스를 설계하고 `get_voltage()` 메소드를 구현하세요.
52. **모터 제어기 인터페이스:** `set_velocity`, `get_position`, `enable`, `disable` 등의 메소드를 가진 모터 제어기 클래스의 인터페이스를 설계하세요.
53. **RTOS 뮤텍스 래퍼:** RTOS가 제공하는 C API(예: `xSemaphoreTake`)를 RAII 패턴을 따르는 C++ `MutexGuard` 클래스로 감싸세요.
54. **핀 멀티플렉싱(Pin Muxing) 설정:** 특정 핀을 UART TX 기능으로 설정하거나, GPIO 기능으로 설정하는 함수를 구현하세요.
55. **저전력 모드 진입/탈출:** MCU를 슬립(sleep) 모드로 전환시키고, 특정 인터럽트(예: 버튼 입력)로 깨어나는 함수를 구현하세요.
56. **타이머/카운터 클래스:** 특정 하드웨어 타이머를 설정하여 1ms마다 인터럽트를 발생시키는 `Timer` 클래스를 구현하세요.
57. **옵저버(Observer) 패턴:** 버튼 상태가 변경될 때마다 등록된 여러 객체에게 알림을 보내는 간단한 옵저버 패턴을 구현하세요.
58. **팩토리(Factory) 패턴:** 센서 종류를 나타내는 ID를 받아, 해당 ID에 맞는 센서 객체(`TemperatureSensor` 등)를 생성하여 반환하는 팩토리 함수를 구현하세요.
59. **싱글톤(Singleton) 패턴:** 시스템 전체에서 유일해야 하는 로거(Logger) 클래스를 싱글톤 패턴으로 구현하세요.
60. **전략(Strategy) 패턴:** 모터 제어 알고리즘(예: PID, Bang-bang)을 쉽게 교체할 수 있도록 전략 패턴을 사용하여 `MotorController`를 설계하세요.

*/

/*
#### Part 3: 하드웨어 추상화 및 드라이버 설계 (문제 36-60)
*C++의 클래스, 상속, 가상 함수 등을 활용하여 하드웨어를 어떻게 구조적으로, 재사용 가능하게, 그리고 안전하게 감싸는지 평가합니다.*
*/

#include <iostream>
#include <cstdint>
#include <string>
#include <cstring>

// 36. GPIO 드라이버 클래스
class GpioPin {
public:
    enum class Direction { In, Out };
    GpioPin(int pin) : pin_(pin), dir_(Direction::In), value_(false) {}
    void set_direction(Direction dir) { dir_ = dir; }
    void write(bool value) { if (dir_ == Direction::Out) value_ = value; }
    bool read() const { return value_; }
    void toggle() { if (dir_ == Direction::Out) value_ = !value_; }
private:
    int pin_;
    Direction dir_;
    bool value_;
};

// 37. UART 드라이버 클래스
class Uart {
public:
    void init(uint32_t baudrate) { baud_ = baudrate; }
    void send_byte(uint8_t byte) { last_sent_ = byte; }
    uint8_t receive_byte() { return last_sent_; }
private:
    uint32_t baud_ = 0;
    uint8_t last_sent_ = 0;
};

// 38. RAII 기반 주변장치 전원 관리
class PeripheralClockGuard {
public:
    PeripheralClockGuard(const std::string& name) : name_(name) {
        std::cout << "[Clock] " << name_ << " enabled\n";
    }
    ~PeripheralClockGuard() {
        std::cout << "[Clock] " << name_ << " disabled\n";
    }
private:
    std::string name_;
};

// 39. 추상 센서 인터페이스 및 구현
class ISensor {
public:
    virtual ~ISensor() = default;
    virtual float read() = 0;
};
class TemperatureSensor : public ISensor {
public:
    float read() override { return 25.0f; }
};
class HumiditySensor : public ISensor {
public:
    float read() override { return 50.0f; }
};

// 40. SPI 드라이버 클래스
class Spi {
public:
    void transfer(uint8_t* tx, uint8_t* rx, size_t len) {
        for (size_t i = 0; i < len; ++i) rx[i] = tx[i]; // loopback for test
    }
    // 칩 셀렉트(CS) 핀은 별도 GpioPin 객체로 관리하는 것이 일반적입니다.
};

// 41. ADC 드라이버 클래스
class AdcChannel {
public:
    AdcChannel(int ch) : channel_(ch) {}
    uint16_t read() { return 1234 + channel_; }
private:
    int channel_;
};

// 42. PWM 드라이버 클래스
class PwmChannel {
public:
    void set_duty_cycle(float duty) { duty_ = duty; }
    void set_frequency(uint32_t freq) { freq_ = freq; }
    float get_duty_cycle() const { return duty_; }
    uint32_t get_frequency() const { return freq_; }
private:
    float duty_ = 0.0f;
    uint32_t freq_ = 0;
};

// 43. I2C 디바이스 드라이버 (예: MPU-6050)
class Mpu6050 {
public:
    uint8_t read_reg(uint8_t reg) { return reg + 1; }
    void write_reg(uint8_t reg, uint8_t val) { /* ... */ }
};

// 44. 인터럽트 핸들러 래퍼
class InterruptHandlerWrapper {
public:
    static void isr_entry(void* ctx) {
        if (ctx) static_cast<InterruptHandlerWrapper*>(ctx)->on_interrupt();
    }
    void on_interrupt() { triggered = true; }
    bool triggered = false;
};

// 45. 워치독 타이머 드라이버
class Watchdog {
public:
    void init(uint32_t timeout_ms) { timeout_ = timeout_ms; fed_ = true; }
    void feed() { fed_ = true; }
    bool is_fed() const { return fed_; }
private:
    uint32_t timeout_ = 0;
    bool fed_ = false;
};

// 46. DMA 전송 관리자
/*
좋은 질문입니다!

1. 왜 memcpy를 썼나?
    - DMA(Direct Memory Access) 전송은 메모리 블록을 한 번에 복사하는 작업이기 때문에,
    C/C++에서 가장 기본적이고 빠른 메모리 복사 함수인 memcpy를 사용한 것입니다.

    - 실제 임베디드 환경에서는 DMA 하드웨어가 직접 복사를 수행하지만,
    시뮬레이션이나 테스트 코드에서는 memcpy로 그 동작을 흉내낼 수 있습니다.

2. C++ 표준에서 더 좋은 방법은 없나?
    - C++17부터는 std::copy (또는 std::copy_n)를 사용할 수 있습니다.
    - std::copy는 타입 안전(type-safe)하고, 포인터뿐 아니라 이터레이터도 지원하며,
    범위 기반 컨테이너에도 사용할 수 있습니다.


예시 (std::copy 사용)

#include <algorithm> // std::copy
#include <cstdint>

void start_transfer(const uint8_t* src, uint8_t* dst, size_t len) {
    std::copy(src, src + len, dst);
}

단, **POD 타입(Plain Old Data)**이나 단순 배열에만 적합합니다.
만약 복사 대상이 클래스 객체라면, 복사 생성자가 호출됩니다.


결론
    - POD 타입(단순 데이터) 복사: std::copy 또는 std::copy_n이 C++스럽고 안전합니다.
    - raw memory 복사: 여전히 memcpy가 빠르고, 임베디드/저수준에서는 표준적입니다.
    - 컨테이너/이터레이터: std::copy가 더 범용적이고 C++ 스타일입니다.
예시: C++ 스타일 DMA 매니저


#include <algorithm>
class DmaManager {
public:
    template<typename T>
    void start_transfer(const T* src, T* dst, size_t len, void (*callback)()) {
        std::copy(src, src + len, dst);
        if (callback) callback();
    }
};

정리:

    C++에서는 std::copy가 더 현대적이고 타입 안전한 방법입니다.
    하지만 임베디드/로우레벨에서는 여전히 memcpy가 널리 쓰입니다.
    상황에 따라 적절한 방법을 선택하세요!

*/
class DmaManager {
public:
    void start_transfer(const void* src, void* dst, size_t len, void (*callback)()) {
        std::memcpy(dst, src, len);
        if (callback) callback();
    }
};

// 47. CAN 메시지 송수신
class CanBus {
public:
    void send(uint32_t id, const uint8_t* data, size_t len) {
        last_id_ = id;
        last_data_ = data[0];
    }
    uint8_t receive(uint32_t& id) {
        id = last_id_;
        return last_data_;
    }
private:
    uint32_t last_id_ = 0;
    uint8_t last_data_ = 0;
};

// 48. 플래시 메모리 드라이버
class Flash {
public:
    void erase_sector(uint32_t addr) { erased_addr_ = addr; }
    void write(uint32_t addr, uint32_t data) { last_write_addr_ = addr; last_write_data_ = data; }
    uint32_t last_write_addr_ = 0, last_write_data_ = 0, erased_addr_ = 0;
};

// 49. 하드웨어 레지스터 맵 구조체
struct TimerRegs {
    volatile uint32_t CR;
    volatile uint32_t SR;
    volatile uint32_t CNT;
    volatile uint32_t PSC;
};

// 50. 템플릿 기반 GPIO 드라이버
template<char PORT, int PIN>
class GpioTemplate {
public:
    static void set() { std::cout << "Set PORT" << PORT << " PIN" << PIN << "\n"; }
    static void clear() { std::cout << "Clear PORT" << PORT << " PIN" << PIN << "\n"; }
};

// 51. 배터리 관리 시스템(BMS) 인터페이스
class Bms {
public:
    float get_voltage() const { return 3.7f; }
    float get_current() const { return 1.2f; }
    float get_temperature() const { return 25.0f; }
};

// 52. 모터 제어기 인터페이스
class MotorController {
public:
    void set_velocity(float v) { velocity_ = v; }
    float get_position() const { return position_; }
    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; }
private:
    float velocity_ = 0.0f;
    float position_ = 0.0f;
    bool enabled_ = false;
};

// 53. RTOS 뮤텍스 래퍼 (RAII)
class MutexGuard {
public:
    MutexGuard() { locked = true; }
    ~MutexGuard() { locked = false; }
    bool locked = false;
};

// 54. 핀 멀티플렉싱(Pin Muxing) 설정
void set_pin_function(int pin, const std::string& func) {
    std::cout << "Pin " << pin << " set to function: " << func << "\n";
}

// 55. 저전력 모드 진입/탈출
void enter_sleep_mode() { std::cout << "MCU entering sleep mode...\n"; }
void wakeup_from_interrupt() { std::cout << "MCU woke up from interrupt!\n"; }

// 56. 타이머/카운터 클래스
class Timer {
public:
    void start(uint32_t ms) { running_ = true; interval_ = ms; }
    void stop() { running_ = false; }
    bool running() const { return running_; }
private:
    bool running_ = false;
    uint32_t interval_ = 0;
};

// 57. 옵저버(Observer) 패턴
class ButtonObserver {
public:
    virtual void on_button_changed(bool state) = 0;
};
class Button {
public:
    void add_observer(ButtonObserver* obs) { observer_ = obs; }
    void set_state(bool state) {
        if (observer_) observer_->on_button_changed(state);
    }
private:
    ButtonObserver* observer_ = nullptr;
};
class Led : public ButtonObserver {
public:
    void on_button_changed(bool state) override {
        std::cout << "LED " << (state ? "ON" : "OFF") << "\n";
    }
};

// 58. 팩토리(Factory) 패턴
ISensor* create_sensor(int id) {
    if (id == 0) return new TemperatureSensor();
    else if (id == 1) return new HumiditySensor();
    else return nullptr;
}

// 59. 싱글톤(Logger) 패턴
class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }
    void log(const std::string& msg) { std::cout << "[LOG] " << msg << "\n"; }
private:
    Logger() = default;
};

// 60. 전략(Strategy) 패턴
class MotorStrategy {
public:
    virtual ~MotorStrategy() = default;
    virtual void control(MotorController& m) = 0;
};
class PidStrategy : public MotorStrategy {
public:
    void control(MotorController& m) override { std::cout << "PID control\n"; }
};
class BangBangStrategy : public MotorStrategy {
public:
    void control(MotorController& m) override { std::cout << "Bang-bang control\n"; }
};
class MotorControllerWithStrategy : public MotorController {
public:
    void set_strategy(MotorStrategy* s) { strategy_ = s; }
    void run_control() { if (strategy_) strategy_->control(*this); }
private:
    MotorStrategy* strategy_ = nullptr;
};

// --- Test code ---
int main() {
    std::cout << "=== Part3 Hardware Abstraction Test ===\n";

    // 36. GPIO
    GpioPin pin(5);
    pin.set_direction(GpioPin::Direction::Out);
    pin.write(true);
    std::cout << "36. GpioPin read: " << pin.read() << "\n";
    pin.toggle();
    std::cout << "36. GpioPin toggled: " << pin.read() << "\n";

    // 37. UART
    Uart uart;
    uart.init(115200);
    uart.send_byte(0xAB);
    std::cout << "37. Uart received: 0x" << std::hex << (int)uart.receive_byte() << std::dec << "\n";

    // 38. RAII PeripheralClockGuard
    {
        PeripheralClockGuard clk("UART1");
    }

    // 39. Sensor Interface
    TemperatureSensor ts;
    HumiditySensor hs;
    std::cout << "39. Temp: " << ts.read() << ", Hum: " << hs.read() << "\n";

    // 40. SPI
    Spi spi;
    uint8_t tx[3] = {1,2,3}, rx[3] = {0};
    spi.transfer(tx, rx, 3);
    std::cout << "40. SPI rx: " << (int)rx[0] << "," << (int)rx[1] << "," << (int)rx[2] << "\n";

    // 41. ADC
    AdcChannel adc(2);
    std::cout << "41. ADC read: " << adc.read() << "\n";

    // 42. PWM
    PwmChannel pwm;
    pwm.set_duty_cycle(0.5f);
    pwm.set_frequency(1000);
    std::cout << "42. PWM duty: " << pwm.get_duty_cycle() << ", freq: " << pwm.get_frequency() << "\n";

    // 43. I2C (MPU6050)
    Mpu6050 mpu;
    std::cout << "43. MPU6050 reg 0x10: " << (int)mpu.read_reg(0x10) << "\n";

    // 44. Interrupt Handler Wrapper
    InterruptHandlerWrapper ihw;
    InterruptHandlerWrapper::isr_entry(&ihw);
    std::cout << "44. Interrupt triggered: " << ihw.triggered << "\n";

    // 45. Watchdog
    Watchdog wdog;
    wdog.init(1000);
    wdog.feed();
    std::cout << "45. Watchdog fed: " << wdog.is_fed() << "\n";

    // 46. DMA Manager
    int src[2] = {1,2}, dst[2] = {0};
    DmaManager dma;
    dma.start_transfer(src, dst, sizeof(src), [](){ std::cout << "46. DMA done!\n"; });
    std::cout << "46. DMA dst: " << dst[0] << "," << dst[1] << "\n";

    // 47. CAN Bus
    CanBus can;
    uint8_t can_data[1] = {0x55};
    can.send(0x123, can_data, 1);
    uint32_t can_id = 0;
    std::cout << "47. CAN received: id=0x" << std::hex << can.receive(can_id) << ", id=" << can_id << std::dec << "\n";

    // 48. Flash
    Flash flash;
    flash.erase_sector(0x1000);
    flash.write(0x1000, 0xDEADBEEF);
    std::cout << "48. Flash last write addr: 0x" << std::hex << flash.last_write_addr_ << ", data: 0x" << flash.last_write_data_ << std::dec << "\n";

    // 49. TimerRegs struct
    TimerRegs tmr = {0};
    tmr.CR = 1; tmr.SR = 2; tmr.CNT = 3; tmr.PSC = 4;
    std::cout << "49. TimerRegs: CR=" << tmr.CR << ", SR=" << tmr.SR << ", CNT=" << tmr.CNT << ", PSC=" << tmr.PSC << "\n";

    // 50. Template GPIO
    GpioTemplate<'A', 3>::set();
    GpioTemplate<'B', 7>::clear();

    // 51. BMS
    Bms bms;
    std::cout << "51. BMS voltage: " << bms.get_voltage() << "\n";

    // 52. MotorController
    MotorController motor;
    motor.set_velocity(100.0f);
    motor.enable();
    std::cout << "52. Motor enabled, position: " << motor.get_position() << "\n";

    // 53. MutexGuard
    {
        MutexGuard mtx;
        std::cout << "53. Mutex locked: " << mtx.locked << "\n";
    }

    // 54. Pin Muxing
    set_pin_function(5, "UART_TX");

    // 55. Low Power Mode
    enter_sleep_mode();
    wakeup_from_interrupt();

    // 56. Timer
    Timer timer;
    timer.start(1);
    std::cout << "56. Timer running: " << timer.running() << "\n";
    timer.stop();

    // 57. Observer Pattern
    Button btn;
    Led led;
    btn.add_observer(&led);
    btn.set_state(true);
    btn.set_state(false);

    // 58. Factory Pattern
    ISensor* s = create_sensor(0);
    if (s) std::cout << "58. Factory sensor read: " << s->read() << "\n";
    delete s;

    // 59. Singleton Logger
    Logger::instance().log("Hello Singleton Logger!");

    // 60. Strategy Pattern
    MotorControllerWithStrategy mcs;
    PidStrategy pid;
    BangBangStrategy bb;
    mcs.set_strategy(&pid);
    mcs.run_control();
    mcs.set_strategy(&bb);
    mcs.run_control();

    return 0;
}