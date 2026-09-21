/*
Day 4: Google Test

주요 초점: TEST_F, ASSERT_*, EXPECT_*
핵심 개념: Calculator 클래스 단위 테스트 작성
목적: C++에서 단위 테스트 프레임워크(Google Test)의 기본 사용법을 익히고, 테스트 가능한 코드를 작성하는 습관을 기른다.
*/

#include <iostream>

// 1. Calculator 클래스 구현
/*
1. Calculator 클래스 구현
   - 문제: 사칙연산(add, sub, mul, div) 메서드를 가진 Calculator 클래스를 만든다.
   - 배움: 테스트 가능한 클래스를 설계하는 방법
   - 목적: 단위 테스트 대상 코드 준비
*/
class Calculator {
public:
    int add(int a, int b) { return a + b; }
    int sub(int a, int b) { return a - b; }
    int mul(int a, int b) { return a * b; }
    int div(int a, int b) { return b != 0 ? a / b : 0; }
};

/*
2. Google Test로 단위 테스트 작성
   - 문제: Calculator의 각 메서드에 대해 TEST_F를 사용해 단위 테스트를 작성한다.
   - 배움: ASSERT_EQ, EXPECT_EQ 등 Google Test의 기본 사용법
   - 목적: 테스트 코드 작성 및 자동 검증 습관
*/
// 아래 코드는 gtest 프레임워크가 필요합니다.
// g++ -std=c++17 day04_gtest_calculator.cpp -lgtest -lpthread 등으로 빌드해야 합니다.

#ifdef UNIT_TEST
#include <gtest/gtest.h>

class CalculatorTest : public ::testing::Test {
protected:
    Calculator calc;
};

TEST_F(CalculatorTest, Add) {
    EXPECT_EQ(calc.add(2, 3), 5);
    EXPECT_EQ(calc.add(-1, 1), 0);
}

TEST_F(CalculatorTest, Sub) {
    EXPECT_EQ(calc.sub(5, 3), 2);
    EXPECT_EQ(calc.sub(0, 1), -1);
}

TEST_F(CalculatorTest, Mul) {
    EXPECT_EQ(calc.mul(4, 3), 12);
    EXPECT_EQ(calc.mul(-2, 3), -6);
}

TEST_F(CalculatorTest, Div) {
    EXPECT_EQ(calc.div(10, 2), 5);
    EXPECT_EQ(calc.div(5, 0), 0); // 0으로 나누기 방지
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#else
// gtest 환경이 아닐 때는 간단히 main에서 직접 테스트
int main() {
    Calculator calc;
    std::cout << "=== Day 4: Google Test & Calculator ===\n";
    std::cout << "add(2, 3) = " << calc.add(2, 3) << "\n";
    std::cout << "sub(5, 3) = " << calc.sub(5, 3) << "\n";
    std::cout << "mul(4, 3) = " << calc.mul(4, 3) << "\n";
    std::cout << "div(10, 2) = " << calc.div(10, 2) << "\n";
    std::cout << "div(5, 0) = " << calc.div(5, 0) << "\n";
    return 0;
}
#endif

/*
[실행 결과 예시]
=== Day 4: Google Test & Calculator ===
add(2, 3) = 5
sub(5, 3) = 2
mul(4, 3) = 12
div(10, 2) = 5
div(5, 0) = 0
*/