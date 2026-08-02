/*
Applied Intuition 코딩 챌린지 대비 50제

카테고리 4: 포인터와 메모리 (Pointers & Memory)
28. const 키워드로 (1) 정수 상수, (2) 상수에 대한 포인터, (3) 정수에 대한 상수 포인터 선언
    Follow-up: const int* p와 int* const p 차이
29. void 포인터란? 유용한 상황 설명
    Follow-up: 역참조 전 타입 캐스팅 이유
30. 함수 포인터로 콜백 메커니즘 예제
    Follow-up: 함수 포인터 배열로 FSM/명령어 처리기 유연성
31. my_strcpy 함수 구현
    Follow-up: 버퍼 오버플로우 방지법(strncpy 등)
32. struct의 메모리 정렬(alignment)과 패딩 설명
    Follow-up: sizeof로 구조체 크기가 예상과 다른 이유
33. 스택과 힙 메모리 차이 설명
    Follow-up: 펌웨어에서 스택 오버플로우의 위험성
34. volatile 키워드가 필요한 코드 예시와 이유
    Follow-up: volatile 없는 코드에서의 최적화 문제
35. static 키워드의 함수 내/파일 범위 사용 차이
    Follow-up: static 함수의 캡슐화 효과

*/

/*
카테고리 4: 포인터와 메모리 (Pointers & Memory)
각 문제는 함수 시그니처, 설명, 예제 변수, follow-up 답변, 테스트 코드(PASS/FAIL)를 포함합니다.
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// 28. const 키워드 선언 예시
// 설명: (1) 정수 상수, (2) 상수에 대한 포인터, (3) 정수에 대한 상수 포인터 선언
// Follow-up: const int* p는 "가리키는 값이 상수", int* const p는 "포인터 자체가 상수"
void const_examples(void) {
    const int a = 10;        // (1) 정수 상수
    const int* p1 = &a;      // (2) 상수에 대한 포인터 (값 변경 불가)
    int b = 20;
    int* const p2 = &b;      // (3) 정수에 대한 상수 포인터 (주소 변경 불가)
    // *p1 = 11; // 컴파일 에러
    *p2 = 21; // OK
    // p2 = &a; // 컴파일 에러
    (void)p1; (void)p2;
}
// Follow-up 답변: const int* p는 값을 못 바꿈, int* const p는 주소를 못 바꿈

// 29. void 포인터란? 유용한 상황 설명
// 설명: void*는 타입에 상관없이 모든 포인터를 받을 수 있음. 예: memcpy, 콜백 등
// Follow-up: 역참조 전에는 반드시 타입 캐스팅 필요 (컴파일러가 타입을 모르기 때문)
void void_pointer_example(void) {
    int x = 42;
    float y = 3.14f;
    void* vp;
    vp = &x;
    int xi = *(int*)vp; // 타입 캐스팅 후 역참조
    vp = &y;
    float yf = *(float*)vp;
    printf("void_pointer_example: xi=%d, yf=%.2f\n", xi, yf);
}
// Follow-up 답변: void*는 타입 정보가 없으므로 역참조 전 캐스팅 필요

// 30. 함수 포인터로 콜백 메커니즘 예제
// 설명: 함수 포인터를 이용해 콜백을 등록하고 호출하는 예제
// Follow-up: 함수 포인터 배열을 사용하면 FSM/명령어 처리기 구현이 유연해짐
typedef void (*callback_t)(int);
void my_callback(int x) { printf("my_callback called with %d\n", x); }
void call_callback(callback_t cb, int val) { cb(val); }
// Follow-up 답변: 함수 포인터 배열로 상태별/명령별 함수 호출 가능

// 31. my_strcpy 함수 구현
// 설명: strcpy와 동일하게 동작하는 함수 구현
// Follow-up: strncpy 등으로 버퍼 오버플로우 방지
char* my_strcpy(char* dst, const char* src) {
    char* ret = dst;
    while ((*dst++ = *src++));
    return ret;
}
// Follow-up 답변: strncpy(dst, src, n) 등으로 복사 길이 제한 필요

// 32. struct의 메모리 정렬(alignment)과 패딩 설명
// 설명: 구조체 멤버의 정렬/패딩으로 인해 sizeof가 예상과 다를 수 있음
// Follow-up: 멤버 순서/정렬에 따라 패딩 발생, #pragma pack 사용 가능
struct S1 { char c; int i; };
struct S2 { int i; char c; };
// Follow-up 답변: 멤버 순서/정렬에 따라 sizeof(struct)가 달라짐

// 33. 스택과 힙 메모리 차이 설명
// 설명: 스택은 함수 호출 시 자동 할당/해제, 힙은 malloc/free로 수동 관리
// Follow-up: 스택 오버플로우는 함수 호출/지역 변수 과다로 발생, 임베디드에서 치명적
void stack_heap_example(void) {
    int stack_var = 1;
    int* heap_var = (int*)malloc(sizeof(int));
    *heap_var = 2;
    printf("stack=%d, heap=%d\n", stack_var, *heap_var);
    free(heap_var);
}
// Follow-up 답변: 스택 오버플로우는 시스템 다운 원인, 힙은 단편화/누수 위험

// 34. volatile 키워드가 필요한 코드 예시와 이유
// 설명: volatile은 변수 값이 예측 불가하게 바뀔 때(예: ISR, HW 레지스터) 사용
// Follow-up: volatile 없으면 컴파일러가 값 캐싱/최적화로 버그 발생
volatile int flag = 0;
void isr_set_flag(void) { flag = 1; }
void main_loop_check_flag(void) {
    if (flag) printf("flag set!\n");
}
// Follow-up 답변: volatile 없으면 flag를 계속 레지스터에서 읽지 않을 수 있음

// 35. static 키워드의 함수 내/파일 범위 사용 차이
// 설명: 함수 내 static 변수는 값이 유지, 파일 범위 static 함수는 외부에서 접근 불가
// Follow-up: static 함수는 파일 내에서만 접근 가능(캡슐화 효과)
static int static_func_var(void) {
    static int cnt = 0;
    return ++cnt;
}
static void file_static_func(void) { printf("file_static_func\n"); }
// Follow-up 답변: static 함수는 외부 링크 안 됨, 내부 구현 숨김

// ------------------- 테스트 코드 -------------------
#define TEST_LABEL_WIDTH 45
void test_result(const char* label, int ok) {
    printf("%-*s %s\n", TEST_LABEL_WIDTH, label, ok ? "[PASS]" : "[FAIL]");
}

int main(void) {
    // 28. const 예제 (컴파일 에러 유발 X, 동작만 확인)
    const_examples();
    test_result("28. const_examples (manual check)", 1);

    // 29. void 포인터 예제
    void_pointer_example();
    test_result("29. void_pointer_example (manual check)", 1);

    // 30. 함수 포인터 콜백
    call_callback(my_callback, 42);
    callback_t arr[2] = {my_callback, my_callback};
    arr[1](99); // 함수 포인터 배열 예시
    test_result("30. callback_t array (manual check)", 1);

    // 31. my_strcpy
    char buf[16];
    my_strcpy(buf, "hello");
    test_result("31. my_strcpy", strcmp(buf, "hello") == 0);

    // 32. struct 패딩/정렬
    test_result("32. sizeof S1", sizeof(struct S1) >= 5);
    test_result("32. sizeof S2", sizeof(struct S2) >= 5);

    // 33. 스택/힙 예제
    stack_heap_example();
    test_result("33. stack_heap_example (manual check)", 1);

    // 34. volatile 예제
    flag = 0;
    isr_set_flag();
    main_loop_check_flag();
    test_result("34. volatile flag (manual check)", 1);

    // 35. static 함수/변수
    int v1 = static_func_var();
    int v2 = static_func_var();
    test_result("35. static_func_var", v2 == v1 + 1);

    return 0;
}