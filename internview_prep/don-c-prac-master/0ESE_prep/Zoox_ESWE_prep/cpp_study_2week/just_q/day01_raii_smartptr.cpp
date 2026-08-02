/*
Day 1: RAII & 스마트 포인터

주요 초점: std::unique_ptr, std::shared_ptr
핵심 개념: C-스타일 자원 관리와 스마트 포인터의 차이, 소유권 이전, 참조 카운팅, 순환 참조 문제와 해결
목적: C++에서 안전하고 효율적으로 자원을 관리하는 방법을 실습을 통해 익힌다.
*/

#include <iostream>
#include <memory>

// 예제에서 사용할 클래스
class RawResource {
public:
    RawResource() { std::cout << "RawResource acquired\n"; }
    ~RawResource() { std::cout << "RawResource released\n"; }
    void hello() { std::cout << "Hello from RawResource\n"; }
};

/*
1. unique_ptr 기본 사용
   - 문제: 동적 할당한 객체를 unique_ptr로 관리해보고, 자동 해제를 경험한다.
   - 배움: RAII의 기본, unique_ptr의 소유권 단일성
   - 목적: 메모리 누수 없는 안전한 자원 관리
*/
void unique_ptr_basic() {
    std::cout << "[unique_ptr_basic]\n";
    std::unique_ptr<RawResource> ptr(new RawResource());
    ptr->hello();
    // 자동으로 소멸자 호출
}

/*
2. unique_ptr 소유권 이전(move)
   - 문제: unique_ptr의 소유권을 다른 unique_ptr로 이동시켜본다.
   - 배움: std::move의 의미, 소유권 이전 후 원 포인터는 nullptr
   - 목적: 자원 소유권 이전의 안전한 패턴 이해
*/
void unique_ptr_move() {
    std::cout << "[unique_ptr_move]\n";
    std::unique_ptr<RawResource> ptr1(new RawResource());
    std::unique_ptr<RawResource> ptr2 = std::move(ptr1); // ptr1은 nullptr
    if (!ptr1) std::cout << "ptr1 is now nullptr\n";
    ptr2->hello();
}

/*
3. shared_ptr 기본 사용
   - 문제: 여러 shared_ptr가 하나의 객체를 공유할 때 참조 카운트 변화를 확인한다.
   - 배움: 참조 카운팅, 자동 해제 시점
   - 목적: 여러 객체가 자원을 공유할 때의 안전한 관리
*/
void shared_ptr_basic() {
    std::cout << "[shared_ptr_basic]\n";
    std::shared_ptr<RawResource> sp1(new RawResource());
    {
        std::shared_ptr<RawResource> sp2 = sp1;
        std::cout << "use_count: " << sp1.use_count() << "\n";
        sp2->hello();
    }
    std::cout << "use_count after inner scope: " << sp1.use_count() << "\n";
}

/*
4. shared_ptr 순환 참조 문제 (난이도↑)
   - 문제: 두 객체가 서로를 shared_ptr로 참조할 때 소멸자가 호출되지 않는 현상 관찰
   - 배움: 순환 참조가 메모리 누수로 이어지는 원리
   - 목적: shared_ptr 사용 시 주의점 인식
*/
struct Node;
using NodePtr = std::shared_ptr<Node>;
struct Node {
    NodePtr next;
    ~Node() { std::cout << "Node destroyed\n"; }
};
void shared_ptr_cycle_problem() {
    std::cout << "[shared_ptr_cycle_problem]\n";
    NodePtr n1 = std::make_shared<Node>();
    NodePtr n2 = std::make_shared<Node>();
    n1->next = n2;
    n2->next = n1; // 순환 참조 발생 (메모리 누수)
    std::cout << "Cycle created (memory leak!)\n";
}

/*
5. weak_ptr로 순환 참조 해결 (난이도↑)
   - 문제: weak_ptr을 사용해 순환 참조를 끊고, 정상적으로 소멸자가 호출되는지 확인
   - 배움: weak_ptr의 역할과 사용법
   - 목적: 안전하게 순환 참조를 방지하는 패턴 습득
*/
struct SafeNode;
using SafeNodePtr = std::shared_ptr<SafeNode>;
struct SafeNode {
    std::weak_ptr<SafeNode> next;
    ~SafeNode() { std::cout << "SafeNode destroyed\n"; }
};
void weak_ptr_break_cycle() {
    std::cout << "[weak_ptr_break_cycle]\n";
    SafeNodePtr n1 = std::make_shared<SafeNode>();
    SafeNodePtr n2 = std::make_shared<SafeNode>();
    n1->next = n2;
    n2->next = n1; // weak_ptr이므로 순환 참조 없음
    std::cout << "Cycle broken with weak_ptr\n";
}

/*
6. C 스타일 자원 관리 vs RAII (난이도↑)
   - 문제: new/delete로 직접 자원 관리와 unique_ptr로 자동 관리 비교
   - 배움: 수동 해제의 위험, RAII의 장점
   - 목적: 현대 C++에서 RAII를 사용하는 이유 체감
*/
void c_style_vs_raii() {
    std::cout << "[c_style_vs_raii]\n";
    // C 스타일
    RawResource* raw = new RawResource();
    raw->hello();
    delete raw; // 직접 해제 필요

    // RAII 스타일
    std::unique_ptr<RawResource> raii(new RawResource());
    raii->hello();
    // 자동 해제
}

int main() {
    std::cout << "=== Day 1: RAII & 스마트 포인터 ===\n";
    unique_ptr_basic();
    std::cout << "-------------------------\n";
    unique_ptr_move();
    std::cout << "-------------------------\n";
    shared_ptr_basic();
    std::cout << "-------------------------\n";
    shared_ptr_cycle_problem();
    std::cout << "-------------------------\n";
    weak_ptr_break_cycle();
    std::cout << "-------------------------\n";
    c_style_vs_raii();
    std::cout << "-------------------------\n";
    return 0;
}

/*
[실행 결과 예시]
=== Day 1: RAII & 스마트 포인터 ===
[unique_ptr_basic]
RawResource acquired
Hello from RawResource
RawResource released
-------------------------
[unique_ptr_move]
RawResource acquired
ptr1 is now nullptr
Hello from RawResource
RawResource released
-------------------------
[shared_ptr_basic]
RawResource acquired
use_count: 2
Hello from RawResource
use_count after inner scope: 1
RawResource released
-------------------------
[shared_ptr_cycle_problem]
Node destroyed
Node destroyed
Cycle created (memory leak!)
-------------------------
[weak_ptr_break_cycle]
SafeNode destroyed
SafeNode destroyed
Cycle broken with weak_ptr
-------------------------
[c_style_vs_raii]
RawResource acquired
Hello from RawResource
RawResource released
RawResource acquired
Hello from RawResource
```
*/