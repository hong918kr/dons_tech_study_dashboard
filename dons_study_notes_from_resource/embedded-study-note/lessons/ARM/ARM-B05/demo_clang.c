/* ARM-B05 demo: 같은 함수를 C 로 쓰면 clang -O2 는 무엇을 내나 (분기? csel?)
 *   cc -std=c11 -O2 -S -fno-asynchronous-unwind-tables demo_clang.c -o - | grep -v '^\s*\.'
 */
#include <stdint.h>

int64_t  c_smax(int64_t a, int64_t b)   { return a > b ? a : b; }
uint64_t c_umax(uint64_t a, uint64_t b) { return a > b ? a : b; }
int64_t  c_abs(int64_t x)               { return x < 0 ? -x : x; }      /* x == INT64_MIN 이면 UB */
int64_t  c_sign(int64_t x)              { return (x > 0) - (x < 0); }
int64_t  c_clamp(int64_t x, int64_t lo, int64_t hi) { if (x < lo) x = lo; if (x > hi) x = hi; return x; }
int64_t  c_sat_add(int64_t a, int64_t b) {
    int64_t r;
    if (__builtin_add_overflow(a, b, &r)) return a < 0 ? INT64_MIN : INT64_MAX;
    return r;
}
int      c_tick_reached(uint32_t now, uint32_t dl) { return (int32_t)(now - dl) >= 0; }
