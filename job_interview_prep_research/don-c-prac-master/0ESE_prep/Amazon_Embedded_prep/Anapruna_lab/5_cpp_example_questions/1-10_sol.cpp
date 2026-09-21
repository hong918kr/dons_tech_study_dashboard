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
ListNode* reverse_list(ListNode* head) {
    ListNode* prev = nullptr;
    while (head) {
        ListNode* next = head->next;
        head->next = prev;
        prev = head;
        head = next;
    }
    return prev;
}

// 2. Implement a circular buffer (C/C++)
class CircularBuffer {
public:
    CircularBuffer(int cap) : buf(cap), head(0), tail(0), count(0), capacity(cap) {}
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
    int size() const { return count; }
private:
    vector<int> buf;
    int head, tail, count, capacity;
};

// 3. Find the first non-repeated character in a string
char first_non_repeated(const char* str) {
    int cnt[256] = {0};
    for (int i = 0; str[i]; ++i) cnt[(unsigned char)str[i]]++;
    for (int i = 0; str[i]; ++i)
        if (cnt[(unsigned char)str[i]] == 1) return str[i];
    return '\0';
}

// 4. Implement your own memcpy/memset
void* my_memcpy(void* dst, const void* src, size_t n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}
void* my_memset(void* dst, int val, size_t n) {
    char* d = (char*)dst;
    for (size_t i = 0; i < n; ++i) d[i] = (char)val;
    return dst;
}

// 5. Implement a thread-safe queue for ISR and main loop
class ISRQueue {
public:
    ISRQueue(int cap) : buf(cap), head(0), tail(0), count(0), capacity(cap) {}
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
    int head, tail, count, capacity;
};

// 6. Explain the difference between struct and class in C++
// struct는 기본 접근 지정자가 public, class는 private입니다.

// 7. Implement a function pointer callback mechanism
typedef void (*Callback)(int);
void call_with_callback(int val, Callback cb) {
    cb(val);
}

// 8. Find the maximum subarray sum (Kadane’s algorithm)
int max_subarray_sum(const vector<int>& arr) {
    int max_sum = INT_MIN, cur = 0;
    for (int v : arr) {
        cur = max(v, cur + v);
        max_sum = max(max_sum, cur);
    }
    return max_sum;
}

// 9. Implement a simple finite state machine
enum State { IDLE, RUN, ERROR };
class SimpleFSM {
public:
    SimpleFSM() : state(IDLE) {}
    void event_start() { if (state == IDLE) state = RUN; }
    void event_error() { if (state == RUN) state = ERROR; }
    void event_reset() { if (state == ERROR) state = IDLE; }
    State get_state() const { return state; }
private:
    State state;
};

// 10. Bit manipulation: swap odd/even bits, count set bits
uint32_t swap_odd_even_bits(uint32_t val) {
    return ((val & 0xAAAAAAAA) >> 1) | ((val & 0x55555555) << 1);
}
int count_set_bits(uint32_t val) {
    int cnt = 0;
    while (val) {
        cnt += val & 1;
        val >>= 1;
    }
    return cnt;
}

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