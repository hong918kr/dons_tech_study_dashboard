// CPP-B13 추가 연습 — 예외/RTTI 없는 에러 처리·타입 판별 패턴 4가지
//   build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <type_traits>

enum class Err : std::uint8_t { None = 0, Timeout, BadId, Range, NotReady };

template <typename T, typename E = Err>
class [[nodiscard]] Result {
public:
    static constexpr Result ok(T v)   { Result r; r.val_ = v; r.has_ = true;  return r; }
    static constexpr Result fail(E e) { Result r; r.err_ = e; r.has_ = false; return r; }
    constexpr bool has_value() const { return has_; }
    constexpr explicit operator bool() const { return has_; }
    constexpr E error() const { return has_ ? E{} : err_; }
    constexpr T value_or(T fb) const { return has_ ? val_ : fb; }
    constexpr T operator*() const { return val_; }          // 전제: has_value()

    /* 연습 1: and_then — 성공이면 f(값) 을 이어서 실행, 실패면 에러를 그대로 흘려보낸다 */
    template <class F>
    constexpr std::invoke_result_t<F, T> and_then(F f) const {
        using R = std::invoke_result_t<F, T>;
        return has_ ? f(val_) : R::fail(err_);
    }
private:
    T val_{};
    E err_{};
    bool has_ = false;
};

constexpr Result<std::uint16_t> odr_from_code(std::uint8_t code) {
    constexpr std::uint16_t hz[8] = {25, 50, 100, 200, 400, 800, 1600, 3200};
    if (code >= 8) return Result<std::uint16_t>::fail(Err::Range);
    return Result<std::uint16_t>::ok(hz[code]);
}
constexpr Result<std::uint32_t> period_us(std::uint16_t hz) {
    if (hz == 0) return Result<std::uint32_t>::fail(Err::Range);
    return Result<std::uint32_t>::ok(1'000'000u / hz);
}
constexpr Result<std::uint32_t> period_from_code(std::uint8_t code) {
    return odr_from_code(code).and_then(period_us);
}

/* 연습 2: 태그를 멤버 대신 virtual 함수로 — 객체 크기 vs 호출 비용 */
enum class Kind : std::uint8_t { Imu, Baro };
struct SensorT { explicit SensorT(Kind k) : k_(k) {} virtual ~SensorT() = default; Kind kind() const { return k_; } const Kind k_; };
struct SensorV { virtual ~SensorV() = default; virtual Kind kind() const = 0; };
struct ImuV final : SensorV {
    Kind kind() const override { return Kind::Imu; }
    static bool classof(const SensorV* s) { return s->kind() == Kind::Imu; }
};
struct BaroV final : SensorV {
    Kind kind() const override { return Kind::Baro; }
    static bool classof(const SensorV* s) { return s->kind() == Kind::Baro; }
};
template <class To, class From>
    requires std::is_base_of_v<From, To>
To* dyn_cast(From* p) { return (p && To::classof(p)) ? static_cast<To*>(p) : nullptr; }

/* 연습 3: 실패할 수 있는 "생성" — 생성자 대신 팩토리 + std::optional */
struct UartCfg {
    std::uint16_t brr;          // USART_BRR = round(pclk / baud), 16 ≤ BRR ≤ 0xFFFF (oversampling 16)
};
constexpr std::optional<UartCfg> make_uart(std::uint32_t pclk, std::uint32_t baud) {
    if (baud == 0) return std::nullopt;
    std::uint32_t brr = (pclk + baud / 2) / baud;           // 반올림
    if (brr < 16 || brr > 0xFFFF) return std::nullopt;
    return UartCfg{static_cast<std::uint16_t>(brr)};
}

/* 연습 4: C HAL 경계 — Result 를 errno 스타일 int + out 파라미터로 */
static std::uint8_t g_status = 0x80;
static Result<std::int16_t> read_x() {
    if (!(g_status & 0x80)) return Result<std::int16_t>::fail(Err::NotReady);
    return Result<std::int16_t>::ok(-1000);
}
constexpr int to_errno(Err e) {
    switch (e) {
    case Err::None:     return 0;
    case Err::Timeout:  return -ETIMEDOUT;
    case Err::BadId:    return -ENODEV;
    case Err::Range:    return -EINVAL;
    case Err::NotReady: return -EAGAIN;
    }
    return -EIO;
}
extern "C" int bmi_read_x(std::int16_t* out) {
    if (!out) return -EINVAL;
    auto r = read_x();
    if (!r) return to_errno(r.error());
    *out = *r;
    return 0;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main() {
    // 연습 1
    CHECK(period_from_code(2).value_or(0) == 10'000u);     // 100 Hz → 10 ms
    CHECK(period_from_code(7).value_or(0) == 312u);        // 3200 Hz → 312 us (버림)
    CHECK(period_from_code(9).error() == Err::Range);      // 앞 단계 에러가 그대로
    static_assert(period_from_code(0).value_or(0) == 40'000u);

    // 연습 2
    CHECK(sizeof(SensorV) == sizeof(void*));               // vptr 만
    CHECK(sizeof(SensorT) == 2 * sizeof(void*));           // vptr + Kind(1B) + 패딩
    ImuV imu; BaroV baro;
    SensorV* list[] = {&imu, &baro};
    CHECK(dyn_cast<ImuV>(list[0]) == &imu && dyn_cast<ImuV>(list[1]) == nullptr);
    CHECK(dyn_cast<BaroV>(list[1]) == &baro);

    // 연습 3
    CHECK(make_uart(16'000'000, 115'200).has_value() && make_uart(16'000'000, 115'200)->brr == 139);
    CHECK(!make_uart(16'000'000, 2'000'000).has_value());  // BRR = 8 < 16
    CHECK(!make_uart(16'000'000, 0).has_value());
    CHECK(make_uart(16'000'000, 300).value_or(UartCfg{0}).brr == 53'333);

    // 연습 4
    std::int16_t x = 0;
    CHECK(bmi_read_x(&x) == 0 && x == -1000);
    g_status = 0;
    CHECK(bmi_read_x(&x) == -EAGAIN);
    CHECK(bmi_read_x(nullptr) == -EINVAL);
    CHECK(to_errno(Err::Timeout) == -ETIMEDOUT);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
