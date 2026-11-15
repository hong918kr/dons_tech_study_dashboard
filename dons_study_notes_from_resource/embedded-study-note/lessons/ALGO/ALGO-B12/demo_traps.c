/* ALGO-B12 demo: 비트 트릭의 함정을 실제로 밟아 본다 (일부러 UB 를 일으키는 줄이 있다 — UBSan 으로 볼 것)
 *
 *   1. signed int 에 x & -x, x & (x-1)       → INT_MIN 에서 부호 있는 오버플로(UB)
 *   2. __builtin_ctz(0) / __builtin_clz(0)  → 정의되지 않음
 *   3. 1u << (32 - clz(x-1)) 식 next_pow2   → x=1 에서 clz(0), x>2^31 에서 32칸 시프트(UB)
 *   4. 가드 없는 고전 smear next_pow2       → 0 과 2^31 초과에서 조용히 0
 *   5. ALIGN_UP 매크로의 ~(a-1) 폭 함정       → 64비트 주소의 상위 32비트가 지워짐 (UB 아님, 그냥 틀린 값)
 *   6. 32비트 ALIGN_UP 의 넘침               → 0 으로 감김
 *   7. uint16_t 의 x & -x                   → int 로 승격되어 계산된다 (값은 맞음, 타입은 int)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -g -O0 -fsanitize=undefined demo_traps.c -o demo_traps && ./demo_traps
 */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#define ALIGN_UP_BAD(x, a)  (((x) + (a) - 1) & ~((a) - 1))                 /* 흔히 보는 형태 */
#define ALIGN_UP_OK(x, a)   (((x) + ((__typeof__(x))(a) - 1)) & ~((__typeof__(x))(a) - 1))  /* 리눅스식: a 를 x 의 타입으로 */

static uint32_t next_pow2_smear_noguard(uint32_t x) {
    x--;
    x |= x >> 1; x |= x >> 2; x |= x >> 4; x |= x >> 8; x |= x >> 16;
    return x + 1u;
}
static uint32_t next_pow2_clz(uint32_t x) { return 1u << (32 - __builtin_clz(x - 1u)); }

int main(void) {
    volatile int vi = INT_MIN;
    volatile uint32_t v0 = 0, v1 = 1, vbig = 0x80000001u;

    printf("--- 1. signed int ---\n");
    int s = vi;
    printf("INT_MIN & -INT_MIN      = %d\n", s & -s);
    printf("INT_MIN & (INT_MIN - 1) = %d\n", s & (s - 1));
    uint32_t u = (uint32_t)vi;
    printf("as uint32_t: lowbit = 0x%08X, clear_lowbit = 0x%08X\n", u & (0u - u), u & (u - 1u));

    printf("--- 2. builtin ctz/clz of 0 ---\n");
    printf("__builtin_ctz(0) = %d\n", __builtin_ctz(v0));
    printf("__builtin_clz(0) = %d\n", __builtin_clz(v0));

    printf("--- 3. next_pow2 via clz ---\n");
    printf("next_pow2_clz(1)          = %u\n", next_pow2_clz(v1));
    printf("next_pow2_clz(0x80000001) = %u\n", next_pow2_clz(vbig));

    printf("--- 4. smear without guards ---\n");
    printf("smear(0) = %u, smear(0x80000001) = %u, smear(0xFFFFFFFF) = %u\n",
           next_pow2_smear_noguard(v0), next_pow2_smear_noguard(vbig), next_pow2_smear_noguard(0xFFFFFFFFu));

    printf("--- 5. ALIGN_UP type width (addr = 0x1_2345_6789) ---\n");
    volatile uint64_t addr = 0x123456789ull;
    uint32_t a32 = 64;
    printf("BAD(addr, 64u)            = 0x%llX\n", (unsigned long long)ALIGN_UP_BAD(addr, 64u));
    printf("BAD(addr, uint32_t a=64)  = 0x%llX\n", (unsigned long long)ALIGN_UP_BAD(addr, a32));
    printf("BAD(addr, 64)  [int]      = 0x%llX\n", (unsigned long long)ALIGN_UP_BAD(addr, 64));
    printf("OK (addr, 64u)            = 0x%llX\n", (unsigned long long)ALIGN_UP_OK(addr, 64u));
    printf("~(64u - 1) as uint64_t    = 0x%016llX\n", (unsigned long long)(uint64_t)~(64u - 1));
    printf("~(64 - 1)  as uint64_t    = 0x%016llX\n", (unsigned long long)(uint64_t)~(64 - 1));

    printf("--- 6. 32-bit ALIGN_UP wrap ---\n");
    volatile uint32_t top = 0xFFFFFFF0u;
    printf("ALIGN_UP_BAD(0xFFFFFFF0u, 64u) = 0x%08X\n", (unsigned)ALIGN_UP_BAD(top, 64u));

    printf("--- 7. uint16_t promotion ---\n");
    volatile uint16_t h = 0x0050;
    printf("uint16_t 0x0050: h & -h = 0x%X, sizeof(h & -h) = %zu, sizeof((uint16_t)(0u - h)) = %zu\n",
           (unsigned)(h & -h), sizeof(h & -h), sizeof((uint16_t)(0u - h)));
    return 0;
}
