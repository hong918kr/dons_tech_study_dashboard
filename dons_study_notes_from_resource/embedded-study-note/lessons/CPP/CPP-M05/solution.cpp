// CPP-M05 CRTP 정적 다형성 드라이버 인터페이스
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
//
// 실제 칩:   hw::read/hw::write 자리가 *(volatile uint32_t*)addr 한 줄 (demo_inline.cpp 참고)
// 이 레슨:   hw:: 가 USART 2개를 흉내 낸다. DR 에 쓴 바이트는 tx[] (= 선) 으로 나가고, 그 뒤 busy_polls 번은 TXE 가 0.
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <type_traits>

using u8  = std::uint8_t;
using u32 = std::uint32_t;

static int g_passed = 0, g_total = 0;
#define CHECK(expr)                                                     \
    do {                                                                \
        ++g_total;                                                      \
        if (expr) { ++g_passed; std::printf("PASS  %s\n", #expr); }     \
        else      { std::printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } \
    } while (0)

// ---------------- 가짜 USART (STM32F4 레지스터 배치) ----------------
namespace hw {
constexpr std::uintptr_t USART1_BASE = 0x4001'1000u, USART2_BASE = 0x4000'4400u;
constexpr std::uintptr_t SR = 0x00, DR = 0x04, BRR = 0x08, CR1 = 0x0C;
constexpr u32 SR_RXNE = 1u << 5, SR_TXE = 1u << 7;
constexpr u32 CR1_RE = 1u << 2, CR1_TE = 1u << 3, CR1_UE = 1u << 13;

struct Device {
    u32 brr, cr1;
    u8 tx[300]; std::size_t tx_n;           // DR 에 쓴 바이트 = 선으로 나간 바이트
    u8 rx[16];  std::size_t rx_r, rx_n;     // 수신 큐
    unsigned busy_polls, busy_left;         // DR 쓰기 뒤 TXE 가 0 인 SR 읽기 횟수
    bool stuck;                             // true 면 TXE 가 영원히 0 (송신기 고장)
};
inline Device devs[2];
inline Device& dev(std::uintptr_t addr) {
    return (addr & ~std::uintptr_t{0x3F}) == USART1_BASE ? devs[0] : devs[1];
}
inline void reset() { std::memset(devs, 0, sizeof devs); }
inline void feed_rx(Device& d, u8 b) { if (d.rx_n < sizeof d.rx) d.rx[d.rx_n++] = b; }

inline u32 read(std::uintptr_t addr) {
    Device& d = dev(addr);
    switch (addr & 0x3F) {
    case SR: {
        u32 v = d.rx_r < d.rx_n ? SR_RXNE : 0;
        if (d.stuck) return v;
        if (d.busy_left > 0) { --d.busy_left; return v; }
        return v | SR_TXE;
    }
    case DR:  return d.rx_r < d.rx_n ? d.rx[d.rx_r++] : 0;
    case BRR: return d.brr;
    case CR1: return d.cr1;
    default:  return 0;
    }
}
inline void write(std::uintptr_t addr, u32 v) {
    Device& d = dev(addr);
    switch (addr & 0x3F) {
    case DR:
        if (d.tx_n < sizeof d.tx) d.tx[d.tx_n++] = static_cast<u8>(v);
        d.busy_left = d.busy_polls;
        break;
    case BRR: d.brr = v; break;
    case CR1: d.cr1 = v; break;
    default:  break;
    }
}
}  // namespace hw

// ---------------- Part A/B: CRTP 인터페이스 ----------------
// 파생 클래스가 구현할 것: bool init_impl(u32), bool put_impl(u8), bool get_impl(u8&)
// 공개 API 와 *_impl 의 이름이 다른 이유는 레슨 5장 참고 (같은 이름이면 무한 재귀).
template <class Derived>
class UartBase {
public:
    bool init(u32 baud)   { return self().init_impl(baud); }
    bool put(u8 b)        { return self().put_impl(b); }
    bool try_get(u8& out) { return self().get_impl(out); }

    // 첫 실패에서 멈추고, 성공한 바이트 수를 돌려준다. (공통 코드: 파생 클래스마다 다시 쓰지 않는다)
    std::size_t write(std::span<const u8> data) {
        std::size_t n = 0;
        for (u8 b : data) { if (!put(b)) break; ++n; }
        return n;
    }
    // NUL 까지 보내고, 성공한 바이트 수를 돌려준다.
    std::size_t print(const char* s) {
        std::size_t n = 0;
        while (s[n] != '\0' && put(static_cast<u8>(s[n]))) ++n;
        return n;
    }

    // Part B: Derived 가 *_impl 3개를 시그니처대로 가졌는가? (private impl 도 friend 라서 보인다)
    static consteval bool implements_interface() {
        return requires(Derived& d, u32 baud, u8 b, u8& out) {
            { d.init_impl(baud) } -> std::same_as<bool>;
            { d.put_impl(b) }     -> std::same_as<bool>;
            { d.get_impl(out) }   -> std::same_as<bool>;
        };
    }

private:
    friend Derived;                     // Part B: Derived 만 이 생성자를 부를 수 있다
    UartBase() = default;               // Part B: private → class B : UartBase<A> 는 생성 불가
    Derived& self() {
        static_assert(implements_interface(), "UartBase<D>: D must define bool init_impl(u32), put_impl(u8), get_impl(u8&)");
        return static_cast<Derived&>(*this);
    }
};

// ---------------- Part A: 구현 1 — 테스트용 MockUart ----------------
class MockUart : public UartBase<MockUart> {
public:
    static constexpr std::size_t kCap = 64;
    u8 tx[kCap]{}; std::size_t tx_n = 0;
    u8 rx[16]{};   std::size_t rx_r = 0, rx_n = 0;
    u32 baud = 0;  unsigned put_calls = 0;
    void feed(u8 b) { if (rx_n < sizeof rx) rx[rx_n++] = b; }

private:
    friend UartBase<MockUart>;          // UartBase 만 private impl 을 부른다
    bool init_impl(u32 b) { baud = b; return b != 0; }
    bool put_impl(u8 b) {
        ++put_calls;
        if (tx_n == kCap) return false;
        tx[tx_n++] = b;
        return true;
    }
    bool get_impl(u8& out) {
        if (rx_r == rx_n) return false;
        out = rx[rx_r++];
        return true;
    }
};

// ---------------- Part A: 구현 2 — Stm32Uart (포트마다 다른 타입) ----------------
template <std::uintptr_t Base>
class Stm32Uart : public UartBase<Stm32Uart<Base>> {
public:
    static constexpr u32 kPclk = 16'000'000;    // HSI 16 MHz 가정
    static constexpr unsigned kMaxPolls = 8;    // TXE 대기 상한 (타임아웃)

private:
    friend UartBase<Stm32Uart<Base>>;
    // BRR = round(PCLK / baud) (16배 오버샘플링), 그 뒤 CR1 = UE|TE|RE. baud == 0 이면 아무것도 쓰지 않고 false.
    bool init_impl(u32 baud) {
        if (baud == 0) return false;
        hw::write(Base + hw::BRR, (kPclk + baud / 2) / baud);
        hw::write(Base + hw::CR1, hw::CR1_UE | hw::CR1_TE | hw::CR1_RE);
        return true;
    }
    // TXE 를 최대 kMaxPolls 번 확인. 켜지면 DR 에 쓰고 true, 끝까지 안 켜지면 false.
    bool put_impl(u8 b) {
        for (unsigned i = 0; i < kMaxPolls; ++i) {
            if (hw::read(Base + hw::SR) & hw::SR_TXE) { hw::write(Base + hw::DR, b); return true; }
        }
        return false;
    }
    // RXNE 면 DR 을 읽어 out 에 넣고 true, 아니면 false (블로킹 없음).
    bool get_impl(u8& out) {
        if ((hw::read(Base + hw::SR) & hw::SR_RXNE) == 0) return false;
        out = static_cast<u8>(hw::read(Base + hw::DR));
        return true;
    }
};
using Usart1 = Stm32Uart<hw::USART1_BASE>;
using Usart2 = Stm32Uart<hw::USART2_BASE>;

// ---------------- Part B: 오용 타입들 (하네스가 trait 로 검사) ----------------
struct WrongDerived : UartBase<MockUart> {};               // 복붙 실수: 자기 이름이 아니라 MockUart
struct NoPut : UartBase<NoPut> {                           // put_impl 빠뜨림
    bool init_impl(u32) { return true; }
    bool get_impl(u8&)  { return false; }
};
struct WrongRet : UartBase<WrongRet> {                     // 반환 타입이 void
    bool init_impl(u32) { return true; }
    void put_impl(u8)   {}
    bool get_impl(u8&)  { return false; }
};
// TODO(Part B): 아래 CHECK 들이 통과하면, 같은 사실을 static_assert 로 못 박는다
static_assert(!std::is_default_constructible_v<WrongDerived>);
static_assert(!std::is_default_constructible_v<UartBase<MockUart>>);
static_assert(UartBase<MockUart>::implements_interface() && UartBase<Usart1>::implements_interface());
static_assert(!UartBase<NoPut>::implements_interface() && !UartBase<WrongRet>::implements_interface());

// ---------------- Part C: 프레임 전송 (어떤 UART 든) ----------------
// 형식: 0x02, LEN, payload..., XOR(LEN ^ payload 전부). payload > 255 면 한 바이트도 안 보내고 false. put 실패도 false.
template <class D>
bool send_frame(UartBase<D>& u, std::span<const u8> payload) {
    if (payload.size() > 255) return false;
    u8 x = static_cast<u8>(payload.size());
    if (!u.put(0x02) || !u.put(x)) return false;
    for (u8 b : payload) {
        if (!u.put(b)) return false;
        x = static_cast<u8>(x ^ b);
    }
    return u.put(x);
}

// ---------------- Part C: 이질 컨테이너 우회 — 손으로 만든 타입 지우기 ----------------
struct AnyUart {
    void* obj;
    bool (*put_fn)(void*, u8);
    bool put(u8 b) const { return put_fn ? put_fn(obj, b) : false; }
};
// UartBase<D>& 를 AnyUart 로 포장한다. (캡처 없는 람다 → 함수 포인터, D 마다 하나 생성)
template <class D>
AnyUart make_any(UartBase<D>& u) {
    return AnyUart{&u, [](void* p, u8 b) { return static_cast<UartBase<D>*>(p)->put(b); }};
}
// ports 각각에 data 전체를 보낸다. "전부 성공한 포트 수"를 돌려준다. 한 포트의 실패가 다른 포트를 막으면 안 된다.
inline std::size_t broadcast(std::span<const AnyUart> ports, std::span<const u8> data) {
    std::size_t ok = 0;
    for (const AnyUart& p : ports) {
        bool all = true;
        for (u8 b : data) {
            if (!p.put(b)) { all = false; break; }
        }
        if (all) ++ok;
    }
    return ok;
}

// ---------------- 테스트 ----------------
static u32 xorshift32(u32& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

template <std::size_t N>
static bool bytes_eq(const u8* got, std::size_t got_n, const u8 (&want)[N]) {
    return got_n == N && std::memcmp(got, want, N) == 0;
}

// 인자를 UartBase<D>& 로 받는 제네릭 함수: 파생→기반 추론으로 D 가 결정된다
template <class D>
std::size_t hello_through_base(UartBase<D>& u) { return u.print("OK"); }

int main() {
    // ---- Part A: 기본 동작 ----
    {
        MockUart m;
        CHECK(m.init(115200));
        CHECK(m.baud == 115200u);
        CHECK(!m.init(0));
        CHECK(m.put(0xA5));
        CHECK(m.tx_n == 1 && m.tx[0] == 0xA5);
        u8 c = 0;
        CHECK(!m.try_get(c));
        m.feed(0x11); m.feed(0x22);
        CHECK(m.try_get(c) && c == 0x11);
        CHECK(m.try_get(c) && c == 0x22);
        CHECK(!m.try_get(c));
    }
    {   // write: 공통 코드는 base 에 1번만
        MockUart m;
        const u8 d[3] = {1, 2, 3};
        CHECK(m.write(d) == 3);
        CHECK(bytes_eq(m.tx, m.tx_n, d));
        CHECK(m.write(std::span<const u8>{}) == 0);
        CHECK(m.put_calls == 3);
    }
    {   // write 경계: 버퍼가 꽉 차면 거기서 멈춘다
        MockUart m;
        u8 big[MockUart::kCap + 5] = {};
        CHECK(m.write(big) == MockUart::kCap);
        CHECK(m.put_calls == MockUart::kCap + 1);   // 실패한 1번까지만 호출
    }
    {   // print
        MockUart m;
        CHECK(m.print("") == 0);
        CHECK(m.print("hi!") == 3);
        CHECK(m.tx_n == 3 && std::memcmp(m.tx, "hi!", 3) == 0);
    }
    {   // Stm32Uart: BRR 표 (표 기반 테스트)
        struct { u32 baud, brr; } const table[] = {
            {9600, 1667}, {19200, 833}, {57600, 278}, {115200, 139}, {921600, 17}, {16'000'000, 1},
        };
        for (const auto& t : table) {
            hw::reset();
            Usart1 u;
            CHECK(u.init(t.baud) && hw::devs[0].brr == t.brr);
        }
        hw::reset();
        Usart1 u;
        CHECK(!u.init(0));
        CHECK(hw::devs[0].brr == 0 && hw::devs[0].cr1 == 0);
        CHECK(u.init(115200) && hw::devs[0].cr1 == (hw::CR1_UE | hw::CR1_TE | hw::CR1_RE));
    }
    {   // 두 포트가 서로 다른 장치로 간다
        hw::reset();
        Usart1 a; Usart2 b;
        CHECK(a.put('A') && b.put('B'));
        CHECK(hw::devs[0].tx_n == 1 && hw::devs[0].tx[0] == 'A');
        CHECK(hw::devs[1].tx_n == 1 && hw::devs[1].tx[0] == 'B');
    }
    {   // TXE 폴링: busy 7번까지는 기다리고(8번째에 성공), 8번이면 타임아웃
        hw::reset();
        Usart1 u;
        hw::devs[0].busy_left = 7;
        CHECK(u.put(0x31));
        hw::reset();
        hw::devs[0].busy_left = 8;
        CHECK(!u.put(0x32));
        CHECK(hw::devs[0].tx_n == 0);
        hw::reset();
        hw::devs[0].stuck = true;
        const u8 d[2] = {1, 2};
        CHECK(u.write(d) == 0);
    }
    {   // 수신: RXNE 없으면 DR 을 건드리지 않는다
        hw::reset();
        Usart2 u;
        u8 c = 0x77;
        CHECK(!u.try_get(c) && c == 0x77);
        hw::feed_rx(hw::devs[1], 0x5A);
        CHECK(u.try_get(c) && c == 0x5A);
        CHECK(!u.try_get(c));
    }
    {   // 파생→기반 추론
        MockUart m;
        hw::reset();
        Usart2 u;
        CHECK(hello_through_base(m) == 2);
        CHECK(hello_through_base(u) == 2);
    }
    // 빈 CRTP 기반은 객체 크기를 늘리지 않는다 (EBO, CPP-B08 참고)
    CHECK(sizeof(Usart1) == 1);
    CHECK(std::is_empty_v<Usart1>);

    // ---- Part B: 컴파일 타임 방어 (trait 로 "컴파일되는가"를 bool 로 받는다) ----
    CHECK(std::is_default_constructible_v<MockUart>);
    CHECK(!std::is_default_constructible_v<WrongDerived>);
    CHECK(!std::is_default_constructible_v<UartBase<MockUart>>);
    CHECK(UartBase<MockUart>::implements_interface());
    CHECK(UartBase<Usart1>::implements_interface());
    CHECK(!UartBase<NoPut>::implements_interface());
    CHECK(!UartBase<WrongRet>::implements_interface());

    // ---- Part C: 프레임 ----
    {
        MockUart m;
        const u8 p[2] = {0x10, 0x20};
        const u8 want[5] = {0x02, 0x02, 0x10, 0x20, 0x32};
        CHECK(send_frame(m, p));
        CHECK(bytes_eq(m.tx, m.tx_n, want));
    }
    {   // 빈 payload: 02 00 00
        MockUart m;
        const u8 want[3] = {0x02, 0x00, 0x00};
        CHECK(send_frame(m, std::span<const u8>{}));
        CHECK(bytes_eq(m.tx, m.tx_n, want));
    }
    {   // payload 256 → 한 바이트도 안 나간다 / 255 → 끝까지 (Stm32 쪽 tx 버퍼 300)
        MockUart m;
        u8 p[256] = {};
        CHECK(!send_frame(m, p));
        CHECK(m.put_calls == 0);
        hw::reset();
        Usart1 u;
        CHECK(send_frame(u, std::span<const u8>(p, 255)));
        CHECK(hw::devs[0].tx_n == 258 && hw::devs[0].tx[1] == 255);
        MockUart small;                             // 64 바이트 mock 은 payload 도중에 실패
        CHECK(!send_frame(small, std::span<const u8>(p, 100)));
        CHECK(small.tx_n == MockUart::kCap);
    }
    {   // 같은 입력이면 Mock 과 Stm32 의 선 위 바이트가 같아야 한다 (고정 시드 무작위 200회)
        u32 seed = 0xC0FFEE11u;
        bool same = true;
        for (int round = 0; round < 200 && same; ++round) {
            u8 p[60];
            std::size_t n = xorshift32(seed) % 61;          // 0..60
            for (std::size_t i = 0; i < n; ++i) p[i] = static_cast<u8>(xorshift32(seed));
            MockUart m; hw::reset();
            hw::devs[1].busy_polls = xorshift32(seed) % 4;  // 0..3 번 busy (kMaxPolls 미만)
            Usart2 u;
            const std::span<const u8> s(p, n);
            same = send_frame(m, s) && send_frame(u, s) && m.tx_n == hw::devs[1].tx_n
                   && std::memcmp(m.tx, hw::devs[1].tx, m.tx_n) == 0;
        }
        CHECK(same);
    }

    // ---- Part C: 이질 컨테이너 ----
    CHECK(!(std::is_same_v<UartBase<MockUart>, UartBase<Usart1>>));
    CHECK(!(std::is_convertible_v<MockUart*, UartBase<Usart1>*>));
    CHECK(!(std::is_convertible_v<Usart1*, UartBase<Usart2>*>));
    CHECK(sizeof(AnyUart) == 2 * sizeof(void*));
    {
        MockUart m; hw::reset();
        Usart1 u1; Usart2 u2;
        const AnyUart ports[3] = {make_any(m), make_any(u1), make_any(u2)};
        const u8 d[3] = {7, 8, 9};
        CHECK(broadcast(ports, d) == 3);
        CHECK(bytes_eq(m.tx, m.tx_n, d));
        CHECK(bytes_eq(hw::devs[0].tx, hw::devs[0].tx_n, d));
        CHECK(bytes_eq(hw::devs[1].tx, hw::devs[1].tx_n, d));
    }
    {   // 가운데 포트 고장: 그 포트만 빠지고 뒤 포트는 계속 받는다
        MockUart m; hw::reset();
        Usart1 u1; Usart2 u2;
        hw::devs[0].stuck = true;
        const AnyUart ports[3] = {make_any(m), make_any(u1), make_any(u2)};
        const u8 d[2] = {1, 2};
        CHECK(broadcast(ports, d) == 2);
        CHECK(hw::devs[0].tx_n == 0);
        CHECK(hw::devs[1].tx_n == 2);
    }
    {   // 포트 0개 / data 0바이트 경계
        MockUart m;
        const AnyUart ports[1] = {make_any(m)};
        const u8 d[1] = {1};
        CHECK(broadcast(std::span<const AnyUart>{}, d) == 0);
        CHECK(broadcast(ports, std::span<const u8>{}) == 1);
        CHECK(m.tx_n == 0);
    }

    std::printf("%d/%d passed\n", g_passed, g_total);
    return g_passed == g_total ? 0 : 1;
}
