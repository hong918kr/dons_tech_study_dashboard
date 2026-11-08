// CPP-B13 -fno-exceptions / -fno-rtti 빌드의 실제 영향
//   예외 대신 Result<T,E>(에러 코드 + 값), dynamic_cast 대신 Kind 태그 + classof + static_cast.
//   build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp
//   (크기/섹션 비교 실험은 build.sh + demo_flags.cpp, 에러 메시지는 demo_errors.cpp)
#include <cstdint>
#include <cstdio>
#include <type_traits>

enum class Err : std::uint8_t { None = 0, Timeout, BadId, Range, NotReady };

/* ---------------- Part A: 예외 없는 에러 전달 — Result<T, E> ---------------- */
static int g_contract_violations = 0;          // 펌웨어라면 여기서 BKPT / 리셋 / 에러 로그
static void on_contract_violation() { ++g_contract_violations; }

template <typename T, typename E = Err>
class [[nodiscard]] Result {
public:
    static constexpr Result ok(T v)   { Result r; r.val_ = v; r.has_ = true;  return r; }
    static constexpr Result fail(E e) { Result r; r.err_ = e; r.has_ = false; return r; }

    constexpr bool has_value() const { return has_; }
    constexpr explicit operator bool() const { return has_; }
    constexpr E error() const { return has_ ? E{} : err_; }

    // 에러 상태에서 value() = 계약 위반. throw 할 수 없으므로 훅을 부르고 T{} 를 돌려준다.
    T value() const {
        if (!has_) { on_contract_violation(); return T{}; }
        return val_;
    }
    constexpr T value_or(T fallback) const {
        return fallback; /* TODO A1: 값이 있으면 값, 없으면 fallback */
    }

private:
    T val_{};
    E err_{};
    bool has_ = false;
};
// 레지스터 하나에 담기는 크기 → AAPCS 에서 r0 하나로 반환 (4바이트 이하 복합 타입)
static_assert(sizeof(Result<std::uint16_t>) == 4);
static_assert(std::is_trivially_copyable_v<Result<std::uint16_t>>);

// ODR(output data rate) 코드 0..7 → Hz. 범위 밖이면 Err::Range
static constexpr std::uint16_t kOdrHz[8] = {25, 50, 100, 200, 400, 800, 1600, 3200};
constexpr Result<std::uint16_t> odr_from_code(std::uint8_t code) {
    (void)code; (void)kOdrHz;
    return Result<std::uint16_t>::ok(0); /* TODO A2: 0..7 → kOdrHz[code], 그 외 Err::Range */
}

/* ---------------- Part B: dynamic_cast 대체 — Kind 태그 + classof ---------------- */
enum class Kind : std::uint8_t { Bmi270, Lsm6dso, Bmp390 };   // [Bmi270, Lsm6dso] = IMU 계열

struct Sensor {
    explicit Sensor(Kind k) : kind_(k) {}
    virtual ~Sensor() = default;
    virtual int channels() const = 0;
    Kind kind() const { return kind_; }
    static constexpr bool classof(const Sensor*) { return true; }
private:
    const Kind kind_;
};
struct Imu : Sensor {                                           // 중간 클래스: kind 가 "범위"
    using Sensor::Sensor;
    int channels() const override { return 6; }
    static bool classof(const Sensor* s) {
        (void)s; return false; /* TODO B1: kind 가 IMU 범위 [Bmi270, Lsm6dso] 인가 */
    }
};
struct Bmi270 final : Imu {
    Bmi270() : Imu(Kind::Bmi270) {}
    static bool classof(const Sensor* s) { return s->kind() == Kind::Bmi270; }
};
struct Lsm6dso final : Imu {
    Lsm6dso() : Imu(Kind::Lsm6dso) {}
    static bool classof(const Sensor* s) { return s->kind() == Kind::Lsm6dso; }
};
struct Bmp390 final : Sensor {
    Bmp390() : Sensor(Kind::Bmp390) {}
    int channels() const override { return 2; }
    static bool classof(const Sensor* s) { return s->kind() == Kind::Bmp390; }
};

// LLVM 스타일 isa<> / dyn_cast<> — RTTI 없이 "안전한 다운캐스트"
template <class To, class From>
    requires std::is_base_of_v<From, std::remove_cv_t<To>>      // 상속 관계가 아니면 후보에서 제외
bool isa(const From* p) {
    (void)p; return false; /* TODO B2: nullptr 이면 false, 아니면 To::classof(p) */
}
template <class To, class From>
    requires std::is_base_of_v<From, std::remove_cv_t<To>>
To* dyn_cast(From* p) {
    (void)p; return nullptr; /* TODO B3: isa<To> 이면 static_cast, 아니면 nullptr */
}
template <class To, class From>
concept DynCastable = requires(From* p) { dyn_cast<To>(p); };

/* ---------------- Part C: mock I2C IMU 드라이버 — 에러 전파 ---------------- */
struct MockI2c {                       // 가짜 버스: 디바이스 레지스터 맵 + NACK 주입
    std::uint8_t reg[128];
    bool nack;
    int writes;
};
enum : std::uint8_t { REG_WHO_AM_I = 0x00, REG_STATUS = 0x03, REG_ACC_X_LSB = 0x0C,
                      REG_ACC_X_MSB = 0x0D, REG_ACC_CONF = 0x40 };
constexpr std::uint8_t kChipId = 0x24, kDrdyAcc = 0x80;

static Result<std::uint8_t> i2c_read(MockI2c& b, std::uint8_t r) {
    if (b.nack) return Result<std::uint8_t>::fail(Err::Timeout);
    return Result<std::uint8_t>::ok(b.reg[r & 0x7F]);
}
static Err i2c_write(MockI2c& b, std::uint8_t r, std::uint8_t v) {
    if (b.nack) return Err::Timeout;
    b.reg[r & 0x7F] = v; ++b.writes;
    return Err::None;
}

// WHO_AM_I 를 읽어 칩 ID 확인. 버스 에러는 그대로 전파, ID 불일치는 BadId
static Result<std::uint8_t> imu_probe(MockI2c& b) {
    (void)b; (void)i2c_read;
    /* TODO C1: WHO_AM_I 읽기 → 실패면 그 에러 전파, kChipId 아니면 BadId */
    return Result<std::uint8_t>::ok(kChipId);
}
// ODR 코드 검증 → 범위 밖이면 버스를 건드리기 전에 Range. 아니면 ACC_CONF 하위 4비트에 기록
static Err imu_configure(MockI2c& b, std::uint8_t odr_code) {
    /* TODO C2: odr_from_code 로 검증(실패 시 버스 접근 금지) → ACC_CONF RMW (하위 4비트만) */
    return i2c_write(b, REG_ACC_CONF, odr_code);
}
// STATUS.drdy 확인 후 X축 16비트(리틀 엔디언) 읽기
static Result<std::int16_t> imu_read_x(MockI2c& b) {
    (void)b; (void)kDrdyAcc;
    /* TODO C3: STATUS.drdy(0x80) 아니면 NotReady, 맞으면 LSB/MSB 읽어 int16 조립 */
    return Result<std::int16_t>::ok(0);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static Bmi270 s_bmi;  static Lsm6dso s_lsm;  static Bmp390 s_baro;   // 힙 없음: 정적 객체
static Sensor* const s_sensors[] = {&s_bmi, &s_baro, &s_lsm};

int main() {
    std::printf("build: exceptions=%s rtti=%s\n",
#if defined(__cpp_exceptions)
                "on",
#else
                "off",
#endif
#if defined(__cpp_rtti)
                "on");
#else
                "off");
#endif

    /* Part A */
    auto ok  = Result<std::uint16_t>::ok(400);
    auto bad = Result<std::uint16_t>::fail(Err::Timeout);
    CHECK(ok.has_value() && ok.value() == 400 && ok.error() == Err::None);
    CHECK(!bad && bad.error() == Err::Timeout);
    CHECK(ok.value_or(7) == 400);
    CHECK(bad.value_or(7) == 7);
    CHECK(odr_from_code(4).value_or(0) == 400);
    CHECK(odr_from_code(7).value_or(0) == 3200);
    CHECK(odr_from_code(8).error() == Err::Range);
    constexpr auto c0 = odr_from_code(0);                       // constexpr 로도 동작
    CHECK(c0.value_or(0) == 25);
    int before = g_contract_violations;
    (void)bad.value();                                          // 에러인데 value() → 훅 1회
    CHECK(g_contract_violations == before + 1);

    /* Part B */
    CHECK(isa<Imu>(s_sensors[0]) && isa<Imu>(s_sensors[2]) && !isa<Imu>(s_sensors[1]));
    CHECK(dyn_cast<Bmi270>(s_sensors[0]) == &s_bmi);
    CHECK(dyn_cast<Bmi270>(s_sensors[2]) == nullptr);           // Lsm6dso 는 Bmi270 아님
    CHECK(dyn_cast<Bmp390>(s_sensors[1]) == &s_baro);
    CHECK(dyn_cast<Imu>(static_cast<Sensor*>(nullptr)) == nullptr);
    const Sensor* cs = &s_lsm;
    CHECK(dyn_cast<const Imu>(cs) != nullptr && dyn_cast<const Imu>(cs)->channels() == 6);
    int imu_count = 0;
    for (Sensor* s : s_sensors) if (dyn_cast<Imu>(s)) ++imu_count;
    CHECK(imu_count == 2);
    static_assert(DynCastable<Imu, Sensor>);
    static_assert(!DynCastable<int, Sensor>);                   // 상속 관계 없는 타입 → 컴파일 불가

    /* Part C */
    MockI2c bus{};
    bus.reg[REG_WHO_AM_I] = kChipId;
    bus.reg[REG_ACC_CONF] = 0xA8;                               // 상위 니블 = 다른 설정, 보존돼야 함
    CHECK(imu_probe(bus).value_or(0) == kChipId);
    bus.reg[REG_WHO_AM_I] = 0x11;
    CHECK(imu_probe(bus).error() == Err::BadId);
    bus.nack = true;
    CHECK(imu_probe(bus).error() == Err::Timeout);              // 버스 에러는 그대로 전파
    bus.nack = false;
    CHECK(imu_configure(bus, 9) == Err::Range && bus.writes == 0);   // 검증 실패 → 버스 쓰기 0회
    CHECK(imu_configure(bus, 4) == Err::None && bus.reg[REG_ACC_CONF] == 0xA4 && bus.writes == 1);
    CHECK(imu_read_x(bus).error() == Err::NotReady);
    bus.reg[REG_STATUS] = kDrdyAcc;
    bus.reg[REG_ACC_X_LSB] = 0x18; bus.reg[REG_ACC_X_MSB] = 0xFC;   // 0xFC18 = -1000
    CHECK(imu_read_x(bus).value_or(0) == -1000);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
