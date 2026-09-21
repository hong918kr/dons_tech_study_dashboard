#include <iostream>
#include <cassert>
using namespace std;

// 단일 연결리스트 노드
struct Node {
    int data;
    Node* next;
    Node(int d) : data(d), next(nullptr) {}
};

// LinkedList 클래스
class LinkedList {
public:
    LinkedList();
    ~LinkedList();
    void push_back(int val);         // 맨 뒤에 노드 추가
    void push_front(int val);        // 맨 앞에 노드 추가
    bool remove(int val);            // 값이 val인 노드 삭제
    void reverse();                  // 리스트 역순
    void print() const;              // 리스트 출력
    int size() const;                // 리스트 길이 반환
    bool find(int val) const;        // 값이 존재하는지 확인

private:
    Node* head;
    int count;
};

// TODO: 각 함수 구현

// 테스트 코드
int main() {
    LinkedList list;
    assert(list.size() == 0);

    list.push_back(10);
    list.push_back(20);
    list.push_front(5);
    list.print(); // 5 10 20

    assert(list.size() == 3);
    assert(list.find(10));
    assert(!list.find(99));

    assert(list.remove(10));
    list.print(); // 5 20
    assert(list.size() == 2);

    list.reverse();
    list.print(); // 20 5

    assert(list.remove(5));
    assert(list.remove(20));
    assert(list.size() == 0);

    cout << "All tests PASS" << endl;
    return 0;
}

/*
문제 예시:
- 단일 연결리스트 클래스를 구현하라.
- push_back, push_front, remove, reverse, find, size, print 멤버함수를 제공하라.
- main에서 위 함수들을 테스트하는 코드를 작성하라.
*/