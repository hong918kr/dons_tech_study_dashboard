// CPP-A01 demo_errors.cpp — "컴파일되면 안 되는" 코드 모음. -DSHOW_ERROR=N 으로 하나씩 켠다.
//   bash asm_targets.sh errors   (각각 실패해야 정상, SHOW_ERROR 없이는 통과)
// 하네스(solution.cpp)는 같은 사실을 requires-expression 개념(CanTransfer 등)과 static_assert 로 증명한다.
#include "spi_typestate.hpp"

struct FakeHw {
    static std::uint32_t read(std::uint32_t) noexcept { return 0; }
    static void write(std::uint32_t, std::uint32_t) noexcept {}
};
struct FakeHw2 : FakeHw {};                               // 다른 주변장치 (SPI2)
using Tok = PeriphToken<FakeHw>;
template <class S> using Sp = Spi<S, FakeHw>;

[[maybe_unused]] static Xfer xfer_free(Sp<Enabled>& s, std::uint8_t b) { return s.transfer(b); }   // 오버로드 집합 방식 비교용

void demo(Tok&& tok) {
    auto d = Sp<Disabled>::from_reset(std::move(tok));
#if SHOW_ERROR == 1
    auto c = std::move(d).configure(Config{});
    (void)c.transfer(0x9F);                               // E1: 켜기 전에 transfer
#elif SHOW_ERROR == 2
    auto e = std::move(d).enable();                       // E2: configure 를 건너뛰고 enable
#elif SHOW_ERROR == 3
    auto c = d.configure(Config{});                       // E3: std::move 없이 (lvalue 에서 전이)
#elif SHOW_ERROR == 4
    auto e = std::move(d).configure(Config{}).enable();
    auto e2 = e;                                          // E4: 상태 객체 복사 (소유권 복제)
#elif SHOW_ERROR == 5
    Tok forged;                                           // E5: 토큰 위조
#elif SHOW_ERROR == 6
    auto t = std::move(d).release();
    auto s2 = Spi<Disabled, FakeHw2>::from_reset(std::move(t));   // E6: SPI1 토큰으로 SPI2
#elif SHOW_ERROR == 7
    std::move(d).configure(Config{});                     // E7: 전이 결과를 버림 ([[nodiscard]] + -Werror)
#elif SHOW_ERROR == 8
    auto e = std::move(d).configure(Config{}).enable();
    auto t = std::move(e).release();                      // E8: 켜진 채로 소유권 반납
#elif SHOW_ERROR == 9
    auto c = std::move(d).configure(Config{});
    (void)xfer_free(c, 0x9F);                             // E9: E1 과 같은 실수, 자유 함수 오버로드 방식의 메시지
#else
    auto e = std::move(d).configure(Config{}).enable();
    (void)e.transfer(0x9F);
#endif
}
