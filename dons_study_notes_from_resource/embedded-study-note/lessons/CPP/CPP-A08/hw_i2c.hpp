/* CPP-A08 hw_i2c.hpp — 제품 빌드용 "진짜" 버스 (타깃 전용, 호스트에서 실행하지 않는다).
 * 가상의 FIFO 형 I2C 컨트롤러 (실제 칩의 레지스터 배치가 아니다). 주소만 STM32F4 I2C1 의 0x4000'5400 을 빌렸다 (대표값).
 *   ADDR/TXLEN/RXLEN 설정 → TXFIFO 에 바이트 → CTRL.START → STATUS.DONE 폴링(상한 있음) → NACK 확인 → RXFIFO 읽기 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include "sensor.hpp"

struct I2cRegs {
    volatile uint32_t CTRL, STATUS, ADDR, TXLEN, RXLEN, TXFIFO, RXFIFO;
};
inline constexpr uintptr_t kI2c1Base = 0x4000'5400u;
inline constexpr uint32_t kCtrlStart = 1u << 0;
inline constexpr uint32_t kStDone = 1u << 0, kStNack = 1u << 1;
inline constexpr uint32_t kSpinLimit = 10'000;   // 무한 대기 금지: 상한 → BusErr::Timeout

inline BusErr hw_xfer(uint8_t addr, const uint8_t* tx, size_t ntx, uint8_t* rx, size_t nrx) {
    I2cRegs& r = *reinterpret_cast<I2cRegs*>(kI2c1Base);
    r.ADDR = addr;
    r.TXLEN = static_cast<uint32_t>(ntx);
    r.RXLEN = static_cast<uint32_t>(nrx);
    for (size_t i = 0; i < ntx; ++i) r.TXFIFO = tx[i];
    r.CTRL = kCtrlStart;
    uint32_t st = 0;
    for (uint32_t spin = 0; ((st = r.STATUS) & kStDone) == 0;)
        if (++spin == kSpinLimit) return BusErr::Timeout;
    if (st & kStNack) return BusErr::Nack;
    for (size_t i = 0; i < nrx; ++i) rx[i] = static_cast<uint8_t>(r.RXFIFO);
    return BusErr::Ok;
}

struct HwBus {   // (1) 템플릿 주입용: 상태 없음, 전부 inline → 드라이버 안으로 녹아든다
    BusErr write_read(uint8_t a, uint8_t reg, std::span<uint8_t> rx) { return hw_xfer(a, &reg, 1, rx.data(), rx.size()); }
    BusErr write(uint8_t a, std::span<const uint8_t> tx) { return hw_xfer(a, tx.data(), tx.size(), nullptr, 0); }
};

class HwBusV final : public II2c {   // (2) 인터페이스 주입용
public:
    BusErr write_read(uint8_t a, uint8_t reg, std::span<uint8_t> rx) override { return hw_xfer(a, &reg, 1, rx.data(), rx.size()); }
    BusErr write(uint8_t a, std::span<const uint8_t> tx) override { return hw_xfer(a, tx.data(), tx.size(), nullptr, 0); }
};
