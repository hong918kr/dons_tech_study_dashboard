#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>

// --- 확장형 RingBuffer (멀티스레드 안전, 오버라이트, 임의 접근) ---
class RingBufferMT {
public:
/*
    static 
        - 클래스의 모든 객체가 공유하는 정적 멤버이다.
        - 객체를 만들지 않아도 RingBufferMT::SIZE로 접근가능하다.
    constexpr
        - 컴파일 타임에 값이 결정되는 상수이다.
        - 최적화와 타입 안정성에 유리하다
    size_t SIZE = 8 
        - 버퍼의 크기를 8로 고정한다.
*/
    static constexpr size_t SIZE = 8;


/*
    생성자
        - 객체가 생성될때 호출되는 함수이다.
        - bool overwrite = false 는 기본 인자(default arguement)로,
        인자를 생략하면 false가 들어간다.

        - 초기화 리스트 (: head(0), tail(0), count(0)
        , overwrite_mode(overwrite))
            - 멤버 변수를 생성과 동시에 초기화한다.
            head
*/
    RingBufferMT(bool overwrite = false)
        : head(0), tail(0), count(0), overwrite_mode(overwrite) {
        std::memset(buf, 0, sizeof(buf));
    }


/*
cpp에서, std::lock_guard<std::mutex> lock(mtx)를 사용하면,
unlock 을 직접 호출하지 않아도 됩니다. 

이유, 
    - std::lock_guard는 생성될때 (mutex를) lock 하고,
    해당 블록(scope) 을 벗어나면 자동으로 unlock한다.
    - 이것이 RAII 패턴이다. 
    - 예외가 발생하거나 return으로 빠져나가도, lock_guard객체가 소멸될때, 
    unlock 이 자동으로 호출된다.
    - lock_guard를 사용하는것이 cpp에서 안전하고 권장되는 방식이다.

*/


    // 멀티스레드 안전 push
    bool push(uint8_t val) {
        std::lock_guard<std::mutex> lock(mtx);
        if (is_full()) {
            if (overwrite_mode) {
                // 오버라이트: 가장 오래된 데이터 덮어쓰기
                buf[head] = val;
                head = (head + 1) % SIZE;
                tail = (tail + 1) % SIZE; // tail도 같이 이동
                // count는 그대로 유지
                return true;
            } else {
                return false;
            }
        }
        buf[head] = val;
        head = (head + 1) % SIZE;
        ++count;
        return true;
    }

    // 멀티스레드 안전 pop
    bool pop(uint8_t& val) {
        std::lock_guard<std::mutex> lock(mtx);
        if (is_empty()) return false;
        val = buf[tail];
        tail = (tail + 1) % SIZE;
        --count;
        return true;
    }

    // 임의 위치 접근 (디버깅용)
    bool peek(size_t idx, uint8_t& val) const {
        std::lock_guard<std::mutex> lock(mtx);
        if (idx >= count) return false;
        size_t real_idx = (tail + idx) % SIZE;
        val = buf[real_idx];
        return true;
    }

    bool is_full() const { return count == SIZE; }
    bool is_empty() const { return count == 0; }
    size_t size() const { return count; }

    void print() const {
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "[RingBufferMT] ";
        size_t idx = tail;
        for (size_t i = 0; i < count; ++i) {
            std::cout << (int)buf[idx] << " ";
            idx = (idx + 1) % SIZE;
        }
        std::cout << "\n";
    }

private:
    uint8_t buf[SIZE];
    size_t head, tail, count;
    bool overwrite_mode;
    mutable std::mutex mtx;
};

// --- UART 수신 버퍼 예시 ---
class MockUART {
public:
    MockUART() : rxbuf(true) {} // 오버라이트 모드로 생성

    // UART 인터럽트에서 호출 (멀티스레드 환경 가정)
    void isr_receive(uint8_t byte) {
        rxbuf.push(byte);
    }

    // 메인 루프에서 호출
    bool read_byte(uint8_t& byte) {
        return rxbuf.pop(byte);
    }

    void debug_print() const { rxbuf.print(); }

    // 디버깅: 최근 n개 데이터 확인
    void debug_peek_all() const {
        std::cout << "[UART RX Peek] ";
        for (size_t i = 0; i < rxbuf.size(); ++i) {
            uint8_t v;
            if (rxbuf.peek(i, v)) std::cout << (int)v << " ";
        }
        std::cout << "\n";
    }

private:
    RingBufferMT rxbuf;
};

// --- 테스트 코드 ---
int main() {
    std::cout << "=== 확장형 RingBuffer & UART 예제 ===\n";

    // 1. 오버라이트 모드 테스트
    RingBufferMT rb(true); // overwrite mode
    for (uint8_t i = 1; i <= 10; ++i) {
        rb.push(i * 10);
        rb.print();
    }
    std::cout << "오버라이트 모드: 가장 오래된 데이터가 덮어써짐\n";

    // 2. 임의 위치 접근(peek)
    std::cout << "임의 위치 peek: ";
    for (size_t i = 0; i < rb.size(); ++i) {
        uint8_t v;
        if (rb.peek(i, v)) std::cout << (int)v << " ";
    }
    std::cout << "\n";

    // 3. 멀티스레드 안전성 테스트
    RingBufferMT mtbuf;
    std::atomic<bool> done{false};
    std::thread producer([&]() {
        for (uint8_t i = 1; i <= 20; ++i) {
            mtbuf.push(i);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        done = true;
    });
    std::thread consumer([&]() {
        uint8_t v;
        while (!done || !mtbuf.is_empty()) {
            if (mtbuf.pop(v)) {
                std::cout << "Pop: " << (int)v << "\n";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    });
    producer.join();
    consumer.join();

    // 4. UART 수신 버퍼 예시
    MockUART uart;
    // ISR에서 데이터 수신 (멀티스레드 시뮬레이션)
    std::thread uart_isr([&]() {
        for (uint8_t i = 100; i < 110; ++i) {
            uart.isr_receive(i);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    // 메인 루프에서 데이터 읽기
    std::thread uart_main([&]() {
        uint8_t v;
        for (int cnt = 0; cnt < 12; ++cnt) {
            if (uart.read_byte(v)) {
                std::cout << "UART Read: " << (int)v << "\n";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(12));
        }
        uart.debug_print();
        uart.debug_peek_all();
    });
    uart_isr.join();
    uart_main.join();

    std::cout << "=== End of Test ===\n";
    return 0;
}

/*
-------------------------------
[스터디/분석/설명]

**확장 아이디어 구현**
1. 멀티스레드 안전: std::mutex로 push/pop/peek 보호, 여러 스레드에서 동시에 접근 가능.
2. 오버라이트(Overwrite) 모드: 가득 찼을 때 push하면 가장 오래된 데이터가 자동으로 덮어써짐(tail도 이동).
3. 임의 위치 접근(peek): tail 기준으로 n번째 데이터를 읽을 수 있어 디버깅에 유용.

**UART 예시**
- UART 인터럽트에서 수신 데이터를 push, 메인 루프에서 pop.
- 오버라이트 모드라 오래된 데이터가 자동으로 사라짐(실제 UART RX 버퍼와 유사).
- 디버깅용으로 전체 버퍼 상태와 임의 위치 데이터도 확인 가능.

**멀티스레드 테스트**
- producer/consumer 스레드로 동시에 push/pop 동작을


*/




/*

C++ 에서 std::lock_guard 외에도 다양한 동기화 도구가 있다. 

1. std::lock_guard
    - 용도: 단순한 임계영역 보호 (뮤텍스 lock / unlock 자동 관리)
    - 특징: Scope기반, 예외 안전, unlock직접 호출 불필요
    - 예시
        std::lock_guard<std::mutex> lock(mtx);

2. std::unique_lock
    - 용도: lock/unlock을 더 세밀하게 제어하거나, condition_variable과함께사용
    - 특징: lock해제/재획득, 지연(lock later), 이동 가능
    - 예시
        std::unique_lock<std::mutex> lock(mtx);
        // ... 필요하면 lock.unlock(), lock.lock() 가능

3. std::scoped_lock (C++17~)
    - 용도: 여러 뮤텍스를 동시에 lock (데드락 방지)
    - 특징: 여러 mutex를 한번에 안전하게 lock
    - 예시
    std::scoped_lock lock(mtx1, mtx2);

4. std::condition_variable
    용도: 스레드 간 "이벤트/상태 변화"를 기다릴 때 (예: 생산자-소비자 패턴)
    특징: unique_lock과 함께 사용, wait/notify
    예시:
        std::mutex mtx;
        std::condition_variable cv;
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, []{ return ready; });
        // 다른 스레드에서 cv.notify_one();

5. std::semaphore (C++20~)
용도: 지정된 개수만큼 동시 접근 허용 (ex: 리소스 제한)
특징: 카운팅 세마포어, acquire/release
예시:

    #include <semaphore>
    std::counting_semaphore<3> sem(3);
    sem.acquire();
    // ... 임계영역 ...
    sem.release();


6. std::shared_mutex (C++17~)
    용도: 여러 reader, 하나의 writer (reader-writer lock)
    특징: shared_lock(읽기), unique_lock(쓰기)
    예시:


    std::shared_mutex smtx;
    std::shared_lock lock(smtx); // 여러 reader
    std::unique_lock lock2(smtx); // writer

요약
    lock_guard: 가장 간단, 스코프 기반 뮤텍스 보호
    unique_lock: 더 유연, condition_variable과 함께 사용
    condition_variable: 스레드 간 신호/이벤트 대기
    semaphore: 동시 접근 제한(리소스 개수 제한)
    shared_mutex: 읽기-쓰기 분리
    scoped_lock: 여러 뮤텍스 동시 lock
        

    상황에 따라 적절한 동기화 도구를 선택해서 사용하면 됩니다!
    특히, 생산자-소비자 패턴이나 이벤트 대기에는 condition_variable,
    리소스 제한에는 semaphore,
    여러 reader/하나의 writer에는 shared_mutex가 많이 쓰입니다.

*/