/* CPP-B10 demo: std::array 를 값으로 넘길 때의 실제 비용 (AArch64 호출 규약)
 * c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_byvalue.cpp | c++filt
 * (-S 만 한다: ext_* 는 다른 번역 단위에 있다고 가정한 선언뿐이라 링크하지 않음)
 */
#include <array>
#include <cstdint>

using Frame = std::array<std::uint8_t, 8>;
using Big   = std::array<std::uint32_t, 64>;   /* 256 바이트 */

/* 피호출자(callee) 쪽: 값/참조 모두 본문은 같다 */
Frame bump_last(Frame f) { f[7] += 1; return f; }          /* 8B: 레지스터 x0 하나로 왕복 */
std::uint32_t first_val(Big b) { return b[0]; }           /* 256B: 호출자가 만든 복사본의 주소를 받음 */

/* 호출자(caller) 쪽: 본문을 못 보는 외부 함수라면 복사가 실제로 일어난다 */
std::uint32_t ext_val(Big b);
std::uint32_t ext_ref(const Big& b);
Big g_big;
std::uint32_t call_val() { return ext_val(g_big); }
std::uint32_t call_ref() { return ext_ref(g_big); }
