/*
Day 7: 종합 복습

주요 초점: 1주차 전체
핵심 개념: 스레드 안전한 카운터 클래스 + gtest + CMake
추천 연습: 스레드 안전한 카운터 클래스 + gtest + CMake
목적: 멀티스레드 환경에서 안전하게 동작하는 클래스를 직접 구현하고, 단위 테스트 및 빌드 자동화까지 실습한다.
*/

#include <iostream>
#include <thread>
#include <vector>
#include <mutex>

/*
1. 스레드 안전한 카운터 클래스 구현
   - 문제: 여러 스레드가 동시에 접근해도 안전하게 동작하는 Counter 클래스를 만든다.
   - 배움: std::mutex를 활용한 임계영역 보호
   - 목적: 데이터 레이스 없는 클래스 설계
*/
class ThreadSafeCounter {
public:
    ThreadSafeCounter() : value(0) {}
    void increment() {
        std::lock_guard<std::mutex> lock(mtx);
        ++value;
    }
    int get() const {
        std::lock_guard<std::mutex> lock(mtx);
        return value;
    }
private:
    mutable std::mutex mtx;
    int value;
};

/*
2. 멀티스레드 환경에서 카운터 테스트
   - 문제: 여러 스레드가 동시에 increment()를 호출할 때 최종 값이 예상대로 나오는지 확인한다.
   - 배움: 멀티스레드 환경에서의 동기화 필요성
   - 목적: 실제 동시성 환경에서의 안전성 검증
*/
void thread_increment(ThreadSafeCounter& counter, int times) {
    for (int i = 0; i < times; ++i) {
        counter.increment();
    }
}

/*
3. (선택) Google Test로 단위 테스트 작성
   - 문제: gtest를 활용해 Counter의 동작을 자동 검증한다.
   - 배움: 단위 테스트 프레임워크 활용법
   - 목적: 코드의 신뢰성 향상
   - (아래 코드는 gtest 환경에서만 활성화)
*/
#ifdef UNIT_TEST
#include <gtest/gtest.h>
TEST(ThreadSafeCounterTest, MultiThreadedIncrement) {
    ThreadSafeCounter counter;
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back(thread_increment, std::ref(counter), 10000);
    for (auto& t : threads) t.join();
    EXPECT_EQ(counter.get(), 40000);
}
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#else
int main() {
    std::cout << "=== Day 7: 스레드 안전 카운터 & 종합 복습 ===\n";
    ThreadSafeCounter counter;
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back(thread_increment, std::ref(counter), 10000);
    for (auto& t : threads) t.join();
    std::cout << "Final counter: " << counter.get() << "\n";
    return 0;
}
#endif

/*
[실행 결과 예시]
=== Day 7: 스레드 안전 카운터 & 종합 복습 ===
Final counter: 40000
*/