/*
Day 5: CMake

주요 초점: add_library, target_link_libraries
핵심 개념: Calculator + gtest 프로젝트 CMake 구성
목적: CMake를 활용해 테스트 가능한 C++ 프로젝트를 구조화하고, 빌드 자동화 및 테스트 연동을 실습한다.
*/

#include <iostream>

/*
1. Calculator 라이브러리 코드 분리
   - 문제: Calculator 클래스를 별도 소스/헤더 파일로 분리하고, add_library로 라이브러리화한다.
   - 배움: 라이브러리와 실행 파일 분리, 재사용성 향상
   - 목적: 유지보수와 테스트가 쉬운 프로젝트 구조 경험
*/
class Calculator {
public:
    int add(int a, int b) { return a + b; }
    int sub(int a, int b) { return a - b; }
    int mul(int a, int b) { return a * b; }
    int div(int a, int b) { return b != 0 ? a / b : 0; }
};

/*
2. CMakeLists.txt 작성
   - 문제: add_library, add_executable, target_link_libraries를 사용해 CMake 프로젝트를 구성한다.
   - 배움: CMake의 기본 명령어와 프로젝트 구조화
   - 목적: 자동화된 빌드 및 테스트 환경 구축
   - 예시:
     add_library(calculator calculator.cpp)
     add_executable(main main.cpp)
     target_link_libraries(main calculator)
*/

/*
3. Google Test 연동
   - 문제: gtest를 find_package로 연동하고, Calculator에 대한 단위 테스트를 별도 테스트 실행 파일로 작성한다.
   - 배움: target_link_libraries로 gtest 연동, add_test로 테스트 자동화
   - 목적: 실무에서 사용하는 테스트 환경 경험
   - 예시:
     add_executable(calculator_test calculator_test.cpp)
     target_link_libraries(calculator_test calculator gtest_main)
     add_test(NAME calc_test COMMAND calculator_test)
*/

// 아래는 main 예제 코드 (테스트는 별도 파일에서 작성)
int main() {
    Calculator calc;
    std::cout << "=== Day 5: CMake & Calculator Project ===\n";
    std::cout << "add(2, 3) = " << calc.add(2, 3) << "\n";
    std::cout << "sub(5, 3) = " << calc.sub(5, 3) << "\n";
    std::cout << "mul(4, 3) = " << calc.mul(4, 3) << "\n";
    std::cout << "div(10, 2) = " << calc.div(10, 2) << "\n";
    std::cout << "div(5, 0) = " << calc.div(5, 0) << "\n";
    return 0;
}

/*
[실행 결과 예시]
=== Day 5: CMake & Calculator Project ===
add(2, 3) = 5
sub(5, 3) = 2
mul(4, 3) = 12
div(10, 2) = 5
div(5, 0) = 0
-------------------------
*/

/*
[CMakeLists.txt 예시]
cmake_minimum_required(VERSION 3.10)
project(CalculatorProject)

add_library(calculator calculator.cpp)
add_executable(main main.cpp)
target_link_libraries(main calculator)

# GoogleTest 연동
find_package(GTest REQUIRED)
add_executable(calculator_test calculator_test.cpp)
target_link_libraries(calculator_test calculator GTest::GTest GTest::Main)
*/