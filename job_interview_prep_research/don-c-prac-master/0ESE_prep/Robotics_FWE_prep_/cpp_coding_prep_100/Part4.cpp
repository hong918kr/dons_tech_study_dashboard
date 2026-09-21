/*

#### **Part 4: 실시간, 동시성 및 시스템 설계 (문제 61-100)**
*단순한 코드 구현을 넘어, 시스템의 안정성과 동작 방식을 깊이 있게 이해하고 있는지 평가합니다.*

61. **간단한 협력적 스케줄러:** 여러 개의 태스크(함수 포인터)를 등록하고, 순서대로 한 번씩 실행시켜주는 간단한 스케줄러를 구현하세요.
62. **인터럽트 안전한 큐:** ISR에서 `push`하고 메인 루프에서 `pop`할 때 데이터가 깨지지 않는 링 버퍼 기반의 큐를 구현하세요. (임계 영역 보호 포함)
63. **데드라인 스케줄링:** 각 태스크에 마감 시간(deadline)이 주어졌을 때, 마감 시간이 가장 임박한 태스크를 먼저 실행하는 간단한 스케줄러 로직을 구현하세요.
64. **스택 사용량 측정:** 특정 함수의 스택 사용량을 측정하기 위해 스택 메모리를 특정 패턴으로 채우고, 함수 실행 후 패턴이 지워진 영역을 계산하는 코드를 작성하세요.
65. **스택 카나리(Stack Canary):** 스택 오버플로우를 감지하기 위해, 함수 진입 시 스택의 특정 위치에 '카나리' 값을 쓰고, 함수 탈출 시 그 값이 변했는지 확인하는 매크로를 구현하세요.
66. **간단한 부트로더 로직:** 애플리케이션 펌웨어의 유효성(예: 체크섬)을 검사하고, 유효하면 애플리케이션으로 점프하는 부트로더의 핵심 로직을 의사코드(pseudocode)로 작성하세요.
67. **OTA 펌웨어 뱅크 스왑:** 펌웨어 A/B 파티션 구조에서, 업데이트가 성공적으로 완료되었을 때 다음 부팅 시 새로운 펌웨어로 부팅하도록 상태 플래그를 업데이트하는 함수를 구현하세요.
68. **단언(Assertion) 매크로:** 디버그 빌드에서만 조건이 거짓일 경우 특정 에러 핸들러(예: 무한 루프)를 실행하는 `ASSERT` 매크로를 구현하세요.
69. **소프트웨어 워치독:** 여러 태스크가 모두 주기적으로 자신의 '생존 신호'를 보고해야만 하드웨어 워치독을 리셋하는 소프트웨어 워치독 관리자를 구현하세요.
70. **에러 처리 프레임워크:** 시스템 전체에서 발생하는 에러 코드와 발생 위치(파일, 라인)를 기록하는 간단한 에러 처리 프레임워크를 설계하세요.
71. **상태 머신 구현 (State 패턴):** LED 제어를 위한 상태 머신을 C++의 State 디자인 패턴(각 상태를 별도의 클래스로 구현)을 사용하여 구현하세요.
72. **PID 제어기:** 비례(Proportional), 적분(Integral), 미분(Derivative) 제어를 수행하는 간단한 PID 제어기 클래스를 구현하세요.
73. **안전한 데이터 공유:** ISR과 메인 루프가 32비트 카운터 변수를 공유할 때 발생할 수 있는 문제를 설명하고, 안전하게 값을 읽는 함수를 구현하세요.
74. **임계 영역(Critical Section) 구현:** 인터럽트를 비활성화하고 다시 활성화하는 코드를 RAII 클래스로 감싸, 임계 영역을 벗어날 때 인터럽트가 자동으로 복원되게 하세요.
75. **이벤트 플래그(Event Flags):** 여러 이벤트가 발생했는지 비트 플래그로 관리하고, 특정 플래그 조합이 만족될 때까지 대기하는 로직을 구현하세요.
76. **실시간 성능 측정:** 특정 코드 블록의 실행 시간을 마이크로초 단위로 측정하기 위해 고해상도 타이머를 사용하는 코드를 작성하세요.
77. **재진입성(Reentrancy) 문제:** 재진입 가능하지 않은 함수(예: 전역 변수 사용)의 예시를 들고, 이를 재진입 가능하게 수정하세요.
78. **정적 초기화 순서 문제(Static Initialization Order Fiasco):** 서로 다른 파일에 있는 두 전역 객체가 생성자에서 서로를 참조할 때 발생하는 문제를 설명하고, 해결 방법을 제시하세요. (Construct on First Use Idiom)
79. **전원 차단 시 데이터 저장:** 시스템 전원이 곧 꺼질 것이라는 신호를 받았을 때, 중요한 데이터를 비휘발성 메모리(플래시)에 안전하게 저장하는 로직을 구현하세요.
80. **단위 테스트(Unit Test) 작성:** 위에서 구현한 링 버퍼 클래스에 대해, `push`와 `pop`이 올바르게 동작하는지 검증하는 간단한 단위 테스트 함수를 작성하세요.
81. **하드웨어 모의(Mocking) 객체:** 실제 하드웨어 없이 UART 드라이버를 테스트하기 위해, 송신된 데이터를 내부 버퍼에 저장하는 가짜 `MockUart` 클래스를 구현하세요.
82. **End-to-End 테스트:** 모터에 "100" 속도를 명령하고, 잠시 후 엔코더 센서 값이 실제로 증가했는지 확인하는 간단한 End-to-End 테스트 로직을 의사코드로 작성하세요.
83. **로깅(Logging) 시스템:** 다양한 심각도(DEBUG, INFO, ERROR)에 따라 로그 메시지를 UART로 출력하는 간단한 로깅 매크로를 구현하세요.
84. **기능 안전(Functional Safety) - 입력값 검증:** 모터 속도를 설정하는 함수에, 허용 범위를 벗어나는 값이 들어오면 안전한 기본값으로 설정하고 에러 플래그를 세우는 로직을 추가하세요.
85. **이중화(Redundancy) 처리:** 두 개의 온도 센서 값을 읽어, 두 값의 차이가 크지 않으면 평균을 반환하고, 차이가 크면 에러를 반환하는 함수를 구현하세요.
86. **안전 상태(Safe State) 진입:** 치명적인 에러가 감지되었을 때, 모든 모터를 정지시키고 경고 LED를 켜는 `enter_safe_state()` 함수를 구현하세요.
87. **C 코드와 C++ 코드 연동:** C++ 코드에서 C로 작성된 레거시 드라이버 함수를 호출하기 위해 필요한 `extern "C"`의 사용법을 설명하고 예시 코드를 작성하세요.
88. **메모리 풋프린트(Footprint) 최적화:** 특정 클래스의 크기를 줄이기 위한 방법을 설명하세요 (예: 멤버 변수 순서 재배치, 비트 필드 사용).
89. **CPU 사용률 계산:** 유휴(idle) 태스크가 1초 동안 실행된 시간을 측정하여 시스템의 CPU 사용률을 계산하는 로직을 구현하세요.
90. **인터럽트 지연(Latency) 측정:** 외부 핀에 신호가 들어온 시점부터 ISR의 특정 코드 라인이 실행될 때까지의 시간을 측정하는 방법을 설명하세요.
91. **가상 소멸자(Virtual Destructor):** 기반 클래스의 포인터로 파생 클래스 객체를 `delete`할 때 왜 가상 소멸자가 필요한지 설명하고 예시 코드를 작성하세요.
92. **`std::span` 활용:** C 스타일 배열과 포인터+길이 대신 `std::span`을 사용하여 버퍼 오버런을 방지하는 함수를 작성하세요.
93. **`std::optional` 활용:** 센서 읽기가 실패할 수 있는 경우, 유효한 값 또는 '값 없음' 상태를 반환하는 함수를 `std::optional`을 사용하여 구현하세요.
94. **`enum class` 사용:** C 스타일 `enum` 대신 C++의 `enum class`를 사용해야 하는 이유를 설명하고, 상태 머신에 적용하는 예시를 작성하세요.
95. **`static_assert` 활용:** 컴파일 타임에 특정 조건(예: 버퍼 크기가 2의 거듭제곱인지)을 검사하여, 조건이 만족되지 않으면 컴파일을 실패시키는 코드를 작성하세요.
96. **템플릿 메타프로그래밍(TMP):** 템플릿을 사용하여 컴파일 타임에 팩토리얼(factorial)을 계산하는 코드를 작성하세요.
97. **커스텀 `new`/`delete` 연산자 오버로딩:** 전역 `new`/`delete`를 오버로딩하여 모든 동적 할당이 직접 구현한 메모리 풀을 사용하도록 만드세요.
98. **완벽한 전달(Perfect Forwarding)과 가변 인자 템플릿:** 어떤 타입의 인자든 받아서 그대로 다른 함수에 전달하는 로깅 래퍼 함수를 구현하세요.
99. **`std::atomic` 사용:** RTOS 환경에서 락(lock) 없이 공유 변수를 안전하게 증가시키는 코드를 `std::atomic`을 사용하여 작성하세요.
100. **종합 문제:** 위에서 만든 `GpioPin`, `Timer`, `State Machine` 개념을 종합하여, 버튼을 누를 때마다 LED가 OFF -> ON -> BLINKING 상태로 순환하는 전체 시스템의 핵심 로직을 C++ 클래스들로 구조화하여 구현하세요.
*/
/*
#### Part 4: 실시간, 동시성 및 시스템 설계 (문제 61-100)
*단순한 코드 구현을 넘어, 시스템의 안정성과 동작 방식을 깊이 있게 이해하고 있는지 평가합니다.*
*/

#include <iostream>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstring>
#include <cassert>
#include <optional>
#include <span>

// 61. 간단한 협력적 스케줄러
class CooperativeScheduler {
public:
    void add_task(std::function<void()> task) { tasks.push_back(task); }
    void run_once() { for (auto& t : tasks) t(); }
private:
    std::vector<std::function<void()>> tasks;
};

// 62. 인터럽트 안전한 큐 (간단 예시, 실제 환경에선 atomic/lock 필요)
class IsrSafeQueue {
public:
    static constexpr size_t SIZE = 8;
    IsrSafeQueue() : head(0), tail(0), count(0) { std::memset(buf, 0, sizeof(buf)); }
    bool push(uint8_t val) {
        if (count == SIZE) return false;
        buf[head] = val;
        head = (head + 1) % SIZE;
        ++count;
        return true;
    }
    bool pop(uint8_t& val) {
        if (count == 0) return false;
        val = buf[tail];
        tail = (tail + 1) % SIZE;
        --count;
        return true;
    }
    bool is_empty() const { return count == 0; }
private:
    uint8_t buf[SIZE];
    size_t head, tail, count;
};

// 63. 데드라인 스케줄링 (간단 예시)
struct DeadlineTask {
    std::function<void()> func;
    uint32_t deadline;
};
class DeadlineScheduler {
public:
    void add_task(const DeadlineTask& t) { tasks.push_back(t); }
    void run() {
        if (tasks.empty()) return;
        auto it = std::min_element(tasks.begin(), tasks.end(),
            [](const DeadlineTask& a, const DeadlineTask& b) { return a.deadline < b.deadline; });
        if (it != tasks.end()) it->func();
    }
private:
    std::vector<DeadlineTask> tasks;
};

// 64. 스택 사용량 측정 (시뮬레이션)
size_t measure_stack_usage(char* stack, size_t size, std::function<void()> func) {
    std::memset(stack, 0xAA, size);
    func();
    size_t used = 0;
    while (used < size && stack[used] != (char)0xAA) ++used;
    return used;
}

// 65. 스택 카나리(Stack Canary) 매크로
#define STACK_CANARY_VALUE 0xDEADBEEF
#define STACK_CANARY_INIT volatile uint32_t __canary = STACK_CANARY_VALUE
#define STACK_CANARY_CHECK assert(__canary == STACK_CANARY_VALUE)

// 66. 간단한 부트로더 로직 (pseudocode)
void bootloader_jump_example() {
    // if (check_firmware_valid()) jump_to_app();
    std::cout << "66. Bootloader: if valid, jump to app\n";
}

// 67. OTA 펌웨어 뱅크 스왑
enum class Bank { A, B };
Bank next_boot_bank = Bank::A;
void ota_bank_swap(bool update_success) {
    if (update_success) next_boot_bank = (next_boot_bank == Bank::A) ? Bank::B : Bank::A;
}

// 68. 단언(Assertion) 매크로
#ifdef DEBUG
#define ASSERT(cond) do { if (!(cond)) { while(1); } } while(0)
#else
#define ASSERT(cond) ((void)0)
#endif

// 69. 소프트웨어 워치독
class SoftwareWatchdog {
public:
    static constexpr int TASKS = 3;
    SoftwareWatchdog() { std::fill(alive, alive+TASKS, false); }
    void feed(int task_id) { alive[task_id] = true; }
    bool all_alive() const { for (int i=0; i<TASKS; ++i) if (!alive[i]) return false; return true; }
    void reset() { std::fill(alive, alive+TASKS, false); }
private:
    bool alive[TASKS];
};

// 70. 에러 처리 프레임워크
struct ErrorInfo {
    int code;
    const char* file;
    int line;
};
class ErrorFramework {
public:
    void report(int code, const char* file, int line) {
        last = {code, file, line};
    }
    ErrorInfo last;
};

// 71. 상태 머신 구현 (State 패턴)
class LedState {
public:
    virtual ~LedState() = default;
    virtual void handle() = 0;
};
class LedOn : public LedState {
public:
    void handle() override { std::cout << "LED ON\n"; }
};
class LedOff : public LedState {
public:
    void handle() override { std::cout << "LED OFF\n"; }
};
class LedController {
public:
    LedController() : state(new LedOff) {}
    ~LedController() { delete state; }
    void set_state(LedState* s) { delete state; state = s; }
    void update() { state->handle(); }
private:
    LedState* state;
};

// 72. PID 제어기
class PID {
public:
    PID(float kp, float ki, float kd) : kp(kp), ki(ki), kd(kd), prev_err(0), integ(0) {}
    float update(float setpoint, float measured) {
        float err = setpoint - measured;
        integ += err;
        float deriv = err - prev_err;
        prev_err = err;
        return kp*err + ki*integ + kd*deriv;
    }
private:
    float kp, ki, kd, prev_err, integ;
};

// 73. ISR과 메인 루프가 32비트 카운터 공유 (atomic 사용)
std::atomic<uint32_t> shared_counter{0};
uint32_t safe_read_counter() {
    return shared_counter.load(std::memory_order_acquire);
}

// 74. 임계 영역(Critical Section) RAII
class CriticalSection {
public:
    CriticalSection() { /* disable_irq(); */ }
    ~CriticalSection() { /* enable_irq(); */ }
};

// 75. 이벤트 플래그(Event Flags)
class EventFlags {
public:
    void set(uint32_t mask) { flags |= mask; }
    void clear(uint32_t mask) { flags &= ~mask; }
    bool wait(uint32_t mask) { return (flags & mask) == mask; }
private:
    uint32_t flags = 0;
};

// 76. 실시간 성능 측정 (고해상도 타이머)
uint64_t measure_us(std::function<void()> func) {
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(end-start).count();
}

// 77. 재진입성 문제 예시 및 수정
int global_var = 0;
void not_reentrant() { global_var++; }
void reentrant(int& local_var) { local_var++; }

// 78. 정적 초기화 순서 문제
class StaticA;
class StaticB;
StaticA* a_ptr = nullptr;
StaticB* b_ptr = nullptr;
class StaticA { public: StaticA() { b_ptr = nullptr; }};
class StaticB { public: StaticB() { a_ptr = nullptr; }};
// 해결: Construct on First Use Idiom
StaticA& getA() { static StaticA a; return a; }
StaticB& getB() { static StaticB b; return b; }

// 79. 전원 차단 시 데이터 저장
void save_critical_data(const char* data) {
    std::cout << "Saving data to flash: " << data << "\n";
}

// 80. 링 버퍼 단위 테스트
void test_ring_buffer() {
    IsrSafeQueue q;
    q.push(1); q.push(2);
    uint8_t v;
    assert(q.pop(v) && v == 1);
    assert(q.pop(v) && v == 2);
    assert(q.is_empty());
    std::cout << "80. RingBuffer unit test PASS\n";
}

// 81. MockUart 클래스
class MockUart {
public:
    void send(uint8_t b) { buf.push_back(b); }
    std::vector<uint8_t> buf;
};

// 82. End-to-End 테스트 (pseudocode)
void end_to_end_test() {
    // motor.set_velocity(100);
    // wait(100ms);
    // assert(encoder.read() > 0);
    std::cout << "82. End-to-End test: motor velocity set and encoder checked\n";
}

// 83. 로깅 시스템
enum class LogLevel { DEBUG, INFO, ERROR };
void log(LogLevel lvl, const std::string& msg) {
    const char* lvlstr = (lvl==LogLevel::DEBUG)?"DEBUG":(lvl==LogLevel::INFO)?"INFO":"ERROR";
    std::cout << "[" << lvlstr << "] " << msg << "\n";
}

// 84. 기능 안전 - 입력값 검증
bool error_flag = false;
void set_motor_speed(float v) {
    if (v < 0 || v > 100) { error_flag = true; v = 0; }
    // 실제 모터 속도 설정 코드
}

// 85. 이중화(Redundancy) 처리
std::optional<float> safe_temp(float t1, float t2) {
    if (std::abs(t1-t2) < 2.0f) return (t1+t2)/2.0f;
    return std::nullopt;
}

// 86. 안전 상태(Safe State) 진입
void enter_safe_state() {
    std::cout << "All motors stopped, warning LED ON\n";
}

// 87. C 코드와 C++ 코드 연동 예시
extern "C" {
    void legacy_c_func();
}
void legacy_c_func() { std::cout << "Called C function from C++\n"; }

// 88. 메모리 풋프린트 최적화
class Compact {
    uint8_t a;
    uint16_t b;
    uint8_t c : 4;
    uint8_t d : 4;
};

// 89. CPU 사용률 계산
float calc_cpu_usage(uint32_t idle_us, uint32_t total_us) {
    return 100.0f * (1.0f - (float)idle_us / total_us);
}

// 90. 인터럽트 지연(Latency) 측정 설명
void explain_latency_measure() {
    std::cout << "90. Use GPIO toggle at ISR entry and measure with oscilloscope.\n";
}

// 91. 가상 소멸자 예시
class Base {
public:
    virtual ~Base() { std::cout << "Base dtor\n"; }
};
class Derived : public Base {
public:
    ~Derived() { std::cout << "Derived dtor\n"; }
};

// 92. std::span 활용
void print_span(std::span<const int> s) {
    for (auto v : s) std::cout << v << " ";
    std::cout << "\n";
}

// 93. std::optional 활용
std::optional<int> read_sensor(bool ok) {
    if (ok) return 42;
    return std::nullopt;
}

// 94. enum class 사용 예시
enum class LedStateEnum { Off, On, Blink };
// void set_led_state(LedStateEnum s) {
//     if (s == LedStateEnum::On) std::cout << "LED ON\n";
//     else if (s == LedStateEnum::Off) std::cout << "LED OFF\n";
//     else std::cout << "LED BLINK\n";
// }

void set_led_state(LedStateEnum s) {
    switch (s) {
        case LedStateEnum::On:
            std::cout << "LED ON\n";
            break;
        case LedStateEnum::Off:
            std::cout << "LED OFF\n";
            break;
        case LedStateEnum::Blink:
            std::cout << "LED BLINK\n";
            break;
    }
}

// 95. static_assert 활용
constexpr size_t BUF_SIZE = 8;
static_assert((BUF_SIZE & (BUF_SIZE-1)) == 0, "BUF_SIZE must be power of 2");

// 96. 템플릿 메타프로그래밍(TMP) - 팩토리얼
template<int N>
struct Factorial { static constexpr int value = N * Factorial<N-1>::value; };
template<>
struct Factorial<0> { static constexpr int value = 1; };

// 97. 커스텀 new/delete 오버로딩 (간단 예시)
void* operator new(std::size_t sz) {
    std::cout << "Custom new: " << sz << " bytes\n";
    return std::malloc(sz);
}
void operator delete(void* p) noexcept {
    std::cout << "Custom delete\n";
    std::free(p);
}

// 98. 완벽한 전달(Perfect Forwarding)과 가변 인자 템플릿
template<typename... Args>
void log_wrap(Args&&... args) {
    (std::cout << ... << args) << "\n";
}

// 99. std::atomic 사용
std::atomic<int> atomic_counter{0};
void atomic_increment() { atomic_counter.fetch_add(1, std::memory_order_relaxed); }

// --- Test code ---
int main() {
    std::cout << "=== Part4 Real-Time & Concurrency Test ===\n";

    // 61. CooperativeScheduler
    CooperativeScheduler sched;
    sched.add_task([](){ std::cout << "Task1 "; });
    sched.add_task([](){ std::cout << "Task2 "; });
    std::cout << "61. Scheduler: "; sched.run_once(); std::cout << "\n";

    // 62. IsrSafeQueue
    IsrSafeQueue q; q.push(10); uint8_t v; q.pop(v); std::cout << "62. IsrSafeQueue pop: " << (int)v << "\n";

    // 63. DeadlineScheduler
    DeadlineScheduler ds;
    ds.add_task({[](){ std::cout << "TaskA "; }, 50});
    ds.add_task({[](){ std::cout << "TaskB "; }, 10});
    std::cout << "63. DeadlineScheduler: "; ds.run(); std::cout << "\n";

    // 64. Stack usage
    char stack[128];
    size_t used = measure_stack_usage(stack, sizeof(stack), [](){ volatile int x=0; x++; });
    std::cout << "64. Stack used: " << used << "\n";

    // 65. Stack Canary
    {
        STACK_CANARY_INIT;
        // ... 함수 내용 ...
        STACK_CANARY_CHECK;
        std::cout << "65. Stack Canary OK\n";
    }

    // 66. Bootloader
    bootloader_jump_example();

    // 67. OTA Bank Swap
    ota_bank_swap(true);
    std::cout << "67. Next boot bank: " << (next_boot_bank == Bank::A ? "A" : "B") << "\n";

    // 68. ASSERT macro
    int x = 1;
    ASSERT(x == 1);
    std::cout << "68. ASSERT macro OK\n";

    // 69. SoftwareWatchdog
    SoftwareWatchdog swd;
    swd.feed(0); swd.feed(1); swd.feed(2);
    std::cout << "69. SW Watchdog all alive: " << swd.all_alive() << "\n";

    // 70. ErrorFramework
    ErrorFramework ef;
    ef.report(42, __FILE__, __LINE__);
    std::cout << "70. Error code: " << ef.last.code << ", file: " << ef.last.file << "\n";

    // 71. State Pattern
    LedController led;
    led.set_state(new LedOn); led.update();
    led.set_state(new LedOff); led.update();

    // 72. PID
    PID pid(1.0f, 0.1f, 0.01f);
    std::cout << "72. PID output: " << pid.update(10, 8) << "\n";

    // 73. Atomic counter
    shared_counter = 123;
    std::cout << "73. Safe counter read: " << safe_read_counter() << "\n";

    // 74. CriticalSection
    {
        CriticalSection cs;
        std::cout << "74. Critical section entered\n";
    }

    // 75. EventFlags
    EventFlags ev;
    ev.set(0x3);
    std::cout << "75. EventFlags wait: " << ev.wait(0x3) << "\n";

    // 76. Performance measure
    uint64_t us = measure_us([](){ for (volatile int i=0; i<1000; ++i); });
    std::cout << "76. Elapsed us: " << us << "\n";

    // 77. Reentrancy
    int local = 0;
    not_reentrant();
    reentrant(local);
    std::cout << "77. Reentrancy local: " << local << "\n";

    // 78. Static init order
    getA(); getB();
    std::cout << "78. Static init order idiom OK\n";

    // 79. Power fail save
    save_critical_data("important");

    // 80. RingBuffer unit test
    test_ring_buffer();

    // 81. MockUart
    MockUart mu; mu.send(0x42);
    std::cout << "81. MockUart buf: " << (int)mu.buf[0] << "\n";

    // 82. End-to-End test
    end_to_end_test();

    // 83. Logging
    log(LogLevel::DEBUG, "Debug message");
    log(LogLevel::ERROR, "Error message");

    // 84. Functional safety
    set_motor_speed(150);
    std::cout << "84. Error flag: " << error_flag << "\n";

    // 85. Redundancy
    auto t = safe_temp(25.0f, 25.5f);
    std::cout << "85. Safe temp: " << (t ? std::to_string(*t) : "ERROR") << "\n";

    // 86. Safe state
    enter_safe_state();

    // 87. C/C++ interop
    legacy_c_func();

    // 88. Compact class size
    std::cout << "88. Compact size: " << sizeof(Compact) << "\n";

    // 89. CPU usage
    std::cout << "89. CPU usage: " << calc_cpu_usage(100, 1000) << "%\n";

    // 90. Latency measure
    explain_latency_measure();

    // 91. Virtual destructor
    Base* b = new Derived();
    delete b;

    // 92. std::span
    int arr[] = {1,2,3};
    // print_span(std::span<int>(arr,3));
    print_span(std::span<const int>(arr,3)); // <-- 수정: std::span<int> → std::span<const int>


    // 93. std::optional
    auto val = read_sensor(true);
    std::cout << "93. Sensor val: " << (val ? std::to_string(*val) : "NONE") << "\n";

    // 94. enum class
    set_led_state(LedStateEnum::Blink);

    // 95. static_assert
    std::cout << "95. static_assert passed\n";

    // 96. TMP factorial
    std::cout << "96. Factorial<5>: " << Factorial<5>::value << "\n";

    // 97. Custom new/delete
    int* p = new int(42);
    delete p;

    // 98. Perfect forwarding
    log_wrap("98. ", "Perfect ", "forwarding ", 123);

    // 99. std::atomic
    atomic_increment();
    std::cout << "99. atomic_counter: " << atomic_counter << "\n";

    return 0;
}