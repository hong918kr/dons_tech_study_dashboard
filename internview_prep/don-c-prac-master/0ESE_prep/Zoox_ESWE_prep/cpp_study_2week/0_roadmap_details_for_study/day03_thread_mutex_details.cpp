/*
Day 3: 동시성 기초

주요 초점: std::thread, std::mutex, std::condition_variable
핵심 개념: 간단한 생산자-소비자 프로그램 작성
목적: C++에서 스레드와 동기화 도구의 기본 사용법을 익히고, 안전하게 데이터를 공유하는 방법을 실습한다.
*/

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>

/*
1. std::thread 기본 사용
   - 문제: 두 개의 스레드를 생성하여 각각 다른 메시지를 출력해본다.
   - 배움: 스레드 생성과 join의 기본
   - 목적: 멀티스레드 프로그램의 구조 이해
*/
void thread_basic() {
    std::cout << "[thread_basic]\n";
    auto func1 = []() { std::cout << "Thread 1 running\n"; };
    auto func2 = []() { std::cout << "Thread 2 running\n"; };
    std::thread t1(func1);
    std::thread t2(func2);
    t1.join();
    t2.join();
}

/*
2. std::mutex로 임계영역 보호
   - 문제: 여러 스레드가 동시에 하나의 카운터를 증가시킬 때, mutex로 경쟁 조건을 방지한다.
   - 배움: mutex lock/unlock의 필요성
   - 목적: 데이터 레이스 방지
*/
void mutex_counter_test() {
    std::cout << "[mutex_counter_test]\n";
    int counter = 0;
    std::mutex mtx;
    auto increment = [&]() {
        for (int i = 0; i < 10000; ++i) {
            std::lock_guard<std::mutex> lock(mtx);
            ++counter;
        }
    };
    std::thread t1(increment);
    std::thread t2(increment);
    t1.join();
    t2.join();
    std::cout << "Final counter: " << counter << "\n";
}

/*
3. std::condition_variable로 생산자-소비자 구현
   - 문제: 한 스레드는 데이터를 생산하고, 다른 스레드는 데이터를 소비하는 구조를 만든다.
   - 배움: 조건 변수로 스레드 간 동기화
   - 목적: 안전한 생산자-소비자 패턴 구현
*/
void producer_consumer_test() {
    std::cout << "[producer_consumer_test]\n";
    std::queue<int> q;
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

    // 생산자
    auto producer = [&]() {
        for (int i = 1; i <= 5; ++i) {
            {
                std::lock_guard<std::mutex> lock(mtx);
                q.push(i);
                std::cout << "Produced: " << i << "\n";
            }
            cv.notify_one();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        {
            std::lock_guard<std::mutex> lock(mtx);
            done = true;
        }
        cv.notify_one();
    };

    // 소비자
    auto consumer = [&]() {
        while (true) {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&]() { return !q.empty() || done; });
            while (!q.empty()) {
                int val = q.front();
                q.pop();
                std::cout << "Consumed: " << val << "\n";
            }
            if (done) break;
        }
    };

    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join();
    t2.join();
}

int main() {
    std::cout << "=== Day 3: 동시성 기초 ===\n";
    thread_basic();
    std::cout << "-------------------------\n";
    mutex_counter_test();
    std::cout << "-------------------------\n";
    producer_consumer_test();
    std::cout << "-------------------------\n";
    return 0;
}

/*
[실행 결과 예시]
=== Day 3: 동시성 기초 ===
[thread_basic]
Thread 1 running
Thread 2 running
-------------------------
[mutex_counter_test]
Final counter: 20000
-------------------------
[producer_consumer_test]
Produced: 1
Consumed: 1
Produced: 2
Consumed: 2
Produced: 3
Consumed: 3
Produced: 4
Consumed: 4
Produced: 5
*/