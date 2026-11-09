// CPP-M02 demo_target.cpp — Field RMW vs 손으로 쓴 C vs volatile 비트필드 (Cortex-M 전용, 호스트에서 실행하지 않는다)
// #include 없음: 맥의 clang 은 bare-metal C++ 헤더가 없어서 필요한 것만 직접 쓴다. 실제 칩이라 volatile MMIO 다.
//   clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c++20 -O2 -fno-exceptions -fno-rtti -c demo_target.cpp -o /tmp/dt.o
//   xcrun llvm-objdump -d --no-show-raw-insn -C /tmp/dt.o
//   (Lima VM) arm-none-eabi-g++ -std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -c demo_target.cpp -o dt.o && arm-none-eabi-objdump -d --no-show-raw-insn -C dt.o
using u32 = unsigned int;

template <u32 Addr, u32 W1C = 0, u32 Res0 = 0>
struct Reg {
    using word = u32;
    static constexpr u32 w1c = W1C, res0 = Res0;
    static u32 read() { return *reinterpret_cast<volatile u32*>(Addr); }
    static void write(u32 v) { *reinterpret_cast<volatile u32*>(Addr) = v; }
};

template <typename F> struct FV { using field = F; u32 bits; };

template <typename First, typename... Rest>
void write(First a, Rest... b) {
    using R = typename First::field::reg;
    constexpr u32 mask = First::field::mask | (0u | ... | Rest::field::mask);
    constexpr u32 keep = ~(mask | R::w1c | R::res0);
    R::write((R::read() & keep) | a.bits | (0u | ... | b.bits));
}

template <typename R, unsigned O, unsigned W>
struct Field {
    using reg = R;
    static constexpr u32 max = W >= 32 ? ~0u : (1u << W) - 1u;
    static constexpr u32 mask = max << O;
    consteval FV<Field> operator=(u32 v) const {       // 이 demo 는 consteval 연산자로 축약 (본문은 Lit 생성자)
        if (v > max) __builtin_trap();
        return {v << O};
    }
    static constexpr FV<Field> wrap(u32 v) { return {(v & max) << O}; }
};

constexpr u32 ADDR = 0x4002'0000u;
using CSR = Reg<ADDR, 0x700u, 0xFFFF'F8F0u>;   // EN[0] DIR[1] PRIO[3:2] res[7:4] TC[8] HT[9] TE[10](W1C) res[31:11]
using EN_t = Field<CSR, 0, 1>;   inline constexpr EN_t EN{};
using PRIO_t = Field<CSR, 2, 2>; inline constexpr PRIO_t PRIO{};

// C 비트필드로 같은 레지스터를 선언 (AAPCS little-endian 이면 LSB 부터 채워진다 — 표준 보장은 아님)
struct CsrBits {
    volatile u32 en : 1; volatile u32 dir : 1; volatile u32 prio : 2; u32 : 4;
    volatile u32 tc : 1; volatile u32 ht : 1; volatile u32 te : 1; u32 : 21;
};
inline CsrBits& bf() { return *reinterpret_cast<CsrBits*>(ADDR); }

// 8비트 선언 비트필드: 접근 폭이 어떻게 되는지 보려고
struct CsrBytes { volatile unsigned char en : 1; };
inline CsrBytes& bf8() { return *reinterpret_cast<CsrBytes*>(ADDR); }

extern "C" {
// 1) Field: 상수 필드 2개 + RMW 1번
void field_two_const() { write(EN = 1u, PRIO = 2u); }
// 2) 손으로 쓴 C: 같은 의미로 W1C/예약 비트까지 지움
void c_two_const() {
    volatile u32& r = *reinterpret_cast<volatile u32*>(ADDR);
    r = (r & ~(0xDu | 0x700u | 0xFFFF'F8F0u)) | 0x1u | (2u << 2);
}
// 3) volatile 비트필드 대입 2번 → RMW 가 2번
void bf_two_const() { bf().en = 1; bf().prio = 2; }
// 4) 런타임 값 (wrap) 도 같은 코드인가
void field_runtime(u32 p) { write(PRIO_t::wrap(p)); }
void c_runtime(u32 p) {
    volatile u32& r = *reinterpret_cast<volatile u32*>(ADDR);
    r = (r & ~(0xCu | 0x700u | 0xFFFF'F8F0u)) | ((p & 0x3u) << 2);
}
// 5) 비트필드 RMW 는 W1C 를 어떻게 다루나: 읽은 값 그대로 되쓴다
void bf_en_only() { bf().en = 1; }
// 6) 선언 타입이 unsigned char 인 volatile 비트필드의 접근 폭
void bf8_en_only() { bf8().en = 1; }
}
