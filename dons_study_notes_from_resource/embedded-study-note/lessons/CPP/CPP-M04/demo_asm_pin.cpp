// CPP-M04 demo: Pin<Port,N> 를 Cortex-M4 로 컴파일해서 demo_asm_c.c 와 명령 수를 비교한다 (asm_compare.sh)
// 헤더 없이 컴파일 (맥 clang 에는 bare-metal C++ 헤더가 없다) — solution.cpp 의 Pin 과 같은 본문이다.
using u32 = unsigned int;

#ifdef FORCE_INLINE
#  define AI [[gnu::always_inline]] inline
#else
#  define AI
#endif

enum class Reg : u32 { MODER = 0x00, IDR = 0x10, ODR = 0x14, BSRR = 0x18 };

// 진짜 MMIO 포트. 주소는 정수 NTTP, 캐스트는 접근 시점에 (CPP-M01 참고)
template <u32 Base>
struct MmioPort {
    AI static u32 read(Reg r) { return *reinterpret_cast<volatile u32*>(Base + static_cast<u32>(r)); }
    AI static void write(Reg r, u32 v) { *reinterpret_cast<volatile u32*>(Base + static_cast<u32>(r)) = v; }
};

template <typename P, unsigned N>
    requires(N < 16)
struct Pin {
    static constexpr unsigned number = N;
    static P port_tag();                       // 선언만: decltype 용
    static constexpr u32 mask = 1u << N;
    static constexpr u32 reset_mask = mask << 16;
    AI static void set() { P::write(Reg::BSRR, mask); }
    AI static void clear() { P::write(Reg::BSRR, reset_mask); }
    AI static void write(bool high) { P::write(Reg::BSRR, high ? mask : reset_mask); }
    AI static bool read() { return (P::read(Reg::IDR) & mask) != 0; }
    AI static void toggle() { P::write(Reg::ODR, P::read(Reg::ODR) ^ mask); }
    AI static void toggle_bsrr() {
        const u32 odr = P::read(Reg::ODR);
        P::write(Reg::BSRR, ((odr & mask) << 16) | (~odr & mask));
    }
    template <typename Lock>
    AI static void toggle_critical() {
        Lock guard;
        toggle();
    }
};

template <typename First, typename... Rest>
struct PinBus {
    using port = decltype(First::port_tag());
    static constexpr u32 mask = (First::mask | ... | Rest::mask);
    AI static constexpr u32 scatter(u32 v) {
        u32 out = 0;
        unsigned i = 0;
        out |= ((v >> i) & 1u) << First::number;
        ++i;
        ((out |= ((v >> i) & 1u) << Rest::number, ++i), ...);
        return out;
    }
    AI static void write(u32 v) {
        const u32 on = scatter(v);
        port::write(Reg::BSRR, on | ((mask & ~on) << 16));
    }
};

// 실제 칩용 critical section: PRIMASK 저장 → cpsid i → 복원 (CPP-B06 RAII)
struct IrqLock {
    u32 primask;
    IrqLock() { asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(primask) : : "memory"); }
    ~IrqLock() { asm volatile("msr primask, %0" : : "r"(primask) : "memory"); }
};

using PortA = MmioPort<0x40020000u>;
using Led   = Pin<PortA, 5>;
using Pin15 = Pin<PortA, 15>;
using Nibble = PinBus<Pin<PortA, 0>, Pin<PortA, 3>, Pin<PortA, 7>, Pin<PortA, 12>>;  // 일부러 흩어진 핀

extern "C" {
void cpp_set()          { Led::set(); }
void cpp_clear()        { Led::clear(); }
void cpp_clear15()      { Pin15::clear(); }
void cpp_write(bool v)  { Led::write(v); }
bool cpp_read()         { return Led::read(); }
void cpp_toggle()       { Led::toggle(); }
void cpp_toggle_bsrr()  { Led::toggle_bsrr(); }
void cpp_toggle_crit()  { Led::toggle_critical<IrqLock>(); }
void cpp_pulse()        { Led::set(); Led::clear(); }
void cpp_two_pins()     { Led::set(); Pin<PortA, 6>::set(); }
void cpp_bus_const()    { Nibble::write(0b1010u); }
void cpp_bus_var(u32 v) { Nibble::write(v); }
}
