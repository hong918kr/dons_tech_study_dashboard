
/*


#### **Part 2: C++로 구현하는 임베디드 자료구조 (No `new`, No `std::vector`) (문제 16-35)**
*힙(heap) 사용이 금지된 환경에서 C++의 클래스와 템플릿을 활용하여 안전하고 효율적인 자료구조를 만드는 능력을 봅니다.*

16. **링 버퍼(Ring Buffer) / 원형 큐(Circular Queue):** `uint8_t` 데이터를 저장하는 고정 크기 링 버퍼 클래스를 구현하세요. `push`, `pop`, `is_full`, `is_empty` 메소드를 포함해야 합니다. (Follow-up: 템플릿을 사용하여 어떤 타입이든 저장할 수 있도록 일반화하세요.)
17. **스택(Stack):** 고정 크기 배열을 사용하여 스택 클래스를 구현하세요. `push`, `pop`, `peek`, `is_empty` 메소드를 포함해야 합니다.
18. **큐(Queue):** 고정 크기 배열을 사용하여 큐 클래스를 구현하세요.
19. **우선순위 큐(Priority Queue):** 고정 크기 배열 기반의 최소 힙(min-heap)을 사용하여 간단한 우선순위 큐를 구현하세요.
20. **고정 블록 할당자(Fixed-Block Allocator) / 메모리 풀(Memory Pool):** 특정 크기(예: 32바이트)의 객체들을 할당하고 해제할 수 있는 메모리 풀 클래스를 구현하세요. `allocate`와 `deallocate` 메소드를 포함해야 합니다. 힙을 사용하면 안 됩니다.
21. **연결 리스트(Linked List):** 20번에서 만든 메모리 풀을 사용하여 노드(Node)를 할당하는 침입형(intrusive) 연결 리스트를 구현하세요.
22. **비트맵(Bitmap):** `uint32_t` 배열을 사용하여, 최대 128개의 리소스(예: 태스크 ID)가 사용 중인지 아닌지를 관리하는 비트맵 클래스를 구현하세요. `set`, `clear`, `is_set` 메소드를 포함해야 합니다.
23. **이동 평균 필터(Moving Average Filter):** 최근 N개의 데이터 샘플을 저장하고 평균을 계산하는 클래스를 구현하세요. 새 데이터가 들어올 때마다 효율적으로 평균을 갱신해야 합니다.
24. **메시지 큐(Message Queue):** 여러 개의 작은 데이터 구조체(메시지)를 저장할 수 있는 고정 크기 메시지 큐를 구현하세요. ISR과 메인 루프 간 통신에 사용될 것을 가정합니다.
25. **템플릿 기반 레지스터 접근:** 레지스터 주소와 데이터 타입을 템플릿 인자로 받아, 타입-세이프(type-safe)한 레지스터 읽기/쓰기 함수를 구현하세요.
26. **룩업 테이블(Lookup Table):** `constexpr`를 사용하여 컴파일 타임에 사인(sine) 함수 룩업 테이블을 생성하는 클래스를 구현하세요.
27. **디바운스(Debounce) 로직:** 버튼 입력(HIGH/LOW)을 받아, 채터링(chattering)을 제거하고 안정적인 눌림/뗌 이벤트를 반환하는 디바운스 클래스를 구현하세요.
28. **해시 테이블(Hash Table):** 고정 크기 배열과 간단한 해시 함수를 사용하여, 문자열 키와 정수 값을 매핑하는 해시 테이블을 구현하세요. (충돌 해결: Linear Probing)
29. **객체 풀(Object Pool):** `placement new`를 사용하여 미리 할당된 메모리 버퍼 위에 객체를 생성하고 재사용하는 객체 풀 클래스를 구현하세요.
30. **탈중앙화 카운터(Decentralized Counter):** 여러 모듈에서 접근하더라도 카운터 값이 깨지지 않도록 보장하는 간단한 카운터 클래스를 구현하세요 (원자적 연산 흉내).
31. **가변 길이 데이터 파서:** `uint8_t` 스트림에서 `[시작 바이트][길이][데이터...][체크섬]` 형식의 패킷을 파싱하는 클래스를 구현하세요.
32. **설정(Configuration) 관리자:** EEPROM이나 플래시 메모리에 저장된 설정 값들을 읽고 쓸 수 있는 클래스를 설계하고, 키-값 쌍을 관리하는 메소드를 구현하세요.
33. **소프트웨어 타이머:** 하드웨어 타이머 1개를 이용하여 여러 개의 소프트웨어 타이머(콜백 함수 등록 가능)를 관리하는 타이머 관리자 클래스를 구현하세요.
34. **CMD 프로세서:** 시리얼 통신으로 들어온 문자열 명령어(예: "led on", "motor speed 100")를 파싱하고 등록된 핸들러 함수를 실행하는 간단한 커맨드 프로세서를 구현하세요.
35. **CRC-16 계산기:** 주어진 데이터 버퍼에 대한 CRC-16 체크섬을 계산하는 함수를 구현하세요.


*/

/*
#### Part 2: C++로 구현하는 임베디드 자료구조 (No `new`, No `std::vector`) (문제 16-35)
*힙(heap) 사용이 금지된 환경에서 C++의 클래스와 템플릿을 활용하여 안전하고 효율적인 자료구조를 만드는 능력을 봅니다.*
*/

#include <cstdint>
#include <cstdio>
#include <cstring>

// 16. 링 버퍼(Ring Buffer) / 원형 큐(Circular Queue)
class RingBuffer {
public:
    static constexpr size_t SIZE = 8;
    RingBuffer() : head(0), tail(0), count(0) { std::memset(buf, 0, sizeof(buf)); }
    bool push(uint8_t val) {
        if (is_full()) return false;
        buf[head] = val;
        head = (head + 1) % SIZE;
        ++count;
        return true;
    }
    bool pop(uint8_t& val) {
        if (is_empty()) return false;
        val = buf[tail];
        tail = (tail + 1) % SIZE;
        --count;
        return true;
    }
    bool is_full() const { return count == SIZE; }
    bool is_empty() const { return count == 0; }
private:
    uint8_t buf[SIZE];
    size_t head, tail, count;
};

// 17. 스택(Stack)
class Stack {
public:
    static constexpr size_t SIZE = 8;
    Stack() : top_idx(0) { std::memset(data, 0, sizeof(data)); }
    bool push(uint8_t val) {
        if (top_idx == SIZE) return false;
        data[top_idx++] = val;
        return true;
    }
    bool pop(uint8_t& val) {
        if (top_idx == 0) return false;
        val = data[--top_idx];
        return true;
    }
    bool peek(uint8_t& val) const {
        if (top_idx == 0) return false;
        val = data[top_idx - 1];
        return true;
    }
    bool is_empty() const { return top_idx == 0; }
private:
    uint8_t data[SIZE];
    size_t top_idx;
};

// 18. 큐(Queue)
class Queue {
public:
    static constexpr size_t SIZE = 8;
    Queue() : front(0), rear(0), count(0) { std::memset(data, 0, sizeof(data)); }
    bool enqueue(uint8_t val) {
        if (count == SIZE) return false;
        data[rear] = val;
        rear = (rear + 1) % SIZE;
        ++count;
        return true;
    }
    bool dequeue(uint8_t& val) {
        if (count == 0) return false;
        val = data[front];
        front = (front + 1) % SIZE;
        --count;
        return true;
    }
    bool is_empty() const { return count == 0; }
private:
    uint8_t data[SIZE];
    size_t front, rear, count;
};

// 19. 우선순위 큐(Priority Queue, Min-Heap)
class MinHeap {
public:
    static constexpr size_t SIZE = 8;
    MinHeap() : sz(0) { std::memset(data, 0, sizeof(data)); }
    bool push(uint8_t val) {
        if (sz == SIZE) return false;
        data[sz] = val;
        size_t i = sz++;
        while (i > 0 && data[(i-1)/2] > data[i]) {
            std::swap(data[i], data[(i-1)/2]);
            i = (i-1)/2;
        }
        return true;
    }
    bool pop(uint8_t& val) {
        if (sz == 0) return false;
        val = data[0];
        data[0] = data[--sz];
        size_t i = 0;
        while (true) {
            size_t l = 2*i+1, r = 2*i+2, smallest = i;
            if (l < sz && data[l] < data[smallest]) smallest = l;
            if (r < sz && data[r] < data[smallest]) smallest = r;
            if (smallest == i) break;
            std::swap(data[i], data[smallest]);
            i = smallest;
        }
        return true;
    }
    bool is_empty() const { return sz == 0; }
private:
    uint8_t data[SIZE];
    size_t sz;
};

// 22. 비트맵(Bitmap)
class Bitmap128 {
public:
    Bitmap128() { std::memset(bits, 0, sizeof(bits)); }
    void set(size_t idx)   { bits[idx/32] |= (1U << (idx%32)); }
    void clear(size_t idx) { bits[idx/32] &= ~(1U << (idx%32)); }
    bool is_set(size_t idx) const { return (bits[idx/32] >> (idx%32)) & 1U; }
private:
    uint32_t bits[4]; // 4*32 = 128
};

// 23. 이동 평균 필터(Moving Average Filter)
class MovingAverage {
public:
    static constexpr size_t N = 4;
    MovingAverage() : sum(0), idx(0), count(0) { std::memset(buf, 0, sizeof(buf)); }
    void add(int val) {
        if (count < N) {
            buf[idx] = val;
            sum += val;
            ++count;
        } else {
            sum -= buf[idx];
            buf[idx] = val;
            sum += val;
        }
        idx = (idx + 1) % N;
    }
    double average() const {
        return count ? static_cast<double>(sum) / count : 0.0;
    }
private:
    int buf[N];
    int sum;
    size_t idx, count;
};

// 24. 메시지 큐(Message Queue)
struct Message {
    uint8_t id;
    uint16_t data;
};
class MessageQueue {
public:
    static constexpr size_t SIZE = 4;
    MessageQueue() : head(0), tail(0), count(0) { std::memset(buf, 0, sizeof(buf)); }
    bool push(const Message& msg) {
        if (count == SIZE) return false;
        buf[head] = msg;
        head = (head + 1) % SIZE;
        ++count;
        return true;
    }
    bool pop(Message& msg) {
        if (count == 0) return false;
        msg = buf[tail];
        tail = (tail + 1) % SIZE;
        --count;
        return true;
    }
    bool is_empty() const { return count == 0; }
private:
    Message buf[SIZE];
    size_t head, tail, count;
};

// 25. 템플릿 기반 레지스터 접근
template<uintptr_t ADDR, typename T>
struct Register {
    static void write(T value) {
        *reinterpret_cast<volatile T*>(ADDR) = value;
    }
    static T read() {
        return *reinterpret_cast<volatile T*>(ADDR);
    }
};

// --- Test code ---
int main() {
    printf("=== Part2 Embedded Data Structures Test ===\n");

    // 16. RingBuffer
    RingBuffer rb;
    for (uint8_t i = 1; i <= 8; ++i) rb.push(i);
    uint8_t val = 0;
    printf("16. RingBuffer pop: ");
    while (rb.pop(val)) printf("%u ", val);
    printf("\n");

    // 17. Stack
    Stack stk;
    for (uint8_t i = 1; i <= 5; ++i) stk.push(i);
    printf("17. Stack pop: ");
    while (stk.pop(val)) printf("%u ", val);
    printf("\n");

    // 18. Queue
    Queue q;
    for (uint8_t i = 1; i <= 5; ++i) q.enqueue(i);
    printf("18. Queue dequeue: ");
    while (q.dequeue(val)) printf("%u ", val);
    printf("\n");

    // 19. MinHeap
    MinHeap heap;
    heap.push(5); heap.push(3); heap.push(7); heap.push(1);
    printf("19. MinHeap pop: ");
    while (heap.pop(val)) printf("%u ", val);
    printf("\n");

    // 22. Bitmap128
    Bitmap128 bm;
    bm.set(5); bm.set(64); bm.set(127);
    printf("22. Bitmap128: ");
    printf("5:%d 64:%d 127:%d 0:%d\n", bm.is_set(5), bm.is_set(64), bm.is_set(127), bm.is_set(0));

    // 23. MovingAverage
    MovingAverage ma;
    ma.add(10); ma.add(20); ma.add(30); ma.add(40);
    printf("23. MovingAverage avg: %.2f\n", ma.average());
    ma.add(50);
    printf("23. MovingAverage avg after add 50: %.2f\n", ma.average());

    // 24. MessageQueue
    MessageQueue mq;
    mq.push({1, 100});
    mq.push({2, 200});
    Message msg;
    printf("24. MessageQueue pop: ");
    while (mq.pop(msg)) printf("[id=%u,data=%u] ", msg.id, msg.data);
    printf("\n");

    // 25. Register (실제 하드웨어 환경에서만 동작)
    // Register<0x40000000, uint32_t>::write(0x12345678);
    // uint32_t regval = Register<0x40000000, uint32_t>::read();