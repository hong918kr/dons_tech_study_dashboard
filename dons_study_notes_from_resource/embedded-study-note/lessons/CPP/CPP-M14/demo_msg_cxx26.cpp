// CPP-M14 demo: C++26 static_assert 사용자 생성 메시지 (P2741) — 컴파일 타임에 문장을 조립한다
//   c++ -std=c++2c -Wall -Wextra -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR demo_msg_cxx26.cpp
//   SHOW_ERROR 없이: 정상 컴파일 (조건이 true 면 메시지는 상수 평가될 뿐 쓰이지 않는다)
#include <cstddef>
#include <cstdint>

enum class Port : std::uint8_t { A, B, C };
template <Port P, unsigned N> struct Pin { static constexpr Port port = P; static constexpr unsigned number = N; };
enum class Signal : std::uint8_t { USART1_TX, I2C1_SCL };
constexpr const char* name(Signal s) {
    switch (s) { case Signal::USART1_TX: return "USART1_TX"; case Signal::I2C1_SCL: return "I2C1_SCL"; }
    return "?";
}

struct AfRow { Port port; std::uint8_t pin; Signal signal; std::uint8_t af; };
inline constexpr AfRow AF_TABLE[] = {
    {Port::A, 9, Signal::USART1_TX, 7}, {Port::B, 6, Signal::USART1_TX, 7},
    {Port::B, 6, Signal::I2C1_SCL, 4},  {Port::B, 8, Signal::I2C1_SCL, 4},
};
constexpr int find_af(Port p, unsigned n, Signal s) {
    for (const AfRow& r : AF_TABLE) if (r.port == p && r.pin == n && r.signal == s) return r.af;
    return -1;
}

struct Msg {                                  // P2741 요구: .size() 와 .data() 를 상수식으로 부를 수 있으면 된다
    char buf[120]{}; std::size_t len = 0;
    constexpr void put(char c) { if (len < sizeof buf) buf[len++] = c; }
    constexpr void put(const char* s) { while (*s) put(*s++); }
    constexpr void put_pin(Port p, unsigned n) {
        put('P'); put(static_cast<char>('A' + static_cast<unsigned>(p)));
        if (n >= 10) put(static_cast<char>('0' + n / 10));
        put(static_cast<char>('0' + n % 10));
    }
    constexpr const char* data() const { return buf; }
    constexpr std::size_t size() const { return len; }
};

template <typename P, Signal S>
consteval Msg why() {
    Msg m;
    m.put_pin(P::port, P::number); m.put(" has no "); m.put(name(S)); m.put("; try:");
    for (const AfRow& r : AF_TABLE) if (r.signal == S) { m.put(' '); m.put_pin(r.port, r.pin); }
    return m;
}

template <typename P, Signal S>
void configure() {
    static_assert(find_af(P::port, P::number, S) >= 0, why<P, S>());
}

int main() {
    configure<Pin<Port::A, 9>, Signal::USART1_TX>();
#ifdef SHOW_ERROR
    configure<Pin<Port::A, 9>, Signal::I2C1_SCL>();
#endif
}
