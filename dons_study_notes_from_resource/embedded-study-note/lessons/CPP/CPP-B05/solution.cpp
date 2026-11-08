// CPP-B05 const·volatile·참조로 레지스터 접근 모델링
// 읽기 전용(RO) / 읽기·쓰기(RW) / 쓰기 전용(WO) 레지스터를 "타입"으로 구분한다.
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti <file>.cpp && ./a.out
#include <cstdint>
#include <cstdio>
#include <type_traits>

// ---------------- mock 하드웨어: USART 레지스터 블록 ----------------
// STM32G0/L4 USART 를 단순화한 레이아웃. 실제 칩이라면 base = 0x40004400 같은 고정 주소.
struct UsartBlock {
    std::uint32_t CR1;  // 0x00 RW  제어 (UE/RE/TE)
    std::uint32_t ISR;  // 0x04 RO  상태 (하드웨어만 바꾼다)
    std::uint32_t ICR;  // 0x08 WO  플래그 지우기 (1을 쓴 비트만 ISR 에서 지워짐)
    std::uint32_t RDR;  // 0x0C RO  수신 데이터
    std::uint32_t TDR;  // 0x10 RW  송신 데이터
};
static UsartBlock g_usart_mem;  // "하드웨어" 메모리 (테스트 코드가 하드웨어 역할)

namespace off {
constexpr std::uintptr_t CR1 = 0x00, ISR = 0x04, ICR = 0x08, RDR = 0x0C, TDR = 0x10;
}
namespace bit {
constexpr std::uint32_t CR1_UE = 1u << 0, CR1_RE = 1u << 2, CR1_TE = 1u << 3;
constexpr std::uint32_t ISR_ORE = 1u << 3, ISR_RXNE = 1u << 5, ISR_TC = 1u << 6, ISR_TXE = 1u << 7;
constexpr std::uint32_t ICR_ORECF = 1u << 3, ICR_TCCF = 1u << 6;
}  // namespace bit

// 하드웨어가 ICR 쓰기를 ISR 에 반영하는 동작 흉내 (테스트용)
static void sim_apply_icr() {
    g_usart_mem.ISR &= ~g_usart_mem.ICR;
    g_usart_mem.ICR = 0;
}

// ---------------- Part A: 주소 → 참조 접근자 ----------------
[[maybe_unused]] static volatile std::uint32_t g_dummy_reg;  // starter 스텁이 돌려줄 자리
using RoReg = volatile const std::uint32_t&;  // TODO(A1): 읽기 전용 레지스터 참조 타입
using RwReg = volatile std::uint32_t&;        // 읽기·쓰기 레지스터 참조 타입

inline RoReg reg_ro(std::uintptr_t base, std::uintptr_t offset) {
    return *reinterpret_cast<volatile const std::uint32_t*>(base + offset);  // TODO(A2)
}
inline RwReg reg_rw(std::uintptr_t base, std::uintptr_t offset) {
    return *reinterpret_cast<volatile std::uint32_t*>(base + offset);  // TODO(A3)
}

// 컴파일타임 판정기: requires-expression 은 "템플릿 안에서" 써야 false 로 떨어진다
template <class R> concept WritableReg = requires(R r) { r = 1u; };
template <class R> concept ReadableReg = requires(R r) { static_cast<std::uint32_t>(r); };

// ---------------- Part B: 드라이버 핸들 + 쓰기 전용 프록시 ----------------
// 참조 타입만으로는 "쓰기 전용"을 표현할 수 없다 → 쓰기만 있는 작은 프록시
class WoReg {
public:
    explicit WoReg(volatile std::uint32_t& r) : r_(r) {}
    void operator=(std::uint32_t v) const { r_ = v; }  // TODO(B2): 쓰기만 허용
    // operator std::uint32_t() 는 일부러 없다 → 읽으려 하면 컴파일 에러
private:
    volatile std::uint32_t& r_;
};

class Usart {
public:
    explicit constexpr Usart(std::uintptr_t base) : base_(base) {}
    RoReg isr() const { return reg_ro(base_, off::ISR); }   // TODO(B1): const 핸들에서도 읽기 가능
    RoReg rdr() const { return reg_ro(base_, off::RDR); }   // TODO(B1)
    RwReg cr1() { return reg_rw(base_, off::CR1); }         // TODO(B1): const 핸들로는 쓰기 불가
    RwReg tdr() { return reg_rw(base_, off::TDR); }
    WoReg icr() { return WoReg{reg_rw(base_, off::ICR)}; }
private:
    std::uintptr_t base_;
};

template <class U> concept IsrReadableFromConst = requires(const U& u) { static_cast<std::uint32_t>(u.isr()); };
template <class U> concept IsrWritable          = requires(U& u) { u.isr() = 1u; };
template <class U> concept Cr1WritableFromConst = requires(const U& u) { u.cr1() = 1u; };
template <class U> concept IcrWritable          = requires(U& u) { u.icr() = 1u; };
template <class U> concept IcrReadable          = requires(U& u) { static_cast<std::uint32_t>(u.icr()); };

// ---------------- Part C: 폴링 USART 드라이버 ----------------
// CR1 에 UE|RE|TE 를 켠다. 다른 비트는 보존.
void usart_enable(Usart& u) {
    u.cr1() |= bit::CR1_UE | bit::CR1_RE | bit::CR1_TE;  // TODO(C1)
}
// ISR 의 mask 비트 중 하나라도 1이 될 때까지 최대 max_spins 번 읽는다. 시간 초과면 false.
bool usart_wait(const Usart& u, std::uint32_t mask, std::uint32_t max_spins) {
    for (std::uint32_t n = 0; n < max_spins; ++n) {       // TODO(C2)
        if (u.isr() & mask) return true;                   // 매 반복마다 실제 load
    }
    return false;
}
// TXE 를 기다렸다가 TDR 에 쓴다. 시간 초과면 아무것도 쓰지 않고 false.
bool usart_put(Usart& u, std::uint8_t b, std::uint32_t max_spins) {
    if (!usart_wait(u, bit::ISR_TXE, max_spins)) return false;  // TODO(C3)
    u.tdr() = b;
    return true;
}
// ISR 을 "한 번만" 읽어 판단. ORE 면 ICR 로 지우고(= 로 쓰기), RXNE 면 RDR 하위 8비트, 없으면 -1.
int usart_get(Usart& u) {
    const std::uint32_t isr = u.isr();                // TODO(C4): 스냅샷 1회
    if (isr & bit::ISR_ORE) u.icr() = bit::ICR_ORECF;
    if (!(isr & bit::ISR_RXNE)) return -1;
    return static_cast<int>(u.rdr() & 0xFFu);
}

// ---------------- 테스트 하네스 ----------------
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main() {
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(&g_usart_mem);

    // Part A — 타입 판정 (값은 컴파일타임에 정해지고, 결과만 런타임에 출력)
    CHECK(!WritableReg<RoReg>);
    CHECK(ReadableReg<RoReg>);
    CHECK(WritableReg<RwReg>);
    CHECK((std::is_same_v<RoReg, const volatile std::uint32_t&>));
    // Part A — 접근자가 mock 메모리의 바로 그 자리를 가리키는가
    g_usart_mem.ISR = 0xC0u;
    CHECK(reg_ro(base, off::ISR) == 0xC0u);
    CHECK(&reg_rw(base, off::TDR) == &g_usart_mem.TDR);
    reg_rw(base, off::TDR) = 0x55u;
    CHECK(g_usart_mem.TDR == 0x55u);
    RoReg live = reg_ro(base, off::ISR);   // 별칭(alias): 하드웨어 변화가 보여야 한다
    g_usart_mem.ISR = bit::ISR_TXE;
    CHECK(live == bit::ISR_TXE);

    // Part B — 핸들의 const 정책 + 쓰기 전용 프록시
    Usart u{base};
    CHECK(IsrReadableFromConst<Usart>);
    CHECK(!IsrWritable<Usart>);
    CHECK(!Cr1WritableFromConst<Usart>);
    CHECK(IcrWritable<Usart> && !IcrReadable<Usart>);
    g_usart_mem.ICR = 0;
    u.icr() = bit::ICR_TCCF;
    CHECK(g_usart_mem.ICR == bit::ICR_TCCF);
    // auto 는 참조·cv 를 버린다 → 스냅샷. decltype(auto) 는 그대로 별칭.
    g_usart_mem.ISR = 0;
    auto snap = u.isr();
    decltype(auto) alias = u.isr();
    g_usart_mem.ISR = bit::ISR_RXNE;
    CHECK((std::is_same_v<decltype(snap), std::uint32_t>));
    CHECK(snap == 0u && alias == bit::ISR_RXNE);

    // Part C — 드라이버
    g_usart_mem = UsartBlock{};
    g_usart_mem.CR1 = 1u << 15;                      // 이미 켜져 있던 다른 비트 (OVER8)
    usart_enable(u);
    CHECK(g_usart_mem.CR1 == ((1u << 15) | bit::CR1_UE | bit::CR1_RE | bit::CR1_TE));
    const Usart& cu = u;
    g_usart_mem.ISR = bit::ISR_TC;
    CHECK(usart_wait(cu, bit::ISR_TC, 5));
    g_usart_mem.ISR = 0;
    CHECK(!usart_wait(cu, bit::ISR_TC, 5));
    g_usart_mem.ISR = bit::ISR_TXE;
    CHECK(usart_put(u, 'A', 10) && g_usart_mem.TDR == 'A');
    g_usart_mem.ISR = 0;
    g_usart_mem.TDR = 0;
    CHECK(!usart_put(u, 'B', 10) && g_usart_mem.TDR == 0u);   // 시간 초과 → 쓰지 않음
    g_usart_mem.ISR = bit::ISR_RXNE;
    g_usart_mem.RDR = 0x141u;                         // 9비트 모드 잔재: 하위 8비트만
    CHECK(usart_get(u) == 0x41);
    g_usart_mem.ISR = 0;
    CHECK(usart_get(u) == -1);
    g_usart_mem.ISR = bit::ISR_RXNE | bit::ISR_ORE;
    g_usart_mem.RDR = 0x42u;
    CHECK(usart_get(u) == 0x42 && g_usart_mem.ICR == bit::ICR_ORECF);
    sim_apply_icr();
    CHECK(g_usart_mem.ISR == bit::ISR_RXNE);         // ORE 만 지워지고 RXNE 는 남아야 한다

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}

// TODO(A4): 위 TODO 를 다 채웠다면 아래 주석을 풀어 "컴파일타임 증명"으로 승격하라.
static_assert(!WritableReg<RoReg> && WritableReg<RwReg>, "RO 는 쓰기 불가, RW 는 쓰기 가능");
static_assert(!IsrWritable<Usart> && !Cr1WritableFromConst<Usart> && !IcrReadable<Usart>);
