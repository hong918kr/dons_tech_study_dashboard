/* CPP-A08 i2c_fake.cpp — 링크 심의 테스트 쪽 정의 (호스트 테스트 빌드에 링크). 같은 이름, 같은 시그니처, 다른 몸통. */
#include "fake_tmp.hpp"

static FakeTmp g_fake;   // 프로그램에 정의가 하나뿐이니 가짜도 하나뿐 — 테스트마다 link_fake().reset()
FakeTmp& link_fake() { return g_fake; }

namespace i2c {
BusErr write_read(uint8_t addr, uint8_t reg, std::span<uint8_t> rx) { return g_fake.write_read(addr, reg, rx); }
BusErr write(uint8_t addr, std::span<const uint8_t> tx) { return g_fake.write(addr, tx); }
}  // namespace i2c
