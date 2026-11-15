/* ALGO-B10 demo: 평균 계산이 Cortex-M 에서 무슨 명령이 되나 (<stdint.h> 만 사용 — libc 없이 크로스 컴파일)
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S demo_cm_avg.c -o - 
 * clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -O2 -S demo_cm_avg.c -o -
 */
#include <stdint.h>

uint32_t avg_div(uint32_t sum, uint32_t count) { return (sum + count / 2u) / count; } /* 웜업: 변수로 나눔 */
uint32_t avg_n64(uint32_t sum)   { return (sum + 32u) / 64u; }   /* 상수 2^k: 컴파일러가 시프트로 */
uint32_t avg_n100(uint32_t sum)  { return (sum + 50u) / 100u; }  /* 상수 2^k 아님: 곱셈+시프트 */
int32_t  savg_n64(int32_t sum)   { return sum / 64; }            /* 부호 있는 / : 0 쪽 버림이라 보정 필요 */
int32_t  sshr_n64(int32_t sum)   { return sum >> 6; }            /* 음수 >> 는 구현 정의 (보통 산술 시프트=floor) */
