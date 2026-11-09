// CPP-M06 측정용: I2C 드라이버 TU
#include "demo_bus.hpp"
#ifndef ONLY_UART
static inline volatile u32& reg(u32 a) { return *reinterpret_cast<volatile u32*>(a); }

bool I2cDrv::init(u32 hz) { hz_ = hz; reg(base_ + 0x1C) = 4000000u / hz; return true; }
u32 I2cDrv::write(const u8* p, u32 n) {
    reg(base_ + 0x10) = static_cast<u32>(addr_) << 1;
    for (u32 i = 0; i < n; ++i) { while (!(reg(base_ + 0x14) & 0x80u)) {} reg(base_ + 0x10) = p[i]; }
    reg(base_) = 0x200u;                           // STOP
    return n;
}
void I2cDrv::selftest() { reg(base_ + 0x20) = 0x22u; }
#endif
