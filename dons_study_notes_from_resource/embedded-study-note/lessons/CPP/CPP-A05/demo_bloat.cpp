// CPP-A05 demo_bloat.cpp — static_vector 를 5 타입 × 3 용량(N) = 15 인스턴스로 쓰는 가짜 펌웨어. bloat.sh 가 빌드/측정한다.
//   -DIMPL=0  fat::static_vector  (모든 멤버 함수가 (T,N) 마다)
//   -DIMPL=1  thin::static_vector (+ demo_thin_core.cpp 를 같이 링크, 또는 -DTHIN_CORE_INLINE)
//   -DNS=3|1  타입마다 N 3종(4,16,64) 또는 1종(16만) — "N 이 코드를 만들지 않는다" 확인용
//   -DSITES=1|3  연산마다 호출처 수. 인스턴스마다 생산자(push_back + insert)·소비자(at + erase + pop_back) 함수를
//                SITES 벌 (상수만 조금씩 달라 서로 다른 함수 — ICF 로 접히지 않게)
//   -DNONTRIV=1 원소 타입을 비-trivial Counted<1..3> × N(4,16,64) 9 인스턴스로 (thin 은 ops 표 경로)
//   -DKEEP_MEM  memcpy/memmove 를 _start 에서 붙잡아 둔다 ("이미 다른 코드가 쓰고 있는 펌웨어" 기준선)
#include "demo_vec.hpp"

#ifndef IMPL
#define IMPL 0
#endif
#ifndef NS
#define NS 3
#endif
#ifndef SITES
#define SITES 1
#endif
#if IMPL == 0
#define VEC fat::static_vector
#else
#define VEC thin::static_vector
#endif
#define NOINL __attribute__((noinline))

struct CanFrame { std::uint32_t id; std::uint8_t dlc; std::uint8_t pad[3]; std::uint8_t data[8]; };

template <class T> inline T mk(std::uint32_t x) { return static_cast<T>(x); }
template <> inline CanFrame mk<CanFrame>(std::uint32_t x) { return CanFrame{x & 0x7FFu, 8, {}, {std::uint8_t(x), 1, 2, 3, 4, 5, 6, std::uint8_t(x >> 8)}}; }
template <class T> inline std::uint32_t key(const T& v) { return static_cast<std::uint32_t>(v); }
inline std::uint32_t key(const CanFrame& f) { return f.id + f.data[7]; }

#define SITE(tag, T, k)                                                                         \
    extern "C" NOINL bool prod_##tag##_##k(std::uint32_t x) {                                   \
        return g_##tag.push_back(mk<T>(x + k)) && g_##tag.insert((x >> k) & 3u, mk<T>(x >> 3)); \
    }                                                                                           \
    extern "C" NOINL std::uint32_t cons_##tag##_##k(std::uint32_t x) {                          \
        std::uint32_t s = 0;                                                                    \
        if (T* p = g_##tag.at((x >> k) & 7u)) s = key(*p);                                      \
        g_##tag.erase((x >> k) & 1u);                                                           \
        g_##tag.pop_back();                                                                     \
        return s + static_cast<std::uint32_t>(g_##tag.size()) + k;                              \
    }
#if SITES == 3
#define INST(tag, T, N) VEC<T, N> g_##tag; SITE(tag, T, 1) SITE(tag, T, 2) SITE(tag, T, 3)
#define CALL(tag) CALLK(tag, 1) CALLK(tag, 2) CALLK(tag, 3)
#else
#define INST(tag, T, N) VEC<T, N> g_##tag; SITE(tag, T, 1)
#define CALL(tag) CALLK(tag, 1)
#endif
#define CALLK(tag, k) acc += prod_##tag##_##k(x) ? 1u : 0u; acc += cons_##tag##_##k(x >> 1);

#if defined(NONTRIV) && NONTRIV
template <int K> struct Counted {                // 복사·대입·소멸이 비-trivial (부수 효과 = 전역 카운터)
    std::uint32_t v;
    explicit Counted(std::uint32_t x) : v(x) {}
    Counted(const Counted& o) : v(o.v) { g_events = g_events + 1; }
    Counted& operator=(const Counted& o) { v = o.v; g_events = g_events + 1; return *this; }
    ~Counted() { g_events = g_events + K; }
    static inline volatile std::uint32_t g_events = 0;
};
template <int K> inline std::uint32_t key(const Counted<K>& c) { return c.v; }
#if !defined(__clang__)    // 소멸자 있는 전역 → __aeabi_atexit(…, &__dso_handle) + newlib 의 _fini 참조. -nostartfiles 링크용 최소 스텁 (CPP-A03 참고)
extern "C" { void* __dso_handle = &__dso_handle; void _fini() {} }
#endif
#define ALL(X) X(c1_4, Counted<1>, 4) X(c1_16, Counted<1>, 16) X(c1_64, Counted<1>, 64)   \
               X(c2_4, Counted<2>, 4) X(c2_16, Counted<2>, 16) X(c2_64, Counted<2>, 64)   \
               X(c3_4, Counted<3>, 4) X(c3_16, Counted<3>, 16) X(c3_64, Counted<3>, 64)
#elif NS == 3
#define ALL(X) X(u8_4, std::uint8_t, 4) X(u8_16, std::uint8_t, 16) X(u8_64, std::uint8_t, 64)         \
               X(u16_4, std::uint16_t, 4) X(u16_16, std::uint16_t, 16) X(u16_64, std::uint16_t, 64)   \
               X(u32_4, std::uint32_t, 4) X(u32_16, std::uint32_t, 16) X(u32_64, std::uint32_t, 64)   \
               X(i32_4, std::int32_t, 4) X(i32_16, std::int32_t, 16) X(i32_64, std::int32_t, 64)      \
               X(can_4, CanFrame, 4) X(can_16, CanFrame, 16) X(can_64, CanFrame, 64)
#else
#define ALL(X) X(u8_16, std::uint8_t, 16) X(u16_16, std::uint16_t, 16) X(u32_16, std::uint32_t, 16) \
               X(i32_16, std::int32_t, 16) X(can_16, CanFrame, 16)
#endif

ALL(INST)

#define CALL1(tag, T, N) CALL(tag)
extern "C" volatile std::uint32_t g_seed;
volatile std::uint32_t g_seed = 1;
volatile std::uint32_t g_sink;
extern "C" [[noreturn]] void _start() {          // 펌웨어 진입점 흉내: 모든 인스턴스를 쓴다 (gc-sections 에 지워지지 않게)
#ifdef KEEP_MEM
    static void* (*volatile keep_cpy)(void*, const void*, std::size_t) = &std::memcpy;
    static void* (*volatile keep_mov)(void*, const void*, std::size_t) = &std::memmove;
    (void)keep_cpy; (void)keep_mov;
#endif
    for (;;) {
        std::uint32_t x = g_seed, acc = 0;
        ALL(CALL1)
        g_sink = acc;
    }
}
