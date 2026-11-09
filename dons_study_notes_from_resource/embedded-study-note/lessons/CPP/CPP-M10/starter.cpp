// CPP-M10 ETL 스타일 static_vector<T, N> 구현
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
//
// 힙 없는 고정 용량 벡터 = std::vector 의 인터페이스 + std::array 의 저장 위치.
//   저장소: alignas(T) std::byte 배열 (수명 규칙은 CPP-M07 참고)
//   칸 [0, size_) 만 객체가 살아 있고, [size_, N) 은 객체가 없는 날것(raw) 메모리다.
//   용량 초과(런타임 상황)  → Overflow 정책: ReturnOnFull(기본, nullptr/false) / TrapOnFull(abort)
//   전제조건 위반(호출자 버그: 빈 벡터 pop_back, 범위 밖 pos) → assert
#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>            // 무작위 차분 테스트의 기준 모델로만 사용 (호스트 전용)

#include <signal.h>          // death test (호스트 전용)
#include <sys/wait.h>
#include <unistd.h>

// ---------------- 용량 초과 정책 ----------------
struct ReturnOnFull { static void on_overflow() noexcept {} };                      // 호출자가 nullptr/false 를 확인
struct TrapOnFull { [[noreturn]] static void on_overflow() noexcept { std::abort(); } };  // 펌웨어면 fault handler/BKPT

// ---------------- Part A/B: static_vector ----------------
template <typename T, std::size_t N, typename Overflow = ReturnOnFull>
class static_vector {
    static_assert(std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>,
                  "static_vector<T>: T must be a non-cv object type");

public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;                 // 연속 저장이므로 포인터가 곧 contiguous iterator
    using const_iterator = const T*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

private:
    static constexpr bool kTrivialDtor = std::is_trivially_destructible_v<T>;
    static constexpr bool kTrivialCopyCtor = std::is_trivially_copy_constructible_v<T>;
    static constexpr bool kTrivialMoveCtor = std::is_trivially_move_constructible_v<T>;
    static constexpr bool kTrivialCopyAsg = kTrivialDtor && kTrivialCopyCtor && std::is_trivially_copy_assignable_v<T>;
    static constexpr bool kTrivialMoveAsg = kTrivialDtor && kTrivialMoveCtor && std::is_trivially_move_assignable_v<T>;

    // N == 0 이어도 크기 0 배열은 ill-formed 이므로 최소 1바이트
    alignas(T) std::byte storage_[N != 0 ? N * sizeof(T) : 1];
    size_type size_ = 0;

    // i 번째 칸의 "주소". 객체가 없어도 되는 용도(construct_at 대상)에만 쓴다.
    T*       slot_(size_type i) noexcept       { return reinterpret_cast<T*>(storage_ + i * sizeof(T)); }
    const T* slot_(size_type i) const noexcept { return reinterpret_cast<const T*>(storage_ + i * sizeof(T)); }

    // Part B: [0, size_) 를 src 의 n 개로 바꾼다. 살아 있는 칸은 대입, 빈 칸은 생성, 남는 칸은 소멸.
    //   It 가 move_iterator 면 그대로 "이동" 버전이 된다.
    template <typename It>
    void assign_range_(It src, size_type n) {
        (void)src; (void)n; // TODO(Part B): i < min(size_, n) 대입 → i < n 은 construct_at → [n, size_) destroy → size_ = n
    }

public:
    // user-provided 라서 `static_vector<...> v{};` 가 저장소 전체를 0 으로 채우지 않는다
    static_vector() noexcept {}

    static_vector(std::initializer_list<T> il) requires std::is_copy_constructible_v<T> {
        for (const T& x : il) if (!push_back(x)) break;
    }

    // ---- Part B: 소멸자 / 복사 / 이동 (조건부 trivial) ----
    ~static_vector() { /* TODO(Part A): 살아 있는 원소 전부 소멸. TODO(Part B): T 가 trivially destructible 이면 trivial 소멸자가 되게 */ }

    static_vector(const static_vector& o) requires std::is_copy_constructible_v<T> { (void)o; } // TODO(Part B)
    static_vector(static_vector&& o) requires std::is_move_constructible_v<T> { (void)o; }      // TODO(Part B)
    static_vector& operator=(const static_vector& o)
        requires(std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>) { (void)o; return *this; } // TODO(Part B)
    static_vector& operator=(static_vector&& o)
        requires(std::is_move_constructible_v<T> && std::is_move_assignable_v<T>) { (void)o; return *this; } // TODO(Part B)

    // ---- 크기 ----
    size_type size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    bool full() const noexcept { return size_ == N; }
    static constexpr size_type capacity() noexcept { return N; }
    static constexpr size_type max_size() noexcept { return N; }

    // ---- 원소 접근 ----
    // std::launder 는 "그 주소에 살아 있는 T 가 있을 것"이 전제조건이라, 비었을 때는 쓰면 안 된다 (CPP-M07 참고)
    T*       data() noexcept       { return size_ ? std::launder(slot_(0)) : slot_(0); }
    const T* data() const noexcept { return size_ ? std::launder(slot_(0)) : slot_(0); }

    T&       operator[](size_type i) noexcept       { assert(i < size_); return *std::launder(slot_(i)); }
    const T& operator[](size_type i) const noexcept { assert(i < size_); return *std::launder(slot_(i)); }
    T&       front() noexcept       { return (*this)[0]; }
    const T& front() const noexcept { return (*this)[0]; }
    T&       back() noexcept       { return (*this)[size_ - 1]; }
    const T& back() const noexcept { return (*this)[size_ - 1]; }
    // at() 의 예외 없는 대체: 범위 밖이면 nullptr
    T*       try_at(size_type i) noexcept       { return i < size_ ? data() + i : nullptr; }
    const T* try_at(size_type i) const noexcept { return i < size_ ? data() + i : nullptr; }

    // ---- 반복자 ----
    iterator begin() noexcept { return data(); }
    iterator end() noexcept { return data() + size_; }
    const_iterator begin() const noexcept { return data(); }
    const_iterator end() const noexcept { return data() + size_; }
    const_iterator cbegin() const noexcept { return data(); }
    const_iterator cend() const noexcept { return data() + size_; }
    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

    // ---- Part A: 끝에 추가 / 제거 ----
    // 성공하면 새 원소 포인터, 꽉 찼으면 Overflow::on_overflow() 후 nullptr (인자는 건드리지 않음)
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    [[nodiscard]] T* emplace_back(Args&&... args) {
        ((void)args, ...);
        return nullptr; // TODO(Part A): full 검사 → std::construct_at(slot_(size_), forward...) → ++size_
    }
    [[nodiscard]] bool push_back(const T& x) requires std::is_copy_constructible_v<T> { return emplace_back(x) != nullptr; }
    [[nodiscard]] bool push_back(T&& x) requires std::is_move_constructible_v<T> { return emplace_back(std::move(x)) != nullptr; }

    void pop_back() {
        // TODO(Part A): 비었으면 assert. 마지막 원소 소멸 후 --size_
    }

    void clear() noexcept {
        // TODO(Part A): 살아 있는 원소 전부 소멸, size_ = 0
    }

    // ---- Part B: 중간 삽입 / 삭제 ----
    // pos 앞에 삽입. 꽉 찼으면 on_overflow() 후 nullptr. 인자가 자기 원소를 가리켜도 안전해야 한다.
    template <typename... Args>
        requires(std::is_constructible_v<T, Args...> && std::is_move_constructible_v<T> && std::is_move_assignable_v<T>)
    [[nodiscard]] iterator emplace(const_iterator pos, Args&&... args) {
        (void)pos; ((void)args, ...);
        return nullptr; // TODO(Part B): pos == end 면 emplace_back. 아니면 임시 객체 → 끝 칸 생성 → move_backward → 대입
    }
    [[nodiscard]] iterator insert(const_iterator pos, const T& x)
        requires(std::is_copy_constructible_v<T> && std::is_move_assignable_v<T>) { return emplace(pos, x); }
    [[nodiscard]] iterator insert(const_iterator pos, T&& x)
        requires(std::is_move_constructible_v<T> && std::is_move_assignable_v<T>) { return emplace(pos, std::move(x)); }

    // pos 원소 제거. 반환: 제거된 다음 원소 위치
    iterator erase(const_iterator pos) requires std::is_move_assignable_v<T> {
        return const_cast<iterator>(pos); // TODO(Part B): 뒤 원소를 한 칸씩 이동 대입 → 맨 끝 칸 소멸 → --size_
    }

    iterator erase(const_iterator first, const_iterator last) requires std::is_move_assignable_v<T> {
        (void)last;
        return const_cast<iterator>(first); // TODO(Part B): first == last 면 no-op. [last, end) 이동 대입 → 남는 꼬리 소멸
    }

    friend bool operator==(const static_vector& a, const static_vector& b)
        requires requires(const T& x) { { x == x } -> std::convertible_to<bool>; } {
        return a.size_ == b.size_ && std::equal(a.begin(), a.end(), b.begin());
    }
};

// ---------------- Part C: CAN 송신 대기열 (버퍼 임대 + 우선순위) ----------------
class BufPool {
    bool used_[8]{};
public:
    unsigned bad_release = 0;
    int acquire() { for (int i = 0; i < 8; ++i) if (!used_[i]) { used_[i] = true; return i; } return -1; }
    void release(int i) { if (i < 0 || i >= 8 || !used_[i]) { ++bad_release; return; } used_[i] = false; }
    unsigned free_count() const { unsigned n = 0; for (bool u : used_) n += !u; return n; }
};
static BufPool g_pool;

// 풀 버퍼 한 칸의 소유권 (move-only RAII, CPP-B06/B07 참고): 소멸·대입되면 자동 반납
class Lease {
    int idx_ = -1;
public:
    Lease() = default;
    explicit Lease(int i) : idx_(i) {}
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    Lease(Lease&& o) noexcept : idx_(std::exchange(o.idx_, -1)) {}
    Lease& operator=(Lease&& o) noexcept { if (this != &o) { reset(); idx_ = std::exchange(o.idx_, -1); } return *this; }
    ~Lease() { reset(); }
    void reset() { if (idx_ >= 0) { g_pool.release(idx_); idx_ = -1; } }
    bool valid() const { return idx_ >= 0; }
    int index() const { return idx_; }
};

struct TxFrame { std::uint32_t id; std::uint32_t deadline_ms; Lease buf; };

template <std::size_t N>
class CanTxQueue {
    static_vector<TxFrame, N> q_;
public:
    const static_vector<TxFrame, N>& items() const { return q_; }

    // id 오름차순 정렬 유지(작은 id = 높은 우선순위), 같은 id 는 먼저 넣은 프레임이 앞.
    // 꽉 찼으면 false 이고 f 는 그대로(버퍼 소유권은 호출자에게 남는다).
    bool enqueue(TxFrame&& f) {
        (void)f;
        return false; // TODO(Part C): full 검사 → upper_bound 로 위치 → insert(std::move)
    }

    // 맨 앞 프레임을 out 으로 옮기고 제거. 비었으면 false.
    bool pop_next(TxFrame& out) {
        (void)out;
        return false; // TODO(Part C)
    }

    // deadline 이 지난 프레임을 모두 버리고 개수를 반환. 시각은 32비트 wrap-around 안전하게 비교.
    std::size_t expire(std::uint32_t now_ms) {
        (void)now_ms;
        return 0; // TODO(Part C): remove_if + erase(범위)
    }
};

// ---------------- 테스트 하네스 ----------------
static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS: %s\n", #expr); } \
                         else std::printf("FAIL: %s (line %d)\n", #expr, __LINE__); } while (0)

// 생성·소멸·대입을 전부 세고, "살아 있는 주소" 장부로 수명 위반까지 잡는 타입.
//   bad 는 죽은 칸에 대입 / 이중 소멸 / 이중 생성 / 죽은 객체에서 복사한 횟수
struct Stats { int ctor, copy, move, copy_asg, move_asg, dtor, live, bad; };
static Stats g{};
static const void* g_alive[64];
static unsigned g_alive_n = 0;
static bool is_alive(const void* p) { for (unsigned i = 0; i < g_alive_n; ++i) if (g_alive[i] == p) return true; return false; }
static void mark_alive(const void* p) { if (is_alive(p) || g_alive_n == 64) { ++g.bad; return; } g_alive[g_alive_n++] = p; ++g.live; }
static void mark_dead(const void* p) {
    for (unsigned i = 0; i < g_alive_n; ++i) if (g_alive[i] == p) { g_alive[i] = g_alive[--g_alive_n]; --g.live; return; }
    ++g.bad;
}
static void reset_all() { g = Stats{}; g_alive_n = 0; }
static void reset_counts() { const int l = g.live, b = g.bad; g = Stats{}; g.live = l; g.bad = b; }
static void need_alive(const void* p) { if (!is_alive(p)) ++g.bad; }

struct Tracked {
    int v;
    explicit Tracked(int x = 0) : v(x) { ++g.ctor; mark_alive(this); }
    Tracked(const Tracked& o) : v(o.v) { ++g.copy; need_alive(&o); mark_alive(this); }
    Tracked(Tracked&& o) noexcept : v(o.v) { ++g.move; need_alive(&o); o.v = -1; mark_alive(this); }
    Tracked& operator=(const Tracked& o) { ++g.copy_asg; need_alive(this); need_alive(&o); v = o.v; return *this; }
    Tracked& operator=(Tracked&& o) noexcept { ++g.move_asg; need_alive(this); need_alive(&o); v = o.v; o.v = -1; return *this; }
    ~Tracked() { ++g.dtor; mark_dead(this); }
    friend bool operator==(const Tracked& a, const Tracked& b) { return a.v == b.v; }
};

struct MoveOnly {
    int v;
    explicit MoveOnly(int x) : v(x) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& o) noexcept : v(std::exchange(o.v, -1)) {}
    MoveOnly& operator=(MoveOnly&& o) noexcept { v = std::exchange(o.v, -1); return *this; }
};
struct NoDefault { int v; explicit NoDefault(int x) : v(x) {} };
struct ConstMember { const int v; };                     // 대입 불가
struct alignas(32) Over { int v; };                      // over-aligned

struct CountOnFull { static inline unsigned hits = 0; static void on_overflow() noexcept { ++hits; } };

static int value_of(int x) { return x; }
static int value_of(const Tracked& x) { return x.v; }
static int value_of(const MoveOnly& x) { return x.v; }

// 크기를 먼저 확인하므로 원소가 없어도 안전
template <typename V>
static bool eq(const V& v, std::initializer_list<int> want) {
    if (v.size() != want.size()) return false;
    auto it = v.begin();
    for (int w : want) { if (value_of(*it) != w) return false; ++it; }
    return true;
}
template <typename V>
static void fill(V& v, std::initializer_list<int> xs) { for (int x : xs) (void)v.emplace_back(x); }

// abort/assert 로 죽는지 자식 프로세스에서 확인. SIGABRT 를 잡아 _exit(42) (크래시 리포트 방지)
template <typename F>
static bool dies(F f) {
    const pid_t p = fork();
    if (p == 0) {
        signal(SIGABRT, [](int) { _exit(42); });
        close(2);                // assert 메시지가 테스트 출력에 섞이지 않게
        f();
        _exit(0);
    }
    int st = 0;
    waitpid(p, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 42;
}

using VI = static_vector<int, 4>;
using VT = static_vector<Tracked, 4>;
using VT5 = static_vector<Tracked, 5>;
using VM = static_vector<MoveOnly, 4>;
using VC = static_vector<ConstMember, 4>;

template <typename V, typename A> concept CanPushLvalue = requires(V& v, const A& a) { v.push_back(a); };
template <typename V> concept CanErase = requires(V& v) { v.erase(v.cbegin()); };
template <typename V, typename A> concept CanInsert = requires(V& v, A&& a) { v.insert(v.cbegin(), std::move(a)); };

static void test_part_a() {
    std::puts("-- Part A: 기본 동작 --");
    reset_all();
    {
        VT v;
        CHECK(v.size() == 0 && v.empty() && !v.full());
        CHECK(VT::capacity() == 4 && VT::max_size() == 4);
        CHECK(g.ctor == 0);                                 // 저장소만 있고 객체는 아직 없다
    }
    reset_all();
    {
        std::array<Tracked, 3> a;
        (void)a;
        CHECK(g.ctor == 3);                                 // 비교: array 는 쓰지 않아도 전부 생성
    }
    reset_all();
    {
        VT v;
        Tracked* p = v.emplace_back(7);
        CHECK(p != nullptr && p == v.data() && p->v == 7);
        CHECK(g.ctor == 1 && g.copy == 0 && g.move == 0);   // emplace_back 은 임시 객체 없이 제자리 생성
        Tracked t(8);
        reset_counts();
        CHECK(v.push_back(t));
        CHECK(g.copy == 1 && g.move == 0 && t.v == 8);
        reset_counts();
        CHECK(v.push_back(std::move(t)));
        CHECK(g.move == 1 && g.copy == 0 && t.v == -1);
        CHECK(eq(v, {7, 8, 8}) && v.size() == 3 && g.live == 4);
        reset_counts();
        v.pop_back();
        CHECK(g.dtor == 1 && eq(v, {7, 8}));
        if (v.size() == 2) CHECK(v.front().v == 7 && v.back().v == 8 && v[1].v == 8);
        else CHECK(false);
        CHECK(v.try_at(1) != nullptr && v.try_at(2) == nullptr);
        reset_counts();
        v.clear();
        CHECK(g.dtor == 2 && v.empty());
        CHECK(v.emplace_back(9) != nullptr && eq(v, {9}));  // 비운 뒤 재사용
    }
    CHECK(g.live == 0 && g.bad == 0);                       // 스코프 끝: 소멸자가 남은 원소를 정리

    {   // 소멸자가 살아 있는 원소만 소멸시키는지: 표로 확인
        struct Row { int n; };
        for (Row r : {Row{0}, Row{1}, Row{4}}) {
            reset_all();
            { VT v; for (int i = 0; i < r.n; ++i) (void)v.emplace_back(i); }
            CHECK(g.ctor == r.n && g.dtor == r.n && g.bad == 0);
        }
    }

    // 반복자
    VI v;
    fill(v, {1, 2, 3});
    int sum = 0;
    for (int x : v) sum += x;
    CHECK(sum == 6);
    CHECK(v.end() - v.begin() == static_cast<std::ptrdiff_t>(v.size()));
    CHECK(v.cbegin() == v.begin() && v.cend() == v.end());
    int rev[3] = {0, 0, 0}, i = 0;
    for (auto it = v.rbegin(); it != v.rend() && i < 3; ++it) rev[i++] = *it;
    CHECK(rev[0] == 3 && rev[1] == 2 && rev[2] == 1);
    CHECK(std::contiguous_iterator<VI::iterator>);
    CHECK(std::ranges::contiguous_range<VI>);
    CHECK(std::ranges::sized_range<VI>);
    std::span<int> s(v);                                    // 연속 + 크기 → span 으로 바로 (CPP-B11 참고)
    CHECK(s.size() == 3 && s.data() == v.data());
    std::sort(v.begin(), v.end(), [](int a, int b) { return a > b; });
    CHECK(eq(v, {3, 2, 1}));

    // push_back 은 재할당이 없으므로 기존 포인터가 그대로 유효하다
    VI w;
    (void)w.emplace_back(10);
    int* first = w.data();
    (void)w.emplace_back(20);
    (void)w.emplace_back(30);
    CHECK(w.size() == 3 && w.data() == first && *first == 10);
    (void)w.push_back(w.size() ? w.front() : 0);            // 자기 원소를 넣어도 안전
    CHECK(eq(w, {10, 20, 30, 10}));

    // 까다로운 T
    static_vector<NoDefault, 2> nd;
    CHECK(nd.emplace_back(3) != nullptr && nd.size() == 1);
    VM m;
    MoveOnly mo(5);
    CHECK(m.push_back(std::move(mo)) && mo.v == -1 && eq(m, {5}));
    VC c;
    CHECK(c.emplace_back(4) != nullptr && c.size() == 1);
    static_vector<Over, 3> ov;
    Over* o1 = ov.emplace_back(Over{1});
    Over* o2 = ov.emplace_back(Over{2});
    CHECK(o1 && o2 && reinterpret_cast<std::uintptr_t>(o1) % 32 == 0 && reinterpret_cast<std::uintptr_t>(o2) % 32 == 0);
    CHECK(alignof(static_vector<Over, 3>) == 32);

    // N == 0
    static_vector<int, 0> z;
    CHECK(z.empty() && z.full() && z.capacity() == 0);
    CHECK(z.emplace_back(1) == nullptr && z.begin() == z.end());
}

struct EraseRow { int pos; std::initializer_list<int> want; int asg; };
struct RangeRow { int first, last; std::initializer_list<int> want; int asg, dtor; };
struct InsertRow { int pos; std::initializer_list<int> want; int ctor, move, asg, dtor; };
struct AssignRow { int dst, src, asg, ctor, dtor; };

static void test_part_b_erase_insert() {
    std::puts("-- Part B: erase/insert 수명 --");
    const EraseRow erows[] = {
        {0, {2, 3, 4, 5}, 4}, {1, {1, 3, 4, 5}, 3}, {2, {1, 2, 4, 5}, 2}, {3, {1, 2, 3, 5}, 1}, {4, {1, 2, 3, 4}, 0},
    };
    for (const EraseRow& r : erows) {
        reset_all();
        {
            VT5 v;
            fill(v, {1, 2, 3, 4, 5});
            reset_counts();
            auto it = v.erase(v.cbegin() + r.pos);
            CHECK(eq(v, r.want) && it == v.begin() + r.pos);
            CHECK(g.move_asg == r.asg && g.dtor == 1 && g.move == 0 && g.copy == 0 && g.copy_asg == 0);
            CHECK(g.live == 4 && g.bad == 0);
        }
        CHECK(g.live == 0 && g.bad == 0);
    }

    const RangeRow rrows[] = {
        {0, 2, {3, 4, 5}, 3, 2}, {1, 3, {1, 4, 5}, 2, 2}, {3, 5, {1, 2, 3}, 0, 2},
        {2, 2, {1, 2, 3, 4, 5}, 0, 0}, {0, 5, {}, 0, 5}, {5, 5, {1, 2, 3, 4, 5}, 0, 0},
    };
    for (const RangeRow& r : rrows) {
        reset_all();
        {
            VT5 v;
            fill(v, {1, 2, 3, 4, 5});
            reset_counts();
            auto it = v.erase(v.cbegin() + r.first, v.cbegin() + r.last);
            CHECK(eq(v, r.want) && it == v.begin() + r.first);
            CHECK(g.move_asg == r.asg && g.dtor == r.dtor && g.bad == 0);
            CHECK(g.live == static_cast<int>(v.size()));
        }
        CHECK(g.live == 0 && g.bad == 0);
    }

    const InsertRow irows[] = {   // 크기 3 → pos 앞에 9 삽입. pos == 3 은 임시 객체 없는 emplace_back
        {0, {9, 1, 2, 3}, 1, 1, 3, 1}, {1, {1, 9, 2, 3}, 1, 1, 2, 1}, {2, {1, 2, 9, 3}, 1, 1, 1, 1}, {3, {1, 2, 3, 9}, 1, 0, 0, 0},
    };
    for (const InsertRow& r : irows) {
        reset_all();
        {
            VT5 v;
            fill(v, {1, 2, 3});
            reset_counts();
            auto it = v.emplace(v.cbegin() + r.pos, 9);
            CHECK(it != nullptr && it == v.begin() + r.pos && eq(v, r.want));
            CHECK(g.ctor == r.ctor && g.move == r.move && g.move_asg == r.asg && g.dtor == r.dtor);
            CHECK(g.bad == 0 && g.live == 4);
        }
        CHECK(g.live == 0 && g.bad == 0);
    }

    // 별칭(aliasing): 인자가 내 원소를 가리키는 삽입
    reset_all();
    {
        VT5 v;
        fill(v, {1, 2, 3});
        auto r0 = v.size() == 3 ? v.insert(v.cbegin(), v[2]) : nullptr;
        CHECK(r0 != nullptr && eq(v, {3, 1, 2, 3}));
        auto r1 = v.size() == 4 ? v.insert(v.cbegin() + 1, v[0]) : nullptr;
        CHECK(r1 != nullptr && eq(v, {3, 3, 1, 2, 3}));
        CHECK(g.bad == 0);
    }
    CHECK(g.live == 0 && g.bad == 0);

    // 이동 전용 타입의 삽입·삭제
    VM m;
    fill(m, {1, 2, 3});
    MoveOnly x(9);
    CHECK(m.insert(m.cbegin() + 1, std::move(x)) != nullptr && eq(m, {1, 9, 2, 3}) && x.v == -1);
    m.erase(m.cbegin());
    CHECK(eq(m, {9, 2, 3}));

    // erase-remove 관용구
    static_vector<int, 8> ev;
    fill(ev, {1, 2, 3, 4, 5, 6});
    ev.erase(std::remove_if(ev.begin(), ev.end(), [](int a) { return a % 2 == 0; }), ev.end());
    CHECK(eq(ev, {1, 3, 5}));
}

static void test_part_b_copy_move() {
    std::puts("-- Part B: 복사/이동 --");
    reset_all();
    {
        VT a;
        fill(a, {1, 2, 3});
        reset_counts();
        VT b(a);
        CHECK(eq(b, {1, 2, 3}) && eq(a, {1, 2, 3}) && g.copy == 3 && g.move == 0);
        reset_counts();
        VT c(std::move(a));                              // 인라인 저장소: 훔칠 포인터가 없어 원소별 이동
        CHECK(eq(c, {1, 2, 3}) && g.move == 3 && g.copy == 0);
        CHECK(a.size() == 3 && eq(a, {-1, -1, -1}));     // 원본의 size 는 유지, 원소는 moved-from
        CHECK(g.live == 9 && g.bad == 0);
    }
    CHECK(g.live == 0 && g.bad == 0);

    const AssignRow rows[] = {   // 대입 = min(dst, src), 생성 = 부족한 만큼, 소멸 = 남는 만큼
        {0, 3, 0, 3, 0}, {2, 3, 2, 1, 0}, {3, 3, 3, 0, 0}, {4, 2, 2, 0, 2}, {2, 0, 0, 0, 2},
    };
    for (const AssignRow& r : rows) {
        reset_all();
        {
            VT d, s;
            for (int i = 0; i < r.dst; ++i) (void)d.emplace_back(100 + i);
            for (int i = 0; i < r.src; ++i) (void)s.emplace_back(i);
            reset_counts();
            d = s;
            CHECK(g.copy_asg == r.asg && g.copy == r.ctor && g.dtor == r.dtor && g.bad == 0);
            CHECK(d.size() == static_cast<std::size_t>(r.src) && d == s);
        }
        CHECK(g.live == 0 && g.bad == 0);
    }
    for (const AssignRow& r : rows) {
        reset_all();
        {
            VT d, s;
            for (int i = 0; i < r.dst; ++i) (void)d.emplace_back(100 + i);
            for (int i = 0; i < r.src; ++i) (void)s.emplace_back(i);
            reset_counts();
            d = std::move(s);
            CHECK(g.move_asg == r.asg && g.move == r.ctor && g.dtor == r.dtor && g.bad == 0);
            CHECK(d.size() == static_cast<std::size_t>(r.src) && s.size() == static_cast<std::size_t>(r.src));
        }
        CHECK(g.live == 0 && g.bad == 0);
    }

    // 자기 대입
    reset_all();
    {
        VT v;
        fill(v, {1, 2});
        VT& alias = v;
        reset_counts();
        v = alias;
        CHECK(eq(v, {1, 2}) && g.copy_asg == 0 && g.bad == 0);
        v = std::move(alias);
        CHECK(eq(v, {1, 2}) && g.move_asg == 0 && g.bad == 0);
    }

    // trivial T: 복사가 비트 복사로 내려온다
    VI a;
    fill(a, {4, 5});
    VI b(a);
    CHECK(eq(b, {4, 5}));
    VI c;
    std::memcpy(static_cast<void*>(&c), static_cast<const void*>(&a), sizeof a);
    CHECK(eq(c, {4, 5}) && c == a);
    VI e;
    fill(e, {9, 9, 9});
    e = a;
    CHECK(eq(e, {4, 5}));
}

static void test_part_b_traits() {
    std::puts("-- Part B: 타입 특성 --");
    CHECK(std::is_trivially_destructible_v<VI>);
    CHECK(!std::is_trivially_destructible_v<VT>);
    CHECK(std::is_trivially_copy_constructible_v<VI>);
    CHECK(std::is_trivially_move_constructible_v<VI>);
    CHECK(std::is_trivially_copy_assignable_v<VI>);
    CHECK(std::is_trivially_move_assignable_v<VI>);
    CHECK(std::is_trivially_copyable_v<VI>);
    CHECK(!std::is_trivially_copyable_v<VT>);
    CHECK(!std::is_trivially_default_constructible_v<VI>);      // size_ = 0 은 해야 한다
    CHECK(std::is_nothrow_move_constructible_v<VT>);            // Tracked 의 이동이 noexcept 이므로

    CHECK(std::is_copy_constructible_v<VT> && std::is_copy_assignable_v<VT>);
    CHECK(!std::is_copy_constructible_v<VM>);                   // 이동 전용 T → 벡터도 이동 전용
    CHECK(!std::is_copy_assignable_v<VM>);
    CHECK(std::is_move_constructible_v<VM> && std::is_move_assignable_v<VM>);
    CHECK(!std::is_move_assignable_v<VC>);                      // 대입 불가 T → 벡터 대입도 불가
    CHECK(std::is_move_constructible_v<VC>);

    CHECK(!(CanPushLvalue<VM, MoveOnly>));
    CHECK((CanPushLvalue<VT, Tracked>));
    CHECK(!CanErase<VC>);
    CHECK(CanErase<VT>);
    CHECK(!(CanInsert<VC, ConstMember>));
    CHECK((CanInsert<VM, MoveOnly>));
    CHECK(!(std::is_constructible_v<static_vector<int, 8>, const static_vector<int, 4>&>));   // N 은 타입의 일부
}

static void test_part_b_overflow() {
    std::puts("-- Part B: 용량 초과 정책 --");
    using VP = static_vector<Tracked, 2, CountOnFull>;
    reset_all();
    CountOnFull::hits = 0;
    {
        VP v;
        fill(v, {1, 2});
        CHECK(CountOnFull::hits == 0 && v.full());
        reset_counts();
        CHECK(v.emplace_back(3) == nullptr && CountOnFull::hits == 1);
        Tracked t(4);
        reset_counts();
        CHECK(!v.push_back(t) && CountOnFull::hits == 2);
        CHECK(!v.push_back(std::move(t)) && CountOnFull::hits == 3);
        CHECK(t.v == 4 && g.copy == 0 && g.move == 0);          // 실패하면 인자를 건드리지 않는다
        CHECK(v.emplace(v.cbegin(), 5) == nullptr && CountOnFull::hits == 4);
        CHECK(g.ctor == 0);                                     // 실패한 emplace 는 임시 객체도 만들지 않는다
        CHECK(v.insert(v.cbegin() + 1, t) == nullptr && CountOnFull::hits == 5 && t.v == 4);
        CHECK(eq(v, {1, 2}) && g.bad == 0);
    }
    CHECK(g.live == 0 && g.bad == 0);

    CountOnFull::hits = 0;
    static_vector<int, 2, CountOnFull> il{1, 2, 3, 4};
    CHECK(eq(il, {1, 2}) && CountOnFull::hits == 1);            // 생성자는 첫 실패에서 멈춘다

    static_vector<int, 2> ret;
    fill(ret, {1, 2});
    CHECK(ret.emplace_back(3) == nullptr && eq(ret, {1, 2}));   // 기본 정책은 조용히 nullptr

    CHECK(dies([] { static_vector<int, 1, TrapOnFull> v; (void)v.emplace_back(1); (void)v.emplace_back(2); }));
    CHECK(!dies([] { static_vector<int, 1, TrapOnFull> v; (void)v.emplace_back(1); }));
    CHECK(dies([] { VI v; v.pop_back(); }));                    // 전제조건 위반 → assert
    CHECK(dies([] { VI v; fill(v, {1}); v.erase(v.cend()); })); // end() 는 지울 수 없다
}

static void test_part_c() {
    std::puts("-- Part C: CAN 송신 대기열 --");
    const auto mk = [](std::uint32_t id, std::uint32_t dl) { return TxFrame{id, dl, Lease(g_pool.acquire())}; };
    const auto ids_are = [](const auto& q, std::initializer_list<std::uint32_t> want) {
        if (q.items().size() != want.size()) return false;
        auto it = q.items().begin();
        for (std::uint32_t w : want) { if (it->id != w) return false; ++it; }
        return true;
    };

    g_pool = BufPool{};
    {
        CanTxQueue<4> q;
        TxFrame a = mk(0x200, 1000), b = mk(0x100, 1001), c = mk(0x300, 1002), d = mk(0x100, 1003);
        CHECK(g_pool.free_count() == 4);
        CHECK(q.enqueue(std::move(a)) && !a.buf.valid());
        CHECK(q.enqueue(std::move(b)));
        CHECK(q.enqueue(std::move(c)));
        CHECK(q.enqueue(std::move(d)));
        CHECK(ids_are(q, {0x100, 0x100, 0x200, 0x300}));
        CHECK(q.items().size() == 4 && q.items().begin()->deadline_ms == 1001);   // 같은 id 는 FIFO
        CHECK(g_pool.free_count() == 4);                                          // 큐가 소유권을 가져갔을 뿐

        TxFrame e = mk(0x050, 2000);
        CHECK(!q.enqueue(std::move(e)) && e.buf.valid());                         // 꽉 참: 소유권은 호출자에게
        CHECK(g_pool.free_count() == 3);
        e.buf.reset();
        CHECK(g_pool.free_count() == 4);

        TxFrame out = mk(0, 0);
        CHECK(g_pool.free_count() == 3);
        CHECK(q.pop_next(out) && out.id == 0x100 && out.deadline_ms == 1001);     // out 의 이전 버퍼는 대입에서 반납
        CHECK(out.buf.valid() && g_pool.free_count() == 4 && ids_are(q, {0x100, 0x200, 0x300}));
        out.buf.reset();
        CHECK(g_pool.free_count() == 5);
    }
    CHECK(g_pool.free_count() == 8 && g_pool.bad_release == 0);                   // 큐가 사라지면 남은 버퍼 전부 반납

    g_pool = BufPool{};
    {
        CanTxQueue<6> q;
        const std::uint32_t kNearWrap = 0xFFFF'FFF0u;
        const struct { std::uint32_t id, dl; } in[] = {
            {1, kNearWrap - 5}, {2, kNearWrap + 5}, {3, kNearWrap + 0x20u /*wrap: 0x10*/}, {4, kNearWrap}, {5, kNearWrap + 1},
        };
        for (const auto& r : in) { TxFrame f = mk(r.id, r.dl); CHECK(q.enqueue(std::move(f))); }
        CHECK(g_pool.free_count() == 3);
        CHECK(q.expire(kNearWrap) == 2);                     // id 1(지남), 4(딱 now)
        CHECK(ids_are(q, {2, 3, 5}));
        CHECK(g_pool.free_count() == 5 && g_pool.bad_release == 0);
        CHECK(q.expire(kNearWrap) == 0);
        CHECK(q.expire(kNearWrap + 0x40u) == 3 && q.items().empty());              // now 가 0 을 넘어 wrap
        CHECK(g_pool.free_count() == 8 && g_pool.bad_release == 0);
        TxFrame out = mk(9, 9);
        CHECK(!q.pop_next(out) && out.id == 9 && out.buf.valid());                 // 빈 큐: out 은 그대로
    }
    CHECK(g_pool.free_count() == 8 && g_pool.bad_release == 0);
}

static std::uint32_t xorshift32(std::uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

static void test_random_differential() {
    std::puts("-- 무작위 차분 테스트 (기준: std::vector, seed 고정) --");
    reset_all();
    std::uint32_t seed = 0x1234ABCDu;
    std::vector<int> model;
    int mismatch = 0, ops[6] = {0, 0, 0, 0, 0, 0};
    {
        static_vector<Tracked, 16> v;
        for (int step = 0; step < 20000; ++step) {
            const std::uint32_t r = xorshift32(seed);
            const int val = static_cast<int>((r >> 8) & 0xFFFF);
            const std::size_t n = model.size();
            const int op = static_cast<int>(r % 6);
            ++ops[op];
            switch (op) {
            case 0: case 1: {   // push (앞쪽 비중 2배)
                const bool ok = v.emplace_back(val) != nullptr;
                if (ok != (n < 16)) ++mismatch;
                if (n < 16) model.push_back(val);
                break;
            }
            case 2:
                if (n) { v.pop_back(); model.pop_back(); }
                break;
            case 3:
                if (n) {
                    const std::size_t i = (r >> 3) % n;
                    v.erase(v.cbegin() + static_cast<std::ptrdiff_t>(i));
                    model.erase(model.begin() + static_cast<std::ptrdiff_t>(i));
                }
                break;
            case 4: {
                const std::size_t i = (r >> 3) % (n + 1);
                const bool ok = v.emplace(v.cbegin() + static_cast<std::ptrdiff_t>(i), val) != nullptr;
                if (ok != (n < 16)) ++mismatch;
                if (n < 16) model.insert(model.begin() + static_cast<std::ptrdiff_t>(i), val);
                break;
            }
            default: {
                const std::size_t a = (r >> 3) % (n + 1);
                const std::size_t b = a + (r >> 11) % (n - a + 1);
                v.erase(v.cbegin() + static_cast<std::ptrdiff_t>(a), v.cbegin() + static_cast<std::ptrdiff_t>(b));
                model.erase(model.begin() + static_cast<std::ptrdiff_t>(a), model.begin() + static_cast<std::ptrdiff_t>(b));
                break;
            }
            }
            bool same = v.size() == model.size() && g.live == static_cast<int>(v.size());
            if (same) for (std::size_t i = 0; i < model.size(); ++i) same = same && (v.begin() + static_cast<std::ptrdiff_t>(i))->v == model[i];
            if (!same) ++mismatch;
        }
        std::printf("ops: push=%d pop=%d erase=%d insert=%d erase_range=%d, final size=%zu\n",
                    ops[0] + ops[1], ops[2], ops[3], ops[4], ops[5], model.size());
        CHECK(mismatch == 0);
        CHECK(g.bad == 0);
    }
    CHECK(g.live == 0 && g.bad == 0);
    CHECK(g.ctor + g.copy + g.move == g.dtor);
}

int main() {
    test_part_a();
    test_part_b_erase_insert();
    test_part_b_copy_move();
    test_part_b_traits();
    test_part_b_overflow();
    test_part_c();
    test_random_differential();
    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
