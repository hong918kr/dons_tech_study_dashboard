/* CPP-B03 demo: 범위 밖 enum 값 — 고정 기저 타입이면 정의됨, 아니면 UB
 *   c++ -std=c++20 -Wall -Wextra -c demo_out_of_range.cpp -DSHOW_ERROR              # constexpr 에서 UB 검출
 *   c++ -std=c++20 -g -O1 -fsanitize=enum demo_out_of_range.cpp -o oor && ./oor     # 런타임 UBSan
 */
#include <cstdint>
#include <cstdio>

enum Legacy { L0, L1, L2, L3 };                          /* 고정 타입 없음 → 값의 범위 [0, 3] */
enum class Fixed : std::uint32_t { F0, F1, F2, F3 };     /* 고정 타입 → uint32_t 의 모든 값 */
enum class Scoped { S0, S1 };                            /* enum class 는 ': int' 가 기본 = 역시 고정 */

constexpr Fixed  f7 = static_cast<Fixed>(7);             /* OK */
constexpr Scoped s9 = static_cast<Scoped>(9);            /* OK */
static_assert(static_cast<std::uint32_t>(f7) == 7);     /* 값 7 이 그대로 보존 */
#ifdef SHOW_ERROR
constexpr Legacy l7 = static_cast<Legacy>(7);            /* UB → 상수식이 아님 → 에러 */
#endif

/* 모든 열거자를 다룬 switch 라도, 범위 밖 값이면 함수 끝으로 떨어진다. clang 은 경고하지 않는다. */
const char* name_bad(Fixed f) {
    switch (f) {
    case Fixed::F0: return "F0";
    case Fixed::F1: return "F1";
    case Fixed::F2: return "F2";
    case Fixed::F3: return "F3";
    }
#ifdef FIX
    return "?";
#endif
}   /* FIX 없이 f7 을 넘기면 값을 반환하지 않고 끝 = UB */

__attribute__((noinline)) Legacy make_legacy(int v) { return static_cast<Legacy>(v); }
__attribute__((noinline)) Fixed  make_fixed(int v)  { return static_cast<Fixed>(v); }
volatile int seven = 7;

int main() {
    Fixed  f = make_fixed(seven);
    Legacy l = make_legacy(seven);
    std::printf("fixed=%u scoped=%d legacy=%d\n",
                static_cast<unsigned>(f), static_cast<int>(s9), static_cast<int>(l));
    std::printf("name_bad(F2)=%s\n", name_bad(Fixed::F2));
}
