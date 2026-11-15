/* ARCH-B01 demo: n = 64 마스크와 음수 왼쪽 시프트 — UB 가 실제로 무엇을 내놓나
 *
 *   naive_mask(n) = (1ull << n) - 1     n = 64 에서 UB (C11 6.5.7p3: 시프트 양 >= 폭)
 *   safe_mask(n)  = 분기로 n >= 64 처리
 *   shr_mask(n)   = UINT64_MAX >> (64 - n)   n = 1..64 에서 안전 (대신 n = 0 이 UB)
 *   imm << 2      음수 imm 의 왼쪽 시프트 = UB (C11 6.5.7p4). imm * 4 는 정의됨.
 *
 * build: cc -std=c11 -O2 demo_ub_shift.c -o /tmp/esn_ARCH-B01/ub && /tmp/esn_ARCH-B01/ub
 *        cc -std=c11 -O2 -fsanitize=undefined demo_ub_shift.c -o /tmp/esn_ARCH-B01/ubsan && /tmp/esn_ARCH-B01/ubsan
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) static uint64_t naive_mask(unsigned n) { return (1ull << n) - 1; }
__attribute__((noinline)) static uint64_t safe_mask(unsigned n)  { return n >= 64 ? UINT64_MAX : (UINT64_C(1) << n) - 1; }
__attribute__((noinline)) static uint64_t shr_mask(unsigned n)   { return UINT64_MAX >> (64 - n); }
__attribute__((noinline)) static int64_t  times4_shift(int64_t imm) { return imm << 2; }
__attribute__((noinline)) static int64_t  times4_mul(int64_t imm)   { return imm * 4; }

int main(void) {
    static volatile unsigned ns[] = {62, 63, 64};          /* volatile: 상수 접기 방지 */
    for (int i = 0; i < 3; i++) {
        unsigned n = ns[i];
        printf("n=%u  naive=0x%016" PRIX64 "  safe=0x%016" PRIX64 "  shr=0x%016" PRIX64 "\n",
               n, naive_mask(n), safe_mask(n), shr_mask(n));
    }
    volatile int64_t imm = -3;                             /* imm26 을 부호 확장한 값이라고 치자 */
    printf("imm=-3  imm<<2=%" PRId64 "  imm*4=%" PRId64 "\n", times4_shift(imm), times4_mul(imm));
    return 0;
}
