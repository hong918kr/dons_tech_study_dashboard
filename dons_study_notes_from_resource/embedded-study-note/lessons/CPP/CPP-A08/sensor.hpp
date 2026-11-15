/* CPP-A08 sensor.hpp — solution.cpp 의 프로토콜·이음매·드라이버 구간을 그대로 옮긴 "제품 헤더".
 * (solution.cpp 는 레슨 사이트용으로 자립해야 해서 같은 코드를 한 번 더 담고 있다. 두 구간이 같다는 것은 link_seam.sh 가 diff 로 확인한다.)
 * 아래 extern template 두 줄만 추가: virtual·링크 심 판의 드라이버 본체는 sensor.cpp 한 곳에서만 컴파일된다. */
#pragma once
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

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

#ifndef SENSOR_INSTANTIATE   // sensor.cpp 만 이 매크로를 정의하고 명시적 인스턴스화를 한다
extern template class TempSensor<II2c>;
extern template class TempSensor<LinkBus>;
#endif
