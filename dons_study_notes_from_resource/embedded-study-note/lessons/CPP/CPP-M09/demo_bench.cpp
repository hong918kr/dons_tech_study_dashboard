// CPP-M09 demo_bench — 이동 비용을 실제 시간으로 (호스트, 참고용. 하네스의 CHECK 에는 쓰지 않는다)
// build: c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_bench.cpp -o bench && ./bench
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

template <class T>
inline void do_not_optimize(T& v) { asm volatile("" : : "r,m"(v) : "memory"); }

template <class Owner>
double ns_per_move(Owner& a, Owner& b, unsigned iters) {
    auto t0 = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < iters; ++i) {
        b = std::move(a);
        do_not_optimize(b);
        a = std::move(b);
        do_not_optimize(a);
    }
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / (2.0 * iters);
}

template <std::size_t N>
struct Inline { std::array<std::uint8_t, N> a{}; };
struct Heap   { std::vector<std::uint8_t> v; };

int main() {
    const unsigned iters = 2'000'000;
    std::printf("%-8s %14s %14s\n", "bytes", "vector ns/move", "array ns/move");
    auto row = [&](auto&& inl, std::size_t n) {
        Heap h1{std::vector<std::uint8_t>(n, 1)}, h2;
        auto i2 = inl;
        double hv = ns_per_move(h1, h2, iters);
        double av = ns_per_move(inl, i2, iters);
        std::printf("%-8zu %14.2f %14.2f\n", n, hv, av);
    };
    row(Inline<16>{}, 16);
    row(Inline<64>{}, 64);
    row(Inline<256>{}, 256);
    row(Inline<1024>{}, 1024);
    row(Inline<4096>{}, 4096);
    row(Inline<65536>{}, 65536);
}
