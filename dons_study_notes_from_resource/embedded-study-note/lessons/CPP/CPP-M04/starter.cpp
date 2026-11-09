// CPP-M04 제로 코스트 GPIO HAL: Pin<Port, N> 템플릿
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
//
// 실제 칩:  Port = MmioPort<0x4002'0000u>  — *reinterpret_cast<volatile u32*>(Base + off)  (demo_asm_pin.cpp)
// 이 레슨:  Port = MockPort<Id>            — 레지스터를 배열로 흉내 내고 모든 접근을 로그에 남긴다.
//           Pin 은 Port 가 무엇인지 모른다: read(Reg) / write(Reg, v) 두 함수만 쓴다 (설계 자체는 CPP-M01 참고)
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <type_traits>

// STM32F4 GPIO 레이아웃을 따른다 (RM0090 8.4). 오프셋만 필요하다.
enum class Reg : std::uint32_t { MODER = 0x00, OTYPER = 0x04, OSPEEDR = 0x08, PUPDR = 0x0C,
                                 IDR = 0x10, ODR = 0x14, BSRR = 0x18, LCKR = 0x1C };
enum class Mode : std::uint32_t { Input = 0b00, Output = 0b01, Alternate = 0b10, Analog = 0b11 };

// ---------------- 가짜 하드웨어 ----------------
namespace mock {
struct Access { unsigned port; Reg reg; bool is_write; std::uint32_t value; };
inline Access   log[128];
inline unsigned log_n = 0;
inline void record(unsigned p, Reg r, bool w, std::uint32_t v) {
    if (log_n < 128) log[log_n++] = Access{p, r, w, v};
}

// "ODR 을 읽은 직후(LDR 과 STR 사이)에 인터럽트가 들어온다"를 결정적으로 재현하는 장치.
// arm_isr() 로 한 번 장전하면, 다음 ODR read 직후 ISR 이 실행된다. masked 면 pending 으로 남는다.
using IsrFn = void (*)();
inline IsrFn armed  = nullptr;
inline bool  masked = false;
inline void arm_isr(IsrFn f) { armed = f; }
inline void deliver_if_armed() {
    if (armed != nullptr && !masked) { IsrFn f = armed; armed = nullptr; f(); }
}
// 실제 칩이면: mrs primask / cpsid i ... msr primask (demo_asm_pin.cpp 의 IrqLock)
struct IrqGuard {
    bool prev;
    IrqGuard() : prev(masked) { masked = true; }
    ~IrqGuard() { masked = prev; deliver_if_armed(); }   // pending ISR 은 unmask 되는 순간 실행
    IrqGuard(const IrqGuard&) = delete;
    IrqGuard& operator=(const IrqGuard&) = delete;
};
}  // namespace mock

template <unsigned Id>
struct MockPort {
    static constexpr unsigned id = Id;
    static inline std::uint32_t regs[8]{};
    static constexpr unsigned idx(Reg r) { return static_cast<std::uint32_t>(r) / 4; }

    static std::uint32_t read(Reg r) {
        const std::uint32_t v = (r == Reg::BSRR) ? 0u : regs[idx(r)];   // BSRR 은 write-only, 읽으면 0
        mock::record(Id, r, false, v);
        if (r == Reg::ODR) mock::deliver_if_armed();
        return v;
    }
    static void write(Reg r, std::uint32_t v) {
        mock::record(Id, r, true, v);
        if (r == Reg::BSRR) {                                           // 하드웨어가 해 주는 일
            std::uint32_t odr = regs[idx(Reg::ODR)];
            odr &= ~(v >> 16);                                          // BRx: 위 16비트 → clear
            odr |= (v & 0xFFFFu);                                       // BSx: 아래 16비트 → set (둘 다면 set 우선)
            regs[idx(Reg::ODR)] = odr & 0xFFFFu;
        } else {
            regs[idx(r)] = v;
        }
    }
    // 테스트벤치 직통 (로그 없음)
    static void poke(Reg r, std::uint32_t v) { regs[idx(r)] = v; }
    static std::uint32_t peek(Reg r) { return regs[idx(r)]; }
    static void reset() { for (auto& x : regs) x = 0; }
};

using PortA = MockPort<0>;
using PortB = MockPort<1>;
using PortC = MockPort<2>;

template <typename P>
concept GpioPort = requires(Reg r, std::uint32_t v) {
    { P::read(r) } -> std::same_as<std::uint32_t>;
    { P::write(r, v) } -> std::same_as<void>;
};

// ---------------- Part A/B: Pin ----------------
template <GpioPort P, unsigned N>
    requires(N < 16)
struct Pin {
    using port = P;
    static constexpr unsigned number = N;
    static constexpr std::uint32_t mask = 1u << N;
    static constexpr std::uint32_t reset_mask = mask << 16;

    // Part A
    static void set() { /* TODO(Part A): BSRR 에 store 1번 */ }
    static void clear() { /* TODO(Part A): BSRR 위쪽 절반 */ }
    static void write(bool high) { (void)high; /* TODO(Part A): 값에 따라 store 1번, 읽기 0번 */ }
    static bool read() { return false; /* TODO(Part A): 핀의 실제 입력 레벨 (IDR) */ }
    static bool output() { return false; /* TODO(Part A): 우리가 내보내라고 한 값 (ODR) */ }
    static void mode(Mode m) {                                          // MODER 2비트 RMW — 초기화 때만
        (void)m; // TODO(Part A): 핀마다 2비트, 옆 핀의 필드를 보존
    }

    // Part B
    static void toggle() { /* TODO(Part B): ODR read-modify-write — 원자적이지 않다 */ }
    static void toggle_bsrr() {                                              // 다른 핀은 안 건드린다
        // TODO(Part B): ODR 을 읽고, 이 핀의 비트만 BSRR 로
    }
    template <typename Lock>
    static void toggle_critical() {                                          // 같은 핀 경쟁까지 막는다
        // TODO(Part B): Lock 의 수명 안에서 toggle()
    }
};

// ---------------- Part C: 극성 어댑터 / 단일 store 버스 / 드라이버 ----------------
template <typename P>
struct ActiveLow {
    static void on() { /* TODO(Part C) */ }
    static void off() { /* TODO(Part C) */ }
    static void write(bool lit) { (void)lit; /* TODO(Part C) */ }
    static bool is_on() { return false; /* TODO(Part C) */ }
};

// 전부 같은 포트 && 핀 번호가 겹치지 않음
template <typename First, typename... Rest>
concept BusPins = true;   // TODO(Part C): 포트 동일성 + 핀 중복 금지

template <typename First, typename... Rest>
    requires BusPins<First, Rest...>
struct PinBus {
    using port = typename First::port;
    static constexpr std::uint32_t mask = (First::mask | ... | Rest::mask);

    // v 의 bit i → i 번째 핀 위치로 (폭을 넘는 v 의 윗비트는 무시)
    static constexpr std::uint32_t scatter(std::uint32_t v) {
        (void)v;
        return 0; // TODO(Part C): fold expression
    }
    // BSRR 한 번으로 모든 핀을 동시에 갱신
    static void write(std::uint32_t v) {
        (void)v; // TODO(Part C): 켤 핀은 아래 절반, 끌 핀은 위 절반, store 1번
    }
};

// HD44780 호환 LCD 의 4비트 모드처럼: RS, 데이터 니블, E 펄스 (실제 칩은 사이에 µs 지연 필요 — 여기선 생략)
template <typename Rs, typename En, typename Data>
struct Lcd4 {
    static void nibble(std::uint8_t n) {
        (void)n; // TODO(Part C): 데이터 → E 상승 → E 하강
    }
    static void send(std::uint8_t byte, bool is_data) {
        (void)byte; (void)is_data; // TODO(Part C): RS → 상위 니블 → 하위 니블
    }
};

// ---------------- 테스트 하네스 ----------------
static int g_pass, g_total;
#define CHECK(...) do { g_total++; if (__VA_ARGS__) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__); } while (0)   // 가변 인자: 조건 속 템플릿 쉼표 때문

static void reset_all() {
    PortA::reset(); PortB::reset(); PortC::reset();
    mock::log_n = 0; mock::armed = nullptr; mock::masked = false;
}
static std::uint32_t xorshift32(std::uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
static unsigned count_log(Reg r, bool is_write) {
    unsigned n = 0;
    for (unsigned i = 0; i < mock::log_n; ++i) if (mock::log[i].reg == r && mock::log[i].is_write == is_write) ++n;
    return n;
}

// "이 타입이 만들어지는가?" — 반드시 템플릿 안에서 물어야 false 가 된다
template <typename P, unsigned N> constexpr bool can_pin = requires { typename Pin<P, N>; };
template <typename... Ps> constexpr bool can_bus = requires { typename PinBus<Ps...>; };

// ---- Part A: 경곗값 핀 번호로 표 구동 ----
template <unsigned N>
static void check_set_clear() {
    using P = Pin<PortA, N>;
    const std::uint32_t bit = 1u << N, others = 0xA5A5u & ~bit;
    reset_all();
    PortA::poke(Reg::ODR, others);
    P::set();
    CHECK(mock::log_n == 1 && mock::log[0].is_write && mock::log[0].reg == Reg::BSRR && mock::log[0].value == bit);
    CHECK(PortA::peek(Reg::ODR) == (others | bit));
    mock::log_n = 0;
    P::clear();
    CHECK(mock::log_n == 1 && mock::log[0].is_write && mock::log[0].reg == Reg::BSRR && mock::log[0].value == (bit << 16));
    CHECK(PortA::peek(Reg::ODR) == others);
}
template <unsigned... Ns> static void check_set_clear_all() { (check_set_clear<Ns>(), ...); }

template <unsigned N>
static void check_mode() {
    using P = Pin<PortB, N>;
    const unsigned shift = 2 * N;
    reset_all();
    PortB::poke(Reg::MODER, 0xFFFF'FFFFu);
    P::mode(Mode::Output);
    CHECK(PortB::peek(Reg::MODER) == ((0xFFFF'FFFFu & ~(0b11u << shift)) | (0b01u << shift)));
    CHECK(count_log(Reg::MODER, false) == 1 && count_log(Reg::MODER, true) == 1);
}

// ---- Part B: 인터럽트 끼어들기 ----
using Led   = Pin<PortA, 5>;
using Other = Pin<PortA, 3>;
static bool g_isr_level;
static void isr_write_other() { Other::write(g_isr_level); }
static void isr_set_other()   { Other::set(); }
static void isr_toggle_led()  { Led::toggle(); }

template <void (*Toggle)()>
static unsigned count_lost_updates(std::uint32_t seed, unsigned rounds, unsigned& fired) {
    reset_all();
    std::uint32_t s = seed;
    unsigned lost = 0;
    fired = 0;
    for (unsigned i = 0; i < rounds; ++i) {
        const std::uint32_t r = xorshift32(s);
        const bool fire = (r & 1u) != 0;
        g_isr_level = ((r >> 8) & 1u) != 0;
        if (fire) { mock::arm_isr(isr_write_other); ++fired; }
        Toggle();
        if (fire && Other::output() != g_isr_level) ++lost;   // ISR 이 쓴 값이 살아남았는가?
        if (fire) Other::write(g_isr_level);                  // 다음 회차를 위해 재동기화
        mock::armed = nullptr;
        mock::log_n = 0;
    }
    return lost;
}

int main() {
    // ---------- Part A: set / clear / write / read / mode ----------
    check_set_clear_all<0, 1, 5, 7, 8, 14, 15>();
    check_mode<0>(); check_mode<7>(); check_mode<15>();

    reset_all();
    PortA::poke(Reg::ODR, 1u << 5);
    Led::write(false);
    CHECK(mock::log_n == 1 && mock::log[0].reg == Reg::BSRR && mock::log[0].value == (1u << 21));
    CHECK(PortA::peek(Reg::ODR) == 0u);
    Led::write(true);
    CHECK(PortA::peek(Reg::ODR) == (1u << 5));
    CHECK(count_log(Reg::BSRR, false) == 0 && count_log(Reg::ODR, true) == 0);      // write-only 를 읽지 않았고 ODR 에 직접 쓰지 않았다

    reset_all();
    PortA::poke(Reg::IDR, 1u << 5);                                                 // 입력 레벨과 출력 래치는 별개
    CHECK(Led::read() && !Led::output());
    PortA::poke(Reg::IDR, 0u);
    PortA::poke(Reg::ODR, 1u << 5);
    CHECK(!Led::read() && Led::output());
    CHECK(mock::log_n == 4 && mock::log[0].reg == Reg::IDR && mock::log[1].reg == Reg::ODR);

    reset_all();
    PortA::write(Reg::BSRR, (1u << 5) | (1u << 21));                                // BSx 와 BRx 를 동시에 쓰면 set 우선 (RM0090)
    CHECK(PortA::peek(Reg::ODR) == (1u << 5));

    reset_all();
    Pin<PortB, 5>::set();
    CHECK(PortA::peek(Reg::ODR) == 0u && PortB::peek(Reg::ODR) == (1u << 5));
    CHECK(mock::log_n == 1 && mock::log[0].port == PortB::id);

    // 컴파일타임 사실 (static_assert 로도 된다 — 여기선 starter 가 빌드되도록 CHECK)
    CHECK(can_pin<PortA, 0> && can_pin<PortA, 15>);
    CHECK(!can_pin<PortA, 16> && !can_pin<PortA, 31>);
    CHECK(std::is_empty_v<Led>);
    CHECK(!std::same_as<Led, Other> && !std::same_as<Pin<PortA, 5>, Pin<PortB, 5>>);

    // ---------- Part B: toggle 과 인터럽트 ----------
    reset_all();
    PortA::poke(Reg::ODR, 0x00FFu);
    Led::toggle();
    CHECK(PortA::peek(Reg::ODR) == (0x00FFu ^ (1u << 5)));
    CHECK(mock::log_n == 2 && !mock::log[0].is_write && mock::log[0].reg == Reg::ODR &&
          mock::log[1].is_write && mock::log[1].reg == Reg::ODR);
    Led::toggle();
    CHECK(PortA::peek(Reg::ODR) == 0x00FFu);

    // RMW toggle 사이에 ISR 이 다른 핀을 set → 그 set 이 사라진다 (버그를 테스트로 고정)
    reset_all();
    mock::arm_isr(isr_set_other);
    Led::toggle();
    CHECK((PortA::peek(Reg::ODR) & (1u << 5)) != 0);                                // toggle 자체는 됐다
    CHECK((PortA::peek(Reg::ODR) & (1u << 3)) == 0);                                // 그러나 PA3 이 사라졌다!

    // BSRR toggle: 같은 끼어들기에서 PA3 이 살아남는다
    reset_all();
    mock::arm_isr(isr_set_other);
    Led::toggle_bsrr();
    CHECK(PortA::peek(Reg::ODR) == ((1u << 5) | (1u << 3)));
    CHECK(mock::log_n == 3 && !mock::log[0].is_write && mock::log[0].reg == Reg::ODR);
    CHECK(mock::log_n == 3 && mock::log[1].reg == Reg::BSRR && mock::log[1].value == (1u << 3));
    CHECK(mock::log_n == 3 && mock::log[2].reg == Reg::BSRR && mock::log[2].value == (1u << 5));
    CHECK(count_log(Reg::ODR, true) == 0);

    reset_all();
    PortA::poke(Reg::ODR, 0xFFFFu);
    Pin<PortA, 15>::toggle_bsrr();
    CHECK(PortA::peek(Reg::ODR) == 0x7FFFu);
    CHECK(mock::log_n == 2 && mock::log[1].value == 0x8000'0000u);                  // 경곗값: PA15 의 reset 비트 = bit 31
    Pin<PortA, 0>::toggle_bsrr();
    CHECK(mock::log_n == 4 && mock::log[3].value == (1u << 16));

    // 고정 시드 2001 회: RMW 는 유실 > 0, BSRR 은 유실 0
    unsigned fired_rmw = 0, fired_bsrr = 0;
    const unsigned lost_rmw = count_lost_updates<Led::toggle>(0x1234'5678u, 2001, fired_rmw);
    const bool led_after_rmw = Led::output();
    const unsigned lost_bsrr = count_lost_updates<Led::toggle_bsrr>(0x1234'5678u, 2001, fired_bsrr);
    const bool led_after_bsrr = Led::output();
    CHECK(fired_rmw == fired_bsrr && fired_rmw > 0);
    CHECK(lost_rmw > 0);
    CHECK(lost_bsrr == 0);
    CHECK(led_after_rmw && led_after_bsrr);                                         // 홀수 번 토글 → 1

    // BSRR toggle 의 한계: 같은 핀을 ISR 도 토글하면 토글 1회가 사라진다
    reset_all();
    mock::arm_isr(isr_toggle_led);
    Led::toggle_bsrr();
    CHECK(Led::output());                                                           // 직렬화됐다면 0 이어야 하는데 1
    reset_all();
    mock::arm_isr(isr_toggle_led);
    Led::toggle_critical<mock::IrqGuard>();
    CHECK(mock::log_n == 4 && mock::log[1].is_write && mock::log[1].reg == Reg::ODR &&
          !mock::log[2].is_write);                                                  // main 의 RMW 가 끝난 뒤에 ISR 의 read
    CHECK(mock::armed == nullptr && !mock::masked);                                 // pending ISR 이 unmask 때 실행됐다
    CHECK(!Led::output());                                                          // critical section: 토글 2회 = 0

    // ---------- Part C: ActiveLow / PinBus / 드라이버 ----------
    using Status = ActiveLow<Pin<PortC, 13>>;                                       // Blue Pill 의 PC13 LED 처럼 active-low
    reset_all();
    Status::on();
    CHECK(mock::log_n == 1 && mock::log[0].port == PortC::id && mock::log[0].value == (1u << 29));
    CHECK(Status::is_on());
    Status::off();
    CHECK(mock::log_n == 3 && mock::log[2].value == (1u << 13) && !Status::is_on());
    Status::write(true);
    CHECK(Status::is_on());
    CHECK(std::is_empty_v<Status>);

    using D0 = Pin<PortA, 0>; using D1 = Pin<PortA, 3>; using D2 = Pin<PortA, 7>; using D3 = Pin<PortA, 12>;
    using Nibble = PinBus<D0, D1, D2, D3>;
    CHECK(Nibble::mask == 0x1089u);
    CHECK(can_bus<D0, D1> && can_bus<D0>);
    CHECK(!can_bus<D0, Pin<PortB, 3>>);                                             // 포트가 다르면 단일 store 불가능
    CHECK(!can_bus<D0, D1, D0>);                                                    // 같은 핀 두 번

    const unsigned pos[4] = {0, 3, 7, 12};
    bool all_ok = true;
    for (std::uint32_t v = 0; v < 16; ++v) {                                        // 전수 검사
        std::uint32_t on = 0;
        for (unsigned b = 0; b < 4; ++b) if (v & (1u << b)) on |= 1u << pos[b];
        const std::uint32_t expect = on | ((0x1089u & ~on) << 16);
        reset_all();
        PortA::poke(Reg::ODR, 1u << 9);                                             // 버스 밖의 핀
        Nibble::write(v);
        all_ok = all_ok && mock::log_n == 1 && mock::log[0].reg == Reg::BSRR && mock::log[0].value == expect &&
                 PortA::peek(Reg::ODR) == (on | (1u << 9));
    }
    CHECK(all_ok);
    CHECK(Nibble::scatter(0xFFFF'FFF0u) == 0u && Nibble::scatter(0xFu) == 0x1089u); // 폭을 넘는 윗비트는 무시
    constexpr std::uint32_t folded = PinBus<Pin<PortA, 2>>::scatter(1u);            // scatter 는 constexpr
    CHECK(folded == 0b100u);

    using Rs = Pin<PortB, 1>; using En = Pin<PortB, 2>;
    using Lcd = Lcd4<Rs, En, Nibble>;
    reset_all();
    Lcd::send(0xA5u, true);
    const struct { unsigned port; std::uint32_t value; } seq[] = {
        {PortB::id, 1u << 1},                                                       // RS=1
        {PortA::id, Nibble::scatter(0xAu) | ((0x1089u & ~Nibble::scatter(0xAu)) << 16)},
        {PortB::id, 1u << 2},  {PortB::id, 1u << 18},                               // E 펄스
        {PortA::id, Nibble::scatter(0x5u) | ((0x1089u & ~Nibble::scatter(0x5u)) << 16)},
        {PortB::id, 1u << 2},  {PortB::id, 1u << 18},
    };
    bool seq_ok = mock::log_n == 7;
    for (unsigned i = 0; seq_ok && i < 7; ++i)
        seq_ok = mock::log[i].is_write && mock::log[i].reg == Reg::BSRR &&
                 mock::log[i].port == seq[i].port && mock::log[i].value == seq[i].value;
    CHECK(seq_ok);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
