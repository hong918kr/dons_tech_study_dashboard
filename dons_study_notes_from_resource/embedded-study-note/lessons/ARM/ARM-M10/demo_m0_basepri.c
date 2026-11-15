/* ENV: qemu */
/* ARM-M10 demo_m0_basepri.c — Cortex-M0 (ARMv6-M) 에는 BASEPRI / FAULTMASK 가 없다.
 * GNU as 는 -mcpu=cortex-m0 에서도 이 명령들을 조용히 어셈블한다 → QEMU -M microbit 에서 실행해 본다.
 * run_qemu.sh m0
 */
#include <stdint.h>
void sh_puts(const char *s);
void sh_hex(uint32_t v);
int main(void) {
    uint32_t b, f;
    __asm__ volatile("movs r0, #0x80\n\tmsr basepri, r0\n\tmrs %0, basepri" : "=r"(b) :: "r0");
    __asm__ volatile("cpsid f\n\tmrs %0, faultmask" : "=r"(f));
    sh_puts("M0: BASEPRI after msr 0x80 = "); sh_hex(b);
    sh_puts("  FAULTMASK after cpsid f = "); sh_hex(f); sh_puts("\n");
    return 0;
}
