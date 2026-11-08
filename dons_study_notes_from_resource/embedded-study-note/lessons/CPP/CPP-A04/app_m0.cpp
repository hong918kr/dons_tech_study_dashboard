// app_m0.cpp — std::atomic 을 쓰는 펌웨어 한 조각 (링크 테스트용)
#include <atomic>
#include <cstdint>
std::atomic<uint32_t> g_events;
std::atomic<uint64_t> g_bytes;
extern "C" void UART0_IRQHandler() { g_events.fetch_add(1, std::memory_order_relaxed); g_bytes.fetch_add(64); }
int main() {
    uint32_t e = g_events.exchange(0);
    uint32_t want = 0;
    g_events.compare_exchange_strong(want, 1);
    return static_cast<int>(e + g_bytes.load());
}
