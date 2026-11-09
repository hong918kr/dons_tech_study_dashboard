// CPP-M07 demo_order — 프로그램이 끝날 때 전역은 어떤 순서로 소멸되는가 (호스트)
//   c++ -std=c++20 -O1 -fno-exceptions -fno-rtti demo_order.cpp -o /tmp/ord && /tmp/ord
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <utility>

struct Noisy {
    const char* name;
    explicit Noisy(const char* n) : name(n) { std::printf("  ctor %s\n", name); }
    ~Noisy() { std::printf("  dtor %s\n", name); }
};

// 소멸자가 없는 정적 인스턴스: 명시적으로 destroy 하지 않으면 소멸되지 않는다
template <typename T>
class Leaky {
    alignas(T) std::byte s_[sizeof(T)]{};
    T* p_ = nullptr;
public:
    constexpr Leaky() noexcept = default;
    template <typename... A> T* emplace(A&&... a) { return p_ = ::new (static_cast<void*>(s_)) T(std::forward<A>(a)...); }
};

Noisy g_first("global first");
Noisy g_second("global second");
constinit Leaky<Noisy> g_leaky;

static void at_exit_hook() { std::printf("  atexit hook\n"); }

Noisy& late() { static Noisy s("function-local (first call)"); return s; }

int main() {
    std::printf("main start\n");
    std::atexit(at_exit_hook);
    late();
    g_leaky.emplace("StaticInstance-style (never destroyed)");
    std::printf("main end\n");
    return 0;
}
