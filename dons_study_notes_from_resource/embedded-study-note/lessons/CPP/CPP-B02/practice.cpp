// CPP-B02 추가 연습 — 한 번에 검증
// 빌드: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o p && ./p
#include <cxxabi.h>
#include <dlfcn.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

static std::string real_symbol(void* fn) {
    Dl_info info{};
    return (dladdr(fn, &info) && info.dli_sname) ? info.dli_sname : "?";
}
static std::string cxa_demangle(const std::string& m) {
    int st = 0;
    char* d = abi::__cxa_demangle(m.c_str(), nullptr, nullptr, &st);
    std::string r = (st == 0 && d) ? d : "<bad>";
    std::free(d);
    return r;
}

// ---------- 연습 1: 미니 디맹글러 (Part A 범위: 네임스페이스 + 기본 타입 + P/PK) ----------
static const char* builtin_name(char c) {
    switch (c) {
    case 'v': return "void";   case 'b': return "bool";   case 'c': return "char";
    case 'a': return "signed char";  case 'h': return "unsigned char";
    case 's': return "short";  case 't': return "unsigned short";
    case 'i': return "int";    case 'j': return "unsigned int";
    case 'l': return "long";   case 'm': return "unsigned long";
    case 'x': return "long long";    case 'y': return "unsigned long long";
    case 'f': return "float";  case 'd': return "double";
    }
    return nullptr;
}
static std::string read_source_name(std::string_view s, std::size_t& i) {
    std::size_t len = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') len = len * 10 + std::size_t(s[i++] - '0');
    std::string id(s.substr(i, len));
    i += len;
    return id;
}
// 맹글링이 아니면 입력을 그대로 돌려준다 (C 심볼). c++filt 와 같은 출력 형식(east const: "char const*").
std::string demangle_simple(std::string_view s) {
    if (s.substr(0, 2) != "_Z") return std::string(s);
    std::size_t i = 2;
    std::string name;
    if (s[i] == 'N') {
        ++i;
        while (i < s.size() && s[i] != 'E') name += (name.empty() ? "" : "::") + read_source_name(s, i);
        ++i;  // 'E'
    } else {
        name = read_source_name(s, i);
    }
    std::string args;
    if (s.substr(i) != "v") {
        while (i < s.size()) {
            bool ptr = false, kon = false;
            if (s[i] == 'P') { ptr = true; ++i; }
            if (s[i] == 'K') { kon = true; ++i; }
            const char* b = builtin_name(s[i++]);
            if (!b) return std::string(s);  // 범위 밖 (치환 S_ 등) → 포기
            if (!args.empty()) args += ", ";
            args += std::string(b) + (kon ? " const" : "") + (ptr ? "*" : "");
        }
    }
    return name + "(" + args + ")";
}

namespace hal { namespace gpio { void write(uint32_t, bool) {} } }
std::size_t uart_write(const uint8_t*, std::size_t) { return 0; }
void hal_init() {}
void dma_start(volatile uint32_t*, uint16_t) {}   // volatile 은 범위 밖 → V 코드 (아래 CHECK 참고)

// ---------- 연습 2: 같은 typedef, 다른 맹글링 (타깃 ABI 의존) ----------
enum class Target { MacArm64, LinuxAarch64, ArmNoneEabi };
enum class Td { U32, U64, Size };
// 실측 (2026-11-07): macOS clang 21 / aarch64 g++ 15.2 / arm-none-eabi-g++ 14.2.1
//   void f(uint32_t, uint8_t, uint64_t, size_t)  →  mac: _Z1fjhym   aarch64: _Z1fjhmm   arm-none-eabi: _Z1fmhyj
char typedef_code(Target t, Td td) {
    switch (td) {
    case Td::U32:  return t == Target::ArmNoneEabi ? 'm' : 'j';     // newlib: uint32_t = unsigned long
    case Td::U64:  return t == Target::LinuxAarch64 ? 'm' : 'y';    // LP64 glibc: unsigned long
    case Td::Size: return t == Target::ArmNoneEabi ? 'j' : 'm';     // ILP32: size_t = unsigned int
    }
    return '?';
}
void abi_probe(uint32_t, uint64_t, std::size_t) {}

// ---------- 연습 3: 오버로드된 C++ API 를 C 에서 부를 수 있게 (EXTERN_C 매크로 + 래퍼) ----------
#ifdef __cplusplus
#define EXTERN_C_BEGIN extern "C" {
#define EXTERN_C_END   }
#else
#define EXTERN_C_BEGIN
#define EXTERN_C_END
#endif

static unsigned g_last_width, g_last_value;
namespace hal {
void reg_write(uint8_t v)  { g_last_width = 8;  g_last_value = v; }
void reg_write(uint16_t v) { g_last_width = 16; g_last_value = v; }
void reg_write(uint32_t v) { g_last_width = 32; g_last_value = v; }
}  // namespace hal

EXTERN_C_BEGIN
// extern "C" 는 오버로드 불가 → 이름에 폭을 넣는다 (C 에는 이름이 하나뿐)
void hal_reg_write_u8(uint8_t v)   { hal::reg_write(v); }
void hal_reg_write_u16(uint16_t v) { hal::reg_write(v); }
void hal_reg_write_u32(uint32_t v) { hal::reg_write(v); }
EXTERN_C_END

// ---------- 연습 4: 치환(substitution) 예측 ----------
namespace hal { namespace gpio { void write(const char*, const char*) {} } }
// 후보 순서: hal → S_, hal::gpio → S0_, char const → S1_, char const* → S2_
static const char* const kPredicted = "_ZN3hal4gpio5writeEPKcS2_";

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main() {
    // 연습 1: 실제 심볼을 미니 디맹글러로 풀고, libc++abi 결과와 비교
    const std::string s_write = real_symbol(reinterpret_cast<void*>(
        static_cast<void (*)(uint32_t, bool)>(&hal::gpio::write)));
    const std::string s_uart = real_symbol(reinterpret_cast<void*>(&uart_write));
    const std::string s_init = real_symbol(reinterpret_cast<void*>(&hal_init));
    CHECK(demangle_simple(s_write) == "hal::gpio::write(unsigned int, bool)");
    CHECK(demangle_simple(s_write) == cxa_demangle(s_write));
    CHECK(demangle_simple(s_uart) == cxa_demangle(s_uart));
    CHECK(demangle_simple(s_init) == "hal_init()");
    CHECK(demangle_simple("hal_gpio_write") == "hal_gpio_write");           // C 심볼은 그대로
    CHECK(real_symbol(reinterpret_cast<void*>(&dma_start)) == "_Z9dma_startPVjt");  // volatile = V

    // 연습 2: 호스트(macOS arm64) 열은 실제 심볼로 확인
    const std::string probe = std::string("_Z9abi_probe") + typedef_code(Target::MacArm64, Td::U32) +
                              typedef_code(Target::MacArm64, Td::U64) + typedef_code(Target::MacArm64, Td::Size);
    CHECK(probe == real_symbol(reinterpret_cast<void*>(&abi_probe)));
    CHECK(typedef_code(Target::ArmNoneEabi, Td::U32) == 'm');
    CHECK(typedef_code(Target::LinuxAarch64, Td::U64) == 'm');
    CHECK(typedef_code(Target::ArmNoneEabi, Td::Size) == 'j');

    // 연습 3: 래퍼 심볼이 맹글링되지 않았고, 올바른 오버로드로 디스패치되는가
    CHECK(real_symbol(reinterpret_cast<void*>(&hal_reg_write_u16)) == "hal_reg_write_u16");
    CHECK(dlsym(RTLD_DEFAULT, "hal_reg_write_u8") == reinterpret_cast<void*>(&hal_reg_write_u8));
    hal_reg_write_u16(0xBEEF);
    CHECK(g_last_width == 16 && g_last_value == 0xBEEF);
    hal_reg_write_u8(0x7F);
    CHECK(g_last_width == 8 && g_last_value == 0x7F);

    // 연습 4: 예측한 치환 이름이 실제와 같은가 + 미니 디맹글러는 치환을 만나면 포기하는가
    const std::string s_sub = real_symbol(reinterpret_cast<void*>(
        static_cast<void (*)(const char*, const char*)>(&hal::gpio::write)));
    CHECK(s_sub == kPredicted);
    CHECK(cxa_demangle(s_sub) == "hal::gpio::write(char const*, char const*)");
    CHECK(demangle_simple(s_sub) == s_sub);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
