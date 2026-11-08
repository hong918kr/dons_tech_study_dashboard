// LLAT-M02 demo_bench.cpp — reader 지연: seqlock vs pthread_rwlock_t vs std::mutex (64 B 스냅샷 복사)
//
// writer 1 개가 정해진 속도(0 / 100k / 1M / 최대 writes/s)로 쓰고, reader R 개(1, 4)가 쉬지 않고 읽는다.
// reader 표본 = 연속 64 번 읽기의 평균 ns (타이머 눈금 41.7 ns 라 한 번씩은 못 잰다) → p50 / p99 / p99.9 / max.
// writer 표본 = 쓰기 1 번(락 대기 포함)의 ns → p50 / p99 / max. seqlock 은 재시도 히스토그램도.
// 숫자는 CHECK 하지 않는다. 다른 벤치마크와 동시에 돌면 절대값이 흔들린다 → 배수만 본다.
//
// build: c++ -std=c++20 -Wall -Wextra -O2 -pthread demo_bench.cpp -o bench && ./bench [ms_per_config=300]
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <pthread.h>
#include <thread>
#include <time.h>
#include <vector>

using std::memory_order_acquire, std::memory_order_relaxed, std::memory_order_release;
static inline std::uint64_t now_ns() { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

struct Quote { std::uint64_t w[8]; };
static std::uint64_t qsum(const std::uint64_t *w) {
    std::uint64_t h = 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 7; i++) { h ^= w[i]; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 31; }
    return h;
}
static Quote make(std::uint64_t n) { Quote q; for (int i = 0; i < 7; i++) q.w[i] = n + static_cast<std::uint64_t>(i); q.w[7] = qsum(q.w); return q; }

// ---- 세 가지 공유 방식 (같은 64 B) ----
struct alignas(128) Seq { std::atomic<std::uint64_t> seq{0}; std::atomic<std::uint64_t> w[8] = {}; };
struct alignas(128) Rw { pthread_rwlock_t l = PTHREAD_RWLOCK_INITIALIZER; Quote q{}; };
struct alignas(128) Mx { std::mutex m; Quote q{}; };
static Seq g_seq;
static Rw g_rw;
static Mx g_mx;

static void seq_store(const Quote &v) {
    const std::uint64_t s = g_seq.seq.load(memory_order_relaxed);
    g_seq.seq.store(s + 1, memory_order_relaxed);
    std::atomic_thread_fence(memory_order_release);
    for (int k = 0; k < 8; k++) g_seq.w[k].store(v.w[k], memory_order_relaxed);
    g_seq.seq.store(s + 2, memory_order_release);
}
static unsigned seq_load(Quote &out) {
    for (unsigned r = 0;; r++) {
        const std::uint64_t s1 = g_seq.seq.load(memory_order_acquire);
        if (s1 & 1u) continue;
        Quote t;
        for (int k = 0; k < 8; k++) t.w[k] = g_seq.w[k].load(memory_order_relaxed);
        std::atomic_thread_fence(memory_order_acquire);
        if (g_seq.seq.load(memory_order_relaxed) != s1) continue;
        out = t;
        return r;
    }
}

enum Scheme { SEQ, RW, MX };
static const char *kScheme[] = {"seqlock", "rwlock", "mutex"};

struct Pct { double p50, p99, p999, mx; };
template <class V> static Pct pct(V &v) {
    if (v.empty()) return {0, 0, 0, 0};
    std::sort(v.begin(), v.end());
    auto at = [&](double q) { return static_cast<double>(v[std::min(v.size() - 1, static_cast<std::size_t>(q * static_cast<double>(v.size())))]); };
    return {at(0.50), at(0.99), at(0.999), static_cast<double>(v.back())};
}

static void run(Scheme sc, int R, long rate, int ms) {
    constexpr int B = 64;
    std::atomic<bool> stop{false};
    std::atomic<int> ready{0};
    std::vector<std::vector<float>> rs(static_cast<std::size_t>(R));
    std::vector<std::array<std::uint64_t, 6>> hist(static_cast<std::size_t>(R));   // 재시도 0,1,2,3-7,8-63,64+
    std::vector<std::uint64_t> reads(static_cast<std::size_t>(R)), torn(static_cast<std::size_t>(R));
    std::vector<std::uint32_t> wlat;
    wlat.reserve(4'000'000);
    std::vector<std::thread> ts;
    for (int r = 0; r < R; r++)
        ts.emplace_back([&, r] {
            auto &smp = rs[static_cast<std::size_t>(r)];
            smp.reserve(2'000'000);
            auto &h = hist[static_cast<std::size_t>(r)];
            h.fill(0);
            Quote q;
            std::uint64_t n = 0, bad = 0;
            ready.fetch_add(1);
            while (!stop.load(memory_order_relaxed)) {
                const std::uint64_t t0 = now_ns();
                for (int i = 0; i < B; i++) {
                    if (sc == SEQ) {
                        const unsigned rt = seq_load(q);
                        h[rt == 0 ? 0 : rt == 1 ? 1 : rt == 2 ? 2 : rt < 8 ? 3 : rt < 64 ? 4 : 5]++;
                    } else if (sc == RW) {
                        pthread_rwlock_rdlock(&g_rw.l); q = g_rw.q; pthread_rwlock_unlock(&g_rw.l);
                    } else {
                        std::lock_guard<std::mutex> g(g_mx.m); q = g_mx.q;
                    }
                    bad += q.w[7] != qsum(q.w);
                }
                const std::uint64_t t1 = now_ns();
                if (smp.size() < smp.capacity()) smp.push_back(static_cast<float>(t1 - t0) / B);
                n += B;
            }
            reads[static_cast<std::size_t>(r)] = n;
            torn[static_cast<std::size_t>(r)] = bad;
        });
    while (ready.load() < R) {}
    std::uint64_t writes = 0;
    const std::uint64_t start = now_ns(), end = start + static_cast<std::uint64_t>(ms) * 1'000'000u;
    const std::uint64_t period = rate > 0 ? 1'000'000'000u / static_cast<std::uint64_t>(rate) : 0;
    std::uint64_t next = start;
    for (std::uint64_t t = start; t < end; t = now_ns()) {
        if (rate == 0) continue;                                   // writer 없음
        if (period && t < next) continue;                          // 속도 맞추기 (busy-wait)
        next += period;
        const Quote v = make(++writes);
        const std::uint64_t w0 = now_ns();
        if (sc == SEQ) seq_store(v);
        else if (sc == RW) { pthread_rwlock_wrlock(&g_rw.l); g_rw.q = v; pthread_rwlock_unlock(&g_rw.l); }
        else { std::lock_guard<std::mutex> g(g_mx.m); g_mx.q = v; }
        if (wlat.size() < wlat.capacity()) wlat.push_back(static_cast<std::uint32_t>(now_ns() - w0));
    }
    stop.store(true);
    for (auto &t : ts) t.join();
    std::vector<float> all;
    std::uint64_t nr = 0, nt = 0;
    std::array<std::uint64_t, 6> hs{};
    for (int r = 0; r < R; r++) {
        all.insert(all.end(), rs[static_cast<std::size_t>(r)].begin(), rs[static_cast<std::size_t>(r)].end());
        nr += reads[static_cast<std::size_t>(r)];
        nt += torn[static_cast<std::size_t>(r)];
        for (int k = 0; k < 6; k++) hs[static_cast<std::size_t>(k)] += hist[static_cast<std::size_t>(r)][static_cast<std::size_t>(k)];
    }
    const Pct rp = pct(all), wp = pct(wlat);
    const char *rl = rate == 0 ? "0" : rate == 100000 ? "100k" : rate == 1000000 ? "1M" : "max";
    std::printf("%-8s R=%d w=%-4s | read ns p50 %6.1f p99 %7.1f p99.9 %8.1f max %9.0f | Mreads/s %6.1f | writes %8llu w_ns p50 %5.0f p99 %6.0f max %8.0f | torn %llu",
                kScheme[sc], R, rl, rp.p50, rp.p99, rp.p999, rp.mx, static_cast<double>(nr) / (ms * 1000.0),
                (unsigned long long)writes, wp.p50, wp.p99, wp.mx, (unsigned long long)nt);
    if (sc == SEQ) {
        const double tot = static_cast<double>(hs[0] + hs[1] + hs[2] + hs[3] + hs[4] + hs[5]);
        std::printf(" | retry%% 0:%.2f 1:%.2f 2:%.2f 3-7:%.2f 8-63:%.2f 64+:%.3f", 100 * hs[0] / tot, 100 * hs[1] / tot,
                    100 * hs[2] / tot, 100 * hs[3] / tot, 100 * hs[4] / tot, 100 * hs[5] / tot);
    }
    std::printf("\n");
}

int main(int argc, char **argv) {
    const int ms = argc > 1 ? std::atoi(argv[1]) : 300;
    seq_store(make(0)); g_rw.q = make(0); g_mx.q = make(0);
    for (int R : {1, 4})
        for (long rate : {0L, 100000L, 1000000L, -1L})
            for (Scheme sc : {SEQ, RW, MX}) run(sc, R, rate, ms);
}
