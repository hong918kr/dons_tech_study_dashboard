#include <iostream>
#include <cassert>
#include <cstring>
using namespace std;

// 1. 포인터/더블포인터 예제 함수
void set_value(int* p, int val) {
    *p = val;
}
void set_value_pp(int** pp, int val) {
    **pp = val;
}

// 2. malloc/free 예제 함수
int* alloc_array(int n) {
    return (int*)malloc(sizeof(int) * n);
}
void free_array(int* arr) {
    free(arr);
}

// 3. 메모리 누수 예제 함수
void memory_leak_example() {
    int* leak = (int*)malloc(sizeof(int) * 10);
    // free(leak); // 누수 발생 (일부러 해제하지 않음)
}

// 4. shallow/deep copy 예제 함수
struct MyStruct {
    int* arr;
    int n;
    MyStruct(int sz) : n(sz) {
        arr = new int[n];
        for (int i = 0; i < n; ++i) arr[i] = i;
    }
    ~MyStruct() {
        delete[] arr;
    }
    // deep copy 생성자
    MyStruct(const MyStruct& other) : n(other.n) {
        arr = new int[n];
        memcpy(arr, other.arr, sizeof(int) * n);
    }
    // deep copy 대입연산자
    MyStruct& operator=(const MyStruct& other) {
        if (this != &other) {
            delete[] arr;
            n = other.n;
            arr = new int[n];
            memcpy(arr, other.arr, sizeof(int) * n);
        }
        return *this;
    }
};

void shallow_copy_example() {
    int* arr1 = new int[3]{1,2,3};
    int* arr2 = arr1; // shallow copy
    arr2[0] = 99;
    assert(arr1[0] == 99); // 둘 다 같은 메모리
    delete[] arr1; // arr2는 dangling pointer가 됨
}

void deep_copy_example() {
    MyStruct a(3);
    a.arr[0] = 42;
    MyStruct b = a; // deep copy
    b.arr[0] = 77;
    assert(a.arr[0] == 42); // deep copy이므로 영향 없음
}

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