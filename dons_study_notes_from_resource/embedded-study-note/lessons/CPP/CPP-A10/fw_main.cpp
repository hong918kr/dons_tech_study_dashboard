// CPP-A10 fw_main.cpp — Cortex-M4 타깃 이미지 (크기 예산 측정용). budget.sh 가 arm-none-eabi-g++ 로 빌드한다.
//   solution.cpp 의 라이브러리 부분(A10_LIB_ONLY) + 실제 MMIO I2C 컨트롤러 + UART 송신 + 벡터 테이블/리셋 핸들러.
//   I2C·UART 레지스터 배치는 "가상 컨트롤러" 대표값이다 (특정 칩 아님 — 실칩이면 그 RM 의 시퀀스로 바꾼다).
//   -DA10_DEMO_HEAP : 일부러 new 를 한 번 써서 budget.sh 의 금지 심볼 검사가 잡는지 본다.
#define A10_LIB_ONLY
#include "solution.cpp"
#include <atomic>

namespace a10::fw {
// CPP-M01 소형판: 주소는 NTTP, 권한은 타입, 캐스트는 접근 순간 (reinterpret_cast 는 상수식에 못 쓴다)
struct WO { static constexpr bool can_read = false, can_write = true; };
template <std::uintptr_t A, class Acc>
struct Reg {
    static std::uint32_t read() requires Acc::can_read { return *reinterpret_cast<const volatile std::uint32_t*>(A); }
    static void write(std::uint32_t v) requires Acc::can_write { *reinterpret_cast<volatile std::uint32_t*>(A) = v; }
};

inline constexpr std::uintptr_t kI2c = 0x4000'5400u, kUart = 0x4000'4400u;   // 대표값
using I2C_CR     = Reg<kI2c + 0x00, bus::RW>;    // bit0 START (STOP 자동)
using I2C_SR     = Reg<kI2c + 0x04, bus::RO>;    // bit1 DONE, bit2 NACK_ADDR, bit3 NACK_DATA, bit4 ARLO
using I2C_ADDR   = Reg<kI2c + 0x08, bus::RW>;
using I2C_NBYTES = Reg<kI2c + 0x0C, bus::RW>;    // [7:0] 쓸 바이트 수, [15:8] 읽을 바이트 수 (FIFO 16 — 대표값)
using I2C_TXDR   = Reg<kI2c + 0x10, WO>;
using I2C_RXDR   = Reg<kI2c + 0x14, bus::RO>;
using I2C_ICR    = Reg<kI2c + 0x18, WO>;         // 1 을 쓰면 플래그 지움
using UART_SR    = Reg<kUart + 0x00, bus::RO>;   // bit7 TXE
using UART_DR    = Reg<kUart + 0x04, WO>;

class HwI2c {
public:
    bool try_lock() { if (locked_) return false; locked_ = true; return true; }   // 단일 스레드 메인 루프 전제
    void unlock() { locked_ = false; }
    Result<void, BusErr> write_read(std::uint8_t addr, std::span<const std::uint8_t> tx, std::span<std::uint8_t> rx) {
        constexpr std::uint32_t DONE = 1u << 1, NACKA = 1u << 2, NACKD = 1u << 3, ARLO = 1u << 4;
        I2C_ICR::write(DONE | NACKA | NACKD | ARLO);
        I2C_ADDR::write(addr);
        I2C_NBYTES::write(static_cast<std::uint32_t>(tx.size() | rx.size() << 8));
        for (std::uint8_t b : tx) I2C_TXDR::write(b);
        I2C_CR::write(1u);
        std::uint32_t sr = 0, spins = 0;
        do {                                                   // 바운드 있는 폴링: 무한 대기 금지
            sr = I2C_SR::read();
            if (++spins > 100'000u) return fail(BusErr::Timeout);
        } while ((sr & (DONE | NACKA | NACKD | ARLO)) == 0);
        if (sr & NACKA) return fail(BusErr::AddrNack);
        if (sr & NACKD) return fail(BusErr::DataNack);
        if (sr & ARLO) return fail(BusErr::ArbLost);
        for (std::uint8_t& b : rx) b = static_cast<std::uint8_t>(I2C_RXDR::read());
        return {};
    }
private:
    bool locked_ = false;
};
static_assert(bus::I2cBus<HwI2c>);

static void uart_send(std::span<const std::uint8_t> s) {
    for (std::uint8_t b : s) {
        while ((UART_SR::read() & (1u << 7)) == 0) {}
        UART_DR::write(b);
    }
}

constinit std::atomic<std::uint32_t> g_ms{0};              // SysTick ISR 가 쓰고 main 이 읽음 (M4: lock-free, CPP-A04)
constinit HwI2c g_i2c;
constinit app::Pipeline<HwI2c> g_pipe{g_i2c, imu::Config{}, app::kAlpha};   // constinit: 생성자 코드 0, .init_array 없음
constinit static_vector<std::uint8_t, 64> g_tx;

[[noreturn]] void run() {
#ifdef A10_DEMO_HEAP
    volatile int* leak = new int(1);                       // 금지: budget.sh 가 _Znwj/malloc 을 잡아야 한다
    *leak = 2;
#endif
    if (!g_pipe.init()) trap();
    for (;;) {
        g_tx.clear();
        const auto r = g_pipe.step(g_ms.load(std::memory_order_relaxed), g_tx);
        if (r) uart_send(g_tx.view());                     // 실패는 파이프라인이 세고, 다음 프레임 flags 로 알린다
        __asm volatile("wfi");
    }
}
}  // namespace a10::fw

/* ---------------- 벡터 테이블 + 리셋 (코어 16개만; 벤더 IRQ 는 칩마다 다름) ---------------- */
extern "C" {
extern std::uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;   // fw.ld
[[noreturn]] void Reset_Handler();
void Default_Handler() { for (;;) {} }
void SysTick_Handler() { a10::fw::g_ms.fetch_add(1, std::memory_order_relaxed); }
}
using Vec = void (*)();
struct VectorTable { const void* sp; Vec h[15]; };
[[gnu::section(".isr_vector"), gnu::used]] constinit const VectorTable g_vectors{
    &_estack,
    {Reset_Handler, Default_Handler, Default_Handler, Default_Handler, Default_Handler, Default_Handler, nullptr,
     nullptr, nullptr, nullptr, Default_Handler, Default_Handler, nullptr, Default_Handler, SysTick_Handler}};

extern "C" [[noreturn]] void Reset_Handler() {
    for (std::uint32_t *s = &_sidata, *d = &_sdata; d < &_edata;) *d++ = *s++;   // .data 복사
    for (std::uint32_t* d = &_sbss; d < &_ebss;) *d++ = 0;                        // .bss 0
    a10::fw::run();                                                               // 정적 생성자 없음 → __libc_init_array 불필요
}
