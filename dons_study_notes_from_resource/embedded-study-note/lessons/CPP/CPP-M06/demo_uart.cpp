// CPP-M06 측정용: UART 드라이버 TU
#include "demo_bus.hpp"
static inline volatile u32& reg(u32 a) { return *reinterpret_cast<volatile u32*>(a); }

bool UartDrv::init(u32 hz) { hz_ = hz; reg(base_ + 0x0C) = 16000000u / hz; reg(base_) = 1; return true; }
u32 UartDrv::write(const u8* p, u32 n) {
    for (u32 i = 0; i < n; ++i) { while (!(reg(base_ + 4) & 0x80u)) {} reg(base_ + 8) = p[i]; }
    return n;
}
void UartDrv::selftest() { reg(base_ + 0x10) = 0xA5u; reg(base_ + 0x10) = 0x5Au; }
void UartDrv::debug_dump() const { reg(base_ + 0x14) = base_; reg(base_ + 0x14) = hz_; }
