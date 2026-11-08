// LLAT-M02 추가 연습 — 한 번에 검증
//   연습 1  엔코더 스냅샷: "ISR" writer → main reader (강제 스케줄로 ISR 이 복사 도중에 온다)
//   연습 2  atomic_ref 판: C 와 공유하는 보통 구조체 그대로, 워드마다 std::atomic_ref
//   연습 3  writer 여러 개: std::mutex + seqcount (Linux seqlock_t 와 같은 구조)
//   연습 4  latch (복사본 2개): reader 가 쓰는 중인 writer 를 절대 기다리지 않는다 (Linux seqcount_latch)
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread practice.cpp -o practice && ./practice
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

using std::memory_order_acquire, std::memory_order_relaxed, std::memory_order_release;

struct Hook { void (*fn)(void *ctx, int step); void *ctx; };
static inline void hook_at(const Hook *h, int step) { if (h) h->fn(h->ctx, step); }

// 본문 Part A 와 같은 seqlock (워드 k 뒤 hook(k))
template <class T> struct SeqLock {
    static_assert(std::is_trivially_copyable_v<T> && sizeof(T) % 8 == 0);
    static constexpr std::size_t N = sizeof(T) / 8;
    using Words = std::array<std::uint64_t, N>;
    alignas(128) std::atomic<std::uint64_t> seq{0};
    std::atomic<std::uint64_t> w[N] = {};
};
template <class T> static void sl_store(SeqLock<T> &sl, const T &v, const Hook *h = nullptr) {
    const auto in = std::bit_cast<typename SeqLock<T>::Words>(v);
    const std::uint64_t s = sl.seq.load(memory_order_relaxed);
    sl.seq.store(s + 1, memory_order_relaxed);
    std::atomic_thread_fence(memory_order_release);
    for (std::size_t k = 0; k < SeqLock<T>::N; k++) { sl.w[k].store(in[k], memory_order_relaxed); hook_at(h, static_cast<int>(k)); }
    sl.seq.store(s + 2, memory_order_release);
}
template <class T> static bool sl_try_load(const SeqLock<T> &sl, T &out, const Hook *h = nullptr) {
    const std::uint64_t s1 = sl.seq.load(memory_order_acquire);
    if (s1 & 1u) return false;
    typename SeqLock<T>::Words tmp;
    for (std::size_t k = 0; k < SeqLock<T>::N; k++) { tmp[k] = sl.w[k].load(memory_order_relaxed); hook_at(h, static_cast<int>(k)); }
    std::atomic_thread_fence(memory_order_acquire);
    if (sl.seq.load(memory_order_relaxed) != s1) return false;
    out = std::bit_cast<T>(tmp);
    return true;
}
template <class T> static unsigned sl_load(const SeqLock<T> &sl, T &out, const Hook *h = nullptr) {
    unsigned r = 0;
    while (!sl_try_load(sl, out, h)) r++;
    return r;
}

// 체크섬 달린 범용 8 워드 스냅샷
struct Snap { std::uint64_t w[7], sum; };
static std::uint64_t snap_sum(const Snap &s) {
    std::uint64_t h = 0x9E3779B97F4A7C15ull;
    for (auto x : s.w) { h ^= x; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 31; }
    return h;
}
static Snap make_snap(std::uint64_t n) { Snap s{}; for (int i = 0; i < 7; i++) s.w[i] = n * 1000003u + static_cast<std::uint64_t>(i); s.sum = snap_sum(s); return s; }
static bool snap_ok(const Snap &s) { return s.sum == snap_sum(s); }

// ======================= 연습 1 — 엔코더 ISR → main =======================
struct EncSnap { std::int64_t pos; std::uint64_t ts_ns; std::int32_t vel; std::uint32_t idx; };   // 24 B = 3 워드
static_assert(sizeof(EncSnap) == 24);
struct EncIsrState { EncSnap cur{}; };                       // ISR 전용 사본 (ISR 만 고친다)
// 엔코더 ISR: 사본에 delta 를 반영하고 게시. idx = 지금까지 ISR 횟수.
static void enc_isr(SeqLock<EncSnap> &sl, EncIsrState &st, std::int32_t delta, std::uint64_t ts) {
    st.cur.pos += delta;
    st.cur.vel = delta;
    st.cur.ts_ns = ts;
    st.cur.idx++;
    sl_store(sl, st.cur);
}
static EncSnap enc_read(const SeqLock<EncSnap> &sl, unsigned *retries, const Hook *h = nullptr) {
    EncSnap s{};
    *retries = sl_load(sl, s, h);
    return s;
}
struct IsrInject { SeqLock<EncSnap> *sl; EncIsrState *st; int at; int times; int fired = 0; };
static void fire_isr(void *c, int step) {
    auto *x = static_cast<IsrInject *>(c);
    if (step == x->at && x->fired < x->times) { x->fired++; enc_isr(*x->sl, *x->st, 10, 5000u + static_cast<std::uint64_t>(x->fired)); }
}

// ======================= 연습 2 — atomic_ref 판 =======================
// C 헤더에 있을 법한 보통 구조체 (atomic 타입 없음). 동시에 접근되는 동안에는 **모든** 접근이 atomic_ref 여야 한다.
struct alignas(64) PlainSlot { std::uint64_t seq; std::uint64_t w[8]; };
static_assert(alignof(PlainSlot) >= std::atomic_ref<std::uint64_t>::required_alignment);
using ARef = std::atomic_ref<std::uint64_t>;
static void ar_store(PlainSlot &p, const Snap &v) {
    const auto in = std::bit_cast<std::array<std::uint64_t, 8>>(v);
    const std::uint64_t s = ARef(p.seq).load(memory_order_relaxed);
    ARef(p.seq).store(s + 1, memory_order_relaxed);
    std::atomic_thread_fence(memory_order_release);
    for (int k = 0; k < 8; k++) ARef(p.w[k]).store(in[static_cast<std::size_t>(k)], memory_order_relaxed);
    ARef(p.seq).store(s + 2, memory_order_release);
}
static bool ar_try_load(PlainSlot &p, Snap &out) {
    const std::uint64_t s1 = ARef(p.seq).load(memory_order_acquire);
    if (s1 & 1u) return false;
    std::array<std::uint64_t, 8> t;
    for (int k = 0; k < 8; k++) t[static_cast<std::size_t>(k)] = ARef(p.w[k]).load(memory_order_relaxed);
    std::atomic_thread_fence(memory_order_acquire);
    if (ARef(p.seq).load(memory_order_relaxed) != s1) return false;
    out = std::bit_cast<Snap>(t);
    return true;
}

// ======================= 연습 3 — writer 여러 개: mutex + seqcount =======================
struct LockedSeq { std::mutex wm; SeqLock<Snap> sl; };      // writer 끼리만 mutex, reader 는 여전히 무락
static void locked_store(LockedSeq &l, const Snap &v) {
    std::lock_guard<std::mutex> g(l.wm);                   // 한 번에 writer 하나 → 그 안은 단일 writer seqlock
    sl_store(l.sl, v);
}

// ======================= 연습 4 — latch (복사본 2개) =======================
// seq 홀수 → copy[0] 을 고치는 중이니 reader 는 copy[1], 짝수 → copy[1] 을 고치는 중이거나 쉬는 중이니 copy[0].
template <class T> struct Latch { alignas(128) std::atomic<std::uint64_t> seq{0}; SeqLock<T> copy[2]; };
template <class T> static void latch_store(Latch<T> &l, const T &v, const Hook *h = nullptr) {
    const auto in = std::bit_cast<typename SeqLock<T>::Words>(v);
    const std::uint64_t s = l.seq.load(memory_order_relaxed);
    l.seq.store(s + 1, memory_order_release);                // reader 를 copy[1] 로 — 지난번 copy[1] 쓰기가 먼저 보여야 하므로 release
    std::atomic_thread_fence(memory_order_release);
    for (std::size_t k = 0; k < SeqLock<T>::N; k++) { l.copy[0].w[k].store(in[k], memory_order_relaxed); hook_at(h, static_cast<int>(k)); }
    l.seq.store(s + 2, memory_order_release);                // reader 를 copy[0] 으로 (이제 새 값)
    std::atomic_thread_fence(memory_order_release);
    for (std::size_t k = 0; k < SeqLock<T>::N; k++) l.copy[1].w[k].store(in[k], memory_order_relaxed);
}
template <class T> static bool latch_try_load(const Latch<T> &l, T &out) {
    const std::uint64_t s = l.seq.load(memory_order_acquire);
    const auto &c = l.copy[s & 1u];
    typename SeqLock<T>::Words tmp;
    for (std::size_t k = 0; k < SeqLock<T>::N; k++) tmp[k] = c.w[k].load(memory_order_relaxed);
    std::atomic_thread_fence(memory_order_acquire);
    if (l.seq.load(memory_order_relaxed) != s) return false;
    out = std::bit_cast<T>(tmp);
    return true;
}
struct LatchInject { Latch<Snap> *l; int at; bool ok = false; Snap seen{}; int fired = 0; };
static void latch_read_mid(void *c, int step) {
    auto *x = static_cast<LatchInject *>(c);
    if (step == x->at && !x->fired) { x->fired = 1; x->ok = latch_try_load(*x->l, x->seen); }
}

// ======================= 하네스 =======================
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

template <class Wr, class Rd> static void stress(int nwriters, std::uint64_t K, Wr wr, Rd rd, std::uint64_t &torn, std::uint64_t &reads) {
    std::atomic<int> left{nwriters};
    std::uint64_t t[2] = {}, r[2] = {};
    std::vector<std::thread> ts;
    for (int w = 0; w < nwriters; w++)
        ts.emplace_back([&, w] {
            for (std::uint64_t n = 1; n <= K; n++) {
                wr(make_snap(static_cast<std::uint64_t>(w) << 40 | n));
                if (n % 8 == 0) std::this_thread::yield();         // reader 에게 틈
            }
            left.fetch_sub(1, memory_order_release);
        });
    for (int i = 0; i < 2; i++)
        ts.emplace_back([&, i] {
            Snap s;
            while (left.load(memory_order_acquire) > 0) { rd(s); r[i]++; if (!snap_ok(s)) t[i]++; }
        });
    for (auto &th : ts) th.join();
    torn = t[0] + t[1];
    reads = r[0] + r[1];
}

static SeqLock<EncSnap> g_enc;
static PlainSlot g_plain;
static LockedSeq g_locked;
static Latch<Snap> g_latch;

int main() {
    // 연습 1
    {
        SeqLock<EncSnap> sl;
        EncIsrState st;
        enc_isr(sl, st, 5, 100);
        unsigned rt = 99;
        EncSnap s = enc_read(sl, &rt);
        CHECK(rt == 0 && s.pos == 5 && s.idx == 1u && s.ts_ns == 100u);
        IsrInject x{&sl, &st, 1, 1};                                         // main 이 워드 1 을 읽은 직후 ISR
        Hook h{fire_isr, &x};
        s = enc_read(sl, &rt, &h);
        CHECK(x.fired == 1 && rt == 1 && s.pos == 15 && s.idx == 2u && s.vel == 10 && s.ts_ns == 5001u);   // 재시도 1번, ISR 뒤 값
        IsrInject y{&sl, &st, 2, 3};                                         // 세 번 연속 방해
        Hook hy{fire_isr, &y};
        s = enc_read(sl, &rt, &hy);
        CHECK(rt == 3 && s.pos == 45 && s.idx == 5u);
    }
    {
        std::atomic<bool> stop{false};                                       // ISR 을 스레드로: 20만 번 delta +1
        EncIsrState st;
        std::thread isr([&] { for (int i = 0; i < 200000; i++) enc_isr(g_enc, st, 1, static_cast<std::uint64_t>(i)); stop.store(true); });
        std::uint64_t bad = 0;
        while (!stop.load()) {
            unsigned rt;
            const EncSnap s = enc_read(g_enc, &rt);
            if (s.pos != static_cast<std::int64_t>(s.idx) || (s.idx && s.ts_ns != s.idx - 1u)) bad++;   // pos == idx, ts == idx-1
        }
        isr.join();
        unsigned rt;
        const EncSnap f = enc_read(g_enc, &rt);
        CHECK(bad == 0u && f.pos == 200000 && f.idx == 200000u);
    }
    // 연습 2
    {
        PlainSlot p{};
        Snap s{};
        CHECK(ar_try_load(p, s) && s.sum == 0u);                             // 0 으로 채운 초기 상태
        ar_store(p, make_snap(3));
        CHECK(p.seq == 2u && ar_try_load(p, s) && snap_ok(s) && s.w[0] == make_snap(3).w[0]);   // (스레드 없음 → 직접 읽어도 됨)
        std::uint64_t torn = 0, reads = 0;
        ar_store(g_plain, make_snap(0));                                     // 0 으로 채운 상태는 체크섬이 틀리니 먼저 한 번 게시
        stress(1, 200000, [](const Snap &v) { ar_store(g_plain, v); },
               [](Snap &o) { while (!ar_try_load(g_plain, o)) {} }, torn, reads);
        CHECK(torn == 0u && ARef(g_plain.seq).load() == 2 * 200001u);
    }
    // 연습 3
    {
        std::uint64_t torn = 0, reads = 0;
        locked_store(g_locked, make_snap(0));
        stress(2, 100000, [](const Snap &v) { locked_store(g_locked, v); },
               [](Snap &o) { sl_load(g_locked.sl, o); }, torn, reads);
        CHECK(torn == 0u);
        CHECK(g_locked.sl.seq.load() == 2 * (2 * 100000u + 1));              // 쓰기 20만+1 번이 하나도 안 사라짐
    }
    // 연습 4
    {
        Latch<Snap> l;
        latch_store(l, make_snap(1));
        Snap s{};
        CHECK(l.seq.load() == 2u && latch_try_load(l, s) && snap_ok(s) && s.w[0] == make_snap(1).w[0]);
        LatchInject x{&l, 3};                                                // writer 가 copy[0] 워드 3 을 쓴 직후 reader
        Hook h{latch_read_mid, &x};
        latch_store(l, make_snap(2), &h);
        CHECK(x.fired == 1 && x.ok && snap_ok(x.seen) && x.seen.w[0] == make_snap(1).w[0]);   // 첫 시도에 성공, 옛 값
        CHECK(latch_try_load(l, s) && s.w[0] == make_snap(2).w[0] && l.seq.load() == 4u);
        std::uint64_t torn = 0, reads = 0;
        latch_store(g_latch, make_snap(0));                                  // 두 복사본 모두 유효한 값으로 시작
        stress(1, 200000, [](const Snap &v) { latch_store(g_latch, v); },
               [](Snap &o) { while (!latch_try_load(g_latch, o)) {} }, torn, reads);
        CHECK(torn == 0u);
    }
    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
