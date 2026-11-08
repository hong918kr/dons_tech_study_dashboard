/* demo_array_size_error.c — 안전한 ARRAY_SIZE 가 포인터를 "컴파일 에러"로 거부하는지 관찰.
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_array_size_error.c -o demo_ase && ./demo_ase   # 통과
 *   cc -std=c11 -Wall -Wextra -O2 -DBREAK demo_array_size_error.c -o demo_ase                # 에러 3개
 *   cc -std=c11 -Wall -Wextra -O2 -DUSE_TYPEOF demo_array_size_error.c -o demo_ase           # typeof 는 C11 에 없음
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef USE_TYPEOF   /* -std=c11 에서는 typeof 가 키워드가 아니다 (gnu11 / c23 에서만) */
#define IS_ARRAY(x) (!__builtin_types_compatible_p(typeof(x), typeof(&(x)[0])))
#else
#define IS_ARRAY(x) (!__builtin_types_compatible_p(__typeof__(x), __typeof__(&(x)[0])))
#endif
#define MUST_BE_ARRAY(x) (0 * sizeof(struct { \
    _Static_assert(IS_ARRAY(x), "ARRAY_SIZE() needs an array, not a pointer"); char dummy_; }))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]) + MUST_BE_ARRAY(x))

struct dma_desc { uint32_t buf, len, ctrl, next; };
static struct dma_desc ring[8];

static size_t count_param(struct dma_desc d[8]) {
#ifdef BREAK
    return ARRAY_SIZE(d);            /* 에러 1: 파라미터는 포인터 */
#else
    (void)d;
    return 8;
#endif
}

int main(void) {
    struct dma_desc *p = ring;
    (void)p;
    printf("ARRAY_SIZE(ring) = %zu, count_param = %zu\n", ARRAY_SIZE(ring), count_param(ring));
#ifdef BREAK
    printf("%zu\n", ARRAY_SIZE(p));         /* 에러 2: 포인터 변수 */
    printf("%zu\n", ARRAY_SIZE(&ring[0]));  /* 에러 3: 첫 원소 주소 */
#endif
    return 0;
}
