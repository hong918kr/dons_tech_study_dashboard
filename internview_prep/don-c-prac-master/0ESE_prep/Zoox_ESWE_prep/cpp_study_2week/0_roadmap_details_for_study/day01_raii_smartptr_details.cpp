/*
[1일차: RAII와 스마트 포인터 실험 코드]
각 항목별로 실험 코드를 작성했습니다.
*/

#include <iostream>
#include <memory>

class RawResource {
public:
    RawResource() { std::cout << "RawResource acquired\n"; }
    ~RawResource() { std::cout << "RawResource released\n"; }
    void hello() { std::cout << "Hello from RawResource\n"; }
};

// 1. new/delete로 메모리 할당/해제하는 클래스
void raw_pointer_demo() {
    std::cout << "[raw_pointer_demo]\n";
    RawResource* ptr = new RawResource();
    ptr->hello();
    delete ptr;
}

// 2. unique_ptr로 관리하도록 리팩토링
void unique_ptr_demo() {
    std::cout << "[unique_ptr_demo]\n";
    std::unique_ptr<RawResource> ptr(new RawResource());
    ptr->hello();
    // 자동 해제
}

// 3. unique_ptr와 shared_ptr의 차이점 예시
void unique_vs_shared_demo() {
    std::cout << "[unique_vs_shared_demo]\n";
    std::unique_ptr<RawResource> uptr(new RawResource());
    // std::unique_ptr<RawResource> uptr2 = uptr; // 컴파일 에러(복사 불가)
    std::unique_ptr<RawResource> uptr2 = std::move(uptr); // 이동만 가능

    std::shared_ptr<RawResource> sptr1(new RawResource());
    std::shared_ptr<RawResource> sptr2 = sptr1; // 복사 가능, 참조 카운트 증가
    std::cout << "sptr1 use_count: " << sptr1.use_count() << "\n";
    std::cout << "sptr2 use_count: " << sptr2.use_count() << "\n";
}

// 4. unique_ptr 소유권 이전(move) 실험
void unique_ptr_move_demo() {
    std::cout << "[unique_ptr_move_demo]\n";
    std::unique_ptr<RawResource> ptr1(new RawResource());
    std::unique_ptr<RawResource> ptr2 = std::move(ptr1);
    if (!ptr1) std::cout << "ptr1 is nullptr after move\n";
    ptr2->hello();
}

// 5. shared_ptr 참조 카운트 확인
void shared_ptr_refcount_demo() {
    std::cout << "[shared_ptr_refcount_demo]\n";
    std::shared_ptr<RawResource> sptr1(new RawResource());
    {
        std::shared_ptr<RawResource> sptr2 = sptr1;
        std::cout << "use_count (inner): " << sptr1.use_count() << "\n";
    }
    std::cout << "use_count (outer): " << sptr1.use_count() << "\n";
}

// 6. shared_ptr 순환 참조 메모리 누수 실험
struct Node;
using NodePtr = std::shared_ptr<Node>;
struct Node {
    NodePtr next;
    ~Node() { std::cout << "Node destroyed\n"; }
};
void shared_ptr_cycle_demo() {
    std::cout << "[shared_ptr_cycle_demo]\n";
    NodePtr n1 = std::make_shared<Node>();
    NodePtr n2 = std::make_shared<Node>();
    n1->next = n2;
    n2->next = n1; // 순환 참조 발생
    std::cout << "Cycle created (memory leak!)\n";
}

// 7. weak_ptr로 순환 참조 해결
struct SafeNode;
using SafeNodePtr = std::shared_ptr<SafeNode>;
struct SafeNode {
    std::weak_ptr<SafeNode> next;
    ~SafeNode() { std::cout << "SafeNode destroyed\n"; }
};
void weak_ptr_cycle_demo() {
    std::cout << "[weak_ptr_cycle_demo]\n";
    SafeNodePtr n1 = std::make_shared<SafeNode>();
    SafeNodePtr n2 = std::make_shared<SafeNode>();
    n1->next = n2;
    n2->next = n1; // weak_ptr이므로 순환 참조 없음
    std::cout << "Cycle broken with weak_ptr\n";
}

int main() {
    std::cout << "=== Day 1: RAII & 스마트 포인터 실험 ===\n";
    raw_pointer_demo();
    std::cout << "-------------------------\n";
    unique_ptr_demo();
    std::cout << "-------------------------\n";
    unique_vs_shared_demo();
    std::cout << "-------------------------\n";
    unique_ptr_move_demo();
    std::cout << "-------------------------\n";
    shared_ptr_refcount_demo();
    std::cout << "-------------------------\n";
    shared_ptr_cycle_demo();
    std::cout << "-------------------------\n";
    weak_ptr_cycle_demo();
    std::cout << "-------------------------\n";
    return 0;
}

/*
[실행 결과 예시]
=== Day 1: RAII & 스마트 포인터 실험 ===
[raw_pointer_demo]
RawResource acquired
Hello from RawResource
RawResource released
-------------------------
[unique_ptr_demo]
RawResource acquired
Hello from RawResource
RawResource released
-------------------------
[unique_vs_shared_demo]
RawResource acquired
RawResource acquired
sptr1 use_count: 2
sptr2 use_count: 2
RawResource released
RawResource released
-------------------------
[unique_ptr_move_demo]
RawResource acquired
ptr1 is nullptr after move
Hello from RawResource
RawResource released
-------------------------
[shared_ptr_refcount_demo]
RawResource acquired
use_count (inner): 2
use_count (outer): 1
RawResource released
-------------------------
[shared_ptr_cycle_demo]
Node destroyed
Node destroyed
Cycle created (memory leak!)
-------------------------
[weak_ptr_cycle_demo]
SafeNode destroyed
SafeNode destroyed
Cycle broken with weak_ptr
-------------------------
*/