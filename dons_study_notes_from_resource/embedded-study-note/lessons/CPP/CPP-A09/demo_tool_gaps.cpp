// demo_tool_gaps.cpp — things MISRA C++:2023 forbids that the clang-tidy run in tidy.sh does NOT report.
//   c++ -std=c++20 -Wall -Wextra -O2 demo_tool_gaps.cpp -o dtg && ./dtg
//   bash tidy.sh gaps        (clang-tidy with the full check set + -Wall -Wextra -Wformat on this file)
#include <cstdarg>
#include <cstdint>
#include <cstdio>

// (1) two side-effecting register reads in ONE expression. C++17 sequences the operands of <<
//     (E1 before E2, [expr.shift]) but NOT the operands of | → order of the two calls is unspecified.
static uint8_t latch = 0xAA, sample_hi = 0x12, sample_lo = 0x34;
static int reads = 0;
static uint8_t rd(int reg)                      // reading HI latches LO (typical 16-bit sensor)
{
    ++reads;
    if (reg == 0) { latch = sample_lo; return sample_hi; }
    return latch;
}
static uint16_t read16() { return static_cast<uint16_t>((rd(0) << 8) | rd(1)); }

// (2) virtual call from a constructor: dispatches to Base::name, not Derived (MISRA 15.1.1)
struct Base {
    Base() { std::printf("ctor sees: %s\n", name()); }
    virtual ~Base() = default;
    Base(const Base&) = delete;
    Base& operator=(const Base&) = delete;
    Base(Base&&) = delete;
    Base& operator=(Base&&) = delete;
    virtual const char* name() const { return "Base"; }
};
struct Uart final : Base {
    const char* name() const override { return "Uart"; }
};

// (3) uint16_t * uint16_t is int * int after promotion → 0xFFFF * 0xFFFF overflows int (UB, MISRA 7.0.5)
static uint32_t area(uint16_t w, uint16_t h) { return w * h; }

// (4) user varargs logger: no format attribute → no type check of the arguments at all
static void log_plain(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
}
#ifdef SHOW_FORMAT
__attribute__((format(printf, 1, 2)))
#endif
static void log_checked(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
}

int main(int argc, char**)
{
    std::printf("read16 = 0x%04X (HI first -> 0x1234, LO first -> 0x12AA), reads=%d\n", read16(), reads);
    Uart u;
    std::printf("after ctor: %s\n", u.name());
    volatile uint16_t w = 0xFFFF;               // volatile: keep the multiply at run time
    std::printf("area = %u\n", area(w, static_cast<uint16_t>(w - argc + 1)));
    const float duty = 50.0F;
    log_plain("duty=%d%%\n", duty);
    log_checked("duty=%d%%\n", duty);
    return 0;
}
