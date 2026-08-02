/*
[2일차: 이동 의미론 (Move Semantics) 실험 코드]
각 항목별로 실험 코드를 작성했습니다.
*/

#include <iostream>
#include <vector>
#include <string>
#include <utility>

// 1. L-value와 R-value의 차이 코드로 설명
void lvalue_rvalue_demo() {
    std::cout << "[lvalue_rvalue_demo]\n";
    int a = 10;         // a는 l-value
    int& lref = a;      // l-value 참조
    int&& rref = 20;    // r-value 참조
    std::cout << "a: " << a << ", lref: " << lref << ", rref: " << rref << "\n";
}

// 2. std::move가 실제로 어떤 역할을 하는지 실험
void std_move_demo() {
    std::cout << "[std_move_demo]\n";
    std::string s1 = "hello";
    std::string s2 = std::move(s1); // s1의 리소스를 s2로 이동
    std::cout << "s1: '" << s1 << "', s2: '" << s2 << "'\n";
}

// 3. std::vector를 멤버로 갖는 클래스 + 복사/이동 생성자/대입 연산자 구현
class MyVec {
public:
    std::vector<int> data;
    MyVec() { std::cout << "기본 생성자\n"; }
    MyVec(const std::vector<int>& d) : data(d) { std::cout << "vector 생성자\n"; }
    MyVec(const MyVec& other) : data(other.data) { std::cout << "복사 생성자\n"; }
    MyVec(MyVec&& other) noexcept : data(std::move(other.data)) { std::cout << "이동 생성자\n"; }
    MyVec& operator=(const MyVec& other) {
        data = other.data;
        std::cout << "복사 대입 연산자\n";
        return *this;
    }
    MyVec& operator=(MyVec&& other) noexcept {
        data = std::move(other.data);
        std::cout << "이동 대입 연산자\n";
        return *this;
    }
};

// 4. 각 생성자/연산자에서 로그를 출력하여 객체가 복사/이동될 때 어떤 함수가 호출되는지 확인
void copy_move_log_demo() {
    std::cout << "[copy_move_log_demo]\n";
    MyVec v1({1,2,3});
    MyVec v2 = v1;             // 복사 생성자
    MyVec v3 = std::move(v1);  // 이동 생성자
    MyVec v4;
    v4 = v2;                   // 복사 대입 연산자
    v4 = std::move(v3);        // 이동 대입 연산자
}

// 5. std::vector에 객체를 push_back할 때 복사와 이동의 차이 실험
void vector_push_back_demo() {
    std::cout << "[vector_push_back_demo]\n";
    std::vector<MyVec> vecs;
    MyVec v({4,5,6});
    vecs.push_back(v);             // 복사 생성자
    vecs.push_back(std::move(v));  // 이동 생성자
}

// 6. 함수 인자로 값/const 참조/우측값 참조로 전달할 때 복사/이동 실험
void by_value(MyVec v) {
    std::cout << "by_value\n";
}
void by_const_ref(const MyVec& v) {
    std::cout << "by_const_ref\n";
}
void by_rvalue_ref(MyVec&& v) {
    std::cout << "by_rvalue_ref\n";
}
void function_arg_demo() {
    std::cout << "[function_arg_demo]\n";
    MyVec v({7,8,9});
    by_value(v);              // 복사 생성자
    by_const_ref(v);          // 복사 없음
    by_rvalue_ref(std::move(v)); // 이동 생성자
}

// 7. 반환값 최적화(RVO)와 이동 생성자 호출 여부 실험
MyVec make_myvec() {
    MyVec temp({10,11,12});
    return temp; // RVO 또는 이동 생성자
}
void rvo_demo() {
    std::cout << "[rvo_demo]\n";
    MyVec v = make_myvec();
}

int main() {
    std::cout << "=== Day 2: 이동 의미론 실험 ===\n";
    lvalue_rvalue_demo();
    std::cout << "-------------------------\n";
    std_move_demo();
    std::cout << "-------------------------\n";
    copy_move_log_demo();
    std::cout << "-------------------------\n";
    vector_push_back_demo();
    std::cout << "-------------------------\n";
    function_arg_demo();
    std::cout << "-------------------------\n";
    rvo_demo();
    std::cout << "-------------------------\n";
    return 0;
}

/*
[실행 결과 예시]
=== Day 2: 이동 의미론 실험 ===
[lvalue_rvalue_demo]
a: 10, lref: 10, rref: 20
-------------------------
[std_move_demo]
s1: '', s2: 'hello'
-------------------------
[copy_move_log_demo]
vector 생성자
복사 생성자
이동 생성자
기본 생성자
복사 대입 연산자
이동 대입 연산자
-------------------------
[vector_push_back_demo]
vector 생성자
복사 생성자
이동 생성자
-------------------------
[function_arg_demo]
vector 생성자
복사 생성자
by_value
by_const_ref
이동 생성자
by_rvalue_ref
-------------------------
[rvo_demo]
vector 생성자
이동 생성자
-------------------------
*/
