// CPP-M05 demo_same_name.cpp — 공개 API 와 구현 이름이 같으면: 빠뜨려도 컴파일되고 무한 재귀
//   c++ -std=c++20 -Wall -Wextra -O2 -S -o - demo_same_name.cpp
//   -Wall 진단 유무와 생성 코드를 함께 본다. 실행하지 않는다 (무한 재귀).
using u8 = unsigned char;
template <class D>
struct SameName {
    bool put(u8 b) { return static_cast<D&>(*this).put(b); }    // D 에 put 이 없으면 → 이 함수 자신
};
struct Forgot : SameName<Forgot> {};    // put 을 안 만들었는데...
bool forgot_call(Forgot& f) { return f.put(1); }
