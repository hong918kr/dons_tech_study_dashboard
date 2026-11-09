// CPP-M09 demo_relocate — vector 재배치에서 noexcept 가 실제로 바꾸는 것을 라이브러리·플래그별로 찍는다 (호스트 전용)
// run: bash run_matrix.sh   (맥 libc++ 2회 + Lima VM libstdc++ 2회)
#include <cstdio>
#include <type_traits>
#include <utility>
#include <vector>

static unsigned g_copies, g_moves;

template <bool NX>
struct P {
    int v = 0;
    P() = default;
    explicit P(int x) : v(x) {}
    P(const P& o) : v(o.v) { ++g_copies; }
    P(P&& o) noexcept(NX) : v(o.v) { ++g_moves; }
    P& operator=(const P&) = default;
    P& operator=(P&&) = default;
};

template <class T>
void run(const char* name) {
    std::vector<T> v;
    v.reserve(8);
    for (int i = 0; i < 8; ++i) v.emplace_back(i);
    g_copies = g_moves = 0;
    v.reserve(16);                                  // 재배치 정확히 1회, 요소 8개
    std::printf("  %-9s is_nothrow_move=%d  move_if_noexcept=%-7s reserve(8->16): copies=%u moves=%u\n", name,
                int(std::is_nothrow_move_constructible_v<T>),
                std::is_rvalue_reference_v<decltype(std::move_if_noexcept(std::declval<T&>()))> ? "T&&" : "const&",
                g_copies, g_moves);
}

int main() {
#if defined(_LIBCPP_VERSION)
    std::printf("library=libc++ %d", _LIBCPP_VERSION);
#elif defined(__GLIBCXX__)
    std::printf("library=libstdc++ %d", __GLIBCXX__);
#endif
#ifdef __cpp_exceptions
    std::puts("  exceptions=ON");
#else
    std::puts("  exceptions=OFF (-fno-exceptions)");
#endif
    run<P<true>>("noexcept");
    run<P<false>>("throwing");

    std::vector<P<true>> v;
    g_copies = g_moves = 0;
    for (int i = 0; i < 64; ++i) v.emplace_back(i);
    std::printf("  push_back x64 without reserve: relocations=%u (final capacity %zu)\n", g_moves + g_copies, v.capacity());
}
