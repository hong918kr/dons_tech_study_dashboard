// CPP-M12 demo: std::function vs Delegate — sizeof + 전역 operator new 치환으로 할당 횟수 세기
//   "malloc 훅"의 이식 가능한 대체: 전역 operator new/delete 는 표준이 허용하는 치환 지점이다 [replacement.functions]
//   build (Mac libc++):  c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_alloc.cpp -o demo_alloc && ./demo_alloc
//   build (VM libstdc++): g++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_alloc.cpp -o demo_alloc && ./demo_alloc
#define main solution_main
#include "solution.cpp"
#undef main
#include <array>
#include <cstdlib>
#include <new>

static std::size_t g_new, g_del, g_bytes;

void* operator new(std::size_t n) {
    ++g_new; g_bytes += n;
    void* p = std::malloc(n ? n : 1);
    if (p == nullptr) std::abort();
    return p;
}
void operator delete(void* p) noexcept { ++g_del; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ++g_del; std::free(p); }

struct Counts { std::size_t ctor, call, copy, move, bytes; };

template <typename F>
__attribute__((noinline)) Counts measure(F fn) {
    Counts c{};
    g_new = g_del = g_bytes = 0;
    std::function<int(int)> f = fn;
    c.ctor = g_new; c.bytes = g_bytes;
    volatile int sink = 0;
    for (int i = 0; i < 1000; ++i) sink = f(i);
    (void)sink;
    c.call = g_new - c.ctor;
    std::size_t before = g_new;
    { auto g = f; (void)g; }
    c.copy = g_new - before;
    before = g_new;
    { auto h = std::move(f); (void)h; }
    c.move = g_new - before;
    return c;
}

static void row(const char* name, std::size_t cap, Counts c) {
    std::printf("%-30s %5zu %6zu %6zu %6zu %6zu %7zu\n", name, cap, c.ctor, c.call, c.copy, c.move, c.bytes);
}

template <std::size_t N>
static void probe() {
    std::array<std::uint8_t, N> blob{};
    auto l = [blob](int x) { return x + blob[0]; };
    char name[32];
    std::snprintf(name, sizeof name, "capture %zu bytes (trivial)", N);
    row(name, sizeof l, measure(l));
}

struct CopyThrows   { std::uint64_t v = 0; CopyThrows() = default; CopyThrows(const CopyThrows& o) : v{o.v} {} };
struct CopyNoexcept { std::uint64_t v = 0; CopyNoexcept() = default; CopyNoexcept(const CopyNoexcept& o) noexcept : v{o.v} {} };

int main() {
#if defined(_LIBCPP_VERSION)
    std::printf("stdlib: libc++ %d\n", _LIBCPP_VERSION);
#elif defined(__GLIBCXX__)
    std::printf("stdlib: libstdc++ %d\n", __GLIBCXX__);
#endif
    std::printf("sizeof(void*)                 = %zu\n", sizeof(void*));
    std::printf("sizeof(std::function<int(int)>) = %zu\n", sizeof(std::function<int(int)>));
    std::printf("sizeof(Delegate<int(int)>)    = %zu\n", sizeof(Delegate<int(int)>));
    std::printf("sizeof(FunctionRef<int(int)>) = %zu\n", sizeof(FunctionRef<int(int)>));
    std::printf("sizeof(int (Counter::*)(int)) = %zu  (멤버 함수 포인터 자체)\n\n", sizeof(int (Counter::*)(int)));

    std::printf("%-30s %5s %6s %6s %6s %6s %7s\n", "callable in std::function", "size", "ctor", "call", "copy", "move", "bytes");
    row("function pointer", sizeof(int (*)(int)), measure(&twice));
    row("captureless lambda", 1, measure([](int x) { return x; }));
    probe<8>(); probe<16>(); probe<24>(); probe<32>(); probe<48>();
    CopyThrows ct; CopyNoexcept cn;
    auto l1 = [ct](int x) { return x + static_cast<int>(ct.v); };
    auto l2 = [cn](int x) { return x + static_cast<int>(cn.v); };
    row("8B capture, copy not noexcept", sizeof l1, measure(l1));
    row("8B capture, copy noexcept", sizeof l2, measure(l2));

    Counter c;
    auto d = Delegate<int(int)>::bind<&Counter::add>(c);
    row("Delegate stored in function", sizeof d, measure(d));

    // Delegate 자체: 생성·호출·복사 전 구간 operator new 0회
    g_new = g_del = 0;
    auto d2 = Delegate<int(int)>::bind<&Counter::add>(c);
    volatile int sink = 0;
    for (int i = 0; i < 1000; ++i) sink = d2(i);
    (void)sink;
    auto d3 = d2; (void)d3;
    std::printf("\nDelegate alone: operator new calls = %zu, delete calls = %zu\n", g_new, g_del);
    return 0;
}
