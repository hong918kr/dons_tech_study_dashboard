/* CPP-A08 추가 연습 — 한 번에 검증
 *   연습 1: 상태 없는 버스는 참조 대신 값으로 — [[no_unique_address]] 로 드라이버 4~8바이트 줄이기
 *   연습 2: 링크 심 + 인스턴스 2개 — 이음매 API 에 버스 번호를 넣어야 하는 이유
 *   연습 3: CMock 식 엄격한 기대(expectation) 목 — 순서·인자 불일치·누락 호출 검출 (생성기는 TEST-B05)
 *   연습 4: C 식 ops 테이블(함수 포인터 + ctx) 을 네 번째 이음매로 — TEST-B04 의 C 기법을 같은 concept 에 꽂기
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
 * 본문(solution.cpp)의 프로토콜·드라이버를 짧게 다시 담았다 (레슨 파일은 각자 자립).
 */
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <type_traits>

static int g_pass = 0, g_total = 0;
#define CHECK(...) do { ++g_total; if (__VA_ARGS__) { ++g_pass; std::printf("PASS %s\n", #__VA_ARGS__); } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__); } while (0)

/* ---------- 본문과 같은 프로토콜 (요약) ---------- */
namespace ts {
inline constexpr uint8_t kAddr = 0x48, kRegTemp = 0x00, kRegConfig = 0x01, kRegDeviceId = 0x0F;
inline constexpr uint16_t kDeviceId = 0x0117, kConfigCont8 = 0x0220;
}  // namespace ts
enum class BusErr : uint8_t { Ok, Nack, Timeout };
enum class DrvErr : uint8_t { Ok, Nack, Timeout, BadCrc, BadId, ConfigMismatch, NotInit };

constexpr uint8_t crc8(const uint8_t* p, size_t n) {
    uint8_t c = 0xFF;
    for (size_t i = 0; i < n; ++i) {
        c = static_cast<uint8_t>(c ^ p[i]);
        for (int b = 0; b < 8; ++b) c = (c & 0x80u) ? static_cast<uint8_t>((c << 1) ^ 0x31u) : static_cast<uint8_t>(c << 1);
    }
    return c;
}
template <class B>
concept I2cBus = requires(B& b, uint8_t a, uint8_t r, std::span<uint8_t> rx, std::span<const uint8_t> tx) {
    { b.write_read(a, r, rx) } -> std::same_as<BusErr>;
    { b.write(a, tx) } -> std::same_as<BusErr>;
};

/* ---------- 연습 1: 버스 보관 방식 BusSlot ----------
 * 상태 있는 버스 → 참조(포인터 하나). 상태 없는(empty) + 기본 생성 가능한 버스 → 값으로, [[no_unique_address]] 로 0바이트. */
template <class Bus> struct BusSlot {
    Bus& ref;
    constexpr explicit BusSlot(Bus& b) : ref(b) {}
    constexpr Bus& get() { return ref; }
};
template <class Bus>
    requires(std::is_empty_v<Bus> && std::is_default_constructible_v<Bus>)
struct BusSlot<Bus> {
    [[no_unique_address]] Bus b;
    constexpr explicit BusSlot(Bus&) {}
    constexpr Bus& get() { return b; }
};

struct DrvStats { uint16_t retries = 0, nacks = 0, timeouts = 0, crc_errors = 0; };

/* 본문 TempSensor 와 같은 로직. 차이는 bus_ 대신 slot_ 하나 */
template <I2cBus Bus>
class TempSensor {
public:
    static constexpr int kTries = 3;
    constexpr explicit TempSensor(Bus& bus, uint8_t addr = ts::kAddr) : slot_(bus), addr_(addr) {}
    [[nodiscard]] DrvErr init() {
        ready_ = false;
        uint16_t id = 0, cfg = 0;
        if (const DrvErr e = read_reg16(ts::kRegDeviceId, id); e != DrvErr::Ok) return e;
        if (id != ts::kDeviceId) return DrvErr::BadId;
        if (const DrvErr e = write_reg16(ts::kRegConfig, ts::kConfigCont8); e != DrvErr::Ok) return e;
        if (const DrvErr e = read_reg16(ts::kRegConfig, cfg); e != DrvErr::Ok) return e;
        if (cfg != ts::kConfigCont8) return DrvErr::ConfigMismatch;
        ready_ = true;
        return DrvErr::Ok;
    }
    [[nodiscard]] DrvErr read_milli_c(int32_t& out) {
        if (!ready_) return DrvErr::NotInit;
        uint16_t raw = 0;
        if (const DrvErr e = read_reg16(ts::kRegTemp, raw); e != DrvErr::Ok) return e;
        out = static_cast<int32_t>(static_cast<int16_t>(raw)) * 125 / 16;
        return DrvErr::Ok;
    }
    const DrvStats& stats() const { return stats_; }
private:
    DrvErr read_reg16(uint8_t reg, uint16_t& out) {
        DrvErr last = DrvErr::Nack;
        for (int i = 0; i < kTries; ++i) {
            if (i > 0) ++stats_.retries;
            uint8_t rx[3] = {};
            const BusErr e = slot_.get().write_read(addr_, reg, rx);
            if (e == BusErr::Timeout) { ++stats_.timeouts; return DrvErr::Timeout; }
            if (e == BusErr::Nack) { ++stats_.nacks; last = DrvErr::Nack; continue; }
            if (crc8(rx, 2) != rx[2]) { ++stats_.crc_errors; last = DrvErr::BadCrc; continue; }
            out = static_cast<uint16_t>(rx[0] << 8 | rx[1]);
            return DrvErr::Ok;
        }
        return last;
    }
    DrvErr write_reg16(uint8_t reg, uint16_t v) {
        uint8_t tx[4] = {reg, static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v & 0xFFu), 0};
        tx[3] = crc8(tx + 1, 2);
        for (int i = 0; i < kTries; ++i) {
            if (i > 0) ++stats_.retries;
            const BusErr e = slot_.get().write(addr_, tx);
            if (e == BusErr::Ok) return DrvErr::Ok;
            if (e == BusErr::Timeout) { ++stats_.timeouts; return DrvErr::Timeout; }
            ++stats_.nacks;
        }
        return DrvErr::Nack;
    }
    [[no_unique_address]] BusSlot<Bus> slot_;   // 슬롯 자체도 비어 있을 수 있으니 여기에도
    uint8_t addr_;
    bool ready_ = false;
    DrvStats stats_{};
};

/* 본문 TempSensor 의 배치를 흉내 낸 비교용 (참조 보관) */
struct RefLayout { void* bus; uint8_t addr; bool ready; DrvStats stats; };

/* ---------- 작은 가짜 장치 (본문 FakeTmp 요약) ---------- */
class MiniTmp {
public:
    uint16_t regs[16] = {};
    uint8_t addr = ts::kAddr;
    int nack_next = 0;         // 앞으로 n 번 NACK
    size_t calls = 0;
    MiniTmp() { regs[ts::kRegDeviceId] = ts::kDeviceId; regs[ts::kRegTemp] = 0x0C80; }
    BusErr write_read(uint8_t a, uint8_t reg, std::span<uint8_t> rx) {
        ++calls;
        if (a != addr || nack_next > 0) { if (nack_next > 0) --nack_next; return BusErr::Nack; }
        const uint16_t v = regs[reg & 0x0Fu];
        uint8_t f[3] = {static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v & 0xFFu), 0};
        f[2] = crc8(f, 2);
        for (size_t i = 0; i < rx.size() && i < 3; ++i) rx[i] = f[i];
        return BusErr::Ok;
    }
    BusErr write(uint8_t a, std::span<const uint8_t> tx) {
        ++calls;
        if (a != addr || nack_next > 0) { if (nack_next > 0) --nack_next; return BusErr::Nack; }
        if (tx.size() != 4 || crc8(tx.data() + 1, 2) != tx[3]) return BusErr::Nack;
        if (tx[0] == ts::kRegConfig) regs[ts::kRegConfig] = static_cast<uint16_t>(tx[1] << 8 | tx[2]);
        return BusErr::Ok;
    }
};

/* ---------- 연습 2: 링크 심 + 버스 번호 ----------
 * 이음매 함수가 하나뿐이면 가짜도 하나뿐 → 두 센서를 따로 고장 낼 수 없다. 버스 번호를 이음매 API 에 넣는다. */
enum class BusId : uint8_t { I2c1 = 0, I2c2 = 1 };
namespace i2c2 {
BusErr write_read(BusId bus, uint8_t addr, uint8_t reg, std::span<uint8_t> rx);
BusErr write(BusId bus, uint8_t addr, std::span<const uint8_t> tx);
}  // namespace i2c2
struct LinkBusN {   // 이제 empty 가 아니다 (1바이트) → BusSlot 은 참조 쪽을 고른다
    BusId id;
    BusErr write_read(uint8_t a, uint8_t r, std::span<uint8_t> rx) { return i2c2::write_read(id, a, r, rx); }
    BusErr write(uint8_t a, std::span<const uint8_t> tx) { return i2c2::write(id, a, tx); }
};
MiniTmp g_bus_fakes[2];   // "i2c_fake.cpp" 역할: 버스마다 가짜 하나
namespace i2c2 {
BusErr write_read(BusId bus, uint8_t addr, uint8_t reg, std::span<uint8_t> rx) {
    return g_bus_fakes[static_cast<uint8_t>(bus)].write_read(addr, reg, rx);
}
BusErr write(BusId bus, uint8_t addr, std::span<const uint8_t> tx) {
    return g_bus_fakes[static_cast<uint8_t>(bus)].write(addr, tx);
}
}  // namespace i2c2

/* ---------- 연습 3: 엄격한 기대 목 ExpectBus ----------
 * expect_*() 로 "이 순서로 이 호출이 와야 한다" 를 적고, 실제 호출이 다르면 오류를 기록, verify() 는 남은 기대(누락)도 잡는다. */
class ExpectBus {
public:
    enum class Kind : uint8_t { WriteRead, Write };
    struct Exp { Kind kind; uint8_t addr; uint8_t reg; uint16_t value; BusErr ret; };
    void expect_read(uint8_t addr, uint8_t reg, uint16_t reply, BusErr ret = BusErr::Ok) { push({Kind::WriteRead, addr, reg, reply, ret}); }
    void expect_write(uint8_t addr, uint8_t reg, uint16_t value, BusErr ret = BusErr::Ok) { push({Kind::Write, addr, reg, value, ret}); }
    BusErr write_read(uint8_t a, uint8_t reg, std::span<uint8_t> rx) {
        const Exp* e = next(Kind::WriteRead, a, reg, 0, false);
        if (e == nullptr) return BusErr::Nack;   // 기대 밖 호출: 오류 기록 + NACK 로 대답
        uint8_t f[3] = {static_cast<uint8_t>(e->value >> 8), static_cast<uint8_t>(e->value & 0xFFu), 0};
        f[2] = crc8(f, 2);
        for (size_t i = 0; i < rx.size() && i < 3; ++i) rx[i] = f[i];
        return e->ret;
    }
    BusErr write(uint8_t a, std::span<const uint8_t> tx) {
        const uint8_t reg = tx.empty() ? 0xFFu : tx[0];
        const uint16_t v = tx.size() >= 3 ? static_cast<uint16_t>(tx[1] << 8 | tx[2]) : 0;
        const Exp* e = next(Kind::Write, a, reg, v, true);
        return e == nullptr ? BusErr::Nack : e->ret;
    }
    int unexpected = 0, mismatched = 0;
    int missing() const { return n_ - pos_; }
    bool verify() const { return unexpected == 0 && mismatched == 0 && missing() == 0; }
private:
    void push(const Exp& e) { if (n_ < 16) q_[n_++] = e; }
    const Exp* next(Kind k, uint8_t a, uint8_t reg, uint16_t v, bool check_value) {
        if (pos_ >= n_) { ++unexpected; return nullptr; }
        const Exp& e = q_[pos_++];
        if (e.kind != k || e.addr != a || e.reg != reg || (check_value && e.value != v)) { ++mismatched; return nullptr; }
        return &e;
    }
    Exp q_[16] = {};
    int n_ = 0, pos_ = 0;
};

/* 순서를 바꾼 "버그 있는" init — CONFIG 를 먼저 쓰고 ID 를 나중에 본다 (엉뚱한 칩에 쓰기 위험) */
template <I2cBus Bus> DrvErr init_config_first(Bus& bus) {
    uint8_t tx[4] = {ts::kRegConfig, 0x02, 0x20, 0};
    tx[3] = crc8(tx + 1, 2);
    if (bus.write(ts::kAddr, tx) != BusErr::Ok) return DrvErr::Nack;
    uint8_t rx[3] = {};
    if (bus.write_read(ts::kAddr, ts::kRegDeviceId, rx) != BusErr::Ok) return DrvErr::Nack;
    return (rx[0] << 8 | rx[1]) == ts::kDeviceId ? DrvErr::Ok : DrvErr::BadId;
}

/* ---------- 연습 4: C 식 ops 테이블을 네 번째 이음매로 ----------
 * TEST-B04 의 함수 포인터 주입: const 테이블(플래시) + 인스턴스 ctx. 어댑터 OpsBus 하나로 같은 concept 를 만족시킨다. */
struct I2cOps {
    BusErr (*write_read)(void* ctx, uint8_t addr, uint8_t reg, uint8_t* rx, size_t n);
    BusErr (*write)(void* ctx, uint8_t addr, const uint8_t* tx, size_t n);
};
struct OpsBus {
    const I2cOps* ops;
    void* ctx;
    BusErr write_read(uint8_t a, uint8_t r, std::span<uint8_t> rx) { return ops->write_read(ctx, a, r, rx.data(), rx.size()); }
    BusErr write(uint8_t a, std::span<const uint8_t> tx) { return ops->write(ctx, a, tx.data(), tx.size()); }
};
/* 가짜용 ops: ctx 를 MiniTmp* 로 되돌린다 */
constexpr I2cOps kMiniOps = {
    [](void* c, uint8_t a, uint8_t r, uint8_t* rx, size_t n) { return static_cast<MiniTmp*>(c)->write_read(a, r, {rx, n}); },
    [](void* c, uint8_t a, const uint8_t* tx, size_t n) { return static_cast<MiniTmp*>(c)->write(a, {tx, n}); },
};

static_assert(I2cBus<MiniTmp> && I2cBus<LinkBusN> && I2cBus<ExpectBus> && I2cBus<OpsBus>);
struct LinkBus0 {   // 연습 1 용: 상태 없는 링크 심 어댑터 (본문 LinkBus 와 같은 모양)
    BusErr write_read(uint8_t a, uint8_t r, std::span<uint8_t> rx) { return i2c2::write_read(BusId::I2c1, a, r, rx); }
    BusErr write(uint8_t a, std::span<const uint8_t> tx) { return i2c2::write(BusId::I2c1, a, tx); }
};

int main() {
    std::printf("-- 연습 1: [[no_unique_address]]\n");
    CHECK(std::is_empty_v<LinkBus0> && !std::is_empty_v<LinkBusN>);
    CHECK(sizeof(TempSensor<LinkBus0>) < sizeof(RefLayout));                 // 참조 하나만큼 줄었다
    CHECK(sizeof(TempSensor<LinkBus0>) == 2u + sizeof(DrvStats));             // addr + ready + stats, 버스 0바이트
    CHECK(sizeof(TempSensor<MiniTmp>) == sizeof(RefLayout));                  // 상태 있는 버스는 그대로 참조
    std::printf("   sizeof: ref=%zu  value(no_unique_address)=%zu\n", sizeof(TempSensor<MiniTmp>), sizeof(TempSensor<LinkBus0>));
    {
        g_bus_fakes[0] = MiniTmp{};
        LinkBus0 b;
        TempSensor<LinkBus0> s{b};
        CHECK(s.init() == DrvErr::Ok);
        int32_t t = 0;
        CHECK(s.read_milli_c(t) == DrvErr::Ok && t == 25'000);
    }

    std::printf("-- 연습 2: 링크 심 + 버스 번호\n");
    {
        g_bus_fakes[0] = MiniTmp{};
        g_bus_fakes[1] = MiniTmp{};
        g_bus_fakes[1].regs[ts::kRegTemp] = 0x2800;                            // 80 °C (모터 쪽)
        LinkBusN b1{BusId::I2c1}, b2{BusId::I2c2};
        TempSensor<LinkBusN> cpu{b1}, motor{b2};
        CHECK(cpu.init() == DrvErr::Ok && motor.init() == DrvErr::Ok);
        g_bus_fakes[1].nack_next = 3;                                          // I2C2 만 고장
        int32_t t1 = 0, t2 = 0;
        CHECK(cpu.read_milli_c(t1) == DrvErr::Ok && t1 == 25'000);
        CHECK(motor.read_milli_c(t2) == DrvErr::Nack);
        CHECK(g_bus_fakes[0].calls == 4u && g_bus_fakes[1].calls == 6u);
        g_bus_fakes[1].nack_next = 0;
        CHECK(motor.read_milli_c(t2) == DrvErr::Ok && t2 == 80'000);
    }

    std::printf("-- 연습 3: 엄격한 기대 목\n");
    {
        ExpectBus m;   // 올바른 순서: ID 읽기 → CONFIG 쓰기 → CONFIG 읽기
        m.expect_read(ts::kAddr, ts::kRegDeviceId, ts::kDeviceId);
        m.expect_write(ts::kAddr, ts::kRegConfig, ts::kConfigCont8);
        m.expect_read(ts::kAddr, ts::kRegConfig, ts::kConfigCont8);
        TempSensor<ExpectBus> s{m};
        CHECK(s.init() == DrvErr::Ok);
        CHECK(m.verify());
    }
    {
        ExpectBus m;   // 같은 기대에 순서가 틀린 구현 → 불일치 검출
        m.expect_read(ts::kAddr, ts::kRegDeviceId, ts::kDeviceId);
        m.expect_write(ts::kAddr, ts::kRegConfig, ts::kConfigCont8);
        m.expect_read(ts::kAddr, ts::kRegConfig, ts::kConfigCont8);
        CHECK(init_config_first(m) != DrvErr::Ok);
        CHECK(!m.verify() && m.mismatched >= 1);
    }
    {
        ExpectBus m;   // 기대 2개만 적음 → 3번째 호출은 "기대 밖"
        m.expect_read(ts::kAddr, ts::kRegDeviceId, ts::kDeviceId);
        m.expect_write(ts::kAddr, ts::kRegConfig, ts::kConfigCont8);
        TempSensor<ExpectBus> s{m};
        CHECK(s.init() != DrvErr::Ok);
        CHECK(m.unexpected >= 1 && !m.verify());
    }
    {
        ExpectBus m;   // 기대했는데 안 온 호출 → missing
        m.expect_read(ts::kAddr, ts::kRegDeviceId, 0x1234);                   // 엉뚱한 칩 → BadId 에서 멈춘다
        m.expect_write(ts::kAddr, ts::kRegConfig, ts::kConfigCont8);
        TempSensor<ExpectBus> s{m};
        CHECK(s.init() == DrvErr::BadId);
        CHECK(m.missing() == 1 && !m.verify());
    }

    std::printf("-- 연습 4: ops 테이블 이음매\n");
    {
        MiniTmp dev;
        OpsBus bus{&kMiniOps, &dev};
        TempSensor<OpsBus> s{bus};
        CHECK(s.init() == DrvErr::Ok);
        dev.nack_next = 2;
        int32_t t = 0;
        CHECK(s.read_milli_c(t) == DrvErr::Ok && t == 25'000 && s.stats().retries == 2u);
        CHECK(sizeof(OpsBus) == 2 * sizeof(void*));                            // 테이블 포인터 + ctx (vptr 1개 + this 와 같은 몫)
        CHECK(!std::is_polymorphic_v<OpsBus>);
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
