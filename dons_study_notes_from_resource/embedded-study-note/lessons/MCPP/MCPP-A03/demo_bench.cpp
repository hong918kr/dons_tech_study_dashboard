// MCPP-A03 demo: 직접 만든 Semaphore / Latch / Mutex  vs  std::counting_semaphore / binary_semaphore / latch / mutex.
//   B1  대기자 없는 release+acquire 한 쌍 (ns) — notify 를 건너뛰는 효과 (항상 notify 하는 변형과 비교)
//   B2  핑퐁 왕복 (두 스레드가 번갈아 release/acquire) — 상대가 금방 오면 wait 의 스핀 단계에서 끝난다
//   B3  "잠든 대기자" 깨우기 지연: 신호 쪽이 100 µs 쉰 뒤 t0 → release/unlock/count_down, 대기자는 반환 직후 t1
//   B4  경합 뮤텍스 처리량 (4 / 8 스레드, 임계 구역 = 카운터 +1)
// 시계: clock_gettime_nsec_np(CLOCK_UPTIME_RAW) (mach 시간 기반, 이 맥은 24 MHz = 41.67 ns 눈금).
// 다른 벤치마크가 동시에 도는 맥이다 — 절대값보다 "배수" 를 본다. 한 번 실행 ≈ 3 초.
// build: c++ -std=c++20 -O2 -pthread demo_bench.cpp -o dbench && ./dbench
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <latch>
#include <mach/mach_time.h>
#include <mutex>
#include <semaphore>
#include <thread>
#include <time.h>
#include <vector>

using i64 = std::int64_t;
using std::memory_order_relaxed, std::memory_order_acquire, std::memory_order_release, std::memory_order_seq_cst;
static inline std::uint64_t now_ns() { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

// ---- solution.cpp 와 같은 구현 (통계 래퍼 없이 std 호출 직접) ----
template <class T, bool kSkip, bool kSep = false> class SemT {   // kSep: count 와 waiters 를 다른 128 B 줄에
public:
    explicit SemT(i64 n) : c_(static_cast<T>(n)) {}
    bool try_acquire() {
        T c = c_.load(memory_order_relaxed);
        while (c > 0)
            if (c_.compare_exchange_weak(c, c - 1, memory_order_acquire, memory_order_relaxed)) return true;
        return false;
    }
    void acquire() {
        while (!try_acquire()) {
            w_.fetch_add(1, memory_order_seq_cst);
            c_.wait(0, memory_order_seq_cst);
            w_.fetch_sub(1, memory_order_seq_cst);
        }
    }
    void release() {
        c_.fetch_add(1, memory_order_seq_cst);
        if (kSkip && w_.load(memory_order_seq_cst) == 0) return;
        c_.notify_one();
    }
private:
    alignas(kSep ? 128 : alignof(std::atomic<T>)) std::atomic<T> c_;
    alignas(kSep ? 128 : alignof(std::atomic<i64>)) std::atomic<i64> w_{0};
};
using SemSkip = SemT<i64, true>;            // 정답: 대기자 없으면 notify 생략 (count·waiters 같은 줄)
using SemSkipSep = SemT<i64, true, true>;   // 같은 알고리즘, waiters 를 다른 캐시 줄로
using SemAlways = SemT<i64, false>;         // 매번 notify_one (int64: 변수 자체 경로)
using SemAlways32 = SemT<std::uint32_t, false>;   // 매번 notify_one (uint32: 전역 표 경로 = ldaddal + 깨우면 전부)

class Latch {
public:
    explicit Latch(i64 n) : c_(n) {}
    void count_down() { if (c_.fetch_sub(1, memory_order_release) == 1) c_.notify_all(); }
    void wait() const {
        for (i64 c = c_.load(memory_order_acquire); c != 0; c = c_.load(memory_order_acquire)) c_.wait(c, memory_order_acquire);
    }
private:
    std::atomic<i64> c_;
};
static std::atomic<bool> g_count_notifies{false};
static std::atomic<i64> g_mx_notifies{0};   // B4: unlock 이 notify_one 을 부른 횟수 (세는 실행에서만)
class Mutex {
public:
    void lock() {
        i64 c = 0;
        if (s_.compare_exchange_strong(c, 1, memory_order_acquire, memory_order_relaxed)) return;
        if (c != 2) c = s_.exchange(2, memory_order_acquire);
        while (c != 0) { s_.wait(2, memory_order_relaxed); c = s_.exchange(2, memory_order_acquire); }
    }
    void unlock() {
        if (s_.exchange(0, memory_order_release) == 2) {
            if (g_count_notifies.load(memory_order_relaxed)) g_mx_notifies.fetch_add(1, memory_order_relaxed);
            s_.notify_one();
        }
    }
private:
    std::atomic<i64> s_{0};
};

struct Stat { double med, p99, min; };
static double g_mean = 0;   // B2: 전체 시간 / 왕복 수 (41.67 ns 눈금에 덜 묶인 평균)
static Stat stats(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return {v[v.size() / 2], v[std::min(v.size() - 1, v.size() * 99 / 100)], v[0]};
}
static bool g_yield_spin = false;            // ./dbench spin : 신호 쪽이 yield 로 돌며 기다린다 (B3 측정 오염 재현)
static void spin_wait_ge(const std::atomic<int> &a, int target) {
    if (g_yield_spin) { while (a.load(memory_order_acquire) < target) std::this_thread::yield(); return; }
    for (int v = a.load(memory_order_acquire); v < target; v = a.load(memory_order_acquire)) a.wait(v, memory_order_acquire);
}
static void bump(std::atomic<int> &a) { a.fetch_add(1, memory_order_release); a.notify_all(); }

// ---- B1 ----
template <class S> static double b1_pair_ns() {
    S s(0);
    constexpr int N = 3000000;
    for (int i = 0; i < 10000; i++) { s.release(); s.acquire(); }
    std::uint64_t t0 = now_ns();
    for (int i = 0; i < N; i++) { s.release(); s.acquire(); }
    return double(now_ns() - t0) / N;
}
template <class S> static double b1_pair_try_ns() {      // acquire 대신 try_acquire (대기 함수에 안 들어감)
    S s(0);
    constexpr int N = 3000000;
    std::uint64_t t0 = now_ns();
    for (int i = 0; i < N; i++) { s.release(); if (!s.try_acquire()) std::printf("!!\n"); }
    return double(now_ns() - t0) / N;
}
template <class S> static double b1_shared_mops(int T) {  // 스레드 T 개가 세마포어 하나를 같이: release → acquire (절대 안 잠든다)
    constexpr int K = 300000;
    S s(0);
    std::vector<std::thread> ts;
    std::uint64_t t0 = now_ns();
    for (int t = 0; t < T; t++) ts.emplace_back([&] { for (int i = 0; i < K; i++) { s.release(); s.acquire(); } });
    for (auto &t : ts) t.join();
    return double(T) * K / (double(now_ns() - t0) / 1e9) / 1e6;
}
template <class M> static double b1_lock_ns() {
    M m;
    constexpr int N = 3000000;
    std::uint64_t t0 = now_ns();
    for (int i = 0; i < N; i++) { m.lock(); m.unlock(); }
    return double(now_ns() - t0) / N;
}
// ---- B2 ----
template <class S> static Stat b2_pingpong() {
    constexpr int R = 20000;
    S a(0), b(0);
    std::vector<double> rtt(R);
    std::uint64_t start = now_ns();
    std::thread peer([&] { for (int i = 0; i < R; i++) { b.acquire(); a.release(); } });
    for (int i = 0; i < R; i++) {
        std::uint64_t t0 = now_ns();
        b.release();
        a.acquire();
        rtt[static_cast<std::size_t>(i)] = double(now_ns() - t0);
    }
    peer.join();
    g_mean = double(now_ns() - start) / R;
    return stats(rtt);
}
// ---- B3 ----
constexpr int R3 = 600;
template <class S> static Stat b3_sem_wake() {
    S s(0);
    std::vector<std::uint64_t> t0(R3), t1(R3);
    std::atomic<int> done{0};
    std::thread w([&] {
        for (int i = 0; i < R3; i++) { s.acquire(); t1[static_cast<std::size_t>(i)] = now_ns(); bump(done); }
    });
    for (int i = 0; i < R3; i++) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        t0[static_cast<std::size_t>(i)] = now_ns();
        s.release();
        spin_wait_ge(done, i + 1);
    }
    w.join();
    std::vector<double> d(R3);
    for (int i = 0; i < R3; i++) d[static_cast<std::size_t>(i)] = double(t1[static_cast<std::size_t>(i)] - t0[static_cast<std::size_t>(i)]);
    return stats(d);
}
template <class M> static Stat b3_mutex_wake() {
    M m;
    std::vector<std::uint64_t> t0(R3), t1(R3);
    std::atomic<int> go{0}, done{0};
    std::thread w([&] {
        for (int i = 0; i < R3; i++) {
            spin_wait_ge(go, i + 1);
            m.lock();                                          // 주인이 100 µs 동안 쥐고 있다 → 잠든다
            t1[static_cast<std::size_t>(i)] = now_ns();
            m.unlock();
            bump(done);
        }
    });
    for (int i = 0; i < R3; i++) {
        m.lock();
        bump(go);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        t0[static_cast<std::size_t>(i)] = now_ns();
        m.unlock();
        spin_wait_ge(done, i + 1);
    }
    w.join();
    std::vector<double> d(R3);
    for (int i = 0; i < R3; i++) d[static_cast<std::size_t>(i)] = double(t1[static_cast<std::size_t>(i)] - t0[static_cast<std::size_t>(i)]);
    return stats(d);
}
template <class L> static Stat b3_latch_wake() {
    constexpr int W = 4;
    std::deque<L> ls;
    for (int i = 0; i < R3; i++) ls.emplace_back(1);
    std::vector<std::uint64_t> t0(R3), t1(static_cast<std::size_t>(R3 * W));
    std::atomic<int> done{0};
    std::vector<std::thread> ws;
    for (int k = 0; k < W; k++)
        ws.emplace_back([&, k] {
            for (int i = 0; i < R3; i++) {
                ls[static_cast<std::size_t>(i)].wait();
                t1[static_cast<std::size_t>(i * W + k)] = now_ns();
                bump(done);
            }
        });
    for (int i = 0; i < R3; i++) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        t0[static_cast<std::size_t>(i)] = now_ns();
        ls[static_cast<std::size_t>(i)].count_down();
        spin_wait_ge(done, (i + 1) * W);
    }
    for (auto &t : ws) t.join();
    std::vector<double> d;
    for (int i = 0; i < R3; i++)
        for (int k = 0; k < W; k++) d.push_back(double(t1[static_cast<std::size_t>(i * W + k)] - t0[static_cast<std::size_t>(i)]));
    return stats(d);
}
// ---- B4 ----
static double g_handoff = 0;                 // 직전 주인과 다른 스레드가 잡은 비율 (높을수록 캐시 줄이 코어 사이를 오간다)
template <class M> static double b4_mops(int T) {
    constexpr int K = 200000;
    M m;
    i64 counter = 0, handoffs = 0;
    int last = -1;                           // plain — 뮤텍스가 보호
    std::vector<std::thread> ts;
    std::uint64_t t0 = now_ns();
    for (int t = 0; t < T; t++)
        ts.emplace_back([&, t] {
            for (int i = 0; i < K; i++) { m.lock(); counter++; if (last != t) { handoffs++; last = t; } m.unlock(); }
        });
    for (auto &t : ts) t.join();
    double s = double(now_ns() - t0) / 1e9;
    if (counter != i64{T} * K) std::printf("!! counter mismatch\n");
    g_handoff = double(handoffs) / double(counter);
    return double(T) * K / s / 1e6;
}

static void pr(const char *name, Stat s) { std::printf("  %-28s med %8.0f  p99 %8.0f  min %6.0f ns\n", name, s.med, s.p99, s.min); }

int main(int argc, char **argv) {
    g_yield_spin = argc > 1 && argv[1][0] == 's';
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    std::printf("mach timebase %u/%u (1 tick = %.2f ns)\n", tb.numer, tb.denom, double(tb.numer) / tb.denom);
    std::printf("B1 대기자 없는 release+acquire 한 쌍 / lock+unlock (ns)\n");
    std::printf("  Semaphore (notify 생략)        %6.2f\n", b1_pair_ns<SemSkip>());
    std::printf("  항상 notify_one, int64         %6.2f\n", b1_pair_ns<SemAlways>());
    std::printf("  항상 notify_one, uint32        %6.2f\n", b1_pair_ns<SemAlways32>());
    std::printf("  std::counting_semaphore        %6.2f\n", b1_pair_ns<std::counting_semaphore<>>());
    std::printf("  std::binary_semaphore          %6.2f\n", b1_pair_ns<std::binary_semaphore>());
    std::printf("  std::counting_semaphore (try)  %6.2f   <- release + try_acquire\n", b1_pair_try_ns<std::counting_semaphore<>>());
    std::printf("  Mutex lock+unlock              %6.2f\n", b1_lock_ns<Mutex>());
    std::printf("  std::mutex lock+unlock         %6.2f\n", b1_lock_ns<std::mutex>());
    std::printf("B1' 스레드 4 개가 세마포어 하나 공유, release+acquire (Mops/s, 안 잠듦)\n");
    std::printf("  Semaphore %5.1f  (waiters 다른 줄) %5.1f  항상 notify int64 %5.1f  uint32 %5.1f  std::counting_semaphore %5.1f\n",
                b1_shared_mops<SemSkip>(4), b1_shared_mops<SemSkipSep>(4), b1_shared_mops<SemAlways>(4),
                b1_shared_mops<SemAlways32>(4), b1_shared_mops<std::counting_semaphore<>>(4));
    std::printf("B2 핑퐁 왕복 (20000 회)\n");
    pr("Semaphore", b2_pingpong<SemSkip>());              std::printf("  %-28s mean %7.0f ns\n", "", g_mean);
    pr("Semaphore (waiters 다른 줄)", b2_pingpong<SemSkipSep>()); std::printf("  %-28s mean %7.0f ns\n", "", g_mean);
    pr("std::counting_semaphore", b2_pingpong<std::counting_semaphore<>>()); std::printf("  %-28s mean %7.0f ns\n", "", g_mean);
    pr("std::binary_semaphore", b2_pingpong<std::binary_semaphore>()); std::printf("  %-28s mean %7.0f ns\n", "", g_mean);
    std::printf("B3 잠든 대기자 깨우기: 신호 t0 → 대기자 반환 t1 (%d 회, 신호 전 100 us 쉼, 신호 쪽 대기 = %s)\n", R3, g_yield_spin ? "yield 스핀" : "atomic wait");
    pr("Semaphore", b3_sem_wake<SemSkip>());
    pr("std::counting_semaphore", b3_sem_wake<std::counting_semaphore<>>());
    pr("Mutex (unlock → lock)", b3_mutex_wake<Mutex>());
    pr("std::mutex (unlock → lock)", b3_mutex_wake<std::mutex>());
    pr("Latch (대기자 4)", b3_latch_wake<Latch>());
    pr("std::latch (대기자 4)", b3_latch_wake<std::latch>());
    std::printf("B4 경합 뮤텍스 처리량 (Mops/s, 스레드당 200000 회)\n");
    for (int T : {4, 8}) {
        double m0 = b4_mops<Mutex>(T), h0 = g_handoff;
        double m1 = b4_mops<std::mutex>(T), h1 = g_handoff;
        g_count_notifies.store(true); g_mx_notifies.store(0);
        b4_mops<Mutex>(T);
        double n0 = double(g_mx_notifies.load()) / (T * 200000.0);
        g_count_notifies.store(false);
        std::printf("  T=%d  Mutex %6.2f (넘겨줌 %4.1f%%, notify/unlock %.2f)   std::mutex %6.2f (넘겨줌 %4.1f%%)\n",
                    T, m0, 100 * h0, n0, m1, 100 * h1);
    }
}
