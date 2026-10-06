# ⚙️ L04 · 임베디드 C++ 기본기 — 테스트 엔지니어가 알아야 할 것만

> Neros에서 C++은 주로 **autonomy · companion(embedded Linux)** 쪽 공고에 나오고 [확인됨 공고 5234134007 · 5241249007], MCU 펌웨어 공고는 C 중심이다 — Ukraine 공고는 "Strong C development skills", Tennessee 공고는 "C/C++" [확인됨 공고 5086180007 · 5095368007]. 테스트 엔지니어에게 필요한 건 C++ 전문가 수준이 아니라 **"C++ 코드를 읽고, 테스트하기 좋은 구조(인터페이스 · RAII)를 알아보고, GoogleTest로 테스트를 쓸 수 있나"**다. 코드는 전부 `lecture/code_cpp/l04_embedded_cpp.cpp` 한 파일이고, **임베디드에서 흔한 설정 `-fno-exceptions -fno-rtti`**, C++17, `-Wall -Wextra -Werror` + ASan/UBSan으로 모든 `assert`가 통과했다 (`make -C lecture/code_cpp test` → `L04: all examples PASS`). 주석에 "컴파일 에러"라고 쓴 4줄은 실제로 주석을 풀어 **컴파일이 실패하는 것까지 확인**했다.

## 0. 한 장 요약

| 주제 | 핵심 한 줄 | 테스트와의 관계 |
|---|---|---|
| C vs C++ (임베디드) | C++의 "공짜" 기능(class, template, constexpr, RAII)은 쓰고, 비용 있는 것(예외, RTTI, heap, iostream)은 끈다 | 빌드 플래그를 알아야 host 테스트 빌드와 타깃 빌드의 차이를 이해한다 |
| 참조 `&` | NULL이 될 수 없는 별칭. 큰 객체는 `const T&` | |
| `enum class` · `constexpr` · `static_assert` | 타입 안전한 enum, 컴파일 타임 계산 · 검증 | 레이아웃 · 상수를 컴파일 타임 테스트로 고정 |
| RAII | 생성자에서 잡고 소멸자에서 놓는다 — **모든 return 경로에서 정리** | critical section · 락 · 리그 자원 정리 |
| class 기본 | 초기화 리스트, `explicit`, `const` 멤버 함수, `private` | |
| 템플릿 | 타입 · 크기를 컴파일 타임에 → `RingBuffer<T, N>`, heap 없음 | |
| `std::array` · `std::optional` | 고정 크기 배열 / 예외 없이 "값 없음" | |
| 인터페이스(가상 함수) + DI | HAL을 인터페이스로 받으면 **fake를 끼워 테스트** | 가장 중요한 테스트 seam |
| 템플릿 주입 | 가상 호출 없이 같은 효과 (zero-cost) | 타깃 성능이 중요할 때의 seam |
| 콜백 | 함수 포인터 + `void*` ctx, 캡처 없는 람다, 템플릿 람다 | |
| 레지스터 | `volatile` 구조체 + `reinterpret_cast`, `static_assert(offsetof)` | 레지스터 블록을 가짜 메모리로 바꿔 host에서 테스트 |
| `extern "C"` | C 코드와 링크 | **GoogleTest로 C 펌웨어를 테스트할 때 필수** |
| 소유권 | 복사 금지 · 이동, 실무는 `unique_ptr` / 정적 할당 | |
| 함정 | slicing, 가상 소멸자, `override`, 정적 초기화 순서 | |

## 1. 임베디드에서 C++의 무엇을 쓰고 무엇을 끄나

| 쓴다 (비용 0에 가깝다) | 조심 / 끈다 | 이유 |
|---|---|---|
| class, 접근 제어, 참조, namespace | 예외 (`-fno-exceptions`) | 코드 크기, 실행 시간 비결정성, unwind 테이블 |
| `constexpr`, `static_assert`, `enum class` | RTTI (`-fno-rtti`): `dynamic_cast`, `typeid` | 코드 크기 |
| 템플릿 (`RingBuffer<T, N>`) | heap: `new`, `std::vector`, `std::string`, `std::function`(할당 가능) | 단편화, 실패 처리, 시간 |
| RAII, 람다(캡처 없거나 참조 캡처) | `iostream` | 크기가 매우 크다 |
| `std::array`, `std::optional`, `<algorithm>` 일부 | 가상 함수 남용 | 객체마다 vptr, 호출마다 간접 점프 |

- 템플릿의 비용: 실행 비용은 없지만 **타입마다 코드가 복제**될 수 있다(code bloat) → map 파일로 크기 추적
- 예외를 끄면 `std::optional::value()`처럼 예외를 던지는 API는 쓰지 않는다 → `if (opt)` / `*opt`

## 2. 참조

```cpp
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
```

- 포인터 vs 참조: 참조는 **NULL이 없고 다시 묶을 수 없다**. "없을 수도 있다"면 포인터나 `std::optional`
- 함정: 호출하는 쪽에서 `scale(v, 4)`만 보고는 v가 바뀐다는 걸 모른다 → 이름 · 문서로 드러낸다

## 3. enum class · constexpr · static_assert

```cpp
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
```

- `enum class Mode : uint8_t` — 이름이 새어 나가지 않고, int로 몰래 바뀌지 않고, **크기를 정할 수 있다**(wire 포맷)
- `constexpr` 함수 · 표는 컴파일 타임에 계산 → 결과가 flash에 상수로
- `static_assert` = **컴파일 타임 테스트**. 크기 · 오프셋 · 상수 관계가 깨지면 빌드가 실패한다

## 4. RAII — C++이 테스트 코드와 펌웨어에 주는 가장 큰 선물

```cpp
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
```

- RAII = Resource Acquisition Is Initialization: **자원의 수명 = 객체의 수명**. 함수 중간에서 `return`해도, 에러 경로여도 소멸자가 정리한다
- C에서 `disable_irq(); ... if (err) { enable_irq(); return; } ... enable_irq();`처럼 짝을 손으로 맞추던 것을 컴파일러가 보장
- 같은 생각이 Python에선 `with` (context manager), pytest에선 `yield` fixture — **"리그 전원은 테스트가 실패해도 반드시 끈다"**
- 복사를 `= delete`로 막는 이유: 복사되면 소멸자가 두 번 불려 "두 번 enable" 같은 버그

## 5. class 기본

```cpp
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
```

| 문법 | 뜻 |
|---|---|
| `: pin_(pin)` | 초기화 리스트 — 생성자 본문 전에 멤버를 초기화 (const · 참조 멤버는 이것만 가능) |
| `explicit` | `Led led = 13;` 같은 암시적 변환 금지 |
| `bool is_on() const` | 이 함수는 객체를 바꾸지 않는다 → const 객체 · const 참조에서도 호출 가능 |
| `bool state_ = false;` | 기본 멤버 초기화 (초기화 안 된 멤버 버그 예방) |
| `struct` vs `class` | 기본 접근이 public vs private — 그 외엔 같다 |

## 6. 템플릿 — 크기가 고정된 RingBuffer<T, N>

```cpp
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
```

- `N`이 템플릿 인자라 **배열이 객체 안에 정적으로** 들어간다 (heap 없음). `static_assert`로 2의 거듭제곱을 컴파일 타임에 강제
- `std::optional<T>`: 예외 없이 "꺼낼 게 없다"를 반환. C의 `bool rb_pop(rb_t*, uint8_t *out)`과 같은 정보
- C 버전과 비교: [L03 10절](2026-10-04_L03_embedded_c_basics.md), Python 버전: [L00 Part 1](2026-10-04_L00_special_lecture.md)

## 7. 인터페이스 + 의존성 주입 — 테스트 가능한 C++의 핵심

```cpp
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
```

- 피검 코드(`TempSensor`)가 **구체 드라이버가 아니라 인터페이스(`II2c&`)**를 받으므로, 테스트는 `FakeI2c`를 넘겨 재시도 · 에러 경로를 하드웨어 없이 검증한다
- GoogleTest라면 `FakeI2c` 대신 gMock: `MOCK_METHOD(bool, read, (uint8_t, uint8_t, uint8_t*, std::size_t), (override));` → [L02 A5](2026-10-04_L02_embedded_test_frameworks.md)
- **가상 소멸자**: 부모 포인터로 객체를 지울 때 자식 소멸자가 불리게. 인터페이스엔 항상 `virtual ~I() = default;`
- 비용: 객체마다 vptr 하나 + 호출마다 간접 점프. 대부분 무시할 수준이지만 μs 단위 ISR 경로라면 8절

## 8. 템플릿 주입 — 가상 호출 없는 테스트 seam

```cpp
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
```

| 테스트 seam | 어떻게 바꿔 끼우나 | 비용 | 언제 |
|---|---|---|---|
| 가상 인터페이스 (7절) | 생성자에 fake 객체 | vptr + 간접 호출 | 대부분의 드라이버 · 서비스 |
| 템플릿 인자 (8절) | `TempSensorT<FakeBus>` | 0 (인라인) | 핫 패스, ISR 근처 |
| 링크 타임 | 테스트 빌드에서 `hal_i2c.c` 대신 `fake_hal_i2c.c` 링크 | 0 | C 코드, 레거시 |
| 전처리기 | `#ifdef UNIT_TEST` | 0 | 최후의 수단 (코드가 지저분해짐) |

> 면접 한 줄: **"I look for a seam — usually an interface the driver depends on, or a template parameter where performance matters — so the logic can run on the host with a fake bus."**

## 9. 콜백 — 함수 포인터, 람다

```cpp
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
```

- ISR · C API에 넘길 땐 **함수 포인터 + `void* ctx`** (캡처 없는 람다는 함수 포인터로 바뀐다)
- C++ 내부에선 **템플릿 + 람다**가 가장 싸다 (인라인됨). `std::function`은 편하지만 캡처가 크면 heap을 쓸 수 있다
- 캡처: `[&total]` 참조, `[total]` 복사, `[&]` 전부 참조 — 콜백이 나중에 불리면 **참조 캡처가 dangling**이 될 수 있다

## 10. 메모리 맵 레지스터

```cpp
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
```

- 레지스터 블록을 `volatile` 멤버를 가진 구조체로 표현하고, 타깃에선 `reinterpret_cast<GpioRegs*>(주소)`
- `static_assert(offsetof(...))`로 **레퍼런스 매뉴얼의 오프셋과 일치**하는지 컴파일 타임에 검사
- 테스트 트릭: 주소를 돌려주는 함수(`gpio()`)만 바꾸면 **host에서 가짜 메모리로 드라이버 로직을 테스트**할 수 있다 (read-modify-write가 다른 핀을 안 건드리는지 등)

## 11. extern "C" — C 펌웨어를 C++ 테스트로

```cpp
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
```

- C++은 오버로딩 때문에 함수 이름을 장식(mangling)한다 → C 컴파일러가 만든 심볼과 이름이 달라 링크 에러
- 그래서 GoogleTest · CppUTest로 C 코드를 테스트할 때 `extern "C" { #include "sensor.h" }` ([L02 A4 · A5](2026-10-04_L02_embedded_test_frameworks.md)가 실제로 그렇게 했다)
- C 헤더를 C++에서도 쓰게 하려면 헤더 안에 `#ifdef __cplusplus extern "C" { #endif`

## 12. 소유권 — 복사 · 이동 · Rule of Zero

```cpp
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
```

- 자원(메모리, 핸들, 포트)을 직접 소유하는 클래스는 **복사 · 이동 · 소멸**을 다 정해야 한다 (Rule of Three/Five)
- 더 좋은 답은 **Rule of Zero**: 소유는 `std::unique_ptr` · `std::array` 같은 표준 타입에 맡기고, 내 클래스엔 특수 멤버 함수를 안 쓴다
- 펌웨어에선 대개 heap 자체를 안 쓴다 → 정적 할당 · 고정 크기 풀

## 13. 함정 — 읽다가 알아봐야 하는 것

```cpp
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
```

| 함정 | 증상 | 예방 |
|---|---|---|
| **object slicing** | 자식을 값으로 넘기면 부모 부분만 복사 → 가상 함수가 부모 것 | 참조 · 포인터로 |
| 가상 소멸자 없음 | 부모 포인터로 delete 하면 자식 소멸자 안 불림 (UB, 누수) | 인터페이스엔 `virtual ~I() = default` |
| `override` 안 씀 | 시그니처가 살짝 달라 오버라이드가 아니라 새 함수가 됨 | 항상 `override` |
| dangling 참조 · 람다 캡처 | 사라진 지역 변수를 참조 | 수명 확인, ASan |
| 정적 초기화 순서 | 다른 파일의 전역 객체가 아직 생성 전 | 함수 안 static (Meyers singleton), 전역 객체 최소화 |
| 정수 함정 | C와 같다 (signed overflow UB, promotion) | [L03 1절](2026-10-04_L03_embedded_c_basics.md) |

## 14. 테스트 엔지니어 관점 정리

| 상황 | 할 것 |
|---|---|
| C++ 모듈을 테스트해야 한다 | 의존성이 **인터페이스 / 템플릿 인자 / 링크 seam** 중 어디로 들어오는지 먼저 본다 |
| seam이 없다 | 개발자와 "HAL을 인터페이스로 빼자"를 제안 — 테스트 가능성은 설계 리뷰 항목 |
| 단위 테스트 프레임워크 | GoogleTest + gMock (C++ 표준), 작은 타깃이면 CppUTest ([L02](2026-10-04_L02_embedded_test_frameworks.md)) |
| host 빌드 vs 타깃 빌드 | 같은 소스를 두 번: host(ASan/UBSan, 예외 유무 동일하게) · 타깃(크로스 컴파일). 플래그 차이를 CI에 명시 |
| 컴파일 타임 테스트 | `static_assert`로 레이아웃 · 상수 · 템플릿 조건 |
| 정리 보장 | 테스트 하네스의 리그 자원도 RAII 객체로 (C++ 하네스라면) |

## 15. 빠른 퀴즈 15

| # | 질문 | 답 |
|---|---|---|
| 1 | 임베디드에서 흔히 끄는 C++ 기능 2개 | 예외, RTTI |
| 2 | 참조와 포인터 차이 | 참조는 NULL 없음, 다시 묶을 수 없음 |
| 3 | `enum class`의 장점 | 범위 · 암시적 변환 없음 · 크기 지정 |
| 4 | `constexpr`란 | 컴파일 타임에 계산할 수 있는 값 · 함수 |
| 5 | RAII 한 문장 | 자원 수명 = 객체 수명, 소멸자가 정리 |
| 6 | 인터페이스에 가상 소멸자가 필요한 이유 | 부모 포인터로 지울 때 자식 소멸자 호출 |
| 7 | `explicit` 생성자 | 암시적 변환 금지 |
| 8 | `const` 멤버 함수 | 객체를 바꾸지 않는다는 약속 |
| 9 | 템플릿 RingBuffer가 heap을 안 쓰는 이유 | 크기 N이 컴파일 타임 상수 → 배열이 객체 안에 |
| 10 | 예외 없이 "값 없음"을 반환하려면 | `std::optional` (또는 bool + out 파라미터) |
| 11 | HAL을 fake로 바꾸는 방법 3개 | 가상 인터페이스, 템플릿 인자, 링크 타임 교체 |
| 12 | `extern "C"`가 필요한 때 | C로 컴파일된 함수를 C++에서 부르거나 그 반대 |
| 13 | object slicing | 자식을 값으로 복사하면 부모 부분만 남음 |
| 14 | `std::function`을 펌웨어에서 조심하는 이유 | 캡처가 크면 heap 할당 가능 |
| 15 | 레지스터 블록 오프셋을 검증하는 법 | `static_assert(offsetof(Regs, FIELD) == N)` |

## 체크

- [ ] 0절 표를 가리고 "임베디드 C++에서 쓰는 것 / 끄는 것"을 말하기
- [ ] 4절 RAII를 Python `with` · pytest `yield` fixture와 연결해서 30초
- [ ] 7 · 8절 테스트 seam 4가지를 표 없이 말하기
- [ ] playground에서 `RingBuffer<T, N>`을 빈 파일로 (`c++ -std=c++17 -fno-exceptions` 로 컴파일해 보기)
- [ ] 15절 퀴즈를 가리고 12개 이상
