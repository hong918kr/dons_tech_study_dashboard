// demo_linker_regs.cpp — MMIO without reinterpret_cast: the LINKER places the register struct (bash tidy.sh vm)
//   arm-none-eabi-g++ ... -Wl,--defsym=PWM1=0x40010000   (in a real project: a line in the .ld script)
#include <cstdint>

struct PwmRegs {
    volatile std::uint32_t pad[11];     // 0x00..0x2B
    volatile std::uint32_t arr;         // 0x2C
    volatile std::uint32_t pad2;        // 0x30
    volatile std::uint32_t ccr[2];      // 0x34, 0x38
};
static_assert(__builtin_offsetof(PwmRegs, ccr) == 0x34, "layout");

extern PwmRegs PWM1;                                       // no definition in C++: the symbol IS the address

void ccr0_linker(std::uint32_t v) { PWM1.ccr[0] = v; }    // (a) linker-placed object
void ccr0_cast(std::uint32_t v)                           // (b) the usual reinterpret_cast (DEV-1 style)
{
    reinterpret_cast<PwmRegs*>(0x40010000u)->ccr[0] = v;  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr) DEV-1
}

extern "C" [[noreturn]] void _start()
{
    ccr0_linker(5u);
    ccr0_cast(6u);
    for (;;) {
    }
}
