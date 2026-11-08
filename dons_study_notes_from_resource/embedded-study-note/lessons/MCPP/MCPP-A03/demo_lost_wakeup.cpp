// MCPP-A03 demo: atomic wait/notify 로도 깨움 분실(lost wakeup)을 "직접" 만들 수 있다 — 네 가지 실수.
//   ① 래치 wait 에 "다시 읽은 값" 을 old 로 넘김        (강제 스케줄: 검사 뒤·wait 앞에 count_down 이 끼어든다)
//   ② 세마포어: "0→1 일 때만 notify_one"               (libc++ 은 같은 조건에 notify_all 을 쓴다 — 이유가 이것)
//   ③ 세마포어: 대기자 수를 count 증가 "전에" 읽음       (강제 스케줄: 읽은 뒤 대기자가 잠든 다음 증가)
//   ④ 대기자 카운트를 relaxed 로 (SB 모양)             (강제 불가 — 스트레스로 몇 번 걸리는지 센다)
// 영원히 잠든 스레드는 감시 쪽이 rescued 플래그 + notify_all 로 깨워 "stuck" 으로 센다 (멈추지 않는다).
// build: c++ -std=c++20 -O2 -pthread demo_lost_wakeup.cpp -o dlw && ./dlw
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

using i64 = std::int64_t;
using namespace std::chrono_literals;
using clk = std::chrono::steady_clock;
static double us_since(clk::time_point t0) { return std::chrono::duration<double, std::micro>(clk::now() - t0).count(); }

// ---------- ① 래치: old 로 무엇을 넘기나 ----------
struct DemoLatch {
    std::atomic<i64> c;
    std::atomic<bool> rescued{false};
    void count_down() { if (c.fetch_sub(1, std::memory_order_release) == 1) c.notify_all(); }
    template <class Hook> void wait_good(Hook hook) {          // 검사에 쓴 값 v 를 그대로 old 로
        for (i64 v = c.load(std::memory_order_acquire); v != 0 && !rescued; v = c.load(std::memory_order_acquire)) {
            hook();
            c.wait(v, std::memory_order_acquire);
        }
    }
    template <class Hook> void wait_bad(Hook hook) {           // 검사 뒤 "다시 읽은" 값을 old 로 — 틀림
        while (c.load(std::memory_order_acquire) != 0 && !rescued) {
            hook();
            c.wait(c.load(std::memory_order_acquire), std::memory_order_acquire);
        }
    }
};
static void demo1() {
    for (int bad = 0; bad < 2; bad++) {
        DemoLatch l{{1}};
        bool fired = false;
        auto hook = [&] { if (!fired) { fired = true; l.count_down(); } };   // 검사(1 을 봄) 직후 마지막 count_down
        std::thread rescuer([&] {
            std::this_thread::sleep_for(200ms);
            if (l.c.load() == 0 && !l.rescued.exchange(true)) { l.c.store(99); l.c.notify_all(); }
        });
        auto t0 = clk::now();
        if (bad) l.wait_bad(hook); else l.wait_good(hook);
        double us = us_since(t0);
        bool stuck = l.rescued.load();
        l.rescued.store(true);                                 // 아직 안 깨운 rescuer 는 아무것도 안 하게
        rescuer.join();
        std::printf("① %-28s : wait 반환까지 %9.1f us  %s\n", bad ? "wait(c.load())  (다시 읽음)" : "wait(v)  (검사한 값)",
                    us, stuck ? "→ STUCK, 감시 쪽이 200 ms 뒤 구출" : "→ 정상");
    }
}

// ---------- ②③④ 세마포어 변형 ----------
enum Kind { OLD0_NOTIFY_ONE, OLD0_NOTIFY_ALL, CHECK_BEFORE_ADD, WAITERS_SEQCST, WAITERS_RELAXED };
struct DemoSem {
    std::atomic<i64> c{0}, waiters{0};
    std::atomic<bool> rescued{false};
    Kind k;
    explicit DemoSem(Kind kind) : k(kind) {}
    bool try_acquire() {
        i64 v = c.load(std::memory_order_relaxed);
        while (v > 0)
            if (c.compare_exchange_weak(v, v - 1, std::memory_order_acquire, std::memory_order_relaxed)) return true;
        return false;
    }
    bool acquire() {                                           // false = 구출당함
        while (!try_acquire()) {
            if (rescued.load()) return false;
            std::memory_order mo = k == WAITERS_RELAXED ? std::memory_order_relaxed : std::memory_order_seq_cst;
            waiters.fetch_add(1, mo);
            c.wait(0, mo);
            waiters.fetch_sub(1, mo);
        }
        return true;
    }
    template <class Hook> void release(Hook between) {
        switch (k) {
        case OLD0_NOTIFY_ONE: if (c.fetch_add(1, std::memory_order_release) == 0) c.notify_one(); break;
        case OLD0_NOTIFY_ALL: if (c.fetch_add(1, std::memory_order_release) == 0) c.notify_all(); break;
        case CHECK_BEFORE_ADD: {
            bool w = waiters.load() != 0;                      // 먼저 읽고
            between();                                         // (강제 스케줄: 여기서 대기자가 등록하고 잠든다)
            c.fetch_add(1);                                    // 나중에 증가
            if (w) c.notify_one();
            break;
        }
        case WAITERS_SEQCST:
        case WAITERS_RELAXED: {
            std::memory_order mo = k == WAITERS_RELAXED ? std::memory_order_relaxed : std::memory_order_seq_cst;
            c.fetch_add(1, k == WAITERS_RELAXED ? std::memory_order_release : std::memory_order_seq_cst);
            if (waiters.load(mo) != 0) c.notify_one();
            break;
        }
        }
    }
    void rescue() { rescued.store(true); c.fetch_add(1000); c.notify_all(); }
};

// ② 대기자 2 개가 확실히 잠든 뒤 release 두 번 연달아 → 둘 다 깨어나야 한다
static void demo2(Kind k, const char *name) {
    int stuck = 0, trials = 10;
    for (int t = 0; t < trials; t++) {
        DemoSem s(k);
        std::atomic<int> got{0};
        std::thread a([&] { if (s.acquire()) got.fetch_add(1); }), b([&] { if (s.acquire()) got.fetch_add(1); });
        std::this_thread::sleep_for(30ms);
        s.release([] {}); s.release([] {});
        std::this_thread::sleep_for(100ms);
        if (got.load() < 2) { stuck++; s.rescue(); }
        a.join(); b.join();
    }
    std::printf("② %-34s : %2d/%d 회 대기자 하나가 영원히 잠듦\n", name, stuck, trials);
}

// ③ release 가 대기자 수를 읽은 "뒤", 대기자가 등록·잠든 "다음" 에 증가
static void demo3() {
    int stuck = 0, trials = 5;
    for (int t = 0; t < trials; t++) {
        DemoSem s(CHECK_BEFORE_ADD);
        std::atomic<int> got{0};
        std::thread w;
        s.release([&] {
            w = std::thread([&] { if (s.acquire()) got.fetch_add(1); });
            while (s.waiters.load() == 0) std::this_thread::yield();
            std::this_thread::sleep_for(30ms);                 // 4 µs 스핀을 넘겨 커널에서 자게
        });
        std::this_thread::sleep_for(100ms);
        if (got.load() == 0) { stuck++; s.rescue(); }
        w.join();
    }
    std::printf("③ %-34s : %2d/%d 회 대기자가 영원히 잠듦\n", "waiters 를 count 증가 전에 읽음", stuck, trials);
}

// ④ 핑퐁 스트레스: 작업자는 ack 하자마자 다시 acquire, 메인은 ack 를 보자마자 release → 등록 창이 계속 겹친다
static void demo4(Kind k, const char *name) {
    constexpr int R = 200000;
    DemoSem s(k);
    std::atomic<int> ack{0};
    std::atomic<bool> stop{false};
    std::thread w([&] {
        while (!stop.load()) {
            if (!s.acquire()) { s.rescued.store(false); continue; }
            ack.fetch_add(1);
        }
    });
    int stuck = 0;
    auto t0 = clk::now();
    for (int r = 0; r < R; r++) {
        s.release([] {});
        auto d = clk::now() + 50ms;
        while (ack.load() <= r) {
            if (clk::now() > d) {                              // 50 ms 동안 못 깨어남 = 깨움 분실로 센다
                stuck++;
                s.c.notify_all();                              // 허가는 이미 있다 — 깨우기만
                d = clk::now() + 50ms;
            }
        }
    }
    stop.store(true); s.rescue(); w.join();
    std::printf("④ %-34s : %d 라운드 중 깨움 분실 %d 회  (%.0f ms)\n", name, R, stuck, us_since(t0) / 1000);
}

int main() {
    demo1();
    demo2(OLD0_NOTIFY_ONE, "0→1 일 때만 notify_one");
    demo2(OLD0_NOTIFY_ALL, "0→1 일 때만 notify_all (libc++)");
    demo2(WAITERS_SEQCST, "대기자 카운트 + notify_one (정답)");
    demo3();
    demo4(WAITERS_SEQCST, "대기자 카운트 seq_cst (정답)");
    demo4(WAITERS_RELAXED, "대기자 카운트 relaxed (SB 위험)");
}
