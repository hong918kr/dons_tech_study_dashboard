/* CPP-A03 추가 연습 — 한 번에 검증
 *   연습 1: -fno-threadsafe-statics 의 guard (락 없음) — ISR 이 생성 도중 들어오면 이중 생성
 *   연습 2: CI 회귀 검사 — 이전/이후 nm 출력에서 "새로 생긴" 런타임 의존성만 뽑기
 *   연습 3: guard 변수 ABI — Itanium(8 B, 첫 바이트) vs ARM(4 B, bit0)
 *   연습 4: 누가 주나 — 아카이브 검색 순서가 바뀌면 다른 오브젝트가 끌려온다 (__aeabi_atexit)
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS %s\n", #expr); } \
                         else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
using std::string_view;
template <class T, std::size_t N> constexpr std::size_t count_of(const T (&)[N]) { return N; }

/* ---- mock 코어 (solution.cpp 와 같은 모델) ---- */
static uint32_t g_primask = 0;
static int g_isr_runs = 0;
static void (*g_pending_isr)() = nullptr;
static void (*g_armed_isr)() = nullptr;
static void dispatch_pending() {
    if (g_pending_isr != nullptr && (g_primask & 1u) == 0) { auto f = g_pending_isr; g_pending_isr = nullptr; ++g_isr_runs; f(); }
}
static void arm_preempt(void (*isr)()) { g_armed_isr = isr; }
static void isr_window() { if (g_armed_isr != nullptr) { g_pending_isr = g_armed_isr; g_armed_isr = nullptr; dispatch_pending(); } }
static void reset_core() { g_primask = 0; g_isr_runs = 0; g_pending_isr = nullptr; g_armed_isr = nullptr; }

/* ================= 연습 1: -fno-threadsafe-statics =================
 * clang thumbv7em 실측: 빠른 경로 "ldrb r0,[guard]; cbz r0, slow", 느린 경로 "bl ctor; movs r0,#1; strb r0,[guard]".
 * 런타임 호출도 busy 표시도 없다 → 생성 도중 ISR 이 같은 함수를 부르면 생성자가 두 번 돈다. */
struct LazyNts {
    uint8_t guard = 0;
    int value = 0;
    int ctor_runs = 0;
    int* get() {
        if (guard == 0) {                       /* ldrb; cbz */
            ++ctor_runs;                        /* bl ctor ... */
            isr_window();                       /* 생성자 도중 IRQ 가능 */
            value = 7;
            guard = 1;                          /* movs #1; strb */
        }
        return &value;
    }
};
static LazyNts g_nts;
static void isr_nts() { (void)g_nts.get(); }

/* ================= 연습 2: 새로 생긴 의존성 ================= */
static bool parse_undef(string_view line, string_view& name) {   /* solution 의 parse_nm_line 축약판: 미정의만 */
    string_view tok[4]; std::size_t n = 0, i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') ++i;
        if (i >= line.size()) break;
        std::size_t j = i; while (j < line.size() && line[j] != ' ') ++j;
        if (n == 4) return false;
        tok[n++] = line.substr(i, j - i); i = j;
    }
    if (n == 1 && tok[0].back() != ':') { name = tok[0]; return true; }
    if (n >= 2 && (tok[n - 2] == "U" || tok[n - 2] == "w")) { name = tok[n - 1]; return true; }
    return false;
}
static bool is_project(string_view s) { return s.substr(0, 4) == "hal_" || s.substr(0, 3) == "_ZN"; }   /* 이 픽스처의 프로젝트 심볼 */
struct NewDeps { std::size_t count = 0; string_view names[16]; };
static NewDeps new_deps(const string_view* before, std::size_t nb, const string_view* after, std::size_t na) {
    NewDeps r{};
    for (std::size_t i = 0; i < na; ++i) {
        string_view a;
        if (!parse_undef(after[i], a) || is_project(a)) continue;
        bool seen = false;
        for (std::size_t k = 0; k < nb && !seen; ++k) { string_view b; seen = parse_undef(before[k], b) && b == a; }
        if (!seen && r.count < 16) r.names[r.count++] = a;
    }
    return r;
}
static const string_view kFixed[] = {     /* fw_app.cpp, llvm-nm -u (실측) */
    "         U __aeabi_memclr4", "         U __aeabi_uldivmod", "         U hal_cycles64",
    "         U hal_otp_offset", "         U hal_uart_write",
};
static const string_view kLegacy[] = {    /* fw_app.cpp -DLEGACY, llvm-nm -u (실측) */
    "         U _ZdlPvj", "         U _Znwj", "         U __aeabi_memclr8", "         U __aeabi_uldivmod",
    "         U __cxa_atexit", "         U __cxa_guard_acquire", "         U __cxa_guard_release", "         U __dso_handle",
    "         U hal_cycles64", "         U hal_otp_offset", "         U hal_uart_write",
};
static const string_view kFixedExc[] = {  /* fw_app.cpp -fexceptions (throw 없음), llvm-nm -u (실측) */
    "         U __aeabi_memclr4", "         U __aeabi_uldivmod", "         U __aeabi_unwind_cpp_pr0",
    "         U hal_cycles64", "         U hal_otp_offset", "         U hal_uart_write",
};

/* ================= 연습 3: guard 변수 ABI ================= */
enum class Abi { Itanium, Arm };
constexpr std::size_t guard_size(Abi a) { return a == Abi::Arm ? 4u : 8u; }   /* nm -S 실측: thumbv7em 4, aarch64-none-elf 8 */
/* Itanium: 첫 바이트가 0 이면 미완료, 1 이면 완료. ARM: 32비트 워드의 최하위 비트 (리틀 엔디언에선 첫 바이트의 bit0) */
static bool guard_is_done(const unsigned char* g, Abi a) {
    if (a == Abi::Itanium) return g[0] != 0;
    uint32_t w; std::memcpy(&w, g, 4);                /* 호스트·Cortex-M 모두 리틀 엔디언 */
    return (w & 1u) != 0;
}

/* ================= 연습 4: 누가 주나 (link_vm.sh 의 아카이브 nm 실측을 표로) ================= */
enum class Ar : uint8_t { Supcxx, Gcc, C, Crt };
struct Def { Ar ar; string_view sym; string_view member; };
static constexpr Def kDefs[] = {
    {Ar::Supcxx, "__aeabi_atexit", "atexit_arm.o"}, {Ar::Supcxx, "_ZdlPvj", "del_ops.o"},
    {Ar::Supcxx, "__dynamic_cast", "dyncast.o"},    {Ar::Supcxx, "__cxa_throw", "eh_throw.o"},
    {Ar::Supcxx, "__cxa_guard_acquire", "guard.o"}, {Ar::Supcxx, "_Znwj", "new_op.o"},
    {Ar::Supcxx, "__cxa_pure_virtual", "pure.o"},
    {Ar::Gcc, "__aeabi_dmul", "_arm_muldivdf3.o"},  {Ar::Gcc, "__aeabi_uldivmod", "_aeabi_uldivmod.o"},
    {Ar::C, "__aeabi_memclr8", "libc_a-aeabi_memclr.o"}, {Ar::C, "__aeabi_atexit", "libc_a-aeabi_atexit.o"},
    {Ar::C, "__aeabi_memcpy4", "libc_a-aeabi_memcpy.o"}, {Ar::C, "memset", "libc_a-memset.o"},
    {Ar::C, "__cxa_atexit", "libc_a-cxa_atexit.o"},      {Ar::C, "atexit", "libc_a-atexit.o"},
    {Ar::Crt, "__dso_handle", "crtbegin.o"},
};
/* 링크 명령의 아카이브 순서대로 찾아 처음 정의한 멤버 (모델: GNU ld 는 미정의가 생긴 뒤의 아카이브를 순서대로 훑는다) */
static string_view who_provides(string_view sym, const Ar* order, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        for (const Def& d : kDefs)
            if (d.ar == order[i] && d.sym == sym) return d.member;
    return {};
}

int main() {
    /* ---- 연습 1 ---- */
    reset_core(); g_nts = LazyNts{};
    CHECK(*g_nts.get() == 7 && g_nts.ctor_runs == 1 && g_nts.guard == 1);
    reset_core(); g_nts = LazyNts{};
    arm_preempt(isr_nts);                        /* 생성자 도중 ISR 이 같은 static 사용 */
    (void)g_nts.get();
    CHECK(g_isr_runs == 1 && g_nts.ctor_runs == 2);   /* 이중 생성 — 아무도 알려 주지 않는다 */
    reset_core(); g_nts = LazyNts{};
    (void)g_nts.get();                           /* 대책: IRQ 켜기 전에 미리 한 번 */
    arm_preempt(isr_nts); isr_window();
    CHECK(g_isr_runs == 1 && g_nts.ctor_runs == 1);

    /* ---- 연습 2 ---- */
    {
        const NewDeps up = new_deps(kFixed, count_of(kFixed), kLegacy, count_of(kLegacy));
        CHECK(up.count == 7 && up.names[0] == "_ZdlPvj" && up.names[2] == "__aeabi_memclr8" && up.names[6] == "__dso_handle");
        const NewDeps down = new_deps(kLegacy, count_of(kLegacy), kFixed, count_of(kFixed));
        CHECK(down.count == 1 && down.names[0] == "__aeabi_memclr4");      /* 정렬이 바뀌어 다른 이름 */
        const NewDeps exc = new_deps(kFixed, count_of(kFixed), kFixedExc, count_of(kFixedExc));
        CHECK(exc.count == 1 && exc.names[0] == "__aeabi_unwind_cpp_pr0"); /* throw 가 없어도 -fexceptions 만으로 */
        CHECK(new_deps(kFixed, count_of(kFixed), kFixed, count_of(kFixed)).count == 0);
    }

    /* ---- 연습 3 ---- */
    {
        CHECK(guard_size(Abi::Arm) == 4 && guard_size(Abi::Itanium) == 8);
        const unsigned char arm_busy[4] = {0x00, 0x01, 0, 0};   /* 워드 0x100: fw_stubs 의 busy 비트 */
        const unsigned char arm_done[4] = {0x01, 0x00, 0, 0};
        CHECK(!guard_is_done(arm_busy, Abi::Arm) && guard_is_done(arm_done, Abi::Arm));
        const unsigned char it_done[8] = {1, 0, 0, 0, 0, 0, 0, 0};
        const unsigned char it_zero[8] = {0, 1, 1, 1, 1, 1, 1, 1};   /* 첫 바이트만 본다 */
        CHECK(guard_is_done(it_done, Abi::Itanium) && !guard_is_done(it_zero, Abi::Itanium));
        const unsigned char arm_bit1[4] = {0x02, 0, 0, 0};       /* bit0 이 아니면 완료가 아니다 */
        CHECK(!guard_is_done(arm_bit1, Abi::Arm) && guard_is_done(arm_bit1, Abi::Itanium));
    }

    /* ---- 연습 4 ---- */
    {
        const Ar order_g[] = {Ar::Supcxx, Ar::Gcc, Ar::C};       /* -lsupc++ -lgcc -lc */
        const Ar order_c[] = {Ar::C, Ar::Gcc};                   /* -lc -lgcc (libsupc++ 없음) */
        CHECK(who_provides("__cxa_guard_acquire", order_g, 3) == "guard.o" && who_provides("__cxa_guard_acquire", order_c, 2).empty());
        CHECK(who_provides("__aeabi_atexit", order_g, 3) == "atexit_arm.o");
        CHECK(who_provides("__aeabi_atexit", order_c, 2) == "libc_a-aeabi_atexit.o");
        CHECK(who_provides("__cxa_atexit", order_g, 3) == "libc_a-cxa_atexit.o");   /* libsupc++ 에는 없다 */
        CHECK(who_provides("__aeabi_uldivmod", order_c, 2) == "_aeabi_uldivmod.o" && who_provides("__dso_handle", order_g, 3).empty());
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
