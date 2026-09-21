#include <iostream>
#include <cassert>
#include <queue>
#include <vector>
#include <cstring>
using namespace std;

// 1. Memory Pool (고정 크기 객체 메모리 풀)
class MemoryPool {
public:
    MemoryPool(size_t obj_size, size_t obj_count)
        : pool(obj_size * obj_count), obj_size(obj_size), obj_count(obj_count) {
        for (size_t i = 0; i < obj_count; ++i)
            free_list.push_back(&pool[i * obj_size]);
    }
    ~MemoryPool() {
        // vector 자동 해제
    }
    void* alloc() {
        if (free_list.empty()) return nullptr;
        void* p = free_list.back();
        free_list.pop_back();
        return p;
    }
    void free(void* p) {
        free_list.push_back(p);
    }
private:
    vector<char> pool;
    vector<void*> free_list;
    size_t obj_size;
    size_t obj_count;
};

// 2. ISR-safe Queue (Lock-free 단순 원형 큐)
class ISRQueue {
public:
    ISRQueue(size_t cap)
        : buf(cap), head(0), tail(0), count(0), capacity(cap) {}
    bool push(int val) {
        if (count == capacity) return false;
        buf[tail] = val;
        tail = (tail + 1) % capacity;
        ++count;
        return true;
    }
    bool pop(int& val) {
        if (count == 0) return false;
        val = buf[head];
        head = (head + 1) % capacity;
        --count;
        return true;
    }
    bool empty() const { return count == 0; }
    bool full() const { return count == capacity; }
private:
    vector<int> buf;
    size_t head, tail, count, capacity;
};

// 3. Circular Buffer (고정 크기, 오버플로우시 덮어쓰기)
class CircularBuffer {
public:
    CircularBuffer(size_t cap)
        : buf(cap), head(0), tail(0), count(0), capacity(cap) {}
    void push(int val) {
        if (full()) {
            head = (head + 1) % capacity;
            --count;
        }
        buf[tail] = val;
        tail = (tail + 1) % capacity;
        ++count;
    }
    bool pop(int& val) {
        if (empty()) return false;
        val = buf[head];
        head = (head + 1) % capacity;
        --count;
        return true;
    }
    bool empty() const { return count == 0; }
    bool full() const { return count == capacity; }
    size_t size() const { return count; }
private:
    vector<int> buf;
    size_t head, tail, count, capacity;
};

// 4. State Machine (간단한 상태 전이 예제)
enum State { IDLE, RUN, ERROR };
class StateMachine {
public:
    StateMachine() : state(IDLE) {}
    void event_start() {
        if (state == IDLE) state = RUN;
    }
    void event_error() {
        if (state == RUN) state = ERROR;
    }
    void event_reset() {
        if (state == ERROR) state = IDLE;
    }
    State get_state() const { return state; }
private:
    State state;
};

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