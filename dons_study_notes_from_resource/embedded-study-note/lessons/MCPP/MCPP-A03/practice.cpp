// MCPP-A03 추가 연습 — atomic wait/notify 로 만드는 프리미티브 네 개 더
//   연습 1  try_acquire_for: atomic::wait 엔 타임아웃이 없다 → 스핀 → yield → sleep 백오프로 폴링 (libc++ 방식)
//   연습 2  재사용 Barrier: 세대(generation) 번호를 "도착 전에" 찍고 그 값에서 잔다 (eventcount 패턴)
//   연습 3  EventGroup: wait-all / wait-any + clear-on-exit (FreeRTOS xEventGroupWaitBits 모양, RTOS-B13)
//   연습 4  Event (단일 대기자 자동 리셋, 0 빈 / 1 게시 / 2 대기) — FreeRTOS 태스크 알림 모양 (RTOS-M01)
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread practice.cpp -o pr && ./pr
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using std::memory_order_relaxed, std::memory_order_acquire, std::memory_order_release, std::memory_order_acq_rel,
    std::memory_order_seq_cst;
using i64 = std::int64_t;
using clk = std::chrono::steady_clock;

// ---- solution.cpp 와 같은 플랫폼 래퍼 ----
struct PlatStats { std::atomic<std::uint64_t> waits{0}, notify_one{0}, notify_all{0}; };
static PlatStats g_st;
static std::atomic<void (*)()> g_before_wait{nullptr};
template <class T> static void plat_wait(const std::atomic<T> &a, T old, std::memory_order mo) {
    g_st.waits.fetch_add(1, memory_order_relaxed);
    if (void (*h)() = g_before_wait.exchange(nullptr)) h();
    a.wait(old, mo);
}
template <class T> static void plat_notify_one(std::atomic<T> &a) { g_st.notify_one.fetch_add(1, memory_order_relaxed); a.notify_one(); }
template <class T> static void plat_notify_all(std::atomic<T> &a) { g_st.notify_all.fetch_add(1, memory_order_relaxed); a.notify_all(); }
static void reset_stats() { g_st.waits.store(0); g_st.notify_one.store(0); g_st.notify_all.store(0); g_before_wait.store(nullptr); }

// ---- solution.cpp 의 Semaphore ----
class Semaphore {
public:
    explicit Semaphore(i64 initial) : count_(initial) {}
    bool try_acquire() {
        i64 c = count_.load(memory_order_relaxed);
        while (c > 0)
            if (count_.compare_exchange_weak(c, c - 1, memory_order_acquire, memory_order_relaxed)) return true;
        return false;
    }
    void acquire() {
        while (!try_acquire()) {
            waiters_.fetch_add(1, memory_order_seq_cst);
            plat_wait(count_, i64{0}, memory_order_seq_cst);
            waiters_.fetch_sub(1, memory_order_seq_cst);
        }
    }
    void release(i64 n = 1) {
        count_.fetch_add(n, memory_order_seq_cst);
        if (waiters_.load(memory_order_seq_cst) == 0) return;
        if (n == 1) plat_notify_one(count_);
        else plat_notify_all(count_);
    }
    i64 value() const { return count_.load(memory_order_relaxed); }
private:
    std::atomic<i64> count_;
    std::atomic<i64> waiters_{0};
};

// ======================= 연습 1 — try_acquire_for =======================
// 64 번 바로 재시도 → 64 번 yield → 그다음은 남은 시간 한도 안에서 50 µs → 100 µs → ... 최대 1 ms 씩 잔다.
static bool sem_try_acquire_for(Semaphore &s, std::chrono::nanoseconds timeout) {
    const auto end = clk::now() + timeout;
    std::chrono::microseconds nap{50};
    for (int i = 0;; i++) {
        if (s.try_acquire()) return true;
        auto now = clk::now();
        if (now >= end) return false;
        if (i < 64) continue;
        if (i < 128) { std::this_thread::yield(); continue; }
        auto left = std::chrono::duration_cast<std::chrono::microseconds>(end - now);
        std::this_thread::sleep_for(std::min(nap, left));
        nap = std::min(nap * 2, std::chrono::microseconds{1000});
    }
}

// ======================= 연습 2 — 재사용 Barrier =======================
class Barrier {
public:
    explicit Barrier(i64 n) : n_(n) {}
    void arrive_and_wait() {
        i64 g = gen_.load(memory_order_acquire);                 // ① 세대를 "도착 전에" 찍는다
        if (arrived_.fetch_add(1, memory_order_acq_rel) + 1 == n_) {
            arrived_.store(0, memory_order_relaxed);             // ② 다음 세대 준비 (gen 의 release 가 게시)
            gen_.fetch_add(1, memory_order_release);
            plat_notify_all(gen_);
            return;
        }
        for (i64 cur = gen_.load(memory_order_acquire); cur == g; cur = gen_.load(memory_order_acquire))
            plat_wait(gen_, g, memory_order_acquire);           // ③ 찍어 둔 g 에서 잔다
    }
    i64 generation() const { return gen_.load(memory_order_relaxed); }
private:
    const i64 n_;
    std::atomic<i64> arrived_{0};
    std::atomic<i64> gen_{0};
};

// ======================= 연습 3 — EventGroup =======================
class EventGroup {
public:
    void set(i64 bits) {
        bits_.fetch_or(bits, memory_order_seq_cst);
        if (waiters_.load(memory_order_seq_cst) != 0) plat_notify_all(bits_);   // 조건이 대기자마다 달라서 all
    }
    // 조건(all: 전부 / any: 하나라도)이 맞을 때의 비트를 돌려준다. clear 면 mask 비트를 원자적으로 지우고 반환.
    i64 wait(i64 mask, bool all, bool clear) {
        i64 v = bits_.load(memory_order_acquire);
        for (;;) {
            bool ok = all ? (v & mask) == mask : (v & mask) != 0;
            if (ok) {
                if (!clear || bits_.compare_exchange_weak(v, v & ~mask, memory_order_acq_rel, memory_order_acquire)) return v;
                continue;                                        // CAS 실패: v 가 새 값으로 바뀌었다
            }
            waiters_.fetch_add(1, memory_order_seq_cst);
            plat_wait(bits_, v, memory_order_seq_cst);           // 조건을 검사한 바로 그 v 에서 잔다
            waiters_.fetch_sub(1, memory_order_seq_cst);
            v = bits_.load(memory_order_acquire);
        }
    }
    i64 bits() const { return bits_.load(memory_order_relaxed); }
private:
    std::atomic<i64> bits_{0};
    std::atomic<i64> waiters_{0};
};

// ======================= 연습 4 — Event (단일 대기자) =======================
class Event {
public:
    void post() {
        if (s_.exchange(1, memory_order_release) == 2) plat_notify_one(s_);   // 대기 중(2)일 때만 깨운다
    }
    void wait() {
        i64 c = s_.load(memory_order_acquire);
        for (;;) {
            if (c == 1) {
                if (s_.compare_exchange_weak(c, 0, memory_order_acquire, memory_order_relaxed)) return;
                continue;
            }
            if (c == 0 && !s_.compare_exchange_weak(c, 2, memory_order_relaxed, memory_order_relaxed)) continue;
            plat_wait(s_, i64{2}, memory_order_acquire);
            c = s_.load(memory_order_acquire);
        }
    }
    i64 state() const { return s_.load(memory_order_relaxed); }
private:
    std::atomic<i64> s_{0};
};

// ======================= 하네스 =======================
static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static std::atomic<bool> g_done{false};
static void watchdog(int seconds) {
    auto end = clk::now() + std::chrono::seconds(seconds);
    while (!g_done.load()) {
        if (clk::now() > end) { std::printf("FAIL watchdog: %d s\n", seconds); std::fflush(stdout); std::_Exit(1); }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
static Barrier *g_bar; static EventGroup *g_eg; static Event *g_ev;
static void hook_other_arrives() { g_bar->arrive_and_wait(); }   // 같은 스레드에서 "두 번째 도착자" 흉내
static void hook_set_bit1() { g_eg->set(0b10); }
static void hook_post() { g_ev->post(); }

int main() {
    std::thread wd(watchdog, 60);
    // ---- 연습 1 ----
    {
        Semaphore s(0);
        auto t0 = clk::now();
        CHECK(!sem_try_acquire_for(s, std::chrono::milliseconds(20)));
        CHECK(clk::now() - t0 >= std::chrono::milliseconds(20));      // 시간 전에 포기하지 않는다
        s.release();
        CHECK(sem_try_acquire_for(s, std::chrono::nanoseconds(0)) && s.value() == 0);
        std::thread r([&] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); s.release(); });
        CHECK(sem_try_acquire_for(s, std::chrono::seconds(5)));
        r.join();
        CHECK(s.value() == 0);
    }
    // ---- 연습 2 ----
    {
        Barrier b(2); g_bar = &b; reset_stats();
        g_before_wait.store(hook_other_arrives);                  // 내가 잠들기 직전 상대가 도착해 세대를 넘긴다
        b.arrive_and_wait();
        CHECK(b.generation() == 1 && g_before_wait.load() == nullptr);
        CHECK(g_st.waits.load() == 1 && g_st.notify_all.load() == 1);
        constexpr int T = 6, P = 300;
        Barrier bb(T);
        std::vector<i64> slot(T, -1);                             // plain — 배리어가 게시
        std::atomic<int> bad{0};
        std::vector<std::thread> ts;
        for (int t = 0; t < T; t++)
            ts.emplace_back([&, t] {
                for (int p = 0; p < P; p++) {
                    slot[static_cast<std::size_t>(t)] = p;
                    bb.arrive_and_wait();
                    for (int u = 0; u < T; u++) if (slot[static_cast<std::size_t>(u)] != p) bad.fetch_add(1);
                    bb.arrive_and_wait();                         // 다 읽기 전에는 아무도 다음 값을 쓰지 않는다
                }
            });
        for (auto &t : ts) t.join();
        CHECK(bad.load() == 0 && bb.generation() == 2 * P);
    }
    // ---- 연습 3 ----
    {
        EventGroup eg; g_eg = &eg; reset_stats();
        eg.set(0b01);
        CHECK(g_st.notify_all.load() == 0);                       // 대기자 없으면 notify 0
        CHECK(eg.wait(0b01, true, false) == 0b01 && eg.bits() == 0b01);
        g_before_wait.store(hook_set_bit1);                       // wait-all(0b11): 잠들기 직전 bit1 이 켜진다
        CHECK(eg.wait(0b11, true, true) == 0b11);
        CHECK(eg.bits() == 0 && g_st.waits.load() == 1 && g_st.notify_all.load() == 1);
        eg.set(0b100);
        CHECK(eg.wait(0b110, false, true) == 0b100 && eg.bits() == 0);   // any
        int bad = 0;
        for (int r = 0; r < 200; r++) {
            EventGroup e;
            std::uint32_t data[3] = {0, 0, 0};                    // plain — set(release 성분) → wait(acquire) 로 게시
            std::vector<std::thread> ts;
            for (int t = 0; t < 3; t++)
                ts.emplace_back([&e, &data, t, r] { data[t] = static_cast<std::uint32_t>(r * 10 + t); e.set(i64{1} << t); });
            i64 got = e.wait(0b111, true, true);
            if (got != 0b111 || e.bits() != 0) bad++;
            for (int t = 0; t < 3; t++) if (data[t] != static_cast<std::uint32_t>(r * 10 + t)) bad++;
            for (auto &t : ts) t.join();
        }
        CHECK(bad == 0);
    }
    // ---- 연습 4 ----
    {
        Event e; g_ev = &e; reset_stats();
        e.post(); e.post(); e.post();                             // 이진: 여러 번 게시해도 1
        CHECK(e.state() == 1 && g_st.notify_one.load() == 0);
        e.wait();
        CHECK(e.state() == 0 && g_st.waits.load() == 0);          // 이미 게시돼 있으면 잠들지 않는다
        g_before_wait.store(hook_post);                           // 2(대기) 로 바꾼 뒤, 잠들기 직전 post
        e.wait();
        CHECK(e.state() == 0 && g_st.waits.load() == 1 && g_st.notify_one.load() == 1);
        constexpr int N = 20000;
        Event ping, pong;
        int seen = 0, order_bad = 0, pc = 0;                      // pc: plain — 핑퐁이 번갈아 넘겨준다
        std::thread peer([&] { for (int i = 0; i < N; i++) { ping.wait(); pc++; pong.post(); } });
        for (int i = 0; i < N; i++) { ping.post(); pong.wait(); seen++; if (pc != i + 1) order_bad++; }
        peer.join();
        CHECK(seen == N && order_bad == 0 && ping.state() == 0 && pong.state() == 0);
    }
    g_done.store(true);
    wd.join();
    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
