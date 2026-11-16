/* ARM-A10 demo_probe_pac.c — PAC 명령을 직접 실행해 (1) 이 프로세스에서 키가 켜져 있나 (2) PAC 이 어느 비트에 들어가나 를 잰다.
 * Mac:  cc -O2 demo_probe_pac.c -o p && ./p            (arm64)
 *       cc -O2 -arch arm64e demo_probe_pac.c -o pe && ./pe
 * VM :  gcc -O2 demo_probe_pac.c -o p && ./p           (Linux: ID 레지스터도 읽는다 — 커널이 EL0 MRS 를 에뮬레이트) */
#include <stdio.h>
#include <stdint.h>
#ifdef __APPLE__
#define T __attribute__((target("v8.3a")))
#else
#define T __attribute__((target("arch=armv8.3-a")))
#endif
T static uint64_t pia(uint64_t p, uint64_t m) { __asm__ volatile("pacia %0, %1" : "+r"(p) : "r"(m)); return p; }
T static uint64_t pib(uint64_t p, uint64_t m) { __asm__ volatile("pacib %0, %1" : "+r"(p) : "r"(m)); return p; }
T static uint64_t pda(uint64_t p, uint64_t m) { __asm__ volatile("pacda %0, %1" : "+r"(p) : "r"(m)); return p; }
T static uint64_t pga(uint64_t p, uint64_t m) { uint64_t r; __asm__ volatile("pacga %0, %1, %2" : "=r"(r) : "r"(p), "r"(m)); return r; }
T static uint64_t xpi(uint64_t p) { __asm__ volatile("xpaci %0" : "+r"(p)); return p; }
#define X(v) ((unsigned long long)(v))
int main(void) {
    const uint64_t p = 0x0000000100004000ull, m = 0x16fdff000ull;
    printf("pacia(p, m)      = %#018llx\n", X(pia(p, m)));
    printf("pacia(p, m) again= %#018llx\n", X(pia(p, m)));
    printf("pacia(p, m+16)   = %#018llx\n", X(pia(p, m + 16)));
    printf("pacib(p, m)      = %#018llx\n", X(pib(p, m)));
    printf("pacda(p, m)      = %#018llx\n", X(pda(p, m)));
    printf("pacga(p, m)      = %#018llx   (범용 키: 32비트 MAC 이 상위 절반에)\n", X(pga(p, m)));
    printf("xpaci(pacib)     = %#018llx\n", X(xpi(pib(p, m))));
    uint64_t oi = 0, od = 0;
    for (uint64_t k = 0; k < 4096; k++) { oi |= pia(p, k * 16) ^ p; od |= pda(p, k * 16) ^ p; }
    printf("4096 modifiers: I-key PAC bits = %#018llx (%d bits), D-key PAC bits = %#018llx (%d bits)\n",
           X(oi), __builtin_popcountll(oi), X(od), __builtin_popcountll(od));
#ifndef __APPLE__
    uint64_t isar1, isar2, pfr1;
    __asm__ volatile("mrs %0, ID_AA64ISAR1_EL1" : "=r"(isar1));
    __asm__ volatile("mrs %0, S3_0_C0_C6_2" : "=r"(isar2));          /* ID_AA64ISAR2_EL1 */
    __asm__ volatile("mrs %0, ID_AA64PFR1_EL1" : "=r"(pfr1));
    printf("ID_AA64ISAR1_EL1 = %#018llx  APA=%llu API=%llu GPA=%llu GPI=%llu\n", X(isar1),
           X(isar1 >> 4 & 15), X(isar1 >> 8 & 15), X(isar1 >> 24 & 15), X(isar1 >> 28 & 15));
    printf("ID_AA64ISAR2_EL1 = %#018llx  GPA3=%llu APA3=%llu\n", X(isar2), X(isar2 >> 8 & 15), X(isar2 >> 12 & 15));
    printf("ID_AA64PFR1_EL1  = %#018llx  BT=%llu\n", X(pfr1), X(pfr1 & 15));
#endif
    return 0;
}
