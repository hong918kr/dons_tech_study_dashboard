/*
Applied Intuition 코딩 챌린지 대비 50제

카테고리 4: 포인터와 메모리 (Pointers & Memory)

28. const 키워드로 (1) 정수 상수, (2) 상수에 대한 포인터, (3) 정수에 대한 상수 포인터 선언
    - 설명: const의 다양한 선언법을 익히고, const int* p와 int* const p의 차이를 이해하세요.
    - Follow-up: const int* p는 "가리키는 값이 상수", int* const p는 "포인터 자체가 상수"

29. void 포인터란? 유용한 상황 설명
    - 설명: void*는 타입에 상관없이 모든 포인터를 받을 수 있습니다. 예: memcpy, 콜백 등.
    - Follow-up: 역참조 전에는 반드시 타입 캐스팅 필요(컴파일러가 타입을 모르기 때문)

30. 함수 포인터로 콜백 메커니즘 예제
    - 설명: 함수 포인터를 이용해 콜백을 등록하고 호출하는 예제를 작성하세요.
    - Follow-up: 함수 포인터 배열을 사용하면 FSM/명령어 처리기 구현이 유연해짐

31. my_strcpy 함수 구현
    - 설명: strcpy와 동일하게 동작하는 함수를 직접 구현하세요.
    - Follow-up: strncpy 등으로 버퍼 오버플로우 방지

32. struct의 메모리 정렬(alignment)과 패딩 설명
    - 설명: 구조체 멤버의 정렬/패딩으로 인해 sizeof가 예상과 다를 수 있음을 확인하세요.
    - Follow-up: 멤버 순서/정렬에 따라 패딩 발생, #pragma pack 사용 가능

33. 스택과 힙 메모리 차이 설명
    - 설명: 스택은 함수 호출 시 자동 할당/해제, 힙은 malloc/free로 수동 관리됨을 설명하세요.
    - Follow-up: 스택 오버플로우는 함수 호출/지역 변수 과다로 발생, 임베디드에서 치명적

34. volatile 키워드가 필요한 코드 예시와 이유
    - 설명: volatile은 변수 값이 예측 불가하게 바뀔 때(예: ISR, HW 레지스터) 사용합니다.
    - Follow-up: volatile 없으면 컴파일러가 값 캐싱/최적화로 버그 발생

35. static 키워드의 함수 내/파일 범위 사용 차이
    - 설명: 함수 내 static 변수는 값이 유지, 파일 범위 static 함수는 외부에서 접근 불가
    - Follow-up: static 함수는 파일 내에서만 접근 가능(캡슐화 효과)

*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// 28. const 키워드 선언 예시
void const_examples(void) {}

// 29. void 포인터란? 유용한 상황 설명
void void_pointer_example(void) {}

// 30. 함수 포인터로 콜백 메커니즘 예제
typedef void (*callback_t)(int);
void my_callback(int x) {}
void call_callback(callback_t cb, int val) {}

// 31. my_strcpy 함수 구현
char* my_strcpy(char* dst, const char* src) {}

// 32. struct의 메모리 정렬(alignment)과 패딩 설명
struct S1 { char c; int i; };
struct S2 { int i; char c; };

// 33. 스택과 힙 메모리 차이 설명
void stack_heap_example(void) {}

// 34. volatile 키워드가 필요한 코드 예시와 이유
volatile int flag = 0;
void isr_set_flag(void) {}
void main_loop_check_flag(void) {}

// 35. static 키워드의 함수 내/파일 범위 사용 차이
static int static_func_var(void) {}
static void file_static_func(void) {}

#define TEST_LABEL_WIDTH 45
void test_result(const char* label, int ok) {
    printf("%-*s %s\n", TEST_LABEL_WIDTH, label, ok ? "[PASS]" : "[FAIL]");
}

int main(void) {
    // 테스트 코드는 남겨둡니다.
    return 0;
}