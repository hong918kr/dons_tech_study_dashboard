// CPP-M07 demo_errors — 컴파일이 막혀야 하는 코드들
//   for n in 1 2 3 4 5 6; do c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=$n demo_errors.cpp; done
// SHOW_ERROR 를 주지 않으면 정상 컴파일된다.
#include <concepts>
#include <cstddef>
#include <memory>
#include <new>
#include <utility>

template <typename T>
class StaticInstance {
#if SHOW_ERROR == 4
    alignas(T) std::byte storage_[sizeof(T)];      // {} 없음 → constinit 불가
#else
    alignas(T) std::byte storage_[sizeof(T)]{};
#endif
    T* ptr_ = nullptr;
public:
    constexpr StaticInstance() noexcept = default;
    StaticInstance(const StaticInstance&) = delete;
    StaticInstance& operator=(const StaticInstance&) = delete;
    template <typename... A> requires std::constructible_from<T, A...>
    T* emplace(A&&... a) { return ptr_ = ::new (static_cast<void*>(storage_)) T(std::forward<A>(a)...); }
};

struct Drv { explicit Drv(int) {} };
struct ClassNew { static void* operator new(std::size_t) = delete; explicit ClassNew(int) {} };

constinit StaticInstance<Drv> g_drv;

struct C { int v; constexpr explicit C(int x) : v(x) {} constexpr ~C() { v = -1; } };

void examples() {
    alignas(ClassNew) static std::byte buf[sizeof(ClassNew)];
    (void)buf;
#if SHOW_ERROR == 1
    StaticInstance<Drv> copy = g_drv;              // 복사 금지
    (void)copy;
#elif SHOW_ERROR == 2
    g_drv.emplace("uart");                         // 맞는 생성자가 없음
#elif SHOW_ERROR == 3
    new (static_cast<void*>(buf)) ClassNew(1);     // :: 없음 → 클래스 범위 operator new 를 찾는다
#endif
}

#if SHOW_ERROR == 5
constexpr int twice() {
    std::allocator<C> a;
    C* p = a.allocate(1);
    std::construct_at(p, 1);
    std::destroy_at(p);
    std::destroy_at(p);                            // 이중 소멸 → 상수 평가 실패
    a.deallocate(p, 1);
    return 1;
}
constexpr int r = twice();
#elif SHOW_ERROR == 6
constexpr int leak() {
    std::allocator<C> a;
    C* p = a.allocate(1);
    std::construct_at(p, 1);                       // destroy/deallocate 누락
    return 1;
}
constexpr int r = leak();
#endif
int main() {}
