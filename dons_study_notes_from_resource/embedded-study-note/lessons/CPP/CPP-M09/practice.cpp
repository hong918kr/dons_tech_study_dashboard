// CPP-M09 추가 연습 — 한 번에 검증
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o m09p && ./m09p
#include <array>
#include <cstddef>
#include <cstdio>
#include <string>
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

struct Ops { unsigned copy_ctor = 0, move_ctor = 0, copy_assign = 0, move_assign = 0; };
inline Ops g;
inline void reset() { g = Ops{}; }
inline unsigned copies() { return g.copy_ctor + g.copy_assign; }
inline unsigned moves()  { return g.move_ctor + g.move_assign; }

template <bool NX>
struct ProbeT {
    int value = 0;
    ProbeT() = default;
    explicit ProbeT(int v) : value(v) {}
    ProbeT(const ProbeT& o) : value(o.value) { ++g.copy_ctor; }
    ProbeT(ProbeT&& o) noexcept(NX) : value(o.value) { ++g.move_ctor; o.value = -1; }
    ProbeT& operator=(const ProbeT& o) { value = o.value; ++g.copy_assign; return *this; }
    ProbeT& operator=(ProbeT&& o) noexcept(NX) { value = o.value; ++g.move_assign; if (this != &o) o.value = -1; return *this; }
};
using Probe         = ProbeT<true>;
using ThrowingProbe = ProbeT<false>;

struct MoveOnlyThrowing {
    MoveOnlyThrowing() = default;
    MoveOnlyThrowing(const MoveOnlyThrowing&) = delete;
    MoveOnlyThrowing(MoveOnlyThrowing&&) noexcept(false) {}
};
struct CopyOnly {                 // move 를 선언하지 않고 복사만 있다
    CopyOnly() = default;
    CopyOnly(const CopyOnly&) {}
};

// 연습 1: move_if_noexcept 규칙을 trait 하나로
template <class T>
inline constexpr bool relocates_by_move_v =
    std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>;

template <class T>
inline constexpr bool mine_matches_std_v =
    relocates_by_move_v<T> == std::is_rvalue_reference_v<decltype(std::move_if_noexcept(std::declval<T&>()))>;

static_assert(mine_matches_std_v<Probe> && mine_matches_std_v<ThrowingProbe>);
static_assert(mine_matches_std_v<MoveOnlyThrowing> && mine_matches_std_v<CopyOnly>);
static_assert(mine_matches_std_v<int> && mine_matches_std_v<std::vector<int>>);

// 연습 2: swap 의 비용. std::swap = 임시 생성 1 + 대입 2
template <std::size_t N>
struct Inline { std::array<Probe, N> items{}; };
struct Heap   { std::vector<Probe> items; };

template <class Owner>
unsigned swap_moves(Owner& a, Owner& b) {
    using std::swap;
    reset();
    swap(a, b);
    return moves();
}

// 연습 3: const 는 move 를 조용히 복사로 바꾼다
inline unsigned const_move_copies() {
    const Probe c(7);
    reset();
    Probe d(std::move(c));
    return copies() + static_cast<unsigned>(d.value != 7);
}

// 연습 4: push_back 재배치와 reserve
inline unsigned push_back_relocations(std::size_t n, bool reserve_first) {
    std::vector<Probe> v;
    if (reserve_first) v.reserve(n);
    reset();
    for (std::size_t i = 0; i < n; ++i) v.emplace_back(static_cast<int>(i));
    return moves() + copies();
}

// 연습 5: SSO. 같은 std::string 인데 길이에 따라 move 가 "포인터 훔치기"일 수도 "요소 복사"일 수도 있다
inline bool data_inside_object(const std::string& s) {
    const char* p   = s.data();
    const char* obj = reinterpret_cast<const char*>(&s);
    return p >= obj && p < obj + sizeof(std::string);
}
struct MoveObservation { bool was_inline; bool data_kept; };

inline MoveObservation observe_string_move(std::string src) {
    const bool inline_before = data_inside_object(src);
    const char* p = src.data();
    std::string dst(std::move(src));
    return MoveObservation{inline_before, dst.data() == p};
}

int main() {
    // 연습 1
    CHECK(relocates_by_move_v<Probe>);
    CHECK(!relocates_by_move_v<ThrowingProbe>);
    CHECK(relocates_by_move_v<MoveOnlyThrowing>);
    CHECK(!relocates_by_move_v<CopyOnly>);
    CHECK(relocates_by_move_v<int>);

    // 연습 2
    { Inline<1>  a, b; CHECK(swap_moves(a, b) == 3u); }
    { Inline<8>  a, b; CHECK(swap_moves(a, b) == 24u); }
    { Inline<32> a, b; CHECK(swap_moves(a, b) == 96u); }
    {
        Heap a, b;
        a.items.resize(100);
        b.items.resize(3);
        CHECK(swap_moves(a, b) == 0u);       // 요소 100개여도 포인터 교환
        CHECK(a.items.size() == 3 && b.items.size() == 100);
    }

    // 연습 3
    CHECK(const_move_copies() == 1u);

    // 연습 4
    CHECK(push_back_relocations(64, true) == 0u);
    CHECK(push_back_relocations(64, false) >= 32u);    // 증가 인수가 2 이하이면 마지막 재배치에서 절반 이상을 옮긴다 (실측: 63회)

    // 연습 5
    {
        const MoveObservation shorty = observe_string_move(std::string(3, 'x'));
        CHECK(shorty.was_inline && !shorty.data_kept);  // SSO: 객체 안에 있으므로 주소가 바뀐다
        const MoveObservation longy = observe_string_move(std::string(200, 'y'));
        CHECK(!longy.was_inline && longy.data_kept);    // 힙: 포인터를 훔쳐서 주소가 그대로다
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
