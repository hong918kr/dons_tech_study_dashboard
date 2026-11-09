// CPP-M11 demo: [[nodiscard]] 가 잡는 것과 못 잡는 것. checks.sh nodiscard 가 CASE 1..12 를 돌려 표를 만든다.
//   단독 실행: c++ -std=c++20 -Wall -Wextra -DCASE=1 -fsyntax-only demo_nodiscard.cpp
#include <cstdint>
#include <tuple>

enum class Err : std::uint8_t { None, Timeout };

template <class T>
class [[nodiscard]] Res {                         // 클래스에 붙인 nodiscard
public:
    constexpr Res(T v) : v_(v) {}
    constexpr bool ok() const { return e_ == Err::None; }
    T v_{};
    Err e_ = Err::None;
};
struct WithDtor {                                  // non-trivial 소멸자를 가진 결과 타입
    int v = 0;
    ~WithDtor() {}
};

Res<int> read_sensor() { return 1; }
Res<int>& read_ref() { static Res<int> r{1}; return r; }
int plain_status() { return 0; }
[[nodiscard]] int fn_level() { return 0; }
[[nodiscard("센서 고장을 놓친다 — 결과를 확인하라")]] int fn_reason() { return 0; }
__attribute__((warn_unused_result)) int gnu_wur() { return 0; }
[[nodiscard]] WithDtor make_withdtor() { return {}; }

using FnType = Res<int> (*)();
using FnLevel = int (*)();

void cases() {
#if CASE == 1
    read_sensor();                     // 클래스 nodiscard: 경고
#elif CASE == 2
    (void)read_sensor();               // (void): 표준이 인정한 무시 — 경고 없음
#elif CASE == 3
    std::ignore = read_sensor();       // std::ignore: 대입이라 "사용" — 경고 없음
#elif CASE == 4
    FnType p = read_sensor;
    p();                               // 함수 포인터로 불러도 반환 타입이 nodiscard — 경고
#elif CASE == 5
    FnLevel p = fn_level;
    p();                               // 함수에 붙인 nodiscard 는 포인터로 가면 사라진다 — 경고 없음
#elif CASE == 6
    read_ref();                        // 참조를 반환하면 nodiscard 타입이어도 경고 없음
#elif CASE == 7
    auto r = read_sensor();            // 변수에 받고 안 씀 — -Wunused-variable (trivial 소멸자라서)
#elif CASE == 8
    auto r = make_withdtor();          // non-trivial 소멸자 — 변수 미사용 경고가 없다
#elif CASE == 9
    plain_status();                    // 일반 int — 경고 없음 (C 스타일 에러 코드의 약점)
#elif CASE == 10
    fn_reason();                       // C++20 이유 문자열이 경고에 들어간다
#elif CASE == 11
    (void)gnu_wur();                   // GNU warn_unused_result + (void): 컴파일러마다 다르다
#elif CASE == 12
    (read_sensor(), 0);                // 콤마 왼쪽은 버려지는 식 — 경고
#endif
}
