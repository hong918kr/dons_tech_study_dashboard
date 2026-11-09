// CPP-M08 demo_crash.cpp — noexcept 없는 operator new 가 고갈 때 nullptr 을 돌려주면 무슨 일이 생기나 (맥 arm64 에서 실행)
//   c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_crash.cpp -o demo_crash && ./demo_crash; echo "exit=$?"
//   c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -DUSE_NOEXCEPT demo_crash.cpp -o demo_crash && ./demo_crash; echo "exit=$?"
#include <cstddef>
#include <cstdio>
#ifdef USE_NOEXCEPT
#define NE noexcept
#else
#define NE
#endif

alignas(4) static unsigned char g_block[2][8];
static int g_next = 0;
[[gnu::noinline]] static void* pool_alloc() noexcept { return g_next < 2 ? g_block[g_next++] : nullptr; }

struct Message {
    unsigned id, seq;
    explicit Message(unsigned i) : id(i), seq(0) { std::puts("  생성자 실행"); }
    static void* operator new(std::size_t) NE { return pool_alloc(); }
    static void  operator delete(void*) noexcept {}
};

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);      // 크래시 직전 출력까지 보이게
    for (unsigned i = 0; i < 3; ++i) {
        std::printf("new #%u\n", i);
        Message* m = new Message(i);
        if (m == nullptr) { std::puts("  고갈: nullptr 받음 — 정상 처리"); break; }
        std::printf("  id=%u\n", m->id);
    }
    return 0;
}
