/* ARM-B11 demo: clang 이 C 의 FP 코드를 무엇으로 내리나 (컴파일만, 실행 안 함)
 *   cc -std=c11 -O2 -S -fno-asynchronous-unwind-tables demo_emit.c -o -
 *   ... -ffp-contract=off / -ffp-contract=fast 로 바꿔 madd_* 비교
 */
#include <math.h>
#include <stdint.h>

float madd_expr(float a, float b, float c) { return a * b + c; }          /* 한 식 */
float madd_stmt(float a, float b, float c) { float p = a * b; return p + c; } /* 두 문장 */
float dot3_c(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

int32_t  cast_f2i(float x)  { return (int32_t)x; }     /* 범위 밖이면 UB — 그래도 명령은? */
uint32_t cast_f2u(float x)  { return (uint32_t)x; }
int32_t  c_lrint(float x)   { return (int32_t)lrintf(x); }
int32_t  c_round(float x)   { return (int32_t)roundf(x); }
int32_t  c_floor(float x)   { return (int32_t)floorf(x); }
float    c_adc(uint32_t raw, float vref) { return (float)raw / 4096.0f * vref; }
int32_t  c_q16(float x)     { return (int32_t)(x * 65536.0f); }
float    c_k(void)          { return 0.1f; }           /* fmov 즉시값으로 못 만드는 상수 */
float    c_k2(void)         { return 2.5f; }           /* 만들 수 있는 상수 */
