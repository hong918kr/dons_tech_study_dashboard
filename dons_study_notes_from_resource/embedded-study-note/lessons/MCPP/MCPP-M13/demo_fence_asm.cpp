// MCPP-M13 demo_fence_asm.cpp — 같은 동기화를 "연산" 으로 쓸 때와 "fence" 로 쓸 때 arm64 명령 비교.
//   c++ -std=c++20 -O2 -S demo_fence_asm.cpp -o - | grep -E '^__Z|ldr|str|ldar|stlr|ldapr|dmb|cbz|cbnz|tbz|b\.'
#include <atomic>
#include <cstdint>

std::atomic<std::uint32_t> flag;
std::uint32_t data, ctrl_word[8];

void pub_op(std::uint32_t v) {            // op: release store
    data = v;
    flag.store(1, std::memory_order_release);
}
void pub_fence(std::uint32_t v) {         // fence: release fence + relaxed store
    data = v;
    std::atomic_thread_fence(std::memory_order_release);
    flag.store(1, std::memory_order_relaxed);
}
std::uint32_t get_op() {                  // op: acquire load
    return flag.load(std::memory_order_acquire) ? data : 0;
}
std::uint32_t get_fence() {               // fence: relaxed load + acquire fence (op–fence 의 받는 쪽)
    if (!flag.load(std::memory_order_relaxed)) return 0;
    std::atomic_thread_fence(std::memory_order_acquire);
    return data;
}
unsigned scan_fence() {                   // 여러 relaxed load 뒤 fence 한 번 (Part C dma_poll 모양)
    unsigned n = 0;
    for (unsigned i = 0; i < 8; i++)
        n += std::atomic_ref<std::uint32_t>(ctrl_word[i]).load(std::memory_order_relaxed) >> 31;
    std::atomic_thread_fence(std::memory_order_acquire);
    return n;
}
void ref_release(std::uint32_t v) {       // atomic_ref 로 plain 워드에 release store
    std::atomic_ref<std::uint32_t>(ctrl_word[0]).store(v, std::memory_order_release);
}
