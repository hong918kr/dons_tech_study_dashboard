// CPP-M10 demo_link — 실제 링크한 Cortex-M4 ELF 로 크기를 잰다 (asm_target.sh link 참고)
//   -DKINDS=1|3   : 서로 다른 N 의 인스턴스 개수 (N 이 타입의 일부라 함수가 늘어나는지)
//   -DPOLICY=ReturnOnFull|TrapOnFull|BkptOnFull
//   -DNDEBUG 유무  : assert 가 끌고 오는 코드
#include "demo_static_vector.hpp"
#ifndef KINDS
#define KINDS 1
#endif
#ifndef POLICY
#define POLICY ReturnOnFull
#endif

// 펌웨어 현실: std::abort 대신 BKPT + 무한 루프
struct BkptOnFull { [[noreturn]] static void on_overflow() noexcept { __asm volatile("bkpt #0"); for (;;) {} } };

volatile int g_sink;
volatile int g_limit = 3;   // 런타임 값: 컴파일러가 "항상 넘친다/안 넘친다"를 증명하지 못하게

template <std::size_t N>
[[gnu::noinline]] void work() {
    static_vector<int, N, POLICY> v;
    const int limit = g_limit;
    for (int i = 0; i < limit; ++i) (void)v.push_back(i);
    (void)v.emplace(v.cbegin() + 1, 42);
    v.erase(v.cbegin());
    v.erase(v.cbegin(), v.cbegin() + 1);
    int s = 0;
    for (int x : v) s += x;
    g_sink = s;
}

extern "C" [[noreturn]] void _start() {
    work<4>();
#if KINDS >= 3
    work<8>();
    work<16>();
#endif
    for (;;) {}
}
