/* ARM-M04 demo_lse_probe.c — LSE 명령을 ARMv8.0 대상으로 어셈블할 수 있나? (컴파일만, 실행 안 함)
 *   cc -std=c11 -O2 -c demo_lse_probe.c -o /dev/null                         Apple 기본(apple-m1): ok
 *   cc -std=c11 -O2 -mcpu=generic -c demo_lse_probe.c -o /dev/null           error: instruction requires: lse
 *   cc -std=c11 -O2 -mcpu=generic -DEXT -c demo_lse_probe.c -o /dev/null     문자열에 .arch_extension lse → ok
 *   cc -std=c11 -O2 -mcpu=generic -march=armv8.1-a -c ... / -march=armv8-a+lse   ok
 */
#include <stdint.h>
uint32_t f(uint32_t *p, uint32_t v) {
    uint32_t old;
#ifdef EXT
    __asm__ volatile(".arch_extension lse\n\tldaddal %w[v], %w[o], %[m]" : [o] "=r"(old), [m] "+Q"(*p) : [v] "r"(v) : "memory");
#else
    __asm__ volatile("ldaddal %w[v], %w[o], %[m]" : [o] "=r"(old), [m] "+Q"(*p) : [v] "r"(v) : "memory");
#endif
    return old;
}
