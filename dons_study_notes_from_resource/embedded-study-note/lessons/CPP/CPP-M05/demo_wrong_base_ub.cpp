// CPP-M05 demo_wrong_base_ub.cpp — 가드가 없으면 잘못된 기반이 컴파일되고, 런타임에 메모리를 망가뜨린다
//   c++ -std=c++20 -g -O0 -fsanitize=address -fno-omit-frame-pointer demo_wrong_base_ub.cpp -o /tmp/ub && /tmp/ub
// 의도적으로 UB 다. 교육용 — ASan 이 잡는 것을 보여 준다.
#include <cstdio>

template <class D>
struct Naive {
    void hit() { static_cast<D&>(*this).count_ += 1; }
};
struct A : Naive<A> { long pad_ = 0; long count_ = 0; };
struct B : Naive<A> {};     // 복붙 실수: B 는 A 가 아닌데 self() 는 A 로 내린다

int main() {
    B b;
    std::printf("sizeof(A)=%zu sizeof(B)=%zu\n", sizeof(A), sizeof(B));
    b.hit();                // A::count_ 는 B 객체(1바이트) 밖
    std::printf("survived\n");
    return 0;
}
