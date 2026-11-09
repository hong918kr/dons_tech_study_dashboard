// LLAT-A06 demo_bench — 호출당 지연 (p50/p99/p99.9) + 버스트 처리량·drop 수. 출력만 한다 (CHECK 없음).
//
//   (1) 호출당 지연: nlog(이 레슨) / snprintf 를 스택 버퍼에 / fprintf 를 파일에 / spdlog 동기 파일 / spdlog 비동기
//       라운드마다 20000 호출, 라운드 사이에 소비자가 다 비우게 기다린다 (버퍼가 차지 않은 정상 상태의 지연).
//       시계 = cntvct_el0 (1 GHz 단위로 보고되지만 이 맥에서 실제 갱신은 ~42 ns 마다) → 한 호출 백분위는
//       42 ns 눈금으로 양자화된다. 평균은 시계 없이 돈 루프 전체 시간 / 호출 수 (양자화 없음).
//   (2) 버스트: 100만 개를 쉬지 않고 → 생산자 처리량, drop 수, 소비자가 다 쓸 때까지 걸린 시간.
//
// build: c++ -std=c++20 -O2 -pthread demo_bench.cpp -o bench && ./bench
// spdlog 포함 (설치하지 않고 헤더만):
//   git clone --depth 1 --branch v1.17.0 https://github.com/gabime/spdlog /tmp/LLAT-A06_work/spdlog
//   c++ -std=c++20 -O2 -pthread -DHAVE_SPDLOG -I/tmp/LLAT-A06_work/spdlog/include demo_bench.cpp -o bench && ./bench
#define NLOG_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"
#include "solution.cpp"

#include <algorithm>
#include <functional>
#ifdef HAVE_SPDLOG
#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/spdlog.h"
#endif

#define FMT_PF "order id=%d px=%.2f qty=%u sym=%s"
static inline std::uint64_t tk() {
    std::uint64_t v;
    asm volatile("mrs %0, cntvct_el0" : "=r"(v) : : "memory");
    return v;
}
static volatile std::uint64_t g_sink_u64;

struct Row { const char *name; double mean; std::uint64_t p50, p99, p999, mx; };
static std::vector<Row> g_rows;

// call(i) 를 rounds × per 번. 홀수 라운드 = 호출마다 시계, 짝수 라운드 = 시계 없이 평균만.
static void measure(const char *name, int rounds, int per, const std::function<void(int)> &call, const std::function<void()> &settle) {
    std::vector<std::uint32_t> lat;
    lat.reserve(static_cast<std::size_t>(rounds) * per);
    double sum_ns = 0;
    long n_mean = 0;
    for (int r = 0; r < 2 * rounds; r++) {
        if (r & 1) {
            for (int i = 0; i < per; i++) {
                const std::uint64_t a = tk();
                call(i);
                const std::uint64_t b = tk();
                lat.push_back(static_cast<std::uint32_t>(b - a));
            }
        } else {
            const std::uint64_t a = tk();
            for (int i = 0; i < per; i++) call(i);
            sum_ns += static_cast<double>(tk() - a);
            n_mean += per;
        }
        settle();
    }
    std::sort(lat.begin(), lat.end());
    auto pct = [&](double p) { return static_cast<std::uint64_t>(lat[static_cast<std::size_t>(p * static_cast<double>(lat.size() - 1))]); };
    g_rows.push_back({name, sum_ns / static_cast<double>(n_mean), pct(0.50), pct(0.99), pct(0.999), lat.back()});
}

static void wait_drained(Logger &lg) {
    for (;;) {
        bool empty = true;
        for (std::uint32_t i = 0; i < lg.nrings(); i++)
            if (lg.ring_at(i).head.load() != lg.ring_at(i).tail.load()) empty = false;
        if (empty) return;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

int main(int argc, char **argv) {
    const int rounds = argc > 1 ? std::atoi(argv[1]) : 10, per = 20000;
    std::printf("per-call latency: %d timed rounds x %d calls each, msg = \"%s\"\n", rounds, per, FMT_PF);

    // ---- nlog: 텍스트 싱크 → 파일, 백그라운드 소비자, 링 4 MB, DROP ----
    {
        std::FILE *f = std::fopen("/tmp/LLAT-A06_bench_nlog.log", "w");
        TextSink sink;
        sink.file = f;
        Logger::Config c;
        c.ring_bytes = 4u << 20;
        Logger lg(c, sink);
        lg.start();
        measure("nlog (this lesson)", rounds, per, [&](int i) {
            NLOG(lg, INF, FMT_PF, i, 1234.5 + i * 0.01, static_cast<unsigned>(i & 1023), "AAPL");
        }, [&] { wait_drained(lg); });
        lg.stop();
        std::uint64_t d = 0;
        for (std::uint32_t i = 0; i < lg.nrings(); i++) d += lg.ring_at(i).dropped.load();
        std::printf("  nlog drops during latency test: %" PRIu64 "\n", d);
        std::fclose(f);
    }
    // ---- snprintf 를 스택 버퍼에 (포맷 비용만, I/O 없음) ----
    measure("snprintf -> stack buf", rounds, per, [&](int i) {
        char b[256];
        const int n = std::snprintf(b, sizeof b, "[%" PRIu64 "] I " FMT_PF "\n", now_ticks(), i, 1234.5 + i * 0.01, static_cast<unsigned>(i & 1023), "AAPL");
        g_sink_u64 = static_cast<std::uint64_t>(n) + static_cast<unsigned char>(b[5]);
    }, [] {});
    // ---- fprintf 를 파일에 (stdio 버퍼 + 잠금 + 가끔 write(2)) ----
    {
        std::FILE *f = std::fopen("/tmp/LLAT-A06_bench_fprintf.log", "w");
        measure("fprintf -> file", rounds, per, [&](int i) {
            std::fprintf(f, "[%" PRIu64 "] I " FMT_PF "\n", now_ticks(), i, 1234.5 + i * 0.01, static_cast<unsigned>(i & 1023), "AAPL");
        }, [] {});
        std::fclose(f);
    }
#ifdef HAVE_SPDLOG
    {
        auto lg = spdlog::basic_logger_st("sync", "/tmp/LLAT-A06_bench_spdlog_sync.log", true);
        measure("spdlog sync file (st)", rounds, per, [&](int i) {
            lg->info("order id={} px={:.2f} qty={} sym={}", i, 1234.5 + i * 0.01, static_cast<unsigned>(i & 1023), "AAPL");
        }, [] {});
        lg->flush();
    }
    {
        spdlog::init_thread_pool(65536, 1);
        auto lg = spdlog::basic_logger_mt<spdlog::async_factory>("async", "/tmp/LLAT-A06_bench_spdlog_async.log", true);
        auto tp = spdlog::thread_pool();
        measure("spdlog async (q 65536, block)", rounds, per, [&](int i) {
            lg->info("order id={} px={:.2f} qty={} sym={}", i, 1234.5 + i * 0.01, static_cast<unsigned>(i & 1023), "AAPL");
        }, [&] { while (tp->queue_size()) std::this_thread::sleep_for(std::chrono::microseconds(200)); });
        lg->flush();
        spdlog::drop_all();
    }
    std::printf("spdlog version %d.%d.%d (header-only, -DHAVE_SPDLOG)\n", SPDLOG_VER_MAJOR, SPDLOG_VER_MINOR, SPDLOG_VER_PATCH);
#else
    std::printf("spdlog: not built (add -DHAVE_SPDLOG -I.../spdlog/include)\n");
#endif
    std::printf("\n%-30s %9s %8s %8s %8s %9s\n", "logger", "mean ns", "p50", "p99", "p99.9", "max");
    for (const Row &r : g_rows)
        std::printf("%-30s %9.1f %8" PRIu64 " %8" PRIu64 " %8" PRIu64 " %9" PRIu64 "\n", r.name, r.mean, r.p50, r.p99, r.p999, r.mx);
    std::printf("(p50..max = single-call cntvct deltas in ns, ~42 ns granularity on this Mac)\n");

    // ---- (2) 버스트: 100만 개 쉬지 않고 ----
    const int N = 1'000'000;
    std::printf("\nburst: %d msgs back-to-back from 1 thread, ring 1 MB (record 48 B -> ~21.8k records)\n", N);
    std::printf("%-32s %8s %8s %8s %10s %6s %7s %8s\n", "config", "prod ms", "Mmsg/s", "dropped", "drained ms", "p50", "p99.9", "max");
    std::vector<std::uint32_t> bl(static_cast<std::size_t>(N));
    auto tails = [&](std::uint64_t &p50, std::uint64_t &p999, std::uint64_t &mx) {
        std::sort(bl.begin(), bl.end());
        p50 = bl[bl.size() / 2];
        p999 = bl[static_cast<std::size_t>(0.999 * static_cast<double>(bl.size() - 1))];
        mx = bl.back();
    };
    auto burst = [&](const char *name, Policy pol, bool binary) {
        std::FILE *f = std::fopen(binary ? "/tmp/LLAT-A06_bench_burst.bin" : "/tmp/LLAT-A06_bench_burst.log", "w");
        TextSink ts;
        ts.file = f;
        BinarySink bs;
        Sink &sink = binary ? static_cast<Sink &>(bs) : static_cast<Sink &>(ts);
        Logger::Config c;
        c.ring_bytes = 1u << 20;
        c.policy = pol;
        c.block_timeout = std::chrono::seconds(5);
        Logger lg(c, sink);
        lg.start();
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; i++) {
            const std::uint64_t a = tk();
            NLOG(lg, INF, FMT_PF, i, 1234.5 + i * 0.01, static_cast<unsigned>(i & 1023), "AAPL");
            bl[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(tk() - a);
        }
        const auto t1 = std::chrono::steady_clock::now();
        lg.stop();
        const auto t2 = std::chrono::steady_clock::now();
        if (binary) std::fwrite(bs.bytes.data(), 1, bs.bytes.size(), f);
        std::fclose(f);
        const double pm = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double dm = std::chrono::duration<double, std::milli>(t2 - t0).count();
        std::uint64_t p50, p999, mx;
        tails(p50, p999, mx);
        std::printf("%-32s %8.1f %8.1f %8" PRIu64 " %10.1f %6" PRIu64 " %7" PRIu64 " %8" PRIu64 "\n", name, pm, N / pm / 1e3,
                    lg.ring_at(0).dropped.load(), dm, p50, p999, mx);
    };
    burst("nlog DROP, text sink", Policy::Drop, false);
    burst("nlog BLOCK, text sink", Policy::Block, false);
    burst("nlog DROP, binary sink", Policy::Drop, true);
    burst("nlog BLOCK, binary sink", Policy::Block, true);
#ifdef HAVE_SPDLOG
    for (int pol = 0; pol < 2; pol++) {
        spdlog::init_thread_pool(8192, 1);
        auto lg = spdlog::basic_logger_mt<spdlog::async_factory>("burst", "/tmp/LLAT-A06_bench_spdlog_burst.log", true);
        if (pol == 1) {   // discard_new 로 같은 sink 를 쓰는 비동기 로거
            spdlog::drop("burst");
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("/tmp/LLAT-A06_bench_spdlog_burst.log", true);
            lg = std::make_shared<spdlog::async_logger>("burst", sink, spdlog::thread_pool(), spdlog::async_overflow_policy::discard_new);
        }
        auto tp = spdlog::thread_pool();
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; i++) {
            const std::uint64_t a = tk();
            lg->info("order id={} px={:.2f} qty={} sym={}", i, 1234.5 + i * 0.01, static_cast<unsigned>(i & 1023), "AAPL");
            bl[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(tk() - a);
        }
        const auto t1 = std::chrono::steady_clock::now();
        while (tp->queue_size()) std::this_thread::sleep_for(std::chrono::microseconds(200));
        lg->flush();
        const auto t2 = std::chrono::steady_clock::now();
        const double pm = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double dm = std::chrono::duration<double, std::milli>(t2 - t0).count();
        std::uint64_t p50, p999, mx;
        tails(p50, p999, mx);
        std::printf("%-32s %8.1f %8.1f %8zu %10.1f %6" PRIu64 " %7" PRIu64 " %8" PRIu64 "\n", pol ? "spdlog async q8192 discard_new" : "spdlog async q8192 block",
                    pm, N / pm / 1e3, tp->discard_counter(), dm, p50, p999, mx);
        lg.reset();
        spdlog::drop_all();
        spdlog::shutdown();
    }
#endif
    return 0;
}
