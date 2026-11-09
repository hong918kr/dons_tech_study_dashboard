// CPP-M04 추가 연습 — 한 번에 검증
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <type_traits>

static int g_pass, g_total;
#define CHECK(...) do { g_total++; if (__VA_ARGS__) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__); } while (0)

// ---- 공용 mock: 접근 로그 ----
enum class R : std::uint32_t { OUT, IN, SET, CLR, XOR };
struct Access { R reg; bool is_write; std::uint32_t value; };
static Access   g_log[64];
static unsigned g_n = 0;
static void record(R r, bool w, std::uint32_t v) { if (g_n < 64) g_log[g_n++] = Access{r, w, v}; }

// STM32 처럼 SET/CLR 이 한 레지스터(BSRR)인 포트 — 표현만 다른 Port 인터페이스
struct BsrrPort {
    static inline std::uint32_t out = 0, in = 0;
    static void reset() { out = 0; in = 0; g_n = 0; }
    static std::uint32_t read_out() { record(R::OUT, false, out); return out; }
    static std::uint32_t read_in() { record(R::IN, false, in); return in; }
    // 한 번의 store 로 set 과 clear 를 동시에 (R::SET 로그 한 줄)
    static void set_clear(std::uint32_t set, std::uint32_t clr) {
        record(R::SET, true, set | (clr << 16));
        out = (out & ~clr) | set;
    }
};

// 연습 3: nRF52 처럼 OUTSET / OUTCLR 이 분리된 포트 (BSRR 없음) — set+clear 는 store 2번
struct SplitPort {
    static inline std::uint32_t out = 0;
    static void reset() { out = 0; g_n = 0; }
    static std::uint32_t read_out() { record(R::OUT, false, out); return out; }
    static void set_bits(std::uint32_t m) { record(R::SET, true, m); out |= m; }
    static void clr_bits(std::uint32_t m) { record(R::CLR, true, m); out &= ~m; }
};

// 연습 4: RP2040 SIO 처럼 XOR 레지스터까지 있는 포트
struct XorPort : SplitPort {
    static void xor_bits(std::uint32_t m) { record(R::XOR, true, m); out ^= m; }
};

// ---- 연습 1: PinBus::read() — 흩어진 핀을 IDR 한 번 읽어 모으기 ----
template <unsigned N> struct PinB { static constexpr unsigned number = N; static constexpr std::uint32_t mask = 1u << N; };

template <typename First, typename... Rest>
struct PinBusR {
    static constexpr std::uint32_t mask = (First::mask | ... | Rest::mask);
    static constexpr std::uint32_t gather(std::uint32_t idr) {
        std::uint32_t out = (idr >> First::number) & 1u;
        unsigned i = 1;
        ((out |= ((idr >> Rest::number) & 1u) << i, ++i), ...);
        return out;
    }
    static std::uint32_t read() { return gather(BsrrPort::read_in()); }
};

// ---- 연습 2: ScopedLow — chip select RAII (CPP-B06) ----
template <typename Pin>
struct ScopedLow {
    ScopedLow() { Pin::clear(); }
    ~ScopedLow() { Pin::set(); }
    ScopedLow(const ScopedLow&) = delete;
    ScopedLow& operator=(const ScopedLow&) = delete;
};
template <unsigned N>
struct BsrrPin {
    static void set() { BsrrPort::set_clear(1u << N, 0u); }
    static void clear() { BsrrPort::set_clear(0u, 1u << N); }
};
static void spi_write_mock(std::uint32_t v) { record(R::XOR, true, v); }   // 로그에 "데이터"로 남긴다

// ---- 연습 3·4: 포트 능력에 맞춰 코드를 고르는 Pin ----
template <typename P>
concept HasXor = requires(std::uint32_t m) { P::xor_bits(m); };

template <typename P, unsigned N>
    requires(N < 32)
struct PinG {
    static constexpr std::uint32_t mask = 1u << N;
    static void set() { P::set_bits(mask); }
    static void clear() { P::clr_bits(mask); }
    static void toggle() {
        if constexpr (HasXor<P>) {
            P::xor_bits(mask);                  // store 1번, 하드웨어가 원자적으로
        } else if (P::read_out() & mask) {
            P::clr_bits(mask);                  // read + store 1번: 다른 핀은 안전, 같은 핀 경쟁은 남음
        } else {
            P::set_bits(mask);
        }
    }
};
// set/clear 동시 갱신: BSRR 포트는 store 1번, 분리 포트는 store 2번
template <typename P>
static void write_mask(std::uint32_t set, std::uint32_t clr) {
    if constexpr (requires { P::set_clear(set, clr); }) {
        P::set_clear(set, clr);
    } else {
        if (clr != 0) P::clr_bits(clr);
        if (set != 0) P::set_bits(set);
    }
}

// ---- 연습 5: Cortex-M3/M4 bit-band alias 주소 계산 (constexpr) ----
constexpr std::uint32_t bitband_alias(std::uint32_t reg_addr, unsigned bit) {
    return 0x4200'0000u + (reg_addr - 0x4000'0000u) * 32u + bit * 4u;
}
constexpr std::uint32_t GPIOA_ODR_ADDR = 0x4002'0014u;
static_assert(bitband_alias(GPIOA_ODR_ADDR, 5) == 0x4240'0294u);
static_assert(bitband_alias(GPIOA_ODR_ADDR, 0) == 0x4240'0280u);
static_assert(bitband_alias(0x4000'0000u, 0) == 0x4200'0000u);
static_assert(bitband_alias(0x4000'0000u, 31) == 0x4200'007Cu);
static_assert(bitband_alias(0x400F'FFFCu, 31) == 0x43FF'FFFCu);   // 주변장치 bit-band 1MB 의 마지막 비트

int main() {
    // 연습 1
    using Bus = PinBusR<PinB<0>, PinB<3>, PinB<7>, PinB<12>>;
    CHECK(Bus::gather(0x1089u) == 0xFu);
    CHECK(Bus::gather(0x0000u) == 0x0u);
    CHECK(Bus::gather(0x1000u) == 0x8u);                // PA12 → bit3
    CHECK(Bus::gather(0x0008u) == 0x2u);                // PA3  → bit1
    CHECK(Bus::gather(0xEF76u) == 0x0u);                // 다른 비트는 전부 무시
    BsrrPort::reset();
    BsrrPort::in = 0x0081u;
    CHECK(Bus::read() == 0b0101u && g_n == 1 && g_log[0].reg == R::IN);   // IDR 한 번 읽기 → 한 시점의 스냅샷

    // 연습 2
    using Cs = BsrrPin<4>;
    BsrrPort::reset();
    {
        ScopedLow<Cs> guard;
        spi_write_mock(0xAB);
        CHECK(g_n == 2 && (BsrrPort::out & (1u << 4)) == 0);
    }
    CHECK(g_n == 3 && g_log[0].value == (1u << 20) && g_log[1].value == 0xABu && g_log[2].value == (1u << 4));
    CHECK(!std::is_copy_constructible_v<ScopedLow<Cs>>);

    // 연습 3 — 분리 포트
    using S5 = PinG<SplitPort, 5>;
    SplitPort::reset();
    S5::set();
    CHECK(g_n == 1 && g_log[0].reg == R::SET && g_log[0].value == (1u << 5) && SplitPort::out == (1u << 5));
    S5::clear();
    CHECK(g_n == 2 && g_log[1].reg == R::CLR && SplitPort::out == 0u);
    SplitPort::reset();
    S5::toggle();
    CHECK(g_n == 2 && g_log[0].reg == R::OUT && g_log[1].reg == R::SET);
    S5::toggle();
    CHECK(g_n == 4 && g_log[3].reg == R::CLR && SplitPort::out == 0u);
    SplitPort::reset();
    write_mask<SplitPort>(0b0011u, 0b1100u);
    CHECK(g_n == 2 && g_log[0].reg == R::CLR && g_log[1].reg == R::SET);   // 분리 포트: store 2번 → 동시 갱신 아님
    BsrrPort::reset();
    write_mask<BsrrPort>(0b0011u, 0b1100u);
    CHECK(g_n == 1 && g_log[0].value == (0b0011u | (0b1100u << 16)));      // BSRR: store 1번
    SplitPort::reset();
    write_mask<SplitPort>(0b0001u, 0u);
    CHECK(g_n == 1 && g_log[0].reg == R::SET);

    // 연습 4 — 하드웨어 XOR 를 고른다
    using X5 = PinG<XorPort, 5>;
    XorPort::reset();
    X5::toggle();
    CHECK(g_n == 1 && g_log[0].reg == R::XOR && XorPort::out == (1u << 5));   // read 없음, store 1번
    X5::toggle();
    CHECK(g_n == 2 && XorPort::out == 0u);
    CHECK(!HasXor<SplitPort> && HasXor<XorPort>);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
