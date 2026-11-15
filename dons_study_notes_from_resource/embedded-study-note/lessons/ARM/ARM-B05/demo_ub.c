/* ARM-B05 demo: abs(INT64_MIN) — ISA 는 정의(wrap), C 는 UB
 *   cc -std=c11 -O2 demo_ub.c solution.S -o /tmp/ARM-B05_ub && /tmp/ARM-B05_ub
 *   cc -std=c11 -O1 -g -fsanitize=undefined demo_ub.c solution.S -o /tmp/ARM-B05_ubsan && /tmp/ARM-B05_ubsan
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int64_t sabs_sel(int64_t x);

__attribute__((noinline)) static int64_t c_abs(int64_t x) { return x < 0 ? -x : x; }
__attribute__((noinline)) static int c_abs_is_negative(int64_t x) { return c_abs(x) < 0; }   /* 컴파일러: "abs 는 음수일 리 없다" */
__attribute__((noinline)) static int asm_abs_is_negative(int64_t x) { return sabs_sel(x) < 0; }

static volatile int64_t g_in = INT64_MIN;           /* volatile: 컴파일러가 값을 미리 알지 못하게 */

int main(void) {
    int64_t x = g_in;
    printf("asm sabs_sel(INT64_MIN)  = %" PRId64 "   negative? %d\n", sabs_sel(x), asm_abs_is_negative(x));
    printf("C   c_abs(INT64_MIN)     = %" PRId64 "   negative? %d\n", c_abs(x), c_abs_is_negative(x));
    return 0;
}
