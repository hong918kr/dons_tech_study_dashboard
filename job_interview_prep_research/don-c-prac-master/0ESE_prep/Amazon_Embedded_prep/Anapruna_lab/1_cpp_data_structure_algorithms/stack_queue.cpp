#include <iostream>
#include <cassert>
#include <vector>
#include <stdexcept>
using namespace std;

// Stack 클래스 (배열 기반)
class Stack {
public:
    Stack();
    void push(int val);
    int pop();
    int top() const;
    bool empty() const;
    int size() const;
private:
    vector<int> data;
};

// MinStack 클래스 (최소값 O(1)로 반환)
class MinStack {
public:
    MinStack();
    void push(int val);
    void pop();
    int top() const;
    int getMin() const;
    bool empty() const;
private:
    vector<int> data;
    vector<int> min_data;
};

// Queue 클래스 (배열 기반, 원형 큐)
class Queue {
public:
    Queue(int cap = 100);
    void push(int val);
    int pop();
    int front() const;
    bool empty() const;
    int size() const;
private:
    vector<int> buf;
    int head, tail, count, capacity;
};

// Circular Buffer (고정 크기, 오버플로우시 덮어쓰기)
class CircularBuffer {
public:
    CircularBuffer(int cap = 5);
    void push(int val);
    bool pop(int& val);
    bool empty() const;
    bool full() const;
    int size() const;
private:
    vector<int> buf;
    int head, tail, count, capacity;
};

// TODO: 각 클래스 함수 구현

// 테스트 코드
int main() {
    // Stack 테스트
    Stack s;
    assert(s.empty());
    s.push(1); s.push(2); s.push(3);
    assert(s.top() == 3);
    assert(s.pop() == 3);
    assert(s.size() == 2);

    // MinStack 테스트
    MinStack ms;
    ms.push(5); ms.push(3); ms.push(7);
    assert(ms.getMin() == 3);
    ms.pop();
    assert(ms.getMin() == 3);
    ms.pop();
    assert(ms.getMin() == 5);

    // Queue 테스트
    Queue q(3);
    assert(q.empty());
    q.push(10); q.push(20); q.push(30);
    assert(q.front() == 10);
    assert(q.pop() == 10);
    assert(q.size() == 2);

    // CircularBuffer 테스트
    CircularBuffer cb(3);
    assert(cb.empty());
    cb.push(1); cb.push(2); cb.push(3);
    assert(cb.full());
    int v;
    assert(cb.pop(v) && v == 1);
    cb.push(4);
    assert(cb.full());
    assert(cb.pop(v) && v == 2);
    assert(cb.pop(v) && v == 3);
    assert(cb.pop(v) && v == 4);
    assert(cb.empty());

    cout << "All tests PASS" << endl;
    return 0;
}

/*
문제 예시:
- Stack, MinStack, Queue, CircularBuffer 클래스를 직접 구현하라.
- 각 클래스의 주요 연산(push, pop, top/front, size, empty 등)을 제공하라.
- main에서 위 클래스들을 테스트하는 코드를 작성하라.
*/