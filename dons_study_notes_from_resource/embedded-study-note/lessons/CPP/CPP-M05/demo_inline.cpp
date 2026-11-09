// CPP-M05 demo_inline.cpp — CRTP vs virtual: 호출이 인라인되는가 (Cortex-M4 asm 비교, 맥에서 헤더 없이)
//   clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c++20 -O2 -fno-exceptions -fno-rtti -S demo_inline.cpp -o -
//   VM: arm-none-eabi-g++ -std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -S demo_inline.cpp -o -
using u8 = unsigned char; using u32 = unsigned int; using uptr = unsigned int;
struct UsartRegs { volatile u32 SR, DR, BRR, CR1; };
constexpr u32 SR_TXE = 1u << 7;
constexpr uptr USART2 = 0x40004400u;
inline UsartRegs& regs(uptr a) { return *reinterpret_cast<UsartRegs*>(a); }
inline void tx(uptr base, u8 b) { auto& r = regs(base); while (!(r.SR & SR_TXE)) {} r.DR = b; }

// ---- CRTP ----
template <class D>
class UartBase {
public:
    bool put(u8 b) { return static_cast<D&>(*this).put_impl(b); }
    void write(const u8* p, u32 n) { for (u32 i = 0; i < n; ++i) put(p[i]); }
protected:
    UartBase() = default;
};
template <uptr Base>
class Stm32Uart : public UartBase<Stm32Uart<Base>> {
    friend UartBase<Stm32Uart<Base>>;
    bool put_impl(u8 b) { tx(Base, b); return true; }
};
using Uart2 = Stm32Uart<USART2>;

// ---- 같은 API 의 virtual 버전 ----
class IUart {
public:
    virtual bool put(u8 b) = 0;
    void write(const u8* p, u32 n) { for (u32 i = 0; i < n; ++i) put(p[i]); }
protected:
    ~IUart() = default;
};
template <uptr Base>
class Stm32UartV final : public IUart {
public:
    bool put(u8 b) override { tx(Base, b); return true; }
};

bool crtp_put(Uart2& u, u8 b)                   { return u.put(b); }
bool virt_put(IUart& u, u8 b)                   { return u.put(b); }   // 구체 타입을 모른다
bool virt_put_known(u8 b) { Stm32UartV<USART2> u; IUart& r = u; return r.put(b); }  // 구체 타입이 보인다
void crtp_write(Uart2& u, const u8* p, u32 n)   { u.write(p, n); }
void virt_write(IUart& u, const u8* p, u32 n)   { u.write(p, n); }
