/*
Day 2: 이동 의미론 (Move Semantics)

주요 초점: L/R-value, std::move, 이동 생성자/이동 대입 연산자
핵심 개념: 복사/이동 생성자 호출 로깅, 자원 소유권 이전의 효율성
목적: C++에서 불필요한 복사를 줄이고, 효율적으로 자원을 이전하는 방법을 실습을 통해 익힌다.
*/

#include <iostream>
#include <vector>
#include <utility>
#include <string>

/*
1. 복사 생성자/이동 생성자/복사 대입/이동 대입 로깅
   - 문제: MyString 클래스를 만들고, 각 생성자/대입 연산자에서 로그를 출력한다.
   - 배움: 객체가 복사/이동될 때 어떤 함수가 호출되는지 확인
   - 목적: 이동 의미론의 동작 원리 체감
*/
class MyString {
public:
    std::string data;

    MyString(const char* s) : data(s) {
        std::cout << "생성자: " << data << "\n";
    }
    MyString(const MyString& other) : data(other.data) {
        std::cout << "복사 생성자: " << data << "\n";
    }
    MyString(MyString&& other) noexcept : data(std::move(other.data)) {
        std::cout << "이동 생성자: " << data << "\n";
    }
    MyString& operator=(const MyString& other) {
        data = other.data;
        std::cout << "복사 대입: " << data << "\n";
        return *this;
    }
    MyString& operator=(MyString&& other) noexcept {
        data = std::move(other.data);
        std::cout << "이동 대입: " << data << "\n";
        return *this;
    }
};

/*
2. std::move를 사용한 벡터 push_back
   - 문제: std::vector에 MyString을 push_back할 때 복사와 이동의 차이를 확인한다.
   - 배움: std::move를 사용하면 이동 생성자가 호출됨을 확인
   - 목적: 불필요한 복사를 줄이는 패턴 습득
*/
void vector_push_back_test() {
    std::cout << "[vector_push_back_test]\n";
    std::vector<MyString> v;
    MyString a("hello");
    v.push_back(a);              // 복사 생성자 호출
    v.push_back(std::move(a));   // 이동 생성자 호출
}

/*
3. 함수 인자 전달: 값/참조/이동
   - 문제: 함수에 객체를 값/const 참조/이동(우측값 참조)으로 전달할 때의 차이 확인
   - 배움: 함수 인자 전달 방식에 따른 복사/이동 동작 이해
   - 목적: 효율적인 함수 인터페이스 설계
*/
void by_value(MyString s) {
    std::cout << "by_value: " << s.data << "\n";
}
void by_const_ref(const MyString& s) {
    std::cout << "by_const_ref: " << s.data << "\n";
}
void by_rvalue_ref(MyString&& s) {
    std::cout << "by_rvalue_ref: " << s.data << "\n";
}

void function_arg_test() {
    std::cout << "[function_arg_test]\n";
    MyString s("world");
    by_value(s);           // 복사 생성자
    by_const_ref(s);       // 복사 없음
    by_rvalue_ref(std::move(s)); // 이동 생성자 (함수 내부에서 이동하면)
}

/*
4. 반환값 최적화(RVO)와 이동
   - 문제: 함수를 통해 객체를 반환할 때 복사/이동 여부 확인
   - 배움: RVO와 이동 생성자의 실제 호출 여부 확인
   - 목적: 반환값 최적화와 이동 의미론의 관계 이해
*/
MyString make_mystring() {
    MyString temp("temp");
    return temp; // RVO 또는 이동 생성자
}

void return_value_test() {
    std::cout << "[return_value_test]\n";
    MyString s = make_mystring();
}

int main() {
    std::cout << "=== Day 2: 이동 의미론 ===\n";
    std::cout << "-------------------------\n";
    MyString s1("first");
    MyString s2 = s1; // 복사 생성자
    MyString s3 = std::move(s1); // 이동 생성자
    std::cout << "-------------------------\n";
    vector_push_back_test();
    std::cout << "-------------------------\n";
    function_arg_test();
    std::cout << "-------------------------\n";
    return_value_test();
    std::cout << "-------------------------\n";
    return 0;
}

/*
[실행 결과 예시]
=== Day 2: 이동 의미론 ===
-------------------------
생성자: first
복사 생성자: first
이동 생성자: first
-------------------------
[vector_push_back_test]
생성자: hello
복사 생성자: hello
이동 생성자: hello
-------------------------
[function_arg_test]
생성자: world
복사 생성자: world
by_value: world
by_const_ref: world
이동 생성자: world
by_rvalue_ref: world
-------------------------
[return_value_test]
생성자: temp
이동 생성자:
*/