// CPP-B12 템플릿 기초: 폭별 레지스터 read/write
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
//
// 실제 칩:   T v = *reinterpret_cast<volatile const T*>(addr);      // MMIO, 컴파일러가 LDRB/LDRH/LDR 하나로
// 이 레슨:   주소 공간을 바이트 배열(mock::mem)로 흉내 내고, std::memcpy 로 읽고 쓴다.
//            uint8_t 배열을 uint32_t* 로 읽으면 strict aliasing 위반(UB)이라서 memcpy 를 쓴다.
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <type_traits>

static_assert(std::endian::native == std::endian::little, "테스트 기대값은 little-endian 기준 (arm64 Mac, Cortex-M 기본)");

// ---------------- 가짜 버스 (mock address space) ----------------
namespace mock {
constexpr std::uintptr_t BASE = 0x4000'0000u;   // 주변장치 영역 시작 (STM32 APB1 같은 느낌)
constexpr std::size_t    SIZE = 64;             // 64바이트짜리 주소 공간
alignas(8) static std::uint8_t mem[SIZE];       // 바이트 단위 백킹 스토어
static unsigned bus_faults = 0;                 // 잘못된 접근 횟수 (실제 칩이면 BusFault/HardFault)

struct Access { std::uintptr_t addr; std::size_t width; bool write; std::uint64_t value; };
static Access   log[64];                        // 접근 기록: 테스트가 "몇 비트 폭으로 접근했나" 확인용
static unsigned log_n = 0;
inline void record(std::uintptr_t a, std::size_t w, bool wr, std::uint64_t v) {
    if (log_n < 64) log[log_n++] = Access{a, w, wr, v};
}
}  // namespace mock

// Part B: 주소 → 백킹 스토어 포인터. 실패하면 bus_faults++ 하고 nullptr.
//   - [BASE, BASE+SIZE) 안에 width 바이트가 전부 들어가야 한다 (addr < BASE 도, 끝을 넘는 것도 실패)
//   - addr 는 width 의 배수(자연 정렬, natural alignment)여야 한다
inline std::uint8_t* mock_ptr(std::uintptr_t addr, std::size_t width) {
    (void)addr; (void)width;
    return nullptr; // TODO(Part B)
}

// ---------------- Part A/B: 허용 타입 판별 ----------------
// C++17 스타일 type trait: "레지스터 한 칸"으로 쓸 수 있는 타입인가?
//   unsigned 정수(uint8/16/32/64_t)만 true. bool, signed, float, enum, 포인터, 128비트는 false.
template <typename T>
inline constexpr bool is_reg_word_v = std::is_integral_v<T>; // TODO(Part B): 너무 느슨하다

// C++20 concept 버전: 같은 규칙을 atomic constraint 들의 && 로 쓴다 (에러 메시지가 어느 조건인지 짚어 줌)
template <typename T>
concept RegWord = std::integral<T>; // TODO(Part B): 너무 느슨하다

// ---------------- Part A: 폭별 read / write (C++17 스타일: 본문 static_assert) ----------------
template <typename T>
T reg_read(std::uintptr_t addr) {
    static_assert(is_reg_word_v<T>, "reg_read<T>: T must be an unsigned integer register type (uint8/16/32/64_t)");
    (void)addr;
    return T{}; // TODO(Part A): mock_ptr → memcpy → mock::record(..., false, v)
}

template <typename T>
void reg_write(std::uintptr_t addr, T value) {
    static_assert(is_reg_word_v<T>, "reg_write<T>: T must be an unsigned integer register type (uint8/16/32/64_t)");
    (void)addr; (void)value; // TODO(Part A): mock_ptr → memcpy → mock::record(..., true, value)
}

// C++20 concept 버전 (제약은 선언에 붙는다 → SFINAE 친화적, 오버로드 후보에서 깔끔하게 빠짐)
template <RegWord T> T    reg_read_c(std::uintptr_t addr)           { return reg_read<T>(addr); }
template <RegWord T> void reg_write_c(std::uintptr_t addr, T value) { reg_write<T>(addr, value); }

// 하드웨어 쪽(테스트벤치) 직접 접근: 검사·기록 없음
template <typename T> void hw_poke(std::uintptr_t a, T v) { std::memcpy(&mock::mem[a - mock::BASE], &v, sizeof v); }
template <typename T> T    hw_peek(std::uintptr_t a)      { T v; std::memcpy(&v, &mock::mem[a - mock::BASE], sizeof v); return v; }

// ---------------- Part C: 폭이 섞인 가짜 센서 인터페이스 "SNS" ----------------
namespace sns {
constexpr std::uintptr_t PERIPH   = mock::BASE + 0x20;
constexpr std::uintptr_t CR       = PERIPH + 0x00;  // u32  bit0 EN
constexpr std::uintptr_t SR       = PERIPH + 0x04;  // u16  bit0 TXE (송신 가능)
constexpr std::uintptr_t DR       = PERIPH + 0x08;  // u8   송신 데이터. 반드시 8비트로 접근 (FIFO에 1바이트 push)
constexpr std::uintptr_t FIFO_LVL = PERIPH + 0x09;  // u8   하드웨어가 관리 (읽기 전용) — DR 바로 옆!
constexpr std::uintptr_t BRR      = PERIPH + 0x0C;  // u16  보레이트 분주값
constexpr std::uintptr_t ID       = PERIPH + 0x10;  // u32  읽기 전용 칩 ID
constexpr std::uint32_t  CR_EN    = 1u << 0;
constexpr std::uint16_t  SR_TXE   = 1u << 0;
constexpr std::uint32_t  CHIP_ID  = 0x5E50'0012u;
}  // namespace sns

// ID 확인 → BRR(16비트) 쓰기 → CR 의 EN 비트 set (32비트 read-modify-write). ID 가 다르면 아무것도 안 쓰고 false.
static bool sns_init(std::uint16_t brr) {
    (void)brr;
    return false; // TODO(Part C)
}

// 바이트마다: SR(16비트)의 TXE 가 0이면 멈춤, 1이면 DR 에 8비트 쓰기. 보낸 바이트 수를 돌려준다.
static std::size_t sns_send(std::span<const std::uint8_t> data) {
    (void)data;
    return 0; // TODO(Part C)
}

// ---------------- 테스트 하네스 ----------------
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

// requires-expression 으로 "이 호출이 컴파일되는가?"를 bool 로 얻는다 (템플릿 안이어야 false 가 됨)
template <typename T> constexpr bool can_read17 = requires(std::uintptr_t a) { reg_read<T>(a); };
template <typename T> constexpr bool can_read20 = requires(std::uintptr_t a) { reg_read_c<T>(a); };
template <typename V> constexpr bool can_write20_deduced = requires(std::uintptr_t a, V v) { reg_write_c(a, v); };

enum class Mode : std::uint32_t { Off, On };

int main() {
    using mock::BASE;
    constexpr std::uintptr_t A = BASE + 0x00;

    // Part A — 폭별 접근과 템플릿 인자 추론
    reg_write<std::uint32_t>(A, 0x1122'3344u);
    CHECK(reg_read<std::uint32_t>(A) == 0x1122'3344u);
    CHECK(reg_read<std::uint8_t>(A + 0) == 0x44u);           // little-endian: 최하위 바이트가 낮은 주소
    CHECK(reg_read<std::uint8_t>(A + 3) == 0x11u);
    CHECK(reg_read<std::uint16_t>(A + 2) == 0x1122u);
    reg_write<std::uint16_t>(A + 2, 0xBEEFu);                 // 위쪽 halfword 만 바꾼다
    CHECK(reg_read<std::uint32_t>(A) == 0xBEEF'3344u);
    reg_write(A + 4, std::uint8_t{0xA5});                     // T 추론 → uint8_t → 1바이트 쓰기
    CHECK(mock::log_n > 0 && mock::log[mock::log_n - 1].width == 1);
    reg_write(A + 8, 0xA5u);                                  // T 추론 → unsigned int → 4바이트 쓰기
    CHECK(mock::log_n > 0 && mock::log[mock::log_n - 1].width == 4);
    reg_write<std::uint64_t>(A + 16, 0x0123'4567'89AB'CDEFull);
    CHECK(reg_read<std::uint32_t>(A + 16) == 0x89AB'CDEFu);

    // Part B — 타입 규칙 (전부 컴파일타임 상수. 여기선 FAIL 줄을 보려고 CHECK 로 찍는다)
    CHECK(is_reg_word_v<std::uint8_t> && is_reg_word_v<std::uint16_t> &&
          is_reg_word_v<std::uint32_t> && is_reg_word_v<std::uint64_t>);
    CHECK(!is_reg_word_v<int>);
    CHECK(!is_reg_word_v<bool>);                              // bool 은 "unsigned integral" 이다!
    CHECK(!is_reg_word_v<float>);
    CHECK(!is_reg_word_v<Mode>);                              // enum class 는 integral 이 아니다
    CHECK(!is_reg_word_v<unsigned __int128>);                 // 128비트 버스 접근은 없다
    CHECK(can_read17<int>);                                   // 주의: static_assert 버전은 "호출 가능해 보인다"
    CHECK(!can_read20<int> && !can_read20<bool> && can_read20<std::uint16_t>);
    CHECK(!can_write20_deduced<int> && can_write20_deduced<unsigned>);   // reg_write_c(a, 5) → T=int → 거부

    // Part B — 정렬·범위 검사 (위반하면 접근 안 함 + bus_faults++)
    unsigned f0 = mock::bus_faults;
    CHECK(reg_read<std::uint32_t>(A + 2) == 0u && mock::bus_faults == f0 + 1);    // 4바이트인데 주소 %4 != 0
    reg_write<std::uint16_t>(A + 1, 0xFFFFu);                                        // 2바이트인데 홀수 주소
    CHECK(mock::bus_faults == f0 + 2 && hw_peek<std::uint32_t>(A) == 0xBEEF'3344u);  // 메모리 그대로
    CHECK(reg_read<std::uint8_t>(A + 1) == 0x33u && mock::bus_faults == f0 + 2);    // 1바이트는 어디든 OK
    (void)reg_read<std::uint32_t>(BASE + mock::SIZE);                               // 끝 바로 다음
    (void)reg_read<std::uint32_t>(BASE - 4);                                        // 시작 앞
    (void)reg_read<std::uint64_t>(BASE + mock::SIZE - 4);                           // 끝을 넘친다 (+%8 위반)
    CHECK(mock::bus_faults == f0 + 5);
    CHECK(reg_read<std::uint64_t>(BASE + mock::SIZE - 8) == 0u && mock::bus_faults == f0 + 5);

    // Part C — 가짜 SNS 주변장치
    hw_poke<std::uint32_t>(sns::ID, sns::CHIP_ID);
    hw_poke<std::uint8_t>(sns::FIFO_LVL, 7);
    hw_poke<std::uint16_t>(sns::SR, sns::SR_TXE);
    unsigned f1 = mock::bus_faults;
    mock::log_n = 0;
    CHECK(sns_init(0x0683u));
    CHECK(hw_peek<std::uint16_t>(sns::BRR) == 0x0683u);
    CHECK((hw_peek<std::uint32_t>(sns::CR) & sns::CR_EN) != 0u);
    const std::uint8_t msg[] = {0x10, 0x20, 0x30};
    CHECK(sns_send(msg) == 3u);
    CHECK(hw_peek<std::uint8_t>(sns::DR) == 0x30u);
    CHECK(hw_peek<std::uint8_t>(sns::FIFO_LVL) == 7u);        // 옆 레지스터를 덮어쓰지 않았다
    unsigned dr_writes = 0, bad_width = 0;
    for (unsigned i = 0; i < mock::log_n; ++i) {
        const auto& e = mock::log[i];
        if (e.write && e.addr == sns::DR) { ++dr_writes; if (e.width != 1) ++bad_width; }
        if (e.addr == sns::BRR && e.width != 2) ++bad_width;
        if (e.addr == sns::CR && e.width != 4) ++bad_width;
        if (e.addr == sns::SR && e.width != 2) ++bad_width;
    }
    CHECK(dr_writes == 3 && bad_width == 0);
    hw_poke<std::uint16_t>(sns::SR, 0);                       // TXE=0: FIFO 가득
    CHECK(sns_send(msg) == 0u);
    hw_poke<std::uint32_t>(sns::ID, 0xDEAD'BEEFu);            // 다른 칩
    hw_poke<std::uint16_t>(sns::BRR, 0);
    CHECK(!sns_init(0x0683u) && hw_peek<std::uint16_t>(sns::BRR) == 0u);
    CHECK(mock::bus_faults == f1);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
