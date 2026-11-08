// LLAT-M02 demo_torn.cpp — 찢어진(torn) 스냅샷 스트레스: 올바른 seqlock vs 깨진 변형 6개
//
// writer 1개(two_writers 만 2개)가 64 B Quote 를 계속 덮어쓰고 reader 3개가 읽는다.
// "받아들인(accepted)" 스냅샷 중 체크섬이 틀린 것 = torn. 올바른 버전은 반드시 0, 나머지는 관찰값이다.
// 깨진 변형이 0 을 보여도 "맞다" 는 뜻이 아니다 — 이 칩·이 실행에서 안 보였을 뿐이다.
//
// build: c++ -std=c++20 -Wall -Wextra -O2 -pthread demo_torn.cpp -o torn && ./torn [variant] [ms]
//   variant: correct memcpy no_rd_fence no_wr_fence all_relaxed no_odd two_writers (생략 = 전부), ms 기본 1000
// memcpy 변형은 data race(UB) 다 — 결과는 "이 컴파일러가 만든 이 바이너리" 의 관찰일 뿐.
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

using namespace std::chrono;
using std::memory_order_acquire, std::memory_order_relaxed, std::memory_order_release;

struct Quote { std::uint64_t w[8]; };                        // 워드 7 = 앞 7 워드 해시
static std::uint64_t qsum(const std::uint64_t *w) {
    std::uint64_t h = 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 7; i++) { h ^= w[i]; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 31; }
    return h;
}
static Quote make(std::uint64_t n) {
    Quote q;
    for (int i = 0; i < 7; i++) q.w[i] = n * 0x100000001B3ull + static_cast<std::uint64_t>(i) * 0x9E37ull + n;
    q.w[7] = qsum(q.w);
    return q;
}

struct alignas(128) Shared {
    std::atomic<std::uint64_t> seq{0};
    std::atomic<std::uint64_t> w[8] = {};
    Quote plain{};                                           // memcpy 변형 전용 (보통 메모리)
};
static Shared g;

enum V { CORRECT, MEMCPY, NO_RD_FENCE, NO_WR_FENCE, ALL_RELAXED, NO_ODD, TWO_WRITERS, NV };
static const char *kName[NV] = {"correct", "memcpy", "no_rd_fence", "no_wr_fence", "all_relaxed", "no_odd", "two_writers"};

static void write_v(V v, const Quote &q) {
    const std::uint64_t s = g.seq.load(memory_order_relaxed);
    g.seq.store(s + 1, memory_order_relaxed);
    if (v != NO_WR_FENCE && v != ALL_RELAXED) std::atomic_thread_fence(memory_order_release);
    if (v == MEMCPY) std::memcpy(&g.plain, &q, sizeof q);  // 일반 store (race)
    else for (int k = 0; k < 8; k++) g.w[k].store(q.w[k], memory_order_relaxed);
    g.seq.store(s + 2, v == ALL_RELAXED ? memory_order_relaxed : memory_order_release);
}
static bool read_v(V v, Quote &out) {
    const std::uint64_t s1 = g.seq.load(v == ALL_RELAXED ? memory_order_relaxed : memory_order_acquire);
    if (v != NO_ODD && (s1 & 1u)) return false;
    Quote t;
    if (v == MEMCPY) std::memcpy(&t, &g.plain, sizeof t);  // 일반 load (race)
    else for (int k = 0; k < 8; k++) t.w[k] = g.w[k].load(memory_order_relaxed);
    if (v != NO_RD_FENCE && v != ALL_RELAXED) std::atomic_thread_fence(memory_order_acquire);
    if (g.seq.load(memory_order_relaxed) != s1) return false;
    out = t;
    return true;
}

static void spin_ns(long ns) {
    const auto t0 = steady_clock::now();
    while (steady_clock::now() - t0 < nanoseconds(ns)) {}
}

static void run(V v, int ms) {
    g.seq.store(0);
    write_v(v == TWO_WRITERS ? CORRECT : v, make(0));
    std::atomic<bool> stop{false};
    std::uint64_t acc[3] = {}, torn[3] = {}, fail[3] = {}, writes[2] = {};
    std::vector<std::thread> ts;
    const int nw = v == TWO_WRITERS ? 2 : 1;
    for (int w = 0; w < nw; w++)
        ts.emplace_back([&, w] {
            std::uint64_t n = 1 + static_cast<std::uint64_t>(w) * (1ull << 40);
            while (!stop.load(memory_order_relaxed)) {
                write_v(v == TWO_WRITERS ? CORRECT : v, make(n++));   // two_writers = 올바른 단일 writer 코드를 2 스레드가
                writes[w]++;
                if ((n & 3) == 0) spin_ns(200);                      // 가끔 틈을 줘야 reader 가 성공한다
            }
        });
    for (int r = 0; r < 3; r++)
        ts.emplace_back([&, r] {
            Quote q;
            while (!stop.load(memory_order_relaxed)) {
                if (!read_v(v == TWO_WRITERS ? CORRECT : v, q)) { fail[r]++; continue; }
                acc[r]++;
                if (q.w[7] != qsum(q.w)) torn[r]++;
            }
        });
    std::this_thread::sleep_for(milliseconds(ms));
    stop.store(true);
    for (auto &t : ts) t.join();
    const std::uint64_t A = acc[0] + acc[1] + acc[2], T = torn[0] + torn[1] + torn[2], F = fail[0] + fail[1] + fail[2];
    std::printf("%-12s writes=%10llu accepted=%10llu retried=%11llu torn=%8llu (%.4f%%)\n", kName[v],
                (unsigned long long)(writes[0] + writes[1]), (unsigned long long)A, (unsigned long long)F,
                (unsigned long long)T, A ? 100.0 * static_cast<double>(T) / static_cast<double>(A) : 0.0);
}

int main(int argc, char **argv) {
    const int ms = argc > 2 ? std::atoi(argv[2]) : 1000;
    for (int v = 0; v < NV; v++)
        if (argc < 2 || std::strcmp(argv[1], kName[v]) == 0) run(static_cast<V>(v), ms);
}
