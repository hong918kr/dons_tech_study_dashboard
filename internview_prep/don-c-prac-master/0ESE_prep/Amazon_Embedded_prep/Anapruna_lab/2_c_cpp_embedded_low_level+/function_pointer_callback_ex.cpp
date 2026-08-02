#include <iostream>
#include <cassert>
using namespace std;

// 1. 함수 포인터 예제
int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int mul(int a, int b) { return a * b; }
int divi(int a, int b) { return b ? a / b : 0; }

// 2. 콜백 함수 예제
void operate_and_print(int a, int b, int (*op)(int, int)) {
    int result = op(a, b);
    cout << "Result: " << result << endl;
}

// 3. 테이블 기반 분기 예제
enum OpType { OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MAX };
int op_max(int a, int b) { return (a > b) ? a : b; }

int (*op_table[OP_MAX])(int, int) = { add, sub, mul, divi, op_max };

// 테스트 코드
int main() {
    // 1. 함수 포인터 직접 사용
    int (*fp)(int, int) = add;
    assert(fp(2, 3) == 5);
    fp = sub;
    assert(fp(5, 2) == 3);

    // 2. 콜백 함수 사용
    operate_and_print(4, 2, mul); // Result: 8
    operate_and_print(9, 3, divi); // Result: 3

    // 3. 테이블 기반 분기
    int a = 10, b = 4;
    assert(op_table[OP_ADD](a, b) == 14);
    assert(op_table[OP_SUB](a, b) == 6);
    assert(op_table[OP_MUL](a, b) == 40);
    assert(op_table[OP_DIV](a, b) == 2);
    assert(op_table[OP_MAX](a, b) == 10);

    cout << "All tests PASS" << endl;
    return 0;
}

/*
문제 예시:
- int형 두 수를 입력받아 연산하는 함수(add, sub, mul, div)를 함수 포인터로 선언하라.
- 함수 포인터를 인자로 받는 콜백 함수를 작성하라.
- 연산 종류에 따라 함수 포인터 테이블을 이용해 분기하는 코드를 작성하라.
- main에서 위 기능들을 테스트하는 코드를 작성하라.
*/