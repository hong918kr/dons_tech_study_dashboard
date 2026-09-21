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
    LinkedList() : head(nullptr), count(0) {}
    ~LinkedList() {
        Node* cur = head;
        while (cur) {
            Node* next = cur->next;
            delete cur;
            cur = next;
        }
    }
    void push_back(int val) {
        Node* n = new Node(val);
        if (!head) {
            head = n;
        } else {
            Node* cur = head;
            while (cur->next) cur = cur->next;
            cur->next = n;
        }
        count++;
    }
    void push_front(int val) {
        Node* n = new Node(val);
        n->next = head;
        head = n;
        count++;
    }
    bool remove(int val) {
        Node** cur = &head;
        while (*cur) {
            if ((*cur)->data == val) {
                Node* to_del = *cur;
                *cur = (*cur)->next;
                delete to_del;
                count--;
                return true;
            }
            cur = &((*cur)->next);
        }
        return false;
    }
    void reverse() {
        Node* prev = nullptr;
        Node* curr = head;
        while (curr) {
            Node* next = curr->next;
            curr->next = prev;
            prev = curr;
            curr = next;
        }
        head = prev;
    }
    void print() const {
        Node* cur = head;
        while (cur) {
            cout << cur->data << " ";
            cur = cur->next;
        }
        cout << endl;
    }
    int size() const {
        return count;
    }
    bool find(int val) const {
        Node* cur = head;
        while (cur) {
            if (cur->data == val) return true;
            cur = cur->next;
        }
        return false;
    }

private:
    Node* head;
    int count;
};

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