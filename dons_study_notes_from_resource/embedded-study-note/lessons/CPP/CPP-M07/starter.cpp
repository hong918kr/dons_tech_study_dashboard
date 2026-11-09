// CPP-M07 placement new와 정적 저장소 수명 관리
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
//
// 힙 없는 펌웨어의 "나중에 만드는 전역 드라이버":
//   alignas(T) std::byte buf[sizeof(T)] (.bss) + ::new (buf) T(...)  (시점은 board_init 이 고른다)
// 이 레슨은 그 저장소의 수명 규칙을 코드로 만들고, 생성/소멸 장부(LifeLog)로 이중 생성·누락 파괴를 잡는다.
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

// ---------------- Part B: 생성/소멸 장부 ----------------
// 살아 있는 객체의 주소를 표로 관리한다. 호스트 테스트 전용 도구.
struct LifeLog {
    static constexpr int CAP = 16;
    const void* live[CAP];
    int live_n, ctors, dtors, double_ctor, bad_dtor;

    int find(const void* p) const {
        for (int i = 0; i < live_n; ++i) if (live[i] == p) return i;
        return -1;
    }
    // 생성: ctors++. 이미 살아 있는 주소에 또 생성되면 double_ctor++ (표에는 다시 넣지 않는다)
    void on_ctor(const void* p) {
        (void)p; // TODO(Part B)
    }
    // 소멸: dtors++. 표에 없는 주소를 소멸하면 bad_dtor++ (이중 파괴, 생성한 적 없는 객체)
    void on_dtor(const void* p) {
        (void)p; // TODO(Part B)
    }
    void reset() { *this = LifeLog{}; }
};
inline constinit LifeLog g_life{};

struct Tracked {
    int value;
    explicit Tracked(int v = 0) : value(v) { g_life.on_ctor(this); }
    Tracked(const Tracked& o) : value(o.value) { g_life.on_ctor(this); }
    Tracked(Tracked&& o) noexcept : value(o.value) { g_life.on_ctor(this); }
    Tracked& operator=(const Tracked&) = delete;
    ~Tracked() { g_life.on_dtor(this); }
};

// ---------------- Part A: StaticInstance<T> ----------------
// 포인터를 저장하는 판: placement new 가 돌려준 포인터를 그대로 쓰므로 std::launder 가 필요 없다.
enum class State : std::uint8_t { Empty, Constructing, Alive, Destroying };

template <typename T>
class StaticInstance {
    alignas(T) std::byte storage_[sizeof(T)]{};   // {} 가 있어야 constinit 가 된다 (demo_errors.cpp SHOW_ERROR=4)
    T* ptr_ = nullptr;
    State state_ = State::Empty;
    std::uint16_t misuse_ = 0;

public:
    constexpr StaticInstance() noexcept = default;
    StaticInstance(const StaticInstance&) = delete;
    StaticInstance& operator=(const StaticInstance&) = delete;
    // 소멸자를 선언하지 않는다: trivially destructible → 전역이어도 atexit 등록이 없다.

    // Empty 일 때만 생성. 아니면 misuse_++ 하고 nullptr (이미 있는 객체는 건드리지 않는다).
    // 생성이 끝난 뒤에야 ptr_ 를 채운다.
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    [[nodiscard]] T* emplace(Args&&... args) {
        ((void)args, ...);
        return nullptr; // TODO(Part A)
    }

    // Alive 일 때만 소멸. 아니면 misuse_++ 하고 false.
    // 소멸자를 부르기 전에 ptr_ 를 먼저 비운다.
    bool destroy() {
        return false; // TODO(Part A)
    }

    T*       get() noexcept       { return ptr_; }
    const T* get() const noexcept { return ptr_; }
    bool has_value() const noexcept { return state_ == State::Alive; }
    unsigned misuse_count() const noexcept { return misuse_; }
};

// ---------------- Part B: LaunderInstance<T> ----------------
// 포인터를 저장하지 않는 판: 저장소 주소에서 T* 를 다시 만들어야 하므로 std::launder 가 필요하다.
template <typename T>
class LaunderInstance {
    alignas(T) std::byte storage_[sizeof(T)]{};
    bool alive_ = false;
    std::uint16_t misuse_ = 0;

public:
    constexpr LaunderInstance() noexcept = default;
    LaunderInstance(const LaunderInstance&) = delete;
    LaunderInstance& operator=(const LaunderInstance&) = delete;

    template <typename... Args>
        requires std::constructible_from<T, Args...>
    [[nodiscard]] T* emplace(Args&&... args) {
        ((void)args, ...);
        return nullptr; // TODO(Part B)
    }
    bool destroy() {
        return false; // TODO(Part B)
    }
    T* get() noexcept {
        return nullptr; // TODO(Part B): 저장소에서 T* 를 다시 만든다
    }
    const T* get() const noexcept {
        return nullptr; // TODO(Part B)
    }
    bool has_value() const noexcept { return alive_; }
    unsigned misuse_count() const noexcept { return misuse_; }
};

// ---------------- Part C: ISR 가 보는 드라이버 ----------------
// 단일 코어 전제: start/stop 은 thread mode 에서만, for_isr 는 ISR 에서.
//   start : 완전히 생성한 뒤에 공개한다
//   stop  : 먼저 공개를 거둔 뒤에 소멸한다
template <typename T>
class IsrVisible {
    StaticInstance<T> inst_;
    std::atomic<T*> published_{nullptr};

public:
    constexpr IsrVisible() noexcept = default;

    template <typename... Args>
    T* start(Args&&... args) {
        ((void)args, ...);
        return nullptr; // TODO(Part C)
    }
    bool stop() {
        return false; // TODO(Part C)
    }
    T* for_isr() noexcept {
        return nullptr; // TODO(Part C)
    }
};

// ---------------- 가짜 UART ----------------
namespace mock {
struct UartRegs { std::uint32_t CR, BRR; };
inline constinit UartRegs uart{};
inline bool clock_on = false;
inline int  dma_claims = 0;
inline int  isr_saw_null = 0, isr_handled = 0;
inline void reset() { uart = UartRegs{}; clock_on = false; dma_claims = 0; isr_saw_null = 0; isr_handled = 0; }
}  // namespace mock

void uart_isr();   // 생성·소멸 도중에 IRQ 가 끼어드는 상황을 흉내 내려고 드라이버가 부른다

class UartDriver {
    Tracked probe_;           // 장부에 생성/소멸이 찍히게 하는 멤버
    std::uint32_t baud_;
    int rx_ = 0;

public:
    explicit UartDriver(std::uint32_t baud) : probe_(1), baud_(baud) {
        mock::clock_on = true;
        mock::uart.BRR = baud;
        ++mock::dma_claims;
        uart_isr();           // 절반만 만들어진 시점의 IRQ
        mock::uart.CR = 1;
    }
    ~UartDriver() {
        mock::uart.CR = 0;
        uart_isr();           // 절반쯤 해체된 시점의 IRQ
        --mock::dma_claims;
        mock::clock_on = false;
    }
    void on_irq() { ++rx_; ++mock::isr_handled; }
    std::uint32_t baud() const { return baud_; }
    int rx_count() const { return rx_; }
};

inline constinit IsrVisible<UartDriver> g_uart;

void uart_isr() {
    if (UartDriver* d = g_uart.for_isr()) d->on_irq();
    else ++mock::isr_saw_null;
}

// 저전력 복귀 등에서 보드레이트를 바꿔 다시 시작한다. 새 드라이버를 돌려준다.
UartDriver* uart_restart(std::uint32_t baud) {
    (void)baud;
    return nullptr; // TODO(Part C)
}

// ---------------- 테스트 하네스 ----------------
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

template <typename T, typename... A>
constexpr bool can_emplace = requires(StaticInstance<T>& s, A&&... a) { s.emplace(std::forward<A>(a)...); };
template <typename T> constexpr bool plain_new_ok  = requires(void* p) { new (p) T(1); };
template <typename T> constexpr bool global_new_ok = requires(void* p) { ::new (p) T(1); };

template <typename T> bool is_aligned(const T* p) {
    return reinterpret_cast<std::uintptr_t>(p) % alignof(T) == 0;
}

struct alignas(64) Over64 {
    std::uint32_t w[4];
    explicit Over64(std::uint32_t x) : w{x, 0, 0, 0} {}
};
struct Holder { char tag; StaticInstance<Over64> d; };

struct ClassNew {                                 // 클래스 전용 operator new 가 전역 placement 를 가린다
    int v;
    explicit ClassNew(int x) : v(x) {}
    static void* operator new(std::size_t) = delete;
};
struct MoveOnly {
    int v;
    explicit MoveOnly(int x) : v(x) {}
    MoveOnly(MoveOnly&&) = default;
    MoveOnly(const MoveOnly&) = delete;
};
struct TakesMoveOnly { int v; explicit TakesMoveOnly(MoveOnly m) : v(m.v) {} };
struct BindsRef { int& r; explicit BindsRef(int& x) : r(x) {} };
struct Agg { int a; int b; };                     // 집합체: C++20 괄호 집합체 초기화 (P0960)
struct OnlyInt { explicit OnlyInt(int) {} };
struct Cfg { int v; explicit Cfg(int x) : v(x) {} };

// 컴파일타임 증명 (starter 에서도 이미 성립해야 한다)
static_assert(!std::is_copy_constructible_v<StaticInstance<Tracked>>);
static_assert(!std::is_copy_assignable_v<StaticInstance<Tracked>>);
static_assert(!std::is_move_constructible_v<StaticInstance<Tracked>>);
static_assert(std::is_trivially_destructible_v<StaticInstance<Tracked>>);    // Tracked 는 non-trivial 인데도
static_assert(alignof(StaticInstance<Over64>) == 64);
static_assert(sizeof(StaticInstance<Over64>) == 128);                        // 상태 몇 바이트에 패딩이 붙는다
static_assert(alignof(Holder) == 64 && sizeof(Holder) == 192);
static_assert(sizeof(LaunderInstance<Tracked>) < sizeof(StaticInstance<Tracked>));
static_assert(can_emplace<Tracked, int> && can_emplace<Tracked>);
static_assert(!can_emplace<Tracked, const char*>);
static_assert(!can_emplace<OnlyInt> && can_emplace<OnlyInt, int>);
static_assert(!plain_new_ok<ClassNew> && global_new_ok<ClassNew>);

constinit static StaticInstance<Tracked> g_ci;   // 동적 초기화 없음 (컴파일이 되면 증명 완료)
static Holder g_holders[3];

// Part A/B 공통: 같은 시나리오를 두 구현에 돌린다
enum class Op { Emplace, Destroy };
struct Step { Op op; int arg; bool ok; bool alive; int value; int ctors, dtors, live_n; unsigned misuse; };
static constexpr Step SEQ[] = {
    // op           arg  ok     alive  value ctors dtors live misuse
    {Op::Destroy,   0,  false, false,  0,    0,    0,    0,   1},   // 빈 것을 소멸
    {Op::Emplace,  11,  true,  true,  11,    1,    0,    1,   1},
    {Op::Emplace,  22,  false, true,  11,    1,    0,    1,   2},   // 이중 생성 거부: 11 유지
    {Op::Destroy,   0,  true,  false,  0,    1,    1,    0,   2},
    {Op::Destroy,   0,  false, false,  0,    1,    1,    0,   3},   // 이중 파괴 거부
    {Op::Emplace,  33,  true,  true,  33,    2,    1,    1,   3},   // 재사용
    {Op::Destroy,   0,  true,  false,  0,    2,    2,    0,   3},
};

template <template <typename> class Inst>
void run_seq() {
    static Inst<Tracked> s;
    g_life.reset();
    CHECK(!s.has_value() && s.get() == nullptr);
    for (const Step& st : SEQ) {
        bool ok;
        if (st.op == Op::Emplace) { Tracked* p = s.emplace(st.arg); ok = p != nullptr; if (p) CHECK(is_aligned(p) && p == s.get()); }
        else ok = s.destroy();
        CHECK(ok == st.ok);
        CHECK(s.has_value() == st.alive && (s.get() != nullptr) == st.alive);
        if (st.alive) CHECK(s.get() && s.get()->value == st.value);
        CHECK(g_life.ctors == st.ctors && g_life.dtors == st.dtors && g_life.live_n == st.live_n);
        CHECK(s.misuse_count() == st.misuse);
    }
    CHECK(g_life.double_ctor == 0 && g_life.bad_dtor == 0);
}

// 재진입: 생성자 안에서 자기 자신을 다시 emplace 하고, 소멸자 안에서 다시 destroy 한다
struct SelfInit { SelfInit(); ~SelfInit(); Tracked t{7}; };
constinit static StaticInstance<SelfInit> g_self;
static int g_self_inner_rejected, g_self_saw_empty, g_self_dtor_rejected;
SelfInit::SelfInit() {
    if (!g_self.emplace()) ++g_self_inner_rejected;
    if (g_self.get() == nullptr && !g_self.has_value()) ++g_self_saw_empty;
}
SelfInit::~SelfInit() {
    if (!g_self.destroy()) ++g_self_dtor_rejected;
    if (g_self.get() == nullptr) ++g_self_saw_empty;
}

int main() {
    // Part A — 수명 상태기계
    run_seq<StaticInstance>();

    {   // 인자 전달
        static StaticInstance<TakesMoveOnly> a;
        TakesMoveOnly* p = a.emplace(MoveOnly{5});
        CHECK(p && p->v == 5);
        CHECK(a.destroy());

        static StaticInstance<BindsRef> b;
        int x = 1;
        BindsRef* q = b.emplace(x);
        CHECK(q && &q->r == &x);
        CHECK(b.destroy());

        static StaticInstance<Agg> c;
        Agg* g = c.emplace(3, 4);
        CHECK(g && g->a == 3 && g->b == 4);
        CHECK(c.destroy());

        static StaticInstance<int> i;
        int* n = i.emplace(7);
        CHECK(n && *n == 7 && is_aligned(n));
        CHECK(i.destroy() && i.get() == nullptr);
    }

    // Part B — 장부가 실제로 버그를 잡는가 (StaticInstance 를 거치지 않은 맨 placement new)
    {
        g_life.reset();
        alignas(Tracked) static std::byte raw[sizeof(Tracked)];
        Tracked* a = ::new (static_cast<void*>(raw)) Tracked(1);
        (void)a;
        Tracked* b = ::new (static_cast<void*>(raw)) Tracked(2);   // 소멸 없이 재생성
        CHECK(g_life.double_ctor == 1 && g_life.ctors == 2 && g_life.dtors == 0);
        std::destroy_at(b);
        CHECK(g_life.live_n == 0);                                 // 주소 표는 비었지만
        CHECK(g_life.ctors != g_life.dtors);                       // 개수는 어긋난다 → 누락 파괴
        std::byte elsewhere[sizeof(Tracked)];
        g_life.on_dtor(elsewhere);
        CHECK(g_life.bad_dtor == 1);                               // 표에 없는 주소의 소멸
    }

    // Part B — launder 판도 같은 시나리오를 통과한다
    run_seq<LaunderInstance>();

    {   // complete const 객체: 재생성 후에는 돌려받은 포인터 또는 launder 로만 접근
        static StaticInstance<const Cfg> c;
        const Cfg* p1 = c.emplace(1);
        CHECK(p1 && p1->v == 1 && c.destroy());
        const Cfg* p2 = c.emplace(2);
        CHECK(p2 && p2->v == 2 && c.get() == p2);
        CHECK(c.destroy());

        static LaunderInstance<const Cfg> d;
        const Cfg* p3 = d.emplace(3);
        CHECK(p3 && p3->v == 3 && d.destroy());
        const Cfg* p4 = d.emplace(4);
        CHECK(p4 && p4->v == 4 && d.get()->v == 4);
        CHECK(d.destroy());
    }

    {   // 정렬: 앞에 char 를 둔 구조체 배열이어도 64바이트 정렬
        for (Holder& h : g_holders) {
            Over64* p = h.d.emplace(0xABCDu);
            CHECK(p && p->w[0] == 0xABCDu);
            CHECK(reinterpret_cast<std::uintptr_t>(p) % 64 == 0);
            CHECK(h.d.destroy());
        }
    }

    {   // 클래스 전용 operator new 가 있어도 ::new 는 된다
        static StaticInstance<ClassNew> s;
        ClassNew* p = s.emplace(9);
        CHECK(p && p->v == 9 && s.destroy());
    }

    {   // constinit 전역도 정상 동작
        g_life.reset();
        CHECK(g_ci.emplace(5) != nullptr && g_ci.get()->value == 5);
        CHECK(g_ci.destroy() && g_life.dtors == 1);
    }

    {   // 재진입: 생성 중·소멸 중의 자기 호출은 거부되고, 그동안 get() 은 nullptr
        g_life.reset();
        SelfInit* p = g_self.emplace();
        CHECK(p != nullptr && g_life.ctors == 1 && g_life.double_ctor == 0);
        CHECK(g_self_inner_rejected == 1 && g_self_saw_empty == 1);
        CHECK(g_self.destroy() && g_self_dtor_rejected == 1 && g_self_saw_empty == 2);
        CHECK(g_life.dtors == 1 && g_life.bad_dtor == 0);
        CHECK(g_self.misuse_count() == 2);
    }

    // Part C — ISR 가 보는 UART 드라이버
    {
        mock::reset();
        g_life.reset();
        UartDriver* d = g_uart.start(115200u);
        CHECK(d && d->baud() == 115200u);
        CHECK(mock::uart.BRR == 115200u && mock::uart.CR == 1u && mock::clock_on && mock::dma_claims == 1);
        CHECK(mock::isr_saw_null == 1 && mock::isr_handled == 0);    // 생성 도중 IRQ 는 드라이버를 못 본다
        CHECK(g_uart.for_isr() == d);
        uart_isr();
        CHECK(d && mock::isr_handled == 1 && d->rx_count() == 1);

        CHECK(g_uart.start(9600u) == nullptr);                       // 이중 시작 거부
        CHECK(mock::uart.BRR == 115200u && mock::dma_claims == 1 && g_life.ctors == 1);

        d = uart_restart(9600u);
        CHECK(d && d->baud() == 9600u && mock::uart.BRR == 9600u && mock::uart.CR == 1u);
        CHECK(mock::dma_claims == 1 && mock::clock_on);
        CHECK(d && d->rx_count() == 0);                              // 새 객체
        CHECK(mock::isr_saw_null == 3);                              // 첫 생성 1 + 이전 소멸 1 + 새 생성 1
        CHECK(g_life.ctors == 2 && g_life.dtors == 1 && g_life.live_n == 1);
        CHECK(g_life.double_ctor == 0 && g_life.bad_dtor == 0);

        CHECK(g_uart.stop());
        CHECK(g_uart.for_isr() == nullptr);
        CHECK(mock::uart.CR == 0u && !mock::clock_on && mock::dma_claims == 0);
        CHECK(g_life.ctors == 2 && g_life.dtors == 2 && g_life.live_n == 0);
        CHECK(!g_uart.stop());                                       // 이미 멈춤
    }
    {   // 재시작 버그 재현: stop 없이 생성하면 DMA 채널이 샌다
        mock::reset();
        g_life.reset();
        alignas(UartDriver) static std::byte raw[sizeof(UartDriver)];
        UartDriver* a = ::new (static_cast<void*>(raw)) UartDriver(1u);
        (void)a;
        UartDriver* b = ::new (static_cast<void*>(raw)) UartDriver(2u);
        CHECK(mock::dma_claims == 2 && g_life.double_ctor == 1);
        std::destroy_at(b);
        CHECK(mock::dma_claims == 1 && g_life.ctors == 2 && g_life.dtors == 1);
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
