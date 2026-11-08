/* CPP-B14 demo — 별도 TU 에 둔 "poison" operator new (demo_poison_app.cpp 와 함께 링크)
 *   c++ $F -Wl,-dead_strip demo_poison_new.cpp demo_poison_app.cpp -o /tmp/b14_two
 *   c++ $F -Wl,-dead_strip -Wl,-why_live,__Znwm demo_poison_new.cpp demo_poison_app.cpp -o /tmp/b14_two
 */
#include <new>
extern "C" [[noreturn]] void HEAP_IS_FORBIDDEN_see_CPP_B14(void);   // 선언만, 정의 없음
void* operator new(std::size_t) { HEAP_IS_FORBIDDEN_see_CPP_B14(); }
void* operator new[](std::size_t) { HEAP_IS_FORBIDDEN_see_CPP_B14(); }
