// MCPP-A03 atomic wait/notify로 동기화 프리미티브 구현
//
// C++20 std::atomic<T>::wait / notify_one / notify_all "만" 써서 동기화 프리미티브 세 개를 만든다.
//   Part A  Semaphore(카운팅) · Latch · Mutex(0/1/2 세 상태 — Drepper 방식, 전체 유도는 OS-A01)
//   Part B  계약 검증: 강제 스케줄(진짜 wait 직전 훅)로 "검사는 끝났고 아직 안 잠든" 창에 상대가 끼어들게 하고,
//           notify 호출 수를 정확히 센다 (대기자 카운트로 notify 생략 / Mutex 는 상태 2 일 때만 깨움)
//   Part C  센서 파이프라인: 드라이버 3개 초기화 Latch + "ISR" 생산자 → 16칸 링(세마포어 2개) → 작업자 3개 + 버스 Mutex
//
// 체크는 "올바른 구현이면 매 실행 항상 성립하는 것" 만 본다 (정확한 개수, 상태 전이, notify 호출 수).
// 지연 시간 비교는 demo_bench.cpp, 깨움 분실 재현은 demo_lost_wakeup.cpp, notify 폭 실험은 demo_notify_width.cpp.
// 멀티스레드 스트레스는 그 Part 의 단위 체크가 다 통과해야 돈다. 감시 스레드: 60 초 안에 안 끝나면 FAIL 로 종료.
//
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread solution.cpp -o so && ./so   (starter.cpp 도 같은 명령)
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <thread>
#include <vector>

using std::memory_order_relaxed, std::memory_order_acquire, std::memory_order_release, std::memory_order_seq_cst;
using i64 = std::int64_t;   // 이 맥 libc++ 의 대기 단위(__cxx_contention_t) = int64_t → 이 폭이면 그 변수 자체에서 잔다

// ---- 플랫폼 래퍼 (하네스 제공, 수정 금지): std::atomic 의 wait/notify 를 그대로 부르되 호출 수를 센다.
struct PlatStats { std::atomic<std::uint64_t> waits{0}, notify_one{0}, notify_all{0}; };
static PlatStats g_st;
static std::atomic<void (*)()> g_before_wait{nullptr};   // 강제 스케줄 훅: 다음 plat_wait 의 진짜 wait 직전에 한 번 실행

template <class T>
static void plat_wait(const std::atomic<T> &a, T old, std::memory_order mo) {
    g_st.waits.fetch_add(1, memory_order_relaxed);
    if (void (*h)() = g_before_wait.exchange(nullptr)) h();   // "검사는 끝났고 아직 안 잠든" 순간에 상대가 끼어든다
    a.wait(old, mo);                                           // 값 == old 인 동안만 잔다. 그냥 돌아올 수도 있다(가짜 깨어남)
}
template <class T> static void plat_notify_one(std::atomic<T> &a) {
    g_st.notify_one.fetch_add(1, memory_order_relaxed);
    a.notify_one();
}
template <class T> static void plat_notify_all(std::atomic<T> &a) {
    g_st.notify_all.fetch_add(1, memory_order_relaxed);
    a.notify_all();
}

// ======================= Part A — 세 프리미티브 =======================
// A1–A3. 카운팅 세마포어. count_ = 남은 허가 수(≥ 0). waiters_ = "잠들려고 등록한" 스레드 수.
//   try_acquire : count > 0 이면 CAS 로 1 감소 (acquire) → true. 0 이면 false. 절대 잠들지 않는다.
//   acquire     : try_acquire 가 될 때까지 [waiters_ 등록 → plat_wait(count_, 0) → 등록 해제] 반복.
//   release(n)  : count += n. 등록된 대기자가 있을 때만 깨운다 (n == 1 → notify_one, n > 1 → notify_all).
//   waiters_ 와 count_ 는 두 스레드가 "서로 다른 변수에 쓰고 상대 것을 읽는" 모양(SB) → 네 연산 모두 seq_cst.
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
            waiters_.fetch_add(1, memory_order_seq_cst);         // ① "나 잔다" 를 먼저 알린다
            plat_wait(count_, i64{0}, memory_order_seq_cst);     // ② count == 0 인 동안만 잔다 (비교는 깨움과 원자적)
            waiters_.fetch_sub(1, memory_order_seq_cst);
        }
    }
    void release(i64 n = 1) {
        count_.fetch_add(n, memory_order_seq_cst);               // ③ 값을 먼저 바꾸고
        if (waiters_.load(memory_order_seq_cst) == 0) return;    // ④ 잠든(잠들려는) 스레드가 없으면 notify 생략
        if (n == 1) plat_notify_one(count_);
        else plat_notify_all(count_);
    }
    i64 value() const { return count_.load(memory_order_relaxed); }
    i64 waiters() const { return waiters_.load(memory_order_relaxed); }
private:
    std::atomic<i64> count_;
    std::atomic<i64> waiters_{0};
};

// A4–A6. 래치 (일회용). count_down 은 release RMW, wait 은 acquire load → 0 을 읽으면 모든 count_down 앞의 쓰기가 보인다
//   (RMW 들이 release sequence 로 이어짐 — MCPP-M13). 0 이 되는 순간 딱 한 번 notify_all.
//   wait 은 "읽은 값 c" 를 그대로 old 로 넘긴다 (다시 읽어서 넘기면 깨움 분실 — Part B, demo_lost_wakeup.cpp ①).
class Latch {
public:
    explicit Latch(i64 n) : count_(n) {}
    void count_down(i64 n = 1) {
        i64 old = count_.fetch_sub(n, memory_order_release);
        if (old == n) plat_notify_all(count_);                   // 마지막 도착자만 깨운다
    }
    bool try_wait() const {
        return count_.load(memory_order_acquire) == 0;
    }
    void wait() const {
        for (i64 c = count_.load(memory_order_acquire); c != 0; c = count_.load(memory_order_acquire))
            plat_wait(count_, c, memory_order_acquire);
    }
    void arrive_and_wait(i64 n = 1) { count_down(n); wait(); }
    i64 value() const { return count_.load(memory_order_relaxed); }
private:
    std::atomic<i64> count_;
};

// A7–A9. 뮤텍스: 0 = 풀림, 1 = 잠김(대기자 없음), 2 = 잠김(대기자 있을 수 있음).
//   lock   : CAS 0→1 (빠른 경로, wait/notify 0 번). 실패하면 exchange(2) 가 0 을 돌려줄 때까지 plat_wait(s_, 2).
//   unlock : exchange(0) 이 2 였을 때만 notify_one. 깨어난 쪽은 대기자가 더 있는지 모르므로 2 로 잡는다(보수적).
class Mutex {
public:
    void lock() {
        i64 c = 0;
        if (s_.compare_exchange_strong(c, 1, memory_order_acquire, memory_order_relaxed)) return;
        if (c != 2) c = s_.exchange(2, memory_order_acquire);
        while (c != 0) {
            plat_wait(s_, i64{2}, memory_order_relaxed);
            c = s_.exchange(2, memory_order_acquire);
        }
    }
    bool try_lock() {
        i64 c = 0;
        return s_.compare_exchange_strong(c, 1, memory_order_acquire, memory_order_relaxed);
    }
    void unlock() {
        if (s_.exchange(0, memory_order_release) == 2) plat_notify_one(s_);
    }
    i64 state() const { return s_.load(memory_order_relaxed); }
private:
    std::atomic<i64> s_{0};
};

// ======================= Part C — 센서 파이프라인 =======================
// 드라이버 3개가 설정(plain struct)을 쓰고 init_done.count_down(). 작업자는 init_done.wait() 뒤에 설정을 읽는다.
// "ISR" 생산자: free_slots.acquire() → ring[head % 16] 쓰기 → filled.release(). 끝나면 독약(seq = -1) 을 작업자 수만큼.
// 작업자: filled.acquire() → bus.lock() { ring[tail % 16] 꺼냄, tail++, 통계 } bus.unlock() → free_slots.release().
struct DriverCfg { std::uint32_t id, odr_hz, range, crc; };
static std::uint32_t cfg_crc(const DriverCfg &c) { return c.id * 2654435761u ^ c.odr_hz ^ (c.range << 7) ^ 0x5EED5EEDu; }
struct Sample { i64 seq; std::uint32_t a, b, sum; };
[[maybe_unused]] static Sample make_sample(i64 s) {
    Sample x{s, static_cast<std::uint32_t>(s) * 2246822519u, ~static_cast<std::uint32_t>(s), 0};
    x.sum = static_cast<std::uint32_t>(x.seq) ^ x.a ^ x.b ^ 0xA5A5A5A5u;
    return x;
}
[[maybe_unused]] static bool sample_ok(const Sample &x) { return x.sum == (static_cast<std::uint32_t>(x.seq) ^ x.a ^ x.b ^ 0xA5A5A5A5u); }

constexpr int kRing = 16, kWorkers = 3, kDrivers = 3;
struct Pipeline {
    DriverCfg cfg[kDrivers]{};
    Latch init_done{kDrivers};
    Sample ring[kRing]{};
    int head = 0;                                  // 생산자만
    Semaphore free_slots{kRing}, filled{0};
    Mutex bus;                                     // tail, 통계, in_cs 검사 보호
    int tail = 0;
    i64 consumed = 0, seq_sum = 0, bad = 0, in_cs = 0, max_in_cs = 0;
    std::atomic<i64> cfg_bad{0};
};
static void driver_init(Pipeline &p, int i) {      // 하네스 제공
    DriverCfg c{static_cast<std::uint32_t>(i + 1), 100u * static_cast<std::uint32_t>(i + 1), 8u << i, 0};
    c.crc = cfg_crc(c);
    p.cfg[i] = c;
    p.init_done.count_down();
}

// C1. 샘플 0..n-1 을 링에 넣고, 끝나면 독약(make_sample(-1)) 을 kWorkers 개 넣는다.
static void isr_producer(Pipeline &p, i64 n) {
    for (i64 s = 0; s < n + kWorkers; s++) {
        p.free_slots.acquire();
        p.ring[p.head % kRing] = make_sample(s < n ? s : -1);
        p.head++;
        p.filled.release();
    }
}
// C2. init_done.wait() → 설정 3개 crc 검사(틀리면 cfg_bad++) → 독약을 꺼낼 때까지 샘플 처리.
//     bus 안에서: in_cs++ → max_in_cs 갱신 → 샘플 꺼내기/통계(consumed, seq_sum, bad) → in_cs--.
static void worker(Pipeline &p) {
    p.init_done.wait();
    for (const DriverCfg &c : p.cfg)
        if (c.crc != cfg_crc(c) || c.id == 0) p.cfg_bad.fetch_add(1, memory_order_relaxed);
    for (;;) {
        p.filled.acquire();
        p.bus.lock();
        if (++p.in_cs > p.max_in_cs) p.max_in_cs = p.in_cs;
        Sample s = p.ring[p.tail % kRing];
        p.tail++;
        if (s.seq >= 0) {
            p.consumed++;
            p.seq_sum += s.seq;
            if (!sample_ok(s)) p.bad++;
        }
        p.in_cs--;
        p.bus.unlock();
        p.free_slots.release();
        if (s.seq < 0) return;
    }
}

// ======================= 하네스 =======================
static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define FAILS() (g_total - g_pass)
static void skip(int n, const char *what) {
    g_total += n;
    std::printf("SKIP %d stress checks (%s): unit checks failed\n", n, what);
}
static void reset_stats() {
    g_st.waits.store(0); g_st.notify_one.store(0); g_st.notify_all.store(0);
    g_before_wait.store(nullptr);
}
static std::atomic<bool> g_done{false};
static void watchdog(int seconds) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!g_done.load()) {
        if (std::chrono::steady_clock::now() > end) {
            std::printf("FAIL watchdog: %d s 안에 안 끝남 (wait 에서 영원히 잠든 스레드?)\n", seconds);
            std::fflush(stdout);
            std::_Exit(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
// 강제 스케줄 훅이 조작할 대상 (훅은 인자 없는 함수 포인터)
static Semaphore *g_sem; static Latch *g_latch; static Mutex *g_mx;
static void hook_release1() { g_sem->release(1); }
static void hook_release3() { g_sem->release(3); }
static void hook_count_down() { g_latch->count_down(); }
static void hook_unlock() { g_mx->unlock(); }

template <class F> static void run_threads(int n, F f) {
    std::vector<std::thread> ts;
    for (int t = 0; t < n; t++) ts.emplace_back(f, t);
    for (auto &t : ts) t.join();
}

int main() {
    std::thread wd(watchdog, 60);
    // ---------- Part A: 단일 스레드 의미 ----------
    int fa = FAILS();
    {
        Semaphore s(2);
        CHECK(s.try_acquire() && s.try_acquire());
        CHECK(!s.try_acquire() && s.value() == 0);               // 0 에서 try 는 실패, 음수로 안 간다
        s.release(); s.release(2);
        CHECK(s.value() == 3);
        s.acquire(); s.acquire(); s.acquire();                   // 허가가 있으면 acquire 는 잠들지 않는다
        CHECK(s.value() == 0 && s.waiters() == 0);
        Latch l(3);
        CHECK(!l.try_wait());
        l.count_down(2);
        CHECK(l.value() == 1 && !l.try_wait());
        l.count_down();
        CHECK(l.try_wait());
        l.wait();                                                // 0 이면 바로 돌아온다
        Latch l2(1);
        l2.arrive_and_wait();
        CHECK(l2.try_wait());
        Mutex m;
        m.lock();
        CHECK(m.state() == 1 && !m.try_lock());
        m.unlock();
        CHECK(m.state() == 0 && m.try_lock());
        m.unlock();
        CHECK(m.state() == 0);
    }
    // ---------- Part A: 스레드 스트레스 ----------
    if (FAILS() == fa) {
        constexpr int T = 8, K = 20000;
        Semaphore pool(3);                                       // DMA 채널 3개를 스레드 8개가 나눠 쓴다
        std::atomic<int> in_use{0}, max_use{0};
        run_threads(T, [&](int) {
            for (int i = 0; i < K; i++) {
                pool.acquire();
                int u = in_use.fetch_add(1, memory_order_relaxed) + 1;
                int mx = max_use.load(memory_order_relaxed);
                while (u > mx && !max_use.compare_exchange_weak(mx, u, memory_order_relaxed)) {}
                in_use.fetch_sub(1, memory_order_relaxed);
                pool.release();
            }
        });
        CHECK(max_use.load() <= 3 && max_use.load() >= 1);
        CHECK(pool.value() == 3 && pool.waiters() == 0);

        constexpr int KM = 50000;
        Mutex mx;
        i64 counter = 0, in_cs = 0, max_cs = 0;                  // plain — 뮤텍스가 보호 (TSan 이 검증)
        run_threads(T, [&](int) {
            for (int i = 0; i < KM; i++) {
                mx.lock();
                if (++in_cs > max_cs) max_cs = in_cs;
                counter++;
                in_cs--;
                mx.unlock();
            }
        });
        CHECK(counter == i64{T} * KM && max_cs == 1 && mx.state() == 0);

        constexpr int R = 200;
        std::deque<Latch> rounds;
        for (int r = 0; r < R; r++) rounds.emplace_back(T);
        std::vector<i64> slot(static_cast<std::size_t>(R * T), -1);   // plain — Latch 가 게시
        std::atomic<int> latch_bad{0};
        run_threads(T, [&](int t) {
            for (int r = 0; r < R; r++) {
                slot[static_cast<std::size_t>(r * T + t)] = r * 1000 + t;
                rounds[static_cast<std::size_t>(r)].arrive_and_wait();
                for (int u = 0; u < T; u++)
                    if (slot[static_cast<std::size_t>(r * T + u)] != r * 1000 + u) latch_bad.fetch_add(1);
            }
        });
        CHECK(latch_bad.load() == 0);
        std::printf("Part A stress: pool max_in_use=%d  mutex counter=%lld  latch rounds=%d\n",
                    max_use.load(), static_cast<long long>(counter), R);
    } else skip(4, "Part A");

    // ---------- Part B: 강제 스케줄 + notify 호출 수 (단일 스레드, 결정적) ----------
    int fb = FAILS();
    {
        // B1. 세마포어: 검사(try 실패) 후, 진짜 wait 직전에 release 가 끼어든다 → wait(0) 은 1 을 보고 바로 돌아와야 한다.
        Semaphore s(0); g_sem = &s; reset_stats();
        g_before_wait.store(hook_release1);
        s.acquire();
        CHECK(g_before_wait.load() == nullptr);                  // 훅이 실제로 실행됐다 = plat_wait 경로를 탔다
        CHECK(s.value() == 0 && s.waiters() == 0);
        CHECK(g_st.waits.load() == 1 && g_st.notify_one.load() == 1);   // 대기자가 등록돼 있었으니 깨웠다

        // B2. 대기자가 없으면 notify 0 번 (1000 쌍)
        reset_stats();
        for (int i = 0; i < 1000; i++) { s.release(); s.acquire(); }
        CHECK(g_st.notify_one.load() == 0 && g_st.notify_all.load() == 0 && g_st.waits.load() == 0);

        // B3. release(3) 이 끼어들면 notify_all 한 번, 남은 허가 2
        reset_stats();
        g_before_wait.store(hook_release3);
        s.acquire();
        CHECK(s.value() == 2 && g_st.notify_all.load() == 1 && g_st.notify_one.load() == 0);

        // B4. 래치: 마지막 count_down 이 wait 직전에 끼어든다. notify_all 은 0 이 되는 순간 딱 한 번.
        Latch l(3); g_latch = &l; reset_stats();
        l.count_down(); l.count_down();
        CHECK(g_st.notify_all.load() == 0);                      // 아직 0 이 아니면 깨우지 않는다
        g_before_wait.store(hook_count_down);
        l.wait();
        CHECK(g_before_wait.load() == nullptr && l.try_wait());
        CHECK(g_st.waits.load() == 1 && g_st.notify_all.load() == 1);
        l.wait();
        CHECK(g_st.waits.load() == 1);                           // 이미 0 → 다시 잠들 일 없음

        // B5. 뮤텍스 빠른 경로: 경합 없는 lock/unlock 1000 번 → wait/notify 0 번
        Mutex m; g_mx = &m; reset_stats();
        for (int i = 0; i < 1000; i++) { m.lock(); m.unlock(); }
        CHECK(g_st.waits.load() == 0 && g_st.notify_one.load() == 0 && m.state() == 0);

        // B6. 뮤텍스 느린 경로: exchange(2) 와 wait(2) 사이에 unlock 이 끼어든다 (단일 스레드로 그 창을 재현)
        m.lock();                                                // 0 → 1
        reset_stats();
        g_before_wait.store(hook_unlock);
        m.lock();                                                // 1 → 2, wait 직전 unlock(2→0, notify) → wait(2) 즉시 반환 → 0→2
        CHECK(g_before_wait.load() == nullptr);
        CHECK(m.state() == 2);                                   // 깨어난 쪽은 보수적으로 2
        CHECK(g_st.waits.load() == 1 && g_st.notify_one.load() == 1);
        m.unlock();
        CHECK(m.state() == 0 && g_st.notify_one.load() == 2);    // 대기자 없어도 2 였으니 한 번 더 깨움 (보수적 비용)
        reset_stats();
    }
    // B7. 진짜 2 스레드: 대기자가 확실히 잠든 뒤 release → 깨어나야 한다 (깨움 분실이면 감시 스레드가 잡는다)
    if (FAILS() == fb) {
        Semaphore s(0);
        std::atomic<int> got{0};
        std::thread w([&] { s.acquire(); got.store(1); });
        while (s.waiters() == 0) std::this_thread::yield();      // 등록할 때까지
        std::this_thread::sleep_for(std::chrono::milliseconds(20));   // 스핀 단계(약 4 µs)를 지나 커널에서 자게
        s.release();
        w.join();
        CHECK(got.load() == 1 && s.value() == 0 && s.waiters() == 0);
    } else skip(1, "Part B");

    // ---------- Part C: 센서 파이프라인 ----------
    if (FAILS() == fa && FAILS() == fb) {
        constexpr i64 N = 100000;
        Pipeline *p = new Pipeline{};
        std::vector<std::thread> ts;
        for (int w = 0; w < kWorkers; w++) ts.emplace_back([p] { worker(*p); });
        for (int d = 0; d < kDrivers; d++) ts.emplace_back([p, d] { driver_init(*p, d); });
        ts.emplace_back([p] { isr_producer(*p, N); });
        for (auto &t : ts) t.join();
        CHECK(p->consumed == N && p->seq_sum == N * (N - 1) / 2 && p->bad == 0);
        CHECK(p->cfg_bad.load() == 0 && p->max_in_cs == 1);
        CHECK(p->tail == p->head && p->head == N + kWorkers);
        CHECK(p->free_slots.value() == kRing && p->filled.value() == 0 && p->bus.state() == 0);
        std::printf("Part C stress: samples=%lld ring=%d workers=%d\n", static_cast<long long>(p->consumed), kRing, kWorkers);
        delete p;
    } else skip(4, "Part C");

    g_done.store(true);
    wd.join();
    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
