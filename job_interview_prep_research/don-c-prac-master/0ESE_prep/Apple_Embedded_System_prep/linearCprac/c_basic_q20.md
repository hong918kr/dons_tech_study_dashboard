# Embedded C Interview Top 20 Questions

## 1. Set a specific bit using bitmask
Write code to set the 3rd bit of an 8-bit variable.

## 2. Extract bit-field from a value
Write code to extract bits 5~7 from a 32-bit value.

## 3. Explain the volatile keyword
Why is `volatile` needed in embedded C? When should it be used?

`volatile`은 변수 값이 프로그램 외부(하드웨어, 인터럽트 등)에 의해 변경될 수 있음을 컴파일러에 알리는 키워드입니다.  
- 컴파일러가 해당 변수에 대한 최적화(캐싱 등)를 하지 않게 하여, 항상 메모리에서 값을 읽고 씁니다.  
- 주로 하드웨어 레지스터, 인터럽트에서 변경되는 변수, 플래그 등에 사용합니다.  
- 예시:  
    ```c
    volatile uint32_t* reg = (uint32_t*)0x40000000;
    *reg = 0x1234; // 항상 메모리에서 읽고 씀
    ```

---

## 4. Pointer vs Array
Explain the difference between `int arr[5]` and `int* p`. What is the relationship between `arr[2]` and `*(arr+2)`?

**답변:**  
- `int arr[5]`는 5개의 int를 담는 배열이고, 메모리에 연속적으로 배치됩니다.  
- `int* p`는 int형 데이터를 가리키는 포인터입니다.  
- `arr[2]`는 배열의 3번째 요소를 의미하며, `*(arr+2)`와 동일합니다.  
- 배열 이름 `arr`는 배열의 첫 번째 요소의 주소로 해석됩니다.  
- 차이점:  
    - 배열은 크기가 고정되어 있고, 포인터는 임의의 메모리 위치를 가리킬 수 있습니다.  
    - 배열은 선언 시 메모리가 할당되고, 포인터는 별도로 할당해야 합니다.

---

## 5. Access struct member
Given `struct S { int a; char b; }; S s;`, write code to assign `0x55` to `s.b`.

## 6. Function pointer usage
Declare `int (*fptr)(int, int);` and show how to call a function using this pointer.

**답변:**  
- 함수 포인터 선언:  
    ```c
    int (*fptr)(int, int);
    ```
- 함수 정의 및 호출 예시:  
    ```c
    int add(int a, int b) { return a + b; }
    fptr = add; // 함수 주소 할당
    int result = fptr(2, 3); // 함수 포인터로 호출
    ```

---



## 7. Memory-mapped register access
Write code to write a 32-bit value to address `0x40000000`.

- 임베디드 시스템에서 하드웨어 레지스터는 특정 주소에 매핑되어 있습니다.  
- 해당 주소에 값을 쓰려면 포인터를 사용합니다.  
    ```c
    #define REG_ADDR 0x40000000
    volatile uint32_t* reg = (volatile uint32_t*)REG_ADDR;
    *reg = 0xDEADBEEF; // 32비트 값 쓰기
    ```
- 반드시 `volatile`을 사용해야 외부에서 값이 바뀌는 것을 컴파일러가 인식합니다.

---


## 8. static variable scope
Explain the difference between `static int cnt;` inside a function and at file scope.

**답변:**  
- 함수 내부의 `static int cnt;`  
    - 함수가 여러 번 호출되어도 값이 유지됨(지역적, 수명은 프로그램 전체).  
    - 함수 외부에서는 접근 불가(지역 스코프).  
- 파일 스코프의 `static int cnt;`  
    - 해당 파일 내에서만 접근 가능(외부 파일에서 참조 불가).  
    - 전역 변수이지만, 파일 단위로 제한됨.
---


## 9. Add node to singly linked list
Write a function to add a node at the tail of a singly linked list.

## 10. Timer interrupt handler signature
Write the basic function signature for a timer interrupt handler in embedded C.

## 11. Implement strcpy
Write a function that copies a string (like `strcpy`).

## 12. Reverse a string in-place
Write a function that reverses a string in-place.

## 13. Find max in array
Write code to find the maximum value in an integer array.

## 14. Sum array elements
Write code to sum all elements in an integer array.

## 15. Struct array access
Given `struct S { int a; }; S arr[5];`, write code to assign `100` to `arr[3].a`.

## 16. Dynamic memory allocation
Write code to allocate memory for 10 integers and explain what must be done after allocation.

## 17. const keyword usage
Explain the difference between `const int* p` and `int* const p`.

## 18. Pass array to function
Write a function signature to pass `int arr[10]` to a function.

## 19. Access struct member via pointer
Given `struct S { int a; }; S* ps;`, write code to assign `5` to `ps->a`.

## 20. Initialize array with for loop
Write code to initialize `int arr[5]` with values `0, 1, 2, 3, 4` using a for loop.