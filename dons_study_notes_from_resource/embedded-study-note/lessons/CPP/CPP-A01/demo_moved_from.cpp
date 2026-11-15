// CPP-A01 demo_moved_from.cpp — C++ typestate 의 구멍: std::move 한 뒤에도 이름은 살아 있고, 타입도 그대로다.
//   bash asm_targets.sh tidy    → clang-tidy bugprone-use-after-move, clang -Wconsumed 실제 출력
// 이 파일은 일부러 "컴파일되는" 버그를 담는다. 기본 경고(-Wall -Wextra)로는 아무것도 안 나온다.
#include "spi_typestate.hpp"

struct FakeHw {
    static std::uint32_t read(std::uint32_t) noexcept { return 0x2u; }
    static void write(std::uint32_t, std::uint32_t) noexcept {}
};
using Tok = PeriphToken<FakeHw>;
template <class S> using Sp = Spi<S, FakeHw>;

// 구멍 1: disable 로 넘긴 뒤 옛 이름으로 transfer — 하드웨어는 꺼졌다
std::uint8_t hole_after_disable(Sp<Enabled>& en) {
    auto r = std::move(en).disable();
    (void)r;
    return en.transfer(0x11).rx;
}
// 구멍 2: 같은 Disabled 를 두 번 configure → Configured 객체 둘이 SPI1 하나를 가리킨다
void hole_double_configure(Tok&& t) {
    auto d = Sp<Disabled>::from_reset(std::move(t));
    auto c1 = std::move(d).configure(Config{});
    auto c2 = std::move(d).configure(Config{Mode::M3, Baud::Div2, false});
    (void)c1; (void)c2;
}
// 구멍 3: 토큰 자체도 이동 후 재사용 가능
void hole_token_reuse(Tok&& t) {
    auto a = Sp<Disabled>::from_reset(std::move(t));
    auto b = Sp<Disabled>::from_reset(std::move(t));
    (void)a; (void)b;
}
// 완화: prvalue 체인 — 중간 상태에 이름을 붙이지 않으면 다시 쓸 이름도 없다
std::uint8_t ok_chain(Tok&& t) {
    auto en = Sp<Disabled>::from_reset(std::move(t)).configure(Config{}).enable();
    return en.transfer(1).rx;
}

// 비교: 전이가 && 한정이 아니었다면 — std::move 가 없으니 clang-tidy 도 아무것도 못 본다
struct LooseOn  { int xfer(int b) { return b + 1; } };
struct LooseOff { LooseOn enable() { return {}; } };
int loose(LooseOff& d) {
    LooseOn a = d.enable();
    LooseOn b = d.enable();                                // 같은 버그 (두 번 enable), 경고 없음
    return a.xfer(1) + b.xfer(2);
}

// clang 고유: Consumed 분석 (-Wconsumed). 속성은 clang 전용 — GCC 는 무시하고 -Wattributes 경고
#if defined(USE_CONSUMED)
#define TS_CONSUMABLE      [[clang::consumable(unconsumed)]]
#define TS_FRESH           [[clang::return_typestate(unconsumed)]]
#define TS_CONSUMES        [[clang::callable_when("unconsumed"), clang::set_typestate(consumed)]]
#define TS_NEEDS_LIVE      [[clang::callable_when("unconsumed")]]
#define TS_MOVE_SRC        [[clang::param_typestate(unconsumed), clang::return_typestate(consumed)]]
#else
#define TS_CONSUMABLE
#define TS_FRESH
#define TS_CONSUMES
#define TS_NEEDS_LIVE
#define TS_MOVE_SRC
#endif
struct COff; struct COn;
template <class S> class TS_CONSUMABLE CSpi {
public:
    TS_FRESH CSpi() noexcept = default;
    CSpi(const CSpi&) = delete;
    TS_FRESH CSpi(CSpi&& o TS_MOVE_SRC) noexcept { (void)o; }
    TS_CONSUMES CSpi<COn> enable() && requires std::same_as<S, COff> { return {}; }
    TS_CONSUMES CSpi<COff> disable() && requires std::same_as<S, COn> { return {}; }
    TS_NEEDS_LIVE int xfer(int b) requires std::same_as<S, COn> { return b + 1; }
};
int consumed_demo() {
    CSpi<COff> d;
    auto e = std::move(d).enable();
    auto off = std::move(e).disable();
    (void)off;
    return e.xfer(3);                                      // -Wconsumed: 'consumed' 상태에서 호출
}
