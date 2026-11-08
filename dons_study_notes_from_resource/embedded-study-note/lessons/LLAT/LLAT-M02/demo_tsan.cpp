// LLAT-M02 demo_tsan.cpp — TSan 이 보는 것과 못 보는 것
//   memcpy   : 데이터가 보통 메모리 + memcpy → C++ data race (UB). TSan 이 보고해야 하는데 — 이 맥에서는 놓쳤다 (md 참고).
//   plain    : 같은 보통 메모리를 워드 단위 load/store 로 (컴파일러가 memcpy 로 바꾸지 못하게) → 같은 data race. TSan 이 보고한다.
//   correct  : 데이터가 relaxed atomic 워드 (Boehm 형태) → race 없음. TSan 조용.
//   no_fence : reader 의 acquire fence 를 뺀 깨진 판 → 실제로 찢어지지만(demo_torn) 접근이 전부 atomic 이라 TSan 은 조용.
// build: c++ -std=c++20 -g -O1 -fsanitize=thread -pthread demo_tsan.cpp -o dts && ./dts memcpy|plain|correct|no_fence
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

using std::memory_order_acquire, std::memory_order_relaxed, std::memory_order_release;
struct Q { std::uint64_t w[8]; };
static std::atomic<std::uint64_t> g_seq{0};
static std::atomic<std::uint64_t> g_w[8];
static Q g_plain;                                            // memcpy / plain 판 전용

static void write_q(int mode, std::uint64_t n) {
    Q q;
    for (auto &x : q.w) x = n;
    const std::uint64_t s = g_seq.load(memory_order_relaxed);
    g_seq.store(s + 1, memory_order_relaxed);
    std::atomic_thread_fence(memory_order_release);
    if (mode == 0) std::memcpy(&g_plain, &q, sizeof q);
    else if (mode == 3) for (int k = 0; k < 8; k++) g_plain.w[k] = n + 0 * static_cast<std::uint64_t>(k);  // 계산한 값을 워드마다 store (memcpy 로 안 바뀜)
    else for (int k = 0; k < 8; k++) g_w[k].store(q.w[k], memory_order_relaxed);
    g_seq.store(s + 2, memory_order_release);
}
static bool read_q(int mode, Q &out) {
    const std::uint64_t s1 = g_seq.load(memory_order_acquire);
    if (s1 & 1u) return false;
    Q t;
    if (mode == 0) std::memcpy(&t, &g_plain, sizeof t);
    else if (mode == 3) {                                    // 워드 0 과 비교하며 읽기 (복사 루프가 아니라서 memcpy 로 안 바뀜)
        t.w[0] = g_plain.w[0];
        for (int k = 1; k < 8; k++) t.w[k] = g_plain.w[k] == t.w[0] ? t.w[0] : ~t.w[0];
    }
    else for (int k = 0; k < 8; k++) t.w[k] = g_w[k].load(memory_order_relaxed);
    if (mode != 2) std::atomic_thread_fence(memory_order_acquire);
    if (g_seq.load(memory_order_relaxed) != s1) return false;
    out = t;
    return true;
}

int main(int argc, char **argv) {
    static const char *kMode[] = {"memcpy", "correct", "no_fence", "plain"};
    int mode = 0;
    for (int m = 0; m < 4; m++) if (argc > 1 && !std::strcmp(argv[1], kMode[m])) mode = m;
    std::atomic<bool> stop{false};
    std::uint64_t ok = 0, bad = 0;
    std::thread rd([&] {
        Q q;
        while (!stop.load(memory_order_acquire))
            if (read_q(mode, q)) { ok++; for (auto x : q.w) if (x != q.w[0]) { bad++; break; } }
    });
    for (std::uint64_t n = 1; n <= 200000; n++) write_q(mode, n);
    stop.store(true, memory_order_release);
    rd.join();
    std::printf("mode=%s accepted=%llu torn=%llu\n", kMode[mode],
                (unsigned long long)ok, (unsigned long long)bad);
}
