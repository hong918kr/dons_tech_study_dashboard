// CPP-M08 demo_size.cpp — ELF 크기 비교: 전역 heap new vs 클래스 풀 (arm-none-eabi-g++ 로 링크, Lima VM)
//   F="-std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -nostartfiles --specs=nosys.specs -Wl,--gc-sections -ffunction-sections -fdata-sections"
//   arm-none-eabi-g++ $F -DUSE_POOL demo_size.cpp -o pool.elf
//   arm-none-eabi-g++ $F            demo_size.cpp -o heap.elf
//   arm-none-eabi-size pool.elf heap.elf
using u32 = unsigned int;
using size_t_ = decltype(sizeof 0);
#ifdef USE_POOL
alignas(4) static unsigned char g_storage[4][8];
static signed char g_next[4] = {1, 2, 3, -1};
static signed char g_head = 0;
struct Message {
    u32 id;
    u32 seq;
    Message(u32 i) : id(i), seq(0) {}
    static void* operator new(size_t_) noexcept {
        if (g_head < 0) return nullptr;
        int i = g_head;
        g_head = g_next[i];
        return g_storage[i];
    }
    static void operator delete(void* p, size_t_) noexcept {
        int i = static_cast<int>((static_cast<unsigned char*>(p) - g_storage[0]) / 8);
        g_next[i] = g_head;
        g_head = static_cast<signed char>(i);
    }
};
#else
struct Message {
    u32 id;
    u32 seq;
    Message(u32 i) : id(i), seq(0) {}
};
#endif

volatile u32 g_sink;

[[gnu::noinline]] static Message* make(u32 i) { return new Message(i); }
[[gnu::noinline]] static void drop(Message* m) { delete m; }

extern "C" void _start() {
    for (;;) {
        Message* m = make(g_sink);
        if (m) { g_sink = m->id; drop(m); }
    }
}
