#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>


using namespace std;
// --- 확장형 RingBuffer (멀티스레드 안전, 오버라이트, 임의 접근) ---
class RingBufferMT {
public:
    static constexpr size_t SIZE = 8;
    RingBufferMT(bool overwrite = false)
    :   head(0),
        tail(0),
        count(0),
        overwrite_mode(overwrite) 
    {
        memset(buf, 0, sizeof(buf));
    }

    bool push (uint8_t val)
    {
        lock_guard<mutex> lock(mtx);
        if (is_full())
        {
            
        }
        
    }

    bool pop(uint8_t& val)
    {
        return true;
    }
    
    bool peek(size_t idx, uint8_t& val) const {
        return true;
    }

    bool is_full() const { return count == SIZE; }
    bool is_empty() const { return count == 0; }
    size_t size() const { return count; }
    
    void print() const {
        lock_guard<mutex> lock(mtx);
        cout << "[RingBufferMT] ";
        size_t idx = tail;
        for (size_t i = 0; i < count; ++i)
        {
            cout << (uint8_t)buf[idx] << " ";
            idx = (idx + 1) % SIZE;
        }
        cout << "\n";
    }
private:
    uint8_t buf[SIZE];
    size_t head, tail, count;
    bool overwrite_mode;
    mutable std::mutex mtx;

};