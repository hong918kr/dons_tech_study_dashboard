// L04 예제 — 임베디드에서 흔한 설정(-fno-exceptions -fno-rtti, heap 없음)으로 컴파일·실행된다.
//   make -C lecture/code_cpp test     (C++17, -Wall -Wextra -Werror, ASan/UBSan)
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <type_traits>
#include <utility>

// --- references ---
static void scale(int &x, int k) { x *= k; }            // 참조: NULL 이 될 수 없는 별칭
static int sum(const std::array<int, 4> &a)              // 큰 객체는 const 참조로 (복사 없음, 수정 불가)
{
    int s = 0;
    for (int v : a)                                      // range-for
        s += v;
    return s;
}

static void demo_references()
{
    int v = 3;
    scale(v, 4);                                         // 호출하는 쪽엔 & 가 안 보인다 → 이름으로 의도를 드러낼 것
    assert(v == 12);
    std::array<int, 4> a{1, 2, 3, 4};                    // 크기가 타입의 일부, sizeof 가 그대로, 범위 체크 가능(at)
    assert(sum(a) == 10 && a.size() == 4);
}
// --- end ---

// --- enum_constexpr ---
enum class Mode : uint8_t { Idle = 0, Armed = 1, Flying = 2 };   // 범위 있는 enum, 크기 지정

constexpr uint32_t bit(unsigned n) { return 1u << n; }           // 컴파일 타임에 계산 가능한 함수
constexpr uint32_t kUartEnable = bit(0) | bit(3);
static_assert(kUartEnable == 0x9, "computed at compile time");
static_assert(sizeof(Mode) == 1, "fits in one byte on the wire");

static void demo_enum_constexpr()
{
    Mode m = Mode::Armed;
    // int x = m;                                        // 컴파일 에러: enum class 는 암시적 변환 없음
    assert(static_cast<uint8_t>(m) == 1);
    constexpr std::array<uint8_t, 3> kTable{0x10, 0x20, 0x30};   // flash 에 놓일 수 있는 상수 표
    static_assert(kTable[2] == 0x30, "");
}
// --- end ---

// --- raii ---
struct FakeIrq {                                         // 테스트용: 인터럽트 on/off 기록
    static int disabled_depth;
    static void disable() { ++disabled_depth; }
    static void enable() { --disabled_depth; }
};
int FakeIrq::disabled_depth = 0;

class CriticalSection {                                  // RAII: 생성자에서 잡고, 소멸자에서 반드시 놓는다
public:
    CriticalSection() { FakeIrq::disable(); }
    ~CriticalSection() { FakeIrq::enable(); }
    CriticalSection(const CriticalSection &) = delete;   // 복사 금지 (두 번 놓는 버그 방지)
    CriticalSection &operator=(const CriticalSection &) = delete;
};

static int shared_counter;

static bool increment_if_below(int limit)
{
    CriticalSection cs;                                  // 이 블록 동안 인터럽트 off
    if (shared_counter >= limit)
        return false;                                    // 일찍 return 해도 소멸자가 enable
    ++shared_counter;
    return true;
}

static void demo_raii()
{
    assert(increment_if_below(1) && !increment_if_below(1));
    assert(FakeIrq::disabled_depth == 0);                // 모든 경로에서 짝이 맞았다
}
// --- end ---

// --- class_basics ---
class Led {
public:
    explicit Led(uint8_t pin) : pin_(pin) {}             // 초기화 리스트, explicit = 암시적 변환 금지
    void on() { state_ = true; }
    void off() { state_ = false; }
    bool is_on() const { return state_; }                // const 멤버 함수: 객체를 안 바꾼다는 약속
    uint8_t pin() const { return pin_; }

private:
    uint8_t pin_;
    bool state_ = false;                                 // 기본 멤버 초기화
};

static void demo_class_basics()
{
    Led led(13);
    // Led bad = 13;                                     // explicit 덕분에 컴파일 에러
    led.on();
    assert(led.is_on() && led.pin() == 13);
    const Led &view = led;
    assert(view.is_on());                                // const 객체에선 const 멤버만 부를 수 있다
}
// --- end ---

// --- template_ring ---
template <typename T, std::size_t N>
class RingBuffer {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of two");
public:
    bool push(const T &v)
    {
        if (size() == N)
            return false;                                // drop-new
        buf_[head_++ & (N - 1)] = v;
        return true;
    }
    std::optional<T> pop()                               // 예외 없이 '없음'을 표현
    {
        if (empty())
            return std::nullopt;
        return buf_[tail_++ & (N - 1)];
    }
    std::size_t size() const { return head_ - tail_; }
    bool empty() const { return head_ == tail_; }
    static constexpr std::size_t capacity() { return N; }

private:
    std::array<T, N> buf_{};                             // 정적 할당 — heap 없음
    uint32_t head_ = 0, tail_ = 0;
};

struct Sample {
    uint16_t id;
    int16_t value;
};

static void demo_template_ring()
{
    RingBuffer<Sample, 4> rb;                            // 타입과 크기가 컴파일 타임에 정해진다
    // RingBuffer<int, 6> bad;                           // static_assert 실패: 6 은 2의 거듭제곱이 아님
    for (uint16_t i = 0; i < 4; i++)
        assert(rb.push(Sample{i, static_cast<int16_t>(-i)}));
    assert(!rb.push(Sample{9, 9}) && rb.size() == 4);
    auto s = rb.pop();
    assert(s && s->id == 0);                             // optional: bool 로 검사, -> 로 접근
    while (rb.pop()) {
    }
    assert(!rb.pop().has_value());
    static_assert(RingBuffer<uint8_t, 16>::capacity() == 16, "");
}
// --- end ---

// --- interface_di ---
class II2c {                                             // HAL 인터페이스 (순수 가상)
public:
    virtual ~II2c() = default;                           // 가상 소멸자: 부모 포인터로 지워도 안전
    virtual bool read(uint8_t addr, uint8_t reg, uint8_t *buf, std::size_t n) = 0;
};

class TempSensor {                                       // 피검 코드: 구체 HAL 이 아니라 인터페이스에 의존
public:
    explicit TempSensor(II2c &bus) : bus_(bus) {}
    std::optional<int16_t> read_centi()
    {
        uint8_t b[2];
        if (!bus_.read(0x48, 0x00, b, 2) && !bus_.read(0x48, 0x00, b, 2))   // 1회 재시도
            return std::nullopt;
        return static_cast<int16_t>((b[0] << 8) | b[1]);
    }

private:
    II2c &bus_;
};

class FakeI2c : public II2c {                            // 테스트용 fake: 대본대로 응답하고 호출을 센다
public:
    bool fail_next = false;
    uint8_t data[2] = {0x09, 0x60};
    int calls = 0;
    bool read(uint8_t, uint8_t, uint8_t *buf, std::size_t n) override
    {
        ++calls;
        if (fail_next) {
            fail_next = false;
            return false;
        }
        std::memcpy(buf, data, n);
        return true;
    }
};

static void demo_interface_di()
{
    FakeI2c bus;
    TempSensor sensor(bus);
    assert(sensor.read_centi() == 2400 && bus.calls == 1);
    bus.fail_next = true;                                // 첫 시도 실패 → 재시도 성공
    assert(sensor.read_centi() == 2400 && bus.calls == 3);
}
// --- end ---

// --- static_poly ---
template <typename Bus>                                  // 템플릿 주입: 가상 호출 없이 (zero-cost)
class TempSensorT {
public:
    explicit TempSensorT(Bus &bus) : bus_(bus) {}
    int16_t read_centi()
    {
        uint8_t b[2] = {0, 0};
        bus_.read(0x48, 0x00, b, 2);
        return static_cast<int16_t>((b[0] << 8) | b[1]);
    }

private:
    Bus &bus_;
};

struct ScriptedBus {                                     // 상속 없이 같은 모양의 메서드만 있으면 된다
    uint8_t data[2];
    bool read(uint8_t, uint8_t, uint8_t *buf, std::size_t n)
    {
        std::memcpy(buf, data, n);
        return true;
    }
};

static void demo_static_poly()
{
    ScriptedBus bus{{0x00, 0x64}};
    TempSensorT<ScriptedBus> s(bus);
    assert(s.read_centi() == 100);
}
// --- end ---

// --- callbacks ---
using RxHandler = void (*)(uint8_t byte, void *ctx);     // C 스타일: 함수 포인터 + context (ISR 친화적)

struct Uart {
    RxHandler handler = nullptr;
    void *ctx = nullptr;
    void on_rx(RxHandler h, void *c) { handler = h, ctx = c; }
    void simulate_rx(uint8_t b) { if (handler) handler(b, ctx); }
};

template <typename F>
static void for_each_byte(const uint8_t *p, std::size_t n, F &&f)   // 템플릿 + 람다: 인라인, heap 없음
{
    for (std::size_t i = 0; i < n; i++)
        f(p[i]);
}

static void demo_callbacks()
{
    RingBuffer<uint8_t, 8> rx;
    Uart uart;
    uart.on_rx([](uint8_t b, void *c) { static_cast<RingBuffer<uint8_t, 8> *>(c)->push(b); }, &rx);
    uart.simulate_rx(0xAA);                              // 캡처 없는 람다는 함수 포인터로 변환된다
    assert(rx.size() == 1 && *rx.pop() == 0xAA);

    const uint8_t frame[] = {1, 2, 3};
    unsigned total = 0;
    for_each_byte(frame, sizeof frame, [&total](uint8_t b) { total += b; });   // [&] 캡처
    assert(total == 6);
}
// --- end ---

// --- registers ---
struct GpioRegs {                                        // 메모리 맵 레지스터 블록을 구조체로
    volatile uint32_t MODER;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
};
static_assert(offsetof(GpioRegs, BSRR) == 8, "layout must match the reference manual");

static GpioRegs fake_gpioa{};                            // 테스트에선 가짜 메모리, 타깃에선 아래 한 줄

static GpioRegs *gpio()
{
    // 타깃: return reinterpret_cast<GpioRegs *>(0x48000000u);
    return &fake_gpioa;
}

static void pin_mode_output(unsigned pin)
{
    uint32_t m = gpio()->MODER;                          // read
    m &= ~(3u << (pin * 2));                             // modify: 2비트 필드 지우고
    m |= 1u << (pin * 2);                                //         01 = output
    gpio()->MODER = m;                                   // write
}

static void demo_registers()
{
    fake_gpioa.MODER = 0xFFFFFFFFu;
    pin_mode_output(5);
    assert(((fake_gpioa.MODER >> 10) & 3u) == 1u);
    assert((fake_gpioa.MODER & ~(3u << 10)) == (0xFFFFFFFFu & ~(3u << 10)));   // 다른 핀은 그대로
}
// --- end ---

// --- extern_c ---
extern "C" {                                             // C 코드와 링크할 때: 이름 장식(mangling) 끄기
uint8_t c_checksum(const uint8_t *p, std::size_t n);     // 보통은 C 헤더를 extern "C" { #include } 로 감싼다
}

uint8_t c_checksum(const uint8_t *p, std::size_t n)      // (여기선 같은 파일에 정의 — 실제론 .c 파일)
{
    uint8_t x = 0;
    while (n--)
        x ^= *p++;
    return x;
}

static void demo_extern_c()
{
    const uint8_t f[] = {0x0F, 0xF0};
    assert(c_checksum(f, 2) == 0xFF);
}
// --- end ---

// --- ownership ---
class Buffer {                                           // 자원을 직접 소유하면 복사/이동을 정해야 한다
public:
    explicit Buffer(std::size_t n) : n_(n), p_(new uint8_t[n]{}) {}
    ~Buffer() { delete[] p_; }
    Buffer(const Buffer &) = delete;                     // 복사 금지 (두 번 delete 방지)
    Buffer &operator=(const Buffer &) = delete;
    Buffer(Buffer &&o) noexcept : n_(o.n_), p_(o.p_) { o.p_ = nullptr; o.n_ = 0; }   // 이동: 소유권 넘기기
    std::size_t size() const { return n_; }
    uint8_t *data() { return p_; }

private:
    std::size_t n_;
    uint8_t *p_;
};

static void demo_ownership()
{
    Buffer a(16);
    a.data()[0] = 7;
    Buffer b(std::move(a));                              // a 는 비고, b 가 소유
    assert(b.size() == 16 && b.data()[0] == 7 && a.size() == 0);
    // Buffer c = b;                                     // 컴파일 에러: 복사 금지
    // 실무: 펌웨어에선 heap 대신 std::array / 정적 풀. 쓸 때는 std::unique_ptr (Rule of Zero)
}
// --- end ---

// --- pitfalls ---
struct Base {
    virtual ~Base() = default;
    virtual int id() const { return 1; }
};
struct Derived : Base {
    int id() const override { return 2; }                // override: 시그니처가 틀리면 컴파일 에러
};

static int id_by_value(Base b) { return b.id(); }        // 값으로 받으면 slicing → Base 부분만 복사
static int id_by_ref(const Base &b) { return b.id(); }

static void demo_pitfalls()
{
    Derived d;
    assert(id_by_value(d) == 1);                         // 함정: 2 가 아니라 1
    assert(id_by_ref(d) == 2);                           // 다형성은 참조/포인터로
    static_assert(std::is_polymorphic<Base>::value, "has a vtable");
    static_assert(sizeof(Base) == sizeof(void *), "one vptr per object (this ABI)");
}
// --- end ---

int main()
{
    demo_references();
    demo_enum_constexpr();
    demo_raii();
    demo_class_basics();
    demo_template_ring();
    demo_interface_di();
    demo_static_poly();
    demo_callbacks();
    demo_registers();
    demo_extern_c();
    demo_ownership();
    demo_pitfalls();
    std::puts("L04: all examples PASS");
    return 0;
}
