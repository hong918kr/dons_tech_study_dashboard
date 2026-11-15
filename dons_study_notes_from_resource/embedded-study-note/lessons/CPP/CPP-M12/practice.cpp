// CPP-M12 추가 연습: MultiDelegate / C HAL 다리 / ROM 디스패치 테이블 / FunctionRef 재시도
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

// ----- 본문 Delegate/FunctionRef 의 축소판 (레슨은 파일마다 자립) -----
template <typename Sig> class Delegate;
template <typename R, typename... A>
class Delegate<R(A...)> {
    using Stub = R (*)(void*, A...);
    void* obj_ = nullptr;
    Stub  stub_ = nullptr;
    constexpr Delegate(void* o, Stub s) noexcept : obj_{o}, stub_{s} {}
    template <auto F> static R fn_stub(void*, A... a) { return std::invoke(F, std::forward<A>(a)...); }
    template <auto M, typename T>
    static R obj_stub(void* p, A... a) { return std::invoke(M, *static_cast<T*>(p), std::forward<A>(a)...); }
public:
    constexpr Delegate() noexcept = default;
    template <auto F> [[nodiscard]] static constexpr Delegate bind() noexcept { return {nullptr, &fn_stub<F>}; }
    template <auto M, typename T>
    [[nodiscard]] static constexpr Delegate bind(T& o) noexcept {
        return {const_cast<void*>(static_cast<const void*>(std::addressof(o))), &obj_stub<M, T>};
    }
    template <auto M, typename T> static Delegate bind(const T&&) = delete;
    constexpr explicit operator bool() const noexcept { return stub_ != nullptr; }
    R operator()(A... a) const { return stub_(obj_, std::forward<A>(a)...); }
    friend constexpr bool operator==(const Delegate&, const Delegate&) = default;
};

template <typename Sig> class FunctionRef;
template <typename R, typename... A>
class FunctionRef<R(A...)> {
    void* obj_;
    R (*thunk_)(void*, A...);
public:
    template <typename F>
        requires (!std::is_same_v<std::remove_cvref_t<F>, FunctionRef> && std::is_invocable_r_v<R, F&, A...>)
    FunctionRef(F&& f) noexcept
        : obj_{const_cast<void*>(static_cast<const void*>(std::addressof(f)))},
          thunk_{[](void* p, A... a) -> R {
              return std::invoke(*static_cast<std::remove_reference_t<F>*>(p), std::forward<A>(a)...);
          }} {}
    R operator()(A... a) const { return thunk_(obj_, std::forward<A>(a)...); }
};

// ----- 하네스 -----
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static std::uint32_t xorshift32(std::uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

// ----- 연습 1: 고정 용량 멀티캐스트 -----
template <typename Sig, std::size_t N> class MultiDelegate;
template <typename... A, std::size_t N>
class MultiDelegate<void(A...), N> {
    using D = Delegate<void(A...)>;
    D slots_[N]{};
public:
    bool add(D d) {
        if (!d) return false;
        for (const D& s : slots_) if (s == d) return false;   // 중복 금지
        for (D& s : slots_) if (!s) { s = d; return true; }
        return false;
    }
    bool remove(D d) {                                        // 자리를 비울 뿐 당기지 않는다
        for (D& s : slots_) if (s == d && s) { s = D{}; return true; }
        return false;
    }
    void fire(A... a) const {                                 // forward 금지: 구독자가 여럿이다
        for (std::size_t i = 0; i < N; ++i) { D d = slots_[i]; if (d) d(a...); }
    }
    std::size_t size() const { std::size_t n = 0; for (const D& s : slots_) n += s ? 1u : 0u; return n; }
};

struct Sink { int got = 0; void on(int v) { got += v; } };
struct SelfRemove {
    MultiDelegate<void(int), 3>* m; int n = 0;
    void on(int) { ++n; m->remove(Delegate<void(int)>::bind<&SelfRemove::on>(*this)); }
};

// ----- 연습 2: void* 컨텍스트를 받는 C HAL 에 Delegate 연결 -----
extern "C" {
typedef void (*hal_cb_t)(void* arg, int status);
static hal_cb_t g_hal_cb;
static void*    g_hal_arg;
static void hal_dma_start(hal_cb_t cb, void* arg) { g_hal_cb = cb; g_hal_arg = arg; }
static void hal_dma_irq(int status) { if (g_hal_cb) g_hal_cb(g_hal_arg, status); }
}
class DmaDriver {
    Delegate<void(int)> done_;
    static void trampoline(void* arg, int status) { (*static_cast<const Delegate<void(int)>*>(arg))(status); }
public:
    void start(Delegate<void(int)> done) { done_ = done; hal_dma_start(&trampoline, &done_); }
};
struct App { int last = -1; void on_dma(int s) { last = s; } };

// ----- 연습 3: ROM(constexpr) 디스패치 테이블 + 컴파일타임 누락 검사 -----
enum class Irq : std::uint8_t { Tim2, Usart1, Exti0, Count };
struct Periph { unsigned hits = 0; void isr() { ++hits; } };
static Periph g_tim2, g_usart1, g_exti0;
using IsrFn = Delegate<void()>;
static constexpr std::array<IsrFn, static_cast<std::size_t>(Irq::Count)> kIsrTable = {
    IsrFn::bind<&Periph::isr>(g_tim2),
    IsrFn::bind<&Periph::isr>(g_usart1),
    IsrFn::bind<&Periph::isr>(g_exti0),
};
template <std::size_t N>
consteval bool all_bound(const std::array<IsrFn, N>& t) {
    for (const IsrFn& f : t) if (!f) return false;
    return true;
}
static_assert(all_bound(kIsrTable), "ISR table has an unbound entry");
static void dispatch(Irq i) { kIsrTable[static_cast<std::size_t>(i)](); }

// ----- 연습 4: 힙 없는 재시도 (캡처 람다 + FunctionRef) -----
struct RetryResult { bool ok; unsigned tries; };
static RetryResult retry(FunctionRef<bool()> op, unsigned max_tries, FunctionRef<void(unsigned)> backoff) {
    for (unsigned t = 1; t <= max_tries; ++t) {
        if (op()) return {true, t};
        if (t != max_tries) backoff(t);
    }
    return {false, max_tries};
}

int main() {
    using D = Delegate<void(int)>;
    // 1. MultiDelegate
    {
        MultiDelegate<void(int), 3> m;
        Sink a, b, c, d;
        CHECK(m.add(D::bind<&Sink::on>(a)) && m.add(D::bind<&Sink::on>(b)));
        CHECK(!m.add(D::bind<&Sink::on>(a)));                       // 중복
        CHECK(!m.add(D{}));                                         // 빈 delegate
        CHECK(m.add(D::bind<&Sink::on>(c)) && !m.add(D::bind<&Sink::on>(d)));   // 가득 참
        m.fire(5);
        CHECK(a.got == 5 && b.got == 5 && c.got == 5 && d.got == 0);
        CHECK(m.remove(D::bind<&Sink::on>(b)) && !m.remove(D::bind<&Sink::on>(b)));
        m.fire(1);
        CHECK(a.got == 6 && b.got == 5 && c.got == 6 && m.size() == 2);
        CHECK(m.add(D::bind<&Sink::on>(d)) && m.size() == 3);       // 빈자리 재사용
    }
    {
        MultiDelegate<void(int), 3> m;
        SelfRemove s{&m}; Sink after;
        CHECK(m.add(D::bind<&SelfRemove::on>(s)) && m.add(D::bind<&Sink::on>(after)));
        m.fire(7);                                                  // 순회 중 자기 제거: 다음 구독자를 건너뛰면 안 된다
        CHECK(s.n == 1 && after.got == 7 && m.size() == 1);
        m.fire(1);
        CHECK(s.n == 1 && after.got == 8);
    }
    {   // 고정 시드 무작위 add/remove vs 단순 모델
        constexpr std::size_t K = 8;
        MultiDelegate<void(int), 4> m;
        Sink sinks[K];
        bool in[K] = {};
        int expect[K] = {};
        std::uint32_t seed = 0x1234'5678u;
        int bad = 0;
        for (int i = 0; i < 5000; ++i) {
            const std::size_t k = xorshift32(seed) % K;
            std::size_t cnt = 0;
            for (bool b : in) cnt += b ? 1u : 0u;
            if (xorshift32(seed) & 1u) {
                bool want = !in[k] && cnt < 4;
                if (m.add(D::bind<&Sink::on>(sinks[k])) != want) ++bad;
                if (want) in[k] = true;
            } else {
                if (m.remove(D::bind<&Sink::on>(sinks[k])) != in[k]) ++bad;
                in[k] = false;
            }
            m.fire(1);
            for (std::size_t j = 0; j < K; ++j) expect[j] += in[j] ? 1 : 0;
        }
        for (std::size_t j = 0; j < K; ++j) if (sinks[j].got != expect[j]) ++bad;
        CHECK(bad == 0);
    }

    // 2. C HAL 다리
    {
        App app; DmaDriver drv;
        drv.start(D::bind<&App::on_dma>(app));
        hal_dma_irq(3);
        CHECK(app.last == 3);
        CHECK(g_hal_arg != static_cast<void*>(&app));               // arg 는 Delegate 의 주소다
        App other;
        drv.start(D::bind<&App::on_dma>(other));
        hal_dma_irq(9);
        CHECK(other.last == 9 && app.last == 3);
        g_hal_cb = nullptr;
    }

    // 3. ROM 디스패치 테이블
    {
        dispatch(Irq::Usart1); dispatch(Irq::Usart1); dispatch(Irq::Exti0);
        CHECK(g_tim2.hits == 0 && g_usart1.hits == 2 && g_exti0.hits == 1);
        CHECK(std::is_trivially_copyable_v<IsrFn> && sizeof(IsrFn) == 2 * sizeof(void*));
    }

    // 4. 재시도 (표 기반)
    {
        struct Case { unsigned fail_first, max; bool ok; unsigned tries, backoffs; };
        const Case cases[] = {
            {0, 3, true, 1, 0},
            {1, 3, true, 2, 1},
            {2, 3, true, 3, 2},
            {3, 3, false, 3, 2},
            {9, 1, false, 1, 0},
        };
        int bad = 0;
        for (const Case& c : cases) {
            unsigned calls = 0, backoffs = 0, last_arg = 0;
            auto r = retry([&] { return ++calls > c.fail_first; },
                           c.max,
                           [&](unsigned t) { ++backoffs; last_arg = t; });
            if (r.ok != c.ok || r.tries != c.tries || backoffs != c.backoffs) ++bad;
            if (c.backoffs != 0 && last_arg != c.backoffs) ++bad;
        }
        CHECK(bad == 0);
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
