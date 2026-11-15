/* ARM-B03 demo_consts.c — clang 이 상수와 마스크를 어떻게 만드는지 본다 (실행 안 함)
 *   clang -O2 -S -fno-asynchronous-unwind-tables demo_consts.c -o -
 */
#include <stdint.h>
/* Part A: 상수 만들기 */
uint64_t c_deadbeef(void)      { return 0xDEADBEEFull; }
uint64_t c_cafe(void)          { return 0xCAFEF00DDEADBEEFull; }
uint64_t c_neg_mostly(void)    { return 0xFFFFFFFF12345678ull; }
uint64_t c_minus2(void)        { return (uint64_t)-2; }
uint64_t c_tie(void)           { return 0xFFFF00000000FFFFull; }
uint64_t c_5555(void)          { return 0x5555555555555555ull; }
uint64_t c_00ff(void)          { return 0x00FF00FF00FF00FFull; }
uint64_t c_00ff_1234(void)     { return 0x00FF00FF00FF1234ull; }
uint64_t c_rep32(void)         { return 0x1234567812345678ull; }
uint64_t c_uart(void)          { return 0x09000000ull; }
/* Part C: x & mask 비용 */
uint64_t and_ff00(uint64_t x)  { return x & 0x000000000000FF00ull; }
uint64_t and_00ff(uint64_t x)  { return x & 0x00FF00FF00FF00FFull; }
uint64_t and_1234(uint64_t x)  { return x & 0x1234ull; }
uint64_t and_cafe(uint64_t x)  { return x & 0xCAFEF00DDEADBEEFull; }
uint32_t and32_12345678(uint32_t x) { return x & 0x12345678u; }
uint32_t clr32_ff00(uint32_t x)     { return x & ~0x0000FF00u; }
uint32_t and32_7fc0(uint32_t x)     { return x & 0x00007FC0u; }
uint32_t and32_5(uint32_t x)        { return x & 0x00000005u; }
uint32_t and32_a5(uint32_t x)       { return x & 0xA5A5A5A5u; }
uint32_t and32_ffff1234(uint32_t x) { return x & 0xFFFF1234u; }
/* Part C: RCC_PLLCFGR.PLLN = 168 패치를 C 로 쓰면? */
uint32_t plln168_c(uint32_t r)      { return (r & ~0x7FC0u) | (168u << 6); }
