// CPP-B11 demo: span은 소유하지 않는다 — 수명이 끝난 버퍼를 가리키는 span
//   c++ -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti demo_dangling.cpp            # 경고 0, 그럴듯한 값 출력
//   c++ -std=c++20 -g -O1 -fsanitize=address,undefined demo_dangling.cpp && ./a.out        # ASan: stack-use-after-scope
//   c++ -std=c++20 -Wall -O2 -DSHOW_WARN demo_dangling.cpp                                  # clang이 잡는 단순 케이스
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <span>

__attribute__((noinline)) static void dma_fill(std::span<std::byte> s) { std::memset(s.data(), 0x5A, s.size()); }
__attribute__((noinline)) static int sum(std::span<const std::byte> s) {
    int t = 0;
    for (std::byte b : s) t += std::to_integer<int>(b);
    return t;
}
#ifdef SHOW_WARN
static std::array<std::byte, 4> make_frame() { return {}; }
static std::span<const std::byte> local_view() { std::array<std::byte, 4> a{}; return a; }
#endif

int main() {
    std::span<const std::byte> view;
    {
        std::array<std::byte, 8> rx{};   // 블록 안 지역 버퍼
        dma_fill(rx);
        view = rx;                       // 블록 밖으로 뷰가 탈출
    }
    std::printf("sum=%d\n", sum(view));  // UB: rx는 이미 죽었다
#ifdef SHOW_WARN
    std::span<const std::byte> t = make_frame();   // -Wdangling-gsl
    std::printf("%d %d\n", sum(t), sum(local_view())); // -Wreturn-stack-address
#endif
}
