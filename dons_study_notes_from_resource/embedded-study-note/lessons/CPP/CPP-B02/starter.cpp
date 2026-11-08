// CPP-B02 extern "C"와 name mangling: C HAL과 링크
//
// 이 파일 하나로 "링커가 보는 이름"을 다룬다 (호스트 도구 코드라 std::string 사용 OK).
//   Part A — Itanium C++ ABI 맹글링 규칙을 직접 구현하고, 실제 컴파일러가 만든 심볼(dladdr)과 비교
//   Part B — Mach-O 의 '_' 접두사, top-level const, nm 출력 한 줄 파싱
//   Part C — 벡터 테이블 "링크" 흉내(dlsym = 이름으로 찾기) + C 콜백 등록(트램펄린)
//
// 빌드: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o s && ./s
// (macOS: export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk)
// Linux 에서는 dlsym 이 실행 파일 심볼을 보도록 -rdynamic 을 추가한다.
#include <cxxabi.h>  // abi::__cxa_demangle — Itanium C++ ABI 런타임(libc++abi / libsupc++)
#include <dlfcn.h>   // dladdr / dlsym — 실행 파일 안의 "진짜" 심볼 이름 조회
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

// ======================= Part A: Itanium 맹글링 (자유 함수만) =======================
enum class Ty { Void, Bool, Char, SChar, UChar, Short, UShort, Int, UInt,
                Long, ULong, LongLong, ULongLong, Float, Double };

struct Param {
    Ty   ty;
    bool ptr = false;            // T*
    bool pointee_const = false;  // const T*  (ptr 일 때만 의미)
    bool top_const = false;      // T const 파라미터 (값 자체가 const) — 시그니처에서 무시된다
};
enum class Lang { C, Cpp };
struct FuncDecl {
    Lang                     lang;
    std::vector<std::string> ns;      // {"hal","gpio"} == hal::gpio::
    std::string              name;
    std::vector<Param>       params;  // 비어 있으면 (void)
};

// 기본 타입 한 글자 코드 (Itanium C++ ABI "Mangling" 장의 <builtin-type> 표)
char builtin_code(Ty t) {
    (void)t;
    return '?';  // TODO: Ty 마다 한 글자 (void=v, bool=b, unsigned int=j, unsigned long=m, ...)
}

static std::string source_name(const std::string& id) { return std::to_string(id.size()) + id; }

// C++ 링키지면 _Z<이름><파라미터>, C 링키지면 이름 그대로.
// 범위: 반복되는 포인터 타입이 없는 함수 (치환 S_ 규칙은 풀이 참고)
std::string mangle(const FuncDecl& d) {
    (void)source_name;
    return d.name;  // TODO: C 링키지면 그대로, C++ 이면 "_Z" + (N...E) 이름 + 파라미터 코드
}

// ======================= Part B: 오브젝트 포맷과 nm 출력 =======================
enum class ObjFmt { ELF, MachO };

// nm 이 보여주는 이름: Mach-O 는 C/C++ 가리지 않고 모든 심볼 앞에 '_' 하나를 더 붙인다.
std::string object_symbol(const FuncDecl& d, ObjFmt f) {
    (void)f;
    return mangle(d);  // TODO: Mach-O 차이 반영
}

struct SymInfo {
    std::string base;     // 마지막 이름 조각 (네임스페이스 제외), 예: "write", "USART1_IRQHandler"
    bool        mangled;  // C++ 맹글링 이름이었나?
};

// nm 한 줄의 심볼을 해석한다. 맹글링이면 <length><id> 를 읽고 N...E 중첩은 마지막 조각을 취한다.
SymInfo parse_symbol(std::string_view sym, ObjFmt f) {
    (void)f;
    return {std::string(sym), false};  // TODO: '_' 접두사 처리, _Z 판별, <length><id> 읽기
}

// ======================= Part C: 벡터 테이블 링크 + C 콜백 =======================
static unsigned g_hits_default, g_hits_systick, g_hits_usart1, g_hits_tim2;

extern "C" void Default_Handler() { ++g_hits_default; }
extern "C" void SysTick_Handler() { ++g_hits_systick; }
extern "C" void USART1_IRQHandler() { ++g_hits_usart1; }
// C-1: 이 핸들러가 벡터 테이블에 걸리게 하라 (힌트: 링커는 "TIM2_IRQHandler" 라는 이름을 찾는다)
void TIM2_IRQHandler() { ++g_hits_tim2; }

using Handler  = void (*)();
using LookupFn = void* (*)(const char* c_name);    // 이름 → 주소 (없으면 nullptr)

// startup 파일의 ".weak X / .thumb_set X, Default_Handler" 를 흉내낸다:
// 이름으로 강한 정의를 찾고, 없으면 Default_Handler 로 채운다.
void link_vectors(const char* const names[], std::size_t n, Handler table[], LookupFn lookup) {
    (void)names; (void)lookup;
    for (std::size_t i = 0; i < n; ++i) table[i] = &Default_Handler;  // TODO: lookup 으로 찾기
}

// nm 심볼 목록에서 "벡터 이름과 같은 base 를 가졌지만 맹글링된" 정의만 있는 ISR 을 찾는다
// (= extern "C" 를 빼먹어서 Default_Handler 가 대신 돌 핸들러). CI 린트로 쓸 수 있다.
std::vector<std::string> lint_isr(const std::vector<std::string>& syms,
                                  const char* const vec_names[], std::size_t n, ObjFmt f) {
    (void)syms; (void)vec_names; (void)n; (void)f;
    return {};  // TODO: parse_symbol 로 base 가 같은 맹글링 정의만 있는 벡터 이름을 모은다
}

// ---- mock C HAL (실제로는 hal.c 에 있고 hal.h 가 extern "C" 로 선언) ----
extern "C" {
typedef void (*hal_rx_cb_t)(void* ctx, uint8_t byte);
static hal_rx_cb_t s_rx_cb;
static void*       s_rx_ctx;
void hal_uart_set_rx_callback(hal_rx_cb_t cb, void* ctx) { s_rx_cb = cb; s_rx_ctx = ctx; }
void hal_sim_rx(uint8_t b) { if (s_rx_cb) s_rx_cb(s_rx_ctx, b); }   // "RX 인터럽트" 흉내
}

// C++ 드라이버: C HAL 의 콜백(함수 포인터 + void* ctx)에 자기 자신을 연결한다
class LineReader {
public:
    void attach() { /* TODO: C HAL 에 콜백 + ctx 등록 */ }
    bool        ready() const { return ready_; }
    const char* line() const { return buf_; }
private:
    static void rx_trampoline(void* ctx, uint8_t b) { (void)ctx; (void)b; /* TODO: ctx → 객체 → on_byte */ }
    void on_byte(uint8_t b) {
        if (b == '\n') { buf_[len_] = '\0'; ready_ = true; len_ = 0; }
        else if (b != '\r' && len_ + 1 < sizeof buf_) buf_[len_++] = static_cast<char>(b);
    }
    char        buf_[32] = {};
    std::size_t len_ = 0;
    bool        ready_ = false;
};

// ======================= 실제 심볼과 비교할 "진짜" 함수들 =======================
namespace hal { namespace gpio { void write(uint32_t, bool) {} } }
void hal_init() {}
std::size_t uart_write(const uint8_t*, std::size_t) { return 0; }
void f_top_const(const int) {}
void f_const_ptr(char* const) {}
namespace drv { extern "C" void drv_c_entry(uint32_t) {} }

// 함수 주소 → 실행 파일 심볼 이름 (dladdr 은 Mach-O 의 앞 '_' 를 떼고 돌려준다 = ELF 식 이름)
static std::string real_symbol(void* fn) {
    Dl_info info{};
    return (dladdr(fn, &info) && info.dli_sname) ? info.dli_sname : "?";
}
static std::string demangle(const std::string& m) {
    int st = 0;
    char* d = abi::__cxa_demangle(m.c_str(), nullptr, nullptr, &st);
    std::string r = (st == 0 && d) ? d : "<bad:" + m + ">";
    std::free(d);
    return r;
}
static void* lookup_real(const char* name) { return dlsym(RTLD_DEFAULT, name); }

// ======================= 테스트 하네스 =======================
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main() {
    using enum Ty;
    // ---- Part A ----
    const FuncDecl d_init{Lang::Cpp, {}, "hal_init", {}};
    const FuncDecl d_write{Lang::Cpp, {"hal", "gpio"}, "write", {{UInt}, {Bool}}};
    const FuncDecl d_uart{Lang::Cpp, {}, "uart_write", {{UChar, true, true}, {ULong}}};
    const FuncDecl d_c{Lang::C, {"drv"}, "drv_c_entry", {{UInt}}};
    CHECK(builtin_code(UInt) == 'j' && builtin_code(ULong) == 'm' && builtin_code(UChar) == 'h');
    CHECK(mangle(d_init) == "_Z8hal_initv");
    CHECK(mangle(d_write) == "_ZN3hal4gpio5writeEjb");
    CHECK(mangle(d_uart) == "_Z10uart_writePKhm");
    CHECK(mangle(d_c) == "drv_c_entry");
    // 실제 컴파일러가 만든 심볼과 일치하는가 (macOS arm64: uint32_t=unsigned int, size_t=unsigned long)
    CHECK(mangle(d_init) == real_symbol(reinterpret_cast<void*>(&hal_init)));
    CHECK(mangle(d_write) == real_symbol(reinterpret_cast<void*>(&hal::gpio::write)));
    CHECK(mangle(d_uart) == real_symbol(reinterpret_cast<void*>(&uart_write)));
    CHECK(mangle(d_c) == real_symbol(reinterpret_cast<void*>(&drv::drv_c_entry)));
    CHECK(demangle(mangle(d_write)) == "hal::gpio::write(unsigned int, bool)");

    // ---- Part B ----
    const FuncDecl d_tc{Lang::Cpp, {}, "f_top_const", {{Int, false, false, true}}};
    const FuncDecl d_cp{Lang::Cpp, {}, "f_const_ptr", {{Char, true, false, true}}};
    CHECK(mangle(d_tc) == "_Z11f_top_consti");
    CHECK(mangle(d_tc) == real_symbol(reinterpret_cast<void*>(&f_top_const)));
    CHECK(mangle(d_cp) == real_symbol(reinterpret_cast<void*>(&f_const_ptr)));
    CHECK(object_symbol(d_write, ObjFmt::MachO) == "__ZN3hal4gpio5writeEjb");
    CHECK(object_symbol(d_c, ObjFmt::MachO) == "_drv_c_entry");
    CHECK(object_symbol(d_write, ObjFmt::ELF) == "_ZN3hal4gpio5writeEjb");
    SymInfo a = parse_symbol("__ZN3hal4gpio5writeEjb", ObjFmt::MachO);
    SymInfo b = parse_symbol("_Z17USART1_IRQHandlerv", ObjFmt::ELF);
    SymInfo c = parse_symbol("_SysTick_Handler", ObjFmt::MachO);
    CHECK(a.base == "write" && a.mangled);
    CHECK(b.base == "USART1_IRQHandler" && b.mangled);
    CHECK(c.base == "SysTick_Handler" && !c.mangled);

    // ---- Part C ----
    static const char* const kVecNames[] = {"SysTick_Handler", "USART1_IRQHandler",
                                            "USART2_IRQHandler", "TIM2_IRQHandler"};
    constexpr std::size_t kN = sizeof kVecNames / sizeof kVecNames[0];
    Handler vt[kN] = {};
    link_vectors(kVecNames, kN, vt, &lookup_real);
    CHECK(vt[0] == &SysTick_Handler && vt[1] == &USART1_IRQHandler);
    CHECK(vt[2] == &Default_Handler);                     // 정의 없음 → weak 기본값
    CHECK(vt[3] == &TIM2_IRQHandler);                    // extern "C" 라야 이름으로 찾힌다
    for (Handler h : vt) if (h) h();                      // "인터럽트 발생"
    CHECK(g_hits_tim2 == 1 && g_hits_default == 1);
    // macOS 에서 실제로 nm 이 보여준 목록 (extern "C" 를 빼먹은 TIM2, 정상인 USART1)
    const std::vector<std::string> nm_out = {"_Default_Handler", "_SysTick_Handler", "_USART1_IRQHandler",
                                             "__Z15TIM2_IRQHandlerv", "__ZN3hal4gpio5writeEjb", "_main"};
    CHECK(lint_isr(nm_out, kVecNames, kN, ObjFmt::MachO) == std::vector<std::string>{"TIM2_IRQHandler"});
    LineReader rd;
    rd.attach();
    for (const char* p = "AT+GMR\r\n"; *p; ++p) hal_sim_rx(static_cast<uint8_t>(*p));
    CHECK(rd.ready() && std::string_view(rd.line()) == "AT+GMR");

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
