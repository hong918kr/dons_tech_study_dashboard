/* CPP-B06 demo: RAII 가드 vs C goto-cleanup — 생성 코드 비교 (컴파일만, 실행 금지)
 *   c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_cost.cpp
 * 호스트가 arm64 라서 Cortex-M 명령 대신 같은 역할의 AArch64 명령을 inline asm 으로 쓴다:
 *   mrs x, daif      ≈ mrs r0, primask   (현재 마스크 읽기, I 비트 = bit7)
 *   msr daifset, #2  ≈ cpsid i           (IRQ 마스크)
 *   msr daifclr, #2  ≈ cpsie i           (IRQ 해제)
 * EL0(사용자 모드)에서 실행하면 트랩되므로 -S 로 어셈블리만 본다.
 */
#include <cstdint>

static inline uint32_t get_mask() { uint64_t v; asm volatile("mrs %0, daif" : "=r"(v) : : "memory"); return uint32_t(v); }
static inline void irq_off() { asm volatile("msr daifset, #2" : : : "memory"); }
static inline void irq_on()  { asm volatile("msr daifclr, #2" : : : "memory"); }

class CriticalSection {
public:
    [[nodiscard]] CriticalSection() noexcept : saved_(get_mask()) { irq_off(); }
    ~CriticalSection() { if ((saved_ & 0x80u) == 0) irq_on(); }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
private:
    uint32_t saved_;
};

uint8_t g_slots[4];

extern "C" int claim_raii(uint8_t owner) {
    if (owner == 0) return -2;
    CriticalSection cs;
    for (int i = 0; i < 4; ++i)
        if (g_slots[i] == 0) { g_slots[i] = owner; return i; }
    return -1;
}

extern "C" int claim_goto(uint8_t owner) {
    if (owner == 0) return -2;
    int rc = -1;
    uint32_t m = get_mask();
    irq_off();
    for (int i = 0; i < 4; ++i)
        if (g_slots[i] == 0) { g_slots[i] = owner; rc = i; goto out; }
out:
    if ((m & 0x80u) == 0) irq_on();
    return rc;
}
