// MCPP-A03 demo: std::atomic<T>::wait / notify_one 이 이 맥의 libc++ 에서 실제로 무엇을 부르나.
// uint32_t 와 int64_t 두 폭으로 한 번씩 "진짜로 잠들었다 깨기" 를 한다 (20 ms 쉬어서 스핀 단계를 넘긴다).
// 관찰: nm -m 으로 libc++ 진입점, lldb 브레이크포인트로 __ulock_wait / __ulock_wake 인자.
//   c++ -std=c++20 -O2 -pthread demo_platform_calls.cpp -o dpc && nm -m dpc | grep -E 'atomic_(wait|notify|monitor)'
//   lldb -b -o 'br set -n __ulock_wait -n __ulock_wake -n os_sync_wait_on_address' -o run \
//        -o 'register read x0 x1 x2' -o continue -o 'register read x0 x1 x2' -o continue ... ./dpc
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

std::atomic<std::uint32_t> a32{0};
std::atomic<std::int64_t> a64{0};

int main() {
    std::printf("&a32=%p  &a64=%p\n", static_cast<void *>(&a32), static_cast<void *>(&a64));
    std::fflush(stdout);
    std::thread t([] {
        a32.wait(0);   // 32 비트: 전역 표(__contention_table_global)의 칸에서 잔다
        a64.wait(0);   // 64 비트: a64 자체에서 잔다 (__contention_table_native 는 대기자 수만 센다)
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    a32.store(1);
    a32.notify_one();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    a64.store(1);
    a64.notify_one();
    t.join();
    std::puts("done");
}
