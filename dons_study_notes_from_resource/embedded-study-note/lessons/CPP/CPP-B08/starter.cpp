/* CPP-B08 클래스 레이아웃과 sizeof: 패딩·빈 클래스·EBO
 *
 * 기준 플랫폼: macOS arm64 (LP64 — 포인터 8바이트, Itanium 계열 C++ ABI).
 * Cortex-M(ILP32 — 포인터 4바이트)에선 vptr/포인터가 낀 값이 달라진다.
 * 그런 값은 sizeof(void*) 로 표현하면 두 타깃 모두에서 맞는다.
 *
 * 빌드: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
 */
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>          // std::launder
#include <type_traits>

/* 멤버 주소 - 객체 주소. offsetof 를 쓸 수 없는 타입(virtual 등)에도 쓰는 "측정기" */
template <class T, class M>
std::size_t addr_off(const T& obj, const M& member) {
    return reinterpret_cast<std::uintptr_t>(&member) - reinterpret_cast<std::uintptr_t>(&obj);
}

/* ================= Part A: sizeof / alignof / 오프셋 예측 ================= */
struct Empty {};
struct PadBad  { uint8_t a; uint32_t b; uint8_t c; };   // C 패딩 복습 (C-B12)
struct PadGood { uint32_t b; uint8_t a; uint8_t c; };
struct WithVirt { virtual void on_irq() {} uint32_t x; };
struct HasEmptyMember { Empty e; uint32_t x; };
struct EboDerived : Empty { uint32_t x; };
struct EboClash : Empty { Empty e; uint32_t x; };        // 베이스와 첫 멤버가 같은 타입
struct NuaMember { [[no_unique_address]] Empty e; uint32_t x; };

// TODO: 컴파일러에 묻기 전에 머리로 예측해 채운다 (포인터가 끼면 sizeof(void*) 로).
namespace pred {
constexpr std::size_t Empty_size           = 0;  // TODO
constexpr std::size_t PadBad_size          = 0;  // TODO
constexpr std::size_t PadGood_size         = 0;  // TODO
constexpr std::size_t WithVirt_size        = 0;  // TODO
constexpr std::size_t WithVirt_align       = 0;  // TODO
constexpr std::size_t WithVirt_x_off       = 0;  // TODO
constexpr std::size_t HasEmptyMember_size  = 0;  // TODO
constexpr std::size_t HasEmptyMember_x_off = 0;  // TODO
constexpr std::size_t EboDerived_size      = 0;  // TODO
constexpr std::size_t EboClash_size        = 0;  // TODO
constexpr std::size_t NuaMember_size       = 0;  // TODO
}  // namespace pred

// TODO: 예측이 맞으면 static_assert 로 "고정" — 레이아웃이 바뀌면 빌드가 깨지게.
//       (offsetof 는 standard-layout 타입에만!)




/* ================= Part B: 타입 특성 예측 + 꼬리 패딩 + 0바이트 정책 ================= */
struct UserCtor { UserCtor() : x(1) {} uint32_t x; };
struct UserCopy { UserCopy() = default; UserCopy(const UserCopy& o) : x(o.x) {} uint32_t x; };
class  MixedAccess { public: uint32_t a; uint32_t get_b() const { return b; } private: uint32_t b; };
struct Base4 { uint32_t a; };
struct Derived4 : Base4 { uint32_t b; };                 // 데이터 멤버가 베이스·파생 양쪽에
struct PodBase    { uint32_t a; uint8_t b; };
struct NonPodBase { NonPodBase() {} uint32_t a; uint8_t b; };
struct AfterPod    : PodBase    { uint8_t c; };
struct AfterNonPod : NonPodBase { uint8_t c; };

struct Traits {
    bool sl, tc, tr;   // standard_layout, trivially_copyable, trivial
    bool operator==(const Traits&) const = default;
};
template <class T> constexpr Traits traits_of() {
    return {std::is_standard_layout_v<T>, std::is_trivially_copyable_v<T>, std::is_trivial_v<T>};
}

namespace pred {
constexpr Traits WithVirt_t    = {};  // TODO {sl, tc, tr}
constexpr Traits UserCtor_t    = {};  // TODO
constexpr Traits UserCopy_t    = {};  // TODO
constexpr Traits MixedAccess_t = {};  // TODO
constexpr Traits Derived4_t    = {};  // TODO
constexpr Traits EboClash_t    = {};  // TODO
constexpr std::size_t AfterPod_size    = 0;  // TODO
constexpr std::size_t AfterNonPod_size = 0;  // TODO (꼬리 패딩!)
}  // namespace pred

struct NoLock  { void lock() {} void unlock() {} };          // ISR 공유 없는 빌드: 아무것도 안 함
struct IrqLock {                                              // mock PRIMASK 저장/복원 (상태 있음)
    uint32_t saved = 0;
    void lock() { saved = 1; }
    void unlock() { saved = 0; }
};

template <class Lock>
class EventCounter {
public:
    void hit() { lock_.lock(); ++count_; lock_.unlock(); }
    uint32_t count() const { return count_; }
private:
    Lock lock_;            // TODO: Lock 이 빈 타입이면 0바이트를 차지하게 (C++20)
    uint32_t count_ = 0;
};

/* ================= Part C: MMIO 에 겹쳐 쓰는 레지스터 블록 + DMA 디스크립터 ================= */
// 데이터시트 (가상의 UART, STM32 USART 스타일):
//   0x00 CR        RW   bit0 UE(enable), bit3 TE(tx enable)
//   0x04 SR        RO   bit7 TXE(tx data register empty)
//   0x08 ~ 0x0F         reserved
//   0x10 BRR       RW   baud divisor = round(pclk / baud)
//   0x14 DR        RW   data (하위 8비트)
//   0x18 FIFO_LVL  RO   16비트
//   0x1A FIFO_THR  RW   16비트           블록 크기 = 0x1C
constexpr uint32_t CR_UE = 1u << 0, CR_TE = 1u << 3, SR_TXE = 1u << 7;

// TODO: 이 클래스는 MMIO 위에 겹쳐(overlay) 써야 한다 → 레이아웃을 깨는 것을 모두 고치고 메서드를 채워라.
class UartRegs {
public:
    volatile uint32_t CR;
    volatile uint32_t SR;
    volatile uint32_t BRR;
    volatile uint32_t DR;
    volatile uint16_t FIFO_LVL;
    volatile uint16_t FIFO_THR;

    UartRegs() = default;                            // 하드웨어가 "만든다" — 초기화 코드 없음
    UartRegs(const UartRegs&) = delete;              // 레지스터 블록 복사는 의미 없음
    UartRegs& operator=(const UartRegs&) = delete;

    virtual void dump() const {}                     // 누군가 디버그용으로 추가함

    void enable_tx() { /* TODO: UE | TE 를 켠다 (다른 비트 보존) */ }
    void set_baud(uint32_t pclk, uint32_t baud) { (void)pclk; (void)baud; /* TODO: 반올림 나눗셈 */ }
    bool tx_ready() const { return false; /* TODO */ }
    void write(uint8_t b) { (void)b; /* TODO */ }
    uint16_t fifo_level() const { return 0; /* TODO */ }
};
// TODO: 레이아웃 static_assert (standard-layout, offsetof, sizeof)



// 모의 주소 공간. 실제 칩이라면 reinterpret_cast<UartRegs*>(0x4001'1000)
alignas(8) static unsigned char mmio[0x40];
static UartRegs* uart1() { return std::launder(reinterpret_cast<UartRegs*>(mmio)); }

// "하드웨어" 쪽 시점: 오프셋으로 바이트를 읽고 쓴다 (리틀 엔디언 — Cortex-M 과 같음)
static void hw_write32(std::size_t off, uint32_t v) {
    volatile unsigned char* p = mmio + off;
    for (int i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(v >> (8 * i));
}
static void hw_write16(std::size_t off, uint16_t v) {
    volatile unsigned char* p = mmio + off;
    for (int i = 0; i < 2; ++i) p[i] = static_cast<unsigned char>(v >> (8 * i));
}
static uint32_t le32(const volatile unsigned char* p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
    return v;
}
static uint32_t hw_read32(std::size_t off) { return le32(mmio + off); }

// DMA 디스크립터: CPU 가 memcpy 로 디스크립터 RAM 에 넣고, DMA 엔진이 바이트로 읽는다.
struct DmaDesc { uint32_t src, dst, len, ctrl; };
struct BadDesc { virtual ~BadDesc() = default; uint32_t src, dst, len, ctrl; };

template <class T>
concept DmaSafe = true;   // TODO: memcpy 로 하드웨어에 넘겨도 되는 타입의 조건

alignas(8) static unsigned char desc_ram[64];
template <DmaSafe T>
void post_desc(std::size_t slot, const T& d) {
    (void)slot; (void)d;  // TODO: slot 번째 칸(slot * sizeof(T))에 바이트 그대로 복사
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main() {
    /* Part A */
    CHECK(sizeof(Empty) == pred::Empty_size);
    CHECK(sizeof(PadBad) == pred::PadBad_size);
    CHECK(sizeof(PadGood) == pred::PadGood_size);
    CHECK(sizeof(WithVirt) == pred::WithVirt_size);
    CHECK(alignof(WithVirt) == pred::WithVirt_align);
    WithVirt wv;
    CHECK(addr_off(wv, wv.x) == pred::WithVirt_x_off);
    HasEmptyMember he{};
    CHECK(sizeof(HasEmptyMember) == pred::HasEmptyMember_size);
    CHECK(addr_off(he, he.x) == pred::HasEmptyMember_x_off);
    CHECK(sizeof(EboDerived) == pred::EboDerived_size);
    CHECK(sizeof(EboClash) == pred::EboClash_size);
    CHECK(sizeof(NuaMember) == pred::NuaMember_size);

    /* Part B */
    CHECK(traits_of<WithVirt>() == pred::WithVirt_t);
    CHECK(traits_of<UserCtor>() == pred::UserCtor_t);
    CHECK(traits_of<UserCopy>() == pred::UserCopy_t);
    CHECK(traits_of<MixedAccess>() == pred::MixedAccess_t);
    CHECK(traits_of<Derived4>() == pred::Derived4_t);
    CHECK(traits_of<EboClash>() == pred::EboClash_t);
    CHECK(sizeof(AfterPod) == pred::AfterPod_size);
    CHECK(sizeof(AfterNonPod) == pred::AfterNonPod_size);
    EventCounter<NoLock> c1;
    c1.hit(); c1.hit();
    CHECK(c1.count() == 2);
    CHECK(sizeof(EventCounter<NoLock>) == sizeof(uint32_t));
    CHECK(sizeof(EventCounter<IrqLock>) == 2 * sizeof(uint32_t));

    /* Part C — 레이아웃 */
    UartRegs* u = uart1();
    CHECK(std::is_standard_layout_v<UartRegs>);
    CHECK(sizeof(UartRegs) == 0x1C);
    CHECK(addr_off(*u, u->CR) == 0x00);
    CHECK(addr_off(*u, u->SR) == 0x04);
    CHECK(addr_off(*u, u->BRR) == 0x10);
    CHECK(addr_off(*u, u->DR) == 0x14);
    CHECK(addr_off(*u, u->FIFO_LVL) == 0x18);
    CHECK(addr_off(*u, u->FIFO_THR) == 0x1A);
    /* Part C — 동작 (하드웨어 쪽은 오프셋으로만 본다) */
    u->enable_tx();
    CHECK(hw_read32(0x00) == (CR_UE | CR_TE));
    u->set_baud(16'000'000, 115'200);
    CHECK(hw_read32(0x10) == 139);                 // 138.89 → 139
    hw_write32(0x04, 0);
    CHECK(!u->tx_ready());
    hw_write32(0x04, SR_TXE);
    CHECK(u->tx_ready());
    u->write('A');
    CHECK(hw_read32(0x14) == 'A');
    hw_write16(0x18, 3);
    CHECK(u->fifo_level() == 3);
    /* Part C — DMA 디스크립터 */
    CHECK(DmaSafe<DmaDesc>);
    CHECK(!DmaSafe<BadDesc>);
    CHECK(!DmaSafe<UserCopy>);
    post_desc(1, DmaDesc{0x2000'0000u, 0x4001'1014u, 64u, 1u});
    CHECK(le32(desc_ram + 16 + 4) == 0x4001'1014u);
    CHECK(le32(desc_ram + 16 + 8) == 64u);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
