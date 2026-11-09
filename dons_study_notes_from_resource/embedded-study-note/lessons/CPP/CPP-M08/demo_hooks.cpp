// CPP-M08 demo_hooks.cpp — 클래스 operator new/delete 의 경계를 실제로 관찰한다 (호스트)
//   c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_hooks.cpp -o demo_hooks && ./demo_hooks
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>

// 전역 배열 new 를 세어 "어디로 갔나"를 본다 (CPP-B14 의 카운터와 같은 방식)
static int g_global_array_news = 0;
void* operator new[](std::size_t n) { ++g_global_array_news; return std::malloc(n); }
void  operator delete[](void* p) noexcept { std::free(p); }
void  operator delete[](void* p, std::size_t) noexcept { std::free(p); }

// ① 배열 new 는 클래스의 단일 객체 operator new 로 오지 않는다
static int g_class_news = 0;
alignas(8) static unsigned char g_arena[256];
struct OnlySingle {
    int v = 0;
    static void* operator new(std::size_t) noexcept { ++g_class_news; return g_arena; }
    static void  operator delete(void*) noexcept {}
};

// ② 가드 없는 상속: 블록보다 큰 파생 클래스가 옆 블록을 덮는다
constexpr std::size_t kUnsafeBlock = 16;
alignas(8) static unsigned char g_unsafe[4][kUnsafeBlock];
static int g_unsafe_next = 0;
struct Unsafe {
    unsigned long tag = 0x1111;
    static void* operator new(std::size_t sz) noexcept {
        std::printf("  Unsafe::operator new(sz=%zu)  블록=%zu\n", sz, kUnsafeBlock);
        return g_unsafe[g_unsafe_next++];
    }
    static void operator delete(void*) noexcept {}
};
struct UnsafeBig : Unsafe { unsigned long extra[4] = {0xAAAA, 0xBBBB, 0xCCCC, 0xDDDD}; };

// ③ sized delete 에 무엇이 오나
struct NonVirt {
    int a = 0;
    static void* operator new(std::size_t) noexcept { return g_arena; }
    static void  operator delete(void*, std::size_t sz) noexcept { std::printf("  NonVirt::operator delete sz=%zu\n", sz); }
};
struct NonVirtD : NonVirt { int b[7] = {}; };
struct Virt {
    virtual ~Virt() = default;
    int a = 0;
    static void* operator new(std::size_t) noexcept { return g_arena; }
    static void  operator delete(void*, std::size_t sz) noexcept { std::printf("  Virt::operator delete sz=%zu\n", sz); }
};
struct VirtD : Virt { int b[7] = {}; };

// ④ 두 형태를 다 선언하면 어느 쪽이 선택되나
struct Both {
    int a = 0;
    static void* operator new(std::size_t) noexcept { return g_arena; }
    static void operator delete(void*) noexcept { std::puts("  Both::operator delete(void*)"); }
    static void operator delete(void*, std::size_t) noexcept { std::puts("  Both::operator delete(void*, size_t)"); }
};

int main() {
    std::puts("① new OnlySingle[3] 은 어디로?");
    auto* arr = new OnlySingle[3];
    std::printf("  클래스 operator new 호출=%d, 전역 operator new[] 호출=%d\n", g_class_news, g_global_array_news);
    delete[] arr;

    std::puts("② 블록보다 큰 파생 클래스 (가드 없음)");
    auto* first  = new Unsafe;
    auto* second = new Unsafe;
    std::printf("  옆 객체 second->tag 전: 0x%lx\n", second->tag);
    delete first;
    g_unsafe_next = 0;                          // 첫 블록을 다시 내준 것처럼
    auto* big = new UnsafeBig;
    std::printf("  옆 객체 second->tag 후: 0x%lx   ← 덮였다\n", second->tag);
    delete big;

    std::puts("③ sized delete 의 sz");
    std::printf("  sizeof NonVirt=%zu NonVirtD=%zu Virt=%zu VirtD=%zu\n",
                sizeof(NonVirt), sizeof(NonVirtD), sizeof(Virt), sizeof(VirtD));
    Virt* v = new VirtD;
    delete v;
    Virt* v2 = new Virt;
    delete v2;
    NonVirtD* nd = new NonVirtD;
    delete nd;                                  // 정적 타입 == 동적 타입 (정상)
#ifdef SHOW_UB
    NonVirt* nb = new NonVirtD;
    delete nb;                                  // UB: [expr.delete]/3 — virtual 소멸자 없이 base 로 delete
#endif

    std::puts("④ 둘 다 선언한 클래스의 delete");
    delete new Both;
    return 0;
}
