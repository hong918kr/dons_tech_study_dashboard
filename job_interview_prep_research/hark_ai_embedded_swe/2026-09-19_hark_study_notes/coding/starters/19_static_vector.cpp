/* 19_static_vector.cpp — 힙 없는 고정 용량 컨테이너 (starter)
 *
 *   make run N=19
 *
 * 아래 TODO 여섯 군데를 채우면 된다. 테스트(main 아래)와 Tracked 클래스는 건드리지 말 것.
 * 구현 전에는 첫 assert에서 멈추는 것이 정상이다.
 *
 * 목표
 *   - 저장소는 buf_ 하나뿐이다. new / malloc / std::vector 금지.
 *   - 원소 생성은 placement new (`::new (addr) T(...)`), 파괴는 `p->~T()`.
 *   - 파괴는 반드시 생성의 역순(뒤에서 앞으로).
 *   - 용량을 넘기면 생성자를 호출하지 않고 nullptr/false 를 돌려준다.
 *
 * 빌드: c++ -std=c++17 -Wall -Wextra -O2 -g -fno-exceptions -fno-rtti
 * 문제: problems/19_static_vector.md
 */

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>
#include <type_traits>
#include <utility>

/* ------------------------------------------------------------------ */
/* 1. static_vector<T, N> — TODO 1~6                                   */
/* ------------------------------------------------------------------ */

template <typename T, std::size_t N>
class static_vector {
    static_assert(N > 0u, "용량 0인 컨테이너는 의미가 없다");

public:
    using value_type = T;
    using size_type = std::size_t;

    static_vector() noexcept = default;

    /* TODO 1: 남아 있는 원소를 전부 파괴한다. (clear 를 쓰면 한 줄) */
    ~static_vector() {}

    /* 복사 금지 — 그대로 둔다. */
    static_vector(const static_vector &) = delete;
    static_vector &operator=(const static_vector &) = delete;

    /* TODO 5: 이동 생성 — other 의 원소를 하나씩 move 생성하고 other 를 비운다. */
    static_vector(static_vector &&other) noexcept { (void)other; }

    /* TODO 6: 이동 대입 — 자기 자신이면 아무것도 하지 않는다.
     *         그렇지 않으면 내 원소를 먼저 전부 파괴한 뒤 옮겨 온다. */
    static_vector &operator=(static_vector &&other) noexcept
    {
        (void)other;
        return *this;
    }

    /* --- 용량 (그대로 둔다) --- */
    static constexpr size_type capacity() noexcept { return N; }
    size_type size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0u; }
    bool full() const noexcept { return size_ == N; }

    /* --- 추가 --- */

    /* TODO 2: 용량이 남아 있으면 slot(size_) 위에 placement new 로 T 를 생성하고,
     *         size_ 를 늘린 뒤 새 원소의 주소를 돌려준다. 꽉 찼으면 nullptr.
     *         인자는 std::forward 로 그대로 전달한다(복사/이동이 생기지 않게). */
    template <typename... Args>
    T *emplace_back(Args &&...args)
    {
        ((void)args, ...);
        return nullptr;
    }

    bool push_back(const T &v) { return emplace_back(v) != nullptr; }
    bool push_back(T &&v) { return emplace_back(std::move(v)) != nullptr; }

    /* --- 제거 --- */

    /* TODO 3: 마지막 원소의 소멸자를 호출하고 size_ 를 줄인다. 비었으면 false. */
    bool pop_back() noexcept { return false; }

    /* TODO 4: 뒤에서 앞으로(생성의 역순) 전부 파괴한다. */
    void clear() noexcept {}

    /* --- 접근 (그대로 둔다) --- */
    T &operator[](size_type i) noexcept
    {
        assert(i < size_);
        return *at(i);
    }
    const T &operator[](size_type i) const noexcept
    {
        assert(i < size_);
        return *at(i);
    }
    T &back() noexcept
    {
        assert(size_ != 0u);
        return *at(size_ - 1u);
    }
    const T &back() const noexcept
    {
        assert(size_ != 0u);
        return *at(size_ - 1u);
    }

    T *data() noexcept { return at(0u); }
    const T *data() const noexcept { return at(0u); }
    T *begin() noexcept { return at(0u); }
    T *end() noexcept { return at(0u) + size_; }
    const T *begin() const noexcept { return at(0u); }
    const T *end() const noexcept { return at(0u) + size_; }

private:
    /* i번째 원소가 놓일 주소. placement new 에 넘길 void*. */
    void *slot(size_type i) noexcept { return static_cast<void *>(&buf_[i * sizeof(T)]); }

    /* std::launder: 같은 저장소에 새 객체가 놓였다는 사실을 컴파일러에 알린다. */
    T *at(size_type i) noexcept
    {
        return std::launder(reinterpret_cast<T *>(&buf_[i * sizeof(T)]));
    }
    const T *at(size_type i) const noexcept
    {
        return std::launder(reinterpret_cast<const T *>(&buf_[i * sizeof(T)]));
    }

    alignas(T) unsigned char buf_[N * sizeof(T)];
    size_type size_ = 0u;
};

/* ------------------------------------------------------------------ */
/* 2. 생성/파괴를 세는 원소 타입                                        */
/* ------------------------------------------------------------------ */

struct Stats {
    unsigned ctor;
    unsigned copy_ctor;
    unsigned move_ctor;
    unsigned dtor;
};

static Stats g_stats = {0u, 0u, 0u, 0u};

/* 파괴 순서를 확인하기 위한 로그 */
enum { LOG_MAX = 64 };
static int g_dtor_log[LOG_MAX];
static unsigned g_dtor_n = 0u;

class Tracked {
public:
    explicit Tracked(int id, std::uint32_t payload = 0u) noexcept : id_(id), payload_(payload)
    {
        ++g_stats.ctor;
    }
    Tracked(const Tracked &o) noexcept : id_(o.id_), payload_(o.payload_) { ++g_stats.copy_ctor; }
    Tracked(Tracked &&o) noexcept : id_(o.id_), payload_(o.payload_)
    {
        o.id_ = kMovedFrom; /* 이동 흔적을 남긴다 */
        o.payload_ = 0u;
        ++g_stats.move_ctor;
    }
    /* 대입은 쓰지 않는다. 컨테이너가 파괴-후-재생성만 하므로 필요가 없다. */
    Tracked &operator=(const Tracked &) = delete;
    Tracked &operator=(Tracked &&) = delete;

    ~Tracked()
    {
        if (g_dtor_n < static_cast<unsigned>(LOG_MAX)) {
            g_dtor_log[g_dtor_n++] = id_;
        }
        ++g_stats.dtor;
    }

    int id() const noexcept { return id_; }
    std::uint32_t payload() const noexcept { return payload_; }

    static const int kMovedFrom = -1;

private:
    int id_;
    std::uint32_t payload_;
};

static_assert(!std::is_trivially_destructible<Tracked>::value,
              "소멸자 호출을 확인하려면 trivial 하지 않아야 한다");

static void reset_counters()
{
    g_stats = Stats{0u, 0u, 0u, 0u};
    g_dtor_n = 0u;
}

static void expect_log(const int *ids, unsigned n)
{
    assert(g_dtor_n == n);
    for (unsigned i = 0u; i < n; ++i) {
        assert(g_dtor_log[i] == ids[i]);
    }
}

/* ------------------------------------------------------------------ */
/* 3. 테스트                                                           */
/* ------------------------------------------------------------------ */

static void test_empty_and_capacity(void)
{
    reset_counters();
    static_vector<Tracked, 4> v;
    assert(v.size() == 0u);
    assert(v.capacity() == 4u);
    assert(v.empty() && !v.full());
    assert(v.begin() == v.end());
    assert(g_stats.ctor == 0u); /* 저장소만 잡혀 있고 객체는 없다 */
    /* 저장소 정렬이 T의 요구를 만족한다 */
    assert(reinterpret_cast<std::uintptr_t>(v.data()) % alignof(Tracked) == 0u);
    puts("ok  1: 빈 컨테이너 — 객체는 0개, 저장소는 alignof(T) 정렬");
}

static void test_emplace_constructs_in_place(void)
{
    reset_counters();
    {
        static_vector<Tracked, 4> v;
        Tracked *p = v.emplace_back(10, 0xAAu);
        assert(p != nullptr);
        assert(v.size() == 1u);
        assert(v[0].id() == 10 && v[0].payload() == 0xAAu);
        assert(&v[0] == p);
        /* 임시 객체 없이 저장소 안에서 바로 생성됐다 */
        assert(g_stats.ctor == 1u && g_stats.copy_ctor == 0u && g_stats.move_ctor == 0u);
    }
    assert(g_stats.dtor == 1u);
    puts("ok  2: emplace_back 은 복사·이동 없이 저장소 안에서 직접 생성한다");
}

static void test_push_back_copy_vs_move(void)
{
    reset_counters();
    {
        static_vector<Tracked, 4> v;
        Tracked src(7, 0x77u);
        assert(g_stats.ctor == 1u);

        assert(v.push_back(src) == true); /* const& 오버로드 -> 복사 */
        assert(g_stats.copy_ctor == 1u && g_stats.move_ctor == 0u);
        assert(v[0].id() == 7 && src.id() == 7); /* 원본 그대로 */

        assert(v.push_back(std::move(src)) == true); /* && 오버로드 -> 이동 */
        assert(g_stats.move_ctor == 1u);
        assert(v[1].id() == 7);
        assert(src.id() == Tracked::kMovedFrom); /* 원본은 비워졌다 */

        assert(v.size() == 2u);
    }
    /* v의 2개 + 지역 src 1개 */
    assert(g_stats.dtor == 3u);
    puts("ok  3: push_back 의 const& / && 오버로드가 복사와 이동을 구분한다");
}

static void test_capacity_overflow(void)
{
    reset_counters();
    {
        static_vector<Tracked, 3> v;
        assert(v.emplace_back(1) != nullptr);
        assert(v.emplace_back(2) != nullptr);
        assert(v.emplace_back(3) != nullptr);
        assert(v.full());
        assert(g_stats.ctor == 3u);

        /* 용량 초과: nullptr을 주고 아무것도 바꾸지 않는다 */
        assert(v.emplace_back(4) == nullptr);
        assert(v.push_back(Tracked(5)) == false); /* 임시 1개는 생성/파괴된다 */
        assert(v.size() == 3u);
        assert(v[2].id() == 3); /* 마지막 원소가 덮이지 않았다 */

        /* 초과 호출로 T의 생성자가 추가로 돌지 않았다(임시 Tracked(5) 하나만) */
        assert(g_stats.ctor == 4u);
        assert(g_stats.move_ctor == 0u);
    }
    puts("ok  4: 용량 초과는 nullptr/false — 생성자 호출도, 덮어쓰기도 없다");
}

static void test_destruction_order_is_reverse(void)
{
    reset_counters();
    {
        static_vector<Tracked, 5> v;
        for (int i = 0; i < 5; ++i) {
            assert(v.emplace_back(i) != nullptr);
        }
        assert(g_stats.ctor == 5u && g_stats.dtor == 0u);
        v.clear();
        assert(v.size() == 0u);
        assert(g_stats.dtor == 5u);
    }
    {
        const int expected[5] = {4, 3, 2, 1, 0}; /* 생성의 역순 */
        expect_log(expected, 5u);
    }
    puts("ok  5: clear() 는 4,3,2,1,0 순서로 — 생성의 역순으로 파괴한다");
}

static void test_pop_back(void)
{
    reset_counters();
    static_vector<Tracked, 4> v;
    (void)v.emplace_back(100);
    (void)v.emplace_back(200);
    assert(v.back().id() == 200);

    assert(v.pop_back() == true);
    assert(g_stats.dtor == 1u && g_dtor_log[0] == 200);
    assert(v.size() == 1u);
    assert(v.back().id() == 100);

    assert(v.pop_back() == true);
    assert(v.empty());
    assert(v.pop_back() == false); /* 빈 컨테이너에서는 실패 */
    assert(g_stats.dtor == 2u);    /* 소멸자가 더 돌지 않았다 */
    puts("ok  6: pop_back 은 마지막 원소만 파괴하고, 빈 상태에서는 false");
}

static void test_container_destructor_cleans_up(void)
{
    reset_counters();
    {
        static_vector<Tracked, 8> v;
        for (int i = 0; i < 6; ++i) {
            (void)v.emplace_back(i * 10);
        }
        assert(g_stats.dtor == 0u); /* 아직 아무것도 파괴되지 않았다 */
    }
    /* 스코프를 벗어나며 소멸자가 6개를 전부, 역순으로 파괴했다 */
    assert(g_stats.dtor == 6u);
    const int expected[6] = {50, 40, 30, 20, 10, 0};
    expect_log(expected, 6u);
    puts("ok  7: 컨테이너 소멸자가 남은 6개를 역순으로 파괴한다 (누수 0)");
}

static void test_move_construct(void)
{
    reset_counters();
    {
        static_vector<Tracked, 4> a;
        (void)a.emplace_back(1);
        (void)a.emplace_back(2);

        static_vector<Tracked, 4> b(std::move(a));

        assert(b.size() == 2u);
        assert(b[0].id() == 1 && b[1].id() == 2);
        assert(a.size() == 0u); /* 원본은 비었다 */
        assert(g_stats.move_ctor == 2u);
        assert(g_stats.copy_ctor == 0u);
        /* 원본의 moved-from 원소 2개가 파괴됐다 */
        assert(g_stats.dtor == 2u);
        const int moved[2] = {Tracked::kMovedFrom, Tracked::kMovedFrom};
        expect_log(moved, 2u);
    }
    assert(g_stats.dtor == 4u); /* b의 2개까지 */
    puts("ok  8: 이동 생성 — 원소를 하나씩 move 하고 원본을 비운다 (O(N))");
}

static void test_move_assign_destroys_old(void)
{
    reset_counters();
    {
        static_vector<Tracked, 4> dst;
        (void)dst.emplace_back(900);
        (void)dst.emplace_back(901);

        static_vector<Tracked, 4> src;
        (void)src.emplace_back(1);

        dst = std::move(src);

        /* 기존 원소 901, 900 이 먼저 파괴되고(역순), 그 다음 src의 moved-from 1개 */
        const int expected[3] = {901, 900, Tracked::kMovedFrom};
        expect_log(expected, 3u);
        assert(dst.size() == 1u && dst[0].id() == 1);
        assert(src.size() == 0u);
    }
    /* 생성 3 + 이동생성 1 == 소멸 4 */
    assert(g_stats.ctor + g_stats.move_ctor + g_stats.copy_ctor == g_stats.dtor);
    puts("ok  9: 이동 대입은 대상의 기존 원소를 먼저 파괴한 뒤 옮긴다");
}

static void test_self_move_assign_is_safe(void)
{
    reset_counters();
    static_vector<Tracked, 4> v;
    (void)v.emplace_back(42);

    static_vector<Tracked, 4> &alias = v;
    v = std::move(alias); /* this == &other 경로 */

    assert(v.size() == 1u);
    assert(v[0].id() == 42);
    assert(g_stats.dtor == 0u);
    v.clear();
    puts("ok 10: 자기 자신에게 이동 대입해도 원소를 파괴하지 않는다");
}

static void test_trivial_element_type(void)
{
    static_vector<std::uint32_t, 4> v;
    assert(v.push_back(1u) && v.push_back(2u) && v.push_back(3u) && v.push_back(4u));
    assert(v.push_back(5u) == false);

    std::uint32_t sum = 0u;
    for (const std::uint32_t *p = v.begin(); p != v.end(); ++p) {
        sum += *p;
    }
    assert(sum == 10u);
    assert(sizeof(v) <= 4u * sizeof(std::uint32_t) + sizeof(std::size_t));
    puts("ok 11: trivial 타입에서도 동작하고, 크기는 저장소 + size 필드뿐이다");
}

static void test_iteration_and_indexing(void)
{
    reset_counters();
    static_vector<Tracked, 4> v;
    for (int i = 0; i < 4; ++i) {
        (void)v.emplace_back(i, static_cast<std::uint32_t>(i) * 2u);
    }
    int seen = 0;
    for (Tracked *p = v.begin(); p != v.end(); ++p) {
        assert(p->id() == seen);
        assert(p->payload() == static_cast<std::uint32_t>(seen) * 2u);
        ++seen;
    }
    assert(seen == 4);
    /* 원소들이 정말 연속으로, sizeof(T) 간격으로 놓여 있다 */
    assert(&v[1] == &v[0] + 1);
    assert(&v[3] == &v[0] + 3);
    v.clear();
    puts("ok 12: 원소가 sizeof(T) 간격으로 연속 배치되어 포인터 순회가 된다");
}

int main(void)
{
    printf("static_vector<Tracked,4>: sizeof=%zu (Tracked=%zu, align=%zu)\n",
           sizeof(static_vector<Tracked, 4>), sizeof(Tracked), alignof(Tracked));

    test_empty_and_capacity();
    test_emplace_constructs_in_place();
    test_push_back_copy_vs_move();
    test_capacity_overflow();
    test_destruction_order_is_reverse();
    test_pop_back();
    test_container_destructor_cleans_up();
    test_move_construct();
    test_move_assign_destroys_old();
    test_self_move_assign_is_safe();
    test_trivial_element_type();
    test_iteration_and_indexing();

    puts("ALL TESTS PASSED");
    return 0;
}
