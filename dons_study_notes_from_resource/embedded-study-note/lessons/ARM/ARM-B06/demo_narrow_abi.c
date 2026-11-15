/* ARM-B06 demo: uint16_t 인자/반환값의 상위 비트는 누가 책임지나 — Apple arm64 vs 일반 AAPCS64(Linux)
 * for t in arm64-apple-macos aarch64-linux-gnu; do
 *   clang --target=$t -O2 -S -fno-asynchronous-unwind-tables -o - demo_narrow_abi.c; done */
#include <stdint.h>
uint16_t ret_narrow(uint32_t x) { return (uint16_t)x; }           /* callee 가 반환값을 확장하나?  */
uint16_t ext_h(void);
uint32_t use_ret(void) { return (uint32_t)ext_h() + 1u; }         /* caller 가 반환값을 다시 확장하나? */
uint32_t use_arg(uint16_t a) { return (uint32_t)a + 1u; }         /* callee 가 인자를 다시 확장하나?  */
uint16_t c_bswap16(uint16_t x) { return (uint16_t)((x >> 8) | (x << 8)); }
