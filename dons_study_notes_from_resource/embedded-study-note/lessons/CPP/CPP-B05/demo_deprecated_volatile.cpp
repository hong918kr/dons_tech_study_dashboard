// CPP-B05 demo: C++20 volatile deprecation (P1152) 과 C++23 부분 해제 (P2327) — clang 이 실제로 뭘 경고하나
//   c++ -std=c++17 -Wall -Wextra -fsyntax-only demo_deprecated_volatile.cpp   → 경고 0
//   c++ -std=c++20 -Wall -Wextra -fsyntax-only demo_deprecated_volatile.cpp   → 아래 (w) 줄만 경고
#include <cstdint>
static volatile std::uint32_t REG;

void ops() {
    REG |= 1u << 3;          //     비트 OR  compound  : C++20 deprecated → C++23 해제 (P2327)
    REG &= ~(1u << 3);       //     비트 AND compound  : 〃
    REG ^= 1u << 4;          //     비트 XOR compound  : 〃
    REG += 1u;               //     산술 compound      : 표준상 C++23 에서도 deprecated, clang 21 은 경고 안 함
    REG <<= 1;               //     시프트 compound    : 〃
    REG++;                   // (w) 후위 증가          : deprecated
    --REG;                   // (w) 전위 감소          : deprecated
    std::uint32_t v = (REG = 5u);   // (w) volatile 대입식의 결과값 사용 : deprecated
    (void)v;
    REG = REG + 1u;          //     명시적 RMW: 경고 없음 (읽기 1번, 쓰기 1번이 눈에 보인다)
}
void param(volatile std::uint32_t x) { (void)x; }   // (w) volatile 값 매개변수 : deprecated

int main() { ops(); param(1u); return 0; }
