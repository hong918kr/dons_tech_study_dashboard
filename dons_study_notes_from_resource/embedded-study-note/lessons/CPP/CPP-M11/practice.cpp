// CPP-M11 추가 연습 — TRY_ASSIGN(표준 C++), must-check Result, and_then 파이프라인, 로그 코드 패킹, 첫 에러 수집
//   build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

static int g_pass = 0, g_total = 0;
#define CHECK(expr)                                                     \
    do {                                                                \
        ++g_total;                                                      \
        if (expr) { ++g_pass; std::printf("PASS  %s\n", #expr); }       \
        else std::printf("FAIL  %s  (line %d)\n", #expr, __LINE__);     \
    } while (0)

/* 본문의 Result 를 trivial 타입만 받도록 줄인 지역 버전 (이 파일은 자기완결) */
template <class E>
struct Unexpected {
    E error;
    constexpr explicit Unexpected(E e) : error(e) {}
};
template <class E> Unexpected(E) -> Unexpected<E>;

template <class T, class E>
class [[nodiscard]] Result {
public:
    using value_type = T;
    using error_type = E;
    constexpr Result(T v) : val_(v), has_(true) {}
    template <class G> requires std::convertible_to<G, E>
    constexpr Result(Unexpected<G> u) : err_(u.error), has_(false) {}
    constexpr bool has_value() const { return has_; }
    constexpr explicit operator bool() const { return has_; }
    constexpr T value() const { return val_; }
    constexpr E error() const { return err_; }
    constexpr T value_or(T f) const { return has_ ? val_ : f; }
    template <class F>
    constexpr auto and_then(F f) const {
        using R = std::invoke_result_t<F, T>;
        if (!has_) return R(Unexpected<E>(err_));
        return f(val_);
    }
    template <class F>
    constexpr auto map_error(F f) const {
        using G = std::invoke_result_t<F, E>;
        if (has_) return Result<T, G>(val_);
        return Result<T, G>(Unexpected<G>(f(err_)));
    }
private:
    union { T val_; E err_; };
    bool has_;
};
template <class E>
class [[nodiscard]] Result<void, E> {
public:
    using value_type = void;
    using error_type = E;
    constexpr Result() = default;
    template <class G> requires std::convertible_to<G, E>
    constexpr Result(Unexpected<G> u) : err_(u.error), has_(false) {}
    constexpr bool has_value() const { return has_; }
    constexpr explicit operator bool() const { return has_; }
    constexpr void value() const {}
    constexpr E error() const { return err_; }
private:
    E err_{};
    bool has_ = true;
};

enum class BusError : std::uint8_t { None = 0, AddrNack, DataNack, ArbLost, Timeout, BusStuck };
enum class DrvCode : std::uint8_t { None = 0, Bus, BadChipId, NotReady, InvalidData, BadArg };
enum class AppCode : std::uint8_t { None = 0, SensorOffline, WrongPart, Busy, DataCorrupt, BadConfig, BusFatal };

using RI = Result<int, BusError>;
using RV = Result<void, BusError>;
static RI ok_i(int v) { return v; }
static RI err_i(BusError e) { return Unexpected{e}; }

/* ---------------- 연습 1: 표준 C++ 만으로 — TRY_ASSIGN ---------------- */
// statement expression 없이 "선언 + early return" 을 두 줄로 펼친다.
// 임시 변수 이름은 __LINE__ 으로 유일하게 (같은 줄에 두 번 쓰면 안 됨 — 그건 문서화).
#define TRY_CAT2(a, b) a##b
#define TRY_CAT(a, b) TRY_CAT2(a, b)
#define TRY_ASSIGN(decl, ...)                                                       \
    auto TRY_CAT(try_tmp_, __LINE__) = (__VA_ARGS__);                               \
    if (!TRY_CAT(try_tmp_, __LINE__).has_value())                                   \
        return Unexpected{TRY_CAT(try_tmp_, __LINE__).error()};                     \
    decl = TRY_CAT(try_tmp_, __LINE__).value()
#define TRY_DO(...)                                                                 \
    do {                                                                            \
        auto TRY_CAT(try_tmp_, __LINE__) = (__VA_ARGS__);                           \
        if (!TRY_CAT(try_tmp_, __LINE__).has_value())                               \
            return Unexpected{TRY_CAT(try_tmp_, __LINE__).error()};                 \
    } while (0)

static int g_steps = 0;
static RI sum3(RI a, RI b, RI c) {
    TRY_ASSIGN(const int x, a);
    ++g_steps;
    TRY_ASSIGN(const int y, b);
    ++g_steps;
    TRY_ASSIGN(const int z, c);
    ++g_steps;
    return x + y + z;
}
static RV void_step(bool good) {
    if (!good) return Unexpected{BusError::Timeout};
    return {};
}
static RI do_then(bool good, int v) {
    TRY_DO(void_step(good));
    return v + 1;
}

/* ---------------- 연습 2: must-check Result (LLVM Error 방식의 디버그 강제) ---------------- */
// [[nodiscard]] 는 "(void)" 와 "변수에 받고 안 보기" 를 못 막는다. 디버그 빌드에서는 소멸자가 "한 번도 검사 안 됨" 을 잡는다.
// 대가: 소멸자가 non-trivial 이 되어 ABI(레지스터 반환)가 바뀐다 — 릴리스에서는 아래 kCheck=false 처럼 꺼서 trivial 로.
static int g_unchecked = 0;

template <bool kCheck>
class [[nodiscard]] Status {
public:
    constexpr Status() = default;
    constexpr explicit Status(BusError e) : err_(e) {}
    Status(Status&& o) noexcept : err_(o.err_), checked_(o.checked_) { o.checked_ = true; }
    Status(const Status&) = delete;
    ~Status() requires kCheck { if (!checked_) ++g_unchecked; }
    ~Status() requires (!kCheck) = default;

    bool ok() { checked_ = true; return err_ == BusError::None; }
    BusError error() { checked_ = true; return err_; }
    void ignore() { checked_ = true; }   // 의도적 무시는 이름을 남긴다 (grep 가능)
private:
    BusError err_ = BusError::None;
    bool checked_ = false;
};
using CheckedStatus = Status<true>;
using ReleaseStatus = Status<false>;
static_assert(!std::is_trivially_destructible_v<CheckedStatus>);
static_assert(std::is_trivially_destructible_v<ReleaseStatus>);

static CheckedStatus chk_write(bool fail) {
    return fail ? CheckedStatus(BusError::Timeout) : CheckedStatus();
}

/* ---------------- 연습 3: and_then 파이프라인 = TRY 체인 ---------------- */
static RI half_if_even(int v) {
    if (v % 2) return Unexpected{BusError::DataNack};
    return v / 2;
}
static RI clamp_range(int v) {
    if (v > 100) return Unexpected{BusError::BusStuck};
    return v;
}
static RI pipe_monadic(RI a) { return a.and_then(half_if_even).and_then(clamp_range); }
static RI pipe_try(RI a) {
    TRY_ASSIGN(const int x, a);
    TRY_ASSIGN(const int y, half_if_even(x));
    TRY_ASSIGN(const int z, clamp_range(y));
    return z;
}

/* ---------------- 연습 4: 플래시 로그용 32비트 코드 ---------------- */
// [31:24] AppCode  [23:16] sensor  [15:12] DrvCode  [11:8] BusError  [7:0] reg
struct LogFields { AppCode app; std::uint8_t sensor; DrvCode drv; BusError bus; std::uint8_t reg; };
constexpr std::uint32_t to_log32(LogFields f) {
    return static_cast<std::uint32_t>(f.app) << 24 | static_cast<std::uint32_t>(f.sensor) << 16 |
           (static_cast<std::uint32_t>(f.drv) & 0xFu) << 12 | (static_cast<std::uint32_t>(f.bus) & 0xFu) << 8 | f.reg;
}
constexpr LogFields from_log32(std::uint32_t c) {
    return LogFields{static_cast<AppCode>(c >> 24), static_cast<std::uint8_t>((c >> 16) & 0xFF),
                     static_cast<DrvCode>((c >> 12) & 0xF), static_cast<BusError>((c >> 8) & 0xF),
                     static_cast<std::uint8_t>(c & 0xFF)};
}
static_assert(to_log32({AppCode::BusFatal, 0x12, DrvCode::Bus, BusError::BusStuck, 0x0F}) == 0x0612150Fu);

/* ---------------- 연습 5: 최선형 초기화 — 끝까지 돌고 첫 에러만 기억 ---------------- */
// fail-fast(TRY) 가 아니라 "모든 단계를 시도" 해야 할 때 (예: 여러 센서 bring-up).
template <class E>
constexpr Result<void, E> first_error() { return {}; }
template <class E, class... Rest>
constexpr Result<void, E> first_error(Result<void, E> head, Rest... rest) {
    Result<void, E> tail = first_error<E>(rest...);    // 인자는 호출 전에 이미 모두 평가되었다
    return head.has_value() ? tail : head;
}

/* ---------------- 테스트 ---------------- */
int main() {
    // 연습 1
    g_steps = 0;
    CHECK(sum3(1, 2, 3).value_or(0) == 6 && g_steps == 3);
    g_steps = 0;
    auto s = sum3(1, Unexpected{BusError::ArbLost}, 3);
    CHECK(!s.has_value() && s.error() == BusError::ArbLost && g_steps == 1);
    g_steps = 0;
    auto s2 = sum3(Unexpected{BusError::Timeout}, 2, 3);
    CHECK(!s2.has_value() && s2.error() == BusError::Timeout && g_steps == 0);
    CHECK(do_then(true, 4).value_or(0) == 5);
    CHECK(do_then(false, 4).error() == BusError::Timeout);

    // 연습 2
    g_unchecked = 0;
    { auto s3 = chk_write(false); (void)s3; }                 // (void) 는 경고를 없앨 뿐 — 소멸자가 잡는다
    CHECK(g_unchecked == 1);
    { auto s3 = chk_write(true); CHECK(!s3.ok()); }
    CHECK(g_unchecked == 1);
    { auto s3 = chk_write(true); CHECK(s3.error() == BusError::Timeout); }
    CHECK(g_unchecked == 1);
    { auto s3 = chk_write(true); s3.ignore(); }               // 의도적 무시
    CHECK(g_unchecked == 1);
    { auto s3 = chk_write(false); CheckedStatus moved = std::move(s3); CHECK(moved.ok()); }
    CHECK(g_unchecked == 1);                                  // 이동으로 책임이 옮겨 가야 이중 집계가 없다

    // 연습 3: 두 방식이 모든 입력에서 같다 (표)
    struct In { int v; bool bad; };
    const In ins[] = {{8, false}, {7, false}, {400, false}, {0, false}, {202, false}, {1, true}};
    int same = 0;
    for (const In& i : ins) {
        const RI a = i.bad ? err_i(BusError::Timeout) : ok_i(i.v);
        const RI p = pipe_monadic(a), q = pipe_try(a);
        same += p.has_value() == q.has_value() && (p.has_value() ? p.value() == q.value() : p.error() == q.error());
    }
    CHECK(same == 6);
    CHECK(pipe_monadic(ok_i(8)).value_or(0) == 4);
    CHECK(pipe_monadic(ok_i(7)).error() == BusError::DataNack);
    CHECK(pipe_monadic(ok_i(400)).error() == BusError::BusStuck);

    // 연습 4: 왕복
    const LogFields lf[] = {
        {AppCode::None, 0, DrvCode::None, BusError::None, 0},
        {AppCode::BusFatal, 0xFF, DrvCode::Bus, BusError::BusStuck, 0xFF},
        {AppCode::WrongPart, 3, DrvCode::BadChipId, BusError::None, 0x0F},
    };
    int rt = 0;
    for (const LogFields& f : lf) {
        const LogFields g = from_log32(to_log32(f));
        rt += g.app == f.app && g.sensor == f.sensor && g.drv == f.drv && g.bus == f.bus && g.reg == f.reg;
    }
    CHECK(rt == 3);
    CHECK(to_log32({AppCode::Busy, 1, DrvCode::Bus, BusError::DataNack, 0x20}) == 0x03011220u);

    // 연습 5
    auto e1 = first_error<BusError>(RV{}, RV{Unexpected{BusError::ArbLost}}, RV{Unexpected{BusError::BusStuck}});
    CHECK(!e1.has_value() && e1.error() == BusError::ArbLost);
    auto e2 = first_error<BusError>(RV{}, RV{});
    CHECK(e2.has_value());
    auto e3 = first_error<BusError>(RV{Unexpected{BusError::Timeout}});
    CHECK(!e3.has_value() && e3.error() == BusError::Timeout);
    auto e4 = first_error<BusError>();
    CHECK(e4.has_value());

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
