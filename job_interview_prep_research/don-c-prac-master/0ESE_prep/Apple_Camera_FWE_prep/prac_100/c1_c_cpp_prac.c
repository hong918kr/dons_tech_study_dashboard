/*

카테고리 1: C/C++ 언어 및 저수준 프로그래밍 (1~25)

1. (C) 주어진 32비트 정수에서 특정 비트(n번째 비트)를 설정(set), 해제(clear), 반전(toggle)하는 매크로를 작성하세요.
2. (C) volatile 키워드의 의미는 무엇이며, 어떤 상황에서 반드시 사용해야 하는지 카메라 펌웨어의 예시(예: 하드웨어 레지스터, ISR 공유 변수)를 들어 설명하세요.
3. (C) const 키워드의 다양한 사용법(상수 변수, 상수 포인터, 상수를 가리키는 포인터 등)을 설명하고, volatile과 const를 함께 사용할 수 있는지 설명하세요.
4. (C) 구조체 패딩(Structure Padding)이 무엇이며 왜 발생하는지 설명하세요. #pragma pack(1) 지시자의 역할은 무엇이며, 사용할 때의 장단점은 무엇인가요?
5. (C) 주어진 정수형 배열에서 엔디안(Endianness)을 변환하는 함수(예: Little-endian to Big-endian)를 구현하세요.
6. (C++) C와 C++의 struct와 class의 차이점은 무엇인가요?
7. (C++) C++에서 new/delete와 C의 malloc/free의 주요 차이점은 무엇이며, 임베디드 시스템에서 동적 할당을 사용할 때 주의할 점은 무엇인가요?
8. (C) 함수 포인터를 사용하여 콜백(callback) 메커니즘을 구현하는 간단한 예제를 작성하세요. (예: 버튼 누름 이벤트 처리)
9. (C) 인라인 함수(inline function)와 매크로(#define)의 차이점을 설명하고, 각각의 장단점을 논하세요.
10. (C) 재진입 가능 함수(Reentrant Function)란 무엇이며, ISR(인터럽트 서비스 루틴)에서 안전하게 호출할 수 있는 함수가 되기 위한 조건은 무엇인가요?
11. (C++) C++의 RAII(Resource Acquisition Is Initialization) 패턴에 대해 설명하고, 뮤텍스 락(lock) 관리에 어떻게 적용될 수 있는지 간단한 코드로 보여주세요.
12. (C) 연결 리스트(Linked List)에서 특정 노드를 삭제하는 함수를 구현하세요.
13. (C) static 키워드가 변수와 함수에 사용될 때 각각 어떤 의미를 가지는지 설명하세요.
14. (C) 포인터와 배열의 관계에 대해 설명하고, arr[i]와 *(arr + i)가 동일한 이유를 설명하세요.
15. (C++) C++ 템플릿을 사용하여 모든 데이터 타입을 처리할 수 있는 간단한 max() 함수를 작성하세요.
16. (C) 비트 필드(Bit-field)를 사용하여 하드웨어 레지스터(예: 8비트 제어 레지스터)를 표현하는 구조체를 정의하고 사용하는 예시를 보여주세요.
17. (C++) C++에서 가상 함수(virtual function)와 다형성(polymorphism)이 어떻게 동작하는지 설명하세요. 임베디드 시스템에서 가상 함수 사용 시 성능 오버헤드는 무엇인가요?
18. (C) setjmp와 longjmp의 사용법과 위험성에 대해 설명하세요.
19. (C++) std::atomic은 무엇이며, volatile과 어떻게 다른가요? 멀티코어 환경에서 공유 변수를 안전하게 증가시키는 예제를 std::atomic으로 작성하세요.
20. (C) 메모리 단편화(Memory Fragmentation)란 무엇이며, 임베디드 시스템에서 이를 피하기 위한 전략은 무엇인가요?
21. (C++) constexpr와 const의 차이점은 무엇인가요?
22. (C) 순환 버퍼(Circular Buffer)를 구현하세요. put, get 함수와 버퍼가 가득 찼는지, 비었는지 확인하는 함수를 포함해야 합니다.
23. (C++) C++11의 std::move와 우측값 참조(rvalue reference)에 대해 설명하세요.
24. (C) typedef와 #define을 사용하여 새로운 타입을 정의할 때의 차이점을 설명하세요.
25. (C++) 람다(Lambda) 함수란 무엇이며, C++11에서 어떻게 사용되는지 간단한 예시를 보여주세요.

*/





/*

카테고리 1: C/C++ 언어 및 저수준 프로그래밍 (1~25)
각 문제는 함수 시그니처, 설명, 개념, 샘플 입력/출력을 포함합니다.

1. // 비트 조작 매크로
// 설명: 32비트 정수에서 n번째 비트를 set/clear/toggle하는 매크로를 작성하세요.
// 개념: 비트 연산, 매크로
// 샘플 입력: x=0x10, n=4 → set: 0x10 | (1<<4) = 0x10, clear: 0x10 & ~(1<<4) = 0x00, toggle: 0x10 ^ (1<<4) = 0x00
#define BIT_SET(x, n)    ((x) | (1U << (n)))
#define BIT_CLEAR(x, n)  ((x) & ~(1U << (n)))
#define BIT_TOGGLE(x, n) ((x) ^ (1U << (n)))

2. // volatile 키워드 사용 예시
// 설명: volatile의 의미와 하드웨어 레지스터/ISR 공유 변수에서의 사용 예시를 설명하세요.
// 개념: 최적화 방지, 메모리 일관성
// 샘플 입력: volatile uint32_t* reg = (uint32_t*)0x40000000; *reg = 0x1;
// 샘플 출력: 항상 메모리에서 읽고 씀, 캐싱/최적화 방지

3. // const와 volatile의 조합
// 설명: const의 다양한 사용법과 volatile과의 조합 가능성 설명
// 개념: 상수성, 변경 불가, 하드웨어 레지스터
// 샘플 입력: const int a = 5; const int* p; int* const q; const volatile uint32_t* reg;
// 샘플 출력: 값/포인터/레지스터가 변경 불가

4. // 구조체 패딩과 #pragma pack
// 설명: 구조체 패딩이 발생하는 이유와 #pragma pack(1)의 효과, 장단점 설명
// 개념: 메모리 정렬, 패딩, 성능
// 샘플 입력: struct { char a; int b; };
// 샘플 출력: sizeof(struct) == 8 (패딩 있음), #pragma pack(1) 사용 시 5

5. // 엔디안 변환 함수
// 설명: 정수형 배열의 엔디안 변환 함수 구현
// 개념: 엔디안, 바이트 스와핑
// 함수 시그니처:
void swap_endian32_array(uint32_t* arr, size_t len);
// 샘플 입력: arr = {0x12345678}, len=1
// 샘플 출력: arr = {0x78563412}

6. // struct vs class 차이
// 설명: C와 C++에서 struct와 class의 차이점 설명
// 개념: 접근제어, 상속, 캡슐화
// 샘플 입력: struct S { int x; }; class C { int x; };
// 샘플 출력: struct는 기본 public, class는 기본 private

7. // new/delete vs malloc/free
// 설명: C++의 new/delete와 C의 malloc/free 차이, 임베디드에서 주의점
// 개념: 생성자/소멸자, 타입 안전성, 메모리 단편화
// 샘플 입력: int* p = new int; int* q = (int*)malloc(sizeof(int));
// 샘플 출력: new는 생성자 호출, delete는 소멸자 호출

8. // 함수 포인터 콜백 예제
// 설명: 함수 포인터를 이용한 콜백 메커니즘 구현 (예: 버튼 이벤트)
// 개념: 함수 포인터, 콜백
// 함수 시그니처:
void register_button_callback(void (*cb)(void));
void button_isr(void);
// 샘플 입력: register_button_callback(my_cb); button_isr();
// 샘플 출력: my_cb()가 호출됨

9. // inline 함수 vs 매크로
// 설명: inline 함수와 매크로의 차이, 장단점
// 개념: 전처리, 타입 안전성, 디버깅
// 샘플 입력: #define SQUARE(x) ((x)*(x)), inline int square(int x) { return x*x; }
// 샘플 출력: SQUARE(2+1) = 5 (매크로 버그), square(2+1) = 9

10. // 재진입 가능 함수
// 설명: 재진입 가능 함수란? ISR에서 안전하게 호출되는 조건
// 개념: 전역 변수 사용 금지, 스택 변수만 사용
// 샘플 입력: int sum(int a, int b) { return a+b; }
// 샘플 출력: sum은 재진입 가능

11. // C++ RAII 패턴과 뮤텍스 관리
// 설명: RAII 개념과 뮤텍스 락 관리에의 적용
// 개념: 자원 관리, 자동 해제
// 함수 시그니처:
class MutexLock {
public:
    MutexLock(Mutex& m); ~MutexLock();
};
// 샘플 입력: { MutexLock lock(m); ... }
// 샘플 출력: 블록 벗어나면 자동 unlock

12. // 연결 리스트 노드 삭제 함수
// 설명: 연결 리스트에서 특정 노드 삭제 함수 구현
// 개념: 포인터 조작, 메모리 해제
// 함수 시그니처:
void delete_node(ListNode** head, int value);
// 샘플 입력: head = 1->2->3, value=2
// 샘플 출력: head = 1->3

13. // static 키워드의 의미
// 설명: static이 변수/함수에 사용될 때 의미 설명
// 개념: 정적 저장, 파일 범위, 내부 연결
// 샘플 입력: static int x; static void foo();
// 샘플 출력: x는 파일 내에서만 접근 가능

14. // 포인터와 배열의 관계
// 설명: arr[i]와 *(arr+i)가 같은 이유 설명
// 개념: 포인터 산술, 배열 인덱싱
// 샘플 입력: int arr[3] = {1,2,3}; arr[1], *(arr+1)
// 샘플 출력: 둘 다 2

15. // C++ 템플릿 max 함수
// 설명: 모든 타입에 대해 동작하는 max() 함수 작성
// 개념: 템플릿, 타입 추론
// 함수 시그니처:
template<typename T>
T my_max(T a, T b) { return a > b ? a : b; }
// 샘플 입력: my_max(3,5), my_max(2.1,1.2)
// 샘플 출력: 5, 2.1

16. // 비트필드로 레지스터 표현
// 설명: 비트필드로 8비트 제어 레지스터 구조체 정의
// 개념: 비트필드, 하드웨어 레지스터
// 샘플 입력: struct Reg { uint8_t en:1, mode:3, res:4; };
// 샘플 출력: Reg r; r.en=1; r.mode=3;

17. // C++ 가상 함수와 다형성
// 설명: 가상 함수와 다형성, 임베디드에서의 오버헤드
// 개념: 가상 테이블, 동적 바인딩
// 샘플 입력: class Base { virtual void foo(); }; class D:public Base{void foo();};
// 샘플 출력: Base* p = new D; p->foo(); // D::foo() 호출

18. // setjmp/longjmp 사용법과 위험성
// 설명: setjmp/longjmp의 동작 원리와 위험성
// 개념: 비정상 흐름 제어, 스택 무결성
// 샘플 입력: if(setjmp(env)==0){...} else {...} longjmp(env,1);
// 샘플 출력: longjmp 호출 시 else 블록 실행

19. // std::atomic과 volatile 차이, 예제
// 설명: std::atomic의 의미, volatile과의 차이, 안전한 증가 예제
// 개념: 원자성, 메모리 일관성
// 함수 시그니처:
#include <atomic>
void atomic_increment(std::atomic<int>& counter);
// 샘플 입력: std::atomic<int> c=0; atomic_increment(c);
// 샘플 출력: c==1

20. // 메모리 단편화와 방지 전략
// 설명: 메모리 단편화란? 임베디드에서의 방지법
// 개념: 동적 할당, 고정 크기 블록, 풀 할당자
// 샘플 입력: malloc/free 반복
// 샘플 출력: 단편화 발생, 풀 할당자 사용 시 해결

21. // constexpr vs const
// 설명: constexpr와 const의 차이점
// 개념: 컴파일 타임 상수, 런타임 상수
// 샘플 입력: constexpr int a=5; const int b=6;
// 샘플 출력: a는 컴파일 타임, b는 런타임 상수

22. // 순환 버퍼 구현
// 설명: put/get, full/empty 확인 함수 포함한 순환 버퍼 구현
// 개념: 버퍼, 인덱스, 모듈로 연산
// 함수 시그니처:
typedef struct { int buf[SIZE]; int head, tail; } RingBuffer;
void rb_put(RingBuffer* rb, int val);
int rb_get(RingBuffer* rb, int* val);
int rb_is_full(const RingBuffer* rb);
int rb_is_empty(const RingBuffer* rb);
// 샘플 입력: put 1,2,3, get 1,2,3
// 샘플 출력: 1,2,3 순서로 반환

23. // std::move와 rvalue reference
// 설명: std::move, rvalue reference 개념과 사용 예시
// 개념: 이동 시맨틱, 임시 객체
// 샘플 입력: std::string a="hi", b=std::move(a);
// 샘플 출력: a는 비어있음, b는 "hi"

24. // typedef vs #define 타입 정의 차이
// 설명: typedef와 #define로 타입 정의할 때 차이점
// 개념: 전처리, 타입 안전성
// 샘플 입력: typedef int myint; #define myint int
// 샘플 출력: typedef는 진짜 타입, #define은 치환

25. // C++11 람다 함수 예시
// 설명: 람다 함수란? C++11에서의 사용 예시
// 개념: 익명 함수, 캡처
// 샘플 입력: auto f = [](int x){return x+1;}; f(2);
// 샘플 출력: 3

*/
