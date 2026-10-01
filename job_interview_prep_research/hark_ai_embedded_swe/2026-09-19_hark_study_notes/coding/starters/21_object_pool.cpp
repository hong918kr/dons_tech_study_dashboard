/* 21_object_pool.cpp — 고정 풀 + placement new (starter)
 *
 *   make run N=21
 *
 * 아래 TODO 다섯 군데를 채우면 된다. 테스트(main 아래)와 Session 클래스는 건드리지 말 것.
 * 구현 전에는 첫 assert에서 멈추는 것이 정상이다.
 *
 * 목표
 *   - 04_mem_pool.c 의 intrusive free list 를 그대로 쓰되, 빌려주는 것이
 *     raw 블록이 아니라 "생성이 끝난 T 객체"가 되게 만든다.
 *   - acquire 는 placement new 로 생성자를, release 는 `p->~T()` 로 소멸자를 호출한다.
 *   - 어떤 슬롯이 살아 있는지 추적해서, 풀이 파괴될 때 남은 객체의 소멸자를 돌린다.
 *   - double release 와 외부 포인터를 거부한다. 거부된 호출은 상태를 바꾸지 않는다.
 *
 * 빌드: c++ -std=c++17 -Wall -Wextra -O2 -g -fno-exceptions -fno-rtti
 * 문제: problems/21_object_pool.md
 */

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>
#include <type_traits>
#include <utility>

/* ------------------------------------------------------------------ */
/* 1. release 결과 코드 (그대로 둔다)                                  */
/* ------------------------------------------------------------------ */

enum class Release : int {
    Ok = 0,        /* 정상 반납. 소멸자가 호출됐다 */
    Foreign = -1,  /* 이 풀의 객체가 아니다 */
    DoubleFree = -2 /* 이미 반납된 슬롯이다 */
};

/* ------------------------------------------------------------------ */
/* 2. object_pool<T, N> — TODO 1~5                                     */
/* ------------------------------------------------------------------ */

template <typename T, std::size_t N>
class object_pool {
    static_assert(N > 0u, "용량 0인 풀은 의미가 없다");

    /* 자유 슬롯일 때는 next 링크, 사용 중일 때는 T의 저장소 (그대로 둔다). */
    union Slot {
        Slot *next;
        alignas(T) unsigned char storage[sizeof(T)];
    };

    static_assert(sizeof(Slot) >= sizeof(T), "슬롯이 T를 담아야 한다");
    static_assert(alignof(Slot) >= alignof(T), "슬롯 정렬이 T의 요구를 만족해야 한다");

public:
    using value_type = T;

    object_pool() noexcept
    {
        /* TODO 1: 슬롯 N개를 free list 로 엮는다.
         *   slots_[0] -> slots_[1] -> ... -> slots_[N-1] -> nullptr,
         *   free_head_ = &slots_[0], live_ 는 전부 false. */
        for (std::size_t i = 0u; i < N; ++i) {
            live_[i] = false;
        }
        free_head_ = nullptr;
    }

    ~object_pool() { destroy_all(); }

    /* 풀은 주소로 식별되는 자원이다. 복사·이동 전부 금지 (그대로 둔다). */
    object_pool(const object_pool &) = delete;
    object_pool &operator=(const object_pool &) = delete;
    object_pool(object_pool &&) = delete;
    object_pool &operator=(object_pool &&) = delete;

    /* TODO 2: free list 에서 슬롯 하나를 떼어 내고, 그 저장소 위에 placement new 로
     *         T 를 생성해서 돌려준다. 비어 있으면 nullptr (생성자를 호출하지 않는다).
     *         live_ / used_ / high_water_ 를 갱신한다. 반복문 금지(O(1)).
     *         주의: storage 를 덮기 전에 next 링크를 먼저 읽어 둬야 한다. */
    template <typename... Args>
    T *acquire(Args &&...args)
    {
        ((void)args, ...);
        return nullptr;
    }

    /* TODO 3: nullptr 은 Ok(no-op). 풀의 슬롯 시작 주소가 아니면 Foreign.
     *         이미 free 상태면 DoubleFree(상태를 하나도 바꾸지 않는다).
     *         그 외에는 소멸자를 호출하고 슬롯을 free list 앞에 다시 끼운다. O(1). */
    Release release(T *p)
    {
        (void)p;
        return Release::Foreign;
    }

    /* TODO 4: 살아 있는 객체를 인덱스 역순으로 전부 파괴하고 free list 에 되돌린다. */
    void destroy_all() noexcept {}

    /* --- 조회 (그대로 둔다) --- */
    static constexpr std::size_t capacity() noexcept { return N; }
    std::size_t used() const noexcept { return used_; }
    std::size_t available() const noexcept { return N - used_; }
    std::size_t high_water() const noexcept { return high_water_; }

    bool owns(const T *p) const noexcept { return index_of(p) != N; }

    T *live_at(std::size_t i) noexcept
    {
        if (i >= N || !live_[i]) {
            return nullptr;
        }
        return object_at(i);
    }

private:
    T *object_at(std::size_t i) noexcept
    {
        return std::launder(reinterpret_cast<T *>(slots_[i].storage));
    }

    std::size_t index_of_slot(const Slot *s) const noexcept
    {
        return static_cast<std::size_t>(s - &slots_[0]);
    }

    /* TODO 5: p 가 풀의 슬롯 시작 주소면 그 인덱스, 아니면 N 을 돌려준다.
     *         거부해야 하는 것: nullptr, 풀 범위 밖 주소, 슬롯 중간 주소.
     *         힌트: unsigned char* 로 캐스팅해 base 와의 거리를 sizeof(Slot) 으로
     *         나눈 나머지가 0인지 본다. */
    std::size_t index_of(const T *p) const noexcept
    {
        (void)p;
        return N;
    }

    Slot slots_[N];
    bool live_[N]; /* 비트맵으로 줄일 수 있다. N바이트 vs N비트의 교환 */
    Slot *free_head_ = nullptr;
    std::size_t used_ = 0u;
    std::size_t high_water_ = 0u;
};

/* ------------------------------------------------------------------ */
/* 3. 소멸자를 확인할 수 있는 객체                                      */
/* ------------------------------------------------------------------ */

/* 오디오 세션 하나. 생성자에서 "채널을 등록"하고 소멸자에서 "해제"한다고 하자.
 * 소멸자가 확실히 돌지 않으면 채널이 영구히 점유된다 — 이것이 raw 블록 풀과의
 * 결정적 차이다. */
static unsigned g_open_channels = 0u;
static unsigned g_ctor = 0u;
static unsigned g_copy = 0u;
static unsigned g_move = 0u;
static unsigned g_dtor = 0u;

enum { LOG_MAX = 32 };
static int g_dtor_log[LOG_MAX];
static unsigned g_dtor_n = 0u;

class Session {
public:
    explicit Session(int id, std::uint32_t rate = 48000u) noexcept : id_(id), rate_(rate)
    {
        ++g_ctor;
        ++g_open_channels;
    }
    Session(const Session &o) noexcept : id_(o.id_), rate_(o.rate_)
    {
        ++g_copy;
        ++g_open_channels;
    }
    Session(Session &&o) noexcept : id_(o.id_), rate_(o.rate_)
    {
        o.id_ = -1; /* moved-from 표시. 채널 수는 그대로 옮겨 온 것 */
        ++g_move;
        ++g_open_channels;
    }
    Session &operator=(const Session &) = delete;
    Session &operator=(Session &&) = delete;

    ~Session()
    {
        if (g_dtor_n < static_cast<unsigned>(LOG_MAX)) {
            g_dtor_log[g_dtor_n++] = id_;
        }
        ++g_dtor;
        assert(g_open_channels > 0u);
        --g_open_channels; /* 소멸자가 빠지면 이 값이 0으로 돌아오지 않는다 */
    }

    int id() const noexcept { return id_; }
    std::uint32_t rate() const noexcept { return rate_; }
    void set_rate(std::uint32_t r) noexcept { rate_ = r; }

private:
    int id_;
    std::uint32_t rate_;
};

static_assert(!std::is_trivially_destructible<Session>::value,
              "소멸자 호출을 세려면 trivial 하지 않아야 한다");

static void reset_counters()
{
    g_open_channels = 0u;
    g_ctor = 0u;
    g_copy = 0u;
    g_move = 0u;
    g_dtor = 0u;
    g_dtor_n = 0u;
}

/* ------------------------------------------------------------------ */
/* 4. 테스트                                                           */
/* ------------------------------------------------------------------ */

static const std::size_t kCap = 4u;
using Pool = object_pool<Session, kCap>;

static void test_initial_state(void)
{
    reset_counters();
    Pool pool;
    assert(pool.capacity() == 4u);
    assert(pool.used() == 0u);
    assert(pool.available() == 4u);
    assert(pool.high_water() == 0u);
    assert(g_ctor == 0u); /* 저장소는 있지만 객체는 없다 */
    assert(g_open_channels == 0u);
    puts("ok  1: 풀 생성만으로는 객체가 만들어지지 않는다 (저장소만 확보)");
}

static void test_acquire_constructs(void)
{
    reset_counters();
    {
        Pool pool;
        Session *a = pool.acquire(11, 44100u);
        assert(a != nullptr);
        assert(a->id() == 11 && a->rate() == 44100u);
        assert(g_ctor == 1u && g_copy == 0u && g_move == 0u);
        assert(g_open_channels == 1u);
        assert(pool.used() == 1u && pool.available() == 3u && pool.high_water() == 1u);
        assert(pool.owns(a));
        /* 반환 주소가 T의 정렬 요구를 만족한다 */
        assert(reinterpret_cast<std::uintptr_t>(a) % alignof(Session) == 0u);
    }
    assert(g_dtor == 1u && g_open_channels == 0u);
    puts("ok  2: acquire 는 인자를 그대로 넘겨 슬롯 안에서 생성자를 호출한다");
}

static void test_release_calls_destructor(void)
{
    reset_counters();
    Pool pool;
    Session *a = pool.acquire(1);
    assert(g_dtor == 0u);

    assert(pool.release(a) == Release::Ok);
    assert(g_dtor == 1u);          /* 바로 소멸자가 돌았다 */
    assert(g_dtor_log[0] == 1);
    assert(g_open_channels == 0u); /* 채널이 해제됐다 */
    assert(pool.used() == 0u && pool.available() == 4u);
    assert(pool.high_water() == 1u); /* high_water 는 내려가지 않는다 */

    assert(pool.release(nullptr) == Release::Ok); /* no-op */
    assert(g_dtor == 1u);
    puts("ok  3: release 가 소멸자를 정확히 1번 호출하고 슬롯을 되돌린다");
}

static void test_exhaustion(void)
{
    reset_counters();
    Pool pool;
    Session *p[kCap];
    for (std::size_t i = 0u; i < kCap; ++i) {
        p[i] = pool.acquire(static_cast<int>(i));
        assert(p[i] != nullptr);
    }
    assert(pool.used() == 4u && pool.available() == 0u);
    assert(g_ctor == 4u);

    /* 고갈: nullptr 이고 생성자는 호출되지 않는다 */
    assert(pool.acquire(99) == nullptr);
    assert(g_ctor == 4u);
    assert(pool.used() == 4u);
    assert(pool.high_water() == 4u);

    /* 하나 반납하면 다시 배급된다 */
    assert(pool.release(p[2]) == Release::Ok);
    Session *q = pool.acquire(99);
    assert(q != nullptr);
    assert(q == p[2]); /* LIFO: 방금 반납한 슬롯이 그대로 나온다 */
    assert(q->id() == 99);
    (void)pool.release(q);
    for (std::size_t i = 0u; i < kCap; ++i) {
        if (i != 2u) {
            (void)pool.release(p[i]);
        }
    }
    assert(g_open_channels == 0u);
    puts("ok  4: 용량 초과는 nullptr(생성자 미호출), 반납 뒤에는 LIFO로 재사용");
}

static void test_double_release(void)
{
    reset_counters();
    Pool pool;
    Session *a = pool.acquire(5);
    assert(pool.release(a) == Release::Ok);
    assert(g_dtor == 1u);

    /* 두 번째 반납은 거부된다. 소멸자가 두 번 돌면 채널 카운터가 음수가 된다. */
    assert(pool.release(a) == Release::DoubleFree);
    assert(g_dtor == 1u);
    assert(pool.used() == 0u);
    assert(pool.available() == 4u); /* free list 에 같은 슬롯이 두 번 들어가지 않았다 */

    /* free list 가 망가지지 않았는지: 용량만큼 다시 배급되는지로 확인 */
    Session *p[kCap];
    for (std::size_t i = 0u; i < kCap; ++i) {
        p[i] = pool.acquire(static_cast<int>(i));
        assert(p[i] != nullptr);
    }
    for (std::size_t i = 0u; i < kCap; ++i) {
        for (std::size_t j = i + 1u; j < kCap; ++j) {
            assert(p[i] != p[j]); /* 같은 슬롯을 두 번 배급하지 않았다 */
        }
    }
    puts("ok  5: double release 는 -2로 거부, free list 와 카운터가 그대로다");
}

static void test_foreign_pointer(void)
{
    reset_counters();
    Pool pool;
    Session *a = pool.acquire(1);

    /* 스택 객체 */
    Session stack_obj(777);
    assert(pool.owns(&stack_obj) == false);
    assert(pool.release(&stack_obj) == Release::Foreign);

    /* 슬롯 중간 주소 */
    unsigned char *mid = reinterpret_cast<unsigned char *>(a) + 1;
    assert(pool.release(reinterpret_cast<Session *>(mid)) == Release::Foreign);

    /* 거부된 호출은 아무 상태도 바꾸지 않았다 */
    assert(g_dtor == 0u);
    assert(pool.used() == 1u);
    assert(a->id() == 1);

    /* one-past-end 주소: 마지막 슬롯 바로 뒤 */
    Session *rest[kCap - 1u];
    for (std::size_t i = 0u; i + 1u < kCap; ++i) {
        rest[i] = pool.acquire(static_cast<int>(100 + i));
        assert(rest[i] != nullptr);
    }
    const std::uintptr_t stride = reinterpret_cast<std::uintptr_t>(rest[0]) -
                                  reinterpret_cast<std::uintptr_t>(a);
    unsigned char *one_past = reinterpret_cast<unsigned char *>(rest[kCap - 2u]) + stride;
    assert(pool.release(reinterpret_cast<Session *>(one_past)) == Release::Foreign);
    for (std::size_t i = 0u; i + 1u < kCap; ++i) {
        assert(pool.release(rest[i]) == Release::Ok);
    }

    assert(pool.release(a) == Release::Ok);
    puts("ok  6: 외부·중간·one-past-end 주소는 모두 -1로 거부 (소멸자 미호출)");
}

static void test_pool_destructor_cleans_up(void)
{
    reset_counters();
    {
        Pool pool;
        (void)pool.acquire(10);
        (void)pool.acquire(20);
        (void)pool.acquire(30); /* 세 개를 반납하지 않고 스코프를 벗어난다 */
        assert(g_open_channels == 3u);
        assert(g_dtor == 0u);
    }
    /* 풀 소멸자가 남은 3개의 소멸자를 대신 돌렸다 */
    assert(g_dtor == 3u);
    assert(g_open_channels == 0u);
    assert(g_dtor_n == 3u);
    assert(g_dtor_log[0] == 30 && g_dtor_log[1] == 20 && g_dtor_log[2] == 10);
    puts("ok  7: 풀 소멸자가 반납되지 않은 3개를 인덱스 역순으로 파괴한다");
}

static void test_move_into_pool(void)
{
    reset_counters();
    {
        Pool pool;
        Session tmp(42, 96000u);
        assert(g_ctor == 1u && g_open_channels == 1u);

        Session *moved = pool.acquire(std::move(tmp)); /* 이동 생성자 경로 */
        assert(moved != nullptr);
        assert(g_move == 1u && g_copy == 0u);
        assert(moved->id() == 42 && moved->rate() == 96000u);
        assert(tmp.id() == -1); /* 원본은 비워졌다 */

        Session src(7);
        Session *copied = pool.acquire(src); /* 복사 생성자 경로 */
        assert(copied != nullptr);
        assert(g_copy == 1u);
        assert(src.id() == 7); /* 원본 유지 */
        assert(copied->id() == 7);

        assert(g_open_channels == 4u); /* tmp, src, 풀 안의 2개 */
    }
    /* 생성(모든 종류) 횟수 == 소멸 횟수 */
    assert(g_ctor + g_copy + g_move == g_dtor);
    assert(g_open_channels == 0u);
    puts("ok  8: acquire 가 이동/복사 생성자를 올바르게 골라 호출한다");
}

static void test_objects_do_not_overlap(void)
{
    reset_counters();
    Pool pool;
    Session *p[kCap];
    for (std::size_t i = 0u; i < kCap; ++i) {
        p[i] = pool.acquire(static_cast<int>(i), 1000u * static_cast<std::uint32_t>(i));
    }
    for (std::size_t i = 0u; i < kCap; ++i) {
        p[i]->set_rate(0xAAAA0000u + static_cast<std::uint32_t>(i));
    }
    for (std::size_t i = 0u; i < kCap; ++i) {
        assert(p[i]->id() == static_cast<int>(i));
        assert(p[i]->rate() == 0xAAAA0000u + static_cast<std::uint32_t>(i));
        assert(pool.live_at(i) == p[i]);
    }
    pool.destroy_all();
    assert(pool.used() == 0u);
    assert(g_dtor == kCap);
    assert(g_open_channels == 0u);
    puts("ok  9: 객체들이 서로 침범하지 않고, live_at 로 순회할 수 있다");
}

static void test_cycles_do_not_leak(void)
{
    reset_counters();
    {
        Pool pool;
        for (unsigned round = 0u; round < 100u; ++round) {
            Session *p[kCap];
            for (std::size_t i = 0u; i < kCap; ++i) {
                p[i] = pool.acquire(static_cast<int>(round * 10u + i));
                assert(p[i] != nullptr);
            }
            assert(pool.available() == 0u);
            for (std::size_t i = 0u; i < kCap; ++i) {
                assert(pool.release(p[i]) == Release::Ok);
            }
            assert(pool.available() == kCap); /* 단편화 0 */
        }
        assert(pool.used() == 0u);
        assert(pool.high_water() == kCap);
    }
    assert(g_ctor == 100u * kCap);
    assert(g_dtor == g_ctor);      /* 생성 == 소멸 */
    assert(g_open_channels == 0u); /* 자원 누수 0 */
    puts("ok 10: 100바퀴 acquire/release 후 생성 횟수 == 소멸 횟수, 누수 0");
}

int main(void)
{
    printf("object_pool<Session,4>: sizeof=%zu, Session=%zu, 슬롯당 오버헤드=%zu바이트\n",
           sizeof(Pool), sizeof(Session),
           (sizeof(Pool) - (kCap * sizeof(Session))) / kCap);

    test_initial_state();
    test_acquire_constructs();
    test_release_calls_destructor();
    test_exhaustion();
    test_double_release();
    test_foreign_pointer();
    test_pool_destructor_cleans_up();
    test_move_into_pool();
    test_objects_do_not_overlap();
    test_cycles_do_not_leak();

    puts("ALL TESTS PASSED");
    return 0;
}
