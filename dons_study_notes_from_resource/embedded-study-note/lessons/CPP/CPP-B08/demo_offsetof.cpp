/* CPP-B08 demo — standard-layout 이 아닌 타입에 offsetof / 오버레이 검사를 걸면?
 *   c++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only demo_offsetof.cpp                 # OK
 *   c++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only -DSHOW_ERROR demo_offsetof.cpp    # 에러 3개
 */
#include <cstddef>
#include <cstdint>
#include <type_traits>

struct Regs { volatile uint32_t CR, SR; };                                     // standard-layout
static_assert(offsetof(Regs, SR) == 4);                                         // OK

#ifdef SHOW_ERROR
struct RegsVirt { volatile uint32_t CR, SR; virtual void dump() const {} };    // vptr 추가
class  RegsMixed { public: volatile uint32_t CR; uint32_t sr() const { return SR; } private: volatile uint32_t SR; };

constexpr std::size_t o1 = offsetof(RegsVirt, SR);    // ① -Winvalid-offsetof (+ -Werror → 에러)
constexpr std::size_t o2 = offsetof(RegsMixed, CR);   // ② 같은 경고: 레이아웃이 같아 보여도 "보장" 없음
static_assert(std::is_standard_layout_v<RegsVirt>, "MMIO overlay must be standard-layout");  // ③
#endif
