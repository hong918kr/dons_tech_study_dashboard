/* C-M11 demo: 같은 펌웨어 함수 3개를 매크로 / static inline / _Generic 으로 각각 빌드해 코드 크기 비교
 *   -DIMPL=1 매크로,  -DIMPL=2 static inline,  -DIMPL=3 _Generic(+inline)
 * 호스트:    cc -std=c11 -O2 -DIMPL=1 -c demo_size.c -o m.o && size -m m.o
 * Cortex-M4: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -Os -DIMPL=1 -c demo_size.c -o m.o
 *            xcrun llvm-size m.o
 */
#include <stdint.h>

extern volatile uint32_t UART_DR;          /* 가짜 RX 데이터 레지스터 (읽으면 FIFO pop) */

#if IMPL != 1   /* .c 파일 안의 안 쓰는 static inline 은 clang -Wunused-function 경고 (헤더 안이면 침묵) */
static inline int32_t  max_i32(int32_t a, int32_t b)   { return a > b ? a : b; }
static inline int32_t  min_i32(int32_t a, int32_t b)   { return a < b ? a : b; }
static inline uint32_t max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }
static inline uint32_t uabs_i32(int32_t x) { return x < 0 ? 0u - (uint32_t)x : (uint32_t)x; }
#endif

#if IMPL == 1
#define MAX(a, b)  ((a) > (b) ? (a) : (b))
#define MIN(a, b)  ((a) < (b) ? (a) : (b))
#define ABS(x)     ((x) < 0 ? -(x) : (x))
#define P_MAX_U(a, b) MAX(a, b)
#define P_MAX_I(a, b) MAX(a, b)
#define P_MIN_I(a, b) MIN(a, b)
#define P_ABS(x)      ((uint32_t)ABS(x))
#elif IMPL == 2
#define P_MAX_U(a, b) max_u32(a, b)
#define P_MAX_I(a, b) max_i32(a, b)
#define P_MIN_I(a, b) min_i32(a, b)
#define P_ABS(x)      uabs_i32(x)
#else
#define max_of(a, b) _Generic((a) + (b), int: max_i32, unsigned int: max_u32)((a), (b))
#define min_of(a, b) _Generic((a) + (b), int: min_i32)((a), (b))
#define abs_of(x)    _Generic((x) + 0, int: uabs_i32)(x)
#define P_MAX_U(a, b) max_of(a, b)
#define P_MAX_I(a, b) max_of(a, b)
#define P_MIN_I(a, b) min_of(a, b)
#define P_ABS(x)      abs_of(x)
#endif

uint32_t peak_abs(const int32_t *s, uint32_t n) {
    uint32_t r = 0;
    for (uint32_t i = 0; i < n; i++) r = P_MAX_U(r, P_ABS(s[i]));
    return r;
}

void clamp_buf(int32_t *s, uint32_t n, int32_t lo, int32_t hi) {
    for (uint32_t i = 0; i < n; i++) s[i] = P_MIN_I(P_MAX_I(s[i], lo), hi);
}

uint32_t rx_peak(uint32_t n) {             /* FIFO 에서 n 바이트를 꺼내며 최대값 */
    uint32_t m = 0;
    for (uint32_t i = 0; i < n; i++) m = P_MAX_U(m, UART_DR);
    return m;
}
