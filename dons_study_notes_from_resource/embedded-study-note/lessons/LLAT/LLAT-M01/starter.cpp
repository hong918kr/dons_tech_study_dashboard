// LLAT-M01 SPSC 큐 지연 튜닝: 캐시된 인덱스·패딩·배치
//
// 단일 생산자/단일 소비자(SPSC) lock-free 링 큐를 "지연 관점" 에서 한 단계씩 고친다. 단계마다 별도 타입이라
// 벤치마크 표(demo_bench.cpp)를 그대로 다시 만들 수 있다. memory_order 정확성 논증은 DS-A01 (여기선 인용만).
//   v0  SpscRing<T,N,0,false>    head/tail 인덱스가 붙어 있음 (한 캐시 라인)                — Part A
//   v1  SpscRing<T,N,128,false>  tail / head / 버퍼를 서로 다른 128 B 라인으로              — Part A
//   v2  SpscRing<T,N,128,true>   상대 인덱스 캐시 (rigtorp/SPSCQueue 방식)                 — Part B
//   v3  v2 + push_n / pop_n      배치: 인덱스 게시(store)를 배치당 1번                      — Part B
//   v4  SpscSlot<T,N>            슬롯마다 시퀀스 번호, 공유 인덱스 없음                      — Part C
//   +   BatchWriter              "B 개 모이거나 max_delay 지나면 flush" = 배치 지연의 상한     — Part C
//
// 체크 테스트는 매 실행 항상 성립하는 것만 본다: FIFO 순서, 손실·중복 0, 꽉 참/빔 경계, 인덱스 wrap,
// 그리고 "상대 인덱스를 몇 번 읽었나 / 내 인덱스를 몇 번 게시했나" 카운터 (단일 스레드 → 결정적).
// 처리량·왕복 지연은 demo_bench.cpp 가 잰다 (CHECK 하지 않는다).
// 새니타이저 빌드(ASan/TSan)에서는 스트레스 메시지 수를 1/20 로 줄인다 (LLAT_M01_SAN).
//
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread solution.cpp -o so && ./so   (starter.cpp 도 같은 명령)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
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

// 이 맥의 캐시 라인 = 128 B (sysctl hw.cachelinesize). libc++ 의 std::hardware_destructive_interference_size 는
// 256 이다 (clang 이 AArch64 전체에 __GCC_DESTRUCTIVE_SIZE=256 을 준다 — 5. 풀이). 128 로 충분한지는 demo_bench 의 v1w.
inline constexpr size_t kLine = 128;
#ifdef __cpp_lib_hardware_interference_size
static_assert(std::hardware_destructive_interference_size >= kLine);
#endif

// ---- 카운터: 각 쪽이 자기 라인에서만 쓰는 보통 변수. "상대 인덱스 load" = 코히어런스 미스 후보 ----
struct ProdStats { uint64_t head_loads = 0, tail_stores = 0; };
struct ConsStats { uint64_t tail_loads = 0, head_stores = 0; };

// Pad == 0 이면 alignas(0) 은 효과가 없다 ([dcl.align]) → 필드가 붙어서 한 라인에 들어간다 (v0).
template <size_t Pad>
struct Indices {
    alignas(Pad) std::atomic<size_t> tail{0};   // 생산자만 쓴다 (다음에 쓸 위치)
    size_t head_cache = 0;                      // 생산자 전용: 마지막으로 읽은 head
    ProdStats ps;
    alignas(Pad) std::atomic<size_t> head{0};   // 소비자만 쓴다 (다음에 읽을 위치)
    size_t tail_cache = 0;                      // 소비자 전용: 마지막으로 읽은 tail
    ConsStats cs;
};

// ======================= Part A/B: 인덱스 두 개짜리 링 (v0 v1 v2 v3) =======================
// free-running 인덱스: tail - head = 들어 있는 개수 (unsigned wrap 은 정의된 동작, DS-A01). 슬롯 낭비 없음.
template <class T, size_t N, size_t Pad, bool Cache>
class SpscRing {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>);
    static constexpr size_t kMask = N - 1;

public:
    static constexpr size_t capacity() { return N; }

    // 테스트용: 빈 큐로 되돌리고 인덱스를 start 에서 시작 (SIZE_MAX 근처 → wrap 검사). 스레드가 없을 때만 부른다.
    void reset(size_t start) {
        ix_.tail.store(start, memory_order_relaxed);
        ix_.head.store(start, memory_order_relaxed);
        ix_.head_cache = ix_.tail_cache = start;
        ix_.ps = {};
        ix_.cs = {};
    }

    // ---- Part A: 한 개씩 ----
    bool try_push(const T &v) {
        // TODO(Part A): 내 tail 을 relaxed 로 읽고, prod_free(t, 1) 이 0 이면 false. 슬롯에 쓰고 tail 을 release 로 게시,
        //               ix_.ps.tail_stores++ 하고 true.
        (void)v;
        return false;
    }
    bool try_pop(T &out) {
        // TODO(Part A): try_push 의 거울상 (cons_avail, head 를 release 로 게시, ix_.cs.head_stores++).
        (void)out;
        return false;
    }

    // ---- Part B (v3): 배치. 들어간/나온 개수를 돌려준다. 인덱스 store 는 호출당 최대 1번 ----
    size_t push_n(const T *src, size_t n) {
        // TODO(Part B): k = min(n, prod_free(t, n)) 개를 쓰고, k != 0 이면 tail 을 한 번만 release 게시 (+ tail_stores++).
        (void)src; (void)n;
        return 0;
    }
    size_t pop_n(T *dst, size_t max) {
        // TODO(Part B): push_n 의 거울상.
        (void)dst; (void)max;
        return 0;
    }

    size_t size_approx() const {   // 스레드가 없을 때만 정확 (테스트용)
        return ix_.tail.load(memory_order_acquire) - ix_.head.load(memory_order_acquire);
    }
    const ProdStats &pstats() const { return ix_.ps; }
    const ConsStats &cstats() const { return ix_.cs; }
    const void *tail_addr() const { return &ix_.tail; }
    const void *head_addr() const { return &ix_.head; }
    const void *buf_addr() const { return &buf_[0]; }

private:
    // 생산자: 지금 확실히 비어 있다고 말할 수 있는 슬롯 수 (need 개 이상이면 상대 인덱스를 다시 안 읽어도 된다)
    size_t prod_free(size_t t, size_t need) {
        if constexpr (Cache) {
            // TODO(Part B): head_cache 로 빈 칸을 계산해 need 개 이상이면 그대로 반환. 모자랄 때만
            //               head 를 acquire 로 다시 읽어 head_cache 갱신 (+ head_loads++).
            (void)t; (void)need;
            return 0;
        } else {
            // TODO(Part A): head 를 acquire 로 읽고 (+ head_loads++) 빈 칸 수 N - (t - h) 반환.
            (void)t; (void)need;
            return 0;
        }
    }
    // 소비자: 지금 확실히 들어 있다고 말할 수 있는 개수
    size_t cons_avail(size_t h, size_t need) {
        if constexpr (Cache) {
            // TODO(Part B): tail_cache 로 (+ 모자랄 때만 tail 재로드, tail_loads++).
            (void)h; (void)need;
            return 0;
        } else {
            // TODO(Part A): tail 을 acquire 로 읽고 (+ tail_loads++) t - h 반환.
            (void)h; (void)need;
            return 0;
        }
    }

    Indices<Pad> ix_;
    alignas(Pad) T buf_[N];
};

template <class T, size_t N> using SpscV0 = SpscRing<T, N, 0, false>;
template <class T, size_t N> using SpscV1 = SpscRing<T, N, kLine, false>;
template <class T, size_t N> using SpscV2 = SpscRing<T, N, kLine, true>;   // v3 = v2 의 push_n/pop_n

// ======================= Part C: 슬롯마다 시퀀스 (v4) =======================
// 슬롯 i 의 seq: pos (pos 에 쓸 차례, 빔) → pos+1 (찼음) → pos+N (다음 바퀴 pos+N 에 쓸 차례).
// 위치(tail_/head_)는 각자 전용 변수 — 상대에게 보여 줄 공유 인덱스가 없다. 동기화는 슬롯의 seq 하나로.
template <class T, size_t N>
class SpscSlot {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>);
    static constexpr size_t kMask = N - 1;
    struct Slot { std::atomic<uint64_t> seq{0}; T val{}; };

public:
    SpscSlot() { reset(0); }
    static constexpr size_t capacity() { return N; }
    void reset(uint64_t start) {   // 스레드가 없을 때만
        for (size_t i = 0; i < N; i++) slots_[(start + i) & kMask].seq.store(start + i, memory_order_relaxed);
        tail_ = head_ = start;
    }
    bool try_push(const T &v) {
        // TODO(Part C): 슬롯 seq 가 tail_ 이면 빈 칸 → val 쓰고 seq = tail_+1 (release), tail_++.
        (void)v;
        return false;
    }
    bool try_pop(T &out) {
        // TODO(Part C): 슬롯 seq 가 head_+1 이면 찬 칸 → val 읽고 seq = head_+N (release), head_++.
        (void)out;
        return false;
    }
    uint64_t slot_seq(uint64_t pos) const { return slots_[pos & kMask].seq.load(memory_order_relaxed); }

private:
    alignas(kLine) Slot slots_[N];
    alignas(kLine) uint64_t tail_ = 0;   // 생산자 전용
    alignas(kLine) uint64_t head_ = 0;   // 소비자 전용
};

// ======================= Part C: 지연 상한이 있는 배치 생산자 =======================
// 모아서 push_n 한 번 → 인덱스 게시·라인 이동이 줄어 처리량이 오른다. 대가: 모으는 동안 첫 메시지가 기다린다.
// 규칙: staged 가 max_batch 개가 되면 add() 가 바로 flush, 첫 staged 메시지가 max_delay_ns 이상 기다렸으면 poll() 이 flush.
// 큐가 꽉 차서 일부만 들어가면 남은 것은 순서대로 앞으로 당겨 둔다 (다음 flush 때 먼저 나간다).
template <class Q, class T, size_t B>
struct BatchWriter {
    Q &q;
    size_t max_batch;          // 1..B
    uint64_t max_delay_ns;
    T stage[B]{};
    size_t n = 0;              // staged 개수
    uint64_t first_ns = 0;     // 가장 오래된 staged 메시지가 들어온 시각
    uint64_t flushes = 0;      // 실제로 1개 이상 게시한 flush 수
    uint64_t sent = 0;

    BatchWriter(Q &q_, size_t mb, uint64_t md) : q(q_), max_batch(std::min(mb, B)), max_delay_ns(md) {}

    // 받으면 true. stage 가 이미 max_batch 개(앞 flush 가 큐 꽉 참으로 못 비움)면 false.
    bool add(const T &v, uint64_t now_ns) {
        // TODO(Part C): stage 가 max_batch 개면 false. 첫 메시지면 first_ns 기록. 넣고, max_batch 개가 되면 flush().
        (void)v; (void)now_ns;
        return false;
    }
    size_t poll(uint64_t now_ns) {   // 시간 조건만 본다
        // TODO(Part C): staged 가 있고 now_ns - first_ns >= max_delay_ns 이면 flush() 결과, 아니면 0.
        (void)now_ns;
        return 0;
    }
    size_t flush() {                 // 게시한 개수
        // TODO(Part C): push_n(stage, n). 1 개 이상 나갔으면 남은 것을 앞으로 당기고 n/flushes/sent 갱신, 나간 수 반환.
        return 0;
    }
};

#ifndef LLAT_M01_NO_MAIN   // demo_*.cpp 는 이 줄 위(자료구조)만 include 해서 쓴다
// ======================= 테스트 하네스 =======================
static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define FAILS() (g_total - g_pass)
static void skip(int n, const char *what) {
    g_total += n;
    std::printf("SKIP %d stress checks (%s): unit checks failed\n", n, what);
}

static uint64_t now_ns() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
constexpr uint64_t kDeadlineNs = 5'000'000'000ull;   // 스트레스 하나가 5초를 넘으면 (스텁/버그) 포기 → FAIL, 멈추지 않음
constexpr uint64_t kMsgs = LLAT_M01_SAN ? 100'000 : 2'000'000;
constexpr uint64_t kRounds = LLAT_M01_SAN ? 10'000 : 100'000;

static uint64_t xorshift64(uint64_t &s) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }

struct StressResult { uint64_t got = 0, bad = 0, sum = 0; bool timeout = false; };
static bool stress_ok(const StressResult &r, uint64_t M) {
    return !r.timeout && r.got == M && r.bad == 0 && r.sum == M * (M + 1) / 2;
}

// 생산자 1..M 을 넣고 소비자는 "바로 앞 값 + 1" 인지 본다 → 순서·손실·중복을 한 번에. Mode 0 = 하나씩, 1 = 배치(무작위 크기)
template <int Mode, class Q>
static StressResult stress(Q &q, uint64_t M, uint64_t seed) {
    StressResult r;
    std::atomic<bool> abort{false};
    const uint64_t deadline = now_ns() + kDeadlineNs;
    std::thread prod([&] {
        uint64_t s = seed, spins = 0, buf[64];
        for (uint64_t i = 1; i <= M;) {
            bool progressed;
            if constexpr (Mode == 0) {
                progressed = q.try_push(i);
                if (progressed) i++;
            } else {
                size_t want = 1 + static_cast<size_t>(xorshift64(s) % 37);
                want = static_cast<size_t>(std::min<uint64_t>(want, M - i + 1));
                for (size_t j = 0; j < want; j++) buf[j] = i + j;
                const size_t k = q.push_n(buf, want);
                progressed = k != 0;
                i += k;
            }
            if (!progressed && (++spins & 4095) == 0 && (abort.load(memory_order_relaxed) || now_ns() > deadline)) {
                abort.store(true, memory_order_relaxed);
                return;
            }
        }
    });
    uint64_t expect = 1, s = seed ^ 0x9E3779B97F4A7C15ull, spins = 0, buf[64];
    while (expect <= M) {
        size_t k = 0;
        if constexpr (Mode == 0) {
            if (q.try_pop(buf[0])) k = 1;
        } else {
            k = q.pop_n(buf, 1 + static_cast<size_t>(xorshift64(s) % 41));
        }
        for (size_t j = 0; j < k; j++, expect++) {
            if (buf[j] != expect) r.bad++;
            r.got++;
            r.sum += buf[j];
        }
        if (k == 0 && (++spins & 4095) == 0 && (abort.load(memory_order_relaxed) || now_ns() > deadline)) {
            abort.store(true, memory_order_relaxed);
            break;
        }
    }
    prod.join();
    r.timeout = abort.load();
    return r;
}

// 두 큐로 왕복: main 이 2r 를 a 에 넣고, echo 스레드가 꺼내 +1 해서 b 에 넣는다. 매 왕복 답이 2r+1 이어야 한다.
template <class Q>
static bool pingpong_ok(Q &a, Q &b, uint64_t R) {
    std::atomic<bool> abort{false};
    const uint64_t deadline = now_ns() + kDeadlineNs;
    auto spin_pop = [&](Q &q, uint64_t &v) {
        for (uint64_t spins = 0; !q.try_pop(v);)
            if ((++spins & 4095) == 0 && (abort.load(memory_order_relaxed) || now_ns() > deadline)) {
                abort.store(true, memory_order_relaxed);
                return false;
            }
        return true;
    };
    std::thread echo([&] {
        uint64_t v;
        for (uint64_t r = 0; r < R; r++) {
            if (!spin_pop(a, v)) return;
            if (!b.try_push(v + 1)) { abort.store(true); return; }   // 한 개만 오가므로 꽉 찰 일이 없다
        }
    });
    uint64_t bad = 0, v = 0;
    for (uint64_t r = 0; r < R; r++) {
        if (!a.try_push(2 * r)) { abort.store(true); break; }
        if (!spin_pop(b, v)) break;
        if (v != 2 * r + 1) bad++;
    }
    echo.join();
    return !abort.load() && bad == 0;
}

// 한 개씩 넣고 빼는 단위 테스트 (v0 v1 v2 v4 공통). start 근처에서 인덱스가 wrap 하도록 3N 번 돌린다.
template <class Q>
static bool unit_fifo(Q &q, uint64_t start) {
    constexpr size_t N = Q::capacity();
    q.reset(start);
    uint64_t v = 0;
    bool ok = !q.try_pop(v);                                   // 빈 큐
    for (uint64_t i = 0; i < N; i++) ok = ok && q.try_push(100 + i);
    ok = ok && !q.try_push(999);                               // N 개 다 들어간다 (슬롯 낭비 없음), N+1 번째는 거절
    for (uint64_t i = 0; i < N; i++) ok = ok && q.try_pop(v) && v == 100 + i;
    ok = ok && !q.try_pop(v);
    uint64_t in = 0, out = 0;                                  // 넣기 2, 빼기 1 … 섞어서 3N 개 (wrap 통과)
    for (int step = 0; step < static_cast<int>(6 * N); step++) {
        if (step % 3 != 2) { if (q.try_push(in)) in++; }
        else if (q.try_pop(v)) { ok = ok && v == out; out++; }
    }
    while (q.try_pop(v)) { ok = ok && v == out; out++; }
    return ok && in == out && in > 0;
}

static bool line_disjoint(const void *a, const void *b) {
    return reinterpret_cast<std::uintptr_t>(a) / kLine != reinterpret_cast<std::uintptr_t>(b) / kLine;
}

constexpr size_t kBig = 1024, kTiny = 4;
static constexpr uint64_t kWrap = UINT64_MAX - 2;   // 인덱스가 몇 번 만에 0 으로 넘어간다

int main() {
    // ---------- Part A: v0 (붙은 인덱스) / v1 (128 B 패딩) ----------
    int fa = FAILS();
    {
        auto q0 = std::make_unique<SpscV0<uint64_t, 8>>();
        auto q1 = std::make_unique<SpscV1<uint64_t, 8>>();
        CHECK(unit_fifo(*q0, 0) && unit_fifo(*q0, kWrap));
        CHECK(unit_fifo(*q1, 0) && unit_fifo(*q1, kWrap));
        // 레이아웃: v0 은 tail/head 가 64 B 안에, v1 은 tail · head · 버퍼가 서로 다른 128 B 라인
        CHECK(static_cast<const char *>(q0->head_addr()) - static_cast<const char *>(q0->tail_addr()) < 64);
        CHECK(line_disjoint(q1->tail_addr(), q1->head_addr()) && line_disjoint(q1->head_addr(), q1->buf_addr()) &&
              line_disjoint(q1->tail_addr(), q1->buf_addr()) && alignof(SpscV1<uint64_t, 8>) == kLine);
        // 카운터: 캐시 없는 판은 try_push 마다 head 를, try_pop 마다 tail 을 읽는다
        q1->reset(0);
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) q1->try_push(static_cast<uint64_t>(i));
        q1->try_push(8);                                                     // 꽉 참: 그래도 1번 읽음
        CHECK(q1->pstats().head_loads == 9u && q1->pstats().tail_stores == 8u);
        for (int i = 0; i < 8; i++) q1->try_pop(v);
        q1->try_pop(v);
        CHECK(q1->cstats().tail_loads == 9u && q1->cstats().head_stores == 8u && v == 7u);
    }
    if (FAILS() == fa) {
        auto a0 = std::make_unique<SpscV0<uint64_t, kBig>>();
        auto t0 = std::make_unique<SpscV0<uint64_t, kTiny>>();
        auto a1 = std::make_unique<SpscV1<uint64_t, kBig>>();
        auto t1 = std::make_unique<SpscV1<uint64_t, kTiny>>();
        auto b1 = std::make_unique<SpscV1<uint64_t, kTiny>>();
        CHECK(stress_ok(stress<0>(*a0, kMsgs, 1), kMsgs) && stress_ok(stress<0>(*t0, kMsgs, 2), kMsgs));
        CHECK(stress_ok(stress<0>(*a1, kMsgs, 3), kMsgs) && stress_ok(stress<0>(*t1, kMsgs, 4), kMsgs));
        CHECK(pingpong_ok(*t1, *b1, kRounds));
    } else {
        skip(3, "Part A");
    }

    // ---------- Part B: v2 (상대 인덱스 캐시) / v3 (배치) ----------
    int fb = FAILS();
    {
        auto q2 = std::make_unique<SpscV2<uint64_t, 8>>();
        CHECK(unit_fifo(*q2, 0) && unit_fifo(*q2, kWrap));
        q2->reset(kWrap);
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) q2->try_push(static_cast<uint64_t>(i));
        CHECK(q2->pstats().head_loads == 0u);                                // 빈 칸이 8 개라고 이미 안다
        CHECK(!q2->try_push(8) && q2->pstats().head_loads == 1u);            // 꽉 참이라고 "보일 때" 만 다시 읽는다
        CHECK(q2->try_pop(v) && v == 0u && q2->cstats().tail_loads == 1u);   // 첫 pop: 캐시가 비어 1번 읽고 8 개를 안다
        for (int i = 1; i < 8; i++) q2->try_pop(v);
        CHECK(v == 7u && q2->cstats().tail_loads == 1u && q2->cstats().head_stores == 8u);
        CHECK(!q2->try_pop(v) && q2->cstats().tail_loads == 2u);
        CHECK(q2->try_push(42) && q2->pstats().head_loads == 2u && q2->try_pop(v) && v == 42u);

        // v3: push_n / pop_n — 인덱스 게시는 호출당 1번, 꽉 차면 부분 성공
        q2->reset(0);
        uint64_t src[16], dst[16] = {};
        for (int i = 0; i < 16; i++) src[i] = 1000u + static_cast<uint64_t>(i);
        CHECK(q2->push_n(src, 5) == 5u && q2->pstats().tail_stores == 1u && q2->pstats().head_loads == 0u);
        CHECK(q2->push_n(src + 5, 5) == 3u && q2->pstats().tail_stores == 2u && q2->pstats().head_loads == 1u);
        CHECK(q2->push_n(src + 8, 5) == 0u && q2->pstats().tail_stores == 2u && q2->pstats().head_loads == 2u);
        CHECK(q2->pop_n(dst, 4) == 4u && dst[0] == 1000u && dst[3] == 1003u && q2->cstats().tail_loads == 1u &&
              q2->cstats().head_stores == 1u);
        CHECK(q2->pop_n(dst, 10) == 4u && dst[0] == 1004u && dst[3] == 1007u && q2->cstats().tail_loads == 2u);
        CHECK(q2->pop_n(dst, 10) == 0u && q2->cstats().head_stores == 2u && q2->size_approx() == 0u);
    }
    if (FAILS() == fb) {
        auto a2 = std::make_unique<SpscV2<uint64_t, kBig>>();
        auto t2 = std::make_unique<SpscV2<uint64_t, kTiny>>();
        auto b2 = std::make_unique<SpscV2<uint64_t, kTiny>>();
        auto c2 = std::make_unique<SpscV2<uint64_t, 64>>();
        CHECK(stress_ok(stress<0>(*a2, kMsgs, 5), kMsgs) && stress_ok(stress<0>(*t2, kMsgs, 6), kMsgs));
        CHECK(stress_ok(stress<1>(*a2, kMsgs, 7), kMsgs) && stress_ok(stress<1>(*c2, kMsgs, 8), kMsgs));
        CHECK(pingpong_ok(*t2, *b2, kRounds));
    } else {
        skip(3, "Part B");
    }

    // ---------- Part C: v4 (슬롯 시퀀스) + BatchWriter ----------
    int fc = FAILS();
    {
        auto q4 = std::make_unique<SpscSlot<uint64_t, 8>>();
        CHECK(unit_fifo(*q4, 0) && unit_fifo(*q4, kWrap));
        q4->reset(0);
        uint64_t v = 0;
        CHECK(q4->try_push(7) && q4->slot_seq(0) == 1u && q4->slot_seq(1) == 1u);   // 슬롯0 = 찼음(0+1), 슬롯1 = 빔(1)
        CHECK(q4->try_pop(v) && v == 7u && q4->slot_seq(0) == 8u);                  // 반납 = 0+N

        auto q3 = std::make_unique<SpscV2<uint64_t, 8>>();
        BatchWriter<SpscV2<uint64_t, 8>, uint64_t, 4> bw(*q3, 4, 1000);
        CHECK(bw.add(1, 0) && bw.add(2, 100) && bw.add(3, 200) && q3->size_approx() == 0u && bw.flushes == 0u);
        CHECK(bw.poll(999) == 0u && bw.poll(1000) == 3u && bw.flushes == 1u && q3->pstats().tail_stores == 1u);
        CHECK(bw.add(4, 2000) && bw.add(5, 2001) && bw.add(6, 2002) && bw.add(7, 2003) && bw.flushes == 2u &&
              q3->size_approx() == 7u && bw.n == 0u);                                // 4 번째 add 가 바로 flush
        CHECK(bw.add(8, 3000) && bw.add(9, 3001) && bw.add(10, 3002) && bw.add(11, 3003) && bw.n == 3u &&
              q3->size_approx() == 8u);                                              // 큐에 1 칸 → 1 개만 나감
        CHECK(bw.add(12, 3004) && bw.n == 4u && !bw.add(13, 3005) && bw.poll(9999) == 0u);   // 큐 꽉 참 → 거절
        uint64_t d[8] = {};
        CHECK(q3->pop_n(d, 8) == 8u && d[0] == 1u && d[7] == 8u && bw.flush() == 4u && bw.n == 0u);
        CHECK(q3->pop_n(d, 8) == 4u && d[0] == 9u && d[3] == 12u && bw.sent == 12u);
    }
    if (FAILS() == fc) {
        auto a4 = std::make_unique<SpscSlot<uint64_t, kBig>>();
        auto t4 = std::make_unique<SpscSlot<uint64_t, kTiny>>();
        auto b4 = std::make_unique<SpscSlot<uint64_t, kTiny>>();
        CHECK(stress_ok(stress<0>(*a4, kMsgs, 9), kMsgs) && stress_ok(stress<0>(*t4, kMsgs, 10), kMsgs));
        CHECK(pingpong_ok(*t4, *b4, kRounds));
        // BatchWriter 를 낀 진짜 스레드: 가짜 시계 = 메시지 번호, max_batch 16, max_delay 5 "틱"
        auto q = std::make_unique<SpscV2<uint64_t, 64>>();
        std::atomic<bool> abort{false};
        StressResult r;
        std::thread prod([&] {
            BatchWriter<SpscV2<uint64_t, 64>, uint64_t, 16> w(*q, 16, 5);
            const uint64_t deadline = now_ns() + kDeadlineNs;
            for (uint64_t i = 1, spins = 0; i <= kMsgs || w.n != 0;) {
                const bool ok = i <= kMsgs ? w.add(i, i) : false;
                if (ok) i++;
                else w.flush();
                w.poll(i);
                if (!ok && (++spins & 4095) == 0 && now_ns() > deadline) { abort.store(true); return; }
            }
        });
        uint64_t expect = 1, buf[32];
        const uint64_t deadline = now_ns() + kDeadlineNs;
        for (uint64_t spins = 0; expect <= kMsgs && !abort.load(memory_order_relaxed);) {
            const size_t k = q->pop_n(buf, 32);
            for (size_t j = 0; j < k; j++, expect++) { r.bad += buf[j] != expect; r.got++; r.sum += buf[j]; }
            if (k == 0 && (++spins & 4095) == 0 && now_ns() > deadline) { abort.store(true); break; }
        }
        prod.join();
        r.timeout = abort.load();
        CHECK(stress_ok(r, kMsgs));
    } else {
        skip(3, "Part C");
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
