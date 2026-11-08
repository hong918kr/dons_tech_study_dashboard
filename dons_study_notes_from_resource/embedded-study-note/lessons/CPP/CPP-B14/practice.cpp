/* CPP-B14 추가 연습 — 한 번에 검증 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory_resource>
#include <new>
#include <string>
#include <vector>

/* ---- 공통: 크기 헤더를 붙이는 계측 할당기 (연습 2·3 에서 사용) ----
 * 블록 앞에 16바이트 헤더를 두고 요청 크기를 적어 둔다 → unsized delete 에서도 크기를 안다.
 * (정렬 > 16 오버로드는 이 연습에서 쓰지 않으므로 라이브러리 기본 구현에 맡긴다) */
struct Stats { unsigned news, deletes, violations; std::size_t live, peak; };
static Stats g;
static bool g_locked = false;                       // 연습 3: init 이후 잠금

static void* alloc_hdr(std::size_t n) {
    g.news++;
    if (g_locked) g.violations++;
    auto* base = static_cast<unsigned char*>(std::malloc(n + 16));
    if (base == nullptr) std::abort();
    std::memcpy(base, &n, sizeof n);
    g.live += n;
    if (g.live > g.peak) g.peak = g.live;
    return base + 16;
}
static void free_hdr(void* p) {
    if (p == nullptr) return;
    auto* base = static_cast<unsigned char*>(p) - 16;
    std::size_t n;
    std::memcpy(&n, base, sizeof n);
    g.live -= n;
    g.deletes++;
    std::free(base);
}
void* operator new(std::size_t n) { return alloc_hdr(n); }
void* operator new[](std::size_t n) { return alloc_hdr(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return alloc_hdr(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return alloc_hdr(n); }
void operator delete(void* p) noexcept { free_hdr(p); }
void operator delete[](void* p) noexcept { free_hdr(p); }
void operator delete(void* p, std::size_t) noexcept { free_hdr(p); }
void operator delete[](void* p, std::size_t) noexcept { free_hdr(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { free_hdr(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { free_hdr(p); }

template <class T> static void escape(T& t) { asm volatile("" : : "r"(&t) : "memory"); }
template <class F> static unsigned count_allocs(F&& f) { unsigned b = g.news; f(); return g.news - b; }

/* 연습 1: SSO / SBO 경계 측정 */
static std::size_t sso_capacity() {               // 할당 없이 담기는 최대 문자열 길이
    std::size_t n = 0;
    while (count_allocs([&] { std::string s(n + 1, 'x'); escape(s); }) == 0) n++;
    return n;
}
template <std::size_t N> static unsigned function_allocs() {   // N 바이트 캡처 std::function
    return count_allocs([] { std::array<std::uint8_t, N> cap{}; std::function<int()> f = [cap] { return cap[0]; }; escape(f); });
}

/* 연습 4: std::function 대신 함수 포인터 + 컨텍스트 */
struct Callback {
    void (*fn)(void* ctx, int ev) = nullptr;
    void* ctx = nullptr;
    void operator()(int ev) const { if (fn) fn(ctx, ev); }
};
struct BigState { std::array<int, 32> hist{}; int last = 0; };
static void on_event(void* ctx, int ev) { auto* s = static_cast<BigState*>(ctx); s->hist[ev % 32]++; s->last = ev; }

/* 연습 5: pmr — 정적 버퍼 위의 vector (전역 힙 사용 0) */
alignas(std::max_align_t) static std::byte g_arena[1024];

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main() {
    /* 연습 1 */
    std::size_t sso = sso_capacity();
    std::printf("  SSO capacity = %zu chars, function: 8B=%u 16B=%u 24B=%u 32B=%u\n", sso,
                function_allocs<8>(), function_allocs<16>(), function_allocs<24>(), function_allocs<32>());
    CHECK(sso >= 15);                              // libstdc++ 15, libc++(64비트) 22
    CHECK(count_allocs([&] { std::string s(sso + 1, 'x'); escape(s); }) == 1);
    CHECK(function_allocs<8>() == 0);
    CHECK(function_allocs<64>() == 1);

    /* 연습 2: 재할당 순간 peak > 최종 버퍼 크기 */
    std::size_t live0 = g.live;
    g.peak = g.live;
    std::size_t cap_bytes = 0;
    {
        std::vector<int> v;
        for (int i = 0; i < 100; i++) v.push_back(i);
        escape(v);
        cap_bytes = v.capacity() * sizeof(int);
        CHECK(g.live - live0 == cap_bytes);
    }
    std::printf("  vector<int> x100: final buffer=%zu B, peak live=%zu B\n", cap_bytes, g.peak - live0);
    CHECK(g.peak - live0 > cap_bytes);            // 옛 버퍼 + 새 버퍼가 잠깐 공존
    CHECK(g.live == live0);

    /* 연습 3: init 단계 할당은 허용, 그 뒤 잠금 */
    static std::vector<int>* table = nullptr;
    table = new std::vector<int>(64, 0);           // init: 허용
    unsigned v0 = g.violations;
    g_locked = true;                               // ---- 여기부터 운영 단계 ----
    (*table)[5] = 1;                               // 기존 버퍼 사용: 할당 없음
    CHECK(g.violations == v0);
    table->push_back(9);                           // capacity 초과 → 재할당 = 위반
    CHECK(g.violations == v0 + 1);
    g_locked = false;

    /* 연습 4 */
    static BigState state;
    unsigned a_cb = count_allocs([] {
        Callback cb{on_event, &state};
        for (int i = 0; i < 100; i++) cb(i);
        escape(cb);
    });
    CHECK(a_cb == 0);
    CHECK(state.last == 99 && state.hist[3] == 4);
    void (*plain)(void*, int) = [](void* ctx, int ev) { static_cast<BigState*>(ctx)->last = -ev; };  // 캡처 없는 람다 → 함수 포인터
    plain(&state, 5);
    CHECK(state.last == -5);

    /* 연습 5 */
    unsigned a_pmr = count_allocs([] {
        std::pmr::monotonic_buffer_resource arena(g_arena, sizeof g_arena, std::pmr::null_memory_resource());
        std::pmr::vector<int> v(&arena);
        for (int i = 0; i < 20; i++) v.push_back(i);
        auto* d = reinterpret_cast<std::byte*>(v.data());
        CHECK(d >= g_arena && d < g_arena + sizeof g_arena);
        CHECK(v[19] == 19);
    });
    CHECK(a_pmr == 0);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
