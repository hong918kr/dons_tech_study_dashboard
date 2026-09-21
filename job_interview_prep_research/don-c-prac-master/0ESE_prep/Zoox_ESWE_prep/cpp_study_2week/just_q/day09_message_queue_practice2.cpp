/*
Day 9: 심화 메시지 큐 & 멀티 프로듀서/컨슈머

주요 초점: 멀티 프로듀서-멀티 컨슈머 메시지 큐, graceful shutdown, 타임아웃 pop
핵심 개념: 여러 생산자/소비자가 동시에 메시지를 주고받는 환경, 안전한 종료, 조건 변수의 wait_for
추천 연습: 실전형 메시지 큐 구현 및 다양한 동시성 패턴 실습
목적: 실무에서 사용하는 고급 메시지 큐 패턴을 직접 구현하며, 동시성 문제 해결 능력 강화
*/

#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>
#include <atomic>
#include <optional>

/*
1. 멀티 프로듀서-멀티 컨슈머 메시지 큐 구현
   - 문제: 여러 생산자와 여러 소비자가 동시에 접근해도 안전하게 동작하는 MessageQueue 클래스를 만든다.
   - 배움: std::mutex, std::condition_variable, std::atomic<bool>을 활용한 동기화 및 안전한 종료
   - 목적: 실전에서 자주 쓰이는 동시성 패턴 구현
*/
template<typename T>
class MessageQueue {
public:
    MessageQueue() : closed(false) {}

    void push(const T& value) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            q.push(value);
        }
        cv.notify_one();
    }

    // 타임아웃이 있는 pop (std::optional 사용)
    std::optional<T> pop_for(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mtx);
        if (!cv.wait_for(lock, timeout, [&]{ return !q.empty() || closed; })) {
            return std::nullopt; // timeout
        }
        if (q.empty()) return std::nullopt;
        T value = q.front();
        q.pop();
        return value;
    }

    // 안전한 종료
    void close() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            closed = true;
        }
        cv.notify_all();
    }

    bool is_closed() const {
        return closed;
    }

private:
    std::queue<T> q;
    mutable std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> closed;
};

/*
2. 멀티 프로듀서/컨슈머 테스트 및 graceful shutdown
   - 문제: 여러 생산자와 여러 소비자가 동시에 메시지를 주고받고, 안전하게 종료한다.
   - 배움: 안전한 종료 패턴, 타임아웃 pop, 동시성 환경에서의 robust한 설계
   - 목적: 실전형 메시지 큐 활용 능력 강화
*/
void producer(MessageQueue<int>& mq, int start, int count, int id) {
    for (int i = 0; i < count; ++i) {
        int val = start + i;
        std::cout << "Producer " << id << " produced: " << val << "\n";
        mq.push(val);
        std::this_thread::sleep_for(std::chrono::milliseconds(30 + id * 10));
    }
}

void consumer(MessageQueue<int>& mq, int id) {
    while (true) {
        auto val = mq.pop_for(std::chrono::milliseconds(100));
        if (!val.has_value()) {
            if (mq.is_closed()) break;
            continue;
        }
        std::cout << "Consumer " << id << " consumed: " << *val << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(60 + id * 10));
    }
}

int main() {
    std::cout << "=== Day 9: 심화 메시지 큐 & 멀티 프로듀서/컨슈머 ===\n";
    MessageQueue<int> mq;
    int producer_count = 2;
    int consumer_count = 3;
    int msg_per_producer = 5;

    std::vector<std::thread> producers, consumers;
    for (int i = 0; i < producer_count; ++i)
        producers.emplace_back(producer, std::ref(mq), i * 100, msg_per_producer, i);

    for (int i = 0; i < consumer_count; ++i)
        consumers.emplace_back(consumer, std::ref(mq), i);

    for (auto& t : producers) t.join();

    // 모든 생산자가 끝나면 큐를 닫는다
    mq.close();

    for (auto& t : consumers) t.join();

    std::cout << "-------------------------\n";
    return 0;
}

/*
[실행 결과 예시]
=== Day 9: 심화 메시지 큐 & 멀티 프로듀서/컨슈머 ===
Producer 0 produced: 0
Producer 1 produced: 100
Consumer 0 consumed: 0
Producer 0 produced: 1
Consumer 1 consumed: 100
Producer 1 produced: 101
Consumer 2 consumed:
*/



/*
아래는 Day 9 예제의 sample test case(실행 예시)에 대한 자세한 설명입니다.

[실행 결과 예시]

=== Day 9: 심화 메시지 큐 & 멀티 프로듀서/컨슈머 ===
Producer 0 produced: 0
Producer 1 produced: 100
Consumer 0 consumed: 0
Producer 0 produced: 1
Consumer 1 consumed: 100
Producer 1 produced: 101
Consumer 2 consumed: 1
Producer 0 produced: 2
Consumer 0 consumed: 101
Producer 1 produced: 102
Consumer 1 consumed: 2
Producer 0 produced: 3
Consumer 2 consumed: 102
Producer 1 produced: 103
Consumer 0 consumed: 3
Producer 0 produced: 4
Consumer 1 consumed: 103
Producer 1 produced: 104
Consumer 2 consumed: 4
Consumer 0 consumed: 104
-------------------------


상세 설명
Producer 0 produced: X
Producer 0번 스레드가 메시지 X(0, 1, 2, 3, 4)를 큐에 넣었음을 의미합니다.

Producer 1 produced: Y
Producer 1번 스레드가 메시지 Y(100, 101, 102, 103, 104)를 큐에 넣었음을 의미합니다.

Consumer N consumed: Z
Consumer N번 스레드가 메시지 Z를 큐에서 꺼내 처리했음을 의미합니다.

동작 흐름
    1. 생산자(Producer) 2명이 각각 5개의 메시지를 생성합니다.

        Producer 0: 0, 1, 2, 3, 4
        Producer 1: 100, 101, 102, 103, 104

    2. 소비자(Consumer) 3명이 메시지 큐에서 메시지를 꺼내 처리합니다.

        각 소비자는 메시지가 큐에 들어올 때까지 기다렸다가, 메시지를 꺼내서 처리합니다.
        메시지 처리 순서는 스레드 스케줄링에 따라 달라질 수 있습니다.
    3. 큐가 비어있고, 모든 생산자가 종료되면(큐가 close되면) 소비자 스레드도 종료됩니다.

예시 해설
    Producer 0이 0을 생산 → Consumer 0이 0을 소비
    Producer 1이 100을 생산 → Consumer 1이 100을 소비
    Producer 0이 1을 생산 → Consumer 2가 1을 소비
    Producer 1이 101을 생산 → Consumer 0이 101을 소비
    ...
마지막으로 모든 메시지가 소비되고, 큐가 닫히면 프로그램이 종료됩니다.


추가 참고
실제 실행 시, 각 메시지의 생산/소비 순서와 소비자 번호는 스레드 스케줄링에 따라 달라질 수 있습니다.
각 메시지는 정확히 한 번만 소비됩니다(중복 소비 없음).
모든 생산자가 종료된 후, 큐를 close()하여 소비자들이 안전하게 종료됩니다.
이 예시는 멀티 프로듀서/멀티 컨슈머 환경에서 메시지 큐가 어떻게 동작하는지, 그리고 안전하게 shutdown이 이루어지는지를 보여줍니다.

*/