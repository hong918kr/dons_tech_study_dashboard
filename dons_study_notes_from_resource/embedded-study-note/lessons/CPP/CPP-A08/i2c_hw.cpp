/* CPP-A08 i2c_hw.cpp — 링크 심의 제품 쪽 정의 (펌웨어 빌드에 링크) */
#include "hw_i2c.hpp"

namespace i2c {
BusErr write_read(uint8_t addr, uint8_t reg, std::span<uint8_t> rx) { return hw_xfer(addr, &reg, 1, rx.data(), rx.size()); }
BusErr write(uint8_t addr, std::span<const uint8_t> tx) { return hw_xfer(addr, tx.data(), tx.size(), nullptr, 0); }
}  // namespace i2c
