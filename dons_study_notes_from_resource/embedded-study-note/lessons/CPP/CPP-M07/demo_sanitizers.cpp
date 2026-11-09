// CPP-M07 demo_sanitizers — 정렬 누락과 소멸 후 접근을 도구로 잡기
//   c++ -std=c++20 -g -O0 -fno-exceptions -fno-rtti -fsanitize=undefined -DCASE=1 demo_sanitizers.cpp -o /tmp/s1 && /tmp/s1
//   c++ -std=c++20 -g -O0 -fno-exceptions -fno-rtti -fsanitize=address   -DCASE=2 demo_sanitizers.cpp -o /tmp/s2 && /tmp/s2
//   c++ -std=c++20 -g -O0 -fno-exceptions -fno-rtti -fsanitize=address   -DCASE=3 demo_sanitizers.cpp -o /tmp/s3 && /tmp/s3
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#if CASE == 3
#include <sanitizer/asan_interface.h>
#endif

struct Regs { std::uint64_t a; std::uint32_t b; };

#if CASE == 1
// alignas 를 빼먹은 저장소 (char 뒤에 있어서 8배수가 아님)
static struct { char tag; std::byte buf[sizeof(Regs)]; } g_bad;
int main() {
    Regs* r = ::new (static_cast<void*>(g_bad.buf)) Regs{1, 2};
    std::printf("addr %% 8 = %lu, a=%llu\n", reinterpret_cast<std::uintptr_t>(r) % 8, static_cast<unsigned long long>(r->a));
}
#else
alignas(Regs) static std::byte g_buf[sizeof(Regs)];
int main() {
    Regs* r = ::new (static_cast<void*>(g_buf)) Regs{1, 2};
    std::destroy_at(r);
#if CASE == 3
    ASAN_POISON_MEMORY_REGION(g_buf, sizeof g_buf);    // destroy 뒤에 저장소를 독 칠한다
#endif
    std::printf("after destroy: a=%llu\n", static_cast<unsigned long long>(r->a));   // 수명이 끝난 객체 접근 (UB)
}
#endif
