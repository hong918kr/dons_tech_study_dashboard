// CPP-M09 move semantics의 비용: 고정 용량 버퍼에선 공짜가 아니다
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti <이 파일>.cpp -o m09 && ./m09
//
// Part A  계측 타입 Probe 로 "vector(포인터 훔치기) vs array 멤버(요소별 이동)" 비용을 센다
// Part B  noexcept 유무가 재배치(relocation)에 주는 영향: move_if_noexcept, -fno-exceptions 의 함정
// Part C  펌웨어 시나리오: 256바이트 프레임을 큐로 넘길 때 값 전달 vs 1바이트 핸들
//
// std::vector 는 호스트 전용 실험이다 (펌웨어는 힙을 안 쓴다, CPP-B14 참고).
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

static int g_pass = 0, g_total = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        ++g_total;                                                      \
        if (cond) { ++g_pass; std::printf("PASS  %s\n", #cond); }       \
        else      { std::printf("FAIL  %s  (line %d)\n", #cond, __LINE__); } \
    } while (0)

// ---------------- 계측 카운터 ----------------
struct Ops {
    unsigned def = 0, copy_ctor = 0, move_ctor = 0, copy_assign = 0, move_assign = 0, dtor = 0;
    unsigned copies() const { return copy_ctor + copy_assign; }
    unsigned moves()  const { return move_ctor + move_assign; }
};
inline Ops g_ops;
inline void ops_reset() { g_ops = Ops{}; }
inline constexpr int MOVED_FROM = -1;        // 이동당한 Probe 의 value

struct Cost { unsigned copies; unsigned moves; };
inline bool operator==(Cost a, Cost b) { return a.copies == b.copies && a.moves == b.moves; }

// ---------------- Part A: 계측 타입 ----------------
// NX = true  → noexcept move (Probe)
// NX = false → noexcept 없는 move (ThrowingProbe, 실제로 던지지는 않는다. "던질 수 있다고 선언"만 한다)
template <bool NX>
struct ProbeT {
    int value = 0;
    ProbeT() { ++g_ops.def; }
    explicit ProbeT(int v) : value(v) {}     // 값 생성은 세지 않는다 (세고 싶은 건 특수 멤버 함수)

    ProbeT(const ProbeT& o) : value(o.value) { ++g_ops.copy_ctor; }
    ProbeT(ProbeT&& o) noexcept(NX) : value(o.value) { ++g_ops.move_ctor; o.value = MOVED_FROM; }
    ProbeT& operator=(const ProbeT& o) { value = o.value; ++g_ops.copy_assign; return *this; }
    ProbeT& operator=(ProbeT&& o) noexcept(NX) {
        ++g_ops.move_assign;
        if (this != &o) { value = o.value; o.value = MOVED_FROM; }
        return *this;
    }
    ~ProbeT() { ++g_ops.dtor; }
};
using Probe         = ProbeT<true>;
using ThrowingProbe = ProbeT<false>;

// 소유 방식만 다른 두 클래스. 둘 다 Rule of Zero: 이동 연산은 컴파일러가 만든다.
struct VecOwner {
    std::vector<Probe> items;
    explicit VecOwner(std::size_t n) {
        items.reserve(n);
        for (std::size_t i = 0; i < n; ++i) items.emplace_back(static_cast<int>(i));
    }
    std::size_t  size() const { return items.size(); }
    const Probe* data() const { return items.data(); }
};

template <std::size_t N>
struct InlineOwner {
    std::array<Probe, N> items{};
    std::size_t count = 0;
    explicit InlineOwner(std::size_t n) : count(n) {
        for (std::size_t i = 0; i < n; ++i) items[i].value = static_cast<int>(i);
    }
    std::size_t  size() const { return count; }
    const Probe* data() const { return items.data(); }
};

struct MoveReport {
    Cost        cost;           // Owner dst(std::move(src)) 동안의 요소 복사/이동 횟수
    bool        buffer_stolen;  // dst.data() 가 이동 전 src.data() 와 같은 주소인가
    std::size_t src_size_after; // 이동 후 src.size()
};

template <class Owner>
MoveReport measure_move(Owner& src) {
    const Probe* before = src.data();
    ops_reset();
    Owner dst(std::move(src));
    const Cost c{g_ops.copies(), g_ops.moves()};
    return MoveReport{c, dst.data() == before, src.size()};
}

// ---------------- Part B: 재배치 (relocation) ----------------
// noexcept(false) + 복사 불가 → move_if_noexcept 가 그래도 이동을 고르는 경우 (약한 보장)
struct MoveOnlyThrowing {
    int value = 0;
    MoveOnlyThrowing() = default;
    explicit MoveOnlyThrowing(int v) : value(v) {}
    MoveOnlyThrowing(const MoveOnlyThrowing&) = delete;
    MoveOnlyThrowing& operator=(const MoveOnlyThrowing&) = delete;
    MoveOnlyThrowing(MoveOnlyThrowing&& o) noexcept(false) : value(o.value) { ++g_ops.move_ctor; o.value = MOVED_FROM; }
    MoveOnlyThrowing& operator=(MoveOnlyThrowing&& o) noexcept(false) {
        ++g_ops.move_assign;
        value = o.value;
        o.value = MOVED_FROM;
        return *this;
    }
};

enum class Reloc { Move, Copy };

// T 를 재배치할 때 std::move_if_noexcept 가 무엇을 고르는지 컴파일 타임에 답한다
template <class T>
constexpr Reloc reloc_choice() {
    using R = decltype(std::move_if_noexcept(std::declval<T&>()));
    return std::is_rvalue_reference_v<R> ? Reloc::Move : Reloc::Copy;
}

// vector 재배치의 미니 모델: from[0..n) 을 to[0..n) (raw storage) 로 옮기고 from 쪽은 파괴한다.
// 반환값: to 쪽에 생성한 요소 수.
template <class T>
std::size_t relocate(T* from, std::size_t n, T* to) {
    for (std::size_t i = 0; i < n; ++i) std::construct_at(to + i, std::move_if_noexcept(from[i]));
    std::destroy_n(from, n);
    return n;
}

// n 개를 담은 vector 를 reserve(2n) 으로 재배치시킬 때의 비용 (요소 수가 정확히 n 이 되게 만든다)
template <class T>
Cost vector_growth_cost(std::size_t n) {
    std::vector<T> v;
    v.reserve(n);
    for (std::size_t i = 0; i < n; ++i) v.emplace_back(static_cast<int>(i));
    ops_reset();
    v.reserve(2 * n);
    return Cost{g_ops.copies(), g_ops.moves()};
}

// 실측 결과 (demo_relocate.cpp 참고): libc++ 는 -fno-exceptions 빌드에서 move_if_noexcept 를 쓰지 않고 std::move 를 쓴다.
// 라이브러리 구현 세부이므로 표준이 보장하지 않는다. libstdc++ 는 플래그와 무관하게 복사한다.
#if defined(_LIBCPP_VERSION) && !defined(__cpp_exceptions)
inline constexpr bool kVectorIgnoresThrowingMove = true;
#else
inline constexpr bool kVectorIgnoresThrowingMove = false;
#endif

// 소멸자를 선언하는 것만으로 implicit move 가 사라진다
struct DtorBlocksMove {
    Probe p;
    explicit DtorBlocksMove(int v) : p(v) {}
    ~DtorBlocksMove() {}
};

// 소멸자는 그대로 두고 move 를 되살린다
struct DtorKeepsMove {
    Probe p;
    explicit DtorKeepsMove(int v) : p(v) {}
    ~DtorKeepsMove() {}
    DtorKeepsMove(const DtorKeepsMove&) = default;
    DtorKeepsMove(DtorKeepsMove&&) noexcept = default;
    DtorKeepsMove& operator=(const DtorKeepsMove&) = default;
    DtorKeepsMove& operator=(DtorKeepsMove&&) noexcept = default;
};

// Rule of Zero 라도 멤버의 이동이 noexcept 가 아니면 implicit move 도 noexcept 가 아니다
struct WrapsThrowing { ThrowingProbe p; };

// ---------------- Part C: 프레임 전달 ----------------
inline constexpr std::size_t kPayload = 256;

struct Traffic { unsigned frame_copies = 0, frame_moves = 0, handle_moves = 0; };
inline Traffic g_traffic;

// 256바이트를 멤버로 "품은" 프레임. move 해도 256바이트를 옮겨야 한다 (카운트만 추가, 동작은 implicit move 와 같다)
struct Frame {
    std::array<std::uint8_t, kPayload> bytes{};
    std::uint16_t len = 0;
    Frame() = default;
    Frame(const Frame& o) : bytes(o.bytes), len(o.len) { ++g_traffic.frame_copies; }
    Frame(Frame&& o) noexcept : bytes(o.bytes), len(o.len) { ++g_traffic.frame_moves; }
    Frame& operator=(const Frame& o) { bytes = o.bytes; len = o.len; ++g_traffic.frame_copies; return *this; }
    Frame& operator=(Frame&& o) noexcept { bytes = o.bytes; len = o.len; ++g_traffic.frame_moves; return *this; }
};

// 이동당하면 스스로 T{} 로 돌아가는 멤버: Rule of Zero 를 유지하면서 "moved-from 은 비어 있다"를 보장한다
template <class T>
struct ExchangeOnMove {
    T value{};
    ExchangeOnMove() = default;
    explicit ExchangeOnMove(T v) : value(v) {}
    ExchangeOnMove(const ExchangeOnMove&) = default;
    ExchangeOnMove& operator=(const ExchangeOnMove&) = default;
    ExchangeOnMove(ExchangeOnMove&& o) noexcept : value(std::exchange(o.value, T{})) {}
    ExchangeOnMove& operator=(ExchangeOnMove&& o) noexcept {
        if (this != &o) value = std::exchange(o.value, T{});
        return *this;
    }
};

struct TxDesc {
    std::array<std::uint8_t, 64>  payload{};
    ExchangeOnMove<std::uint16_t> len;
};

// 고정 슬롯 4개짜리 프레임 풀 (최소 mock. 풀 자체는 CPP-M08 / DS 트랙 담당)
inline constexpr std::uint8_t kNoFrame = 0xFF;

class FramePool {
    std::array<Frame, 4> slots_{};
    std::uint8_t used_ = 0;
    unsigned double_frees_ = 0;
public:
    void reset() { used_ = 0; double_frees_ = 0; }
    std::uint8_t alloc() {
        for (std::uint8_t i = 0; i < 4; ++i) {
            const auto bit = static_cast<std::uint8_t>(1u << i);
            if ((used_ & bit) == 0) { used_ = static_cast<std::uint8_t>(used_ | bit); return i; }
        }
        return kNoFrame;
    }
    void free(std::uint8_t i) {
        const auto bit = static_cast<std::uint8_t>(i < 4 ? (1u << i) : 0u);
        if ((used_ & bit) == 0) { ++double_frees_; return; }
        used_ = static_cast<std::uint8_t>(used_ & ~bit);
    }
    Frame&   at(std::uint8_t i)    { return slots_[i & 3u]; }   // mock 이라 범위 밖은 접어 넣는다
    int      used_count() const    { return std::popcount(used_); }
    unsigned double_frees() const  { return double_frees_; }
};
inline FramePool g_pool;

// 풀 슬롯 하나의 소유권을 들고 다니는 1바이트짜리 이동 전용 핸들
class FrameHandle {
    std::uint8_t idx_ = kNoFrame;
    void release() { if (idx_ != kNoFrame) { g_pool.free(idx_); idx_ = kNoFrame; } }
public:
    FrameHandle() = default;
    explicit FrameHandle(std::uint8_t idx) : idx_(idx) {}
    FrameHandle(const FrameHandle&) = delete;
    FrameHandle& operator=(const FrameHandle&) = delete;
    FrameHandle(FrameHandle&& o) noexcept : idx_(std::exchange(o.idx_, kNoFrame)) { ++g_traffic.handle_moves; }
    FrameHandle& operator=(FrameHandle&& o) noexcept {
        ++g_traffic.handle_moves;
        if (this != &o) { release(); idx_ = std::exchange(o.idx_, kNoFrame); }
        return *this;
    }
    ~FrameHandle() { release(); }
    bool         valid() const { return idx_ != kNoFrame; }
    std::uint8_t index() const { return idx_; }
    Frame&       frame()       { return g_pool.at(idx_); }
};

// ISR → 태스크 큐 (슬롯이 T 를 값으로 품는다). push 는 rvalue 만 받아서 복사가 몰래 일어날 수 없다.
template <class T, std::size_t N>
class Ring {
    std::array<T, N> slots_{};
    std::size_t head_ = 0, tail_ = 0, count_ = 0;
public:
    bool push(T&& v) noexcept(std::is_nothrow_move_assignable_v<T>) {
        if (count_ == N) return false;
        slots_[head_] = std::move(v);
        head_ = (head_ + 1) % N;
        ++count_;
        return true;
    }
    bool pop(T& out) noexcept(std::is_nothrow_move_assignable_v<T>) {
        if (count_ == 0) return false;
        out = std::move(slots_[tail_]);
        tail_ = (tail_ + 1) % N;
        --count_;
        return true;
    }
    std::size_t size() const { return count_; }
};

// item 을 hops 번의 "큐 넣기 → 꺼내기" 로 통과시킨다. 끝나면 item 에 원래 내용이 돌아와 있어야 한다.
// 같은 코드로 T = Frame 과 T = FrameHandle 을 돌려 비용 차이가 오직 T 에서만 나오게 한다.
template <class T>
void route(T& item, unsigned hops) {
    Ring<T, 2> q;
    for (unsigned h = 0; h < hops; ++h) {
        q.push(std::move(item));
        q.pop(item);
    }
}

// ---------------- 컴파일 타임 검사 ----------------
template <class T> concept CanPushLvalue = requires(Ring<T, 2>& r, T& v) { r.push(v); };
template <class T> concept CanPushRvalue = requires(Ring<T, 2>& r, T& v) { r.push(std::move(v)); };

static_assert(!std::is_copy_constructible_v<FrameHandle> && !std::is_copy_assignable_v<FrameHandle>);
static_assert(std::is_nothrow_move_constructible_v<FrameHandle> && std::is_nothrow_move_assignable_v<FrameHandle>);
static_assert(sizeof(FrameHandle) == 1);
static_assert(!CanPushLvalue<FrameHandle> && CanPushRvalue<FrameHandle>);
static_assert(!CanPushLvalue<Frame> && CanPushRvalue<Frame>);          // Frame 도 lvalue 는 못 넣는다 (몰래 복사 금지)

// noexcept 는 -fno-exceptions 빌드에서도 타입 시스템에 그대로 남는다
static_assert(std::is_nothrow_move_constructible_v<Probe>);
static_assert(!std::is_nothrow_move_constructible_v<ThrowingProbe>);
static_assert(std::is_same_v<decltype(std::move_if_noexcept(std::declval<ThrowingProbe&>())), const ThrowingProbe&>);
static_assert(std::is_same_v<decltype(std::move_if_noexcept(std::declval<Probe&>())), Probe&&>);

// implicit move 가 사라져도 trait 은 true 다: const& 복사 생성자가 rvalue 를 받기 때문
static_assert(std::is_move_constructible_v<DtorBlocksMove>);
static_assert(!std::is_nothrow_move_constructible_v<WrapsThrowing>);
static_assert(std::is_nothrow_move_constructible_v<VecOwner>);

// 메모리 배치: 인라인 소유는 크기가 N 에 비례, vector 는 포인터 3개로 고정
static_assert(sizeof(InlineOwner<64>) > sizeof(InlineOwner<8>));
static_assert(sizeof(VecOwner) == sizeof(std::vector<int>));
static_assert(std::is_trivially_copyable_v<std::array<std::uint8_t, 64>>);

int main() {
    // Part A — 요소별 비용 표: vector 는 n 과 무관하게 0, 인라인은 "용량" N 만큼
    struct VecCase { std::size_t n; };
    for (const VecCase& t : {VecCase{1}, VecCase{8}, VecCase{64}}) {
        VecOwner src(t.n);
        const MoveReport r = measure_move(src);
        CHECK(r.cost == (Cost{0, 0}));
        CHECK(r.buffer_stolen);
        CHECK(r.src_size_after == 0);             // 표준 문구가 직접 보장하진 않는다(LWG 2321). libc++/libstdc++ 실측 결과
    }
    struct InlineCase { std::size_t used; };
    for (const InlineCase& t : {InlineCase{0}, InlineCase{1}, InlineCase{8}, InlineCase{64}}) {
        InlineOwner<64> src(t.used);
        const MoveReport r = measure_move(src);
        CHECK(r.cost == (Cost{0, 64}));           // used 가 아니라 capacity 만큼 이동한다
        CHECK(!r.buffer_stolen);
        CHECK(r.src_size_after == t.used);        // count 는 그대로 남는다
    }
    {   // 용량이 다르면 비용도 비례
        InlineOwner<8> s8(3);
        InlineOwner<32> s32(3);
        CHECK(measure_move(s8).cost == (Cost{0, 8}));
        CHECK(measure_move(s32).cost == (Cost{0, 32}));
    }
    {   // 값 흐름: 사용 중인 요소는 전부 옮겨지고 원본 요소는 moved-from
        InlineOwner<8> src(8);
        ops_reset();
        InlineOwner<8> dst(std::move(src));
        CHECK(dst.items[0].value == 0 && dst.items[7].value == 7);
        CHECK(src.items[0].value == MOVED_FROM && src.items[7].value == MOVED_FROM);
        CHECK(g_ops.move_ctor == 8 && g_ops.copy_ctor == 0);
    }
    {   // 이동 대입: 인라인은 요소별 대입, vector 는 버퍼 교체
        InlineOwner<8> a(2), b(5);
        ops_reset();
        a = std::move(b);
        CHECK(g_ops.move_assign == 8 && g_ops.copy_assign == 0 && a.size() == 5);
        VecOwner c(3), d(7);
        const Probe* dp = d.data();
        ops_reset();
        c = std::move(d);
        CHECK(g_ops.moves() == 0 && g_ops.copies() == 0);
        CHECK(c.data() == dp && c.size() == 7);
        CHECK(g_ops.dtor == 3);                   // c 가 갖고 있던 3개만 파괴된다
    }
    {   // trivially copyable 요소: move 는 그냥 복사. 원본 바이트가 그대로 남는다
        std::array<std::uint8_t, 64> a{};
        for (std::size_t i = 0; i < a.size(); ++i) a[i] = static_cast<std::uint8_t>(i * 7);
        auto b = std::move(a);
        CHECK(b == a);
    }

    // Part B — move_if_noexcept 선택표 (컴파일 타임)
    CHECK(reloc_choice<Probe>() == Reloc::Move);
    CHECK(reloc_choice<ThrowingProbe>() == Reloc::Copy);
    CHECK(reloc_choice<MoveOnlyThrowing>() == Reloc::Move);   // 복사가 불가능하면 약한 보장으로 이동
    CHECK(reloc_choice<WrapsThrowing>() == Reloc::Copy);
    CHECK(reloc_choice<int>() == Reloc::Move);

    // Part B — relocate 미니 모델 실측
    {
        auto run = [](auto tag, std::size_t n, Cost want) {
            using T = typename decltype(tag)::type;
            std::allocator<T> a;
            T* from = a.allocate(n);
            T* to   = a.allocate(n);
            for (std::size_t i = 0; i < n; ++i) std::construct_at(from + i, static_cast<int>(i));
            ops_reset();
            const std::size_t made = relocate(from, n, to);
            const Cost c{g_ops.copies(), g_ops.moves()};
            bool values_ok = (made == n);
            for (std::size_t i = 0; i < made; ++i) values_ok = values_ok && (to[i].value == static_cast<int>(i));
            if (made == n) std::destroy_n(to, n); else std::destroy_n(from, n);
            a.deallocate(from, n);
            a.deallocate(to, n);
            return c == want && values_ok;
        };
        CHECK(run(std::type_identity<Probe>{}, 8, Cost{0, 8}));
        CHECK(run(std::type_identity<ThrowingProbe>{}, 8, Cost{8, 0}));
        CHECK(run(std::type_identity<MoveOnlyThrowing>{}, 8, Cost{0, 8}));
        CHECK(run(std::type_identity<Probe>{}, 1, Cost{0, 1}));
        CHECK(run(std::type_identity<ThrowingProbe>{}, 1, Cost{1, 0}));
    }

    // Part B — 실제 std::vector (호스트 전용)
    CHECK(vector_growth_cost<Probe>(8) == (Cost{0, 8}));
    CHECK(vector_growth_cost<ThrowingProbe>(8) ==
          (kVectorIgnoresThrowingMove ? Cost{0, 8} : Cost{8, 0}));
    CHECK(vector_growth_cost<MoveOnlyThrowing>(8) == (Cost{0, 8}));

    // Part B — 소멸자 선언이 implicit move 를 없앤다
    {
        DtorBlocksMove a(1);
        ops_reset();
        DtorBlocksMove b(std::move(a));           // "이동"처럼 생겼지만 복사 생성자가 불린다
        CHECK(g_ops.copy_ctor == 1 && g_ops.move_ctor == 0 && b.p.value == 1);
        CHECK(a.p.value == 1);                    // 원본이 그대로
    }
    {
        DtorKeepsMove a(2);
        ops_reset();
        DtorKeepsMove b(std::move(a));
        CHECK(g_ops.copy_ctor == 0 && g_ops.move_ctor == 1 && b.p.value == 2);
        CHECK(a.p.value == MOVED_FROM);
        CHECK(std::is_nothrow_move_constructible_v<DtorKeepsMove>);
    }

    // Part C — moved-from 인라인 프레임은 "여전히 유효해 보인다"
    {
        Frame a;
        a.len = 3;
        a.bytes[0] = 0xAA;
        g_traffic = Traffic{};
        Frame b(std::move(a));
        CHECK(b.len == 3 && b.bytes[0] == 0xAA);
        CHECK(a.len == 3 && a.bytes[0] == 0xAA);  // 이동 후에도 원본이 그대로: 두 번 처리 버그의 씨앗
        CHECK(g_traffic.frame_moves == 1 && g_traffic.frame_copies == 0);
    }
    {   // ExchangeOnMove: 원본 길이가 0 으로 돌아간다
        TxDesc a;
        a.len = ExchangeOnMove<std::uint16_t>(3);
        a.payload[0] = 0x5A;
        TxDesc b(std::move(a));
        CHECK(b.len.value == 3 && a.len.value == 0);
        CHECK(b.payload[0] == 0x5A);
        CHECK(a.payload[0] == 0x5A);              // payload 는 복사될 뿐 지워지지 않는다 (지우려면 추가 비용)
        TxDesc c;
        c = std::move(b);
        CHECK(c.len.value == 3 && b.len.value == 0);
        CHECK(std::is_nothrow_move_constructible_v<TxDesc> && std::is_nothrow_move_assignable_v<TxDesc>);
        CHECK(!std::is_trivially_copyable_v<TxDesc>);
    }

    // Part C — 핸들의 소유권 규칙
    {
        g_pool.reset();
        {
            FrameHandle a(g_pool.alloc());
            CHECK(a.valid() && g_pool.used_count() == 1);
            FrameHandle b(std::move(a));
            CHECK(!a.valid() && b.valid());
            CHECK(g_pool.used_count() == 1);
        }
        CHECK(g_pool.used_count() == 0);          // b 만 슬롯을 돌려준다
        CHECK(g_pool.double_frees() == 0);        // moved-from a 의 소멸은 아무것도 안 한다
    }
    {   // 이동 대입: 덮어쓰이는 쪽의 슬롯이 먼저 풀로 돌아가야 한다
        g_pool.reset();
        {
            FrameHandle a(g_pool.alloc());
            FrameHandle b(g_pool.alloc());
            CHECK(g_pool.used_count() == 2);
            a = std::move(b);
            CHECK(g_pool.used_count() == 1 && a.valid() && !b.valid());
            FrameHandle& alias = a;
            a = std::move(alias);                 // 자기 이동 대입: 슬롯을 잃으면 안 된다
            CHECK(a.valid() && g_pool.used_count() == 1);
        }
        CHECK(g_pool.used_count() == 0 && g_pool.double_frees() == 0);
    }
    {   // 풀이 비면 무효 핸들
        g_pool.reset();
        {
            FrameHandle h0(g_pool.alloc()), h1(g_pool.alloc()), h2(g_pool.alloc()), h3(g_pool.alloc());
            FrameHandle h4(g_pool.alloc());
            CHECK(h3.valid() && !h4.valid());
        }
        CHECK(g_pool.used_count() == 0 && g_pool.double_frees() == 0);
    }

    // Part C — 같은 route() 로 값 전달 vs 핸들 전달
    for (unsigned hops : {1u, 2u, 3u, 8u}) {
        Frame f;
        f.len = 3;
        f.bytes[0] = 0xAA;
        g_traffic = Traffic{};
        route(f, hops);
        CHECK(g_traffic.frame_moves == 2 * hops && g_traffic.frame_copies == 0);
        CHECK(f.len == 3 && f.bytes[0] == 0xAA);

        g_pool.reset();
        {
            FrameHandle h(g_pool.alloc());
            h.frame().len = 3;
            h.frame().bytes[0] = 0xAA;
            const Frame* where = &h.frame();
            g_traffic = Traffic{};
            route(h, hops);
            CHECK(g_traffic.handle_moves == 2 * hops && g_traffic.frame_moves == 0);
            CHECK(h.valid() && &h.frame() == where);   // 페이로드는 한 번도 움직이지 않았다
            CHECK(h.frame().len == 3 && h.frame().bytes[0] == 0xAA);
        }
        CHECK(g_pool.used_count() == 0 && g_pool.double_frees() == 0);
    }
    {   // 전송 바이트 비교 (3홉 = DMA ISR → 파서 → 앱)
        const std::size_t by_value  = 2 * 3 * sizeof(Frame);
        const std::size_t by_handle = 2 * 3 * sizeof(FrameHandle);
        CHECK(by_value == 1548 && by_handle == 6);
    }
    {   // 풀 슬롯이 있는 동안 Ring 은 핸들을 들고 있을 수 있다 (FIFO 순서)
        g_pool.reset();
        {
            Ring<FrameHandle, 2> q;
            FrameHandle a(g_pool.alloc()), b(g_pool.alloc()), c(g_pool.alloc());
            const std::uint8_t ia = a.index(), ib = b.index();
            CHECK(q.push(std::move(a)) && q.push(std::move(b)));
            CHECK(!q.push(std::move(c)) && c.valid());   // 꽉 찬 큐는 소유권을 가져가지 않는다
            FrameHandle out;
            CHECK(q.pop(out) && out.index() == ia);
            CHECK(q.pop(out) && out.index() == ib);       // 덮어쓰인 ia 슬롯은 풀로 돌아간다
            CHECK(g_pool.used_count() == 2);
            CHECK(!q.pop(out) && out.valid());
        }
        CHECK(g_pool.used_count() == 0 && g_pool.double_frees() == 0);
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
