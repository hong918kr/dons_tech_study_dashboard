/*
Day 6: 임베디드 개념

주요 초점: 비트 연산, RTOS 기초
핵심 개념: LeetCode 비트 연산 문제 풀이
추천 연습: LeetCode 비트 연산 문제 풀이
목적: 임베디드 시스템에서 자주 쓰이는 비트 연산과 RTOS의 기본 개념을 실습을 통해 익힌다.
*/

#include <iostream>
#include <vector>
#include <cstdint>

/*
1. 비트 반전 (Bit Reverse)
   - 문제: 8비트 unsigned 정수의 비트를 반전시키는 함수를 작성하라.
   - 배움: 비트 시프트와 마스킹
   - 목적: 비트 단위 연산의 기초 이해
*/
uint8_t reverse_bits(uint8_t n) {
    uint8_t res = 0;
    for (int i = 0; i < 8; ++i) {
        res <<= 1;
        res |= (n & 1);
        n >>= 1;
    }
    return res;
}

/*
2. 1의 개수 세기 (Hamming Weight)
   - 문제: 32비트 unsigned 정수에서 1로 설정된 비트의 개수를 반환하라.
   - 배움: 비트 마스킹, Brian Kernighan 알고리즘
   - 목적: 효율적인 비트 카운팅 습득
*/
int hamming_weight(uint32_t n) {
    int count = 0;
    while (n) {
        n &= (n - 1);
        ++count;
    }
    return count;
}

/*
3. 두 수의 비트 차이(해밍 거리)
   - 문제: 두 정수의 비트가 몇 개 다른지 반환하라.
   - 배움: XOR와 hamming weight의 결합
   - 목적: 비트 연산의 실전 활용
*/
int hamming_distance(uint32_t a, uint32_t b) {
    return hamming_weight(a ^ b);
}

/*
4. RTOS-like Task Scheduling (난이도↑)
   - 문제: 간단한 라운드로빈 방식의 태스크 스케줄러를 구현하라.
   - 배움: 임베디드 RTOS의 기본 구조
   - 목적: 태스크 개념과 스케줄링 원리 체험
*/
void task1() { std::cout << "Task 1 running\n"; }
void task2() { std::cout << "Task 2 running\n"; }
void task3() { std::cout << "Task 3 running\n"; }

void simple_scheduler() {
    std::cout << "[simple_scheduler]\n";
    using TaskFunc = void(*)();
    std::vector<TaskFunc> tasks = {task1, task2, task3};
    for (int i = 0; i < 6; ++i) {
        tasks[i % tasks.size()]();
    }
}

int main() {
    std::cout << "=== Day 6: 임베디드 비트 연산 & RTOS 기초 ===\n";

    // 1. 비트 반전
    uint8_t val = 0b11010010;
    std::cout << "[reverse_bits] " << std::hex << (int)val << " -> " << (int)reverse_bits(val) << std::dec << "\n";

    // 2. 1의 개수 세기
    uint32_t num = 0b10110101;
    std::cout << "[hamming_weight] " << num << " -> " << hamming_weight(num) << "\n";

    // 3. 해밍 거리
    uint32_t a = 0b10101010, b = 0b11110000;
    std::cout << "[hamming_distance] " << a << ", " << b << " -> " << hamming_distance(a, b) << "\n";

    // 4. 간단한 RTOS 스케줄러
    simple_scheduler();

    return 0;
}

/*
[실행 결과 예시]
=== Day 6: 임베디드 비트 연산 & RTOS 기초 ===
[reverse_bits] d2 -> 4b
[hamming_weight] 181 -> 5
[hamming_distance] 170, 240 -> 4
[simple_scheduler]
Task 1 running
Task 2 running
Task 3 running
Task 1 running
Task 2 running
Task 3
*/