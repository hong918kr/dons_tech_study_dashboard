#include <iostream>
#include <cassert>
#include <queue>
#include <vector>
#include <cstring>
using namespace std;

// 1. Memory Pool (고정 크기 객체 메모리 풀)
class MemoryPool {
public:
    MemoryPool(size_t obj_size, size_t obj_count);
    ~MemoryPool();
    void* alloc();
    void free(void* p);
private:
    vector<char> pool;
    vector<void*> free_list;
    size_t obj_size;
    size_t obj_count;
};

// 2. ISR-safe Queue (Lock-free 단순 원형 큐)
class ISRQueue {
public:
    ISRQueue(size_t cap);
    bool push(int val); // ISR에서 호출 가능
    bool pop(int& val); // 메인 루프에서 호출
    bool empty() const;
    bool full() const;
private:
    vector<int> buf;
    size_t head, tail, count, capacity;
};

// 3. Circular Buffer (고정 크기, 오버플로우시 덮어쓰기)
class CircularBuffer {
public:
    CircularBuffer(size_t cap);
    void push(int val);
    bool pop(int& val);
    bool empty() const;
    bool full() const;
    size_t size() const;
private:
    vector<int> buf;
    size_t head, tail, count, capacity;
};

// 4. State Machine (간단한 상태 전이 예제)
enum State { IDLE, RUN, ERROR };
class StateMachine {
public:
    StateMachine();
    void event_start();
    void event_error();
    void event_reset();
    State get_state() const;
private:
    State state;
};

// TODO: 각 클래스 함수 구현

// 테스트 코드
int main() {
    // 1. Memory Pool
    MemoryPool mp(sizeof(int), 4);
    void* p1 = mp.alloc();
    void* p2 = mp.alloc();
    mp.free(p1);
    void* p3 = mp.alloc();
    assert(p3 == p1); // freed block reused

    // 2. ISR-safe Queue
    ISRQueue q(3);
    assert(q.push(1));
    assert(q.push(2));
    assert(q.push(3));
    assert(!q.push(4)); // full
    int v;
    assert(q.pop(v) && v == 1);
    assert(q.pop(v) && v == 2);
    assert(q.pop(v) && v == 3);
    assert(!q.pop(v)); // empty

    // 3. Circular Buffer
    CircularBuffer cb(2);
    cb.push(10); cb.push(20);
    assert(cb.full());
    cb.push(30); // overwrite oldest
    assert(cb.full());
    assert(cb.pop(v) && v == 20);
    assert(cb.pop(v) && v == 30);
    assert(cb.empty());

    // 4. State Machine
    StateMachine sm;
    assert(sm.get_state() == IDLE);
    sm.event_start();
    assert(sm.get_state() == RUN);
    sm.event_error();
    assert(sm.get_state() == ERROR);
    sm.event_reset();
    assert(sm.get_state() == IDLE);

    cout << "All tests PASS" << endl;
    return 0;
}

/*
문제 예시:
- 고정 크기 객체를 관리하는 Memory Pool 클래스를 구현하라.
- ISR-safe(인터럽트 안전)한 단순 원형 큐를 구현하라.
- Circular Buffer(고정 크기, 오버플로우시 덮어쓰기)를 구현하라.
- 간단한 상태 전이(State Machine) 클래스를 구현하라.
- main에서 위 기능들을 테스트하는 코드를 작성하라.
*/