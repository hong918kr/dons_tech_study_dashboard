// CPP-M06 측정용: SPI 드라이버 TU
#include "demo_bus.hpp"
#ifndef ONLY_UART
static inline volatile u32& reg(u32 a) { return *reinterpret_cast<volatile u32*>(a); }

bool SpiDrv::init(u32 hz) { hz_ = hz; reg(base_) = 8000000u / hz; return true; }
u32 SpiDrv::write(const u8* p, u32 n) {
    reg(base_ + 0x18) = 0;                         // CS low
    for (u32 i = 0; i < n; ++i) { reg(base_ + 0x0C) = p[i]; while (reg(base_ + 8) & 0x80u) {} }
    reg(base_ + 0x18) = 1;                         // CS high
    return n;
}
void SpiDrv::selftest() { reg(base_ + 0x1C) = 0x11u; }
#endif
