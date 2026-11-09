// CPP-M10 demo_errors — 제약(requires)이 잘못된 사용을 호출 지점에서 막는지 확인
// build: c++ -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti demo_errors.cpp -o demo_errors && ./demo_errors
// error: c++ -std=c++20 -fsyntax-only -fno-exceptions -fno-rtti -DSHOW_ERROR demo_errors.cpp
#include <cstdio>
#include "demo_static_vector.hpp"

struct MoveOnly { int v; explicit MoveOnly(int x) : v(x) {} MoveOnly(MoveOnly&&) = default; MoveOnly& operator=(MoveOnly&&) = default; };
struct ConstMember { const int v; };

int main() {
    static_vector<MoveOnly, 4> m;
    (void)m.emplace_back(1);
    static_vector<ConstMember, 4> c;
    (void)c.emplace_back(2);
    static_vector<int, 4> small;
#ifdef SHOW_ERROR
    auto copy = m;                              // ① 이동 전용 T → 벡터 복사 불가
    MoveOnly x(3);
    (void)m.push_back(x);                       // ② 이동 전용 T 의 lvalue push_back
    c.erase(c.cbegin());                        // ③ 대입 불가 T 는 erase 불가
    static_vector<int, 8> big = small;          // ④ N 이 다르면 다른 타입
#endif
    std::printf("ok: %zu %zu %zu\n", m.size(), c.size(), small.size());
}
