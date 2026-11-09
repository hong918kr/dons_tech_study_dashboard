// CPP-M05 demo_bloat.cpp — 템플릿 코드 크기: "타입 수"가 늘면 .text 가 늘어나는가 (bloat.sh 가 빌드/측정)
// -DMODE=0 CRTP, 포트마다 다른 타입 (Stm32Uart<Base>)        → 공통 코드도 포트 수만큼 복제
// -DMODE=1 CRTP, 타입 1개 + 런타임 base 멤버                  → 복제 없음
// -DMODE=2 CRTP 타입 N개 + 공통 코드를 비-템플릿으로 끌어올림 → 얇은 트램펄린만 복제
// -DMODE=3 virtual 인터페이스 + 구현 1개 (런타임 base)        → 복제 없음
// -DN_INST=1..8   -DFRAME_NOINLINE: write_frame 을 out-of-line 으로 강제 (컴파일러의 인라인 판단을 제거)
using u8 = unsigned char; using u32 = unsigned int; using uptr = unsigned int;
struct UsartRegs { volatile u32 SR, DR, BRR, CR1; };
constexpr u32 SR_TXE = 1u << 7;
constexpr uptr PORT0 = 0x40004400u, PORT_STEP = 0x400u;
inline UsartRegs& regs(uptr a) { return *reinterpret_cast<UsartRegs*>(a); }
inline bool tx(uptr base, u8 b) {
    auto& r = regs(base);
    for (u32 i = 0; i < 1000; ++i) { if (r.SR & SR_TXE) { r.DR = b; return true; } }
    return false;
}
const u8 g_payload[16] = {1, 2, 3, 2, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
volatile u32 g_len = 16;
inline u8 crc8(u8 c, u8 b) {
    c = static_cast<u8>(c ^ b);
    for (int i = 0; i < 8; ++i) c = static_cast<u8>((c & 0x80) ? (c << 1) ^ 7 : c << 1);
    return c;
}
enum : u8 { STX = 0x02, ESC = 0x10 };
#ifdef FRAME_NOINLINE
#define FRAME_ATTR __attribute__((noinline))
#else
#define FRAME_ATTR
#endif

// 프레임 본체를 "1바이트 송신 함수"에 대해 일반화한 공통 조각 (MODE 0/1/3 은 복사해서 씀)
#define FRAME_BODY(PUTB)                                                \
    if (n > 255) return false;                                          \
    if (!(PUTB)(STX)) return false;                                     \
    u8 crc = crc8(0, static_cast<u8>(n));                               \
    if (!(PUTB)(static_cast<u8>(n))) return false;                      \
    for (u32 i = 0; i < n; ++i) {                                       \
        u8 b = p[i];                                                    \
        crc = crc8(crc, b);                                             \
        if (b == STX || b == ESC) {                                     \
            if (!(PUTB)(ESC)) return false;                             \
            b = static_cast<u8>(b ^ 0x20);                              \
        }                                                               \
        if (!(PUTB)(b)) return false;                                   \
    }                                                                   \
    return (PUTB)(crc);

template <class D>
class UartBase {
public:
    bool put(u8 b) { return static_cast<D&>(*this).put_impl(b); }
    FRAME_ATTR bool write_frame(const u8* p, u32 n) {
        auto put1 = [this](u8 b) { return put(b); };
        FRAME_BODY(put1)
    }
protected:
    UartBase() = default;
};

#if MODE == 0
template <uptr Base>
class Uart : public UartBase<Uart<Base>> {
    friend UartBase<Uart<Base>>;
    bool put_impl(u8 b) { return tx(Base, b); }
};
template <int I> __attribute__((noinline)) void drive() {
    Uart<PORT0 + PORT_STEP * I> u;
    u.write_frame(g_payload, g_len);
}
#elif MODE == 1
class Uart : public UartBase<Uart> {
    friend UartBase<Uart>;
    const uptr base_;
    bool put_impl(u8 b) { return tx(base_, b); }
public:
    explicit constexpr Uart(uptr b) : base_(b) {}
};
template <int I> __attribute__((noinline)) void drive() {
    Uart u(PORT0 + PORT_STEP * I);
    u.write_frame(g_payload, g_len);
}
#elif MODE == 2
using PutFn = bool (*)(void*, u8);
__attribute__((noinline)) inline bool frame_core(void* ctx, PutFn f, const u8* p, u32 n) {
    auto put1 = [&](u8 b) { return f(ctx, b); };
    FRAME_BODY(put1)
}
template <uptr Base>
class Uart : public UartBase<Uart<Base>> {
    friend UartBase<Uart<Base>>;
    bool put_impl(u8 b) { return tx(Base, b); }
public:
    bool write_frame_thin(const u8* p, u32 n) {
        return frame_core(this, [](void* s, u8 b) { return static_cast<Uart*>(s)->put(b); }, p, n);
    }
};
template <int I> __attribute__((noinline)) void drive() {
    Uart<PORT0 + PORT_STEP * I> u;
    u.write_frame_thin(g_payload, g_len);
}
#elif MODE == 3
class IUart {
public:
    virtual bool put(u8 b) = 0;
    FRAME_ATTR bool write_frame(const u8* p, u32 n) {
        auto put1 = [this](u8 b) { return put(b); };
        FRAME_BODY(put1)
    }
protected:
    ~IUart() = default;
};
class UartV final : public IUart {
    const uptr base_;
public:
    explicit constexpr UartV(uptr b) : base_(b) {}
    bool put(u8 b) override { return tx(base_, b); }
};
__attribute__((noinline)) void via_iface(IUart& u) { u.write_frame(g_payload, g_len); }
template <int I> __attribute__((noinline)) void drive() {
    UartV u(PORT0 + PORT_STEP * I);
    via_iface(u);
}
#endif

template <int I> void drive_all() {
    drive<I>();
    if constexpr (I > 0) drive_all<I - 1>();
}
extern "C" void _start() {
    drive_all<N_INST - 1>();
    for (;;) {}
}
