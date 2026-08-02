/*
Day 8: 종합 실습

주요 초점: 전체 워크플로
핵심 개념: 메시지 큐 문제 풀이 (시간 제한 없음)
추천 연습: 메시지 큐 문제 풀이 (시간 제한 없음)
목적: 스레드 간 안전하게 데이터를 주고받는 메시지 큐를 직접 구현하고, 생산자-소비자 패턴을 실습한다.
*/

#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>

/*
1. 스레드 안전한 메시지 큐 구현
   - 문제: 여러 스레드가 동시에 접근해도 안전하게 동작하는 MessageQueue 클래스를 만든다.
   - 배움: std::mutex, std::condition_variable을 활용한 동기화
   - 목적: 데이터 레이스 없는 안전한 큐 설계
*/
template<typename T>
class MessageQueue {
public:
    void push(const T& value) {
        std::lock_guard<std::mutex> lock(mtx);
        q.push(value);
        cv.notify_one();
    }
    T pop() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]{ return !q.empty(); });
        T value = q.front();
        q.pop();
        return value;
    }
private:
    std::queue<T> q;
    std::mutex mtx;
    std::condition_variable cv;
};

/*
2. 생산자-소비자 패턴 테스트
   - 문제: 생산자 스레드가 메시지를 큐에 넣고, 소비자 스레드가 메시지를 꺼내서 처리한다.
   - 배움: 스레드 간 안전한 통신 및 동기화
   - 목적: 실제 동시성 환경에서의 메시지 큐 활용 경험
*/
void producer(MessageQueue<int>& mq, int count) {
    for (int i = 1; i <= count; ++i) {
        std::cout << "Produced: " << i << "\n";
        mq.push(i);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void consumer(MessageQueue<int>& mq, int count) {
    for (int i = 1; i <= count; ++i) {
        int val = mq.pop();
        std::cout << "Consumed: " << val << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
}

int main() {
    std::cout << "=== Day 8: 메시지 큐 & 종합 실습 ===\n";
    MessageQueue<int> mq;
    int msg_count = 5;

    std::thread prod(producer, std::ref(mq), msg_count);
    std::thread cons(consumer, std::ref(mq), msg_count);

    prod.join();
    cons.join();

    std::cout << "-------------------------\n";
    return 0;
}

/*
[실행 결과 예시]
=== Day 8: 메시지 큐 & 종합 실습 ===
Produced: 1
Consumed: 1
Produced: 2
Consumed: 2
Produced: 3
Consumed: 3
Produced: 4
Consumed: 4
Produced: 5
Consumed: 5
*/