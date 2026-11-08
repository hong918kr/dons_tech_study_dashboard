// LLAT-M02 Seqlock: 단일 writer 스냅샷 공유
//
// 호스트 모델: 피드 핸들러 스레드(writer 1개)가 종목별 최우선 호가(top of book) 64 B 스냅샷을 계속 덮어쓰고,
// 전략 스레드(reader 여러 개)가 writer 를 한 번도 막지 않고 "한 순간의 온전한" 스냅샷을 복사해 간다.
//   Part A  Boehm(2012) 형태의 C++ seqlock: atomic seq + relaxed atomic 워드 복사 + fence 두 개
//   Part B  깨지는 경우를 강제 스케줄(hook)로 재현: 검사 없는 복사, 홀수 검사 누락, writer 2개 →
//           고친 것: CAS 로 writer 끼리 배제(try_store_mw), 재시도 상한(load_bounded)
//   Part C  시장 데이터 북: 종목 4개 × seqlock, 피드 writer 는 자기 master 사본만 읽는다,
//           전략 reader 는 체크섬·seq_no 단조성 검사 + 재시도 히스토그램
//
// 체크 테스트는 "올바른 구현이면 매 실행 항상 성립하는 것" 만 본다 (torn 0 개, 정확한 버전 수, 강제 스케줄 결과).
// 재시도 히스토그램·지연 숫자는 출력만 한다 (demo_bench.cpp / demo_torn.cpp 가 측정 담당).
//
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread solution.cpp -o so && ./so   (starter.cpp 도 같은 명령)
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <type_traits>
#include <vector>

using std::memory_order_acquire, std::memory_order_relaxed, std::memory_order_release;

// ======================= 공용: 스냅샷 타입과 강제 스케줄 hook =======================
// 64 B 최우선 호가. 패딩 없음 (4+4+8+8+8+8+4+4+8+8). checksum = 앞 7 워드의 해시 → 섞인(torn) 복사 검출.
struct Quote {
    std::uint32_t inst, flags;
    std::uint64_t seq_no;            // 종목별 피드 시퀀스 (업데이트마다 +1)
    std::uint64_t ts_ns;             // 거래소 타임스탬프
    std::int64_t bid_px, ask_px;     // 가격 (1e-4 단위 정수)
    std::uint32_t bid_qty, ask_qty;
    std::int64_t last_px;
    std::uint64_t checksum;
};
static_assert(sizeof(Quote) == 64 && std::is_trivially_copyable_v<Quote>);

static std::uint64_t quote_sum(const Quote &q) {
    const auto w = std::bit_cast<std::array<std::uint64_t, 8>>(q);
    std::uint64_t h = 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 7; i++) { h ^= w[i]; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 31; }
    return h;
}
static bool quote_ok(const Quote &q) { return q.checksum == quote_sum(q); }
static bool same(const Quote &a, const Quote &b) { return std::memcmp(&a, &b, sizeof a) == 0; }
// n 번째 버전: 8 워드가 전부 n 에 따라 바뀐다 → 어느 두 버전을 섞어도 체크섬이 틀린다.
static Quote make_quote(std::uint32_t inst, std::uint64_t n) {
    Quote q{};
    q.inst = inst;
    q.flags = static_cast<std::uint32_t>(n * 2654435761u);
    q.seq_no = n;
    q.ts_ns = 1'700'000'000'000'000'000ull + n * 1000u;
    q.bid_px = 1'000'000 + static_cast<std::int64_t>(n % 977u);
    q.ask_px = q.bid_px + 1 + static_cast<std::int64_t>(n % 3u);
    q.bid_qty = static_cast<std::uint32_t>(100u + n % 1000u);
    q.ask_qty = static_cast<std::uint32_t>(200u + n % 777u);
    q.last_px = q.bid_px + static_cast<std::int64_t>(n & 1u);
    q.checksum = quote_sum(q);
    return q;
}

// 강제 스케줄: writer 는 데이터 워드 k 를 쓴 직후, reader 는 워드 k 를 읽은 직후 hook(k) 를 부른다.
// 테스트는 hook 안에서 "다른 스레드" 의 동작(쓰기/읽기)을 같은 스레드에서 실행해 인터리빙을 고정한다.
struct Hook { void (*fn)(void *ctx, int step); void *ctx; };
static inline void hook_at(const Hook *h, int step) { if (h) h->fn(h->ctx, step); }

// 시퀀스 + 데이터 워드. 데이터도 atomic 워드라서 reader 와 writer 가 겹쳐도 data race(UB) 가 아니다.
// alignas(128): 이 맥(M6)의 캐시 라인 128 B (sysctl hw.cachelinesize) — 종목마다 라인 하나.
template <class T> struct SeqLock {
    static_assert(std::is_trivially_copyable_v<T> && sizeof(T) % 8 == 0);
    static constexpr std::size_t N = sizeof(T) / 8;
    using Words = std::array<std::uint64_t, N>;
    alignas(128) std::atomic<std::uint64_t> seq{0};    // 짝수 = 안정, 홀수 = 쓰는 중, seq/2 = 쓴 횟수
    std::atomic<std::uint64_t> w[N] = {};
};
template <class T> static std::uint64_t sl_version(const SeqLock<T> &sl) { return sl.seq.load(memory_order_acquire) / 2; }

// ======================= Part A — 단일 writer seqlock =======================
// A1. writer (반드시 하나). seq 를 홀수로 → release fence → 데이터 워드 relaxed store (워드 k 뒤 hook(k)) → seq 짝수 release store.
//     writer 는 seq 를 바꾸는 유일한 스레드라 현재 값은 relaxed load 로 충분하다.
template <class T> static void sl_store(SeqLock<T> &sl, const T &v, const Hook *h = nullptr) {
    (void)sl; (void)v; (void)h; // TODO
}
// A2. reader 1회 시도. seq acquire load (홀수면 false) → 워드 relaxed load (워드 k 뒤 hook(k)) →
//     acquire fence → seq relaxed 재로드. 두 값이 같을 때만 out 에 쓰고 true. 실패하면 out 은 그대로.
template <class T> static bool sl_try_load(const SeqLock<T> &sl, T &out, const Hook *h = nullptr) {
    (void)sl; (void)out; (void)h;
    return false; // TODO
}
// A3. 성공할 때까지 sl_try_load 반복 (hook 은 매 시도에 그대로 넘긴다). 반환 = 실패한 시도 수(재시도 횟수).
template <class T> static unsigned sl_load(const SeqLock<T> &sl, T &out, const Hook *h = nullptr) {
    (void)sl; (void)out; (void)h;
    return ~0u; // TODO (스텁은 루프를 돌지 않는다 — 시작 코드가 멈추지 않게)
}

// ======================= Part B — 무엇이 깨지나, 어떻게 막나 =======================
// (제공) 깨진 reader 두 개 — 데이터는 atomic 워드라 UB 는 아니지만 "일관성" 이 없다.
template <class T> static T naive_load(const SeqLock<T> &sl, const Hook *h = nullptr) {   // seq 를 아예 안 본다
    typename SeqLock<T>::Words tmp;
    for (std::size_t k = 0; k < SeqLock<T>::N; k++) { tmp[k] = sl.w[k].load(memory_order_relaxed); hook_at(h, static_cast<int>(k)); }
    return std::bit_cast<T>(tmp);
}
template <class T> static bool load_no_odd_check(const SeqLock<T> &sl, T &out) {           // s1 == s2 만 본다
    const std::uint64_t s1 = sl.seq.load(memory_order_acquire);
    typename SeqLock<T>::Words tmp;
    for (std::size_t k = 0; k < SeqLock<T>::N; k++) tmp[k] = sl.w[k].load(memory_order_relaxed);
    std::atomic_thread_fence(memory_order_acquire);
    if (sl.seq.load(memory_order_relaxed) != s1) return false;
    out = std::bit_cast<T>(tmp);
    return true;
}

// B1. writer 가 여럿일 때: seq 를 짝수→홀수로 CAS 해서 "쓰기 권한" 을 얻는다 (실패 = 다른 writer 가 쓰는 중 → false,
//     아무것도 안 씀). CAS 성공은 acquire (앞 writer 의 데이터 store 뒤에 내 store 가 오게), 그 뒤 release fence,
//     데이터 relaxed store (워드 k 뒤 hook(k)), seq 짝수 release store. 반환 = 썼는가.
template <class T> static bool sl_try_store_mw(SeqLock<T> &sl, const T &v, const Hook *h = nullptr) {
    (void)sl; (void)v; (void)h;
    return false; // TODO
}
// B2. 실시간 reader: 최대 max_tries 번만 시도. 성공하면 사용한 시도 횟수(1..max_tries), 다 실패하면 -1 (out 그대로).
template <class T> static int sl_load_bounded(const SeqLock<T> &sl, T &out, int max_tries, const Hook *h = nullptr) {
    (void)sl; (void)out; (void)max_tries; (void)h;
    return 0; // TODO
}

// ======================= Part C — 시장 데이터 북 =======================
constexpr int kInst = 4;
constexpr int kHist = 7;                                     // 재시도 0, 1, 2, 3, 4–7, 8–15, 16+
[[maybe_unused]] static int retry_bucket(unsigned r) { return r < 4 ? static_cast<int>(r) : r < 8 ? 4 : r < 16 ? 5 : 6; }

struct Book { SeqLock<Quote> q[kInst]; };                    // 공유: 종목마다 seqlock 하나 (128 B 정렬)
struct Feed { Quote master[kInst]; std::uint64_t updates = 0; };   // writer 전용 사본 — reader 는 절대 안 본다
struct Update { std::uint32_t inst, side; std::int64_t px; std::uint32_t qty; std::uint64_t ts_ns; };  // side 0 bid, 1 ask, 2 trade
struct Reader { std::uint64_t last_seq[kInst] = {}; std::uint64_t reads = 0, fresh = 0, torn = 0, regress = 0, hist[kHist] = {}; };

static void feed_init(Book &b, Feed &f) {
    for (int i = 0; i < kInst; i++) {
        f.master[i] = make_quote(static_cast<std::uint32_t>(i), 0);
        sl_store(b.q[i], f.master[i]);
    }
}
// C1. 피드 writer: master[u.inst] 를 고친다 (seq_no+1, ts_ns, flags = side, side 별로 bid / ask 가격·수량, 또는 trade 면 last_px),
//     checksum 을 다시 계산하고 sl_store 로 게시, updates++. 공유 북에서 읽어 오지 않는다 (writer 는 자기 사본이 진실).
static void feed_on_update(Book &b, Feed &f, const Update &u) {
    (void)b; (void)f; (void)u; // TODO
}
// C2. 전략 reader: sl_load 로 복사 → reads++, hist[retry_bucket(재시도)]++ → 체크섬이 틀리면 torn++ 하고 false →
//     seq_no 가 이 종목에서 본 마지막 값보다 작으면 regress++ 하고 false → 더 크면 fresh++, last_seq 갱신, true.
//     같으면(새 소식 없음) false.
static bool strat_poll(const Book &b, int inst, Reader &r, Quote &out) {
    (void)b; (void)inst; (void)r; (void)out;
    return false; // TODO
}

// ======================= 테스트 하네스 =======================
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define FAILS() (g_total - g_pass)
static void skip(int n, const char *what) {
    g_total += n;
    std::printf("SKIP %d stress checks (%s): unit checks failed\n", n, what);
}
static void gap_ns(long ns) {                        // writer 를 잠깐 쉬게 하는 busy-wait (reader 가 성공할 틈)
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::nanoseconds(ns)) {}
}
static std::uint32_t xorshift32(std::uint32_t &s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

// hook 컨텍스트: step == at 일 때 times 번까지 "다른 스레드" 동작을 끼워 넣는다.
struct Inject {
    SeqLock<Quote> *sl; int at; int times; std::uint64_t next_n;   // 끼워 넣을 쓰기의 버전 번호
    int fired = 0; bool reader_ok = true; Quote seen{};            // reader 를 끼운 경우의 결과
};
static void inject_write(void *c, int step) {        // reader 도중 writer 가 한 번 통째로 지나간다
    auto *x = static_cast<Inject *>(c);
    if (step == x->at && x->fired < x->times) { x->fired++; sl_store(*x->sl, make_quote(7, x->next_n++)); }
}
static void inject_try_load(void *c, int step) {     // writer 도중 reader 가 한 번 시도한다
    auto *x = static_cast<Inject *>(c);
    if (step == x->at && x->fired < x->times) { x->fired++; x->reader_ok = sl_try_load(*x->sl, x->seen); }
}
static void inject_no_odd(void *c, int step) {       // writer 도중 "홀수 검사 없는" reader
    auto *x = static_cast<Inject *>(c);
    if (step == x->at && x->fired < x->times) { x->fired++; x->reader_ok = load_no_odd_check(*x->sl, x->seen); }
}
static void inject_writer2(void *c, int step) {      // writer A 도중 writer B 가 단일 writer 용 sl_store 로 끼어든다
    auto *x = static_cast<Inject *>(c);
    if (step == x->at && x->fired < x->times) { x->fired++; sl_store(*x->sl, make_quote(8, x->next_n++)); }
}
static void inject_writer2_mw(void *c, int step) {   // 같은 상황, 둘 다 sl_try_store_mw
    auto *x = static_cast<Inject *>(c);
    if (step == x->at && x->fired < x->times) { x->fired++; x->reader_ok = sl_try_store_mw(*x->sl, make_quote(8, x->next_n++)); }
}

static SeqLock<Quote> g_sl;
static Book g_book;

int main() {
    // ---------- Part A ----------
    int fa = FAILS();
    {
        SeqLock<Quote> sl;
        Quote out = make_quote(0, 0), q1 = make_quote(7, 1);
        sl_store(sl, q1);
        CHECK(sl.seq.load() == 2u && sl_version(sl) == 1u);                 // 쓰기 1번 = seq 0→2
        CHECK(sl_try_load(sl, out) && same(out, q1));
        sl_store(sl, make_quote(7, 2));
        CHECK(sl.seq.load() == 4u && sl_try_load(sl, out) && out.seq_no == 2u && quote_ok(out));

        Inject x{&sl, 3, 1, 3};                                             // reader 가 워드 3 을 읽은 직후 쓰기
        Hook h{inject_write, &x};
        Quote before = out;
        CHECK(!sl_try_load(sl, out, &h) && same(out, before));              // 실패, out 은 그대로
        CHECK(x.fired == 1 && sl_try_load(sl, out) && out.seq_no == 3u && quote_ok(out));

        Inject y{&sl, 3, 1, 0};                                             // writer 가 워드 3 을 쓴 직후 reader 시도
        Hook hy{inject_try_load, &y};
        sl_store(sl, make_quote(7, 4), &hy);
        CHECK(y.fired == 1 && !y.reader_ok);                                // 홀수 seq → 시도 실패
        CHECK(sl.seq.load() == 8u && sl_try_load(sl, out) && out.seq_no == 4u);

        Inject z{&sl, 5, 2, 5};                                             // 처음 두 시도 도중 쓰기 → 재시도 2
        Hook hz{inject_write, &z};
        CHECK(sl_load(sl, out, &hz) == 2u && out.seq_no == 6u && quote_ok(out));
        CHECK(sl_load(sl, out) == 0u && sl_version(sl) == 6u);
    }
    if (FAILS() == fa) {                                                    // 진짜 스레드: writer 1 + reader 3
        constexpr std::uint64_t K = 300000;
        sl_store(g_sl, make_quote(7, 0));
        std::atomic<bool> done{false};
        std::uint64_t torn[3] = {}, back[3] = {}, reads[3] = {}, retr[3] = {};
        std::vector<std::thread> rs;
        for (int t = 0; t < 3; t++)
            rs.emplace_back([&, t] {
                std::uint64_t last = 0;
                Quote q;
                while (!done.load(memory_order_acquire)) {
                    retr[t] += sl_load(g_sl, q);
                    reads[t]++;
                    if (!quote_ok(q)) torn[t]++;
                    if (q.seq_no < last) back[t]++;
                    last = q.seq_no;
                }
            });
        for (std::uint64_t n = 1; n <= K; n++) {
            sl_store(g_sl, make_quote(7, n));
            if (n % 4 == 0) gap_ns(1000);                                   // 4번에 1번 1 µs 쉼 (나머지는 연속 쓰기)
        }
        done.store(true, memory_order_release);
        for (auto &th : rs) th.join();
        std::printf("Part A stress: writes=%llu reads=%llu/%llu/%llu retries=%llu/%llu/%llu\n", (unsigned long long)K,
                    (unsigned long long)reads[0], (unsigned long long)reads[1], (unsigned long long)reads[2],
                    (unsigned long long)retr[0], (unsigned long long)retr[1], (unsigned long long)retr[2]);
        CHECK(torn[0] + torn[1] + torn[2] == 0u);                           // 찢어진 스냅샷 0 개
        CHECK(back[0] + back[1] + back[2] == 0u);                           // seq_no 가 뒤로 가지 않는다
        Quote q;
        CHECK(sl_version(g_sl) == K + 1 && sl_try_load(g_sl, q) && same(q, make_quote(7, K)));
    } else skip(3, "Part A");

    // ---------- Part B ----------
    int fb = FAILS();
    {
        SeqLock<Quote> sl;
        sl_store(sl, make_quote(7, 1));
        Inject x{&sl, 3, 1, 2};                                             // ① 검사 없는 복사 + 도중 쓰기
        Hook h{inject_write, &x};
        Quote t = naive_load(sl, &h);
        CHECK(x.fired == 1 && !quote_ok(t));                                // 워드 0–3 은 v1, 4–7 은 v2 → 찢어짐

        Inject y{&sl, 3, 1, 0};                                             // ② 홀수 검사 없는 reader 가 쓰기 한가운데서
        Hook hy{inject_no_odd, &y};
        sl_store(sl, make_quote(7, 3), &hy);
        CHECK(y.fired == 1 && y.reader_ok && !quote_ok(y.seen));            // s1 == s2 == 홀수 → 찢어진 걸 받아들임
        Inject y2{&sl, 3, 1, 0};                                            //    올바른 reader 는 같은 자리에서 거절
        Hook hy2{inject_try_load, &y2};
        sl_store(sl, make_quote(7, 4), &hy2);
        CHECK(y2.fired == 1 && !y2.reader_ok);
    }
    {
        SeqLock<Quote> sl;                                                  // ③ writer 2개가 단일 writer 용 sl_store
        sl_store(sl, make_quote(7, 1));                                     // seq 2
        Inject x{&sl, 3, 1, 1};
        Hook h{inject_writer2, &x};
        sl_store(sl, make_quote(7, 2), &h);                                 // A: 2→3 … B: 3→4(짝수!)→5 … A: →4
        Quote out{};
        CHECK(x.fired == 1 && sl.seq.load() == 4u);                         // seq 가 5 → 4 로 뒤로 갔다
        CHECK(sl_try_load(sl, out) && !quote_ok(out));                      // "안정" 처럼 보이는데 찢어져 있다
    }
    {
        SeqLock<Quote> sl;                                                  // B1 고친 것: CAS 로 writer 끼리 배제
        CHECK(sl_try_store_mw(sl, make_quote(7, 1)) && sl.seq.load() == 2u);
        Inject x{&sl, 3, 1, 1};
        Hook h{inject_writer2_mw, &x};
        CHECK(sl_try_store_mw(sl, make_quote(7, 2), &h));                   // A 는 성공
        Quote out{};
        CHECK(x.fired == 1 && !x.reader_ok);                                // B 는 홀수 seq 를 보고 물러남
        CHECK(sl.seq.load() == 4u && sl_try_load(sl, out) && same(out, make_quote(7, 2)));
        CHECK(sl_try_store_mw(sl, make_quote(8, 1)) && sl.seq.load() == 6u);   // A 가 끝난 뒤엔 B 성공

        Inject z{&sl, 2, 1000, 10};                                         // B2 매 시도마다 쓰기 → 상한 5 번 다 실패
        Hook hz{inject_write, &z};
        Quote keep = out;
        CHECK(sl_load_bounded(sl, out, 5, &hz) == -1 && same(out, keep) && z.fired == 5);
        Inject z2{&sl, 2, 2, 20};                                           // 처음 두 번만 방해 → 세 번째 시도에 성공
        Hook hz2{inject_write, &z2};
        CHECK(sl_load_bounded(sl, out, 5, &hz2) == 3 && out.seq_no == 21u && quote_ok(out));
    }
    if (FAILS() == fb) {                                                    // 진짜 스레드: writer 2 (CAS) + reader 2
        constexpr std::uint64_t K = 100000;
        static SeqLock<Quote> sl;
        sl_store(sl, make_quote(7, 0));
        std::atomic<int> writers_left{2};
        std::uint64_t torn[2] = {}, reads[2] = {};
        std::vector<std::thread> ts;
        for (int w = 0; w < 2; w++)
            ts.emplace_back([&, w] {
                for (std::uint64_t n = 1; n <= K; n++) {
                    const Quote q = make_quote(static_cast<std::uint32_t>(10 + w), n);
                    while (!sl_try_store_mw(sl, q)) std::this_thread::yield();
                    if (n % 4 == 0) gap_ns(1000);
                }
                writers_left.fetch_sub(1, memory_order_release);
            });
        for (int t = 0; t < 2; t++)
            ts.emplace_back([&, t] {
                Quote q;
                while (writers_left.load(memory_order_acquire) > 0) {
                    sl_load(sl, q);
                    reads[t]++;
                    if (!quote_ok(q)) torn[t]++;
                }
            });
        for (auto &th : ts) th.join();
        std::printf("Part B stress: 2 writers x %llu, reads=%llu/%llu\n", (unsigned long long)K,
                    (unsigned long long)reads[0], (unsigned long long)reads[1]);
        CHECK(torn[0] + torn[1] == 0u);
        CHECK(sl_version(sl) == 2 * K + 1);                                 // 쓰기가 하나도 안 사라졌다
    } else skip(2, "Part B");

    // ---------- Part C ----------
    int fc = FAILS();
    {
        Book *b = new Book{};
        Feed f{};
        feed_init(*b, f);
        Reader r{};
        Quote out{};
        CHECK(!strat_poll(*b, 1, r, out) && r.reads == 1u && r.torn == 0u && r.fresh == 0u);   // seq_no 0 = 새 소식 아님
        feed_on_update(*b, f, Update{1, 0, 1'000'500, 300, 111});
        feed_on_update(*b, f, Update{1, 1, 1'000'510, 400, 222});
        feed_on_update(*b, f, Update{2, 2, 999'999, 0, 333});
        CHECK(f.updates == 3u && f.master[1].seq_no == 2u && f.master[1].bid_px == 1'000'500 && f.master[1].ask_qty == 400u);
        CHECK(f.master[2].last_px == 999'999 && f.master[2].flags == 2u && quote_ok(f.master[2]));
        CHECK(strat_poll(*b, 1, r, out) && same(out, f.master[1]) && r.last_seq[1] == 2u);
        CHECK(!strat_poll(*b, 1, r, out) && r.fresh == 1u && r.reads == 3u);   // 바뀐 것 없음
        CHECK(sl_version(b->q[1]) == 3u && sl_version(b->q[0]) == 1u);       // init 1번 + 업데이트 2번
        CHECK(r.hist[0] == 3u && r.regress == 0u);
        delete b;
    }
    if (FAILS() == fc) {                                                    // 피드 1 + 전략 3, 업데이트 60만 개
        constexpr std::uint64_t K = 600000;
        static Feed f;
        feed_init(g_book, f);
        std::atomic<bool> done{false};
        Reader rd[3];
        std::vector<std::thread> rs;
        for (int t = 0; t < 3; t++)
            rs.emplace_back([&, t] {
                Quote q;
                int i = t;
                while (!done.load(memory_order_acquire)) { strat_poll(g_book, i, rd[t], q); i = (i + 1) % kInst; }
            });
        std::uint32_t s = 12345;
        for (std::uint64_t n = 0; n < K; n++) {
            const std::uint32_t r = xorshift32(s);
            feed_on_update(g_book, f, Update{r % kInst, (r >> 4) % 3, 1'000'000 + static_cast<std::int64_t>((r >> 8) % 500),
                                             (r >> 16) % 1000, n});
        }
        done.store(true, memory_order_release);
        for (auto &th : rs) th.join();
        std::uint64_t torn = 0, regress = 0, hist[kHist] = {};
        for (auto &r : rd) {
            torn += r.torn; regress += r.regress;
            for (int k = 0; k < kHist; k++) hist[k] += r.hist[k];
        }
        std::printf("Part C stress: updates=%llu reads=%llu/%llu/%llu fresh=%llu/%llu/%llu\n", (unsigned long long)f.updates,
                    (unsigned long long)rd[0].reads, (unsigned long long)rd[1].reads, (unsigned long long)rd[2].reads,
                    (unsigned long long)rd[0].fresh, (unsigned long long)rd[1].fresh, (unsigned long long)rd[2].fresh);
        std::printf("  retry histogram 0/1/2/3/4-7/8-15/16+: %llu %llu %llu %llu %llu %llu %llu\n", (unsigned long long)hist[0],
                    (unsigned long long)hist[1], (unsigned long long)hist[2], (unsigned long long)hist[3],
                    (unsigned long long)hist[4], (unsigned long long)hist[5], (unsigned long long)hist[6]);
        CHECK(torn == 0u && regress == 0u);                                 // 찢어짐 0, 역행 0
        bool final_ok = true;
        std::uint64_t vers = 0;
        for (int i = 0; i < kInst; i++) {
            Quote q;
            sl_load(g_book.q[i], q);
            final_ok = final_ok && same(q, f.master[i]);
            vers += sl_version(g_book.q[i]);
        }
        CHECK(final_ok);                                                    // 마지막 스냅샷 == writer 사본
        CHECK(f.updates == K && vers == K + kInst);                         // 업데이트 K + init 4
    } else skip(3, "Part C");

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
