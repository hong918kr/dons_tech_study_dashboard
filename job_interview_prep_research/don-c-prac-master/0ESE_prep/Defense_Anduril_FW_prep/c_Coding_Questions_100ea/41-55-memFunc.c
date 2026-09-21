#include <stddef.h>
#include <stdio.h>

/*
41. void* my_memset(void *s, int c, size_t n) 구현
    Example: my_memset(buf, 0xAA, 4) -> buf = {0xAA, 0xAA, 0xAA, 0xAA}
*/
void* my_memset(void *s, int c, size_t n) {
    // TODO: implement
    return s;
}

/*
42. void* my_memcpy(void *dest, const void *src, size_t n) 구현
    Example: my_memcpy(dest, src, 4) -> dest = src[0..3]
*/
void* my_memcpy(void *dest, const void *src, size_t n) {
    // TODO: implement
    return dest;
}

/*
43. void* my_memmove(void *dest, const void *src, size_t n) 구현
    Example: my_memmove(dest, src, 4) -> dest = src[0..3] (safe for overlap)
*/
void* my_memmove(void *dest, const void *src, size_t n) {
    // TODO: implement
    return dest;
}

/*
44. int my_memcmp(const void *s1, const void *s2, size_t n) 구현
    Example: my_memcmp("abc", "abd", 3) -> negative value
*/
int my_memcmp(const void *s1, const void *s2, size_t n) {
    // TODO: implement
    return 0;
}

/*
45. void* my_memchr(const void *s, int c, size_t n) 구현
    Example: my_memchr("abc", 'b', 3) -> pointer to 'b'
*/
void* my_memchr(const void *s, int c, size_t n) {
    // TODO: implement
    return NULL;
}

/*
46. 포인터를 사용하여 두 정수 스왑
    Example: swap_int(&a, &b) -> a, b values swapped
*/
void swap_int(int *a, int *b) {
    // TODO: implement
}

/*
47. 포인터를 사용하여 문자열을 인플레이스로 뒤집기
    Example: reverse_string("hello") -> "olleh"
*/
void reverse_string(char *s) {
    // TODO: implement
}

/*
48. const int *p, int * const p, const int * const p의 차이 설명
    See comments below for explanation.
*/
// 48
/*
const int *p: 포인터 p가 가리키는 값은 변경할 수 없으나, p 자체는 다른 주소를 가리킬 수 있음.
int * const p: 포인터 p는 한 번 초기화되면 다른 주소를 가리킬 수 없으나, 가리키는 값은 변경 가능.
const int * const p: 포인터 p도 다른 주소를 가리킬 수 없고, 가리키는 값도 변경 불가.
*/

/*
49. volatile 키워드의 의미와 사용 사례 설명
    See comments below for explanation.
*/
// 49
/*
volatile 키워드는 변수의 값이 예기치 않게 변경될 수 있음을 컴파일러에 알림.
주로 하드웨어 레지스터, 인터럽트 서비스 루틴, 멀티스레드 환경에서 사용.
컴파일러가 해당 변수에 대한 최적화를 하지 않도록 함.
*/

/*
50. 함수 포인터 배열을 이용한 디스패치 테이블 구현
    Example: dispatch_table_example() calls functions by index
*/
typedef void (*func_ptr_t)(void);
void dispatch_table_example(void) {
    // TODO: implement
}

/*
51. 정적 메모리 풀을 이용한 간단한 malloc/free 구현
    Example: static_malloc(16), static_free(ptr)
*/
void* static_malloc(size_t size) {
    // TODO: implement
    return NULL;
}
void static_free(void *ptr) {
    // TODO: implement
}

/*
52. 댕글링 포인터(Dangling Pointer)란 무엇이며 어떻게 발생하는가?
    See comments below for explanation.
*/
// 52
/*
댕글링 포인터란 이미 해제되었거나 유효하지 않은 메모리 주소를 가리키는 포인터.
주로 free() 이후 포인터를 사용하거나, 지역 변수의 주소를 반환할 때 발생.
*/

/*
53. void* 포인터의 용도와 안전한 사용법
    See comments below for explanation.
*/
// 53
/*
void* 포인터는 타입에 상관없이 모든 데이터의 주소를 저장할 수 있음.
타입 캐스팅을 통해 원하는 타입으로 변환하여 사용.
사용 시 올바른 타입으로 변환하여 접근해야 하며, 타입 정보를 잃지 않도록 주의해야 함.
*/

/*
54. 구조체 패딩(padding)과 정렬(alignment)의 원리 설명
    See comments below for explanation.
*/
// 54
/*
구조체 패딩은 멤버의 정렬을 맞추기 위해 삽입되는 여분의 바이트.
정렬(alignment)은 특정 타입의 데이터가 메모리에서 특정 경계에 위치해야 함을 의미.
효율적인 접근과 하드웨어 요구사항을 충족하기 위해 사용됨.
*/

/*
55. 포인터 배열과 배열 포인터의 차이 설명
    See comments below for explanation.
*/
// 55
/*
포인터 배열: 여러 개의 포인터를 원소로 가지는 배열 (예: int *arr[10])
배열 포인터: 배열 전체를 가리키는 포인터 (예: int (*arr)[10])
*/

// --- Test code ---
int main(void) {
    char buf[8] = {0};
    char src[8] = "abcdef";
    int a = 1, b = 2;

    my_memset(buf, 0xAA, 4);
    printf("my_memset(buf, 0xAA, 4): ");
    for (int i = 0; i < 4; ++i) printf("%02X ", (unsigned char)buf[i]);
    printf("\n"); // Expected: AA AA AA AA

    my_memcpy(buf, src, 4);
    printf("my_memcpy(buf, src, 4): %.*s\n", 4, buf); // Expected: abcd

    my_memmove(buf + 2, buf, 4);
    printf("my_memmove(buf+2, buf, 4): %.*s\n", 6, buf); // Expected: ababcd

    printf("my_memcmp(\"abc\", \"abd\", 3): %d\n", my_memcmp("abc", "abd", 3)); // Expected: negative value

    printf("my_memchr(\"abc\", 'b', 3): %s\n", (char*)my_memchr("abc", 'b', 3)); // Expected: bc

    printf("Before swap: a=%d, b=%d\n", a, b);
    swap_int(&a, &b);
    printf("After swap: a=%d, b=%d\n", a, b); // Expected: a=2, b=1

    char str[] = "hello";
    reverse_string(str);
    printf("reverse_string(\"hello\"): %s\n", str); // Expected: olleh

    dispatch_table_example(); // Example call

    void *ptr = static_malloc(16);
    printf("static_malloc(16): %p\n", ptr);
    static_free(ptr);

    return 0;
}

/*
------------------ Example Input/Output ------------------

my_memset(buf, 0xAA, 4): AA AA AA AA 
my_memcpy(buf, src, 4): abcd
my_memmove(buf+2, buf, 4): ababcd
my_memcmp("abc", "abd", 3): negative value
my_memchr("abc", 'b', 3): bc
Before swap: a=1, b=2
After swap: a=2, b=1
reverse_string("hello"): olleh
dispatch_table_example(): (calls functions by index)
static_malloc(16): (pointer address)
static_free(ptr): (no output)

----------------------------------------------------------
*/