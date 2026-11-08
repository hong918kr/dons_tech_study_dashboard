/* C-M11 demo: C-B15 매크로의 숨은 UB 를 UBSan 으로 — inline 함수 버전은 조용하다
 *   cc -std=c11 -g -O0 -fsanitize=undefined demo_macro_ub.c -o demo_macro_ub && ./demo_macro_ub
 */
#include <stdint.h>
#include <stdio.h>

#define ABS(x)     ((x) < 0 ? -(x) : (x))
#define SQUARE(x)  ((x) * (x))

static inline uint32_t uabs_i32(int32_t x)   { return x < 0 ? 0u - (uint32_t)x : (uint32_t)x; }
static inline int64_t  square_i32(int32_t x) { return (int64_t)x * x; }

int main(int argc, char **argv) {
    (void)argv;
    volatile int32_t imin = INT32_MIN, big = 50000 + (argc - 1);   /* 상수 접기 방지 */
    int32_t a = imin, b = big;
    printf("ABS(INT32_MIN)      = %d\n", ABS(a));
    printf("SQUARE(50000)       = %d\n", SQUARE(b));
    printf("uabs_i32(INT32_MIN) = %u\n", uabs_i32(a));
    printf("square_i32(50000)   = %lld\n", (long long)square_i32(b));
    return 0;
}
