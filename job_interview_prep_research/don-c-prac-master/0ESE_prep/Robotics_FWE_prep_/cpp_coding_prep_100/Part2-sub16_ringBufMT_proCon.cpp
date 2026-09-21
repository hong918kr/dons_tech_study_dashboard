/*
네, 가능합니다!
생산자-소비자 패턴은 하나 이상의 생산자(Producer)가 데이터를 버퍼에 넣고,
하나 이상의 소비자(Consumer)가 버퍼에서 데이터를 꺼내는 구조입니다.
이때 std::condition_variable을 사용하면,
버퍼가 비었을 때 소비자가 기다리고,
버퍼가 가득 찼을 때 생산자가 기다릴 수 있습니다.

아래는 RingBufferMT를 활용한 생산자-소비자 패턴 예제입니다.

설명
std::condition_variable을 사용해,
버퍼가 가득 차면 생산자는 대기(not_full.wait)
버퍼가 비면 소비자는 대기(not_empty.wait)
생산자가 데이터를 넣으면 소비자에게 알림(not_empty.notify_one)
소비자가 데이터를 꺼내면 생산자에게 알림(not_full.notify_one)
이 구조는 데이터 손실 없이 안전하게 동작합니다.
이렇게 하면 RingBufferMT를 생산자-소비자 패턴에 완벽하게 활용할 수 있습니다!

*/
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>
#include <cstdint>
#include <cstring>

// 멀티스레드 안전 RingBuffer (overwrite는 사용하지 않음)
class RingBufferMT {
public:
    static constexpr size_t SIZE = 8;
    RingBufferMT() : head(0), tail(0), count(0) {
        std::memset(buf, 0, sizeof(buf));
    }

    // 생산자: 데이터 추가 (버퍼가 가득 차면 대기)
    void push(uint8_t val) {
        std::unique_lock<std::mutex> lock(mtx);
        not_full.wait(lock, [this](){ return count < SIZE; });
        buf[head] = val;
        head = (head + 1) % SIZE;
        ++count;
        lock.unlock();
        not_empty.notify_one();
    }

    // 소비자: 데이터 꺼내기 (버퍼가 비면 대기)
    uint8_t pop() {
        std::unique_lock<std::mutex> lock(mtx);
        not_empty.wait(lock, [this](){ return count > 0; });
        uint8_t val = buf[tail];
        tail = (tail + 1) % SIZE;
        --count;
        lock.unlock();
        not_full.notify_one();
        return val;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mtx);
        return count;
    }

private:
    uint8_t buf[SIZE];
    size_t head, tail, count;
    mutable std::mutex mtx;
    std::condition_variable not_full, not_empty;
};

// --- 생산자-소비자 테스트 코드 ---
int main() {
    RingBufferMT rb;

    // 생산자 스레드
    std::thread producer([&]() {
        for (uint8_t i = 1; i <= 20; ++i) {
            rb.push(i);
            std::cout << "[Producer] Pushed: " << (int)i << " (size=" << rb.size() << ")\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    });

    // 소비자 스레드
    std::thread consumer([&]() {
        for (int i = 0; i < 20; ++i) {
            uint8_t val = rb.pop();
            std::cout << "    [Consumer] Popped: " << (int)val << " (size=" << rb.size() << ")\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }
    });

    producer.join();
    consumer.join();

    std::cout << "=== 생산자-소비자 패턴 테스트 종료 ===\n";
    return 0;
}