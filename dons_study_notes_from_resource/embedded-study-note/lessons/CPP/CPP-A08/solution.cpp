/* CPP-A08 테스트 가능한 C++ 드라이버 설계: 의존성 주입 vs 링크 심
 *
 * I2C 온도 센서 드라이버 TempSensor<Bus> 를 한 번만 쓰고, 하드웨어와의 이음매(seam)를 세 가지로 바꿔 끼운다.
 *   (1) 템플릿 파라미터 주입      TempSensor<FakeBus>   — 버스 타입이 컴파일 시점에 정해진다 (가상 함수 없음)
 *   (2) 인터페이스(virtual) 주입   TempSensor<II2c>      — 드라이버는 II2c& 만 안다. 호출은 vtable 경유
 *   (3) 링크 타임 대체             TempSensor<LinkBus>   — i2c::write_read() 의 "정의" 를 어느 .cpp 와 링크하느냐로 정한다
 *   같은 테스트 묶음(TEST_CASE_TEMPLATE)이 세 Rig 에 대해 세 번 돈다.
 *   Part A: CRC-8·온도 변환 + 가짜 장치 FakeTmp(스크립트 응답 + 호출 로그) + 드라이버 본체
 *   Part B: 오류 주입(NACK·타임아웃·CRC·ID·설정 무시) → 재시도 정책과 통계, virtual 어댑터, 링크 심 가짜와 테스트 격리
 *   Part C: TempMonitor — 100 ms tick 감시, 과열 알람(히스테리시스), 연속 실패 → Faulted → 주기적 재초기화
 *
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
 *   -DUSE_DOCTEST 이면 아래 미니 하네스 대신 진짜 doctest.h 로 같은 테스트를 빌드한다 (doctest_check.sh).
 */
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <span>
#include <type_traits>

/* ===================== 테스트 하네스 (doctest 와 같은 매크로 이름) =====================
 * TEST_CASE / TEST_CASE_TEMPLATE / CHECK 만 쓴다 → doctest.h 로 바꿔도 테스트 본문은 그대로 컴파일된다.
 * 출력: 테스트 케이스마다 PASS/FAIL 한 줄, 실패한 CHECK 는 파일:줄, 마지막에 "N/M passed" (CHECK 개수). */
#ifdef USE_DOCTEST
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#else
namespace mini {
struct Case { const char* name; const char* type; void (*fn)(); };
inline Case g_cases[64];
inline int g_ncases = 0, g_pass = 0, g_total = 0;
inline bool add(const char* name, const char* type, void (*fn)()) {
    if (g_ncases < 64) g_cases[g_ncases++] = Case{name, type, fn};
    return true;
}
template <class... Ts, class L> bool add_types(const char* name, L) {   // 타입마다 L{}.operator()<T>() 를 등록
    (add(name, Ts::kName, [] { L{}.template operator()<Ts>(); }), ...);
    return true;
}
inline void check(bool ok, const char* expr, const char* file, int line) {
    ++g_total;
    if (ok) ++g_pass;
    else std::printf("    FAIL %s:%d  %s\n", file, line, expr);
}
}  // namespace mini
#define MINI_CAT2(a, b) a##b
#define MINI_CAT(a, b) MINI_CAT2(a, b)
#define CHECK(...) ::mini::check(static_cast<bool>(__VA_ARGS__), #__VA_ARGS__, __FILE__, __LINE__)
#define TEST_CASE(name)                                                                                   \
    static void MINI_CAT(mini_tc_, __LINE__)();                                                           \
    [[maybe_unused]] static const bool MINI_CAT(mini_reg_, __LINE__) =                                    \
        ::mini::add(name, "", &MINI_CAT(mini_tc_, __LINE__));                                             \
    static void MINI_CAT(mini_tc_, __LINE__)()
#define TEST_CASE_TEMPLATE(name, T, ...)                                                                  \
    template <class T> static void MINI_CAT(mini_tc_, __LINE__)();                                        \
    [[maybe_unused]] static const bool MINI_CAT(mini_reg_, __LINE__) =                                    \
        ::mini::add_types<__VA_ARGS__>(name, []<class U>() { MINI_CAT(mini_tc_, __LINE__)<U>(); });       \
    template <class T> static void MINI_CAT(mini_tc_, __LINE__)()
#endif

/* ===================== 센서 프로토콜 (가상 센서 "TS117", 대표값) =====================
 * 레지스터 맵은 TI TMP117 류를 본떴고(TEMP 0x00, CONFIG 0x01, DEVICE_ID 0x0F = 0x0117, 7.8125 m°C/LSB),
 * 각 16비트 워드 뒤에 Sensirion SHT3x 류 CRC-8(다항식 0x31, 초기값 0xFF)을 붙였다. 실제 TMP117 에는 CRC 가 없다.
 *   읽기:  [W addr][reg] [Sr addr R] → MSB, LSB, CRC(MSB,LSB)
 *   쓰기:  [W addr][reg][MSB][LSB][CRC]   — CRC 가 틀리면 장치가 NACK */
namespace ts {
inline constexpr uint8_t kAddr = 0x48;
inline constexpr uint8_t kRegTemp = 0x00, kRegConfig = 0x01, kRegDeviceId = 0x0F;
inline constexpr uint16_t kDeviceId = 0x0117;
inline constexpr uint16_t kConfigCont8 = 0x0220;   // 연속 변환 + 평균 8회 (대표값)
}  // namespace ts

enum class BusErr : uint8_t { Ok, Nack, Timeout };
enum class DrvErr : uint8_t { Ok, Nack, Timeout, BadCrc, BadId, ConfigMismatch, NotInit };

/* [A1] CRC-8: poly 0x31 (x^8+x^5+x^4+1), init 0xFF, 반사 없음, 최종 XOR 없음. 검사값: {0xBE,0xEF} → 0x92 */
constexpr uint8_t crc8(const uint8_t* p, size_t n) {
    uint8_t c = 0xFF;
    for (size_t i = 0; i < n; ++i) {
        c = static_cast<uint8_t>(c ^ p[i]);
        for (int b = 0; b < 8; ++b)
            c = (c & 0x80u) ? static_cast<uint8_t>((c << 1) ^ 0x31u) : static_cast<uint8_t>(c << 1);
    }
    return c;
}

/* [A2] raw(2의 보수 16비트) → m°C. 1 LSB = 7.8125 m°C = 125/16. 정수만, 0 쪽으로 버림 (C++ 정수 나눗셈) */
constexpr int32_t to_milli_c(uint16_t raw) {
    return static_cast<int32_t>(static_cast<int16_t>(raw)) * 125 / 16;
}

/* ===================== 세 가지 이음매 =====================
 * 버스가 갖춰야 할 것 = concept. (1)·(3) 은 이 concept 를 정적으로, (2) 는 II2c 의 가상 함수로 만족한다. */
template <class B>
concept I2cBus = requires(B& b, uint8_t addr, uint8_t reg, std::span<uint8_t> rx, std::span<const uint8_t> tx) {
    { b.write_read(addr, reg, rx) } -> std::same_as<BusErr>;
    { b.write(addr, tx) } -> std::same_as<BusErr>;
};

class II2c {   // (2) 인터페이스: 순수 가상 2개, protected 비가상 소멸자 (기반 포인터 delete 금지 — CPP-M06 Part C)
public:
    virtual BusErr write_read(uint8_t addr, uint8_t reg, std::span<uint8_t> rx) = 0;
    virtual BusErr write(uint8_t addr, std::span<const uint8_t> tx) = 0;
protected:
    ~II2c() = default;
};

namespace i2c {   // (3) 링크 심: 선언만. 정의는 링크되는 쪽(i2c_hw.cpp / i2c_fake.cpp)이 정한다
BusErr write_read(uint8_t addr, uint8_t reg, std::span<uint8_t> rx);
BusErr write(uint8_t addr, std::span<const uint8_t> tx);
}  // namespace i2c
struct LinkBus {  // 상태 없는 어댑터 — 템플릿 인자 자리만 채운다. 인라인되면 드라이버가 i2c::write_read 를 직접 부른다
    BusErr write_read(uint8_t a, uint8_t reg, std::span<uint8_t> rx) { return i2c::write_read(a, reg, rx); }
    BusErr write(uint8_t a, std::span<const uint8_t> tx) { return i2c::write(a, tx); }
};

/* ===================== 드라이버 (한 번만 쓴다) ===================== */
struct DrvStats { uint16_t retries = 0, nacks = 0, timeouts = 0, crc_errors = 0; };

template <I2cBus Bus>
class TempSensor {
public:
    static constexpr int kTries = 3;   // NACK·CRC 오류는 최대 3번 시도. 타임아웃은 재시도하지 않는다
    constexpr explicit TempSensor(Bus& bus, uint8_t addr = ts::kAddr) : bus_(bus), addr_(addr) {}
    [[nodiscard]] DrvErr init();                       // ID 확인 → CONFIG 쓰기 → 읽어서 확인
    [[nodiscard]] DrvErr read_milli_c(int32_t& out);   // init 전이면 NotInit (버스 접근 없음)
    bool ready() const { return ready_; }
    const DrvStats& stats() const { return stats_; }
private:
    DrvErr read_reg16(uint8_t reg, uint16_t& out);
    DrvErr write_reg16(uint8_t reg, uint16_t value);
    Bus& bus_;
    uint8_t addr_;
    bool ready_ = false;
    DrvStats stats_{};
};

/* [A3][B1] 레지스터 하나 읽기: 3바이트(MSB, LSB, CRC). NACK·CRC 오류 → 재시도(최대 kTries), 타임아웃 → 즉시 반환 */
template <I2cBus Bus>
DrvErr TempSensor<Bus>::read_reg16(uint8_t reg, uint16_t& out) {
    DrvErr last = DrvErr::Nack;
    for (int attempt = 0; attempt < kTries; ++attempt) {
        if (attempt > 0) ++stats_.retries;
        uint8_t rx[3] = {};
        const BusErr e = bus_.write_read(addr_, reg, rx);
        if (e == BusErr::Timeout) { ++stats_.timeouts; return DrvErr::Timeout; }
        if (e == BusErr::Nack) { ++stats_.nacks; last = DrvErr::Nack; continue; }
        if (crc8(rx, 2) != rx[2]) { ++stats_.crc_errors; last = DrvErr::BadCrc; continue; }
        out = static_cast<uint16_t>(rx[0] << 8 | rx[1]);
        return DrvErr::Ok;
    }
    return last;
}

/* [A3][B1] 레지스터 쓰기: reg, MSB, LSB, CRC. NACK → 재시도, 타임아웃 → 즉시 반환 */
template <I2cBus Bus>
DrvErr TempSensor<Bus>::write_reg16(uint8_t reg, uint16_t value) {
    uint8_t tx[4] = {reg, static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFFu), 0};
    tx[3] = crc8(tx + 1, 2);
    for (int attempt = 0; attempt < kTries; ++attempt) {
        if (attempt > 0) ++stats_.retries;
        const BusErr e = bus_.write(addr_, tx);
        if (e == BusErr::Ok) return DrvErr::Ok;
        if (e == BusErr::Timeout) { ++stats_.timeouts; return DrvErr::Timeout; }
        ++stats_.nacks;
    }
    return DrvErr::Nack;
}

/* [A3] init: DEVICE_ID 확인(틀리면 BadId, CONFIG 는 건드리지 않음) → CONFIG 쓰기 → 읽어서 같은지 확인 */
template <I2cBus Bus>
DrvErr TempSensor<Bus>::init() {
    ready_ = false;
    uint16_t id = 0;
    if (const DrvErr e = read_reg16(ts::kRegDeviceId, id); e != DrvErr::Ok) return e;
    if (id != ts::kDeviceId) return DrvErr::BadId;
    if (const DrvErr e = write_reg16(ts::kRegConfig, ts::kConfigCont8); e != DrvErr::Ok) return e;
    uint16_t cfg = 0;
    if (const DrvErr e = read_reg16(ts::kRegConfig, cfg); e != DrvErr::Ok) return e;
    if (cfg != ts::kConfigCont8) return DrvErr::ConfigMismatch;
    ready_ = true;
    return DrvErr::Ok;
}

/* [A3] 온도 읽기 */
template <I2cBus Bus>
DrvErr TempSensor<Bus>::read_milli_c(int32_t& out) {
    if (!ready_) return DrvErr::NotInit;
    uint16_t raw = 0;
    if (const DrvErr e = read_reg16(ts::kRegTemp, raw); e != DrvErr::Ok) return e;
    out = to_milli_c(raw);
    return DrvErr::Ok;
}

/* ===================== 가짜 장치 FakeTmp: 스크립트 응답 + 오류 주입 + 호출 로그 =====================
 * 레지스터 파일을 가진 "센서 모형". 각 트랜잭션은 주입 큐에서 Fault 하나를 꺼내 적용한다 (비었으면 None). */
enum class Fault : uint8_t { None, Nack, Timeout, BadCrc };
enum class Op : uint8_t { WriteRead, Write };
struct Call { Op op; uint8_t addr; uint8_t reg; uint8_t len; uint16_t value; BusErr result; };

class FakeTmp {
public:
    static constexpr size_t kLog = 48, kQ = 16;
    uint16_t regs[16] = {};
    uint8_t addr = ts::kAddr;
    bool ignore_config_writes = false;   // "설정이 안 먹는 칩" (전원 불량·쓰기 보호 흉내)

    FakeTmp() { reset(); }
    void reset() {
        for (uint16_t& r : regs) r = 0;
        regs[ts::kRegDeviceId] = ts::kDeviceId;
        regs[ts::kRegTemp] = 0x0C80;     // 25.000 °C
        addr = ts::kAddr;
        ignore_config_writes = false;
        nlog_ = dropped_ = 0;
        fq_head_ = fq_n_ = tq_head_ = tq_n_ = 0;
    }
    void inject(Fault f) { if (fq_n_ < kQ) fq_[(fq_head_ + fq_n_++) % kQ] = f; }
    void inject(Fault f, int times) { for (int i = 0; i < times; ++i) inject(f); }
    void script_temp(std::initializer_list<uint16_t> raws) {   // TEMP 읽기마다 하나씩. 다 쓰면 regs[TEMP] 유지
        for (uint16_t r : raws) if (tq_n_ < kQ) tq_[(tq_head_ + tq_n_++) % kQ] = r;
    }
    size_t pending_faults() const { return fq_n_; }
    size_t ncalls() const { return nlog_; }
    const Call& call(size_t i) const { return log_[i]; }
    size_t count(Op op, uint8_t reg) const {
        size_t n = 0;
        for (size_t i = 0; i < nlog_; ++i) if (log_[i].op == op && log_[i].reg == reg) ++n;
        return n;
    }

    /* [A4] write_read: 주소 불일치 → NACK. Fault 적용. 성공이면 rx = MSB, LSB, CRC (BadCrc 면 CRC 한 비트 뒤집음).
     *      TEMP 는 스크립트가 있으면 그 값을 꺼낸다 (전송이 성공했을 때만 소비). 모든 호출을 로그에 남긴다. */
    BusErr write_read(uint8_t a, uint8_t reg, std::span<uint8_t> rx) {
        const Fault f = next_fault();
        uint16_t v = 0;
        BusErr res = BusErr::Ok;
        if (a != addr || f == Fault::Nack) res = BusErr::Nack;
        else if (f == Fault::Timeout) res = BusErr::Timeout;
        else {
            v = regs[reg & 0x0Fu];
            if (reg == ts::kRegTemp && tq_n_ > 0) { v = tq_[tq_head_]; tq_head_ = (tq_head_ + 1) % kQ; --tq_n_; }
            uint8_t frame[3] = {static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v & 0xFFu), 0};
            frame[2] = crc8(frame, 2);
            if (f == Fault::BadCrc) frame[2] ^= 0x01u;
            for (size_t i = 0; i < rx.size() && i < 3; ++i) rx[i] = frame[i];
        }
        log(Call{Op::WriteRead, a, reg, static_cast<uint8_t>(rx.size()), v, res});
        return res;
    }
    /* [A4] write: 주소 불일치·Nack → NACK, Timeout. 4바이트 + CRC 맞음 → 쓰기 가능한 레지스터(CONFIG)만 갱신.
     *      CRC 틀림 → NACK (장치가 거부). 모든 호출을 로그에 남긴다 (value = 보낸 값). */
    BusErr write(uint8_t a, std::span<const uint8_t> tx) {
        const Fault f = next_fault();
        const uint8_t reg = tx.empty() ? 0xFFu : tx[0];
        const uint16_t v = tx.size() >= 3 ? static_cast<uint16_t>(tx[1] << 8 | tx[2]) : 0;
        BusErr res = BusErr::Ok;
        if (a != addr || f == Fault::Nack) res = BusErr::Nack;
        else if (f == Fault::Timeout) res = BusErr::Timeout;
        else if (tx.size() != 4 || crc8(tx.data() + 1, 2) != tx[3]) res = BusErr::Nack;
        else if (reg == ts::kRegConfig && !ignore_config_writes) regs[ts::kRegConfig] = v;
        log(Call{Op::Write, a, reg, static_cast<uint8_t>(tx.size()), v, res});
        return res;
    }

private:
    Fault next_fault() {
        if (fq_n_ == 0) return Fault::None;
        const Fault f = fq_[fq_head_];
        fq_head_ = (fq_head_ + 1) % kQ;
        --fq_n_;
        return f;
    }
    void log(const Call& c) { if (nlog_ < kLog) log_[nlog_++] = c; else ++dropped_; }
    Call log_[kLog] = {};
    size_t nlog_ = 0, dropped_ = 0;
    Fault fq_[kQ] = {};
    size_t fq_head_ = 0, fq_n_ = 0;
    uint16_t tq_[kQ] = {};
    size_t tq_head_ = 0, tq_n_ = 0;
};

/* ===================== 세 Rig: 같은 FakeTmp 를 세 이음매로 꽂는다 ===================== */
class FakeBus {   // (1) 템플릿 주입용: 가상 함수 없음, FakeTmp 로 그대로 전달
public:
    explicit FakeBus(FakeTmp& d) : d_(d) {}
    BusErr write_read(uint8_t a, uint8_t reg, std::span<uint8_t> rx) { return d_.write_read(a, reg, rx); }
    BusErr write(uint8_t a, std::span<const uint8_t> tx) { return d_.write(a, tx); }
private:
    FakeTmp& d_;
};

/* [B2] (2) 인터페이스 주입용 가짜: II2c 를 구현해 FakeTmp 로 전달 */
class FakeBusV final : public II2c {
public:
    explicit FakeBusV(FakeTmp& d) : d_(d) {}
    BusErr write_read(uint8_t a, uint8_t reg, std::span<uint8_t> rx) override { return d_.write_read(a, reg, rx); }
    BusErr write(uint8_t a, std::span<const uint8_t> tx) override { return d_.write(a, tx); }
private:
    FakeTmp& d_;
};

/* [B3] (3) 링크 심 가짜: 테스트 빌드에서 링크되는 i2c::write_read / i2c::write 정의.
 * 한 프로그램에 정의는 하나뿐 → 가짜도 전역 하나. 이 파일 안에서는 이 블록이 "i2c_fake.cpp" 역할이다
 * (진짜로 파일을 나눈 빌드: link_seam.sh). 전역이므로 테스트마다 상태를 초기화해야 한다. */
FakeTmp g_link_fake;
namespace i2c {
BusErr write_read(uint8_t addr, uint8_t reg, std::span<uint8_t> rx) { return g_link_fake.write_read(addr, reg, rx); }
BusErr write(uint8_t addr, std::span<const uint8_t> tx) { return g_link_fake.write(addr, tx); }
}  // namespace i2c
void link_fake_reset() { g_link_fake.reset(); }

struct RigT {
    static constexpr const char* kName = "template";
    FakeTmp dev;
    FakeBus bus{dev};
    TempSensor<FakeBus> s{bus};
    FakeTmp& fake() { return dev; }
};
struct RigV {
    static constexpr const char* kName = "virtual";
    FakeTmp dev;
    FakeBusV bus{dev};
    TempSensor<II2c> s{bus};
    FakeTmp& fake() { return dev; }
};
struct RigL {
    static constexpr const char* kName = "link";
    LinkBus bus;
    TempSensor<LinkBus> s{bus};
    RigL() { link_fake_reset(); }        // 전역 가짜: 테스트마다 새 상태로
    FakeTmp& fake() { return g_link_fake; }
};

/* ===================== Part C: TempMonitor — 100 ms 마다 tick() ===================== */
enum class MonState : uint8_t { Init, Running, Faulted };

template <class Sensor>
class TempMonitor {
public:
    static constexpr int32_t kAlarmOn = 85'000, kAlarmOff = 80'000;   // m°C, 히스테리시스 5 °C
    static constexpr uint8_t kMaxFails = 3;                           // 연속 실패 3번 → Faulted
    static constexpr uint8_t kReinitTicks = 10;                       // Faulted 에서 10 tick(1 s)마다 init 재시도
    explicit TempMonitor(Sensor& s) : s_(s) {}
    void tick();
    MonState state() const { return state_; }
    bool alarm() const { return alarm_; }
    int32_t filtered() const { return filtered_; }
    uint32_t fault_events() const { return fault_events_; }
private:
    void enter_faulted() { state_ = MonState::Faulted; alarm_ = true; wait_ = 0; ++fault_events_; }   // fail-safe: 알람 켬
    void enter_running() { state_ = MonState::Running; fails_ = 0; n_ = 0; }
    void push(int32_t v) {   // 4점 이동 평균. (재)시작 후 첫 샘플은 창 전체를 그 값으로 채운다
        if (n_ == 0) { for (int32_t& w : win_) w = v; n_ = 4; idx_ = 0; }
        else { win_[idx_] = v; idx_ = static_cast<uint8_t>((idx_ + 1) & 3u); }
        filtered_ = (win_[0] + win_[1] + win_[2] + win_[3]) / 4;
    }
    Sensor& s_;
    MonState state_ = MonState::Init;
    bool alarm_ = false;
    uint8_t fails_ = 0, wait_ = 0, idx_ = 0, n_ = 0;
    int32_t win_[4] = {};
    int32_t filtered_ = 0;
    uint32_t fault_events_ = 0;
};

/* [C1] tick: Init → init() 성공이면 Running, 실패면 Faulted.
 *      Running → 읽기 실패 연속 kMaxFails 번이면 Faulted. 성공이면 fails_=0, push, 알람 히스테리시스 (켜짐 ≥ On, 꺼짐 ≤ Off).
 *      Faulted → kReinitTicks 번째 tick 마다 init() 재시도, 성공이면 Running (알람은 다음 샘플이 판단). */
template <class Sensor>
void TempMonitor<Sensor>::tick() {
    switch (state_) {
    case MonState::Init:
        if (s_.init() == DrvErr::Ok) enter_running();
        else enter_faulted();
        return;
    case MonState::Faulted:
        if (++wait_ < kReinitTicks) return;
        wait_ = 0;
        if (s_.init() == DrvErr::Ok) enter_running();
        return;
    case MonState::Running: {
        int32_t t = 0;
        if (s_.read_milli_c(t) != DrvErr::Ok) {
            if (++fails_ >= kMaxFails) enter_faulted();
            return;
        }
        fails_ = 0;
        push(t);
        if (!alarm_ && filtered_ >= kAlarmOn) alarm_ = true;
        else if (alarm_ && filtered_ <= kAlarmOff) alarm_ = false;
        return;
    }
    }
}

/* ===================== 컴파일 시점 증명 (requires / traits) ===================== */
struct OnlyRead {   // write 가 없는 버스 → concept 불만족
    BusErr write_read(uint8_t, uint8_t, std::span<uint8_t>) { return BusErr::Ok; }
};
template <class B> constexpr bool can_make_sensor = requires { typename TempSensor<B>; };
static_assert(I2cBus<FakeBus> && I2cBus<II2c> && I2cBus<LinkBus>, "세 이음매 모두 같은 concept 를 만족");
static_assert(!I2cBus<OnlyRead> && !I2cBus<int>);
static_assert(can_make_sensor<FakeBus> && !can_make_sensor<OnlyRead>, "잘못된 버스는 TempSensor<B> 자체가 안 만들어진다");
static_assert(std::is_abstract_v<II2c> && !std::is_destructible_v<II2c>, "II2c* 로 delete 불가 (protected 소멸자)");
static_assert(!std::is_polymorphic_v<TempSensor<FakeBus>> && !std::is_polymorphic_v<TempSensor<II2c>>,
              "드라이버 자체에는 vptr 가 없다 — vptr 는 버스 쪽(FakeBusV)에");
static_assert(std::is_polymorphic_v<FakeBusV> && std::is_empty_v<LinkBus> && !std::is_polymorphic_v<FakeBus>);

/* ===================== 테스트 ===================== */
constexpr uint16_t raw_of(int32_t milli_c) { return static_cast<uint16_t>(static_cast<int16_t>(milli_c * 16 / 125)); }

TEST_CASE("A: crc8 and temperature conversion") {
    const uint8_t beef[2] = {0xBE, 0xEF};
    CHECK(crc8(beef, 2) == 0x92);                 // SHT3x 데이터시트 예시로 알려진 값
    const uint8_t zero[2] = {0x00, 0x00};
    CHECK(crc8(zero, 2) == 0x81);
    CHECK(crc8(beef, 0) == 0xFF);                 // 빈 입력 = 초기값
    CHECK(to_milli_c(0x0C80) == 25'000);
    CHECK(to_milli_c(0xFF80) == -1'000);
    CHECK(to_milli_c(0x0001) == 7);               // 7.8125 → 0 쪽으로 버림
    CHECK(to_milli_c(0xFFFF) == -7);
    CHECK(to_milli_c(0x7FFF) == 255'992);
    CHECK(to_milli_c(0x8000) == -256'000);
    CHECK(raw_of(85'000) == 10'880);
}

TEST_CASE_TEMPLATE("A: init probes ID, writes CONFIG, reads it back", R, RigT, RigV, RigL) {
    R r;
    CHECK(r.s.init() == DrvErr::Ok);
    CHECK(r.s.ready());
    FakeTmp& f = r.fake();
    CHECK(f.ncalls() == 3u);
    CHECK(f.call(0).op == Op::WriteRead);
    CHECK(f.call(0).reg == ts::kRegDeviceId);
    CHECK(f.call(0).len == 3u);
    CHECK(f.call(1).op == Op::Write);
    CHECK(f.call(1).reg == ts::kRegConfig);
    CHECK(f.call(1).value == ts::kConfigCont8);
    CHECK(f.call(1).len == 4u);
    CHECK(f.call(2).reg == ts::kRegConfig);
    CHECK(f.regs[ts::kRegConfig] == ts::kConfigCont8);
    CHECK(f.call(0).addr == ts::kAddr);
}

TEST_CASE_TEMPLATE("A: read converts scripted raw values", R, RigT, RigV, RigL) {
    R r;
    int32_t t = 123;
    CHECK(r.s.read_milli_c(t) == DrvErr::NotInit);   // init 전: 버스에 아무것도 안 나간다
    CHECK(r.fake().ncalls() == 0u);
    CHECK(t == 123);
    CHECK(r.s.init() == DrvErr::Ok);
    r.fake().script_temp({0x0C80, 0xFF80, 0x0001, 0x7FFF, 0x8000});
    const int32_t want[] = {25'000, -1'000, 7, 255'992, -256'000, 25'000};   // 마지막: 스크립트 소진 → regs[TEMP]
    for (int32_t w : want) {
        CHECK(r.s.read_milli_c(t) == DrvErr::Ok);
        CHECK(t == w);
    }
    CHECK(r.fake().count(Op::WriteRead, ts::kRegTemp) == 6u);
}

TEST_CASE_TEMPLATE("B: NACK is retried, then gives up after kTries", R, RigT, RigV, RigL) {
    R r;
    CHECK(r.s.init() == DrvErr::Ok);
    int32_t t = 0;
    r.fake().inject(Fault::Nack, 2);
    CHECK(r.s.read_milli_c(t) == DrvErr::Ok);            // 3번째 시도에 성공
    CHECK(t == 25'000);
    CHECK(r.s.stats().nacks == 2u);
    CHECK(r.s.stats().retries == 2u);
    r.fake().inject(Fault::Nack, 4);
    const size_t before = r.fake().ncalls();
    CHECK(r.s.read_milli_c(t) == DrvErr::Nack);
    CHECK(r.fake().ncalls() - before == 3u);             // 정확히 3번 시도
    CHECK(r.fake().pending_faults() == 1u);              // 4번째 주입은 남아 있다
}

TEST_CASE_TEMPLATE("B: timeout is not retried", R, RigT, RigV, RigL) {
    R r;
    CHECK(r.s.init() == DrvErr::Ok);
    int32_t t = 0;
    r.fake().inject(Fault::Timeout);
    CHECK(r.s.read_milli_c(t) == DrvErr::Timeout);
    CHECK(r.fake().ncalls() == 4u);                      // init 3 + 읽기 1
    CHECK(r.s.stats().timeouts == 1u);
    CHECK(r.s.stats().retries == 0u);
    r.fake().inject(Fault::Timeout);                     // init 도중 타임아웃 → 바로 반환
    CHECK(r.s.init() == DrvErr::Timeout);
    CHECK(!r.s.ready());
}

TEST_CASE_TEMPLATE("B: bad CRC is retried; three bad CRCs is an error", R, RigT, RigV, RigL) {
    R r;
    CHECK(r.s.init() == DrvErr::Ok);
    int32_t t = 0;
    r.fake().script_temp({raw_of(30'000), raw_of(31'000)});
    r.fake().inject(Fault::BadCrc);
    CHECK(r.s.read_milli_c(t) == DrvErr::Ok);
    CHECK(t == 31'000);                                  // CRC 틀린 첫 응답(30 °C)은 버려졌다
    CHECK(r.s.stats().crc_errors == 1u);
    r.fake().inject(Fault::BadCrc, 3);
    CHECK(r.s.read_milli_c(t) == DrvErr::BadCrc);
    CHECK(r.s.stats().crc_errors == 4u);
    CHECK(t == 31'000);                                  // 실패하면 out 을 건드리지 않는다
}

TEST_CASE_TEMPLATE("B: wrong ID, ignored CONFIG, wrong address", R, RigT, RigV, RigL) {
    R r;
    r.fake().regs[ts::kRegDeviceId] = 0x1234;
    CHECK(r.s.init() == DrvErr::BadId);
    CHECK(!r.s.ready());
    CHECK(r.fake().ncalls() == 1u);                      // 엉뚱한 칩에 CONFIG 를 쓰지 않는다
    r.fake().regs[ts::kRegDeviceId] = ts::kDeviceId;
    r.fake().ignore_config_writes = true;
    CHECK(r.s.init() == DrvErr::ConfigMismatch);
    CHECK(!r.s.ready());
    r.fake().ignore_config_writes = false;
    r.fake().addr = 0x49;                                // 보드에서 ADDR 핀이 다르게 묶였다
    CHECK(r.s.init() == DrvErr::Nack);
    CHECK(r.fake().count(Op::WriteRead, ts::kRegDeviceId) == 1u + 1u + 3u);
}

TEST_CASE_TEMPLATE("B: every test starts from a clean fake", R, RigT, RigV, RigL) {
    R r;   // 바로 앞 테스트는 addr=0x49 로 바꾸고 로그 5줄을 남긴 채 끝났다. 링크 심 가짜는 전역이라 reset 이 없으면 여기로 샌다
    CHECK(r.fake().ncalls() == 0u);
    CHECK(r.fake().pending_faults() == 0u);
    CHECK(r.s.init() == DrvErr::Ok);
    CHECK(r.s.stats().retries == 0u);
}

TEST_CASE_TEMPLATE("C: monitor heats up, alarm with hysteresis", R, RigT, RigV, RigL) {
    R r;
    TempMonitor<decltype(r.s)> m(r.s);
    r.fake().script_temp({raw_of(25'000), raw_of(25'000), raw_of(90'000), raw_of(90'000), raw_of(90'000),
                          raw_of(90'000), raw_of(84'000), raw_of(84'000), raw_of(80'000), raw_of(79'000),
                          raw_of(79'000), raw_of(79'000)});
    m.tick();                                             // tick 1: init
    CHECK(m.state() == MonState::Running);
    const int32_t want_f[] = {25'000, 25'000, 41'250, 57'500, 73'750, 90'000, 88'500, 87'000, 84'500, 81'750, 80'500, 79'250};
    const bool want_a[] = {false, false, false, false, false, true, true, true, true, true, true, false};
    int bad = 0;
    for (int i = 0; i < 12; ++i) {
        m.tick();
        if (m.filtered() != want_f[i] || m.alarm() != want_a[i]) ++bad;
    }
    CHECK(bad == 0);
    CHECK(m.filtered() == 79'250);
    CHECK(!m.alarm());
    CHECK(r.fake().ncalls() == 3u + 12u);
}

TEST_CASE_TEMPLATE("C: alarm thresholds are inclusive (>= on, <= off)", R, RigT, RigV, RigL) {
    R r;
    TempMonitor<decltype(r.s)> m(r.s);
    r.fake().script_temp({raw_of(85'000), raw_of(80'000), raw_of(80'000), raw_of(80'000), raw_of(80'000)});
    m.tick();                                             // init
    m.tick();                                             // 첫 샘플 85 °C → 창 [85,85,85,85] = 85000 ≥ On
    CHECK(m.filtered() == 85'000);
    CHECK(m.alarm());
    for (int i = 0; i < 3; ++i) m.tick();                 // 83750, 82500, 81250 — 아직 켜짐
    CHECK(m.filtered() == 81'250);
    CHECK(m.alarm());
    m.tick();                                             // 80000 ≤ Off → 꺼짐
    CHECK(m.filtered() == 80'000);
    CHECK(!m.alarm());
}

TEST_CASE_TEMPLATE("C: three failures -> Faulted, re-init every 10 ticks", R, RigT, RigV, RigL) {
    R r;
    TempMonitor<decltype(r.s)> m(r.s);
    m.tick();                                             // tick 1: init (3 호출)
    m.tick();                                             // tick 2: 25 °C
    r.fake().inject(Fault::Nack, 2);
    m.tick();                                             // tick 3: NACK 2번은 드라이버가 흡수 → 실패 아님
    CHECK(m.state() == MonState::Running);
    r.fake().inject(Fault::Timeout, 2);
    m.tick();                                             // tick 4, 5: 실패 2번
    m.tick();
    m.tick();                                             // tick 6: 성공 → 연속 실패 카운트 0 으로
    CHECK(m.state() == MonState::Running);
    r.fake().inject(Fault::Timeout, 3);
    m.tick();
    m.tick();
    CHECK(m.state() == MonState::Running);               // 연속 실패 2번까지는 버틴다
    m.tick();                                             // tick 9: 연속 3번째 → Faulted
    CHECK(m.state() == MonState::Faulted);
    CHECK(m.alarm());                                     // 모르면 위험한 쪽으로
    CHECK(m.fault_events() == 1u);
    const size_t calls = r.fake().ncalls();
    CHECK(calls == 3u + 1u + 3u + 2u + 1u + 3u);
    for (int i = 0; i < 9; ++i) m.tick();                 // tick 10..18: 대기, 버스 조용
    CHECK(r.fake().ncalls() == calls);
    CHECK(m.state() == MonState::Faulted);
    m.tick();                                             // tick 19: 재초기화
    CHECK(m.state() == MonState::Running);
    CHECK(r.fake().ncalls() == calls + 3u);
    m.tick();                                             // tick 20: 25 °C → 알람 해제
    CHECK(!m.alarm());
    CHECK(m.filtered() == 25'000);
}

TEST_CASE_TEMPLATE("C: init fails (wrong chip) -> Faulted until fixed", R, RigT, RigV, RigL) {
    R r;
    TempMonitor<decltype(r.s)> m(r.s);
    r.fake().regs[ts::kRegDeviceId] = 0x0000;
    m.tick();
    CHECK(m.state() == MonState::Faulted);
    CHECK(m.alarm());
    for (int i = 0; i < 10; ++i) m.tick();               // tick 11: 재시도했지만 여전히 BadId
    CHECK(m.state() == MonState::Faulted);
    CHECK(r.fake().ncalls() == 2u);                       // BadId 는 ID 읽기 1번으로 끝
    r.fake().regs[ts::kRegDeviceId] = ts::kDeviceId;      // 전원 레일이 올라왔다
    for (int i = 0; i < 10; ++i) m.tick();               // tick 21
    CHECK(m.state() == MonState::Running);
    CHECK(m.fault_events() == 1u);                        // 재시도 실패는 새 사건이 아니다
}

#ifndef USE_DOCTEST
int main() {
    for (int i = 0; i < mini::g_ncases; ++i) {
        const int fails_before = mini::g_total - mini::g_pass;
        mini::g_cases[i].fn();
        const bool ok = (mini::g_total - mini::g_pass) == fails_before;
        const char* ty = mini::g_cases[i].type;
        std::printf("%s %s%s%s%s\n", ok ? "PASS" : "FAIL", mini::g_cases[i].name, ty[0] ? " <" : "", ty, ty[0] ? ">" : "");
    }
    std::printf("%d/%d passed\n", mini::g_pass, mini::g_total);
    return mini::g_pass == mini::g_total ? 0 : 1;
}
#endif
