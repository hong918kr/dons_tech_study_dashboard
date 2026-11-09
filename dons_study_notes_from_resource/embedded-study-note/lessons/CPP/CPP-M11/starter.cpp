// CPP-M11 예외 없는 에러 처리: Result<T, E> (expected 스타일)
//   build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
//   TRY 는 GNU statement expression (clang/gcc 확장) 이다 — ISO C++ 아님. 표준만 쓰는 대안은 practice.cpp 의 TRY_ASSIGN.
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <type_traits>
#include <utility>

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass = 0, g_total = 0;
#define CHECK(expr)                                                     \
    do {                                                                \
        ++g_total;                                                      \
        if (expr) { ++g_pass; std::printf("PASS  %s\n", #expr); }       \
        else std::printf("FAIL  %s  (line %d)\n", #expr, __LINE__);     \
    } while (0)

/* ---------------- Part A: Result<T, E> ---------------- */
static int g_bad_access = 0;   // 펌웨어라면 여기서 __BKPT() / 에러 로그 + 리셋

template <class U>
U bad_access_value() {         // 계약 위반 경로: 절대 constexpr 로 평가되지 않음
    ++g_bad_access;
    if constexpr (std::is_default_constructible_v<U>) return U{};
    else __builtin_trap();
}

// 에러를 값과 구별해 주는 포장지. T 와 E 가 같은 타입이어도 모호하지 않게 만든다.
template <class E>
struct Unexpected {
    E error;
    constexpr explicit Unexpected(E e) : error(std::move(e)) {}
};
template <class E> Unexpected(E) -> Unexpected<E>;

template <class T, class E> class Result;

template <class T, class E>
class [[nodiscard]] Result {
    template <class, class> friend class Result;
    static constexpr bool kTrivCopy = std::is_trivially_copy_constructible_v<T> && std::is_trivially_copy_constructible_v<E>;
    static constexpr bool kTrivMove = std::is_trivially_move_constructible_v<T> && std::is_trivially_move_constructible_v<E>;
    static constexpr bool kTrivDtor = std::is_trivially_destructible_v<T> && std::is_trivially_destructible_v<E>;
    static constexpr bool kCopyable = std::is_copy_constructible_v<T> && std::is_copy_constructible_v<E>;

public:
    using value_type = T;
    using error_type = E;

    constexpr Result(T v) : val_(std::move(v)), has_(true) {}                  // return 값;
    template <class G> requires std::convertible_to<G, E>
    constexpr Result(Unexpected<G> u) : err_(std::move(u.error)), has_(false) {}   // return Unexpected{e};

    // 특수 멤버: T, E 가 trivial 이면 trivial 로 남긴다 → AAPCS 레지스터 반환이 가능해진다 (P0848)
    constexpr Result(const Result&) requires kTrivCopy = default;
    constexpr Result(const Result& o) requires (!kTrivCopy && kCopyable) : has_(o.has_) {
        if (has_) std::construct_at(std::addressof(val_), o.val_);
        else      std::construct_at(std::addressof(err_), o.err_);
    }
    constexpr Result(Result&&) requires kTrivMove = default;
    constexpr Result(Result&& o) requires (!kTrivMove) : has_(o.has_) {
        if (has_) std::construct_at(std::addressof(val_), std::move(o.val_));
        else      std::construct_at(std::addressof(err_), std::move(o.err_));
    }
    constexpr Result& operator=(const Result&) requires (kTrivCopy && kTrivDtor) = default;
    constexpr Result& operator=(Result&&) requires (kTrivMove && kTrivDtor) = default;
    constexpr ~Result() requires kTrivDtor = default;
    constexpr ~Result() {
        if (has_) std::destroy_at(std::addressof(val_));
        else      std::destroy_at(std::addressof(err_));
    }

    constexpr bool has_value() const { return has_; }
    constexpr explicit operator bool() const { return has_; }

    constexpr T value() const& { if (!has_) return bad_access_value<T>(); return val_; }
    constexpr T value() &&     { if (!has_) return bad_access_value<T>(); return std::move(val_); }
    constexpr E error() const  { if (has_) return bad_access_value<E>(); return err_; }
    constexpr T value_or(T fallback) const { return has_ ? val_ : std::move(fallback); }

    // TODO(A1) map_error: 에러면 f(err) 로 E→G 변환, 값이면 그대로 옮겨 담는다.
    template <class F>
    constexpr auto map_error(F f) && {
        using G = std::invoke_result_t<F, E>;
        (void)f;
        return Result<T, G>(Unexpected<G>(G{}));   // TODO(A1)
    }

    // TODO(A2) map: 값이면 f(val) 결과를 담는다. f 가 void 를 돌려주면 Result<void, E>. 에러면 f 를 부르지 않는다.
    template <class F>
    constexpr auto map(F f) && {
        using U = std::invoke_result_t<F, T>;
        (void)f;
        if constexpr (std::is_void_v<U>) return Result<U, E>();   // TODO(A2)
        else return Result<U, E>(Unexpected<E>(E{}));             // TODO(A2)
    }

    // TODO(A3) and_then: 값이면 f(val) (Result 를 돌려주는 함수) 호출, 에러면 f 를 부르지 않고 에러를 옮겨 담는다.
    template <class F>
    constexpr auto and_then(F f) && {
        using R = std::invoke_result_t<F, T>;
        static_assert(std::is_same_v<typename R::error_type, E>, "and_then: 에러 타입이 같아야 한다 — 먼저 map_error");
        (void)f;
        return R(Unexpected<E>(E{}));   // TODO(A3)
    }

    // lvalue 용: 복사 뒤 위의 && 버전을 부른다
    template <class F> constexpr auto map_error(F f) const& { return Result(*this).map_error(std::move(f)); }
    template <class F> constexpr auto map(F f)       const& { return Result(*this).map(std::move(f)); }
    template <class F> constexpr auto and_then(F f)  const& { return Result(*this).and_then(std::move(f)); }

private:
    union { T val_; E err_; };
    bool has_;
};

// Result<void, E>: "성공/실패" 만 있는 쓰기·설정 함수의 반환형. C 의 int status 에 해당.
template <class E>
class [[nodiscard]] Result<void, E> {
public:
    using value_type = void;
    using error_type = E;

    constexpr Result() = default;                                              // return {};
    template <class G> requires std::convertible_to<G, E>
    constexpr Result(Unexpected<G> u) : err_(std::move(u.error)), has_(false) {}

    constexpr bool has_value() const { return has_; }
    constexpr explicit operator bool() const { return has_; }
    constexpr void value() const { if (!has_) bad_access_value<int>(); }       // TRY 가 void 로도 동작하게
    constexpr E error() const { if (has_) return bad_access_value<E>(); return err_; }

    // TODO(A4) map_error: 성공이면 Result<void,G> 성공, 실패면 f(err) 로 변환.
    template <class F>
    constexpr auto map_error(F f) const {
        using G = std::invoke_result_t<F, E>;
        (void)f;
        return Result<void, G>(Unexpected<G>(G{}));   // TODO(A4)
    }

    // TODO(A5) and_then: 성공이면 f() 호출 (f 는 인자 없음, Result 반환), 실패면 f 를 부르지 않는다.
    template <class F>
    constexpr auto and_then(F f) const {
        using R = std::invoke_result_t<F>;
        static_assert(std::is_same_v<typename R::error_type, E>, "and_then: 에러 타입이 같아야 한다 — 먼저 map_error");
        (void)f;
        return R(Unexpected<E>(E{}));   // TODO(A5)
    }

private:
    E err_{};
    bool has_ = true;
};

/* ---------------- Part B: 계층형 에러 + TRY ---------------- */
// 1층: 버스 (HAL). 4비트에 들어가야 한다.
enum class BusError : std::uint8_t { None = 0, AddrNack, DataNack, ArbLost, Timeout, BusStuck };
// 2층: 드라이버. 4비트에 들어가야 한다.
enum class DrvCode : std::uint8_t { None = 0, Bus, BadChipId, NotReady, InvalidData, BadArg };

// 드라이버 에러 = 코드 + 원인 + "어느 레지스터" 를 16비트에 패킹 → Result<int16_t, DrvError> 가 4바이트 (r0 반환)
//   [15:12] DrvCode   [11:8] BusError   [7:0] reg
class DrvError {
public:
    constexpr DrvError() = default;
    // TODO(B1) 세 필드를 raw_ 에 패킹한다.
    constexpr DrvError(DrvCode c, BusError cause, std::uint8_t reg) {
        (void)c; (void)cause; (void)reg;   // TODO(B1)
    }
    static constexpr DrvError bus(BusError b, std::uint8_t reg) { return DrvError(DrvCode::Bus, b, reg); }
    static constexpr DrvError at(DrvCode c, std::uint8_t reg) { return DrvError(c, BusError::None, reg); }

    // TODO(B2) 세 getter
    constexpr DrvCode code() const { return DrvCode::None; }
    constexpr BusError cause() const { return BusError::None; }
    constexpr std::uint8_t reg() const { return 0; }

    constexpr std::uint16_t raw() const { return raw_; }
    friend constexpr bool operator==(DrvError, DrvError) = default;

private:
    std::uint16_t raw_ = 0;
};

// 3층: 애플리케이션. "무슨 일이 일어났나" 보다 "무엇을 할까" 로 분류한다.
enum class AppCode : std::uint8_t { None = 0, SensorOffline, WrongPart, Busy, DataCorrupt, BadConfig, BusFatal };
struct AppError {
    AppCode code = AppCode::None;
    std::uint8_t sensor = 0;   // 어느 센서 인스턴스
    DrvError why{};            // 드라이버 층 컨텍스트 그대로 보존
    friend constexpr bool operator==(AppError, AppError) = default;
};

// 층 경계에서 쓰는 mapper: 컨텍스트(reg, sensor)를 캡처한다.
constexpr auto at_reg(std::uint8_t reg) {
    return [reg](BusError) { (void)reg; return DrvError{}; };     // TODO(B3)
}

// TODO(B4) 드라이버 에러 → 앱 분류. 표는 md §5 참고.
constexpr AppCode classify(DrvError e) {
    (void)e;
    return AppCode::None;   // TODO(B4)
}

constexpr auto to_app(std::uint8_t sensor) {
    return [sensor](DrvError) { (void)sensor; return AppError{}; };   // TODO(B5)
}

constexpr bool is_retryable(AppError e) { return e.code == AppCode::Busy; }

// TODO(B6) TRY: 에러면 그 에러를 들고 "둘러싼 함수" 에서 return, 값이면 식의 값이 된다. Result<void,E> 도 동작해야 한다.
#define TRY(...)                                                                          \
    ({                                                                                    \
        auto&& try_r_ = (__VA_ARGS__);                                                    \
        /* TODO(B6): 에러면 return. 지금은 에러를 무시하고 value() 를 부른다 */                 \
        std::forward<decltype(try_r_)>(try_r_).value();                                   \
    })

/* ---------------- Part C: 가짜 I2C 와 온도 센서 드라이버 ---------------- */
// TMP117 을 단순화한 모델: 16비트 레지스터, big-endian 전송, 1 LSB = 1/128 °C.
constexpr std::uint8_t REG_TEMP = 0x00, REG_CONFIG = 0x01, REG_DEVICE_ID = 0x0F;
constexpr std::uint16_t CFG_DRDY = 1u << 13, CFG_AVG_MASK = 3u << 5;
inline constexpr std::uint16_t ID_MASK = 0x0FFF, ID_VALUE = 0x0117;
constexpr std::uint16_t RAW_INVALID = 0x8000;

class MockI2c {
public:
    static constexpr std::uint8_t kAddr = 0x48;
    std::uint16_t regs[16] = {};
    int fail_at = -1;                    // 이 번호의 트랜잭션에서 실패 (-1: 실패 없음)
    BusError fail_with = BusError::None;
    bool sticky = false;                 // true: fail_at 부터 전부 실패 (버스 고착)
    int ready_after = 0;                 // CONFIG 를 이 횟수만큼 읽은 뒤부터 DRDY=1

    unsigned txn_count = 0, writes = 0, cfg_reads = 0;
    std::uint8_t log_reg[32] = {};

    void clear_log() { txn_count = writes = cfg_reads = 0; }

    Result<void, BusError> write_read(std::uint8_t addr, const std::uint8_t* w, std::size_t nw,
                                      std::uint8_t* r, std::size_t nr) {
        const unsigned idx = txn_count++;
        if (idx < 32) log_reg[idx] = nw > 0 ? w[0] : 0xFF;
        if (fail_at >= 0 && (sticky ? idx >= static_cast<unsigned>(fail_at) : idx == static_cast<unsigned>(fail_at)))
            return Unexpected{fail_with};
        if (addr != kAddr) return Unexpected{BusError::AddrNack};
        if (nw == 0 || w[0] >= 16) return Unexpected{BusError::DataNack};
        const std::uint8_t reg = w[0];
        if (nw == 3 && nr == 0) {
            regs[reg] = static_cast<std::uint16_t>(w[1] << 8 | w[2]);
            ++writes;
            return {};
        }
        if (nw == 1 && nr == 2) {
            std::uint16_t v = regs[reg];
            if (reg == REG_CONFIG) {
                const bool ready = static_cast<int>(cfg_reads++) >= ready_after;
                v = static_cast<std::uint16_t>(ready ? (v | CFG_DRDY) : (v & ~CFG_DRDY));
            }
            r[0] = static_cast<std::uint8_t>(v >> 8);
            r[1] = static_cast<std::uint8_t>(v & 0xFF);
            return {};
        }
        return Unexpected{BusError::DataNack};
    }
};

// TODO(C1) reg 포인터 1바이트 쓰고 2바이트 읽기 (big-endian). 버스 에러는 at_reg(reg) 로 올린다.
Result<std::uint16_t, DrvError> read_reg(MockI2c& bus, std::uint8_t reg) {
    (void)bus; (void)reg;
    return std::uint16_t{0};   // TODO(C1)
}

// TODO(C2) reg + 값 2바이트 쓰기.
Result<void, DrvError> write_reg(MockI2c& bus, std::uint8_t reg, std::uint16_t v) {
    (void)bus; (void)reg; (void)v;
    return {};   // TODO(C2)
}

// TODO(C3) DEVICE_ID 하위 12비트가 ID_VALUE 가 아니면 BadChipId@REG_DEVICE_ID.
Result<void, DrvError> tmp_probe(MockI2c& bus) {
    (void)bus;
    return {};   // TODO(C3)
}

// TODO(C4) avg(0..3)를 CONFIG[6:5] 에 RMW. 검증 먼저 — 잘못된 avg 면 버스 접근 0회, BadArg@REG_CONFIG.
Result<void, DrvError> tmp_set_avg(MockI2c& bus, std::uint8_t avg) {
    (void)bus; (void)avg;
    return {};   // TODO(C4)
}

// TODO(C5) CONFIG 의 DRDY 확인(아니면 NotReady@REG_CONFIG) → TEMP 읽기 → 0x8000 이면 InvalidData@REG_TEMP
//          → raw * 100 / 128 을 centi-°C 로. 음수도 0 방향 절삭.
Result<std::int16_t, DrvError> tmp_read_centi(MockI2c& bus) {
    (void)bus;
    return std::int16_t{0};   // TODO(C5)
}

// TODO(C6) probe → read. 드라이버 에러는 to_app(sensor) 로 올린다.
Result<std::int16_t, AppError> app_read_temp(MockI2c& bus, std::uint8_t sensor) {
    (void)bus; (void)sensor;
    return std::int16_t{0};   // TODO(C6)
}

// TODO(C7) probe → set_avg (Result<void> 만의 체인).
Result<void, AppError> app_bringup(MockI2c& bus, std::uint8_t sensor, std::uint8_t avg) {
    (void)bus; (void)sensor; (void)avg;
    return {};   // TODO(C7)
}

// TODO(C8) 재시도: tries 번까지 app_read_temp. is_retryable 이 아닌 에러는 즉시 반환. tries==0 이면 BadConfig.
//          *attempts 에 실제 시도 횟수.
Result<std::int16_t, AppError> app_read_retry(MockI2c& bus, std::uint8_t sensor, unsigned tries, unsigned* attempts) {
    (void)bus; (void)sensor; (void)tries;
    *attempts = 0;
    return std::int16_t{0};   // TODO(C8)
}

/* ---------------- 컴파일 타임 검증 (타입 규칙) ---------------- */
static_assert(sizeof(DrvError) == 2);
static_assert(sizeof(AppError) == 4);
static_assert(sizeof(Result<std::int16_t, DrvError>) == 4);   // AAPCS32: r0 하나로 반환되는 크기
static_assert(sizeof(Result<void, DrvError>) == 4);
static_assert(std::is_trivially_copyable_v<Result<std::int16_t, DrvError>>);
static_assert(std::is_trivially_destructible_v<Result<std::int16_t, DrvError>>);
static_assert(std::is_trivially_copyable_v<Result<void, DrvError>>);
// 층을 건너뛰는 암묵 변환은 없다 (컨텍스트 없이 올리는 코드는 컴파일 에러)
static_assert(!std::is_convertible_v<BusError, DrvError>);
static_assert(!std::is_convertible_v<DrvError, AppError>);
static_assert(!std::is_constructible_v<Result<int, DrvError>, Unexpected<BusError>>);
static_assert(!std::is_constructible_v<Result<int, AppError>, Unexpected<DrvError>>);
static_assert(!std::is_convertible_v<Result<int, BusError>, Result<int, DrvError>>);
// Result<T,T> 도 Unexpected 덕에 값/에러가 모호하지 않다
static_assert(std::is_constructible_v<Result<int, int>, Unexpected<int>>);
template <class R> concept ReturnsBraceEmpty = requires { R{}; };
static_assert(ReturnsBraceEmpty<Result<void, DrvError>>);
static_assert(!ReturnsBraceEmpty<Result<int, DrvError>>);     // Result<int,E> 는 기본 생성 불가 (값이 없는 성공은 없다)

/* ---------------- 테스트 ---------------- */
struct Counted {
    static inline int live = 0;
    int v;
    explicit Counted(int x) : v(x) { ++live; }
    Counted(const Counted& o) : v(o.v) { ++live; }
    Counted(Counted&& o) noexcept : v(o.v) { o.v = -1; ++live; }
    ~Counted() { --live; }
};
struct MoveOnly {
    int v;
    explicit MoveOnly(int x) : v(x) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& o) noexcept : v(o.v) { o.v = -1; }
};
static_assert(!std::is_copy_constructible_v<Result<MoveOnly, BusError>>);
static_assert(std::is_move_constructible_v<Result<MoveOnly, BusError>>);

using RI = Result<int, BusError>;
using RV = Result<void, BusError>;

static int g_calls = 0;
static RI make_ok(int v) { return v; }
static RI make_err(BusError e) { return Unexpected{e}; }
static RV void_ok() { return {}; }
static RV void_err(BusError e) { return Unexpected{e}; }

// TRY 테스트용 함수들
static RI try_add(RI a, RI b) {
    const int x = TRY(a);
    ++g_calls;                 // a 가 실패면 여기에 오면 안 된다
    const int y = TRY(b);
    ++g_calls;
    return x + y;
}
static RI try_after_void(RV s, int v) {
    TRY(s);
    ++g_calls;
    return v * 2;
}
static RI try_nested(RI a) {
    return make_ok(TRY(make_ok(TRY(a) + 1)) + 1);   // TRY 안의 TRY
}
static Result<MoveOnly, BusError> make_mo(int v) { return MoveOnly(v); }
static Result<int, BusError> try_move_only(int v) {
    MoveOnly m = TRY(make_mo(v));
    return m.v + 1;
}
static Result<int, DrvError> try_mapped(RI a, std::uint8_t reg) {
    const int x = TRY(RI(a).map_error(at_reg(reg)));
    return x + 1;
}

static void test_part_a() {
    std::printf("--- Part A: Result<T,E>, Result<void,E> ---\n");
    constexpr RI cok = 7;
    constexpr RI cerr = Unexpected{BusError::Timeout};
    static_assert(cok.has_value() && cok.value() == 7);
    static_assert(!cerr.has_value() && cerr.error() == BusError::Timeout);
    CHECK(make_ok(5).has_value());
    CHECK(make_ok(5).value() == 5);
    CHECK(!make_err(BusError::ArbLost));
    CHECK(make_err(BusError::ArbLost).error() == BusError::ArbLost);
    CHECK(make_err(BusError::ArbLost).value_or(-9) == -9);
    CHECK(make_ok(5).value_or(-9) == 5);
    // T==E 여도 값/에러 구별
    Result<int, int> same_v = 3, same_e = Unexpected{3};
    CHECK(same_v.has_value() && !same_e.has_value());
    // 잘못된 접근은 훅 + 기본값 (throw 불가)
    g_bad_access = 0;
    (void)make_err(BusError::Timeout).value();
    CHECK(g_bad_access == 1);
    (void)make_ok(1).error();
    CHECK(g_bad_access == 2);

    // A1 map_error
    auto m1 = make_err(BusError::DataNack).map_error([](BusError b) { return static_cast<int>(b) + 100; });
    CHECK(!m1 && m1.error() == 102);
    auto m2 = make_ok(9).map_error([](BusError b) { return static_cast<int>(b) + 100; });
    CHECK(m2 && m2.value() == 9);
    // lvalue 호출도 동작 (복사 후 && 버전)
    const RI lv = 4;
    auto m3 = lv.map_error([](BusError) { return 0; });
    CHECK(m3 && m3.value() == 4);

    // A2 map
    g_calls = 0;
    auto mp = make_ok(4).map([](int v) { return v * 10; });
    CHECK(mp && mp.value() == 40);
    auto mp_err = make_err(BusError::Timeout).map([](int v) { ++g_calls; return v * 10; });
    CHECK(!mp_err && mp_err.error() == BusError::Timeout);
    CHECK(g_calls == 0);
    int sink = 0;
    auto mv = make_ok(6).map([&](int v) { sink = v; });         // void 반환 → Result<void, E>
    static_assert(std::is_same_v<decltype(mv), RV>);
    CHECK(mv.has_value() && sink == 6);

    // A3 and_then 은 실패에서 멈춘다
    auto halve = [](int v) -> RI {
        if (v % 2) return Unexpected{BusError::DataNack};
        return v / 2;
    };
    CHECK(make_ok(8).and_then(halve).and_then(halve).value() == 2);
    CHECK(make_ok(6).and_then(halve).and_then(halve).error() == BusError::DataNack);
    g_calls = 0;
    auto at_err = make_err(BusError::Timeout).and_then([&](int) -> RI { ++g_calls; return 1; });
    CHECK(!at_err && g_calls == 0);

    // 수명: 비자명 타입의 생성/소멸이 맞아야 한다
    Counted::live = 0;
    {
        Result<Counted, BusError> a = Counted(1);
        Result<Counted, BusError> b = a;                       // 복사
        Result<Counted, BusError> c = std::move(a);            // 이동
        Result<Counted, BusError> e = Unexpected{BusError::Timeout};
        CHECK(Counted::live == 3);
        CHECK(b.value().v == 1 && c.value().v == 1);
        CHECK(!e && e.error() == BusError::Timeout);
    }
    CHECK(Counted::live == 0);
    Result<MoveOnly, BusError> mo = MoveOnly(42);
    Result<MoveOnly, BusError> mo2 = std::move(mo);
    CHECK(std::move(mo2).value().v == 42);

    // Result<void,E> — A4, A5
    CHECK(void_ok().has_value());
    CHECK(!void_err(BusError::BusStuck).has_value());
    CHECK(void_err(BusError::BusStuck).error() == BusError::BusStuck);
    auto vm = void_err(BusError::ArbLost).map_error([](BusError b) { return static_cast<int>(b); });
    static_assert(std::is_same_v<decltype(vm), Result<void, int>>);
    CHECK(!vm && vm.error() == 3);
    auto vm_ok = void_ok().map_error([](BusError b) { return static_cast<int>(b); });
    CHECK(vm_ok.has_value());
    g_calls = 0;
    auto va = void_ok().and_then([]() -> RI { ++g_calls; return 11; });
    CHECK(va && va.value() == 11 && g_calls == 1);
    g_calls = 0;
    auto vb = void_err(BusError::Timeout).and_then([]() -> RI { ++g_calls; return 11; });
    CHECK(!vb && vb.error() == BusError::Timeout && g_calls == 0);
}

static void test_part_b() {
    std::printf("--- Part B: 계층형 에러, TRY ---\n");
    // B1, B2 패킹: 경계값 표
    struct Row { DrvCode c; BusError b; std::uint8_t r; std::uint16_t raw; };
    constexpr Row rows[] = {
        {DrvCode::None, BusError::None, 0x00, 0x0000},
        {DrvCode::Bus, BusError::AddrNack, 0x00, 0x1100},
        {DrvCode::Bus, BusError::BusStuck, 0xFF, 0x15FF},
        {DrvCode::BadChipId, BusError::None, 0x0F, 0x200F},
        {DrvCode::NotReady, BusError::None, 0x01, 0x3001},
        {DrvCode::InvalidData, BusError::None, 0x00, 0x4000},
        {DrvCode::BadArg, BusError::Timeout, 0xA5, 0x54A5},
    };
    int ok = 0;
    for (const Row& r : rows) {
        const DrvError e(r.c, r.b, r.r);
        ok += e.code() == r.c && e.cause() == r.b && e.reg() == r.r && e.raw() == r.raw;
    }
    CHECK(ok == 7);
    constexpr DrvError ce = DrvError::bus(BusError::DataNack, 0x0F);
    CHECK(ce.code() == DrvCode::Bus && ce.cause() == BusError::DataNack && ce.reg() == 0x0F);
    constexpr DrvError ca = DrvError::at(DrvCode::NotReady, 0x01);
    CHECK(ca.code() == DrvCode::NotReady && ca.cause() == BusError::None && ca.reg() == 0x01);
    CHECK(ce == DrvError::bus(BusError::DataNack, 0x0F) && !(ce == ca));

    // B3 at_reg
    constexpr auto m = at_reg(0x2C);
    CHECK(m(BusError::Timeout) == DrvError::bus(BusError::Timeout, 0x2C));

    // B4 분류표
    struct Cls { DrvError in; AppCode out; };
    constexpr Cls cls[] = {
        {DrvError::bus(BusError::AddrNack, 1), AppCode::SensorOffline},
        {DrvError::bus(BusError::Timeout, 1), AppCode::SensorOffline},
        {DrvError::bus(BusError::DataNack, 1), AppCode::Busy},
        {DrvError::bus(BusError::ArbLost, 1), AppCode::Busy},
        {DrvError::bus(BusError::BusStuck, 1), AppCode::BusFatal},
        {DrvError::at(DrvCode::BadChipId, 1), AppCode::WrongPart},
        {DrvError::at(DrvCode::NotReady, 1), AppCode::Busy},
        {DrvError::at(DrvCode::InvalidData, 1), AppCode::DataCorrupt},
        {DrvError::at(DrvCode::BadArg, 1), AppCode::BadConfig},
    };
    ok = 0;
    for (const Cls& c : cls) ok += classify(c.in) == c.out;
    CHECK(ok == 9);

    // B5 to_app: 앱 층에서도 드라이버 컨텍스트가 그대로 보인다
    const AppError ae = to_app(3)(DrvError::bus(BusError::BusStuck, 0x0F));
    CHECK(ae.code == AppCode::BusFatal && ae.sensor == 3);
    CHECK(ae.why.cause() == BusError::BusStuck && ae.why.reg() == 0x0F);

    // B6 TRY
    g_calls = 0;
    auto a = try_add(2, 3);
    CHECK(a.has_value() && a.value() == 5 && g_calls == 2);
    g_calls = 0;
    auto b = try_add(Unexpected{BusError::Timeout}, 3);
    CHECK(!b.has_value() && b.error() == BusError::Timeout && g_calls == 0);
    g_calls = 0;
    auto c = try_add(2, Unexpected{BusError::DataNack});
    CHECK(!c.has_value() && c.error() == BusError::DataNack && g_calls == 1);
    g_calls = 0;
    CHECK(try_after_void(void_ok(), 4).value_or(0) == 8 && g_calls == 1);
    g_calls = 0;
    auto d = try_after_void(void_err(BusError::ArbLost), 4);
    CHECK(!d.has_value() && d.error() == BusError::ArbLost && g_calls == 0);
    CHECK(try_nested(1).value_or(0) == 3);
    CHECK(!try_nested(Unexpected{BusError::BusStuck}).has_value());
    CHECK(try_move_only(41).value_or(0) == 42);
    auto e = try_mapped(Unexpected{BusError::Timeout}, 0x77);
    CHECK(!e.has_value() && e.error() == DrvError::bus(BusError::Timeout, 0x77));
    CHECK(try_mapped(10, 0x77).value_or(0) == 11);
}

static std::uint32_t g_xs = 0xC0FFEE11u;
static std::uint32_t xorshift32() {
    g_xs ^= g_xs << 13; g_xs ^= g_xs >> 17; g_xs ^= g_xs << 5;
    return g_xs;
}

static void arm_good_sensor(MockI2c& bus, std::uint16_t raw) {
    bus.regs[REG_DEVICE_ID] = ID_VALUE | 0x1000;   // 상위 4비트 = 리비전 (무시되어야 함)
    bus.regs[REG_CONFIG] = 0x0220;
    bus.regs[REG_TEMP] = raw;
    bus.fail_at = -1; bus.fail_with = BusError::None; bus.sticky = false; bus.ready_after = 0;
    bus.clear_log();
}

static void test_part_c() {
    std::printf("--- Part C: I2C 드라이버 전파 체인 ---\n");
    MockI2c bus;

    // C1, C2
    arm_good_sensor(bus, 0);
    bus.regs[0x07] = 0xBEEF;
    CHECK(read_reg(bus, 0x07).value_or(0) == 0xBEEF);
    CHECK(bus.txn_count == 1 && bus.log_reg[0] == 0x07);
    auto bad_reg = read_reg(bus, 0x20);                    // 없는 레지스터 → 데이터 NACK, 그 reg 가 기록돼야
    CHECK(!bad_reg.has_value() && bad_reg.error() == DrvError::bus(BusError::DataNack, 0x20));
    bus.clear_log();
    CHECK(write_reg(bus, 0x03, 0x1234).has_value());
    CHECK(bus.regs[0x03] == 0x1234 && bus.writes == 1);

    // C3 probe
    arm_good_sensor(bus, 0);
    CHECK(tmp_probe(bus).has_value());
    bus.regs[REG_DEVICE_ID] = 0x0119;
    auto wrong = tmp_probe(bus);
    CHECK(!wrong.has_value() && wrong.error() == DrvError::at(DrvCode::BadChipId, REG_DEVICE_ID));

    // C4 set_avg: 검증이 버스 접근보다 먼저
    arm_good_sensor(bus, 0);
    auto bad_avg = tmp_set_avg(bus, 4);
    CHECK(!bad_avg.has_value() && bad_avg.error() == DrvError::at(DrvCode::BadArg, REG_CONFIG));
    CHECK(bus.txn_count == 0);
    CHECK(tmp_set_avg(bus, 2).has_value());
    CHECK(bus.regs[REG_CONFIG] == ((0x0220 & ~CFG_AVG_MASK) | (2u << 5)));
    CHECK(bus.writes == 1 && bus.txn_count == 2);
    // RMW 도중 쓰기에서 실패: 읽기는 됐지만 값은 그대로
    arm_good_sensor(bus, 0);
    bus.fail_at = 1; bus.fail_with = BusError::Timeout;
    auto rmw_fail = tmp_set_avg(bus, 1);
    CHECK(!rmw_fail.has_value() && rmw_fail.error() == DrvError::bus(BusError::Timeout, REG_CONFIG));
    CHECK(bus.regs[REG_CONFIG] == 0x0220 && bus.writes == 0);

    // C5 온도 변환 표 (raw → centi-°C)
    struct T { std::uint16_t raw; std::int16_t centi; };
    constexpr T temps[] = {
        {3200, 2500}, {0, 0}, {1, 0}, {0xFFFF, 0} /* -1/128 → 0 방향 절삭 */, {0xFFC0, -50},
        {0x7FFF, 25599}, {0x8001, -25599}, {128, 100},
    };
    int ok = 0;
    for (const T& t : temps) {
        arm_good_sensor(bus, t.raw);
        ok += tmp_read_centi(bus).value_or(-32768) == t.centi;
    }
    CHECK(ok == 8);
    arm_good_sensor(bus, RAW_INVALID);
    auto inv = tmp_read_centi(bus);
    CHECK(!inv.has_value() && inv.error() == DrvError::at(DrvCode::InvalidData, REG_TEMP));
    arm_good_sensor(bus, 100);
    bus.ready_after = 1;
    auto nr = tmp_read_centi(bus);
    CHECK(!nr.has_value() && nr.error() == DrvError::at(DrvCode::NotReady, REG_CONFIG));
    CHECK(bus.txn_count == 1);                             // NotReady 면 TEMP 를 읽지 않는다

    // C6 전파 체인: 트랜잭션 × 버스 에러 전 조합 (reg 와 분류가 모두 맞아야, 실패 후 버스 접근 0회)
    constexpr std::uint8_t seq_regs[3] = {REG_DEVICE_ID, REG_CONFIG, REG_TEMP};
    struct BE { BusError b; AppCode app; };
    constexpr BE bes[] = {
        {BusError::AddrNack, AppCode::SensorOffline}, {BusError::DataNack, AppCode::Busy},
        {BusError::ArbLost, AppCode::Busy}, {BusError::Timeout, AppCode::SensorOffline},
        {BusError::BusStuck, AppCode::BusFatal},
    };
    ok = 0;
    for (int at = 0; at < 3; ++at) {
        for (const BE& be : bes) {
            arm_good_sensor(bus, 3200);
            bus.fail_at = at; bus.fail_with = be.b;
            auto r = app_read_temp(bus, 7);
            ok += !r.has_value() && r.error() == AppError{be.app, 7, DrvError::bus(be.b, seq_regs[at])} &&
                  bus.txn_count == static_cast<unsigned>(at) + 1;
        }
    }
    CHECK(ok == 15);
    arm_good_sensor(bus, 3200);
    CHECK(app_read_temp(bus, 7).value_or(0) == 2500);
    CHECK(bus.txn_count == 3);

    // C7 Result<void> 체인
    arm_good_sensor(bus, 0);
    CHECK(app_bringup(bus, 2, 3).has_value());
    CHECK((bus.regs[REG_CONFIG] & CFG_AVG_MASK) == (3u << 5));
    arm_good_sensor(bus, 0);
    auto bad_cfg = app_bringup(bus, 2, 9);
    CHECK(!bad_cfg.has_value() && bad_cfg.error().code == AppCode::BadConfig && bad_cfg.error().sensor == 2);
    CHECK(bus.writes == 0);

    // C8 재시도
    unsigned attempts = 0;
    arm_good_sensor(bus, 3200);
    bus.ready_after = 2;
    CHECK(app_read_retry(bus, 1, 5, &attempts).value_or(0) == 2500);
    CHECK(attempts == 3);
    arm_good_sensor(bus, 3200);
    bus.ready_after = 100;
    auto exhausted = app_read_retry(bus, 1, 4, &attempts);
    CHECK(!exhausted.has_value() && exhausted.error().code == AppCode::Busy && attempts == 4);
    arm_good_sensor(bus, 3200);
    bus.regs[REG_DEVICE_ID] = 0;
    auto fatal = app_read_retry(bus, 1, 4, &attempts);
    CHECK(!fatal.has_value() && fatal.error().code == AppCode::WrongPart && attempts == 1);
    arm_good_sensor(bus, 3200);
    auto zero = app_read_retry(bus, 1, 0, &attempts);
    CHECK(!zero.has_value() && zero.error().code == AppCode::BadConfig && attempts == 0 && bus.txn_count == 0);

    // 고정 시드 퍼즈: 실패 위치/종류를 무작위로 주입해도 불변식이 유지되는가
    int bad = 0;
    g_xs = 0xC0FFEE11u;
    for (int i = 0; i < 2000; ++i) {
        const std::uint32_t x = xorshift32();
        const int at = static_cast<int>(x % 4) - 1;              // -1 (정상) .. 2
        const BE& be = bes[(x >> 4) % 5];
        const auto raw = static_cast<std::uint16_t>(x >> 16);
        arm_good_sensor(bus, raw);
        bus.fail_at = at; bus.fail_with = be.b;
        auto r = app_read_temp(bus, 5);
        if (at >= 0) {
            bad += !(!r.has_value() && r.error() == AppError{be.app, 5, DrvError::bus(be.b, seq_regs[at])} &&
                     bus.txn_count == static_cast<unsigned>(at) + 1);
        } else if (raw == RAW_INVALID) {
            bad += r.has_value() || r.error().code != AppCode::DataCorrupt;
        } else {
            const auto want = static_cast<std::int32_t>(static_cast<std::int16_t>(raw)) * 100 / 128;
            bad += !(r.has_value() && r.value() == want && bus.txn_count == 3);
        }
    }
    CHECK(bad == 0);
}

int main() {
    test_part_a();
    test_part_b();
    test_part_c();
    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
