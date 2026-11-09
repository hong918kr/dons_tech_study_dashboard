// LLAT-M01 demo_bench — v0..v4 처리량 · 왕복 지연 · 배치 지연 · QoS (숫자는 CHECK 하지 않는다)
//
//   ./bench tput  [runs] [M]      생산자 1 → 소비자 1, M 개 (기본 5 runs × 2e7). Mops/s + 메시지당 상대 인덱스 load 수
//   ./bench rtt   [runs]          큐 2 개 왕복(echo). 표본 = 64 왕복 평균 (시계 눈금 41.67 ns 때문 — 5. 풀이)
//   ./bench rtt1                  같은 것을 표본 = 왕복 1 번으로 (눈금이 보이는지)
//   ./bench paced [period_ns]     생산자가 일정 간격으로 메시지를 만들 때 한 방향 지연: 즉시 push vs 배치 vs 지연 상한 배치
//   ./bench qos                   v2 를 QoS 조합(ui/ui, bg/bg, ui/bg)으로 — P/E 코어 유도 효과
//
// solution.cpp 를 그대로 include 해서 레슨 코드와 같은 타입을 잰다 (카운터 포함: 각 쪽 자기 라인의 보통 변수 ++).
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread demo_bench.cpp -o bench
#define LLAT_M01_NO_MAIN
#include "solution.cpp"

#include <cstdlib>
#include <cstring>
#include <mach/mach_time.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <string>
#include <vector>

static mach_timebase_info_data_t g_tb;
static inline uint64_t ticks() { return mach_absolute_time(); }
static double t2ns(double t) { return t * g_tb.numer / g_tb.denom; }
static uint64_t ns2t(uint64_t ns) { return ns * g_tb.denom / g_tb.numer; }
static size_t cpu_now() { size_t c = 0; pthread_cpu_number_np(&c); return c; }
static bool is_e(size_t c) { return c < 6; }   // LLAT-M05 의 관찰(추정): cpu 0–5 = E 코어
static void loadavg(const char *tag) {
    double l[3] = {};
    getloadavg(l, 3);
    std::printf("[load average %s: %.2f %.2f %.2f]\n", tag, l[0], l[1], l[2]);
}
static qos_class_t qos_of(const char *s) {
    if (!std::strcmp(s, "bg")) return QOS_CLASS_BACKGROUND;
    if (!std::strcmp(s, "ut")) return QOS_CLASS_UTILITY;
    return QOS_CLASS_USER_INTERACTIVE;
}

struct ECount { uint64_t e = 0, n = 0; void sample() { n++; e += is_e(cpu_now()); } int pct() const { return n ? int(100 * e / n) : -1; } };

template <class Q> constexpr bool has_stats = requires(Q &q) { q.pstats(); };

// ---------------- 처리량 ----------------
struct Tput { double mops; double hl, tl, ts, hs; int ep, ec; };

template <class Q, int Mode>
static Tput tput_once(uint64_t M, size_t B, qos_class_t qp, qos_class_t qc) {
    auto q = std::make_unique<Q>();
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    ECount ep, ec;
    uint64_t t0 = 0, t1 = 0, sum = 0;
    std::thread prod([&] {
        pthread_set_qos_class_self_np(qp, 0);
        ready++;
        while (!go.load(memory_order_acquire)) {}
        uint64_t buf[64], it = 0;
        for (uint64_t i = 1; i <= M; it++) {
            if ((it & 0xFFFF) == 0) ep.sample();
            if constexpr (Mode == 0) {
                if (q->try_push(i)) i++;
            } else {
                const size_t want = static_cast<size_t>(std::min<uint64_t>(B, M - i + 1));
                for (size_t j = 0; j < want; j++) buf[j] = i + j;
                i += q->push_n(buf, want);
            }
        }
    });
    std::thread cons([&] {
        pthread_set_qos_class_self_np(qc, 0);
        ready++;
        while (!go.load(memory_order_acquire)) {}
        t0 = ticks();
        uint64_t got = 0, buf[64], s = 0, it = 0;
        while (got < M) {
            if ((it++ & 0xFFFF) == 0) ec.sample();
            if constexpr (Mode == 0) {
                if (q->try_pop(buf[0])) { s += buf[0]; got++; }
            } else {
                const size_t k = q->pop_n(buf, B);
                for (size_t j = 0; j < k; j++) s += buf[j];
                got += k;
            }
        }
        t1 = ticks();
        sum = s;
    });
    while (ready.load() != 2) {}
    go.store(true, memory_order_release);
    prod.join();
    cons.join();
    if (sum != M * (M + 1) / 2) std::printf("!! sum mismatch\n");
    Tput r{double(M) / (t2ns(double(t1 - t0)) / 1e3), 0, 0, 0, 0, ep.pct(), ec.pct()};
    if constexpr (has_stats<Q>) {
        r.hl = double(q->pstats().head_loads) / M; r.ts = double(q->pstats().tail_stores) / M;
        r.tl = double(q->cstats().tail_loads) / M; r.hs = double(q->cstats().head_stores) / M;
    }
    return r;
}

template <class Q, int Mode>
static void tput_row(const char *name, int runs, uint64_t M, size_t B = 32,
                     qos_class_t qp = QOS_CLASS_USER_INTERACTIVE, qos_class_t qc = QOS_CLASS_USER_INTERACTIVE) {
    std::vector<double> v;
    Tput last{};
    std::string eps;
    for (int i = 0; i < runs; i++) {
        last = tput_once<Q, Mode>(M, B, qp, qc);
        v.push_back(last.mops);
        eps += std::to_string(last.ep) + "/" + std::to_string(last.ec) + " ";
    }
    std::sort(v.begin(), v.end());
    std::printf("%-22s med %7.1f  min %7.1f  max %7.1f Mops/s | per msg: head_ld %.4f tail_ld %.4f tail_st %.4f head_st %.4f | E%% p/c %s\n",
                name, v[v.size() / 2], v.front(), v.back(), last.hl, last.tl, last.ts, last.hs, eps.c_str());
}

// ---------------- 왕복 지연 ----------------
struct Pct { double p50, p99, p999, mx; };
static Pct pct(std::vector<double> &v) {
    std::sort(v.begin(), v.end());
    auto at = [&](double p) { size_t i = size_t(p * double(v.size())); return v[std::min(i, v.size() - 1)]; };
    return {at(0.50), at(0.99), at(0.999), v.back()};
}

template <class Q, bool Batch>
static Pct rtt_once(size_t samples, size_t batch, qos_class_t qp, qos_class_t qe, int &ep, int &ee) {
    auto a = std::make_unique<Q>(), b = std::make_unique<Q>();
    std::vector<double> out;
    out.reserve(samples);
    ECount cp, ce;
    std::atomic<bool> stop{false};
    std::thread echo([&] {
        pthread_set_qos_class_self_np(qe, 0);
        uint64_t buf[32];
        for (uint64_t done = 0; !stop.load(memory_order_relaxed);) {
            size_t k;
            if constexpr (Batch) k = a->pop_n(buf, 32);
            else k = a->try_pop(buf[0]) ? 1 : 0;
            if (k == 0) continue;
            if ((done++ & 4095) == 0) ce.sample();
            for (size_t j = 0; j < k; j++) buf[j]++;
            if constexpr (Batch) { size_t s = 0; while (s < k) s += b->push_n(buf + s, k - s); }
            else while (!b->try_push(buf[0])) {}
        }
    });
    std::thread ping([&] {
        pthread_set_qos_class_self_np(qp, 0);
        const size_t warm = samples / 10;
        uint64_t x = 0, y = 0;
        for (size_t s = 0; s < samples + warm; s++) {
            const uint64_t t0 = ticks();
            for (size_t j = 0; j < batch; j++, x += 2) {
                while (!a->try_push(x)) {}
                while (!b->try_pop(y)) {}
            }
            const uint64_t t1 = ticks();
            if (s >= warm) out.push_back(t2ns(double(t1 - t0)) / double(batch));
            if ((s & 63) == 0) cp.sample();
        }
        if (y != x - 1) std::printf("!! echo mismatch\n");
        stop.store(true);
    });
    ping.join();
    echo.join();
    ep = cp.pct();
    ee = ce.pct();
    return pct(out);
}

template <class Q, bool Batch>
static void rtt_row(const char *name, int runs, size_t samples, size_t batch,
                    qos_class_t qp = QOS_CLASS_USER_INTERACTIVE, qos_class_t qe = QOS_CLASS_USER_INTERACTIVE) {
    std::printf("%-22s", name);
    for (int i = 0; i < runs; i++) {
        int ep, ee;
        const Pct p = rtt_once<Q, Batch>(samples, batch, qp, qe, ep, ee);
        std::printf(" | p50 %6.1f p99 %6.1f p99.9 %7.1f E%%%d/%d", p.p50, p.p99, p.p999, ep, ee);
    }
    std::printf("\n");
}

// ---------------- 일정 간격 생산자의 한 방향 지연 (배치가 지연을 해치는 경우) ----------------
// mode 0: v2 try_push 즉시 / 1: BatchWriter(16, 무한 대기) / 2: BatchWriter(16, max_delay) / 3: BatchWriter(64, max_delay)
static void paced_row(int mode, uint64_t period_ns, uint64_t delay_ns, uint64_t M) {
    using Q = SpscV2<uint64_t, 1024>;
    auto q = std::make_unique<Q>();
    std::vector<double> lat;
    lat.reserve(M);
    std::atomic<bool> go{false};
    uint64_t pops = 0;
    std::thread cons([&] {
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
        go.store(true);
        uint64_t buf[64], got = 0;
        while (got < M) {
            const size_t k = q->pop_n(buf, 64);
            if (k == 0) continue;
            const uint64_t now = ticks();
            pops++;
            for (size_t j = 0; j < k; j++) lat.push_back(t2ns(double(now - buf[j])));
            got += k;
        }
    });
    std::thread prod([&] {
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
        while (!go.load()) {}
        const uint64_t per = ns2t(period_ns), dly = mode == 1 ? UINT64_MAX / 2 : ns2t(delay_ns);
        const size_t mb = mode == 3 ? 64 : 16;
        BatchWriter<Q, uint64_t, 64> w(*q, mb, dly);
        uint64_t next = ticks();
        for (uint64_t i = 0; i < M; i++) {
            next += per;
            for (uint64_t now; (now = ticks()) < next;) if (mode >= 2) w.poll(now);
            const uint64_t gen = ticks();                    // 메시지가 "생긴" 시각
            if (mode == 0) { while (!q->try_push(gen)) {} }
            else while (!w.add(gen, gen)) w.flush();
        }
        while (w.n != 0) w.flush();                          // 끝: 남은 것 내보내기 (무한 대기 판도)
    });
    prod.join();
    cons.join();
    const Pct p = pct(lat);
    static const char *names[] = {"v2 immediate", "batch16 no-timeout", "batch16 max_delay", "batch64 max_delay"};
    std::printf("%-20s p50 %8.1f p99 %8.1f p99.9 %8.1f max %9.1f ns | publishes/msg %.3f  consumer pops/msg %.3f\n",
                names[mode], p.p50, p.p99, p.p999, p.mx, double(q->pstats().tail_stores) / double(M),
                double(pops) / double(M));
}

int main(int argc, char **argv) {
    mach_timebase_info(&g_tb);
    const std::string mode = argc > 1 ? argv[1] : "tput";
    std::printf("timebase %u/%u (1 tick = %.2f ns), hw_destructive_interference_size = %zu, kLine = %zu\n",
                g_tb.numer, g_tb.denom, t2ns(1), std::hardware_destructive_interference_size, kLine);
    using V0 = SpscV0<uint64_t, 1024>;
    using V1 = SpscV1<uint64_t, 1024>;
    using V1w = SpscRing<uint64_t, 1024, 256, false>;   // hardware_destructive_interference_size(256) 로 패딩
    using V2 = SpscV2<uint64_t, 1024>;
    using V2u = SpscRing<uint64_t, 1024, 0, true>;     // 캐시는 하지만 패딩 없음 (상호작용 확인용)
    using V4 = SpscSlot<uint64_t, 1024>;
    if (mode == "tput") {
        const int runs = argc > 2 ? std::atoi(argv[2]) : 5;
        const uint64_t M = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 20'000'000;
        loadavg("before");
        {
            tput_row<V0, 0>("v0 adjacent", runs, M);
            tput_row<V1, 0>("v1 pad128", runs, M);
            tput_row<V1w, 0>("v1w pad256 (hdis)", runs, M);
            tput_row<V2u, 0>("v2u cached, no pad", runs, M);
            tput_row<V2, 0>("v2 cached idx", runs, M);
            tput_row<V2, 1>("v3 batch 8", runs, M, 8);
            tput_row<V2, 1>("v3 batch 32", runs, M, 32);
            tput_row<V4, 0>("v4 slot seq", runs, M);
        }
        loadavg("after");
    } else if (mode == "rtt" || mode == "rtt1") {
        const int runs = argc > 2 ? std::atoi(argv[2]) : 3;
        const size_t batch = mode == "rtt1" ? 1 : 64;
        const size_t samples = mode == "rtt1" ? 400'000 : 20'000;
        std::printf("ns per round trip, sample = %zu round trips, %zu samples (+10%% warm-up dropped)\n", batch, samples);
        loadavg("before");
        if (mode == "rtt1") {
            rtt_row<V2, false>("v2 cached idx", runs, samples, batch);
        } else {
            rtt_row<V0, false>("v0 adjacent", runs, samples, batch);
            rtt_row<V1, false>("v1 pad128", runs, samples, batch);
            rtt_row<V1w, false>("v1w pad256 (hdis)", runs, samples, batch);
            rtt_row<V2u, false>("v2u cached, no pad", runs, samples, batch);
            rtt_row<V2, false>("v2 cached idx", runs, samples, batch);
            rtt_row<V2, true>("v3 batch (echo pop_n)", runs, samples, batch);
            rtt_row<V4, false>("v4 slot seq", runs, samples, batch);
        }
        loadavg("after");
    } else if (mode == "paced") {
        const uint64_t period = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1000;
        const uint64_t M = 200'000;
        std::printf("producer makes 1 msg every %llu ns, %llu msgs, max_delay = 2000 ns; latency = consumer pop time - msg creation\n",
                    (unsigned long long)period, (unsigned long long)M);
        loadavg("before");
        for (int m = 0; m < 4; m++) paced_row(m, period, 2000, M);
        loadavg("after");
    } else if (mode == "qos") {
        loadavg("before");
        const char *combos[][2] = {{"ui", "ui"}, {"ut", "ut"}, {"bg", "bg"}, {"ui", "bg"}};
        for (auto &c : combos) {
            char name[40];
            std::snprintf(name, sizeof name, "v2 rtt %s/%s", c[0], c[1]);
            rtt_row<V2, false>(name, 2, 10'000, 64, qos_of(c[0]), qos_of(c[1]));
        }
        for (auto &c : combos) {
            char name[40];
            std::snprintf(name, sizeof name, "v2 tput %s/%s", c[0], c[1]);
            tput_row<V2, 0>(name, 3, 10'000'000, 32, qos_of(c[0]), qos_of(c[1]));
        }
        loadavg("after");
    } else {
        std::printf("usage: %s tput|rtt|rtt1|paced|qos\n", argv[0]);
        return 2;
    }
    return 0;
}
