/* ARM-A04 demo_ctr.c — CTR_EL0 를 EL0 에서 읽어 디코드 (리눅스용: 커널이 읽기를 허용하거나 에뮬레이션)
 *
 * Lima VM: gcc -O2 -static demo_ctr.c -o /tmp/ARM-A04_work/ctr && /tmp/ARM-A04_work/ctr
 *          qemu-aarch64 -cpu cortex-a53 /tmp/ARM-A04_work/ctr        (QEMU CPU 모델 값)
 * macOS 에서는 mrs ctr_el0 가 SIGILL 로 죽는다 (demo_wx.c 참고).
 */
#include <stdint.h>
#include <stdio.h>
int main(void) {
    uint64_t c, m;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(c));
    __asm__ volatile("mrs %0, midr_el1" : "=r"(m));          /* 리눅스는 MIDR_EL1 읽기를 트랩해서 에뮬레이션 */
    unsigned erg = (c >> 20) & 15, cwg = (c >> 24) & 15;
    printf("MIDR_EL1=0x%08llx CTR_EL0=0x%09llx  IminLine=%uB DminLine=%uB L1Ip=%u ERG=%uB CWG=%uB IDC=%u DIC=%u\n",
           (unsigned long long)m, (unsigned long long)c, 4u << (c & 15), 4u << ((c >> 16) & 15), (unsigned)(c >> 14) & 3,
           erg ? 4u << erg : 2048, cwg ? 4u << cwg : 2048, (unsigned)(c >> 28) & 1, (unsigned)(c >> 29) & 1);
    return 0;
}
