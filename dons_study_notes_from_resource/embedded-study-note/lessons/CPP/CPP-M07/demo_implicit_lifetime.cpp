// CPP-M07 demo_implicit_lifetime — 어떤 타입이 implicit-lifetime 인가 (std::is_implicit_lifetime 은 C++23)
//   c++ -std=c++23 -fno-exceptions -fno-rtti demo_implicit_lifetime.cpp -o /tmp/il && /tmp/il
#include <cstdint>
#include <cstdio>
#include <type_traits>

struct Pod            { std::uint32_t a; };
struct AggUserCtorMem { struct M { M() {} int x; } m; };          // 집합체, 소멸자 user-provided 아님
struct AggUserDtor    { int a; ~AggUserDtor() {} };               // 집합체지만 소멸자 user-provided
struct UserCtorOnly   { int a; UserCtorOnly(int x) : a(x) {} };   // 복사 생성자가 trivial
struct Driver         { int a; Driver() : a(0) {} ~Driver() {} };
struct Virt           { virtual void f(); };

#define SHOW(T) std::printf("%-16s implicit_lifetime=%d trivially_copyable=%d aggregate=%d\n", #T, \
    std::is_implicit_lifetime_v<T>, std::is_trivially_copyable_v<T>, std::is_aggregate_v<T>)

int main() {
    SHOW(int);
    SHOW(Pod);
    SHOW(AggUserCtorMem);
    SHOW(AggUserDtor);
    SHOW(UserCtorOnly);
    SHOW(Driver);
    SHOW(Virt);
    using PodArr = Pod[4];
    using DrvArr = Driver[4];
    SHOW(PodArr);
    SHOW(DrvArr);
}
