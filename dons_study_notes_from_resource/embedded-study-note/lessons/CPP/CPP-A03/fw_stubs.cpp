/* CPP-A03 fw_stubs.cpp — C++ 런타임 스텁 층 (단일 코어 Cortex-M, 펌웨어는 끝나지 않는다).
 * 의도적으로 operator new 는 정의하지 않는다: 힙을 쓰는 코드는 링크 단계에서 걸리게. */
#include <cstddef>
#include <cstdint>

namespace {
struct FaultRecord { uint32_t code; uintptr_t lr; uint32_t count; };
[[gnu::section(".noinit")]] FaultRecord g_fault;              /* 리셋 뒤에도 남기는 자리 (링커 스크립트에 .noinit 필요) */
[[noreturn]] void fw_trap(uint32_t code, void* lr) {
    g_fault.code = code; g_fault.lr = reinterpret_cast<uintptr_t>(lr); ++g_fault.count;
    __asm volatile("bkpt #0");                                  /* 디버거가 있으면 여기서 멈춤 */
    for (;;) { }                                                /* 없으면 WDT 가 리셋 */
}
inline uint32_t irq_save() { uint32_t pm; __asm volatile("mrs %0, primask\n cpsid i" : "=r"(pm) :: "memory"); return pm; }
inline void irq_restore(uint32_t pm) { __asm volatile("msr primask, %0" :: "r"(pm) : "memory"); }
constexpr uint32_t kDone = 1u, kBusy = 1u << 8;                 /* bit0 = 완료 (컴파일러의 인라인 검사가 보는 비트) */
}

extern "C" {
void* __dso_handle = &__dso_handle;                             /* 정적 펌웨어엔 DSO 가 하나: 값은 "구별되는 주소" 면 된다 */
int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }   /* 끝나지 않는 펌웨어: 등록하지 않고 성공 */
int __aeabi_atexit(void* obj, void (*dtor)(void*), void* dso) { return __cxa_atexit(dtor, obj, dso); } /* 인자 순서 주의 */

[[noreturn]] void __cxa_pure_virtual() { fw_trap(0xC0DE0001u, __builtin_return_address(0)); }

int __cxa_guard_acquire(uint32_t* g) {
    const uint32_t pm = irq_save();                             /* 검사+표시를 ISR 에 대해 원자적으로 */
    const uint32_t v = *g;
    if (v & kDone) { irq_restore(pm); return 0; }
    if (v & kBusy) { irq_restore(pm); fw_trap(0xC0DE0002u, __builtin_return_address(0)); } /* 초기화 중 재진입 */
    *g = v | kBusy;
    irq_restore(pm);                                            /* 생성자는 인터럽트를 켠 채 돈다 */
    return 1;
}
void __cxa_guard_release(uint32_t* g) {
    __atomic_store_n(g, kDone, __ATOMIC_RELEASE);               /* 인라인 빠른 경로(ldrb; dmb)와 짝 */
}
void __cxa_guard_abort(uint32_t* g) { __atomic_store_n(g, 0u, __ATOMIC_RELEASE); }
}

void operator delete(void* p) noexcept { fw_trap(0xC0DE0003u, p); }              /* D0 슬롯용: 불리면 버그 */
void operator delete(void* p, std::size_t) noexcept { fw_trap(0xC0DE0003u, p); }
