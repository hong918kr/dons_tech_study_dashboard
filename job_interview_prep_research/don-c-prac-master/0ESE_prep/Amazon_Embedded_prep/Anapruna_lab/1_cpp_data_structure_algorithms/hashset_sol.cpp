#include <iostream>
#include <vector>
#include <list>
#include <cassert>
using namespace std;

// 간단한 해시셋 (정수만 지원, Separate Chaining)
class HashSet {
public:
    HashSet(int cap = 8) : table(cap), count(0), capacity(cap) {}

    bool insert(int key) {
        int idx = hash(key);
        for (int v : table[idx]) {
            if (v == key) return false; // 이미 있음
        }
        table[idx].push_back(key);
        count++;
        return true;
    }

    bool remove(int key) {
        int idx = hash(key);
        for (auto it = table[idx].begin(); it != table[idx].end(); ++it) {
            if (*it == key) {
                table[idx].erase(it);
                count--;
                return true;
            }
        }
        return false;
    }

    bool contains(int key) const {
        int idx = hash(key);
        for (int v : table[idx]) {
            if (v == key) return true;
        }
        return false;
    }

    void print() const {
        for (int i = 0; i < capacity; ++i) {
            cout << "Bucket " << i << ":";
            for (int v : table[i]) {
                cout << " " << v;
            }
            cout << endl;
        }
    }

    int size() const {
        return count;
    }

private:
    vector<list<int>> table;
    int count;
    int capacity;
    int hash(int key) const { return (key % capacity + capacity) % capacity; }
};

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