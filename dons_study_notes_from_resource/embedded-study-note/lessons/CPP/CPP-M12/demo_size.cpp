// CPP-M12 demo: 같은 타이머 테이블을 Delegate / std::function 으로 각각 링크해 ELF 크기 비교 (measure.sh size)
#define main solution_main
#include "solution.cpp"
#undef main

// USE_FUNCTION=1: 포인터 하나 캡처(SBO 안) / USE_FUNCTION=2: 3워드 캡처(힙으로 감)
#ifdef USE_FUNCTION
using Cb = std::function<void(std::uint32_t)>;
#else
using Cb = Delegate<void(std::uint32_t)>;
#endif

// deadline 에 = 0 을 안 주면 Slot 이 상수 초기화되지 않아 Delegate 판에서도 .init_array 가 생긴다 (본문 참고)
struct Slot { Cb cb; std::uint32_t deadline = 0; };
#ifdef USE_FUNCTION
extern "C" {
void* __dso_handle = nullptr;   // -nostartfiles 에서 전역 소멸자 등록(__cxa_atexit)에 필요
void _init() {}                 // newlib __libc_init_array/fini_array 가 찾는다 (crti 없음)
void _fini() {}
}
static Slot g_table[4];
#else
static constinit Slot g_table[4];
#endif
static Led g_a, g_b;

extern "C" void _start() {
#if !defined(USE_FUNCTION)
    g_table[0].cb = Cb::bind<&Led::on_tick>(g_a);
    g_table[1].cb = Cb::bind<&Led::on_tick>(g_b);
#elif USE_FUNCTION == 1
    g_table[0].cb = [p = &g_a](std::uint32_t n) { p->on_tick(n); };
    g_table[1].cb = [p = &g_b](std::uint32_t n) { p->on_tick(n); };
#else
    std::uint32_t pad1 = 1, pad2 = 2;
    g_table[0].cb = [p = &g_a, pad1, pad2](std::uint32_t n) { p->on_tick(n + pad1 + pad2); };
    g_table[1].cb = [p = &g_b, pad1, pad2](std::uint32_t n) { p->on_tick(n + pad1 + pad2); };
#endif
    g_table[0].deadline = 10;
    g_table[1].deadline = 20;
    for (std::uint32_t now = 0;; ++now)
        for (Slot& s : g_table)
            if (s.cb && static_cast<std::int32_t>(now - s.deadline) >= 0) s.cb(now);
}
