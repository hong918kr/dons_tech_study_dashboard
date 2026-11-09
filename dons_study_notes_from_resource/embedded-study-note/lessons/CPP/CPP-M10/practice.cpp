// CPP-M10 추가 연습 — 한 번에 검증
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
//
// 본문 static_vector 의 핵심만 줄인 sv<T, N> 위에 연습 1~5 의 답을 얹었다.
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

// 연습 3: N 을 담을 수 있는 가장 작은 부호 없는 정수
template <std::size_t N>
using smallest_size_t = std::conditional_t<(N <= 0xFF), std::uint8_t,
                        std::conditional_t<(N <= 0xFFFF), std::uint16_t,
                        std::conditional_t<(N <= 0xFFFF'FFFFu), std::uint32_t, std::size_t>>>;

template <typename T, std::size_t N>
class sv {
    alignas(T) std::byte storage_[N != 0 ? N * sizeof(T) : 1];
    smallest_size_t<N> size_ = 0;

    T* slot_(std::size_t i) noexcept { return reinterpret_cast<T*>(storage_ + i * sizeof(T)); }
    const T* slot_(std::size_t i) const noexcept { return reinterpret_cast<const T*>(storage_ + i * sizeof(T)); }

public:
    sv() noexcept {}
    ~sv() requires std::is_trivially_destructible_v<T> = default;
    ~sv() { truncate(0); }
    sv(const sv&) = delete;
    sv& operator=(const sv&) = delete;

    // 연습 5: 더 작은 용량에서만 변환 (M == N 은 복사 생성자의 몫)
    template <std::size_t M>
        requires(M < N && std::is_copy_constructible_v<T>)
    explicit sv(const sv<T, M>& o) {
        for (const T& x : o) (void)emplace_back(x);
    }

    std::size_t size() const noexcept { return size_; }
    static constexpr std::size_t capacity() noexcept { return N; }
    T* data() noexcept { return size_ ? std::launder(slot_(0)) : slot_(0); }
    const T* data() const noexcept { return size_ ? std::launder(slot_(0)) : slot_(0); }
    T* begin() noexcept { return data(); }
    T* end() noexcept { return data() + size_; }
    const T* begin() const noexcept { return data(); }
    const T* end() const noexcept { return data() + size_; }

    template <typename... A>
    T* emplace_back(A&&... a) {
        if (size_ == N) return nullptr;
        T* p = std::construct_at(slot_(size_), std::forward<A>(a)...);
        ++size_;
        return p;
    }

    void truncate(std::size_t count) noexcept {
        if (count >= size_) return;
        std::destroy(data() + count, data() + size_);
        size_ = static_cast<smallest_size_t<N>>(count);
    }

    // 연습 1: 늘릴 때는 값 초기화(또는 value 복사), 줄일 때는 소멸. N 을 넘으면 N 까지만 채우고 false.
    bool resize(std::size_t count) requires std::is_default_constructible_v<T> {
        truncate(count);
        const std::size_t target = std::min(count, N);
        while (size_ < target) (void)emplace_back();
        return count <= N;
    }
    bool resize(std::size_t count, const T& value) requires std::is_copy_constructible_v<T> {
        truncate(count);
        const std::size_t target = std::min(count, N);
        while (size_ < target) (void)emplace_back(value);
        return count <= N;
    }

    // 연습 2: 순서를 포기한 O(1) 삭제. 마지막 원소를 pos 로 옮기고 끝 칸을 소멸.
    void swap_remove(std::size_t i) {
        assert(i < size_);
        T* const d = data();
        if (i != static_cast<std::size_t>(size_ - 1)) d[i] = std::move(d[size_ - 1]);   // 자기 이동 대입 방지
        std::destroy_at(d + size_ - 1);
        --size_;
    }
};

// 연습 4: C++20 std::erase_if 와 같은 모양의 자유 함수. 지운 개수 반환.
template <typename T, std::size_t N, typename Pred>
std::size_t erase_if(sv<T, N>& v, Pred pred) {
    T* const new_end = std::remove_if(v.begin(), v.end(), pred);
    const auto removed = static_cast<std::size_t>(v.end() - new_end);
    v.truncate(v.size() - removed);
    return removed;
}

// ---------------- 하네스 ----------------
static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS: %s\n", #expr); } \
                         else std::printf("FAIL: %s (line %d)\n", #expr, __LINE__); } while (0)

struct T1 {
    static inline int live = 0, self_move = 0, moves = 0;
    int v;
    explicit T1(int x = 0) : v(x) { ++live; }
    T1(const T1& o) : v(o.v) { ++live; }
    T1(T1&& o) noexcept : v(o.v) { o.v = -1; ++live; ++moves; }
    T1& operator=(T1&& o) noexcept { if (this == &o) ++self_move; v = o.v; o.v = -1; ++moves; return *this; }
    T1& operator=(const T1&) = default;
    ~T1() { --live; }
};

template <typename V>
static bool eq(const V& v, std::initializer_list<int> want) {
    if (v.size() != want.size()) return false;
    auto it = v.begin();
    for (int w : want) { if (it->v != w) return false; ++it; }
    return true;
}
static bool eq_int(const sv<int, 6>& v, std::initializer_list<int> want) {
    return v.size() == want.size() && std::equal(v.begin(), v.end(), want.begin());
}

static void ex1_resize() {
    std::puts("-- 연습 1: resize --");
    sv<int, 6> v;
    (void)v.emplace_back(7);
    CHECK(v.resize(3) && eq_int(v, {7, 0, 0}));          // 값 초기화: 쓰레기가 아니라 0
    CHECK(v.resize(5, 9) && eq_int(v, {7, 0, 0, 9, 9}));
    CHECK(v.resize(2) && eq_int(v, {7, 0}));
    CHECK(!v.resize(10, 1) && v.size() == 6);            // 넘으면 N 까지만 채우고 false
    {
        sv<T1, 4> t;
        CHECK(t.resize(4) && T1::live == 4);
        CHECK(t.resize(1) && T1::live == 1);
    }
    CHECK(T1::live == 0);
}

static void ex2_swap_remove() {
    std::puts("-- 연습 2: swap_remove --");
    T1::self_move = 0;
    T1::moves = 0;
    sv<T1, 4> v;
    for (int x : {1, 2, 3, 4}) (void)v.emplace_back(x);
    T1::moves = 0;
    v.swap_remove(0);
    CHECK(eq(v, {4, 2, 3}) && T1::moves == 1);           // 원소 수와 무관하게 이동 1번
    v.swap_remove(2);                                    // 마지막 칸 지우기: 자기 이동 대입이 없어야 한다
    CHECK(eq(v, {4, 2}) && T1::self_move == 0);
    v.swap_remove(1);
    v.swap_remove(0);
    CHECK(v.size() == 0 && T1::self_move == 0);
}

static void ex3_size_type() {
    std::puts("-- 연습 3: 작은 size 타입 --");
    CHECK(sizeof(sv<std::uint8_t, 16>) == 17);
    CHECK(sizeof(sv<std::uint8_t, 255>) == 256);
    CHECK(sizeof(sv<std::uint8_t, 256>) == 258);         // uint16_t 로 커지고 정렬 패딩은 없다 (alignof == 2)
    CHECK(sizeof(sv<std::uint32_t, 16>) == 68);          // T 가 4바이트 정렬이면 뒤 패딩 때문에 절약 없음
    CHECK((std::is_same_v<smallest_size_t<255>, std::uint8_t>));
    CHECK((std::is_same_v<smallest_size_t<256>, std::uint16_t>));
    CHECK((std::is_same_v<smallest_size_t<65536>, std::uint32_t>));
    sv<int, 3> v;                                        // N 경계에서 size_ 가 넘치지 않는지
    for (int i = 0; i < 5; ++i) (void)v.emplace_back(i);
    CHECK(v.size() == 3);
    sv<std::uint8_t, 255> big;
    for (int i = 0; i < 300; ++i) (void)big.emplace_back(static_cast<std::uint8_t>(i));
    CHECK(big.size() == 255);                            // uint8_t size_ 가 255 에서 멈추고 wrap 하지 않는다
}

static void ex4_erase_if() {
    std::puts("-- 연습 4: erase_if --");
    {
        sv<T1, 6> v;
        for (int x : {1, 2, 3, 4, 5, 6}) (void)v.emplace_back(x);
        CHECK(erase_if(v, [](const T1& t) { return t.v % 2 == 0; }) == 3);
        CHECK(eq(v, {1, 3, 5}) && T1::live == 3);
        CHECK(erase_if(v, [](const T1&) { return false; }) == 0 && v.size() == 3);
        CHECK(erase_if(v, [](const T1&) { return true; }) == 3 && v.size() == 0 && T1::live == 0);
    }
    CHECK(T1::live == 0);
}

static void ex5_convert() {
    std::puts("-- 연습 5: 작은 용량에서 변환 --");
    CHECK((std::is_constructible_v<sv<int, 4>, const sv<int, 2>&>));
    CHECK(!(std::is_constructible_v<sv<int, 4>, const sv<int, 8>&>));  // 넘칠 수 있는 변환은 컴파일 불가
    CHECK(!(std::is_convertible_v<const sv<int, 2>&, sv<int, 4>>));    // explicit: 암시적 복사가 숨지 않게
    sv<int, 2> a;
    (void)a.emplace_back(1);
    (void)a.emplace_back(2);
    sv<int, 4> b(a);
    CHECK(b.size() == 2 && *b.begin() == 1 && *(b.begin() + 1) == 2);
    CHECK(b.capacity() == 4);
}

int main() {
    ex1_resize();
    ex2_swap_remove();
    ex3_size_type();
    ex4_erase_if();
    ex5_convert();
    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
