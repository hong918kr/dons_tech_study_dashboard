#include <iostream>
#include <vector>
#include <list>
#include <cassert>
using namespace std;

// 간단한 해시셋 (정수만 지원, Separate Chaining)
class HashSet {
public:
    HashSet(int cap = 8);
    bool insert(int key);      // key 추가, 이미 있으면 false
    bool remove(int key);      // key 삭제, 없으면 false
    bool contains(int key) const; // key 존재 여부
    void print() const;        // 전체 버킷 출력
    int size() const;          // 원소 개수

private:
    vector<list<int>> table;
    int count;
    int capacity;
    int hash(int key) const { return key % capacity; }
};

// TODO: 각 함수 구현

// 테스트 코드
int main() {
    HashSet hs(5);
    assert(hs.size() == 0);

    assert(hs.insert(10));
    assert(hs.insert(15)); // 충돌 발생 (10%5==0, 15%5==0)
    assert(hs.insert(7));
    assert(!hs.insert(10)); // 이미 있음
    assert(hs.size() == 3);

    assert(hs.contains(15));
    assert(!hs.contains(99));

    assert(hs.remove(10));
    assert(!hs.remove(10)); // 이미 삭제됨
    assert(hs.size() == 2);

    hs.print(); // 버킷별로 원소 출력

    cout << "All tests PASS" << endl;
    return 0;
}

/*
문제 예시:
- 정수만 지원하는 간단한 해시셋(HashSet) 클래스를 구현하라.
- insert, remove, contains, size, print 멤버함수를 제공하라.
- Separate Chaining(연결리스트) 방식으로 충돌을 처리하라.
- main에서 위 함수들을 테스트하는 코드를 작성하라.
*/