/* ARM-B07 demo: clang -O2 이 C 곱셈·나눗셈을 어떤 AArch64 명령으로 내리는가
 * clang -O2 -S -fno-asynchronous-unwind-tables -o - demo_clang.c | grep -v '^\s*\.'
 */
#include <stdint.h>

unsigned __int128 c_mul128_u(uint64_t a, uint64_t b) { return (unsigned __int128)a * b; }
__int128          c_mul128_s(int64_t a, int64_t b)   { return (__int128)a * b; }
uint64_t c_madd(uint64_t a, uint64_t b, uint64_t c)  { return c + a * b; }
uint64_t c_widen_ok(uint32_t a, uint32_t b)          { return (uint64_t)a * b; }
uint64_t c_widen_bug(uint32_t a, uint32_t b)         { return a * b; }      /* 32비트 곱 후 확장 */
uint64_t c_mod_u(uint64_t a, uint64_t b)             { return a % b; }
int64_t  c_mod_s(int64_t a, int64_t b)               { return a % b; }
uint64_t c_div10(uint64_t x)                         { return x / 10; }
int64_t  c_sdiv8(int64_t x)                          { return x / 8; }      /* 음수 보정 필요 */
int      c_mul_ovf(uint64_t a, uint64_t b, uint64_t *r) { return __builtin_mul_overflow(a, b, r); }
