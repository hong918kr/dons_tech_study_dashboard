// CPP-B13 demo_errors.cpp — -fno-exceptions -fno-rtti 에서 무엇이 "컴파일 에러"가 되는지 확인
//   for n in 1 2 3 4 5 6; do c++ -std=c++20 -fno-exceptions -fno-rtti -DSHOW_ERROR=$n -c demo_errors.cpp -o /dev/null; done
//   (SHOW_ERROR 없이 빌드하면 에러 없이 통과 — 5번 upcast/6번 noexcept 는 플래그와 무관하게 OK)
#include <typeinfo>

struct Base { virtual ~Base() = default; virtual int id() const { return 0; } };
struct Derived : Base { int id() const override { return 1; } };
struct DriverError { int code; };

int f(Base* b, Derived* d) {
    int r = 0;
    (void)b;
#if defined(SHOW_ERROR) && SHOW_ERROR == 1
    if (!b) throw DriverError{-1};                       // throw
#endif
#if defined(SHOW_ERROR) && SHOW_ERROR == 2
    try { r = b->id(); } catch (...) { r = -1; }         // try / catch
#endif
#if defined(SHOW_ERROR) && SHOW_ERROR == 3
    if (auto* p = dynamic_cast<Derived*>(b)) r = p->id(); // downcast: RTTI 필요
#endif
#if defined(SHOW_ERROR) && SHOW_ERROR == 4
    r = typeid(*b) == typeid(Derived);                   // typeid: RTTI 필요
#endif
#if defined(SHOW_ERROR) && SHOW_ERROR == 5
    r = typeid(int).name()[0];                           // 정적 타입 typeid 도 막힌다
#endif
#if defined(SHOW_ERROR) && SHOW_ERROR == 6
    r = dynamic_cast<void*>(b) != nullptr;               // void* 캐스트: vtable 의 offset-to-top 만 쓴다
#endif
    Base* up = dynamic_cast<Base*>(d);                   // upcast 는 컴파일타임에 결정 → -fno-rtti 에서도 OK
    r += up->id();
    return r;
}
int g() noexcept { return 0; }                           // noexcept 는 -fno-exceptions 에서도 그대로 쓸 수 있다
#if !defined(__cpp_exceptions) && !defined(__cpp_rtti)
static_assert(true, "feature macros: both undefined under -fno-exceptions -fno-rtti");
#endif
