#include <iostream>
#include <cassert>
#include <cstring>
using namespace std;

// 1. 포인터/더블포인터 예제 함수
void set_value(int* p, int val);                // 포인터로 값 설정
void set_value_pp(int** pp, int val);           // 더블포인터로 값 설정

// 2. malloc/free 예제 함수
int* alloc_array(int n);                        // n개 int 동적할당
void free_array(int* arr);                      // 동적할당 해제

// 3. 메모리 누수 예제 함수
void memory_leak_example();                     // 메모리 누수 발생 예시

// 4. shallow/deep copy 예제 함수
struct MyStruct {
    int* arr;
    int n;
    MyStruct(int sz);
    ~MyStruct();
    MyStruct(const MyStruct& other);            // deep copy 생성자
    MyStruct& operator=(const MyStruct& other); // deep copy 대입연산자
};
void shallow_copy_example();
void deep_copy_example();

// TODO: 각 함수 구현

// 테스트 코드
int main() {
    // 1. 포인터/더블포인터
    int x = 0;
    set_value(&x, 42);
    assert(x == 42);
    int* px = &x;
    set_value_pp(&px, 99);
    assert(x == 99);

    // 2. malloc/free
    int* arr = alloc_array(5);
    for (int i = 0; i < 5; ++i) arr[i] = i * 2;
    for (int i = 0; i < 5; ++i) assert(arr[i] == i * 2);
    free_array(arr);

    // 3. 메모리 누수 예시 (실행해도 눈에 안 보이지만, 코드 리뷰용)
    memory_leak_example();

    // 4. shallow/deep copy
    shallow_copy_example();
    deep_copy_example();

    cout << "All tests PASS" << endl;
    return 0;
}

/*
문제 예시:
- 포인터/더블포인터를 이용해 값을 변경하는 함수를 구현하라.
- malloc/free를 이용해 동적 메모리 할당/해제하는 함수를 구현하라.
- 메모리 누수 예시 코드를 작성하라.
- 구조체의 shallow copy와 deep copy 차이를 보여주는 코드를 작성하라.
- main에서 위 함수들을 테스트하는 코드를 작성하라.
*/