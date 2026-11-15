/* CPP-A01 Typestate 패턴: 설정 순서를 타입으로 강제
 *
 * 맥(호스트)에서 STM32F4 류 SPI 블록(CR1/SR/DR, RM0090 대표값)을 mock 으로 흉내 내고, "지금 상태"를 타입에 담은 드라이버를 만든다.
 *   Part A: Spi<Disabled> → configure() → Spi<Configured> → enable() → Spi<Enabled>, disable() 로 되돌아가기.
 *           transfer 는 Enabled 에만 있다 — 잘못된 순서가 "컴파일 안 됨"을 requires-expression 으로 증명한다.
 *   Part B: 어려운 부분 — 실패할 수 있는 전이(원래 상태를 돌려받기), 런타임에만 아는 상태(std::variant 로 adopt/normalize),
 *           주변장치 소유권 토큰(take 는 한 번만), moved-from 객체 구멍.
 *   Part C: 부트로더가 SPI 를 켜 둔 채 넘겨준 상황에서 SPI NOR 플래시 JEDEC ID 읽기 (상태를 잃지 않는 부팅 경로).
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
 */
#include <atomic>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS %s\n", #expr); } \
                         else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

// ---- BEGIN driver (spi_typestate.hpp 와 같은 내용) ----
/* ================= 레지스터 맵: STM32F4 SPI 레이아웃 (RM0090 대표값) ================= */
namespace reg {
constexpr std::uint32_t CR1 = 0x00, SR = 0x08, DR = 0x0C;                     // 오프셋
constexpr std::uint32_t CR1_CPHA = 1u << 0, CR1_CPOL = 1u << 1, CR1_MSTR = 1u << 2, CR1_BR_POS = 3,
                        CR1_SPE = 1u << 6, CR1_LSBFIRST = 1u << 7, CR1_SSI = 1u << 8, CR1_SSM = 1u << 9;
constexpr std::uint32_t SR_RXNE = 1u << 0, SR_TXE = 1u << 1, SR_BSY = 1u << 7;
}  // namespace reg

enum class Mode : std::uint8_t { M0 = 0, M1 = 1, M2 = 2, M3 = 3 };          // 값의 bit1 = CPOL, bit0 = CPHA
static_assert(reg::CR1_CPOL == 2u && reg::CR1_CPHA == 1u, "Mode 값을 CR1[1:0] 에 그대로 넣는다");
enum class Baud : std::uint8_t { Div2 = 0, Div4, Div8, Div16, Div32, Div64, Div128, Div256 };   // CR1.BR[2:0]
struct Config { Mode mode = Mode::M0; Baud baud = Baud::Div8; bool lsb_first = false; };

/* CR1 값 (SPE 제외). 마스터 + 소프트웨어 NSS (SSM=SSI=1: NSS 핀 없이 마스터 유지 → MODF 없음) */
constexpr std::uint32_t encode_cr1(const Config& c) noexcept {
    return reg::CR1_MSTR | reg::CR1_SSM | reg::CR1_SSI | (std::uint32_t(c.baud) << reg::CR1_BR_POS) |
           std::uint32_t(c.mode) | (c.lsb_first ? reg::CR1_LSBFIRST : 0u);
}

/* 실제 칩용 접근 정책 (CPP-M01 의 MmioBus 를 줄인 것). 호스트 테스트는 MockHw 를 쓴다 */
template <std::uintptr_t Base> struct MmioHw {
    static std::uint32_t read(std::uint32_t off) noexcept { return *reinterpret_cast<volatile std::uint32_t*>(Base + off); }
    static void write(std::uint32_t off, std::uint32_t v) noexcept { *reinterpret_cast<volatile std::uint32_t*>(Base + off) = v; }
};

/* ================= 소유권 토큰: 주변장치(Hw) 하나당 프로그램 전체에서 하나 ================= */
template <class Hw> class PeriphToken {
public:
    PeriphToken(const PeriphToken&) = delete;
    PeriphToken& operator=(const PeriphToken&) = delete;
    PeriphToken(PeriphToken&&) noexcept = default;           // 이동은 된다 — 그리고 원본도 여전히 "토큰"이다 (Part B)
    PeriphToken& operator=(PeriphToken&&) = delete;
    ~PeriphToken() = default;

    // 처음 한 번만 토큰을 준다. 두 번째부터 nullopt. exchange 라서 ISR/스레드가 동시에 불러도 한 명만 받는다.
    [[nodiscard]] static std::optional<PeriphToken> take() noexcept {
        if (taken_.exchange(true, std::memory_order_acq_rel)) return std::nullopt;
        return PeriphToken{};
    }
    static void reset_for_test() noexcept { taken_.store(false); }   // 호스트 테스트 전용

private:
    PeriphToken() noexcept = default;                         // 아무도 직접 만들 수 없다
    static inline std::atomic<bool> taken_{false};
};

/* ================= 상태 태그와 드라이버 ================= */
struct Disabled {};
struct Configured {};
struct Enabled {};
template <class S> concept SpiState = std::same_as<S, Disabled> || std::same_as<S, Configured> || std::same_as<S, Enabled>;

struct Xfer { bool ok; std::uint8_t rx; };
constexpr unsigned kPolls = 1000;

template <SpiState S, class Hw> class Spi;
template <class Hw> using AnySpi = std::variant<Spi<Disabled, Hw>, Spi<Configured, Hw>, Spi<Enabled, Hw>>;
template <class Next, class Prev> using Fallible = std::variant<Next, Prev>;   // 0 = 성공한 새 상태, 1 = 실패 → 원래 상태를 돌려받음
template <class Hw> [[nodiscard]] AnySpi<Hw> adopt(PeriphToken<Hw>&& t) noexcept;

template <SpiState S, class Hw> class Spi {
public:
    using state = S;
    Spi(const Spi&) = delete;
    Spi& operator=(const Spi&) = delete;
    Spi(Spi&&) noexcept = default;       // 빈 객체의 이동 = 아무 일도 안 한다 → 원본도 계속 쓸 수 있다 (Part B 의 구멍)
    Spi& operator=(Spi&&) = delete;
    ~Spi() = default;                    // 하드웨어를 건드리지 않는다: moved-from 객체에도 소멸자는 불리니까

    // "리셋 직후"라고 가정한다 — 가정이라는 걸 이름에 드러낸다. 모르면 adopt().
    [[nodiscard]] static Spi from_reset(PeriphToken<Hw>&& t) noexcept requires std::same_as<S, Disabled> {
        return Spi{std::move(t)};
    }

    // Part A — TODO A1: SPE=0 인 지금 CR1 에 encode_cr1(c) 를 "한 번" 쓴다 (설정 변경은 꺼져 있을 때만 안전)
    [[nodiscard]] Spi<Configured, Hw> configure(const Config& c) && noexcept requires std::same_as<S, Disabled> {
        (void)c;                                                   // TODO A1
        return Spi<Configured, Hw>{std::move(tok_)};
    }

    // Part A — TODO A2: CR1 의 SPE 비트만 켠다 (읽고-고쳐-쓰기, 다른 비트는 그대로)
    [[nodiscard]] Spi<Enabled, Hw> enable() && noexcept requires std::same_as<S, Configured> {
        // TODO A2
        return Spi<Enabled, Hw>{std::move(tok_)};
    }

    // 설정을 지우고 리셋 상태로 (주어진 코드)
    [[nodiscard]] Spi<Disabled, Hw> reset() && noexcept requires std::same_as<S, Configured> {
        Hw::write(reg::CR1, 0u);
        return Spi<Disabled, Hw>{std::move(tok_)};
    }

    // 토큰을 돌려준다 (주어진 코드). Disabled 에서만 — 켜진 채로 소유권을 놓지 못하게
    [[nodiscard]] PeriphToken<Hw> release() && noexcept requires std::same_as<S, Disabled> { return std::move(tok_); }

    // Part A — TODO A3: TXE 를 기다려 DR 에 쓰고, RXNE 를 기다려 DR 을 읽는다. 각 대기는 max_polls 번까지 → 넘으면 {false, 0}
    [[nodiscard]] Xfer transfer(std::uint8_t tx, unsigned max_polls = kPolls) & noexcept requires std::same_as<S, Enabled> {
        (void)tx; (void)max_polls;                                 // TODO A3
        return {false, 0};
    }

    // Part A — TODO A4: BSY=0 을 max_polls 번까지 기다린 뒤 SPE 만 끈다 → index 0 (Configured).
    //          BSY 가 안 풀리면 아무것도 쓰지 않고 *자기 자신* 을 index 1 로 돌려준다 (상태도 토큰도 잃지 않는다)
    [[nodiscard]] Fallible<Spi<Configured, Hw>, Spi> disable(unsigned max_polls = kPolls) && noexcept
        requires std::same_as<S, Enabled> {
        (void)max_polls;                                           // TODO A4 (지금은 "항상 실패")
        return Fallible<Spi<Configured, Hw>, Spi>{std::in_place_index<1>, std::move(*this)};
    }

private:
    template <SpiState, class> friend class Spi;
    template <class H> friend AnySpi<H> adopt(PeriphToken<H>&& t) noexcept;
    explicit Spi(PeriphToken<Hw>&& t) noexcept : tok_(std::move(t)) {}
    [[no_unique_address]] PeriphToken<Hw> tok_;
};

/* Part B — TODO B1: 상태를 모를 때 (부트로더 핸드오프, 소프트 리셋). CR1 을 "한 번" 읽고, 아무것도 쓰지 않는다.
 *   SPE=1 → Enabled,  CR1≠0 (SPE=0) → Configured,  CR1==0 → Disabled */
template <class Hw> AnySpi<Hw> adopt(PeriphToken<Hw>&& t) noexcept {
    // TODO B1 (지금은 "항상 Disabled 라고 가정" — 바로 이 레슨이 막으려는 버그)
    return AnySpi<Hw>{Spi<Disabled, Hw>{std::move(t)}};
}

/* Part B — TODO B2: 어떤 상태든 원하는 설정 c 의 Configured 로 만든다.
 *   Disabled → configure,  Configured → reset → configure,  Enabled → disable → reset → configure.
 *   disable 이 실패하면(BSY) 받은 Enabled 를 그대로 돌려준다. 켜진 채로 CR1 설정 비트를 바꾸면 안 된다. */
template <class Hw> [[nodiscard]] AnySpi<Hw> normalize(AnySpi<Hw>&& any, const Config& c, unsigned max_polls = kPolls) noexcept {
    (void)c; (void)max_polls;                                  // TODO B2
    return std::move(any);
}
// ---- END driver ----

/* ================= mock SPI 블록 (호스트 테스트용) =================
 * 접근 기록(log) + 하드웨어 규칙 위반 카운터. 실제 칩에서 이 위반들은 "조용히 이상하게 동작"한다. */
struct Access {
    char op; std::uint32_t off; std::uint32_t val;            // op: 'R' / 'W'
    bool operator==(const Access&) const = default;
};
struct MockSpi {
    std::uint32_t cr1 = 0;
    bool rxne = false;
    std::uint8_t rx = 0;
    unsigned bsy_reads = 0;            // 앞으로 SR 읽기 N번 동안 BSY=1 (~0u = 영원히)
    int cfg_while_on = 0;              // SPE=1 인 채로 SPE 외의 CR1 비트를 바꾼 쓰기 (RM: 통신 중 BR/CPOL/CPHA 변경 금지)
    int dropped_tx = 0;                // SPE=0 (또는 슬레이브 모드)에서 DR 쓰기 → 전송 안 됨
    std::uint8_t (*slave)(std::uint8_t) = nullptr;            // 상대 칩: MOSI 바이트 → MISO 바이트
    Access log[96]{};
    int n = 0;

    void rec(char op, std::uint32_t off, std::uint32_t v) { if (n < 96) log[n++] = Access{op, off, v}; }
    std::uint32_t read(std::uint32_t off) {
        std::uint32_t v = 0;
        if (off == reg::CR1) {
            v = cr1;
        } else if (off == reg::SR) {
            v = reg::SR_TXE | (rxne ? reg::SR_RXNE : 0u);
            if (bsy_reads > 0) { v |= reg::SR_BSY; if (bsy_reads != ~0u) --bsy_reads; }
        } else if (off == reg::DR) {
            v = rx; rxne = false;
        }
        rec('R', off, v);
        return v;
    }
    void write(std::uint32_t off, std::uint32_t v) {
        rec('W', off, v);
        if (off == reg::CR1) {
            if ((cr1 & reg::CR1_SPE) != 0u && ((cr1 ^ v) & ~reg::CR1_SPE) != 0u) ++cfg_while_on;
            cr1 = v;
        } else if (off == reg::DR) {
            if ((cr1 & reg::CR1_SPE) != 0u && (cr1 & reg::CR1_MSTR) != 0u) {
                rx = slave != nullptr ? slave(static_cast<std::uint8_t>(v)) : 0xFFu;
                rxne = true;
            } else {
                ++dropped_tx;
            }
        }
    }
    int writes_to(std::uint32_t off) const {
        int k = 0;
        for (int i = 0; i < n; ++i) k += (log[i].op == 'W' && log[i].off == off) ? 1 : 0;
        return k;
    }
    bool log_is(std::initializer_list<Access> want) const {
        if (static_cast<int>(want.size()) != n) return false;
        int i = 0;
        for (const Access& a : want) if (!(log[i++] == a)) return false;
        return true;
    }
    bool cr1_writes_are(std::initializer_list<std::uint32_t> want) const {   // CR1 쓰기 값만 순서대로 비교
        auto it = want.begin();
        for (int i = 0; i < n; ++i) {
            if (log[i].op != 'W' || log[i].off != reg::CR1) continue;
            if (it == want.end() || *it != log[i].val) return false;
            ++it;
        }
        return it == want.end();
    }
};
static MockSpi g_mock[2];
template <int I> struct MockHw {
    static std::uint32_t read(std::uint32_t off) noexcept { return g_mock[I].read(off); }
    static void write(std::uint32_t off, std::uint32_t v) noexcept { g_mock[I].write(off, v); }
};
using H1 = MockHw<0>;   // "SPI1"
using H2 = MockHw<1>;   // "SPI2"
template <class S> using S1 = Spi<S, H1>;

/* ================= Part C: SPI NOR 플래시 (W25Q 류, JEDEC ID 명령 9Fh — 대표값) ================= */
constexpr std::uint8_t CMD_JEDEC_ID = 0x9F;
static bool g_cs_low = false;
static int g_cs_edges = 0;
static int g_flash_pos = -1;                                 // -1 = 명령 대기, 0..2 = ID 바이트 위치
static std::uint8_t g_flash_id[3] = {0xEF, 0x40, 0x18};

static std::uint8_t flash_slave(std::uint8_t mosi) {
    if (!g_cs_low) return 0xFF;                               // CS 가 high 면 칩은 듣지 않는다
    if (g_flash_pos < 0) { g_flash_pos = (mosi == CMD_JEDEC_ID) ? 0 : 3; return 0xFF; }
    return g_flash_pos < 3 ? g_flash_id[g_flash_pos++] : 0xFF;
}
class ScopedCs {                                              // CPP-B06 의 RAII 가드와 같은 모양
public:
    ScopedCs() noexcept { g_cs_low = true; ++g_cs_edges; }
    ~ScopedCs() { g_cs_low = false; ++g_cs_edges; g_flash_pos = -1; }
    ScopedCs(const ScopedCs&) = delete;
    ScopedCs& operator=(const ScopedCs&) = delete;
};

/* Part C — TODO C1: CS low 동안 9Fh 하나 + 더미(0x00) 3바이트를 보내고, 더미에 대한 응답 3바이트를 MSB 먼저 합친다.
 *   전송이 하나라도 실패하면 nullopt (CS 는 그래도 반드시 올린다) */
template <class Hw> [[nodiscard]] std::optional<std::uint32_t> read_jedec_id(Spi<Enabled, Hw>& spi) noexcept {
    (void)spi;                                                 // TODO C1
    return std::nullopt;
}

template <class Hw> struct FlashBoot { AnySpi<Hw> spi; std::uint32_t jedec; bool ok; };

/* Part C — TODO C2: adopt → normalize(c) → (Configured 가 됐으면) enable → read_jedec_id → expect_id 비교.
 *   어떤 경로로 끝나든 SPI 객체(= 토큰)를 결과 안에 담아 돌려준다. 끄지 못했으면 Enabled 그대로, ok=false. */
template <class Hw>
[[nodiscard]] FlashBoot<Hw> boot_flash(PeriphToken<Hw>&& tok, const Config& c, std::uint32_t expect_id) noexcept {
    (void)c; (void)expect_id;                                  // TODO C2
    return {adopt(std::move(tok)), 0u, false};
}

/* 비교용 C 스타일 초기화 (주어진 코드): 현재 상태를 보지 않고 "설정 + 켜기" 를 한 번에 쓴다 */
template <class Hw> void naive_init(const Config& c) noexcept { Hw::write(reg::CR1, encode_cr1(c) | reg::CR1_SPE); }

/* 구멍 시연 (주어진 코드): disable 로 "소비"한 en 을 다시 쓴다 — 컴파일은 된다 */
template <class Hw> Xfer buggy_sleep_then_send(Spi<Enabled, Hw>& en) {
    auto r = std::move(en).disable();                          // 여기서 en 은 논리적으로 끝났다
    (void)r;                                                   // (게다가 r 안의 Configured 객체와 토큰도 버려진다)
    return en.transfer(0x11, 20);                              // 그런데 en 은 여전히 Spi<Enabled> 이름이다
}

/* ================= 컴파일 시간 증명: "이 코드는 컴파일되면 안 된다" ================= */
template <class T> concept CanTransfer       = requires(T& s, std::uint8_t b) { s.transfer(b); };
template <class T> concept CanTransferRvalue = requires(T& s, std::uint8_t b) { std::move(s).transfer(b); };
template <class T> concept CanConfigure      = requires(T& s, Config c) { std::move(s).configure(c); };
template <class T> concept CanConfigureLval  = requires(T& s, Config c) { s.configure(c); };
template <class T> concept CanEnable         = requires(T& s) { std::move(s).enable(); };
template <class T> concept CanDisable        = requires(T& s) { std::move(s).disable(); };
template <class T> concept CanRelease        = requires(T& s) { std::move(s).release(); };
template <class T, class Tok> concept CanFromReset = requires(Tok& t) { T::from_reset(std::move(t)); };

static_assert(CanTransfer<S1<Enabled>> && !CanTransfer<S1<Configured>> && !CanTransfer<S1<Disabled>>,
              "transfer 는 Enabled 에만");
static_assert(!CanTransferRvalue<S1<Enabled>>, "임시 Enabled 에 transfer 금지 (토큰을 버리게 되므로)");
static_assert(CanConfigure<S1<Disabled>> && !CanConfigure<S1<Configured>> && !CanConfigure<S1<Enabled>>);
static_assert(!CanConfigureLval<S1<Disabled>>, "전이는 && — std::move 없이 부르면 컴파일 에러");
static_assert(CanEnable<S1<Configured>> && !CanEnable<S1<Disabled>> && !CanEnable<S1<Enabled>>, "상태 건너뛰기 금지");
static_assert(CanDisable<S1<Enabled>> && !CanDisable<S1<Configured>>);
static_assert(CanRelease<S1<Disabled>> && !CanRelease<S1<Enabled>>, "켜진 채로 소유권을 놓지 못한다");
static_assert(CanFromReset<S1<Disabled>, PeriphToken<H1>> && !CanFromReset<Spi<Disabled, H2>, PeriphToken<H1>>,
              "SPI1 토큰으로 SPI2 를 만들 수 없다");
static_assert(!CanFromReset<S1<Enabled>, PeriphToken<H1>>, "Enabled 를 맨손으로 만들 수 없다");
static_assert(!std::is_default_constructible_v<PeriphToken<H1>> && !std::is_copy_constructible_v<PeriphToken<H1>>,
              "토큰은 위조·복제 불가");
static_assert(!std::is_copy_constructible_v<S1<Enabled>> && !std::is_move_assignable_v<S1<Enabled>>);
static_assert(std::is_same_v<decltype(std::declval<S1<Disabled>>().configure(Config{})), S1<Configured>>);
static_assert(std::is_same_v<decltype(std::declval<S1<Enabled>>().disable()), Fallible<S1<Configured>, S1<Enabled>>>);
// 비용 0 의 타입 쪽 증거: 빈 객체, 사소한(trivial) 복사·소멸 → 레지스터로 전달, 소멸자 코드 없음
static_assert(sizeof(S1<Enabled>) == 1 && std::is_empty_v<S1<Enabled>> && std::is_empty_v<PeriphToken<H1>>);
static_assert(std::is_trivially_copyable_v<S1<Enabled>> && std::is_trivially_destructible_v<S1<Enabled>>);

/* ================= 테스트 ================= */
static void reset_all() {
    g_mock[0] = MockSpi{}; g_mock[1] = MockSpi{};
    g_cs_low = false; g_cs_edges = 0; g_flash_pos = -1;
    g_flash_id[0] = 0xEF; g_flash_id[1] = 0x40; g_flash_id[2] = 0x18;
    PeriphToken<H1>::reset_for_test(); PeriphToken<H2>::reset_for_test();
}
static std::uint8_t invert_slave(std::uint8_t b) { return static_cast<std::uint8_t>(~b); }
static std::uint32_t xorshift32(std::uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

static void test_part_a() {
    std::printf("-- Part A: Disabled → Configured → Enabled → (disable) --\n");
    reset_all();
    CHECK(encode_cr1(Config{}) == 0x314u);                                         // MSTR | BR=Div8 | SSI | SSM
    CHECK(encode_cr1(Config{Mode::M3, Baud::Div2, true}) == 0x387u);
    bool table_ok = true;                                                          // 4 모드 × 8 분주 전부
    for (unsigned m = 0; m < 4; ++m)
        for (unsigned b = 0; b < 8; ++b) {
            const std::uint32_t v = encode_cr1(Config{Mode(m), Baud(b), false});
            table_ok = table_ok && (v & 3u) == m && ((v >> 3) & 7u) == b && (v & reg::CR1_SPE) == 0u;
        }
    CHECK(table_ok);

    auto tok = PeriphToken<H1>::take();
    CHECK(tok.has_value());
    if (!tok) return;
    auto d = S1<Disabled>::from_reset(std::move(*tok));
    CHECK(g_mock[0].n == 0);                                                       // 만들기만 해서는 하드웨어를 안 건드린다

    auto c = std::move(d).configure(Config{});
    CHECK(g_mock[0].log_is({{'W', reg::CR1, 0x314u}}));                            // 정확히 쓰기 1번
    g_mock[0].n = 0;

    auto e = std::move(c).enable();
    CHECK(g_mock[0].log_is({{'R', reg::CR1, 0x314u}, {'W', reg::CR1, 0x354u}}));  // RMW: SPE 만 추가
    g_mock[0].n = 0;

    g_mock[0].slave = invert_slave;
    const Xfer x = e.transfer(0x5A);
    CHECK(x.ok && x.rx == 0xA5u);
    CHECK(g_mock[0].log_is({{'R', reg::SR, 0x2u}, {'W', reg::DR, 0x5Au}, {'R', reg::SR, 0x3u}, {'R', reg::DR, 0xA5u}}));

    std::uint32_t seed = 0xC0FFEEu;                                                // 결정적 바이트 256개
    bool all_ok = true;
    for (int i = 0; i < 256; ++i) {
        const std::uint8_t tx = static_cast<std::uint8_t>(xorshift32(seed));
        const Xfer y = e.transfer(tx);
        all_ok = all_ok && y.ok && y.rx == static_cast<std::uint8_t>(~tx);
    }
    CHECK(all_ok && g_mock[0].dropped_tx == 0);

    g_mock[0].n = 0;
    auto r = std::move(e).disable();
    CHECK(r.index() == 0u);
    CHECK(g_mock[0].log_is({{'R', reg::SR, 0x2u}, {'R', reg::CR1, 0x354u}, {'W', reg::CR1, 0x314u}}));
    if (auto* off = std::get_if<0>(&r)) {                                          // 설정은 남아 있다 → 다시 켜기만
        auto e2 = std::move(*off).enable();
        CHECK(g_mock[0].cr1 == 0x354u && e2.transfer(0x01).rx == 0xFEu);
        auto r2 = std::move(e2).disable();
        if (auto* off2 = std::get_if<0>(&r2)) {
            auto t2 = std::move(*off2).reset().release();                           // Disabled 로 가야만 토큰 반납
            CHECK(g_mock[0].cr1 == 0u);
            CHECK(!PeriphToken<H1>::take().has_value());                           // 토큰은 여전히 한 개 (t2)
            auto d3 = S1<Disabled>::from_reset(std::move(t2));                      // 반납받은 토큰으로 다시 시작
            auto c3 = std::move(d3).configure(Config{Mode::M1, Baud::Div4, false});
            CHECK(g_mock[0].cr1 == 0x30Du);
            (void)c3;
        } else {
            CHECK(false); CHECK(false); CHECK(false);              // 테스트 개수를 경로와 상관없이 같게
        }
    } else {
        CHECK(false); CHECK(false); CHECK(false); CHECK(false);
    }
    CHECK(g_mock[0].cfg_while_on == 0);
}

static void test_part_b() {
    std::printf("-- Part B: 소유권 토큰, 실패하는 전이, 런타임 상태 (variant) --\n");
    reset_all();
    auto t1 = PeriphToken<H1>::take();
    auto t1b = PeriphToken<H1>::take();
    auto t2 = PeriphToken<H2>::take();
    CHECK(t1.has_value() && !t1b.has_value() && t2.has_value());                   // SPI1 은 한 번, SPI2 는 따로

    // B1 adopt: 읽기 1번, 쓰기 0번으로 상태 판별
    {
        reset_all(); auto t = PeriphToken<H1>::take();
        auto a = adopt(std::move(*t));
        CHECK(a.index() == 0u && g_mock[0].log_is({{'R', reg::CR1, 0u}}));
    }
    {
        reset_all(); g_mock[0].cr1 = 0x314u; auto t = PeriphToken<H1>::take();
        auto a = adopt(std::move(*t));
        CHECK(a.index() == 1u && g_mock[0].writes_to(reg::CR1) == 0);
    }
    {
        reset_all(); g_mock[0].cr1 = 0x347u; auto t = PeriphToken<H1>::take();       // 부트로더: Mode3, Div2, SPE=1
        auto a = adopt(std::move(*t));
        CHECK(a.index() == 2u && g_mock[0].n == 1);
    }

    // B2 normalize: 켜진 채로 설정 비트를 바꾸지 않는다
    {
        reset_all(); g_mock[0].cr1 = 0x347u; auto t = PeriphToken<H1>::take();
        auto a = normalize(adopt(std::move(*t)), Config{});
        CHECK(a.index() == 1u && g_mock[0].cr1 == 0x314u);
        CHECK(g_mock[0].cr1_writes_are({0x307u, 0u, 0x314u}) && g_mock[0].cfg_while_on == 0);
    }
    {
        reset_all(); g_mock[0].cr1 = 0x30Du; auto t = PeriphToken<H1>::take();
        auto a = normalize(adopt(std::move(*t)), Config{});
        CHECK(a.index() == 1u && g_mock[0].cr1_writes_are({0u, 0x314u}));
    }
    {
        reset_all(); auto t = PeriphToken<H1>::take();
        auto a = normalize(adopt(std::move(*t)), Config{});
        CHECK(a.index() == 1u && g_mock[0].cr1_writes_are({0x314u}));
    }
    {   // BSY 가 영원히 → 끄지 못함 → Enabled 를 돌려받고, CR1 은 한 번도 안 썼다
        reset_all(); g_mock[0].cr1 = 0x347u; g_mock[0].bsy_reads = ~0u; auto t = PeriphToken<H1>::take();
        auto a = normalize(adopt(std::move(*t)), Config{}, 50);
        CHECK(a.index() == 2u && g_mock[0].writes_to(reg::CR1) == 0 && g_mock[0].cfg_while_on == 0);
    }

    // 실패하는 전이: disable 이 원래 객체를 돌려준다 → 계속 쓸 수 있다
    {
        reset_all(); g_mock[0].slave = invert_slave; auto t = PeriphToken<H1>::take();
        auto e = S1<Disabled>::from_reset(std::move(*t)).configure(Config{}).enable();   // prvalue 체인: 중간 이름이 없다
        g_mock[0].bsy_reads = ~0u;
        auto r = std::move(e).disable(50);
        CHECK(r.index() == 1u && (g_mock[0].cr1 & reg::CR1_SPE) != 0u);
        if (auto* back = std::get_if<1>(&r)) {
            g_mock[0].bsy_reads = 3;                                               // 이번엔 3번 읽은 뒤 풀린다
            CHECK(back->transfer(0x0F).rx == 0xF0u);                              // 돌려받은 Enabled 는 진짜 동작한다
            g_mock[0].bsy_reads = 3;
            g_mock[0].n = 0;
            auto r2 = std::move(*back).disable(50);
            int sr_reads = 0;
            for (int i = 0; i < g_mock[0].n; ++i) sr_reads += (g_mock[0].log[i].off == reg::SR) ? 1 : 0;
            CHECK(r2.index() == 0u && sr_reads == 4 && g_mock[0].cr1 == 0x314u);
        } else {
            CHECK(false);
            CHECK(false);
        }
    }

    // 구멍: moved-from 객체는 여전히 Spi<Enabled> 이름이다 — 하드웨어는 꺼졌는데 컴파일은 된다
    {
        reset_all(); g_mock[0].slave = invert_slave; auto t = PeriphToken<H1>::take();
        auto e = S1<Disabled>::from_reset(std::move(*t)).configure(Config{}).enable();
        const Xfer x = buggy_sleep_then_send(e);
        CHECK(!x.ok && g_mock[0].dropped_tx == 1);                                 // DR 쓰기 무시 → RXNE 안 옴 → 타임아웃
    }
}

static void test_part_c() {
    std::printf("-- Part C: 부트로더 핸드오프 + SPI NOR JEDEC ID --\n");
    constexpr std::uint32_t kId = 0xEF4018u;
    {   // 콜드 리셋
        reset_all(); g_mock[0].slave = flash_slave; auto t = PeriphToken<H1>::take();
        auto b = boot_flash(std::move(*t), Config{}, kId);
        CHECK(b.ok && b.jedec == kId && b.spi.index() == 2u);
        CHECK(g_mock[0].cr1_writes_are({0x314u, 0x354u}) && g_cs_edges == 2 && !g_cs_low);
        CHECK(g_mock[0].writes_to(reg::DR) == 4 && g_mock[0].cfg_while_on == 0);
    }
    {   // 부트로더가 Mode3/Div2 로 켜 둔 채 넘겨줌
        reset_all(); g_mock[0].slave = flash_slave; g_mock[0].cr1 = 0x347u; auto t = PeriphToken<H1>::take();
        auto b = boot_flash(std::move(*t), Config{}, kId);
        CHECK(b.ok && b.jedec == kId);
        CHECK(g_mock[0].cr1_writes_are({0x307u, 0u, 0x314u, 0x354u}) && g_mock[0].cfg_while_on == 0);
        CHECK(!PeriphToken<H1>::take().has_value());                               // 토큰은 b.spi 안에 있다
    }
    {   // 같은 상황에서 C 스타일 naive_init: 켜진 채로 모드·분주를 바꾼다 (주어진 코드, 비교용)
        reset_all(); g_mock[0].cr1 = 0x347u;
        naive_init<H1>(Config{});
        CHECK(g_mock[0].cfg_while_on == 1);
    }
    {   // BSY 가 안 풀림 → 끄지 못함: 설정을 건드리지 않고 Enabled 그대로 돌려준다
        reset_all(); g_mock[0].slave = flash_slave; g_mock[0].cr1 = 0x347u; g_mock[0].bsy_reads = ~0u;
        auto t = PeriphToken<H1>::take();
        auto b = boot_flash(std::move(*t), Config{}, kId);
        CHECK(!b.ok && b.spi.index() == 2u && g_mock[0].writes_to(reg::CR1) == 0 && g_cs_edges == 0);
    }
    {   // 다른 칩 (ID 불일치): 실패지만 SPI 는 Enabled 로 남아 재시도/진단 가능
        reset_all(); g_mock[0].slave = flash_slave; g_flash_id[0] = 0xC2; auto t = PeriphToken<H1>::take();
        auto b = boot_flash(std::move(*t), Config{}, kId);
        CHECK(!b.ok && b.jedec == 0xC24018u && b.spi.index() == 2u && !g_cs_low);
    }
    {   // 전송 실패 (RXNE 안 옴) 중에도 CS 는 반드시 올라간다
        reset_all(); auto t = PeriphToken<H1>::take();
        auto b = boot_flash(std::move(*t), Config{}, kId);
        if (auto* en = std::get_if<2>(&b.spi)) {
            g_mock[0].cr1 &= ~reg::CR1_MSTR;                                       // 슬레이브 모드로 바뀐 척 → DR 쓰기 무시
            g_cs_edges = 0;
            const auto id = read_jedec_id(*en);
            CHECK(!id.has_value() && g_cs_edges == 2 && !g_cs_low);
        } else {
            CHECK(false);
        }
    }
}

int main() {
    test_part_a();
    test_part_b();
    test_part_c();
    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
