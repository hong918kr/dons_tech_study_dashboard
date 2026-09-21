#include <iostream>
#include <vector>
#include <cstring>
#include <cassert>
#include <queue>
#include <climits>
using namespace std;

// 1. Reverse a singly linked list (C/C++)
struct ListNode {
    int val;
    ListNode* next;
    ListNode(int v) : val(v), next(nullptr) {}
};
ListNode* reverse_list(ListNode* head);

// 2. Implement a circular buffer (C/C++)
class CircularBuffer {
public:
    CircularBuffer(int cap);
    bool push(int val);
    bool pop(int& val);
    bool empty() const;
    bool full() const;
    int size() const;
private:
    vector<int> buf;
    int head, tail, count, capacity;
};

// 3. Find the first non-repeated character in a string
char first_non_repeated(const char* str);

// 4. Implement your own memcpy/memset
void* my_memcpy(void* dst, const void* src, size_t n);
void* my_memset(void* dst, int val, size_t n);

// 5. Implement a thread-safe queue for ISR and main loop
class ISRQueue {
public:
    ISRQueue(int cap);
    bool push(int val); // ISR에서 호출
    bool pop(int& val); // 메인 루프에서 호출
    bool empty() const;
    bool full() const;
private:
    vector<int> buf;
    int head, tail, count, capacity;
};

// 6. Explain the difference between struct and class in C++
// (설명은 주석으로 작성)

// 7. Implement a function pointer callback mechanism
typedef void (*Callback)(int);
void call_with_callback(int val, Callback cb);

// 8. Find the maximum subarray sum (Kadane’s algorithm)
int max_subarray_sum(const vector<int>& arr);

// 9. Implement a simple finite state machine
enum State { IDLE, RUN, ERROR };
class SimpleFSM {
public:
    SimpleFSM();
    void event_start();
    void event_error();
    void event_reset();
    State get_state() const;
private:
    State state;
};

// 10. Bit manipulation: swap odd/even bits, count set bits
uint32_t swap_odd_even_bits(uint32_t val);
int count_set_bits(uint32_t val);

// =================== 테스트 코드 ===================
int main() {
    // 1. Reverse a singly linked list
    ListNode* n1 = new ListNode(1);
    ListNode* n2 = new ListNode(2);
    ListNode* n3 = new ListNode(3);
    n1->next = n2; n2->next = n3;
    ListNode* rev = reverse_list(n1);
    assert(rev->val == 3 && rev->next->val == 2 && rev->next->next->val == 1 && rev->next->next->next == nullptr);

    // 2. Circular Buffer
    CircularBuffer cb(2);
    assert(cb.push(10));
    assert(cb.push(20));
    assert(!cb.push(30)); // full
    int v;
    assert(cb.pop(v) && v == 10);
    assert(cb.push(30));
    assert(cb.pop(v) && v == 20);
    assert(cb.pop(v) && v == 30);
    assert(cb.empty());

    // 3. First non-repeated character
    assert(first_non_repeated("swiss") == 'w');
    assert(first_non_repeated("aabbcc") == '\0');

    // 4. memcpy/memset
    char buf[10];
    my_memset(buf, 'A', 5);
    assert(strncmp(buf, "AAAAA", 5) == 0);
    char src[5] = "test";
    my_memcpy(buf, src, 5);
    assert(strncmp(buf, "test", 4) == 0);

    // 5. ISR-safe queue
    ISRQueue q(2);
    assert(q.push(1));
    assert(q.push(2));
    assert(!q.push(3));
    assert(q.pop(v) && v == 1);
    assert(q.pop(v) && v == 2);
    assert(!q.pop(v));

    // 6. struct vs class: struct는 기본 public, class는 기본 private (주석 참고)

    // 7. Function pointer callback
    bool called = false;
    auto cb_func = [](int x) { assert(x == 42); };
    call_with_callback(42, cb_func);

    // 8. Maximum subarray sum
    vector<int> arr = {-2, 1, -3, 4, -1, 2, 1, -5, 4};
    assert(max_subarray_sum(arr) == 6); // [4,-1,2,1]

    // 9. Simple FSM
    SimpleFSM fsm;
    assert(fsm.get_state() == IDLE);
    fsm.event_start();
    assert(fsm.get_state() == RUN);
    fsm.event_error();
    assert(fsm.get_state() == ERROR);
    fsm.event_reset();
    assert(fsm.get_state() == IDLE);

    // 10. Bit manipulation
    assert(swap_odd_even_bits(0b10101010) == 0b01010101);
    assert(count_set_bits(0b1011) == 3);

    cout << "All tests PASS" << endl;
    return 0;
}