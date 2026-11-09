// ROS-M03 demo: 탐지기를 끄고(실제 rclcpp 처럼 시간 제한만) 진짜 벽시계 타이머로 돌린다. 숫자는 CHECK 에 넣지 않는다.
//   표 1  타이머 콜백 sync 호출의 6칸 + async 1칸 (20 ms 타이머, 약 1 ms 응답 지연, sync 예산 200 ms, 칸당 1초)
//   표 2  옆 콜백이 sync 로 막혀 있을 때 5 ms 제어 타이머의 최대 간격
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread demo_matrix.cpp -o dm && ./dm
#define ROS_M03_NO_MAIN
#include "solution.cpp"

#include <algorithm>
#include <atomic>

class Ticker {
public:
    Ticker(Executor &ex, int timer, ms period) : th_([&ex, timer, period, this] {
        auto next = Clock::now() + period;
        while (!stop_.load()) {
            std::this_thread::sleep_until(next);
            next += period;
            ex.trigger_timer(timer);
        }
    }) {}
    void stop() { stop_.store(true); if (th_.joinable()) th_.join(); }
    ~Ticker() { stop(); }
private:
    std::atomic<bool> stop_{false};
    std::thread th_;
};

// 외부 서버의 지연: 붙잡힌 응답을 약 1 ms 마다 풀어 준다.
class Net {
public:
    explicit Net(Executor &ex) : th_([&ex, this] {
        while (!stop_.load()) { ex.deliver_held(); std::this_thread::sleep_for(ms(1)); }
        ex.deliver_held();
    }) {}
    void stop() { stop_.store(true); if (th_.joinable()) th_.join(); }
    ~Net() { stop(); }
private:
    std::atomic<bool> stop_{false};
    std::thread th_;
};

static const ms kRun{1000};

struct Row1 { int runs = 0, ok = 0, timeouts = 0, orphans = 0; };

static Row1 table1(int threads, Arrange arr, bool use_async) {
    Executor ex(false);   // 구조 탐지 끔
    Group *a = ex.create_group("node_default", arr == Arrange::SameReentrant ? GroupType::Reentrant : GroupType::MutuallyExclusive);
    Group *b = arr == Arrange::Separate ? ex.create_group("client_cb", GroupType::MutuallyExclusive) : a;
    int svc = ex.create_service("add", nullptr, [](int x) { return x + 1; });
    int cli = ex.create_client("cli", b, svc);
    std::atomic<int> runs{0}, ok{0}, to{0};
    int t = ex.create_timer("tick", a, [&] {
        runs++;
        if (use_async) { ex.async_send(cli, 1, [&](int) { ok++; }); return; }
        CallResult r = ex.call_sync(cli, 1, ms(200));
        if (r.status == CallStatus::Ok) ok++; else to++;
    });
    ex.set_hold_external(true);
    ex.set_keep_alive(true);
    Net net(ex);
    Ticker tick(ex, t, ms(20));
    std::thread stopper([&] { std::this_thread::sleep_for(kRun); tick.stop(); ex.set_keep_alive(false); });
    ex.spin(threads, ms(5000));
    stopper.join();
    net.stop();
    return Row1{runs.load(), ok.load(), to.load(), ex.orphans()};
}

struct Row2 { long max_gap_us = 0; int ctrl_runs = 0, ok = 0, timeouts = 0; };

static Row2 table2(int threads, bool separate, bool use_async) {
    Executor ex(false);
    Group *def = ex.create_group("node_default", GroupType::MutuallyExclusive);
    Group *poll_g = separate ? ex.create_group("poll_cb", GroupType::MutuallyExclusive) : def;
    Group *cli_g = separate ? ex.create_group("client_cb", GroupType::MutuallyExclusive) : def;
    Group *ctrl_g = separate ? ex.create_group("ctrl_cb", GroupType::MutuallyExclusive) : def;
    int svc = ex.create_service("add", nullptr, [](int x) { return x + 1; });
    int cli = ex.create_client("cli", cli_g, svc);
    std::atomic<int> ok{0}, to{0}, ctrl_runs{0};
    std::atomic<long> max_gap{0};
    std::mutex mg;
    Clock::time_point last{};
    int ctrl = ex.create_timer("ctrl", ctrl_g, [&] {
        auto now = Clock::now();
        std::lock_guard<std::mutex> lk(mg);
        if (last != Clock::time_point{}) {
            long gap = std::chrono::duration_cast<std::chrono::microseconds>(now - last).count();
            max_gap = std::max(max_gap.load(), gap);
        }
        last = now;
        ctrl_runs++;
    });
    int poll = ex.create_timer("poll", poll_g, [&] {
        if (use_async) { ex.async_send(cli, 1, [&](int) { ok++; }); return; }
        CallResult r = ex.call_sync(cli, 1, ms(30));
        if (r.status == CallStatus::Ok) ok++; else to++;
    });
    ex.set_hold_external(true);
    ex.set_keep_alive(true);
    Net net(ex);
    Ticker t_ctrl(ex, ctrl, ms(5)), t_poll(ex, poll, ms(100));
    std::thread stopper([&] { std::this_thread::sleep_for(ms(1500)); t_ctrl.stop(); t_poll.stop(); ex.set_keep_alive(false); });
    ex.spin(threads, ms(5000));
    stopper.join();
    net.stop();
    return Row2{max_gap.load(), ctrl_runs.load(), ok.load(), to.load()};
}

int main() {
    printf("표 1: 20 ms 타이머, 콜백 안에서 호출, 응답 지연 약 1 ms, sync 예산 200 ms, 칸당 %lld ms, 탐지기 없음\n",
           static_cast<long long>(kRun.count()));
    printf("%-18s %-24s %-6s %6s %6s %8s %8s\n", "executor", "timer/client group", "call", "runs", "ok", "timeout", "orphan");
    struct { int th; Arrange a; bool as; } cells[] = {
        {1, Arrange::SameMutEx, false}, {1, Arrange::SameReentrant, false}, {1, Arrange::Separate, false},
        {2, Arrange::SameMutEx, false}, {2, Arrange::SameReentrant, false}, {2, Arrange::Separate, false},
        {1, Arrange::SameMutEx, true},
    };
    for (auto &c : cells) {
        Row1 r = table1(c.th, c.a, c.as);
        std::string name = c.th == 1 ? "Single" : "Multi(" + std::to_string(c.th) + ")";
        printf("%-18s %-24s %-6s %6d %6d %8d %8d\n", name.c_str(), arrange_name(c.a), c.as ? "async" : "sync", r.runs, r.ok, r.timeouts, r.orphans);
    }
    printf("\n표 2: 5 ms 제어 타이머 + 100 ms 폴 타이머(콜백에서 서비스 호출), sync 예산 30 ms, 1.5초\n");
    printf("%-30s %12s %10s %6s %8s\n", "구성", "max gap(us)", "ctrl runs", "ok", "timeout");
    struct { const char *n; int th; bool sep, as; } rows[] = {
        {"Single, 한 그룹, sync", 1, false, false},
        {"Multi(3), 그룹 분리, sync", 3, true, false},
        {"Single, 한 그룹, async", 1, false, true},
    };
    for (auto &c : rows) {
        Row2 r = table2(c.th, c.sep, c.as);
        printf("%-30s %12ld %10d %6d %8d\n", c.n, r.max_gap_us, r.ctrl_runs, r.ok, r.timeouts);
    }
    return 0;
}
