/* CPP-A01 추가 연습 — 한 번에 검증
 *   연습 1: CheckedSpi — moved-from 사용을 런타임에 잡는 디버그용 typestate (비용: 1바이트 + 분기)
 *   연습 2: DynSpi — 상태를 런타임 변수로 들고 다니는 탈출구, 같은 오용이 "에러 코드"가 된다
 *   연습 3: 필수 필드를 강제하는 typestate 빌더 (constexpr, build() 는 둘 다 정했을 때만)
 *   연습 4: CS 트랜잭션을 타입으로 — transfer 는 select() 가 돌려준 Selected 에만
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
 * solution.cpp 와 독립: 아주 작은 mock 과 드라이버를 이 파일 안에 다시 둔다.
 */
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS %s\n", #expr); } \
                         else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------- 작은 mock: CR1 (SPE=bit6), DR 쓰기 수, 꺼진 상태 DR 쓰기, CS 핀 ---------- */
constexpr std::uint32_t SPE = 1u << 6;
struct Mock { std::uint32_t cr1 = 0; int dr_writes = 0; int dropped = 0; int cr1_writes = 0; bool cs_low = false; int cs_edges = 0; };
static Mock g;
static void cr1_write(std::uint32_t v) { g.cr1 = v; ++g.cr1_writes; }
static std::uint8_t dr_xfer(std::uint8_t b) {           // 켜져 있으면 ~b 를 돌려받는다
    if ((g.cr1 & SPE) == 0u) { ++g.dropped; return 0; }
    ++g.dr_writes;
    return static_cast<std::uint8_t>(~b);
}
struct Disabled {}; struct Configured {}; struct Enabled {};

/* ================= 연습 1: CheckedSpi ================= */
static int g_dead_use = 0;                               // 디버그 빌드라면 여기서 assert/fault log
template <class S> class CheckedSpi {
public:
    static CheckedSpi make() noexcept requires std::same_as<S, Disabled> { return CheckedSpi{}; }
    CheckedSpi(const CheckedSpi&) = delete;
    CheckedSpi(CheckedSpi&& o) noexcept : live_(o.live_) { o.live_ = false; }   // 이동하면 원본은 죽는다
    CheckedSpi& operator=(CheckedSpi&&) = delete;

    CheckedSpi<Configured> configure(std::uint32_t cfg) && noexcept requires std::same_as<S, Disabled> {
        if (!alive()) return CheckedSpi<Configured>{false};
        live_ = false;
        cr1_write(cfg);
        return CheckedSpi<Configured>{true};
    }
    CheckedSpi<Enabled> enable() && noexcept requires std::same_as<S, Configured> {
        if (!alive()) return CheckedSpi<Enabled>{false};
        live_ = false;
        cr1_write(g.cr1 | SPE);
        return CheckedSpi<Enabled>{true};
    }
    CheckedSpi<Configured> disable() && noexcept requires std::same_as<S, Enabled> {
        if (!alive()) return CheckedSpi<Configured>{false};
        live_ = false;
        cr1_write(g.cr1 & ~SPE);
        return CheckedSpi<Configured>{true};
    }
    int transfer(std::uint8_t b) & noexcept requires std::same_as<S, Enabled> {
        if (!alive()) return -1;                         // 하드웨어에 닿기 전에 막는다
        return dr_xfer(b);
    }
    bool live() const noexcept { return live_; }

private:
    template <class> friend class CheckedSpi;
    explicit CheckedSpi(bool live = true) noexcept : live_(live) {}
    bool alive() const noexcept { if (!live_) ++g_dead_use; return live_; }
    bool live_;
};
static_assert(sizeof(CheckedSpi<Enabled>) == 1, "비용: 상태 플래그 1바이트");
static_assert(!std::is_trivially_copyable_v<CheckedSpi<Enabled>>, "사용자 정의 이동 → 더는 trivially copyable 이 아니다");

static void test_ex1() {
    std::printf("-- 연습 1: CheckedSpi --\n");
    g = Mock{}; g_dead_use = 0;
    auto en = CheckedSpi<Disabled>::make().configure(0x314u).enable();
    CHECK(en.transfer(0x0F) == 0xF0 && en.live());
    auto off = std::move(en).disable();
    CHECK(!en.live() && off.live());
    CHECK(en.transfer(0x11) == -1 && g_dead_use == 1 && g.dropped == 0);   // 구멍을 런타임에 잡았다, DR 은 안 건드림
    auto en2 = std::move(off).enable();
    auto en3 = std::move(off).enable();                  // 같은 Configured 를 두 번 → 두 번째는 죽은 객체
    CHECK(en2.live() && !en3.live() && g_dead_use == 2 && g.cr1_writes == 4);
    CheckedSpi<Enabled> moved = std::move(en2);          // 진짜 이동 생성자도 원본을 죽인다
    CHECK(moved.live() && !en2.live() && moved.transfer(1) == 0xFE);
}

/* ================= 연습 2: DynSpi (런타임 탈출구) ================= */
enum class St : std::uint8_t { Disabled, Configured, Enabled };
enum class Err : std::uint8_t { Ok, WrongState };
class DynSpi {
public:
    explicit DynSpi(St s) noexcept : st_(s) {}           // adopt 처럼 "읽어서 알게 된" 상태로 시작
    Err configure(std::uint32_t cfg) noexcept {
        if (st_ != St::Disabled) return Err::WrongState;
        cr1_write(cfg); st_ = St::Configured; return Err::Ok;
    }
    Err enable() noexcept {
        if (st_ != St::Configured) return Err::WrongState;
        cr1_write(g.cr1 | SPE); st_ = St::Enabled; return Err::Ok;
    }
    Err disable() noexcept {
        if (st_ != St::Enabled) return Err::WrongState;
        cr1_write(g.cr1 & ~SPE); st_ = St::Configured; return Err::Ok;
    }
    Err transfer(std::uint8_t b, std::uint8_t& rx) noexcept {
        if (st_ != St::Enabled) return Err::WrongState;
        rx = dr_xfer(b); return Err::Ok;
    }
    St state() const noexcept { return st_; }
private:
    St st_;
};
static_assert(sizeof(DynSpi) == 1);

static void test_ex2() {
    std::printf("-- 연습 2: DynSpi --\n");
    g = Mock{};
    DynSpi s{St::Disabled};
    std::uint8_t rx = 0;
    CHECK(s.transfer(0x9F, rx) == Err::WrongState && g.dropped == 0 && g.dr_writes == 0);   // typestate 라면 컴파일 에러
    CHECK(s.enable() == Err::WrongState && g.cr1_writes == 0);
    CHECK(s.configure(0x314u) == Err::Ok && s.enable() == Err::Ok && s.state() == St::Enabled);
    CHECK(s.configure(0x307u) == Err::WrongState && g.cr1 == (0x314u | SPE));               // 켜진 채 재설정 거부
    CHECK(s.transfer(0x5A, rx) == Err::Ok && rx == 0xA5u);
    CHECK(s.disable() == Err::Ok && s.transfer(1, rx) == Err::WrongState && g.dropped == 0);
    DynSpi h{St::Enabled};                                                                    // 핸드오프: 켜진 상태로 시작
    g.cr1 = 0x347u;
    CHECK(h.configure(0x314u) == Err::WrongState && h.disable() == Err::Ok && h.state() == St::Configured);
}

/* ================= 연습 3: typestate 빌더 ================= */
enum class Mode : std::uint8_t { M0, M1, M2, M3 };
enum class Baud : std::uint8_t { Div2, Div4, Div8, Div16, Div32, Div64, Div128, Div256 };
struct Config { Mode mode; Baud baud; bool lsb_first; };

template <bool HasMode = false, bool HasBaud = false> class ConfigBuilder {
public:
    constexpr ConfigBuilder() noexcept = default;
    constexpr ConfigBuilder<true, HasBaud> mode(Mode m) const noexcept requires (!HasMode) { return {m, b_, lsb_}; }
    constexpr ConfigBuilder<HasMode, true> baud(Baud b) const noexcept requires (!HasBaud) { return {m_, b, lsb_}; }
    constexpr ConfigBuilder lsb_first() const noexcept { return {m_, b_, true}; }            // 선택 필드: 아무 때나
    constexpr Config build() const noexcept requires (HasMode && HasBaud) { return {m_, b_, lsb_}; }
private:
    template <bool, bool> friend class ConfigBuilder;
    constexpr ConfigBuilder(Mode m, Baud b, bool l) noexcept : m_(m), b_(b), lsb_(l) {}
    Mode m_ = Mode::M0;
    Baud b_ = Baud::Div2;
    bool lsb_ = false;
};
template <class B> concept CanBuild = requires(const B& b) { b.build(); };
template <class B> concept CanSetMode = requires(const B& b) { b.mode(Mode::M0); };
constexpr std::uint32_t encode(const Config& c) {
    return 0x304u | (std::uint32_t(c.baud) << 3) | std::uint32_t(c.mode) | (c.lsb_first ? 0x80u : 0u);
}
constexpr Config kCfg = ConfigBuilder<>{}.baud(Baud::Div2).lsb_first().mode(Mode::M3).build();   // 순서 자유
static_assert(encode(kCfg) == 0x387u);
static_assert(!CanBuild<ConfigBuilder<>> && !CanBuild<ConfigBuilder<true, false>> && CanBuild<ConfigBuilder<true, true>>);
static_assert(!CanSetMode<ConfigBuilder<true, false>>, "같은 필드 두 번 금지");
static_assert(sizeof(ConfigBuilder<true, true>) == 3);

static void test_ex3() {
    std::printf("-- 연습 3: typestate 빌더 --\n");
    CHECK(encode(kCfg) == 0x387u);
    const Config c = ConfigBuilder<>{}.mode(Mode::M1).baud(Baud::Div4).build();
    CHECK(encode(c) == 0x30Du && !c.lsb_first);
    CHECK(CanBuild<decltype(ConfigBuilder<>{}.mode(Mode::M0).baud(Baud::Div8))>);
    CHECK(!CanBuild<decltype(ConfigBuilder<>{}.mode(Mode::M0).lsb_first())>);
}

/* ================= 연습 4: CS 트랜잭션을 타입으로 ================= */
class Bus;
class Selected {                                         // 존재하는 동안 CS=low. transfer 는 여기에만 있다
public:
    Selected(const Selected&) = delete;
    Selected& operator=(const Selected&) = delete;
    ~Selected() { g.cs_low = false; ++g.cs_edges; }
    std::uint8_t transfer(std::uint8_t b) noexcept { return dr_xfer(b); }
private:
    friend class Bus;
    explicit Selected(Bus& b) noexcept : bus_(b) { g.cs_low = true; ++g.cs_edges; }
    [[maybe_unused]] Bus& bus_;                          // "빌린" 버스 (수명은 C++ 가 검사하지 않는다)
};
class Bus {                                              // Enabled 상태의 SPI 라고 보자
public:
    Bus() noexcept { cr1_write(0x314u | SPE); }
    [[nodiscard]] Selected select() & noexcept { return Selected{*this}; }   // prvalue → 복사·이동 없이 호출자에게
};
template <class T> concept HasTransfer = requires(T& t) { t.transfer(std::uint8_t{}); };
template <class T> concept CanSelectTemp = requires { std::declval<T&&>().select(); };
static_assert(!HasTransfer<Bus> && HasTransfer<Selected>, "CS 없이 전송 불가");
static_assert(!CanSelectTemp<Bus>, "임시 Bus 에서 select 금지 (& 한정)");
static_assert(!std::is_move_constructible_v<Selected>, "트랜잭션을 함수 밖으로 빼돌리지 못한다 (prvalue 반환만)");

static std::uint32_t read_id(Bus& bus) {
    auto t = bus.select();
    (void)t.transfer(0x9F);
    std::uint32_t id = 0;
    for (int i = 0; i < 3; ++i) id = (id << 8) | t.transfer(static_cast<std::uint8_t>(i));
    return id;                                           // t 소멸 → CS high
}

static void test_ex4() {
    std::printf("-- 연습 4: CS 트랜잭션 --\n");
    g = Mock{};
    Bus bus;
    CHECK(read_id(bus) == 0xFFFEFDu && !g.cs_low && g.cs_edges == 2 && g.dr_writes == 4);
    {
        auto t = bus.select();
        CHECK(g.cs_low);
        (void)t.transfer(0);
    }
    CHECK(!g.cs_low && g.cs_edges == 4);
}

int main() {
    test_ex1();
    test_ex2();
    test_ex3();
    test_ex4();
    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
