// CPP-M14 demo: Cortex-M4 오브젝트로 "검증은 공짜인가?" 확인 (헤더 없음 — 맥 clang 만으로 thumbv7em 컴파일)
//   clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c++20 -O2 -fno-exceptions -fno-rtti -c demo_target.cpp -o /tmp/t.o
//   clang++ ... -DWITH_RUNTIME -c demo_target.cpp -o /tmp/rt.o   ← 런타임 표 탐색판 추가
//   clang++ ... -DWITH_BOARD -c demo_target.cpp -o /tmp/bd.o     ← 보드 표 펼치기판 추가
//   xcrun llvm-objdump -d --no-show-raw-insn /tmp/t.o ; xcrun llvm-size -A /tmp/t.o ; xcrun llvm-nm -C --size-sort /tmp/t.o
using u8 = unsigned char;
using u32 = unsigned int;

enum class Port : u8 { A, B, C };
template <Port P, unsigned N> struct Pin { static constexpr Port port = P; static constexpr unsigned number = N; };
enum class Signal : u8 { USART1_TX, USART1_RX, I2C1_SCL };
struct AfRow { Port port; u8 pin; Signal signal; u8 af; };
inline constexpr AfRow AF_TABLE[] = {
    {Port::A,  9, Signal::USART1_TX, 7},
    {Port::A, 10, Signal::USART1_RX, 7},
    {Port::B,  6, Signal::USART1_TX, 7},
    {Port::B,  6, Signal::I2C1_SCL,  4},
    {Port::B,  8, Signal::I2C1_SCL,  4},
};
constexpr int find_af(Port p, unsigned n, Signal s) {
    for (const AfRow& r : AF_TABLE) if (r.port == p && r.pin == n && r.signal == s) return r.af;
    return -1;
}

// STM32F4 주소 — RCC 0x40023800, AHB1ENR +0x30, GPIOA 0x40020000 (포트당 0x400), MODER +0, OTYPER +4, AFRL +0x20, AFRH +0x24
constexpr u32 gpio_base(Port p) { return 0x4002'0000u + 0x400u * static_cast<u32>(p); }
inline volatile u32& reg(u32 addr) { return *reinterpret_cast<volatile u32*>(addr); }
inline void modify(u32 addr, u32 mask, u32 bits) { reg(addr) = (reg(addr) & ~mask) | bits; }
inline void rcc_gpio_enable(Port p) { reg(0x4002'3830u) |= 1u << static_cast<u32>(p); }

template <Port Pt, unsigned N, unsigned Af, u32 Otype>
inline void apply_pin() {
    const u32 base = gpio_base(Pt);
    rcc_gpio_enable(Pt);
    modify(base + (N < 8 ? 0x20u : 0x24u), 0xFu << ((N % 8) * 4), u32{Af} << ((N % 8) * 4));
    modify(base + 0x04u, 1u << N, Otype << N);
    modify(base + 0x00u, 3u << (N * 2), 2u << (N * 2));
}

template <typename P, Signal S>
void configure() {
    constexpr int af = find_af(P::port, P::number, S);
    static_assert(af >= 0, "configure<P, S>: signal S is not routed to pin P on this chip");
    if constexpr (af >= 0) apply_pin<P::port, P::number, static_cast<unsigned>(af), 0u>();
}

// (1) HAL: 컴파일 타임에 검증된 호출
extern "C" void cfg_hal() { configure<Pin<Port::A, 9>, Signal::USART1_TX>(); }

// (2) 손으로 쓴 C 스타일
extern "C" void cfg_hand() {
    reg(0x4002'3830u) |= 1u;
    reg(0x4002'0024u) = (reg(0x4002'0024u) & ~(0xFu << 4)) | (7u << 4);
    reg(0x4002'0004u) = (reg(0x4002'0004u) & ~(1u << 9));
    reg(0x4002'0000u) = (reg(0x4002'0000u) & ~(3u << 18)) | (2u << 18);
}

#ifdef WITH_RUNTIME
// (3) 런타임 표 탐색판: 잘못된 조합도 컴파일되고, 실행 중에 -1 을 돌려준다. 표가 플래시에 남는다.
extern "C" int cfg_runtime(Port p, unsigned n, Signal s) {
    const int af = find_af(p, n, s);
    if (af < 0) return -1;
    const u32 base = gpio_base(p);
    rcc_gpio_enable(p);
    modify(base + (n < 8 ? 0x20u : 0x24u), 0xFu << ((n % 8) * 4), static_cast<u32>(af) << ((n % 8) * 4));
    modify(base + 0x04u, 1u << n, 0u);
    modify(base + 0x00u, 3u << (n * 2), 2u << (n * 2));
    return 0;
}
#endif

#ifdef WITH_BOARD
// (4) 보드 표를 NTTP 행으로 펼치기: 표(BOARD)도 AF_TABLE 도 런타임 데이터로 남지 않아야 한다
struct PinAssign { Port port; u8 pin; Signal signal; bool open_drain; };
template <PinAssign R> inline void apply_row() {
    apply_pin<R.port, R.pin, static_cast<unsigned>(find_af(R.port, R.pin, R.signal)), R.open_drain ? 1u : 0u>();
}
template <unsigned...> struct Seq {};
template <const auto& B, unsigned... I> inline void apply_all(Seq<I...>) { (apply_row<B[I]>(), ...); }
static constexpr PinAssign BOARD[] = {
    {Port::A,  9, Signal::USART1_TX, false},
    {Port::A, 10, Signal::USART1_RX, false},
    {Port::B,  6, Signal::I2C1_SCL,  true},
};
extern "C" void cfg_board() { apply_all<BOARD>(Seq<0, 1, 2>{}); }
#endif
