// CPP-M12 무힙 콜백: std::function 대신 delegate / function_ref
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
//
// C 의 콜백:   void (*cb)(void* ctx, int v) + void* ctx   ← 타입 검사 없음, 캐스트는 사람 몫
// 이 레슨:     Delegate = { 문맥(ctx), 스텁(stub) } 두 워드. 캐스트는 컴파일러가 만든 스텁 안에 숨긴다.
//              힙 없음, trivially copyable, 소유권 없음(수명은 호출자 책임).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>    // std::invoke 만 쓴다 (std::function 은 demo 에서만 비교)
#include <memory>        // std::addressof
#include <type_traits>
#include <utility>

// ---------------- Part B: 제약 조건 ----------------
// F 를 A... 로 호출할 수 있고 결과가 R 로 (암시적) 변환되는가?  const 안 맞음, 시그니처 안 맞음 → false
template <typename R, typename F, typename... A>
inline constexpr bool callable_as_v = true; // TODO(Part B): 너무 느슨하다

// 캡처 없는 람다만 true (C++20: 캡처 없는 closure type 만 기본 생성 가능 [expr.prim.lambda.closure])
template <typename L>
inline constexpr bool captureless_v = true; // TODO(Part B): 너무 느슨하다

// ---------------- Part A/B: Delegate ----------------
template <typename Sig> class Delegate;

template <typename R, typename... Args>
class Delegate<R(Args...)> {
    // 함수 포인터를 void* 에 넣는 변환은 conditionally-supported [expr.reinterpret.cast] → union 으로 따로 둔다
    union Ctx { void* obj; void (*fn)(); };
    using Stub = R (*)(Ctx, Args...);

    Ctx  ctx_{.obj = nullptr};
    Stub stub_ = nullptr;

    constexpr Delegate(Ctx c, Stub s) noexcept : ctx_{c}, stub_{s} {}

    // R 이 void 면 대상의 반환값을 버려야 한다 ("return 비void식;" 은 void 함수에서 컴파일 에러)
    template <typename F, typename... A>
    static R call_(F&& f, A&&... a) {
        (void)f; ((void)a, ...);
        return R(); // TODO(Part B): void 이면 반환값을 버리고, 아니면 std::invoke 결과를 반환
    }

    // 스텁: 시그니처는 전부 R(Ctx, Args...) 로 같고, 호출 대상은 템플릿 인자에 박혀 있다
    template <auto F>
    static R fn_stub(Ctx, Args... a) {
        ((void)a, ...);
        return R(); // TODO(Part A): F 를 호출
    }
    template <auto M, typename T>
    static R obj_stub(Ctx c, Args... a) {
        (void)c; ((void)a, ...);
        return R(); // TODO(Part A): c.obj 를 T* 로 되돌려(저장할 때의 타입 그대로!) M 호출
    }
    template <typename L>
    static R lambda_stub(Ctx, Args... a) {
        ((void)a, ...);
        return R(); // TODO(Part A): L 을 그 자리에서 기본 생성해 호출
    }
    static R ptr_stub(Ctx c, Args... a) {
        (void)c; ((void)a, ...);
        return R(); // TODO(Part B): c.fn 을 원래 함수 포인터 타입으로 되돌려 호출
    }

public:
    constexpr Delegate() noexcept = default;

    // Part A: 자유 함수 — bind<&f>()
    template <auto F> requires callable_as_v<R, decltype(F), Args...>
    [[nodiscard]] static constexpr Delegate bind() noexcept {
        return Delegate{}; // TODO(Part A)
    }

    // Part A: 멤버 함수(const 포함) / 첫 인자로 문맥을 받는 자유 함수 — bind<&Foo::on>(foo)
    template <auto M, typename T> requires callable_as_v<R, decltype(M), T&, Args...>
    [[nodiscard]] static constexpr Delegate bind(T& obj) noexcept {
        (void)obj;
        return Delegate{}; // TODO(Part A): const T 도 받도록 void* 로 저장
    }
    // TODO(Part B): 임시 객체(const 임시 포함)를 막는 deleted 오버로드를 여기에 선언

    // Part A: 캡처 없는 람다
    template <typename L> requires (captureless_v<L> && callable_as_v<R, L&, Args...>)
    [[nodiscard]] static constexpr Delegate from_lambda(L) noexcept {
        return Delegate{}; // TODO(Part A)
    }

    // Part B: 런타임에야 정해지는 함수 포인터 (C 드라이버 테이블 등). nullptr → 빈 Delegate
    [[nodiscard]] static Delegate from_ptr(R (*fp)(Args...)) noexcept {
        (void)fp;
        return Delegate{}; // TODO(Part B)
    }

    constexpr explicit operator bool() const noexcept { return stub_ != nullptr; }

    // 전제조건: *this 가 비어 있지 않다 (검사 없음 — 빈 Delegate 호출은 null 점프)
    R operator()(Args... a) const {
        ((void)a, ...);
        return R(); // TODO(Part A): stub_ 에 ctx_ 와 인자를 전달
    }

    // Part B: 같은 대상인가? (타이머 해제·옵저버 제거에 필요). union 은 활성 멤버만 읽어야 한다
    friend constexpr bool operator==(const Delegate& a, const Delegate& b) noexcept {
        (void)a; (void)b;
        return false; // TODO(Part B): stub 비교 → 활성 멤버 비교
    }
};

// ---------------- Part B: FunctionRef (C++26 std::function_ref 의 축소판) ----------------
// 비소유 "함수 인자 전용" 뷰. 캡처 람다도 받는다. 저장하면 안 된다.
template <typename Sig> class FunctionRef;

template <typename R, typename... Args>
class FunctionRef<R(Args...)> {
    union Ctx { void* obj; void (*fn)(); };
    using Thunk = R (*)(Ctx, Args...);
    Ctx   ctx_;
    Thunk thunk_;

    template <typename F, typename... A>
    static R call_(F&& f, A&&... a) {
        (void)f; ((void)a, ...);
        return R(); // TODO(Part B): Delegate::call_ 과 동일
    }

public:
    // 호출 가능한 "객체" (람다, 펑터). 함수 이름은 is_object 에서 빠져 아래 함수 포인터 생성자로 간다
    template <typename F>
        requires (!std::is_same_v<std::remove_cvref_t<F>, FunctionRef> &&
                  std::is_object_v<std::remove_reference_t<F>> &&
                  callable_as_v<R, std::remove_reference_t<F>&, Args...>)
    constexpr FunctionRef(F&& f) noexcept
        : ctx_{.obj = nullptr}, thunk_{nullptr} { (void)f; } // TODO(Part B): 주소 저장 + 되돌려 호출하는 thunk

    FunctionRef(R (*fp)(Args...)) noexcept
        : ctx_{.obj = nullptr}, thunk_{nullptr} { (void)fp; } // TODO(Part B)

    R operator()(Args... a) const {
        (void)ctx_; (void)thunk_; ((void)a, ...);
        return R(); // TODO(Part B)
    }
};

// ---------------- Part C: 인터럽트 흉내 (호스트 단일 스레드) ----------------
// 실제 칩: Guard = PRIMASK 저장 + cpsid i (CPP-B06 참고), preempt_point 는 없음(어디서든 걸릴 수 있으니까)
namespace irq {
inline int  depth = 0;
inline bool pending = false;
inline bool in_isr = false;
inline void (*handler)() = nullptr;
inline unsigned preempted_writes = 0;   // 쓰기 도중 ISR 이 실제로 끼어든 횟수

inline void run_isr() { in_isr = true; handler(); in_isr = false; }
inline void preempt_point() {           // "하필 여기서 타이머 인터럽트가 걸렸다"
    if (handler == nullptr || in_isr) return;
    if (depth > 0) { pending = true; return; }   // 마스크 중 → 해제 시 처리
    ++preempted_writes;
    run_isr();
}
struct Guard {
    Guard() { ++depth; }
    ~Guard() { if (--depth == 0 && pending && !in_isr) { pending = false; run_isr(); } }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};
}  // namespace irq

// ---------------- Part C: 타이머 콜백 테이블 ----------------
using TimerCb = Delegate<void(std::uint32_t)>;   // 인자: 현재 tick

struct TimerId {
    std::uint8_t idx = 0xFF;
    std::uint8_t gen = 0;
    constexpr bool valid() const { return idx != 0xFF; }
};

template <std::size_t N>
class TimerTable {
    static_assert(N > 0 && N < 0xFF, "idx 0xFF 는 무효 핸들 전용");
    struct Slot {
        TimerCb       cb{};
        std::uint32_t deadline = 0;
        std::uint32_t period = 0;   // 0 = one-shot
        std::uint8_t  gen = 0;      // 재사용 시 증가 → 낡은 핸들(stale handle) 검출
        bool          active = false;
    };
    Slot     slots_[N]{};
    unsigned overruns_ = 0;

    static constexpr std::uint32_t MAX_SPAN = 0x7FFF'FFFFu;   // 부호 비교가 유효한 최대 간격

public:
    // 메인 문맥·콜백(ISR) 문맥 양쪽에서 호출 가능. delay: 1..2^31-1, period: 0(one-shot) 또는 1..2^31-1
    TimerId arm(std::uint32_t now, std::uint32_t delay, std::uint32_t period, TimerCb cb) {
        (void)now; (void)delay; (void)period; (void)cb;
        return {}; // TODO(Part C): 입력 검증 → Guard → 빈 슬롯 찾기 → gen 증가 → 필드 기록 (사이에 preempt_point) → active 마지막
    }

    // 낡은 핸들, 이미 끝난 타이머 → false (남의 타이머를 끄면 안 된다)
    bool cancel(TimerId id) {
        (void)id;
        return false; // TODO(Part C): 범위·gen·active 검사 후 해제
    }

    // 타이머 ISR 에서 호출. now 는 단조 증가 카운터(2^32 에서 wrap)
    void tick(std::uint32_t now) {
        (void)now; // TODO(Part C): wrap-safe 만료 판정 → 복사 → one-shot 해제/periodic 재계산 → 호출
    }

    unsigned active_count() const {
        unsigned n = 0;
        for (const Slot& s : slots_) n += s.active ? 1u : 0u;
        return n;
    }
    unsigned overruns() const { return overruns_; }
};

// RAII: 객체가 죽으면 타이머도 해제 (CPP-B06 참고)
template <std::size_t N>
class ScopedTimer {
    TimerTable<N>* table_;
    TimerId        id_;
public:
    ScopedTimer(TimerTable<N>& t, std::uint32_t now, std::uint32_t delay, std::uint32_t period, TimerCb cb)
        : table_{&t}, id_{} { (void)now; (void)delay; (void)period; (void)cb; } // TODO(Part C)
    ~ScopedTimer() {
        (void)table_; // TODO(Part C)
    }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
    bool armed() const { return id_.valid(); }
};

// ---------------- 테스트 하네스 ----------------
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

// 재현 가능한 의사 난수 (고정 시드)
static std::uint32_t xorshift32(std::uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

// ----- 콜백 대상들 -----
static int twice(int x) { return 2 * x; }
static long widen(int x) { return x + 1L; }
static int  g_note = 0;
static void note(int v) { g_note += v; }
static int  with_ctx(int& ctx, int v) { ctx += v; return ctx; }   // 첫 인자가 문맥인 자유 함수

struct Counter {
    int sum = 0;
    int add(int x) { sum += x; return sum; }
    int get(int k) const { return sum * k; }
};
struct Overloaded {
    int f(int) { return 1; }
    int f(int) const { return 2; }
    int f(long) { return 3; }
};
struct BaseA { int a = 1; };
struct BaseB { int b = 7; int get_b(int k) const { return b * k; } };
struct Multi : BaseA, BaseB {};           // BaseB 부분은 Multi 시작 주소에서 떨어져 있다

struct Token {
    int v; int* moves;
    Token(int x, int* m) : v{x}, moves{m} {}
    Token(Token&& o) noexcept : v{o.v}, moves{o.moves} { ++*moves; }
    Token(const Token&) = delete;
};
static int take(Token t) { return t.v; }

// Part B: "이 bind 가 컴파일되는가?" — T 는 값 범주까지 담는다 (Foo& = lvalue, Foo = rvalue)
template <auto M, typename T, typename Sig>
constexpr bool can_bind = requires(T&& t) { Delegate<Sig>::template bind<M>(static_cast<T&&>(t)); };
template <typename L, typename Sig>
constexpr bool can_from_lambda = requires(L l) { Delegate<Sig>::from_lambda(l); };

static void apply3(FunctionRef<void(int)> f) { f(1); f(2); f(3); }
static int  twice_via(FunctionRef<int(int)> f, int x) { return f(x); }
struct ConstDoubler { int operator()(int x) const { return 2 * x; } };

// constinit: 동적 초기화 없음 (CPP-B09 참고)
static Counter g_counter;
static constinit Delegate<int(int)> g_d_free = Delegate<int(int)>::bind<&twice>();
static constinit Delegate<int(int)> g_d_obj  = Delegate<int(int)>::bind<&Counter::add>(g_counter);

// Part C 대상
struct Led { unsigned toggles = 0; void on_tick(std::uint32_t) { ++toggles; } };
struct Rearmer {
    TimerTable<4>* t; int n = 0;
    void on(std::uint32_t now) { if (++n < 3) t->arm(now, 5, 0, TimerCb::bind<&Rearmer::on>(*this)); }
};
struct Canceller {
    TimerTable<4>* t; TimerId victim{}; int n = 0;
    void on(std::uint32_t) { ++n; t->cancel(victim); }
};
struct Spawner {
    TimerTable<4>* t; Led* led; int n = 0;
    void on(std::uint32_t now) { ++n; t->arm(now, 1, 0, TimerCb::bind<&Led::on_tick>(*led)); }
};
struct Blinker {   // 멤버 순서 중요: count 를 timer 보다 먼저 초기화
    unsigned count = 0;
    ScopedTimer<4> timer;
    Blinker(TimerTable<4>& t, std::uint32_t now) : timer{t, now, 10, 10, TimerCb::bind<&Blinker::on>(*this)} {}
    void on(std::uint32_t) { ++count; }
};
static TimerTable<2>* g_isr_table;
static std::uint32_t  g_isr_now;

// 64비트 시간으로 다시 계산하는 기준 모델 (wrap 없음) vs 32비트 구현
static bool random_vs_model() {
    constexpr std::size_t N = 6;
    struct M { bool on; std::uint64_t dl; std::uint32_t pd; TimerId id; };
    TimerTable<N> t;
    Led  hits[N];
    M    m[N] = {};
    unsigned fires[N] = {};
    std::uint32_t seed = 0xC0FF'EE01u;
    std::uint64_t now = 0xFFFF'FF00ull;               // 시작 직후 32비트 wrap 을 지나간다
    for (int step = 0; step < 20000; ++step) {
        const std::uint32_t op = xorshift32(seed) % 4;
        if (op == 0) {
            const std::uint32_t delay = 1 + xorshift32(seed) % 50;
            const std::uint32_t period = (xorshift32(seed) & 1u) ? 0u : 1 + xorshift32(seed) % 30;
            std::size_t f = 0;
            while (f < N && m[f].on) ++f;
            std::size_t want = f;                     // N 이면 "가득 참" 기대
            if (f == N) {
                TimerId id = t.arm(static_cast<std::uint32_t>(now), delay, period, TimerCb::bind<&Led::on_tick>(hits[0]));
                if (id.valid()) return false;
            } else {
                TimerId id = t.arm(static_cast<std::uint32_t>(now), delay, period, TimerCb::bind<&Led::on_tick>(hits[want]));
                if (!id.valid() || id.idx != want) return false;
                m[want] = M{true, now + delay, period, id};
            }
        } else if (op == 1) {
            const std::size_t i = xorshift32(seed) % N;
            const bool expect = m[i].on;
            if (t.cancel(m[i].id) != expect) return false;
            m[i].on = false;
        }
        now += 1 + xorshift32(seed) % 3;
        t.tick(static_cast<std::uint32_t>(now));
        for (std::size_t i = 0; i < N; ++i) {
            if (!m[i].on || now < m[i].dl) continue;
            ++fires[i];
            if (m[i].pd == 0) m[i].on = false;
            else m[i].dl += ((now - m[i].dl) / m[i].pd + 1) * m[i].pd;
        }
        for (std::size_t i = 0; i < N; ++i) if (hits[i].toggles != fires[i]) return false;
    }
    return true;
}

int main() {
    using DI = Delegate<int(int)>;

    // Part A — 기본 바인딩
    DI empty;
    CHECK(!empty);
    auto d_free = DI::bind<&twice>();
    CHECK(static_cast<bool>(d_free) && d_free(21) == 42);
    Counter c;
    auto d_add = DI::bind<&Counter::add>(c);
    CHECK(d_add(5) == 5 && d_add(6) == 11 && c.sum == 11);
    auto d_copy = d_add;                                        // 복사는 "같은 객체를 가리키는 포인터 복사"
    CHECK(d_copy(1) == 12 && c.sum == 12);
    const Counter& cc = c;
    auto d_get = DI::bind<&Counter::get>(cc);
    CHECK(d_get(2) == 24);
    auto d_get2 = DI::bind<&Counter::get>(c);                   // 비 const 객체에 const 멤버 → 허용
    CHECK(d_get2(1) == 12);
    auto plain = [](int x) { return x + 100; };
    auto d_lam = DI::from_lambda(plain);
    CHECK(static_cast<bool>(d_lam) && d_lam(1) == 101);
    auto d_lam2 = DI::bind<+[](int x) { return x * x; }>();     // + 로 함수 포인터 → NTTP
    CHECK(static_cast<bool>(d_lam2) && d_lam2(9) == 81);
    auto d_note = Delegate<void(int)>::bind<&note>();
    g_note = 0; d_note(3); d_note(4);
    CHECK(g_note == 7);
    d_add = {};                                                 // reset
    CHECK(!d_add);
    CHECK(static_cast<bool>(g_d_free) && g_d_free(4) == 8);     // constinit 자유 함수
    CHECK(static_cast<bool>(g_d_obj) && g_d_obj(9) == 9 && g_counter.sum == 9);

    // 레이아웃 (컴파일타임 사실이지만 FAIL 줄을 보려고 CHECK 로 찍는다)
    CHECK(sizeof(DI) == 2 * sizeof(void*));
    CHECK(std::is_trivially_copyable_v<DI> && std::is_trivially_destructible_v<DI>);

    // Part B — 제약: const 정확성, 시그니처, 임시 객체, 캡처 람다
    CHECK((can_bind<&Counter::add, Counter&, int(int)>));
    CHECK((!can_bind<&Counter::add, const Counter&, int(int)>));        // 비const 멤버 + const 객체
    CHECK((can_bind<&Counter::get, const Counter&, int(int)>));
    CHECK((!can_bind<&Counter::add, Counter&, int(const char*)>));      // 인자 안 맞음
    CHECK((!can_bind<&Counter::add, Counter&, void*(int)>));            // 반환 안 맞음
    CHECK((!can_bind<&Counter::add, Counter, int(int)>));               // 임시 객체
    CHECK((!can_bind<&Counter::get, const Counter, int(int)>));         // const 임시 — T& 로는 못 막는다
    int cap_val = 0;
    auto capturing = [&cap_val](int) { return cap_val; };
    CHECK((can_from_lambda<decltype(plain), int(int)>));
    CHECK((!can_from_lambda<decltype(capturing), int(int)>));
    CHECK(capturing(0) == 0);

    // Part B — 까다로운 호출 경로
    Multi mi;
    auto d_mi = DI::bind<&BaseB::get_b>(mi);                            // this 보정이 일어나야 한다
    CHECK(d_mi(3) == 21);
    Counter vc;
    auto d_void = Delegate<void(int)>::bind<&Counter::add>(vc);         // int 반환을 버리는 void 시그니처
    d_void(8);
    CHECK(vc.sum == 8);
    auto d_widen = Delegate<long(int)>::bind<&twice>();                 // int → long 변환 반환
    CHECK(d_widen(10) == 20L);
    auto d_widen2 = Delegate<long(int)>::bind<&widen>();
    CHECK(d_widen2(10) == 11L);
    Overloaded ov;
    const Overloaded& cov = ov;
    CHECK((DI::bind<static_cast<int (Overloaded::*)(int)>(&Overloaded::f)>(ov)(0) == 1));
    CHECK((DI::bind<static_cast<int (Overloaded::*)(int) const>(&Overloaded::f)>(cov)(0) == 2));
    int ctx_val = 10;
    auto d_ctx = DI::bind<&with_ctx>(ctx_val);                          // 멤버 아닌 함수 + 문맥
    CHECK(d_ctx(5) == 15 && ctx_val == 15);
    int moves = 0;
    auto d_tok = Delegate<int(Token)>::bind<&take>();
    CHECK(d_tok(Token{77, &moves}) == 77);
    CHECK(moves == 2);                                                  // operator() → stub → take, 값 인자마다 2회
    int ref_target = 0;
    auto d_ref = Delegate<void(int&)>::bind<+[](int& r) { r = 99; }>();
    d_ref(ref_target);
    CHECK(ref_target == 99);

    // Part B — 런타임 함수 포인터
    int (*fp)(int) = twice;
    auto d_ptr = DI::from_ptr(fp);
    CHECK(static_cast<bool>(d_ptr) && d_ptr(8) == 16);
    fp = nullptr;
    CHECK(!DI::from_ptr(fp));

    // Part B — 동등 비교 표
    Counter c1, c2;
    struct EqCase { DI a; DI b; bool eq; };
    const EqCase eq_cases[] = {
        {DI{}, DI{}, true},
        {DI::bind<&twice>(), DI::bind<&twice>(), true},
        {DI::bind<&Counter::add>(c1), DI::bind<&Counter::add>(c1), true},
        {DI::bind<&Counter::add>(c1), DI::bind<&Counter::add>(c2), false},
        {DI::bind<&Counter::add>(c1), DI::bind<&Counter::get>(c1), false},
        {DI::bind<&Counter::add>(c1), DI{}, false},
        {DI::from_ptr(twice), DI::from_ptr(twice), true},
        {DI::from_ptr(twice), DI::bind<&twice>(), false},               // 대상은 같아도 경로가 다르면 다른 Delegate
    };
    int eq_bad = 0;
    for (const EqCase& e : eq_cases) eq_bad += ((e.a == e.b) != e.eq) ? 1 : 0;
    CHECK(eq_bad == 0);

    // Part B — FunctionRef
    int total = 0;
    auto visit = [&total](int x) { total += x; };
    apply3(visit);
    CHECK(total == 6);
    apply3([&total](int x) { total += 10 * x; });                       // 임시 람다: 호출이 끝날 때까지 산다
    CHECK(total == 66);
    auto counting = [n = 0](int) mutable { return ++n; };
    FunctionRef<int(int)> r_counting = counting;
    (void)r_counting(0); (void)r_counting(0);
    CHECK(counting(0) == 3);                                            // 복사본이 아니라 원본을 호출했다
    const ConstDoubler cd;
    CHECK(twice_via(cd, 4) == 8);
    CHECK(twice_via(twice, 5) == 10);                                   // 함수 이름 → 함수 포인터 생성자
    int (*fp2)(int) = twice;
    CHECK(twice_via(fp2, 6) == 12);
    CHECK(sizeof(FunctionRef<void()>) == 2 * sizeof(void*));
    CHECK((!std::is_constructible_v<FunctionRef<void()>, int>));
    CHECK((!std::is_constructible_v<FunctionRef<int(int)>, decltype(+[](const char*) { return 0; })>));

    // Part C — 입력 경계값
    {
        TimerTable<4> t; Led led;
        auto cb = TimerCb::bind<&Led::on_tick>(led);
        struct ArmCase { std::uint32_t delay, period; bool cb_ok; bool expect; };
        const ArmCase cases[] = {
            {0, 0, true, false},                       // delay 0: "이번 tick 에 실행"은 허용 안 함
            {0x8000'0000u, 0, true, false},            // 2^31: 부호 비교 불가
            {1, 0x8000'0000u, true, false},
            {1, 0, false, false},                      // 빈 콜백
            {1, 0, true, true},
            {0x7FFF'FFFFu, 0x7FFF'FFFFu, true, true},
        };
        int bad = 0;
        for (const ArmCase& k : cases) {
            TimerId id = t.arm(100, k.delay, k.period, k.cb_ok ? cb : TimerCb{});
            if (id.valid() != k.expect) ++bad;
            if (id.valid()) t.cancel(id);
        }
        CHECK(bad == 0);
        CHECK(t.active_count() == 0);
    }

    // Part C — one-shot, wrap-around
    {
        TimerTable<4> t; Led led;
        auto cb = TimerCb::bind<&Led::on_tick>(led);
        CHECK(t.arm(100, 10, 0, cb).valid());
        t.tick(109); CHECK(led.toggles == 0);
        t.tick(110); CHECK(led.toggles == 1);
        t.tick(111); CHECK(led.toggles == 1 && t.active_count() == 0);

        Led w;
        CHECK(t.arm(0xFFFF'FFF0u, 0x20, 0, TimerCb::bind<&Led::on_tick>(w)).valid());   // deadline = 0x10
        t.tick(0xFFFF'FFFFu); CHECK(w.toggles == 0);                                    // now >= deadline 이면 여기서 터진다
        t.tick(0x0000'000Fu); CHECK(w.toggles == 0);
        t.tick(0x0000'0010u); CHECK(w.toggles == 1);
    }

    // Part C — periodic: 드리프트 없음, 놓친 주기는 overrun 으로 집계
    {
        TimerTable<4> t; Led led;
        CHECK(t.arm(0, 10, 10, TimerCb::bind<&Led::on_tick>(led)).valid());
        t.tick(10); CHECK(led.toggles == 1);
        t.tick(25); CHECK(led.toggles == 2);          // 5 늦게 처리해도 다음 만료는 30 (35 아님)
        t.tick(29); CHECK(led.toggles == 2);
        t.tick(30); CHECK(led.toggles == 3);
        t.tick(75);                                   // 40·50·60·70 만료 → 1회 호출 + 3회 overrun
        CHECK(led.toggles == 4 && t.overruns() == 3);
        t.tick(79); CHECK(led.toggles == 4);
        t.tick(80); CHECK(led.toggles == 5);
    }

    // Part C — 콜백 안에서 테이블 조작
    {
        TimerTable<4> t;
        Rearmer re{&t};
        CHECK(t.arm(0, 5, 0, TimerCb::bind<&Rearmer::on>(re)).valid());
        t.tick(5);  CHECK(re.n == 1 && t.active_count() == 1);   // 같은 슬롯에 자기 재등록
        t.tick(10); CHECK(re.n == 2);
        t.tick(15); CHECK(re.n == 3 && t.active_count() == 0);
    }
    {
        TimerTable<4> t; Led victim; Canceller k{&t};
        TimerId a = t.arm(0, 5, 0, TimerCb::bind<&Canceller::on>(k));      // slot 0
        k.victim  = t.arm(0, 5, 0, TimerCb::bind<&Led::on_tick>(victim));  // slot 1, 같은 tick 만료
        CHECK(a.valid() && k.victim.valid());
        t.tick(5);
        CHECK(k.n == 1 && victim.toggles == 0 && t.active_count() == 0);   // 앞 슬롯이 뒤 슬롯을 취소
    }
    {
        TimerTable<4> t; Led fresh; Spawner sp{&t, &fresh};
        CHECK(t.arm(0, 5, 0, TimerCb::bind<&Spawner::on>(sp)).valid());
        t.tick(5);  CHECK(sp.n == 1 && fresh.toggles == 0);                // 이번 tick 에 새로 등록된 건 이번에 안 돈다
        t.tick(6);  CHECK(fresh.toggles == 1);
    }

    // Part C — 낡은 핸들, 용량
    {
        TimerTable<2> t; Led a, b;
        TimerId old_id = t.arm(0, 1, 0, TimerCb::bind<&Led::on_tick>(a));
        t.tick(1);                                                         // 끝남 → slot 0 재사용 가능
        TimerId new_id = t.arm(1, 5, 0, TimerCb::bind<&Led::on_tick>(b));
        CHECK(old_id.valid() && new_id.valid() && old_id.idx == new_id.idx && old_id.gen != new_id.gen);
        CHECK(!t.cancel(old_id) && t.active_count() == 1);                 // 남의 타이머를 끄면 안 된다
        CHECK(t.cancel(new_id) && !t.cancel(new_id));                      // 이중 취소
        CHECK(!t.cancel(TimerId{}));
        TimerId x = t.arm(0, 9, 0, TimerCb::bind<&Led::on_tick>(a));
        TimerId y = t.arm(0, 9, 0, TimerCb::bind<&Led::on_tick>(a));
        TimerId z = t.arm(0, 9, 0, TimerCb::bind<&Led::on_tick>(a));
        CHECK(x.valid() && y.valid() && !z.valid());                       // 가득 차면 무효 핸들
        t.cancel(y);
        CHECK(t.arm(0, 9, 0, TimerCb::bind<&Led::on_tick>(a)).valid());
    }

    // Part C — 쓰기 도중 인터럽트: 마스크 구간이면 ISR 은 해제 시점까지 밀린다
    {
        TimerTable<2> t; Led led;
        g_isr_table = &t;
        g_isr_now = 101;
        irq::handler = [] { g_isr_table->tick(g_isr_now); };
        irq::preempted_writes = 0;
        TimerId id = t.arm(100, 1, 0, TimerCb::bind<&Led::on_tick>(led));
        CHECK(id.valid() && irq::preempted_writes == 0);                   // 쓰기 도중 끼어들지 않았다
        CHECK(led.toggles == 1 && !irq::pending);                          // 해제 직후 밀린 tick 이 돌아 만료 처리
        irq::handler = nullptr;
    }

    // Part C — RAII 해제: 객체가 죽은 뒤에는 콜백이 오지 않는다
    {
        TimerTable<4> t;
        {
            Blinker b{t, 0};
            CHECK(b.timer.armed() && t.active_count() == 1);
            t.tick(10); t.tick(20);
            CHECK(b.count == 2);
        }
        CHECK(t.active_count() == 0);
        t.tick(30);                                                        // 죽은 Blinker 를 부르면 ASan 이 잡는다
        CHECK(t.active_count() == 0);
    }

    // Part C — 고정 시드 무작위 시퀀스 vs 64비트 기준 모델
    CHECK(random_vs_model());

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
