// CPP-M09 demo_arm_reserve — arm-none-eabi-g++ 의 libstdc++ 가 -fno-exceptions 에서 재배치에 무엇을 부르는지 본다
// 멤버를 선언만 하면 인라인이 안 돼서 어셈블리의 bl 대상이 그대로 "복사 vs 이동"을 보여 준다. (컴파일만, 실행 안 함)
// build: arm-none-eabi-g++ -std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -DNX=false -S demo_arm_reserve.cpp -o -
#include <vector>
#ifndef NX
#define NX false
#endif
struct T {
    int v;
    T(const T&);
    T(T&&) noexcept(NX);
    ~T();
};
void grow(std::vector<T>& v) { v.reserve(16); }
