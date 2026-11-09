// CPP-M14 demo: 템플릿 안의 static_assert(false) — dependent-false 함정 (CWG2518)
// c++ -std=c++20 -Wall -Wextra -fno-exceptions -fno-rtti -fsyntax-only demo_dep_false.cpp            ← 인스턴스화 안 하면 통과?
// c++ -std=c++20 -Wall -Wextra -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=N demo_dep_false.cpp   ← N=1,2,4 는 인스턴스화 → 실패, N=3 은 정의만
// c++ -std=c++17 ... 도 같은 결과인지 비교한다
#include <type_traits>

template <typename...> inline constexpr bool dependent_false_v = false;

// 형태 A: 비의존적 false — 예전 규칙이면 정의만으로 ill-formed NDR (CWG2518 이전)
template <typename T> unsigned width_a() {
    if constexpr (std::is_same_v<T, unsigned char>) return 8;
    else if constexpr (std::is_same_v<T, unsigned>) return 32;
    else static_assert(false, "width_a<T>: unsupported T");
}

// 형태 B: 의존적 false — 어느 컴파일러든, 어느 표준이든 안전한 고전 방식
template <typename T> unsigned width_b() {
    if constexpr (std::is_same_v<T, unsigned char>) return 8;
    else if constexpr (std::is_same_v<T, unsigned>) return 32;
    else static_assert(dependent_false_v<T>, "width_b<T>: unsupported T");
}

// 형태 C: 템플릿 "정의" 안의 비의존 조건식 — CWG2518 이후에는 정의만으로 실패하지 않는다 (아래 SHOW_ERROR=3 실행 결과 참고)
#if defined(SHOW_ERROR) && (SHOW_ERROR == 3 || SHOW_ERROR == 4)
template <typename T> void non_dependent() { static_assert(sizeof(int) == 1, "non-dependent condition"); }
#endif

int main() {
    unsigned s = width_a<unsigned char>() + width_b<unsigned>();
#if defined(SHOW_ERROR) && SHOW_ERROR == 1
    s += width_a<float>();
#elif defined(SHOW_ERROR) && SHOW_ERROR == 2
    s += width_b<float>();
#elif defined(SHOW_ERROR) && SHOW_ERROR == 4
    non_dependent<int>();
#endif
    return s == 40 ? 0 : 1;
}
