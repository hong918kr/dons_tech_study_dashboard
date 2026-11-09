// CPP-M08 추가 연습 — 한 번에 검증
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>

// 본문 풀을 짧게 줄인 판 (임계 구역·포이즌 생략, 이 연습의 주제가 아니다)
template <std::size_t BlockSize, std::size_t BlockAlign, std::uint16_t N>
class StaticPool {
    static_assert(BlockSize % BlockAlign == 0);
    static constexpr std::uint16_t kNil = 0xFFFFu;
public:
    constexpr StaticPool() = default;
    void* alloc() noexcept {
        std::uint16_t i;
        if (head_ != kNil)   { i = head_; head_ = next_[i]; }
        else if (fresh_ < N) { i = fresh_++; }
        else return nullptr;
        ++used_;
        return &storage_[i * BlockSize];
    }
    void release(void* p) noexcept {
        if (!contains(p)) return;
        const auto i = static_cast<std::uint16_t>(
            (reinterpret_cast<std::uintptr_t>(p) - reinterpret_cast<std::uintptr_t>(storage_)) / BlockSize);
        next_[i] = head_; head_ = i; --used_;
    }
    bool contains(const void* p) const noexcept {
        const auto a = reinterpret_cast<std::uintptr_t>(p), b = reinterpret_cast<std::uintptr_t>(storage_);
        return a >= b && a - b < N * BlockSize;
    }
    std::uint16_t used() const noexcept { return used_; }
private:
    alignas(BlockAlign) std::byte storage_[N * BlockSize]{};
    std::uint16_t next_[N]{};
    std::uint16_t head_ = kNil, fresh_ = 0, used_ = 0;
};

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

// 연습 1 — 재사용 믹스인: 타입마다 자기 풀
//   base 절 시점에는 T 가 불완전 타입이라 sizeof(T) 를 클래스 멤버 선언에 못 쓴다 → 함수 안의 지역 static 으로 미룬다.
//   constinit 지역 static 은 상수 초기화라서 guard(__cxa_guard_acquire)도 필요 없다.
template <class T, std::uint16_t N>
struct PoolAllocated {
    static auto& pool() noexcept {
        static constinit StaticPool<sizeof(T), alignof(T), N> p;
        return p;
    }
    static void* operator new(std::size_t sz) noexcept { return sz == sizeof(T) ? pool().alloc() : nullptr; }
    static void  operator delete(void* p, std::size_t) noexcept { pool().release(p); }
    static void* operator new[](std::size_t) = delete;
    static void  operator delete[](void*)    = delete;
};
struct Frame : PoolAllocated<Frame, 2> { std::uint32_t words[4]{}; };
struct Event : PoolAllocated<Event, 3> { std::uint8_t  code = 0; };
struct BiggerFrame : Frame { std::uint32_t more = 0; };

// 연습 2 — 과정렬(over-aligned) 타입: 풀 저장소의 alignas 가 책임진다
struct alignas(32) DmaDesc : PoolAllocated<DmaDesc, 3> { std::uint32_t ctrl = 0; };
static_assert(alignof(DmaDesc) > __STDCPP_DEFAULT_NEW_ALIGNMENT__, "이 타입은 new-extended alignment 여야 연습의 의미가 있다");

// 연습 3 — 우선순위 예비 풀: 클래스 전용 placement 인자
enum class Prio { Normal, High };
inline constinit StaticPool<16, 8, 2> g_normal;
inline constinit StaticPool<16, 8, 1> g_reserve;
struct Packet {
    std::uint64_t a = 0, b = 0;
    static void* operator new(std::size_t, Prio pr) noexcept {
        void* p = g_normal.alloc();
        if (p == nullptr && pr == Prio::High) p = g_reserve.alloc();
        return p;
    }
    // 생성자가 예외를 던질 때만 불리는 짝. 예외를 안 쓰면 없어도 되지만 보통 같이 둔다.
    static void operator delete(void* p, Prio) noexcept { release(p); }
    // delete 식이 찾는 일반 해제 함수: 어느 풀인지는 포인터 범위로 안다
    static void operator delete(void* p) noexcept { release(p); }
    static void release(void* p) noexcept {
        if (g_normal.contains(p)) g_normal.release(p);
        else if (g_reserve.contains(p)) g_reserve.release(p);
    }
};
template <class T> constexpr bool can_plain_new = requires { new T; };

// 연습 4 — std::make_unique / unique_ptr 는 클래스 operator new/delete 를 탄다
struct Job : PoolAllocated<Job, 2> {
    int v;
    explicit Job(int x) noexcept : v(x) {}
};

// 연습 5 — 정적 초기화 순서: 풀이 constinit 이면 다른 전역의 초기화 안에서 new 해도 안전 (CPP-B09 참고)
struct Early : PoolAllocated<Early, 2> { int x = 42; };
Early* g_early = new Early;

int main() {
    // 연습 5 (다른 테스트보다 먼저)
    CHECK(g_early != nullptr && Early::pool().contains(g_early) && g_early->x == 42);
    CHECK(Early::pool().used() == 1u);
    delete g_early;
    CHECK(Early::pool().used() == 0u);

    // 연습 1
    auto* f1 = new Frame;
    auto* f2 = new Frame;
    auto* f3 = new Frame;
    CHECK(f1 && f2 && f3 == nullptr);
    auto* e1 = new Event;                      // Frame 이 고갈돼도 Event 풀은 별개
    CHECK(e1 != nullptr && Event::pool().used() == 1u && Frame::pool().used() == 2u);
    CHECK(Frame::pool().contains(f1) && !Event::pool().contains(f1));
    auto* bf = new BiggerFrame;                // sz != sizeof(Frame) → 거절
    CHECK(bf == nullptr);
    delete f1; delete f2; delete e1;
    CHECK(Frame::pool().used() == 0u && Event::pool().used() == 0u);

    // 연습 2
    DmaDesc* d[3] = {new DmaDesc, new DmaDesc, new DmaDesc};
    bool aligned = true;
    for (auto* p : d) aligned = aligned && p != nullptr && reinterpret_cast<std::uintptr_t>(p) % 32 == 0;
    CHECK(aligned);
    CHECK(sizeof(DmaDesc) == 32u && DmaDesc::pool().used() == 3u);
    CHECK(new DmaDesc == nullptr);
    for (auto* p : d) delete p;
    CHECK(DmaDesc::pool().used() == 0u);

    // 연습 3
    CHECK(!can_plain_new<Packet>);             // placement 형만 선언 → new Packet 은 컴파일 에러
    Packet* p1 = new (Prio::Normal) Packet;
    Packet* p2 = new (Prio::Normal) Packet;
    Packet* p3 = new (Prio::Normal) Packet;    // normal 고갈 → 예비는 건드리지 않음
    CHECK(p1 && p2 && p3 == nullptr);
    CHECK(g_reserve.used() == 0u);
    Packet* p4 = new (Prio::High) Packet;      // High 는 예비까지 쓴다
    CHECK(p4 != nullptr && g_reserve.contains(p4));
    Packet* p5 = new (Prio::High) Packet;      // 예비도 고갈
    CHECK(p5 == nullptr);
    delete p4;                                 // 포인터 범위로 예비 풀에 반납
    CHECK(g_reserve.used() == 0u && g_normal.used() == 2u);
    delete p1; delete p2;
    CHECK(g_normal.used() == 0u);

    // 연습 4
    CHECK(sizeof(std::unique_ptr<Job>) == sizeof(Job*));
    {
        auto j1 = std::make_unique<Job>(7);
        auto j2 = std::make_unique<Job>(8);
        CHECK(j1 && j2 && j1->v == 7 && j2->v == 8);
        CHECK(Job::pool().contains(j1.get()) && Job::pool().used() == 2u);
        auto j3 = std::make_unique<Job>(9);    // 고갈 → 빈 unique_ptr
        CHECK(!j3);
    }
    CHECK(Job::pool().used() == 0u);           // 스코프 끝에서 풀로 반납

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
