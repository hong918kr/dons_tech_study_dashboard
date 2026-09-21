/*
[4일차: Google Test(gTest) 기초 실습 Step by Step]

Google Test(gTest)는 C++ 단위 테스트 프레임워크입니다.
아래 코드는 gTest의 기본 개념과 사용법을 단계별로 익힐 수 있도록 구성되어 있습니다.

- gTest의 주요 특징:
  * TEST, TEST_F, ASSERT_*, EXPECT_* 등 다양한 매크로 제공
  * 테스트 자동 실행 및 결과 리포트
  * 예외, 조건, 비교 등 다양한 검증 지원

- 빌드 예시:
  g++ -std=c++17 -lgtest -lgtest_main -pthread day04_gtest_calculator_details.cpp -o gtest_sample
  또는 CMake로 빌드하세요.
*/

#include <gtest/gtest.h>
#include <stdexcept>

// Step 1. Google Test 기본 테스트
// TEST(테스트케이스이름, 테스트이름) 형태로 작성
TEST(BasicTest, SimpleAssertions) {
    // EXPECT_EQ: 두 값이 같은지 확인 (실패해도 계속 진행)
    EXPECT_EQ(1 + 1, 2);
    // ASSERT_TRUE: 조건이 참인지 확인 (실패 시 해당 테스트 즉시 종료)
    ASSERT_TRUE(3 > 2);
}

// Step 2. Calculator 클래스 구현
// 테스트 대상이 되는 간단한 계산기 클래스
class Calculator {
public:
    int add(int a, int b) const { return a + b; }
    int sub(int a, int b) const { return a - b; }
    int mul(int a, int b) const { return a * b; }
    int div(int a, int b) const {
        if (b == 0) throw std::invalid_argument("divide by zero");
        return a / b;
    }
};

// Step 3. Calculator 테스트 픽스처 정의
// 여러 테스트에서 공통으로 사용할 Calculator 인스턴스를 준비
class CalculatorTest : public ::testing::Test {
protected:
    Calculator calc;
};

// Step 4. 각 메서드에 대해 TEST_F로 단위 테스트 작성
TEST_F(CalculatorTest, Add) {
    EXPECT_EQ(calc.add(2, 3), 5);
    EXPECT_EQ(calc.add(-1, 1), 0);
}

TEST_F(CalculatorTest, Subtract) {
    EXPECT_EQ(calc.sub(5, 3), 2);
    EXPECT_EQ(calc.sub(0, 1), -1);
}

TEST_F(CalculatorTest, Multiply) {
    EXPECT_EQ(calc.mul(4, 3), 12);
    EXPECT_EQ(calc.mul(-2, 2), -4);
}

TEST_F(CalculatorTest, Divide) {
    EXPECT_EQ(calc.div(10, 2), 5);
    EXPECT_EQ(calc.div(-6, 3), -2);
}

// Step 5. 예외 상황 테스트 (0으로 나누기)
// EXPECT_THROW(함수, 예외타입): 해당 함수가 예외를 던지는지 확인
TEST_F(CalculatorTest, DivideByZeroThrows) {
    EXPECT_THROW(calc.div(1, 0), std::invalid_argument);
}

// Step 6. 다양한 gTest 매크로 사용 예시
TEST_F(CalculatorTest, VariousMacros) {
    ASSERT_EQ(calc.add(1, 2), 3);         // 같음
    EXPECT_NE(calc.sub(5, 2), 10);        // 다름
    EXPECT_GE(calc.mul(2, 3), 6);         // 크거나 같음
    EXPECT_LE(calc.div(8, 2), 4);         // 작거나 같음
    EXPECT_TRUE(calc.add(0, 0) == 0);     // 참
    EXPECT_FALSE(calc.sub(1, 2) == 2);    // 거짓
}

// Step 7. main 함수는 생략 가능 (gtest_main 링크 시 자동 실행)
// int main(int argc, char** argv) {
//     ::testing::InitGoogleTest(&argc, argv);
//     return RUN_ALL_TESTS();
// }

/*
실행 방법:
1. g++ -std=c++17 -lgtest -lgtest_main -pthread day04_gtest_calculator_details.cpp -o gtest_sample
2. ./gtest_sample
3. 모든 테스트가 [  PASSED  ]로 나오면 성공!

[추가 설명]
- TEST와 TEST_F의 차이: TEST는 독립 테스트, TEST_F는 테스트 픽스처(공용 자원) 사용
- ASSERT_*는 실패 시 즉시 중단, EXPECT_*는 실패해도 계속 진행
- 다양한 비교 매크로(EXPECT_EQ, EXPECT_NE, EXPECT_LT, EXPECT_GT 등)와 예외 검증(EXPECT_THROW)