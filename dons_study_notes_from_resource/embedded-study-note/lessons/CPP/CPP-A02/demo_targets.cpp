// CPP-A02 demo_targets.cpp — 타깃(Cortex-M4) 쪽 코드: 두 디스패치 방식 + 벡터 테이블 + weak alias
//   bash asm_targets.sh asm       clang thumbv7em -S (지시어 제거)
//   bash asm_targets.sh sections  .o 의 섹션/심볼 (llvm-objdump -h -t)
//   bash asm_targets.sh vm        Lima VM 에서 arm-none-eabi-g++ 로 링크 → 벡터 테이블 바이트, weak 덮어쓰기
// -DBAD_SP_CAST : 초기 SP 를 reinterpret_cast 로 넣은 판 (상수 초기화가 아니게 됨 → 무슨 일이?)
#include <cstdint>

constexpr unsigned kNumIrq = 96;               // 이 레슨의 모델: 외부 IRQ 96개 (칩마다 다름)
extern "C" { typedef void isr_t(void); }        // C 언어 링크 함수 타입 [dcl.link]

/* ---------- 드라이버 (on_irq 는 다른 TU 에 있다고 가정: noinline) ---------- */
struct UartRegs { volatile uint32_t SR; volatile uint32_t DR; };
class Uart {
public:
    constexpr explicit Uart(uintptr_t base) : base_(base) {}
    [[gnu::noinline]] void on_irq() {
        auto* r = reinterpret_cast<UartRegs*>(base_);
        if (r->SR & (1u << 5)) { rx_sum += r->DR & 0xFFu; ++rx; }   // RXNE (bit 5, STM32 USART 대표값)
    }
    uint32_t rx = 0, rx_sum = 0;
private:
    uintptr_t base_;
};
Uart g_uart1{0x40011000u}, g_uart2{0x40004400u}, g_uart3{0x40004800u}, g_uart5{0x40005000u};   // constexpr ctor → .data, init 코드 없음

/* ---------- (1) 런타임 등록 테이블: IRQ 번호 → {thunk, obj} ---------- */
using Thunk = void (*)(void*);
struct Binding { Thunk fn; void* obj; };
Binding g_slots[kNumIrq];                       // RAM (.bss) — 실행 중 attach
volatile uint32_t g_unhandled;

template <class T, void (T::*M)()> void member_thunk(void* p) { (static_cast<T*>(p)->*M)(); }

static inline uint32_t read_ipsr() { uint32_t r; __asm volatile("mrs %0, ipsr" : "=r"(r)); return r; }

extern "C" void Default_Handler(void) { g_unhandled = g_unhandled + 1; }

extern "C" void irq_trampoline(void) {          // 모든 외부 IRQ 벡터가 이 함수 하나를 가리킨다
    const uint32_t n = read_ipsr() - 16u;
    if (n < kNumIrq) {
        const Binding b = g_slots[n];
        if (b.fn != nullptr) { b.fn(b.obj); return; }
    }
    Default_Handler();
}
void attach_uart5() { g_slots[53] = Binding{&member_thunk<Uart, &Uart::on_irq>, &g_uart5}; }   // 실행 중 등록

/* ---------- (2) 컴파일 타임 바인딩: Trampoline<IRQ, 인스턴스, 멤버> ---------- */
template <unsigned N, auto& Inst, auto M>
    requires (N < kNumIrq)
struct Trampoline {
    static void isr() { (Inst.*M)(); }          // C++ 링크의 정적 멤버 함수 (템플릿은 C 링크 불가 [temp.pre])
};
using U1 = Trampoline<37, g_uart1, &Uart::on_irq>;
using U2 = Trampoline<38, g_uart2, &Uart::on_irq>;
using U3 = Trampoline<39, g_uart3, &Uart::on_irq>;

/* ---------- (3) 벡터 테이블: .isr_vector, weak alias ---------- */
extern "C" void Reset_Handler(void) { attach_uart5(); for (;;) {} }
extern "C" void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
extern "C" void SysTick_Handler(void)   __attribute__((weak, alias("Default_Handler")));
extern "C" void UART4_IRQHandler(void)  __attribute__((weak, alias("Default_Handler")));   // C 스타트업 방식 1개
extern "C" uint32_t _estack;                     // 링커 스크립트가 정의 (스택 꼭대기)

union Vec { const void* sp; isr_t* fn; };         // [0] 만 데이터 주소, 나머지는 함수 — 캐스트 없이 상수식
struct VecTable { Vec v[16 + kNumIrq]; };

consteval VecTable make_vectors() {               // 테이블 전체를 컴파일 타임에 계산 → .isr_vector (플래시)
    VecTable t{};
    t.v[0].sp = &_estack;
    t.v[1].fn = &Reset_Handler;
    for (unsigned i = 2; i < 16 + kNumIrq; ++i) t.v[i].fn = &Default_Handler;
    constexpr unsigned kReserved[] = {7, 8, 9, 10, 13};
    for (unsigned i : kReserved) t.v[i].fn = nullptr;   // 예약 슬롯
    t.v[3].fn = &HardFault_Handler;
    t.v[15].fn = &SysTick_Handler;
    t.v[16 + 37].fn = &U1::isr;                   // 정적 멤버 함수 → C 링크 함수 포인터 자리 (Q3)
    t.v[16 + 38].fn = &U2::isr;
    t.v[16 + 39].fn = &U3::isr;
    t.v[16 + 52].fn = &UART4_IRQHandler;          // weak alias: 다른 TU 의 강한 정의가 덮는다
    t.v[16 + 53].fn = &irq_trampoline;            // UART5: 런타임 등록 테이블 경로
    return t;
}

#ifndef BAD_SP_CAST
__attribute__((section(".isr_vector"), used)) constinit const VecTable g_vectors = make_vectors();
#else
using vec_t = void (*)(void);
vec_t pick_hardfault();                           // 다른 TU (예: 빌드 설정에 따라 핸들러를 고르는 함수)
#ifdef WITH_CONSTINIT
__attribute__((section(".isr_vector"), used)) constinit
#else
__attribute__((section(".isr_vector"), used))
#endif
extern const vec_t g_vectors[16 + kNumIrq] = {
    reinterpret_cast<vec_t>(&_estack), &Reset_Handler,          // reinterpret_cast 는 상수식이 아니다 [expr.const]
#ifdef DYN_ENTRY
    pick_hardfault(),                                            // 함수 호출 결과 → 진짜 동적 초기화
#endif
};
#endif

/* RAM 벡터 테이블 (VTOR 재배치용, BM-A01): 정렬 = 엔트리 수×4 이상의 2의 거듭제곱 (112×4=448 → 512) */
alignas(512) isr_t* g_ram_vectors[16 + kNumIrq];

/* 멤버 함수 포인터는 벡터 테이블 엔트리(1워드)에 못 들어간다: 크기와 인코딩 관찰 */
struct Dev { virtual void v(); void nv(); int pad; };
void (Dev::*g_pmf_nv)() = &Dev::nv;
void (Dev::*g_pmf_v)()  = &Dev::v;
