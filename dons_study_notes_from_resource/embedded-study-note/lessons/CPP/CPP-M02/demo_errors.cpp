// CPP-M02 demo_errors.cpp — 컴파일 금지 항목의 실제 에러 메시지
//   for n in 1 2 3 4 5 6 7 8; do c++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only -DSHOW_ERROR=$n demo_errors.cpp; done
//   c++ -std=c++20 -fsyntax-only demo_errors.cpp   # SHOW_ERROR 없이는 깨끗하게 통과
#define main solution_main
#include "solution.cpp"
#undef main
using namespace dmax;

#if defined(SHOW_ERROR) && SHOW_ERROR == 1
auto e1 = hw::write(EN = true, BURST = 1);                  // 다른 레지스터의 필드
#elif defined(SHOW_ERROR) && SHOW_ERROR == 2
auto e2 = hw::write(BURST = 1, BURST = 2);                  // 같은 필드 두 번 (겹침)
#elif defined(SHOW_ERROR) && SHOW_ERROR == 3
auto e3 = hw::write(THRESH = 256);                          // 상수 범위 밖 → consteval
#elif defined(SHOW_ERROR) && SHOW_ERROR == 4
auto e4 = hw::write(THRESH.of<256>());                      // 상수 범위 밖 → static_assert
#elif defined(SHOW_ERROR) && SHOW_ERROR == 5
void e5(unsigned x) { hw::write(THRESH = x); }              // 변수를 '=' 로 → 정책을 고르라는 뜻
#elif defined(SHOW_ERROR) && SHOW_ERROR == 6
auto e6 = hw::write(TC = true);                             // W1C 필드는 '=' 금지 → ack()
#elif defined(SHOW_ERROR) && SHOW_ERROR == 7
void e7() { THRESH = 3; }                                   // 값만 만들고 버림 (아무 일도 안 함)
#elif defined(SHOW_ERROR) && SHOW_ERROR == 8
hw::Field<CSR, 4, 4> e8;                                    // 예약 비트 위의 필드
#endif
