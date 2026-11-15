// fixed_mc100.cpp — CPP-A09 refactor of legacy_mc100.cpp. Same section order, so `diff -u` lines up.
// Builds with the firmware flags: -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti (tidy.sh build).
// Rule ids "M:x.y.z" = MISRA C++:2023 rule numbers (checked against the public Polyspace rule list; text not quoted).
// Every behaviour change vs legacy is listed as D1..D9 in the lesson; everything else is old == new.
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace mc {

// ---------------------------------------------------------------- registers (M:6.7.2 no globals, M:8.2.5 one deviation)
enum class RegOff : std::uint8_t { HallDr = 0x10U / 4U, NtcDr = 0x14U / 4U, PwmArr = 0x2CU / 4U, PwmCcr0 = 0x34U / 4U };
inline constexpr std::size_t kRegWords = 32U;
inline constexpr std::uintptr_t kPeriphBase = 0x4001'0000U;
inline constexpr std::size_t kMotors = 2U;

class RegBlock {                                   // injected: real MMIO block or a host mock array
public:
    explicit RegBlock(std::span<volatile std::uint32_t, kRegWords> words) noexcept : words_{words} {}
    [[nodiscard]] std::uint32_t read(RegOff off, std::size_t idx = 0U) const noexcept
    {
        return words_[static_cast<std::size_t>(off) + idx];
    }
    void write(RegOff off, std::size_t idx, std::uint32_t v) const noexcept
    {
        words_[static_cast<std::size_t>(off) + idx] = v;
    }
private:
    std::span<volatile std::uint32_t, kRegWords> words_;
};

// Deviation DEV-1 (M:8.2.5, M:8.2.6): turning a datasheet address into a pointer needs reinterpret_cast.
// It lives here and nowhere else; host tests never call this function.
inline RegBlock mmio_block() noexcept
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr) DEV-1
    auto* const base = reinterpret_cast<volatile std::uint32_t*>(kPeriphBase);
    return RegBlock{std::span<volatile std::uint32_t, kRegWords>{base, kRegWords}};
}

// ---------------------------------------------------------------- logging (M:21.10.1 no <cstdarg>)
template <std::size_t N>
class Log {
public:
    void put(std::string_view s) noexcept
    {
        for (const char c : s) {
            push(c);
        }
    }
    void put(std::int32_t v) noexcept                     // "%d" without varargs; INT32_MIN safe
    {
        auto mag = static_cast<std::uint32_t>(v);          // C++20 [conv.integral]: modular, well defined
        if (v < 0) {
            push('-');
            mag = 0U - mag;                                // unsigned negate = |v|, also for INT32_MIN
        }
        std::array<char, kMaxDigits> tmp{};
        std::size_t n = 0U;
        bool more = true;
        while (more) {
            tmp[n] = static_cast<char>('0' + static_cast<int>(mag % kRadix));
            mag /= kRadix;
            ++n;
            more = (mag != 0U);
        }
        while (n > 0U) {
            --n;
            push(tmp[n]);
        }
    }
    [[nodiscard]] std::string_view view() const noexcept { return {buf_.data(), len_}; }
    [[nodiscard]] bool truncated() const noexcept { return truncated_; }
    void clear() noexcept
    {
        len_ = 0U;
        truncated_ = false;
    }
private:
    static constexpr std::size_t kMaxDigits = 10U;
    static constexpr std::uint32_t kRadix = 10U;
    void push(char c) noexcept                             // D8: full buffer → drop + flag, never overflow
    {
        if (len_ < N) {
            buf_[len_] = c;
            ++len_;
        } else {
            truncated_ = true;
        }
    }
    std::array<char, N> buf_{};
    std::size_t len_{0U};
    bool truncated_{false};
};
inline constexpr std::size_t kLogBytes = 64U;
using McLog = Log<kLogBytes>;

// ---------------------------------------------------------------- Part A: conversions, casts, unions
inline constexpr std::int32_t kPctMax = 100;
inline constexpr std::int32_t kDutyMax = 255;

[[nodiscard]] constexpr std::uint8_t duty_from_pct(std::int32_t pct) noexcept   // M:7.0.6, D1 saturate
{
    std::int32_t d = 0;
    if (pct >= kPctMax) {
        d = kDutyMax;
    } else if (pct > 0) {
        d = (pct * kDutyMax) / kPctMax;
    } else {
        d = 0;
    }
    return static_cast<std::uint8_t>(d);
}

struct Telemetry {
    std::uint8_t type{0U};
    std::uint16_t id{0U};
    std::int16_t value{0};
    std::uint16_t crc{0U};
};
inline constexpr std::size_t kFrameLen = 7U;
inline constexpr std::size_t kIdOff = 1U;           // wire layout: [type][id:2][value:2][crc:2]
inline constexpr std::size_t kValueOff = 3U;
inline constexpr std::size_t kCrcOff = 5U;

[[nodiscard]] constexpr std::uint16_t le16(std::span<const std::uint8_t, 2> b) noexcept
{
    constexpr std::uint32_t kByteBits = 8U;
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << kByteBits));
}

// M:8.2.5 / M:8.2.2: no reinterpret_cast, no C-style cast; byte order explicit (little endian on the wire)
[[nodiscard]] constexpr Telemetry decode(std::span<const std::uint8_t, kFrameLen> f) noexcept
{
    return Telemetry{.type = f[0],
                     .id = le16(f.subspan<kIdOff, 2>()),
                     .value = std::bit_cast<std::int16_t>(le16(f.subspan<kValueOff, 2>())),
                     .crc = le16(f.subspan<kCrcOff, 2>())};
}

[[nodiscard]] constexpr std::uint32_t float_bits(float x) noexcept   // M:12.3.1 no union
{
    return std::bit_cast<std::uint32_t>(x);
}

inline constexpr float kQ3Scale = 8.0F;
inline constexpr float kQ3Hi = 32768.0F;                // first value whose truncation does not fit int16
inline constexpr float kQ3Lo = -32769.0F;

[[nodiscard]] inline std::int16_t rpm_to_q3(float rpm) noexcept      // [conv.fpint]: out of range is UB → D3
{
    const float q = rpm * kQ3Scale;
    std::int16_t r = 0;
    if (std::isnan(q)) {
        r = 0;
    } else if (q >= kQ3Hi) {
        r = std::numeric_limits<std::int16_t>::max();
    } else if (q <= kQ3Lo) {
        r = std::numeric_limits<std::int16_t>::min();
    } else {
        r = static_cast<std::int16_t>(q);
    }
    return r;
}

// ---------------------------------------------------------------- Part B: classes and lifetime
inline constexpr std::size_t kMaxWindow = 256U;

template <std::size_t N>
class SampleWindow {                               // rule of 0: no new[], no owning pointer, copies are deep
    static_assert(N > 0U && N <= kMaxWindow, "window size");
public:
    void push(std::int16_t v) noexcept
    {
        buf_[head_] = v;
        head_ = (head_ + 1U) % N;
        if (count_ < N) {
            ++count_;
        }
    }
    [[nodiscard]] std::int16_t mean() const noexcept
    {
        std::int32_t s = 0;
        for (std::size_t i = 0U; i < count_; ++i) {
            s += buf_[i];
        }
        return (count_ == 0U) ? std::int16_t{0} : static_cast<std::int16_t>(s / static_cast<std::int32_t>(count_));
    }
    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    void clear() noexcept                          // D5: legacy only zeroed count → mean read stale slots
    {
        count_ = 0U;
        head_ = 0U;
    }
private:
    std::array<std::int16_t, N> buf_{};
    std::size_t head_{0U};
    std::size_t count_{0U};
};

struct PidGains {                                  // one struct instead of 3 swappable floats
    float kp{0.0F};
    float ki{0.0F};
    float kd{0.0F};
};

class Pid {
public:
    explicit Pid(PidGains g) noexcept : g_{g} {}
    void reset() noexcept
    {
        integ_ = 0.0F;
        d_filt_ = 0.0F;
        prev_ = 0.0F;
    }
    [[nodiscard]] float step(float err) noexcept
    {
        integ_ = std::clamp(integ_ + err, -kIntegLimit, kIntegLimit);
        // double on purpose: bit-identical to legacy (0.9 * float promotes). Changing to 0.9F is D-change (연습 1)
        d_filt_ = static_cast<float>((kAlpha * static_cast<double>(d_filt_)) +
                                     (kBeta * static_cast<double>(err - prev_)));
        prev_ = err;
        return (g_.kp * err) + (g_.ki * integ_) + (g_.kd * d_filt_);
    }
private:
    static constexpr float kIntegLimit = 1000.0F;
    static constexpr double kAlpha = 0.9;
    static constexpr double kBeta = 0.1;
    PidGains g_;
    float integ_{0.0F};                            // M:15.1.4: NSDMI → never indeterminate (D4)
    float d_filt_{0.0F};
    float prev_{0.0F};
};

class Sensor {                                     // interface: no delete through base (protected dtor)
public:
    Sensor(const Sensor&) = delete;                // M:15.0.1: polymorphic base → no copy/move (no slicing)
    Sensor& operator=(const Sensor&) = delete;
    Sensor(Sensor&&) = delete;
    Sensor& operator=(Sensor&&) = delete;
    [[nodiscard]] virtual std::int16_t read() noexcept = 0;
    virtual void calibrate() noexcept {}           // replaces dynamic_cast<HallSensor*> (RTTI off)
protected:
    Sensor() = default;
    ~Sensor() = default;                           // protected, non-virtual: no deleting dtor, no operator delete
};

class HallSensor final : public Sensor {
public:
    explicit HallSensor(RegBlock regs) noexcept : regs_{regs} {}
    [[nodiscard]] std::int16_t read() noexcept override
    {
        hist_.push(static_cast<std::int16_t>(regs_.read(RegOff::HallDr)));   // C++20: modular, [conv.integral]
        return hist_.mean();
    }
    void calibrate() noexcept override { hist_.clear(); }
private:
    static constexpr std::size_t kHistory = 8U;
    RegBlock regs_;
    SampleWindow<kHistory> hist_{};
};

class NtcSensor final : public Sensor {
public:
    explicit NtcSensor(RegBlock regs) noexcept : regs_{regs} {}
    [[nodiscard]] std::int16_t read() noexcept override
    {
        return static_cast<std::int16_t>(regs_.read(RegOff::NtcDr));
    }
private:
    RegBlock regs_;
};

inline void calibrate_all(std::span<Sensor* const> sensors) noexcept
{
    for (Sensor* const s : sensors) {
        s->calibrate();
    }
}

enum class Status : std::uint8_t { Ok, BadMotor, BadRpm };
inline constexpr std::int32_t kMaxRpm = 12000;

// M:18.x: no exceptions in this build (-fno-exceptions); the error is a [[nodiscard]] value (CPP-M11)
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) DEV-2: unsigned index vs signed rpm, both range-checked
[[nodiscard]] inline Status set_speed(const RegBlock& regs, std::size_t motor, std::int32_t rpm) noexcept
{
    Status st = Status::Ok;
    if (motor >= kMotors) {
        st = Status::BadMotor;                     // D6: legacy wrote some other register
    } else if ((rpm < 0) || (rpm > kMaxRpm)) {
        st = Status::BadRpm;                       // D7: negative rpm was accepted (unsigned wrap)
    } else {
        const std::uint64_t arr = regs.read(RegOff::PwmArr);
        const std::uint64_t ccr = (static_cast<std::uint64_t>(rpm) * arr) / static_cast<std::uint64_t>(kMaxRpm);
        regs.write(RegOff::PwmCcr0, motor, static_cast<std::uint32_t>(ccr));
    }
    return st;
}

// ---------------------------------------------------------------- Part C: control flow
inline constexpr std::array<std::uint16_t, 16> kRpmLut{0U,    300U,  600U,  900U,  1200U, 1800U, 2400U,  3000U,
                                                       3600U, 4800U, 6000U, 7200U, 8400U, 9600U, 10800U, 12000U};

[[nodiscard]] constexpr std::size_t lut_find(std::span<const std::uint16_t> t, std::uint16_t rpm) noexcept
{
    std::size_t lo = 0U;                           // M:8.2.10: iterative lower bound, O(1) stack
    std::size_t hi = t.size();
    while (lo < hi) {
        const std::size_t mid = lo + ((hi - lo) / 2U);
        if (t[mid] < rpm) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    return lo;
}

[[nodiscard]] constexpr std::size_t rpm_index(std::uint16_t rpm) noexcept { return lut_find(kRpmLut, rpm); }

// M:9.6.x: no goto. Probe is injected (template seam, CPP-A08), fault counter and log are passed in.
template <typename Probe>
[[nodiscard]] std::int32_t motor_init(Probe probe, std::int32_t retries, McLog& log, std::int32_t& faults) noexcept
{
    std::int32_t tries = 0;
    bool ok = false;
    while (!ok) {
        ok = (probe() == 0);
        if (!ok) {
            ++tries;
            if (tries >= retries) {
                break;
            }
        }
    }
    std::int32_t result = tries;
    if (ok) {
        log.put("init ok ");
    } else {
        ++faults;
        log.put("init fail ");
        result = -1;
    }
    log.put(tries);
    log.put("\n");
    return result;
}

inline void report(const RegBlock& regs, std::size_t motor, McLog& log) noexcept   // D9: integer %, no float varargs
{
    constexpr std::uint64_t kPercent = 100U;
    const std::uint64_t arr = regs.read(RegOff::PwmArr);
    const std::uint64_t ccr = regs.read(RegOff::PwmCcr0, motor);
    const std::uint64_t pct = (arr == 0U) ? 0U : ((ccr * kPercent) / arr);
    log.put("m");
    log.put(static_cast<std::int32_t>(motor));
    log.put(" duty=");
    log.put(static_cast<std::int32_t>(std::min<std::uint64_t>(pct, kPercent)));
    log.put("%\n");
}

}  // namespace mc
