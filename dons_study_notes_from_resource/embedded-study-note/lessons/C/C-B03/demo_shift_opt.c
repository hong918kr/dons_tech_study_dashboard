/* C-B03 demo — 같은 소스, 같은 칩인데 -O 레벨에 따라 답이 바뀐다 (UB 의 진짜 위험)
 *   cc -std=c11 -O0 demo_shift_opt.c -o opt0 && ./opt0
 *   cc -std=c11 -O2 demo_shift_opt.c -o opt2 && ./opt2
 */
#include <stdio.h>

static volatile unsigned vn = 32;

/* "시프트 결과가 0이면 n이 너무 큰 것" 이라는 (틀린) 사후 검사 */
__attribute__((noinline)) static int mask_is_zero(unsigned n) {
    return (1u << n) == 0;
}

int main(void) {
    printf("mask_is_zero(32) = %d\n", mask_is_zero(vn));
    return 0;
}
