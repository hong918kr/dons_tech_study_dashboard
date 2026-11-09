// LLAT-M01 추가 연습 — 한 번에 검증
//   연습 1  zero-copy API: 생산자 alloc()/commit(), 소비자 front()/pop() (rigtorp/SPSCQueue 의 front/pop 과 같은 생각)
//   연습 2  FastForward 방식: 슬롯마다 bool(full) 플래그 하나 (Giacomoni 외, PPoPP 2008) — v4 의 seq 대신
//   연습 3  consume_all(fn): 소비자 쪽 "있는 만큼" 배치 — tail 1번 읽고 head 1번 게시 (지연을 늘리지 않는 배치)
//   연습 4  BatchWriter 지연 상한: 가짜 시계로 "모든 메시지의 대기 <= max_delay + poll 간격" 을 정확히 확인
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread practice.cpp -o pr && ./pr
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>
#include <type_traits>

using std::memory_order_acquire, std::memory_order_relaxed, std::memory_order_release;
using std::size_t, std::uint64_t;

#if defined(__has_feature)
#  if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#    define LLAT_M01_SAN 1
#  endif
#endif
#ifndef LLAT_M01_SAN
#  define LLAT_M01_SAN 0
#endif
inline constexpr size_t kLine = 128;

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint64_t now_ns() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
constexpr uint64_t kDeadlineNs = 5'000'000'000ull;
constexpr uint64_t kMsgs = LLAT_M01_SAN ? 50'000 : 1'000'000;

// ---------------- 연습 1 + 3: v2 레이아웃(패딩 + 상대 인덱스 캐시) 위의 zero-copy / consume_all ----------------
template <class T, size_t N>
class Ring {
    static_assert((N & (N - 1)) == 0 && N >= 2);
    static constexpr size_t kMask = N - 1;
public:
    // 생산자: 다음 빈 슬롯의 포인터 (없으면 nullptr). 거기에 직접 쓰고 commit() 으로 게시.
    T *alloc() {
        const size_t t = tail_.load(memory_order_relaxed);
        if (t - head_cache_ == N) {
            head_cache_ = head_.load(memory_order_acquire);
            head_loads_++;
            if (t - head_cache_ == N) return nullptr;
        }
        return &buf_[t & kMask];
    }
    void commit() { tail_.store(tail_.load(memory_order_relaxed) + 1, memory_order_release); }
    // 소비자: 맨 앞 원소 포인터 (없으면 nullptr). 다 읽은 뒤 pop() 으로 반납 — pop 전까지 슬롯은 내 것.
    T *front() {
        const size_t h = head_.load(memory_order_relaxed);
        if (tail_cache_ == h) {
            tail_cache_ = tail_.load(memory_order_acquire);
            tail_loads_++;
            if (tail_cache_ == h) return nullptr;
        }
        return &buf_[h & kMask];
    }
    void pop() { head_.store(head_.load(memory_order_relaxed) + 1, memory_order_release); head_stores_++; }
    // 연습 3: 지금 있는 것을 전부 fn 으로 처리. tail 은 최대 1번 읽고 head 는 최대 1번 게시.
    template <class F>
    size_t consume_all(F &&fn) {
        const size_t h = head_.load(memory_order_relaxed);
        if (tail_cache_ == h) {
            tail_cache_ = tail_.load(memory_order_acquire);
            tail_loads_++;
        }
        const size_t k = tail_cache_ - h;
        for (size_t i = 0; i < k; i++) fn(buf_[(h + i) & kMask]);
        if (k != 0) { head_.store(h + k, memory_order_release); head_stores_++; }
        return k;
    }
    uint64_t head_loads_ = 0;                   // (생산자 라인에 있어야 하지만 연습용이라 간단히 여기 둔다)
    uint64_t tail_loads_ = 0, head_stores_ = 0;
private:
    alignas(kLine) std::atomic<size_t> tail_{0};
    size_t head_cache_ = 0;
    alignas(kLine) std::atomic<size_t> head_{0};
    size_t tail_cache_ = 0;
    alignas(kLine) T buf_[N];
};

// 64 B 시장 데이터 틱: 마지막 워드 = 앞 7 워드 체크섬 → 반쯤 쓴 슬롯을 읽으면 들킨다
struct Tick { uint64_t w[7]; uint64_t sum; };
static_assert(sizeof(Tick) == 64);
static uint64_t tick_sum(const Tick &t) {
    uint64_t h = 0x9E3779B97F4A7C15ull;
    for (uint64_t x : t.w) { h ^= x; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 31; }
    return h;
}
static void make_tick(Tick &t, uint64_t n) {
    for (int i = 0; i < 7; i++) t.w[i] = n * 0x100000001B3ull + static_cast<uint64_t>(i);
    t.sum = tick_sum(t);
}

// ---------------- 연습 2: FastForward — 슬롯마다 full 플래그 ----------------
// 위치는 각자 전용. full == 0 이면 생산자 차례, 1 이면 소비자 차례. 두 주체뿐이라 1비트로 충분하다.
// (v4 의 seq 는 "몇 번째 바퀴인가" 까지 담아서 MPMC(DS-A03)로 확장되지만, SPSC 에서는 bool 이면 된다.)
template <class T, size_t N>
class FastForward {
    static_assert((N & (N - 1)) == 0 && N >= 2);
    struct Slot { std::atomic<std::uint32_t> full{0}; T val{}; };
public:
    bool try_push(const T &v) {
        Slot &s = slots_[tail_ & (N - 1)];
        if (s.full.load(memory_order_acquire) != 0) return false;   // 소비자가 아직 안 비움 (acquire: 그 읽기가 끝났음)
        s.val = v;
        s.full.store(1, memory_order_release);
        tail_++;
        return true;
    }
    bool try_pop(T &out) {
        Slot &s = slots_[head_ & (N - 1)];
        if (s.full.load(memory_order_acquire) == 0) return false;
        out = s.val;
        s.full.store(0, memory_order_release);
        head_++;
        return true;
    }
private:
    alignas(kLine) Slot slots_[N];
    alignas(kLine) uint64_t tail_ = 0;
    alignas(kLine) uint64_t head_ = 0;
};

// ---------------- 연습 4: BatchWriter (solution.cpp 와 같은 규칙) + 가짜 시계 ----------------
template <class T, size_t N>
struct SimpleQ {   // 단일 스레드 시뮬레이션용 큐: push_n 만, 게시 시각을 기록
    T items[N]; uint64_t at[N]; size_t n = 0; uint64_t now = 0;
    size_t push_n(const T *src, size_t k) {
        const size_t m = std::min(k, N - n);
        for (size_t i = 0; i < m; i++) { items[n] = src[i]; at[n] = now; n++; }
        return m;
    }
};
template <class Q, class T, size_t B>
struct BatchWriter {
    Q &q; size_t max_batch; uint64_t max_delay;
    T stage[B]{}; size_t n = 0; uint64_t first = 0, flushes = 0;
    bool add(const T &v, uint64_t now) {
        if (n >= max_batch) return false;
        if (n == 0) first = now;
        stage[n++] = v;
        if (n == max_batch) flush();
        return true;
    }
    size_t poll(uint64_t now) { return (n != 0 && now - first >= max_delay) ? flush() : 0; }
    size_t flush() {
        if (n == 0) return 0;
        const size_t k = q.push_n(stage, n);
        if (k == 0) return 0;
        for (size_t i = k; i < n; i++) stage[i - k] = stage[i];
        n -= k; flushes++;
        return k;
    }
};

template <class Push, class Pop>
static bool run_pair(Push push, Pop pop) {   // 생산자 스레드 + 소비자(현재 스레드), 둘 다 마감 시간 있음
    std::atomic<bool> abort{false};
    const uint64_t deadline = now_ns() + kDeadlineNs;
    std::thread p([&] {
        for (uint64_t i = 1, spins = 0; i <= kMsgs;) {
            if (push(i)) { i++; continue; }
            if ((++spins & 4095) == 0 && (abort.load() || now_ns() > deadline)) { abort.store(true); return; }
        }
    });
    uint64_t expect = 1, bad = 0;
    for (uint64_t spins = 0; expect <= kMsgs;) {
        const int r = pop(expect);   // 1 = 하나 받음(맞음), -1 = 받았는데 틀림, 0 = 없음
        if (r != 0) { bad += r < 0; expect++; continue; }
        if ((++spins & 4095) == 0 && (abort.load() || now_ns() > deadline)) { abort.store(true); break; }
    }
    p.join();
    return !abort.load() && bad == 0;
}

int main() {
    // ---- 연습 1: zero-copy ----
    {
        auto r = std::make_unique<Ring<Tick, 4>>();
        CHECK(r->front() == nullptr);
        for (uint64_t i = 0; i < 4; i++) { Tick *t = r->alloc(); make_tick(*t, i); r->commit(); }
        CHECK(r->alloc() == nullptr && r->head_loads_ == 1u);             // 꽉 참은 캐시 확인 후 1번만 재로드
        Tick *f = r->front();
        CHECK(f != nullptr && f->w[0] == 0u && tick_sum(*f) == f->sum);
        r->pop();
        CHECK(r->alloc() != nullptr && r->head_loads_ == 2u);             // 한 칸 비었음을 재로드로 안다
        auto big = std::make_unique<Ring<Tick, 16>>();
        const bool ok = run_pair(
            [&](uint64_t i) { Tick *t = big->alloc(); if (!t) return false; make_tick(*t, i); big->commit(); return true; },
            [&](uint64_t e) { Tick *t = big->front(); if (!t) return 0;
                              const bool good = t->w[0] == e * 0x100000001B3ull && tick_sum(*t) == t->sum;
                              big->pop(); return good ? 1 : -1; });
        CHECK(ok);
    }
    // ---- 연습 2: FastForward ----
    {
        auto q = std::make_unique<FastForward<uint64_t, 4>>();
        uint64_t v = 0;
        bool ok = !q->try_pop(v);
        for (uint64_t i = 0; i < 4; i++) ok = ok && q->try_push(i);
        ok = ok && !q->try_push(9);
        for (uint64_t i = 0; i < 4; i++) ok = ok && q->try_pop(v) && v == i;
        CHECK(ok && !q->try_pop(v));
        auto big = std::make_unique<FastForward<uint64_t, 8>>();
        CHECK(run_pair([&](uint64_t i) { return big->try_push(i); },
                       [&](uint64_t e) { uint64_t x; if (!big->try_pop(x)) return 0; return x == e ? 1 : -1; }));
    }
    // ---- 연습 3: consume_all ----
    {
        auto r = std::make_unique<Ring<uint64_t, 8>>();
        for (uint64_t i = 0; i < 5; i++) { *r->alloc() = 10 + i; r->commit(); }
        uint64_t sum = 0;
        const size_t k = r->consume_all([&](uint64_t x) { sum += x; });
        CHECK(k == 5u && sum == 60u && r->tail_loads_ == 1u && r->head_stores_ == 1u);   // 5 개에 load 1 · store 1
        CHECK(r->consume_all([&](uint64_t) {}) == 0u && r->tail_loads_ == 2u && r->head_stores_ == 1u);
        auto big = std::make_unique<Ring<uint64_t, 64>>();
        uint64_t expect = 1, bad = 0;
        std::atomic<bool> abort{false};
        const uint64_t deadline = now_ns() + kDeadlineNs;
        std::thread p([&] {
            for (uint64_t i = 1, spins = 0; i <= kMsgs;) {
                uint64_t *s = big->alloc();
                if (s) { *s = i++; big->commit(); continue; }
                if ((++spins & 4095) == 0 && (abort.load() || now_ns() > deadline)) { abort.store(true); return; }
            }
        });
        for (uint64_t spins = 0; expect <= kMsgs;) {
            if (big->consume_all([&](uint64_t x) { bad += x != expect; expect++; }) != 0) continue;
            if ((++spins & 4095) == 0 && (abort.load() || now_ns() > deadline)) { abort.store(true); break; }
        }
        p.join();
        CHECK(!abort.load() && bad == 0 && big->head_stores_ < kMsgs);   // 게시 수 < 메시지 수 (배치가 실제로 일어남)
    }
    // ---- 연습 4: 지연 상한 ----
    {
        // 메시지가 100 틱마다 생기고, 생산자는 10 틱마다 poll 한다. max_batch 8, max_delay 350.
        // 상한: 대기(게시 시각 - 생성 시각) <= max_delay + poll 간격 - 1 = 359. 실제 최댓값과 flush 수를 정확히 확인.
        SimpleQ<uint64_t, 4096> q;
        BatchWriter<SimpleQ<uint64_t, 4096>, uint64_t, 8> w(q, 8, 350);
        uint64_t gen[1000];
        size_t made = 0;
        for (uint64_t t = 0; t < 100'000 || w.n != 0; t += 10) {
            q.now = t;
            if (t % 100 == 0 && made < 1000) { gen[made] = t; w.add(made, t); made++; }
            w.poll(t);
        }
        uint64_t worst = 0;
        bool order = q.n == 1000;
        for (size_t i = 0; i < q.n && order; i++) {
            order = q.items[i] == i;
            worst = std::max(worst, q.at[i] - gen[i]);
        }
        CHECK(order && worst == 350u);                  // 생성 0,100,200,300 → 350 에 flush: 첫 메시지가 정확히 350 대기
        CHECK(w.flushes == 250u);                       // 4 개씩 (0·100·200·300 → 350) × 250
        // 같은 흐름에 max_batch 4 → 4 번째 add 에서 바로 flush → 첫 메시지 대기 300
        SimpleQ<uint64_t, 4096> q2;
        BatchWriter<SimpleQ<uint64_t, 4096>, uint64_t, 8> w2(q2, 4, 350);
        made = 0;
        for (uint64_t t = 0; t < 100'000 || w2.n != 0; t += 10) {
            q2.now = t;
            if (t % 100 == 0 && made < 1000) { w2.add(made, t); made++; }
            w2.poll(t);
        }
        worst = 0;
        for (size_t i = 0; i < q2.n; i++) worst = std::max(worst, q2.at[i] - gen[i]);
        CHECK(q2.n == 1000u && worst == 300u && w2.flushes == 250u);
    }
    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
