/* C-M11 demo: _Generic 의 함정을 직접 찍어 본다 (플랫폼마다 답이 다른 것 포함)
 *   cc -std=c11 -Wall -Wextra demo_generic.c -o demo_generic && ./demo_generic
 *   -DCONST_ASSOC : 'const int' 연관 항목 → 영원히 선택 안 됨 (clang 경고)
 *   -DMIXED       : max_strict(int, unsigned) → 컴파일 에러 (원하는 동작)
 *   -DNO_DEFAULT  : 안쪽 _Generic 에 default 가 없는 max_strict → 올바른 호출도 컴파일 에러
 *   -DLONG_DOUBLE : max_of(long double) → 표에 없는 타입 = 컴파일 에러
 */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define type_name(x) _Generic((x),                                              \
        _Bool: "_Bool", char: "char",                                           \
        signed char: "signed char", unsigned char: "unsigned char",             \
        short: "short", unsigned short: "unsigned short",                       \
        int: "int", unsigned int: "unsigned int",                               \
        long: "long", unsigned long: "unsigned long",                           \
        long long: "long long", unsigned long long: "unsigned long long",       \
        float: "float", double: "double", long double: "long double",           \
        char *: "char *", const char *: "const char *",                         \
        int *: "int *", const int *: "const int *", void *: "void *",           \
        default: "other")
#define SHOW(e) printf("  %-28s -> %s\n", #e, type_name(e))

static inline int      max_i32(int a, int b)                { return a > b ? a : b; }
static inline unsigned max_u32(unsigned a, unsigned b)      { return a > b ? a : b; }
static inline long long max_i64(long long a, long long b)   { return a > b ? a : b; }
#define max_of(a, b) _Generic((a) + (b), int: max_i32, unsigned int: max_u32, long long: max_i64)((a), (b))

void max_strict_TYPE_MISMATCH(void);
#ifdef NO_DEFAULT
#define max_strict(a, b) _Generic((a) + 0,                                  \
        int:          _Generic((b) + 0, int: max_i32),                      \
        unsigned int: _Generic((b) + 0, unsigned int: max_u32))((a), (b))
#else
#define max_strict(a, b) _Generic((a) + 0,                                                       \
        int:          _Generic((b) + 0, int: max_i32,          default: max_strict_TYPE_MISMATCH), \
        unsigned int: _Generic((b) + 0, unsigned int: max_u32, default: max_strict_TYPE_MISMATCH))((a), (b))
#endif

enum led_state { LED_OFF, LED_ON };          /* 음수 값이 없는 enum */
enum temp_zone { ZONE_COLD = -1, ZONE_OK };  /* 음수 값이 있는 enum */

int main(void) {
    const int ci = 1;
    volatile unsigned vr = 2;
    int arr[4] = { 0 };
    enum led_state led = LED_ON;
    enum temp_zone zone = ZONE_OK;
    int si = -1;
    unsigned ua = 1u;

    printf("[lvalue 변환 / decay]\n");
    SHOW(ci); SHOW(vr); SHOW(arr); SHOW(&arr[0]); SHOW("abc");
    printf("[문자 / 정수 승격]\n");
    SHOW('a'); SHOW((char)'a'); SHOW((uint8_t)1); SHOW((int8_t)1); SHOW((uint8_t)1 + (uint8_t)1);
    printf("  CHAR_MIN = %d  (0 이면 이 플랫폼 char 는 unsigned)\n", CHAR_MIN);
    printf("[typedef 의 정체 — 플랫폼마다 다르다]\n");
    SHOW((int32_t)0); SHOW((int64_t)0); SHOW((uint64_t)0); SHOW((size_t)0); SHOW(sizeof(int));
    SHOW((ptrdiff_t)0); SHOW((intptr_t)0);
    printf("[enum — 호환 정수 타입은 구현 정의]\n");
    SHOW(led); SHOW(zone); SHOW(LED_ON);
    printf("[섞인 타입]\n");
    SHOW(si + ua);
    printf("  max_of(-1, 1u) = %u   (unsigned 로 비교됐다)\n", max_of(si, ua));
    printf("  max_strict(-1, 1) = %d\n", max_strict(si, 1));
#ifdef CONST_ASSOC
    printf("  %s\n", _Generic(ci, const int: "const int", int: "int"));
#endif
#ifdef MIXED
    printf("  %u\n", max_strict(si, ua));
#endif
#ifdef LONG_DOUBLE
    long double ld = 1.0L;
    printf("  %Lf\n", max_of(ld, ld));
#endif
    return 0;
}
