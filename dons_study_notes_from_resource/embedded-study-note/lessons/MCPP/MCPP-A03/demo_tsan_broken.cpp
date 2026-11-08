// MCPP-A03 demo: 순서를 relaxed 로 내린 뮤텍스 — 상호 배제는 지켜지는데 TSan 은 레이스를 보고한다 (그리고 맞다).
// 상태 전이(CAS/exchange)는 원자적이라 "동시에 두 명" 은 없다. 그러나 lock 이 acquire 가 아니면
// 앞 주인의 counter++ 가 다음 주인에게 happens-before 로 넘어가지 않는다 → counter 는 data race ([intro.races]).
// build: c++ -std=c++20 -g -O1 -fsanitize=thread -pthread demo_tsan_broken.cpp -o dtb && ./dtb
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

class RelaxedMutex {
public:
    void lock() {
        std::int64_t c = 0;
        if (s_.compare_exchange_strong(c, 1, std::memory_order_relaxed)) return;   // 틀림: acquire 여야
        if (c != 2) c = s_.exchange(2, std::memory_order_relaxed);
        while (c != 0) { s_.wait(2, std::memory_order_relaxed); c = s_.exchange(2, std::memory_order_relaxed); }
    }
    void unlock() { if (s_.exchange(0, std::memory_order_relaxed) == 2) s_.notify_one(); }   // 틀림: release 여야
private:
    std::atomic<std::int64_t> s_{0};
};

int main() {
    RelaxedMutex m;
    long counter = 0;
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; t++) ts.emplace_back([&] { for (int i = 0; i < 10000; i++) { m.lock(); counter++; m.unlock(); } });
    for (auto &t : ts) t.join();
    std::printf("counter = %ld (기대 40000)\n", counter);
}
