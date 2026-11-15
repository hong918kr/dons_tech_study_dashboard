/* ARCH-B02 demo_x86_flags.c — x86 의 CF 는 borrow 그 자체 (Arm C 의 반대) 를 실제 x86 명령으로 확인
 *
 *   Apple Silicon 에서 x86_64 로 빌드해 Rosetta 2 로 실행한다:
 *     cc -std=c11 -Wall -Wextra -Werror -O2 -arch x86_64 demo_x86_flags.c -o demo_x86_flags && ./demo_x86_flags
 *   SUB/ADD 직후 pushfq 로 EFLAGS 를 읽는다: CF=bit0, ZF=bit6, SF=bit7, OF=bit11.
 *   (Rosetta 가 번역한 결과이므로 "진짜 Intel/AMD 칩" 측정은 아니다 — 의미 확인용.)
 */
#include <stdint.h>
#include <stdio.h>

#if !defined(__x86_64__)
#error "x86_64 로 빌드하세요: -arch x86_64 (Rosetta 로 실행)"
#endif

enum { CF = 1u << 0, ZF = 1u << 6, SF = 1u << 7, OF = 1u << 11 };

static uint64_t x86_sub(uint32_t a, uint32_t b) {
    uint64_t f;
    __asm__ volatile("subl %2, %1\n\tpushfq\n\tpopq %0" : "=r"(f), "+r"(a) : "r"(b) : "cc");
    return f & (CF | ZF | SF | OF);
}
static uint64_t x86_add(uint32_t a, uint32_t b) {
    uint64_t f;
    __asm__ volatile("addl %2, %1\n\tpushfq\n\tpopq %0" : "=r"(f), "+r"(a) : "r"(b) : "cc");
    return f & (CF | ZF | SF | OF);
}

/* Arm 모델 (solution.c 와 같은 식) */
static uint8_t arm_nzcv(uint32_t a, uint32_t b, unsigned cin) {
    uint64_t s = (uint64_t)a + b + cin;
    uint32_t r = (uint32_t)s;
    return (uint8_t)(((r >> 31) & 1u) << 3 | (r == 0) << 2 | (unsigned)((s >> 32) & 1u) << 1 |
                     ((((a ^ r) & (b ^ r)) >> 31) & 1u));
}

static uint32_t rng = 0x2545F491u;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

int main(void) {
    const uint32_t s[][2] = { { 5, 3 }, { 3, 5 }, { 7, 7 }, { 0x1234, 0 }, { 0x80000000u, 1 } };
    printf("SUB a-b     x86 CF ZF SF OF | Arm N Z C V\n");
    for (unsigned i = 0; i < 5; i++) {
        uint64_t f = x86_sub(s[i][0], s[i][1]);
        uint8_t  g = arm_nzcv(s[i][0], ~s[i][1], 1);
        printf("%08X-%-3X     %u  %u  %u  %u |     %u %u %u %u\n", s[i][0], s[i][1],
               (unsigned)!!(f & CF), (unsigned)!!(f & ZF), (unsigned)!!(f & SF), (unsigned)!!(f & OF),
               g >> 3 & 1u, g >> 2 & 1u, g >> 1 & 1u, g & 1u);
    }
    /* 무작위 1,000,000쌍: SUB 에서 CF == !C, ADD 에서 CF == C, 나머지 Z/N/V 는 같다 */
    long bad_sub = 0, bad_add = 0;
    for (int i = 0; i < 1000000; i++) {
        uint32_t a = xs32(), b = xs32();
        if (i % 8 == 0) b = a;                       /* ZF 케이스도 섞는다 */
        uint8_t g = arm_nzcv(a, ~b, 1);
        uint64_t want = (g & 2 ? 0 : CF) | (g & 4 ? ZF : 0) | (g & 8 ? SF : 0) | (g & 1 ? OF : 0);
        if (x86_sub(a, b) != want) bad_sub++;
        g = arm_nzcv(a, b, 0);
        want = (g & 2 ? CF : 0) | (g & 4 ? ZF : 0) | (g & 8 ? SF : 0) | (g & 1 ? OF : 0);
        if (x86_add(a, b) != want) bad_add++;
    }
    printf("SUB: x86 CF == !Arm C  mismatches = %ld / 1000000\n", bad_sub);
    printf("ADD: x86 CF ==  Arm C  mismatches = %ld / 1000000\n", bad_add);
    return (bad_sub || bad_add) ? 1 : 0;
}
