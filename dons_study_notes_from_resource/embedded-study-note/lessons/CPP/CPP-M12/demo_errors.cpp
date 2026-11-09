// CPP-M12 demo: Delegate 가 컴파일 단계에서 막는 것들
//   c++ -std=c++20 -fsyntax-only -DSHOW_ERROR=1 demo_errors.cpp   (1~5)
#define main solution_main
#include "solution.cpp"
#undef main

int main() {
    using D = Delegate<int(int)>;
    Counter c; const Counter cc{};
    (void)c; (void)cc;
#if SHOW_ERROR == 1
    auto d = D::bind<&Counter::add>(cc);          // 비 const 멤버를 const 객체에
#elif SHOW_ERROR == 2
    auto d = D::bind<&Counter::add>(Counter{});   // 임시 객체 → 곧바로 dangling
#elif SHOW_ERROR == 3
    auto d = Delegate<int(const char*)>::bind<&Counter::add>(c);  // 시그니처 불일치
#elif SHOW_ERROR == 4
    int x = 1;
    auto d = D::from_lambda([x](int) { return x; });  // 캡처 있는 람다
#elif SHOW_ERROR == 5
    auto d = D::bind<&Overloaded::f>(*new Overloaded);   // 오버로드 집합 → auto NTTP 추론 불가
#else
    auto d = D::bind<&Counter::add>(c);
#endif
    (void)d;
}
