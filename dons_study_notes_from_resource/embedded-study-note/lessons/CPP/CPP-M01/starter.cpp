// CPP-M01 타입 안전 레지스터 맵: Reg<Addr, T, Access>
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
//
// 설계 세 줄 요약
//   1) 주소는 정수 NTTP. reinterpret_cast 는 상수식에 못 쓰므로, 캐스트는 "접근하는 순간" Bus 안에서 한다.
//   2) 접근 권한은 태그 타입(RO/WO/RW) + requires 절 → 잘못된 접근은 "그런 함수 없음" 컴파일 에러.
//   3) Bus 는 정책 템플릿 인자. 실제 칩 = MmioBus, 테스트 = MockBus(바이트 배열 + 접근 로그). API 는 똑같다.
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <type_traits>

// 레지스터 한 칸으로 쓸 수 있는 타입 (규칙 자체는 CPP-B12 참고, Cortex-M 이라 4바이트까지)
template <typename T>
concept RegWord = std::unsigned_integral<T> && !std::same_as<T, bool> && (sizeof(T) <= 4);

// ---------------- 접근 권한 태그 ----------------
struct RO { static constexpr bool can_read = true,  can_write = false; };
struct WO { static constexpr bool can_read = false, can_write = true;  };
struct RW { static constexpr bool can_read = true,  can_write = true;  };

template <typename A>
concept AccessPolicy = requires {
    { A::can_read }  -> std::convertible_to<bool>;
    { A::can_write } -> std::convertible_to<bool>;
};

// ---------------- 진짜 버스: 캐스트는 여기 한 곳에만 ----------------
struct MmioBus {
    template <RegWord T> [[nodiscard]] static T read(std::uintptr_t a) noexcept {
        return *reinterpret_cast<const volatile T*>(a);
    }
    template <RegWord T> static void write(std::uintptr_t a, T v) noexcept {
        *reinterpret_cast<volatile T*>(a) = v;
    }
};

// ---------------- 가짜 버스: 주소를 그대로 받아 배열 칸으로 번역하고 전부 기록 ----------------
struct MockBus {
    static constexpr std::uintptr_t BASE = 0x4000'4400u;   // 실제 칩과 같은 주소를 그대로 쓴다
    static constexpr std::size_t    SIZE = 0x40;

    struct Record { std::uintptr_t addr; std::size_t width; bool write; std::uint32_t value; };
    static constexpr std::size_t LOG_CAP = 256;

    alignas(8) static inline std::uint8_t    mem[SIZE] = {};
    static inline std::array<Record, LOG_CAP> log{};
    static inline std::size_t log_n  = 0;
    static inline unsigned    faults = 0;                   // 범위 밖/비정렬 (실제 칩이면 BusFault)
    static inline void (*hook)(const Record&) = nullptr;    // 접근 직후 호출되는 "장치 모델"

    static void reset() {
        std::memset(mem, 0, SIZE);
        log_n = 0; faults = 0; hook = nullptr;
    }
    static std::uint8_t* slot(std::uintptr_t a, std::size_t w) {
        if (a < BASE || a - BASE > SIZE - w || (a & (w - 1)) != 0) { ++faults; return nullptr; }
        return &mem[a - BASE];
    }
    static void after(const Record& r) {
        if (log_n < LOG_CAP) log[log_n++] = r;
        if (hook) hook(r);
    }

    // Part A: slot() → memcpy → after({주소, 폭, 쓰기여부, 값}). slot 이 nullptr 이면 읽기는 0, 쓰기는 무시.
    template <RegWord T> [[nodiscard]] static T read(std::uintptr_t a) {
        (void)a;
        return T{};  // TODO(Part A)
    }
    template <RegWord T> static void write(std::uintptr_t a, T v) {
        (void)a; (void)v;  // TODO(Part A)
    }

    // 하드웨어(테스트벤치) 쪽 직접 접근: 로그·hook 없음
    template <RegWord T> static void poke(std::uintptr_t a, T v) { std::memcpy(&mem[a - BASE], &v, sizeof v); }
    template <RegWord T> static T    peek(std::uintptr_t a)      { T v; std::memcpy(&v, &mem[a - BASE], sizeof v); return v; }
};

// Part B: "버스 자격" — T 를 인자로 받는 정적 read/write 멤버 템플릿이 있어야 한다.
template <typename B>
concept RegisterBus = true;  // TODO(Part B): 너무 느슨하다

// ---------------- Reg: 주소·폭·권한·버스가 전부 타입에 있다 (객체 상태 없음) ----------------
template <std::uintptr_t Addr, typename T, typename A, typename Bus = MmioBus>
    requires RegWord<T> && AccessPolicy<A> && RegisterBus<Bus>  // TODO(Part B): 자연 정렬 제약을 추가
struct Reg {
    using value_type = T;
    using access     = A;
    using bus        = Bus;
    static constexpr std::uintptr_t address = Addr;
    static constexpr std::size_t    width   = sizeof(T);

    [[nodiscard]] static T read() noexcept  // TODO(Part B): 읽을 수 없으면 이 함수가 없어야 한다 (requires)
    {
        return T{};  // TODO(Part A): Bus 에 Addr 와 T 를 넘긴다
    }

    static void write(T v) noexcept  // TODO(Part B): 쓸 수 없으면 이 함수가 없어야 한다 (requires)
    {
        (void)v;  // TODO(Part A)
    }

    // read-modify-write: 정확히 읽기 1번 + 쓰기 1번. 읽을 수 없는/쓸 수 없는 레지스터엔 존재하면 안 된다.
    static void modify(T clear_mask, T set_mask) noexcept  // TODO(Part B): requires
    {
        (void)clear_mask; (void)set_mask;  // TODO(Part B)
    }

    static void set_bits(T m) noexcept  // TODO(Part B): requires
    {
        (void)m;  // TODO(Part B): modify 로
    }

    static void clear_bits(T m) noexcept  // TODO(Part B): requires
    {
        (void)m;  // TODO(Part B): modify 로
    }
};

// ---------------- Part C: 레지스터 맵 한 벌 (Bus, Base 만 바꿔 끼운다) ----------------
template <typename Bus, std::uintptr_t Base = 0x4000'4400u>
struct UartMap {
    using SR  = Reg<Base + 0x00, std::uint32_t, RO, Bus>;   // 상태
    using RDR = Reg<Base + 0x04, std::uint8_t,  RO, Bus>;   // 읽으면 RX FIFO 가 한 칸 빠진다 (부작용!)
    using TDR = Reg<Base + 0x08, std::uint8_t,  WO, Bus>;   // 송신 FIFO 로 push
    using CR  = Reg<Base + 0x0C, std::uint32_t, RW, Bus>;
    using BRR = Reg<Base + 0x10, std::uint16_t, RW, Bus>;
    using ICR = Reg<Base + 0x14, std::uint32_t, WO, Bus>;   // 플래그 클리어 전용
    using ID  = Reg<Base + 0x18, std::uint32_t, RO, Bus>;
};

namespace uart {
constexpr std::uint32_t SR_RXNE = 1u << 0;
constexpr std::uint32_t SR_TXE  = 1u << 1;
constexpr std::uint32_t SR_ORE  = 1u << 3;
constexpr std::uint32_t CR_EN   = 1u << 0;
constexpr std::uint32_t CHIP_ID = 0x5541'5254u;   // "UART"
}  // namespace uart

// ID 확인 → BRR 쓰기 → CR 의 EN 을 RMW 로 set. ID 가 다르면 아무것도 쓰지 않고 false.
template <typename Map>
bool uart_init(std::uint16_t brr) noexcept {
    (void)brr;
    return false;  // TODO(Part C)
}

// 바이트마다 SR.TXE 를 폴링(최대 max_polls 회) 후 TDR 에 쓴다. 시간 초과면 그때까지 보낸 수를 돌려준다.
template <typename Map>
std::size_t uart_send(std::span<const std::uint8_t> data, unsigned max_polls) noexcept {
    (void)data; (void)max_polls;
    return 0;  // TODO(Part C)
}

// SR.RXNE 가 서 있는 동안 RDR 을 바이트당 "정확히 한 번" 읽는다. out 이 차면 멈춘다.
template <typename Map>
std::size_t uart_recv(std::span<std::uint8_t> out) noexcept {
    (void)out;
    return 0;  // TODO(Part C)
}

// SR.ORE 가 서 있으면 ICR(쓰기 전용)에 같은 비트를 써서 지우고 true.
template <typename Map>
bool uart_clear_overrun() noexcept {
    return false;  // TODO(Part C)
}

// ---------------- 테스트 하네스 ----------------
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

// "이 호출이 컴파일되는가?" → bool. 템플릿 안이어야 잘못된 식이 하드 에러가 아니라 false 가 된다.
template <typename R> concept CanRead   = requires { { R::read() } -> std::same_as<typename R::value_type>; };
template <typename R> concept CanWrite  = requires(typename R::value_type v) { R::write(v); };
template <typename R> concept CanModify = requires(typename R::value_type v) { R::modify(v, v); };
template <typename R> concept CanSet    = requires(typename R::value_type v) { R::set_bits(v); };
template <std::uintptr_t A, typename T> constexpr bool reg_ok = requires { typename Reg<A, T, RW, MockBus>; };
template <typename B> constexpr bool bus_ok = requires { typename Reg<MockBus::BASE, std::uint32_t, RW, B>; };

constexpr std::uintptr_t B = MockBus::BASE;
using R32   = Reg<B + 0x20, std::uint32_t, RW, MockBus>;
using R16   = Reg<B + 0x24, std::uint16_t, RW, MockBus>;
using R8    = Reg<B + 0x26, std::uint8_t,  RW, MockBus>;
using R8n   = Reg<B + 0x27, std::uint8_t,  RW, MockBus>;   // R8 바로 옆 바이트
using RoReg = Reg<B + 0x28, std::uint32_t, RO, MockBus>;
using WoReg = Reg<B + 0x2C, std::uint32_t, WO, MockBus>;
using Off   = Reg<B + MockBus::SIZE, std::uint32_t, RW, MockBus>;   // 배열 끝 바로 다음
using Mock  = UartMap<MockBus>;
using Real  = UartMap<MmioBus>;

struct NotABus {};
struct NoWriteBus { template <RegWord T> static T read(std::uintptr_t) { return T{}; } };
struct ByValueBus {   // write 가 void 가 아니다
    template <RegWord T> static T read(std::uintptr_t) { return T{}; }
    template <RegWord T> static int write(std::uintptr_t, T) { return 0; }
};

// 컴파일 타임 증명: 규칙을 다 채운 뒤 0 → 1 로 바꾼다.
#if 0  // TODO(Part B)
static_assert(CanRead<RoReg> && !CanWrite<RoReg> && !CanModify<RoReg> && !CanSet<RoReg>);
static_assert(!CanRead<WoReg> && CanWrite<WoReg> && !CanModify<WoReg> && !CanSet<WoReg>);
static_assert(CanRead<R32> && CanWrite<R32> && CanModify<R32> && CanSet<R32>);
static_assert(!reg_ok<B + 2, std::uint32_t> && !reg_ok<B + 1, std::uint16_t> && reg_ok<B + 1, std::uint8_t>);
static_assert(RegisterBus<MmioBus> && RegisterBus<MockBus>);
static_assert(!RegisterBus<NotABus> && !RegisterBus<NoWriteBus> && !RegisterBus<ByValueBus>);
#endif

// ---- 고정 시드 PRNG (결정적 랜덤 테스트) ----
static std::uint32_t xorshift32(std::uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

// ---- 가짜 UART 장치 모델: 버스 접근 "직후" hook 으로 불린다 ----
namespace dev {
static std::uint8_t rx[512]; static std::size_t rx_head, rx_len;
static std::uint8_t tx[512]; static std::size_t tx_len;
static unsigned busy_left, busy_reload;          // TDR 쓰기 후 SR 폴링 몇 번 동안 TXE=0 인가

static void refresh_rx() {
    std::uint32_t sr = MockBus::peek<std::uint32_t>(Mock::SR::address);
    if (rx_head < rx_len) { MockBus::poke<std::uint8_t>(Mock::RDR::address, rx[rx_head]); sr |= uart::SR_RXNE; }
    else sr &= ~uart::SR_RXNE;
    MockBus::poke<std::uint32_t>(Mock::SR::address, sr);
}
static void model(const MockBus::Record& r) {
    std::uint32_t sr = MockBus::peek<std::uint32_t>(Mock::SR::address);
    if (r.write && r.addr == Mock::TDR::address) {
        if (tx_len < sizeof tx) tx[tx_len++] = static_cast<std::uint8_t>(r.value);
        busy_left = busy_reload;
        if (busy_left) MockBus::poke<std::uint32_t>(Mock::SR::address, sr & ~uart::SR_TXE);
    } else if (r.write && r.addr == Mock::ICR::address) {
        MockBus::poke<std::uint32_t>(Mock::SR::address, sr & ~r.value);
    } else if (!r.write && r.addr == Mock::SR::address && busy_left) {
        if (--busy_left == 0) MockBus::poke<std::uint32_t>(Mock::SR::address, sr | uart::SR_TXE);
    } else if (!r.write && r.addr == Mock::RDR::address) {
        if (rx_head < rx_len) ++rx_head;
        refresh_rx();
    }
}
static void start(unsigned reload) {
    MockBus::reset();
    rx_head = rx_len = tx_len = 0; busy_left = 0; busy_reload = reload;
    MockBus::poke<std::uint32_t>(Mock::ID::address, uart::CHIP_ID);
    MockBus::poke<std::uint32_t>(Mock::SR::address, uart::SR_TXE);
    MockBus::hook = model;
}
static void feed(std::span<const std::uint8_t> bytes) {
    for (std::uint8_t b : bytes) rx[rx_len++] = b;
    refresh_rx();
}
}  // namespace dev

static std::size_t count_log(std::uintptr_t addr, bool write) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < MockBus::log_n; ++i)
        if (MockBus::log[i].addr == addr && MockBus::log[i].write == write) ++n;
    return n;
}

// Part A 표 기반: 폭마다 경계값 왕복 + "접근 1번당 로그 1줄"
template <typename R>
static void check_width_table() {
    using T = typename R::value_type;
    const T max = static_cast<T>(~T{0});
    const T table[] = {T{0}, T{1}, T{0x7F}, T{0x80}, max, static_cast<T>(max ^ 1u)};
    for (T v : table) {
        MockBus::reset();
        R::write(v);
        const T back = R::read();
        const auto& w = MockBus::log[0];
        const auto& r = MockBus::log[1];
        CHECK(back == v && MockBus::peek<T>(R::address) == v);
        CHECK(MockBus::log_n == 2 && w.write && !r.write);
        CHECK(w.addr == R::address && w.width == sizeof(T) && w.value == v && r.width == sizeof(T));
    }
}

struct ModifyCase { std::uint32_t init, clear, set, expect; };

int main() {
    // Part A — 타입에 담긴 정보
    CHECK(R32::address == B + 0x20 && R32::width == 4 && R16::width == 2 && R8::width == 1);
    CHECK((std::is_same_v<R16::value_type, std::uint16_t>));
    CHECK((std::is_same_v<Reg<0x4000'0000u, std::uint32_t, RW>::bus, MmioBus>));   // 기본 버스는 진짜
    CHECK(std::is_empty_v<R32> && sizeof(R32) == 1);                               // 상태 없음

    // Part A — 표 기반 왕복 (폭 3종 × 경계값 6개)
    check_width_table<R32>();
    check_width_table<R16>();
    check_width_table<R8>();

    // Part A — 주소 번역과 옆 칸 보존
    MockBus::reset();
    MockBus::poke<std::uint8_t>(R8n::address, 0xA5);
    R8::write(0x3C);
    CHECK(MockBus::mem[0x26] == 0x3C && MockBus::mem[0x27] == 0xA5);               // 옆 바이트 그대로
    MockBus::poke<std::uint32_t>(RoReg::address, 0xCAFE'F00Du);
    CHECK(RoReg::read() == 0xCAFE'F00Du);                                           // 하드웨어가 올린 값을 읽는다
    WoReg::write(0x1234'5678u);
    CHECK(MockBus::peek<std::uint32_t>(WoReg::address) == 0x1234'5678u);

    // Part A — volatile 흉내: 같은 값을 두 번 읽어도 접근은 두 번 일어나야 한다
    MockBus::reset();
    (void)RoReg::read(); (void)RoReg::read();
    CHECK(count_log(RoReg::address, false) == 2);

    // Part A — 범위 밖 접근은 fault 로 세고 배열은 건드리지 않는다
    MockBus::reset();
    Off::write(0xFFFF'FFFFu);
    CHECK(Off::read() == 0u && MockBus::faults == 2 && MockBus::log_n == 0);

    // Part B — 권한이 API 를 결정한다 (static_assert 와 같은 내용을 런타임 줄로도 찍는다)
    CHECK(CanRead<RoReg>);
    CHECK(!CanWrite<RoReg>);
    CHECK(!CanModify<RoReg>);
    CHECK(!CanRead<WoReg>);
    CHECK(CanWrite<WoReg>);
    CHECK(!CanModify<WoReg>);
    CHECK(!CanSet<WoReg>);
    CHECK(CanRead<R32> && CanWrite<R32> && CanModify<R32> && CanSet<R32>);

    // Part B — 정렬 / 타입 / 버스 제약은 Reg 를 "이름 짓는" 순간 검사된다
    CHECK(!(reg_ok<B + 2, std::uint32_t>));
    CHECK(!(reg_ok<B + 1, std::uint16_t>));
    CHECK((reg_ok<B + 1, std::uint8_t>));
    CHECK((reg_ok<B + 2, std::uint16_t>));
    CHECK(!(reg_ok<B, int>) && !(reg_ok<B, bool>) && !(reg_ok<B, std::uint64_t>));
    CHECK(RegisterBus<MmioBus> && RegisterBus<MockBus>);
    CHECK(!RegisterBus<NotABus> && !RegisterBus<NoWriteBus> && !RegisterBus<ByValueBus>);
    CHECK(!bus_ok<NotABus> && bus_ok<MockBus>);

    // Part B — modify 표: 경계값
    static constexpr ModifyCase cases[] = {
        {0x0000'0000u, 0x0000'0000u, 0x0000'0000u, 0x0000'0000u},
        {0xFFFF'FFFFu, 0x0000'00F0u, 0x0000'0030u, 0xFFFF'FF3Fu},
        {0xFFFF'FFFFu, 0xFFFF'FFFFu, 0x0000'0000u, 0x0000'0000u},
        {0x0000'0000u, 0xFFFF'FFFFu, 0xFFFF'FFFFu, 0xFFFF'FFFFu},
        {0x8000'0001u, 0x8000'0000u, 0x0000'0002u, 0x0000'0003u},
        {0x1234'5678u, 0x0000'FF00u, 0x0000'AB00u, 0x1234'AB78u},
    };
    for (const auto& c : cases) {
        MockBus::reset();
        MockBus::poke<std::uint32_t>(R32::address, c.init);
        R32::modify(c.clear, c.set);
        CHECK(MockBus::peek<std::uint32_t>(R32::address) == c.expect);
        CHECK(MockBus::log_n == 2 && !MockBus::log[0].write && MockBus::log[1].write);   // R 1번 → W 1번
    }

    // Part B — 좁은 폭: ~clear 의 정수 승격이 값을 망치지 않는다
    MockBus::reset();
    MockBus::poke<std::uint8_t>(R8::address, 0xFF);
    R8::modify(0x80, 0x01);
    CHECK(MockBus::peek<std::uint8_t>(R8::address) == 0x7F);
    MockBus::poke<std::uint16_t>(R16::address, 0xFFFF);
    R16::clear_bits(0x8001);
    CHECK(MockBus::peek<std::uint16_t>(R16::address) == 0x7FFE);
    R16::set_bits(0x8000);
    CHECK(MockBus::peek<std::uint16_t>(R16::address) == 0xFFFE);

    // Part B — 고정 시드 랜덤: 그림자 모델과 2000 라운드 비교
    {
        MockBus::reset();
        std::uint32_t s = 0x1234'5678u, shadow32 = 0;
        std::uint8_t shadow8 = 0;
        unsigned bad_value = 0, bad_count = 0;
        for (int i = 0; i < 2000; ++i) {
            const std::uint32_t clr = xorshift32(s), set = xorshift32(s);
            MockBus::log_n = 0;
            R32::modify(clr, set);
            shadow32 = (shadow32 & ~clr) | set;
            if (MockBus::log_n != 2) ++bad_count;
            if (MockBus::peek<std::uint32_t>(R32::address) != shadow32) ++bad_value;
            const auto c8 = static_cast<std::uint8_t>(clr), s8 = static_cast<std::uint8_t>(set);
            R8::modify(c8, s8);
            shadow8 = static_cast<std::uint8_t>((shadow8 & ~c8) | s8);
            if (MockBus::peek<std::uint8_t>(R8::address) != shadow8) ++bad_value;
        }
        CHECK(bad_value == 0 && bad_count == 0);
    }

    // Part C — 같은 맵, 같은 주소: 버스만 다르다
    CHECK(Mock::SR::address == Real::SR::address && Mock::ID::address == Real::ID::address);
    CHECK(!(std::is_same_v<Mock::SR, Real::SR>));
    CHECK(!CanWrite<Mock::SR> && !CanRead<Mock::TDR> && !CanRead<Mock::ICR> && !CanWrite<Mock::RDR>);
    (void)&uart_send<Real>;  (void)&uart_recv<Real>;                  // MMIO 경로도 같은 소스로 컴파일된다
    (void)&uart_init<Real>;  (void)&uart_clear_overrun<Real>;

    // Part C — init: 접근 순서가 정확히 ID(R) → BRR(W) → CR(R) → CR(W)
    dev::start(0);
    CHECK(uart_init<Mock>(0x0683u));
    CHECK(MockBus::log_n == 4);
    CHECK(MockBus::log[0].addr == Mock::ID::address  && !MockBus::log[0].write);
    CHECK(MockBus::log[1].addr == Mock::BRR::address &&  MockBus::log[1].write && MockBus::log[1].width == 2);
    CHECK(MockBus::log[2].addr == Mock::CR::address  && !MockBus::log[2].write);
    CHECK(MockBus::log[3].addr == Mock::CR::address  &&  MockBus::log[3].write && MockBus::log[3].value == uart::CR_EN);
    dev::start(0);
    MockBus::poke<std::uint32_t>(Mock::ID::address, 0xDEAD'BEEFu);
    CHECK(!uart_init<Mock>(0x0683u) && MockBus::log_n == 1);                       // 다른 칩: 읽기 1번뿐

    // Part C — send: 폴링 + 8비트 쓰기 + 타임아웃
    {
        const std::uint8_t msg[] = {0x10, 0x20, 0x30};
        dev::start(2);                                                             // 바이트마다 SR 2번 동안 TXE=0
        CHECK(uart_send<Mock>(msg, 10) == 3u);
        CHECK(dev::tx_len == 3 && dev::tx[0] == 0x10 && dev::tx[2] == 0x30);
        CHECK(count_log(Mock::TDR::address, true) == 3);
        CHECK(MockBus::log[1].width == 1);                                         // log[0]=SR 폴링, log[1]=TDR 쓰기
        CHECK(count_log(Mock::SR::address, false) == 7);                           // 1 + (2+1) + (2+1)
        dev::start(5);
        CHECK(uart_send<Mock>(msg, 3) == 1u);                                      // 첫 바이트 뒤 TXE 가 안 돌아온다
        dev::start(0);
        MockBus::poke<std::uint32_t>(Mock::SR::address, 0u);
        CHECK(uart_send<Mock>(msg, 4) == 0u && count_log(Mock::SR::address, false) == 4);
    }

    // Part C — recv: RDR 는 바이트당 정확히 한 번 (두 번 읽으면 데이터가 사라진다)
    {
        const std::uint8_t in[] = {0xAA, 0xBB, 0xCC, 0xDD};
        std::uint8_t out[8] = {};
        dev::start(0);
        dev::feed(in);
        CHECK(uart_recv<Mock>(std::span(out, 2)) == 2u);                           // 버퍼가 차면 멈춘다
        CHECK(out[0] == 0xAA && out[1] == 0xBB);
        CHECK(uart_recv<Mock>(std::span(out, 8)) == 2u && out[0] == 0xCC && out[1] == 0xDD);
        CHECK(count_log(Mock::RDR::address, false) == 4);
        CHECK(uart_recv<Mock>(std::span(out, 8)) == 0u);                           // 비었다
        CHECK(count_log(Mock::RDR::address, false) == 4);
    }

    // Part C — overrun: 쓰기 전용 ICR 로 지운다 (RMW 없음)
    dev::start(0);
    CHECK(!uart_clear_overrun<Mock>() && MockBus::log_n == 1);
    MockBus::poke<std::uint32_t>(Mock::SR::address, uart::SR_TXE | uart::SR_ORE);
    CHECK(uart_clear_overrun<Mock>());
    CHECK(MockBus::peek<std::uint32_t>(Mock::SR::address) == uart::SR_TXE);        // ORE 만 지워졌다
    CHECK(count_log(Mock::ICR::address, true) == 1 && count_log(Mock::ICR::address, false) == 0);

    // Part C — 고정 시드 랜덤 스트림: 쪼개 받아도 순서·개수가 같다
    {
        std::uint32_t s = 0xC0DE'0001u;
        unsigned bad = 0;
        for (int round = 0; round < 200; ++round) {
            dev::start(xorshift32(s) % 4);
            std::uint8_t in[64], out[64] = {}, tx_msg[32];
            const std::size_t len = 1 + xorshift32(s) % 64;
            for (std::size_t i = 0; i < len; ++i) in[i] = static_cast<std::uint8_t>(xorshift32(s));
            dev::feed(std::span(in, len));
            std::size_t got = 0;
            MockBus::log_n = 0;
            for (int guard = 0; got < len && guard < 2000; ++guard) {   // 구현 전에도 무한 루프가 되지 않게
                const std::size_t want = 1 + xorshift32(s) % 7;
                const std::size_t room = want < len - got ? want : len - got;
                got += uart_recv<Mock>(std::span(out + got, room));
                MockBus::log_n = 0;
            }
            if (std::memcmp(in, out, len) != 0) ++bad;
            for (auto& b : tx_msg) b = static_cast<std::uint8_t>(xorshift32(s));
            if (uart_send<Mock>(tx_msg, 16) != sizeof tx_msg) ++bad;
            if (dev::tx_len != sizeof tx_msg || std::memcmp(dev::tx, tx_msg, sizeof tx_msg) != 0) ++bad;
        }
        CHECK(bad == 0);
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
