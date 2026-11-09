// CPP-M11 demo: 같은 3층 드라이버 체인을 네 가지 에러 전달 방식으로 — thumbv7em 코드 크기 비교
//   VARIANT=1 C 스타일: uint16_t 에러 코드 반환 + int16_t* out 파라미터
//   VARIANT=2 Result<int16_t, DrvErr16>   (4바이트 → r0 반환)
//   VARIANT=3 std::expected<int16_t, DrvErr16>  (C++23; 필요시 -DUSE_VALUE 로 .value())
//   VARIANT=4 Result<int16_t, DrvErrWide> (8바이트 → 숨은 포인터로 메모리 반환)
//   VARIANT=5 std::expected<int16_t, DrvErrWide>
// checks.sh size 가 ARM 타깃으로 빌드해 표를 만든다. (헤더 없이 쓰는 코드가 아니라 recipe (b)/(c) 필요)
#include <cstdint>

extern "C" int i2c_xfer(std::uint8_t addr, const std::uint8_t* w, unsigned nw, std::uint8_t* r, unsigned nr);

#define NOINLINE [[gnu::noinline]]

struct DrvErr16 {                        // 2바이트: [15:12]code [11:8]bus [7:0]reg
    std::uint16_t raw;
    static constexpr DrvErr16 bus(int b, std::uint8_t reg) { return {static_cast<std::uint16_t>(0x1000 | (b & 0xF) << 8 | reg)}; }
    static constexpr DrvErr16 at(int code, std::uint8_t reg) { return {static_cast<std::uint16_t>((code & 0xF) << 12 | reg)}; }
};
struct DrvErrWide {                      // 4바이트: 원인을 넉넉히
    std::uint8_t code, bus, reg, pad;
    static constexpr DrvErrWide make(int c, int b, std::uint8_t reg) {
        return {static_cast<std::uint8_t>(c), static_cast<std::uint8_t>(b), reg, 0};
    }
    static constexpr DrvErrWide bus_err(int b, std::uint8_t reg) { return make(1, b, reg); }
    static constexpr DrvErrWide at(int code, std::uint8_t reg) { return make(code, 0, reg); }
};

constexpr std::uint8_t REG_TEMP = 0, REG_CONFIG = 1, REG_ID = 0x0F, ADDR = 0x48;
constexpr std::uint16_t DRDY = 1u << 13;

#if VARIANT == 1
/* ---------------- C 스타일 ---------------- */
using Err = std::uint16_t;                // 0 = OK, 나머지는 패킹된 에러
NOINLINE Err read_reg(std::uint8_t reg, std::uint16_t* out) {
    const std::uint8_t tx[1] = {reg};
    std::uint8_t rx[2];
    const int b = i2c_xfer(ADDR, tx, 1, rx, 2);
    if (b != 0) return DrvErr16::bus(b, reg).raw;
    *out = static_cast<std::uint16_t>(rx[0] << 8 | rx[1]);
    return 0;
}
NOINLINE Err probe() {
    std::uint16_t id;
    const Err e = read_reg(REG_ID, &id);
    if (e != 0) return e;
    if ((id & 0x0FFF) != 0x0117) return DrvErr16::at(2, REG_ID).raw;
    return 0;
}
NOINLINE Err read_centi(std::int16_t* out) {
    std::uint16_t cfg, raw;
    Err e = read_reg(REG_CONFIG, &cfg);
    if (e != 0) return e;
    if ((cfg & DRDY) == 0) return DrvErr16::at(3, REG_CONFIG).raw;
    e = read_reg(REG_TEMP, &raw);
    if (e != 0) return e;
    if (raw == 0x8000) return DrvErr16::at(4, REG_TEMP).raw;
    *out = static_cast<std::int16_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(raw)) * 100 / 128);
    return 0;
}
extern "C" std::uint32_t app_tick(std::int16_t* out) {
    Err e = probe();
    if (e != 0) return 0x80000000u | e;
    e = read_centi(out);
    if (e != 0) return 0x80000000u | e;
    return 0;
}

#else
/* ---------------- Result / expected ---------------- */
#if VARIANT == 3 || VARIANT == 5
#include <expected>
#endif

template <class E> struct Unexpected { E error; };
template <class E> Unexpected(E) -> Unexpected<E>;

template <class T, class E>
class [[nodiscard]] Result {
public:
    constexpr Result(T v) : val_(v), has_(true) {}
    constexpr Result(Unexpected<E> u) : err_(u.error), has_(false) {}
    constexpr bool has_value() const { return has_; }
    constexpr T value() const { return val_; }
    constexpr const T& operator*() const { return val_; }
    constexpr E error() const { return err_; }
private:
    union { T val_; E err_; };
    bool has_;
};
template <class E>
class [[nodiscard]] Result<void, E> {
public:
    constexpr Result() = default;
    constexpr Result(Unexpected<E> u) : err_(u.error), has_(false) {}
    constexpr bool has_value() const { return has_; }
    constexpr E error() const { return err_; }
private:
    E err_{};
    bool has_ = true;
};

#if VARIANT == 2
using DE = DrvErr16;
template <class T> using R = Result<T, DE>;
template <class E> constexpr auto mk_err(E e) { return Unexpected<E>{e}; }
#define DRV_BUS(b, r) DrvErr16::bus(b, r)
#elif VARIANT == 3
using DE = DrvErr16;
template <class T> using R = std::expected<T, DE>;
template <class E> constexpr auto mk_err(E e) { return std::unexpected<E>(e); }
#define DRV_BUS(b, r) DrvErr16::bus(b, r)
#elif VARIANT == 4
using DE = DrvErrWide;
template <class T> using R = Result<T, DE>;
template <class E> constexpr auto mk_err(E e) { return Unexpected<E>{e}; }
#define DRV_BUS(b, r) DrvErrWide::bus_err(b, r)
#elif VARIANT == 5
using DE = DrvErrWide;
template <class T> using R = std::expected<T, DE>;
template <class E> constexpr auto mk_err(E e) { return std::unexpected<E>(e); }
#define DRV_BUS(b, r) DrvErrWide::bus_err(b, r)
#endif

// value 는 has_value() 확인 뒤라 안전. USE_VALUE: expected 의 throw 경로가 어떻게 되는지 보기 위한 스위치.
#if defined(USE_VALUE)
#define GET(r) (r).value()
#else
#define GET(r) (*(r))
#endif
#define TRYV(var, ...)                                  \
    auto try_##var = (__VA_ARGS__);                     \
    if (!try_##var.has_value()) return mk_err(try_##var.error()); \
    const auto var = GET(try_##var)
#define TRYD(...)                                       \
    do { auto t_ = (__VA_ARGS__); if (!t_.has_value()) return mk_err(t_.error()); } while (0)

NOINLINE R<std::uint16_t> read_reg(std::uint8_t reg) {
    const std::uint8_t tx[1] = {reg};
    std::uint8_t rx[2];
    const int b = i2c_xfer(ADDR, tx, 1, rx, 2);
    if (b != 0) return mk_err(DRV_BUS(b, reg));
    return static_cast<std::uint16_t>(rx[0] << 8 | rx[1]);
}
NOINLINE R<void> probe() {
    TRYV(id, read_reg(REG_ID));
    if ((id & 0x0FFF) != 0x0117) return mk_err(DE::at(2, REG_ID));
    return {};
}
NOINLINE R<std::int16_t> read_centi() {
    TRYV(cfg, read_reg(REG_CONFIG));
    if ((cfg & DRDY) == 0) return mk_err(DE::at(3, REG_CONFIG));
    TRYV(raw, read_reg(REG_TEMP));
    if (raw == 0x8000) return mk_err(DE::at(4, REG_TEMP));
    return static_cast<std::int16_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(raw)) * 100 / 128);
}
extern "C" std::uint32_t app_tick(std::int16_t* out) {
    const auto p = probe();
    if (!p.has_value()) return 0x80000000u;
    const auto r = read_centi();
    if (!r.has_value()) return 0x80000000u;
    *out = *r;
    return 0;
}
#endif

#if defined(WITH_START)
extern "C" int i2c_xfer(std::uint8_t, const std::uint8_t*, unsigned, std::uint8_t* r, unsigned n) {
    for (unsigned i = 0; i < n; ++i) r[i] = 0;
    return 0;
}
extern "C" void _start() {
    std::int16_t out = 0;
    volatile std::uint32_t rc = app_tick(&out);
    (void)rc;
    for (;;) {}
}
#endif
