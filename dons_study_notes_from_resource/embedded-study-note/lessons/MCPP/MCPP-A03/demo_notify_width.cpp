// MCPP-A03 demo: notify_one 은 정말 "하나" 만 깨우나? — 대기 변수의 폭에 따라 다르다 (이 맥 libc++).
// 대기자 4 개가 값 0 에서 잠든 뒤(30 ms), 값을 1 로 바꾸고 notify_one 한 번 → 30 ms 뒤 몇 개가 돌아왔나 센다.
//   uint32_t : 전역 표 칸에서 자고, notify_one 도 __ulock_wake(0x105 = ...|ULF_WAKE_ALL) → 전부 깨어난다
//   int64_t  : 변수 자체에서 자고, notify_one = __ulock_wake(5) → 하나만
// build: c++ -std=c++20 -O2 -pthread demo_notify_width.cpp -o dnw && ./dnw
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

template <class T> static int trial() {
    std::atomic<T> v{0};
    std::atomic<int> returned{0};
    std::vector<std::thread> ts;
    for (int i = 0; i < 4; i++) ts.emplace_back([&] { v.wait(0); returned.fetch_add(1); });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));   // 4 µs 스핀을 넘겨 커널에서 자게
    v.store(1);
    v.notify_one();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    int r = returned.load();
    v.notify_all();                                                // 남은 대기자 정리
    for (auto &t : ts) t.join();
    return r;
}

int main() {
    for (int k = 0; k < 5; k++) {
        int r32 = trial<std::uint32_t>();
        int r64 = trial<std::int64_t>();
        std::printf("trial %d: notify_one 뒤 돌아온 대기자 (4 개 중)  uint32_t=%d  int64_t=%d\n", k, r32, r64);
    }
}
