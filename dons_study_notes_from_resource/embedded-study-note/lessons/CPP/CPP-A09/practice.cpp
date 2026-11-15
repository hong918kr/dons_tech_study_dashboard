/* CPP-A09 추가 연습 — 한 번에 검증
 *   연습 1: 0.9 → 0.9F 는 "경고 수정" 인가 "계약 변경" 인가 (Pid 필터, 비트 차이 계수)
 *   연습 2: set_speed — throw/전역 REG 매크로 대신 Status + 레지스터 구조체 (D6, D7)
 *   연습 3: uint16_t * uint16_t 의 int 승격 오버플로 (도구가 못 잡은 것)
 *   연습 4: NOLINT 위생 검사기 — 체크 이름 + 이탈(DEV) 번호 없는 억제는 거부
 *   연습 5: rule of 5 가 맞는 경우 — 이동 전용 DMA 채널 핸들 (자기 대입 포함)
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
 */
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <type_traits>
#include <utility>

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS %s\n", #expr); } \
                         else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static std::uint32_t g_rng = 0x2545F491u;
static std::uint32_t rnd() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

/* ---------------- 연습 1: 필터 상수의 타입 ---------------- */
namespace ex1 {
struct FiltD {                                     // fixed_mc100 의 Pid 필터 줄 (legacy 와 비트 동등)
    float d{0.0F}, prev{0.0F};
    float step(float err)
    {
        d = static_cast<float>((0.9 * static_cast<double>(d)) + (0.1 * static_cast<double>(err - prev)));
        prev = err;
        return d;
    }
};
struct FiltF {                                     // "경고 고치기": 0.9F / 0.1F — M4F 에서 하드웨어 FPU 로
    float d{0.0F}, prev{0.0F};
    float step(float err)
    {
        d = (0.9F * d) + (0.1F * (err - prev));
        prev = err;
        return d;
    }
};
}  // namespace ex1

/* ---------------- 연습 2: set_speed ---------------- */
namespace ex2 {
struct PwmRegs {                                   // 레지스터 블록을 구조체로 (오프셋은 legacy 매크로와 같게)
    std::array<volatile std::uint32_t, 4> reserved0;   // 0x00..0x0F
    volatile std::uint32_t hall_dr;                    // 0x10
    volatile std::uint32_t ntc_dr;                     // 0x14
    std::array<volatile std::uint32_t, 5> reserved1;   // 0x18..0x2B
    volatile std::uint32_t arr;                        // 0x2C
    volatile std::uint32_t reserved2;                  // 0x30
    std::array<volatile std::uint32_t, 2> ccr;         // 0x34, 0x38
};
static_assert(offsetof(PwmRegs, hall_dr) == 0x10 && offsetof(PwmRegs, arr) == 0x2C && offsetof(PwmRegs, ccr) == 0x34);
static_assert(std::is_standard_layout_v<PwmRegs>);

enum class Status : std::uint8_t { Ok, BadMotor, BadRpm };
inline constexpr std::int32_t kMaxRpm = 12000;

[[nodiscard]] Status set_speed(PwmRegs& pwm, std::size_t motor, std::int32_t rpm) noexcept
{
    Status st = Status::Ok;
    if (motor >= pwm.ccr.size()) {
        st = Status::BadMotor;
    } else if ((rpm < 0) || (rpm > kMaxRpm)) {
        st = Status::BadRpm;
    } else {
        const std::uint64_t ccr = (static_cast<std::uint64_t>(rpm) * pwm.arr) / static_cast<std::uint64_t>(kMaxRpm);
        pwm.ccr[motor] = static_cast<std::uint32_t>(ccr);
    }
    return st;
}
// legacy 식 그대로 (REG(PWM_CCR + motor*4) = rpm * REG(PWM_ARR) / MAX_RPM;) — int * uint32 → uint32 연산
inline std::uint32_t legacy_ccr(int rpm, std::uint32_t arr) { return rpm * arr / 12000; }
}  // namespace ex2

/* ---------------- 연습 3: 승격 ---------------- */
namespace ex3 {
[[nodiscard]] constexpr std::uint32_t area(std::uint16_t w, std::uint16_t h) noexcept
{
    return static_cast<std::uint32_t>(w) * static_cast<std::uint32_t>(h);   // 곱하기 "전에" 넓힌다
}
}  // namespace ex3

/* ---------------- 연습 4: NOLINT 위생 ---------------- */
namespace ex4 {
// 규칙: NOLINT 가 없으면 OK. 있으면 (a) 괄호 안에 체크 이름이 하나 이상, (b) '*' 글롭 금지,
//       (c) 닫는 괄호 뒤에 "DEV-<숫자>" 이탈 번호, (d) NOLINTBEGIN/END 범위 억제 금지.
[[nodiscard]] constexpr bool nolint_ok(std::string_view line) noexcept
{
    const std::size_t at = line.find("NOLINT");
    if (at == std::string_view::npos) {
        return true;
    }
    std::string_view rest = line.substr(at + 6);
    if (rest.starts_with("BEGIN") || rest.starts_with("END")) {
        return false;
    }
    if (rest.starts_with("NEXTLINE")) {
        rest.remove_prefix(8);
    }
    if (!rest.starts_with("(")) {
        return false;
    }
    const std::size_t close = rest.find(')');
    if (close == std::string_view::npos || close == 1) {
        return false;
    }
    const std::string_view checks = rest.substr(1, close - 1);
    if (checks.find('*') != std::string_view::npos) {
        return false;
    }
    const std::string_view tail = rest.substr(close + 1);
    const std::size_t dev = tail.find("DEV-");
    return dev != std::string_view::npos && dev + 4 < tail.size() && tail[dev + 4] >= '0' && tail[dev + 4] <= '9';
}
}  // namespace ex4

/* ---------------- 연습 5: rule of 5 — 이동 전용 자원 핸들 ---------------- */
namespace ex5 {
inline std::uint8_t g_dma_busy = 0;                // 채널 8개 비트맵 (모의 하드웨어)
inline int g_releases = 0;

class DmaChannel {
public:
    static DmaChannel claim() noexcept             // 빈 채널이 없으면 invalid 핸들
    {
        for (std::uint8_t ch = 0; ch < 8U; ++ch) {
            const auto bit = static_cast<std::uint8_t>(1U << ch);
            if ((g_dma_busy & bit) == 0U) {
                g_dma_busy = static_cast<std::uint8_t>(g_dma_busy | bit);
                return DmaChannel{ch};
            }
        }
        return DmaChannel{kNone};
    }
    DmaChannel(const DmaChannel&) = delete;
    DmaChannel& operator=(const DmaChannel&) = delete;
    DmaChannel(DmaChannel&& o) noexcept : ch_{std::exchange(o.ch_, kNone)} {}
    DmaChannel& operator=(DmaChannel&& o) noexcept
    {
        if (this != &o) {                          // 자기 이동 대입: 자원을 잃지 않는다
            release();
            ch_ = std::exchange(o.ch_, kNone);
        }
        return *this;
    }
    ~DmaChannel() { release(); }
    [[nodiscard]] bool valid() const noexcept { return ch_ != kNone; }
    [[nodiscard]] std::uint8_t channel() const noexcept { return ch_; }
private:
    static constexpr std::uint8_t kNone = 0xFFU;
    explicit DmaChannel(std::uint8_t ch) noexcept : ch_{ch} {}
    void release() noexcept
    {
        if (ch_ != kNone) {
            g_dma_busy = static_cast<std::uint8_t>(g_dma_busy & ~(1U << ch_));
            ++g_releases;
            ch_ = kNone;
        }
    }
    std::uint8_t ch_;
};
}  // namespace ex5

int main()
{
    /* 연습 1 */
    {
        ex1::FiltD a;
        ex1::FiltF b;
        int diff = 0;
        float worst = 0.0F;
        for (int i = 0; i < 20000; i++) {
            const float e = static_cast<float>(static_cast<std::int32_t>(rnd() % 60001u) - 30000) / 100.0F;
            const float x = a.step(e), y = b.step(e);
            if (std::bit_cast<std::uint32_t>(x) != std::bit_cast<std::uint32_t>(y)) diff++;
            worst = std::max(worst, std::fabs(x - y));
            b.d = a.d;                              // 매 스텝 상태를 맞춰 "한 스텝" 차이만 본다
        }
        std::printf("ex1: %d / 20000 steps differ, worst |diff| = %g\n", diff, static_cast<double>(worst));
        CHECK(diff > 1000);                         // 비트 동등이 아니다 → D 목록에 올릴 계약 변경
        CHECK(worst < 1.0e-4F);                     // 크기는 float 몇 ulp 수준
    }

    /* 연습 2 */
    {
        ex2::PwmRegs pwm{};
        pwm.arr = 1000;
        unsigned bad = 0;
        for (std::int32_t rpm = 0; rpm <= ex2::kMaxRpm; rpm++) {
            for (std::size_t m = 0; m < 2; m++) {
                if (ex2::set_speed(pwm, m, rpm) != ex2::Status::Ok || pwm.ccr[m] != ex2::legacy_ccr(rpm, pwm.arr)) bad++;
            }
        }
        CHECK(bad == 0);                                                       // 유효 도메인: legacy 와 같은 값
        pwm.ccr[0] = 7;
        pwm.ccr[1] = 7;
        CHECK(ex2::set_speed(pwm, 2, 100) == ex2::Status::BadMotor && pwm.ccr[0] == 7 && pwm.ccr[1] == 7);   // D6
        CHECK(ex2::legacy_ccr(-1, 1000) == 357913u);                           // legacy: 음수 rpm → 거의 최대 듀티
        CHECK(ex2::set_speed(pwm, 0, -1) == ex2::Status::BadRpm && ex2::set_speed(pwm, 0, 12001) == ex2::Status::BadRpm &&
              pwm.ccr[0] == 7);                                                // D7
        pwm.arr = 0xFFFFFFFFu;
        CHECK(ex2::set_speed(pwm, 1, 12000) == ex2::Status::Ok && pwm.ccr[1] == 0xFFFFFFFFu);   // 64비트 중간값
        CHECK(ex2::legacy_ccr(12000, 0xFFFFFFFFu) != 0xFFFFFFFFu);             // legacy 는 32비트에서 감싼다
    }

    /* 연습 3 */
    {
        CHECK(ex3::area(0xFFFF, 0xFFFF) == 0xFFFE0001u);
        unsigned bad = 0;
        for (int i = 0; i < 100000; i++) {
            const auto w = static_cast<std::uint16_t>(rnd()), h = static_cast<std::uint16_t>(rnd());
            if (ex3::area(w, h) != static_cast<std::uint32_t>(static_cast<std::uint64_t>(w) * h)) bad++;
        }
        CHECK(bad == 0);
        static_assert(ex3::area(0xFFFF, 0xFFFF) == 0xFFFE0001u);              // 상수 평가 = UB 면 컴파일 에러
    }

    /* 연습 4: fixed_mc100.cpp 의 실제 두 줄 + 나쁜 예 */
    {
        using ex4::nolint_ok;
        CHECK(nolint_ok("    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr) DEV-1"));
        CHECK(nolint_ok("// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) DEV-2: unsigned index vs signed rpm"));
        CHECK(nolint_ok("    return x;   // plain line"));
        CHECK(!nolint_ok("    p = (T*)q;  // NOLINT"));
        CHECK(!nolint_ok("    p = (T*)q;  // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)"));      // 이유 없음
        CHECK(!nolint_ok("    // NOLINTNEXTLINE(cppcoreguidelines-*) DEV-3"));                       // 글롭
        CHECK(!nolint_ok("// NOLINTBEGIN(misc-no-recursion) DEV-4"));                                 // 범위 억제
        CHECK(!nolint_ok("// NOLINT() DEV-5") && !nolint_ok("// NOLINT(misc-no-recursion) DEV-x"));
    }

    /* 연습 5 */
    {
        static_assert(!std::is_copy_constructible_v<ex5::DmaChannel> && !std::is_copy_assignable_v<ex5::DmaChannel>);
        static_assert(std::is_nothrow_move_constructible_v<ex5::DmaChannel> && std::is_nothrow_move_assignable_v<ex5::DmaChannel>);
        {
            ex5::DmaChannel a = ex5::DmaChannel::claim();
            ex5::DmaChannel b = ex5::DmaChannel::claim();
            CHECK(a.valid() && b.valid() && a.channel() == 0 && b.channel() == 1 && ex5::g_dma_busy == 0x03);
            ex5::DmaChannel c = std::move(a);
            CHECK(!a.valid() && c.channel() == 0 && ex5::g_dma_busy == 0x03);   // NOLINT(bugprone-use-after-move) DEV-9 moved-from 상태 검사
            c = std::move(b);                                                  // c 의 0번을 먼저 반납
            CHECK(c.channel() == 1 && ex5::g_dma_busy == 0x02 && ex5::g_releases == 1);
            ex5::DmaChannel& alias = c;
            c = std::move(alias);                                              // 자기 이동 대입
            CHECK(c.valid() && c.channel() == 1 && ex5::g_dma_busy == 0x02);
        }
        CHECK(ex5::g_dma_busy == 0 && ex5::g_releases == 2);                   // 모든 채널 반납, 정확히 한 번씩
        std::array<ex5::DmaChannel, 8> all{ex5::DmaChannel::claim(), ex5::DmaChannel::claim(), ex5::DmaChannel::claim(),
                                           ex5::DmaChannel::claim(), ex5::DmaChannel::claim(), ex5::DmaChannel::claim(),
                                           ex5::DmaChannel::claim(), ex5::DmaChannel::claim()};
        CHECK(ex5::g_dma_busy == 0xFF && !ex5::DmaChannel::claim().valid() && all[7].channel() == 7);
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
