/* ARM-B05 demo: cmp 결과 NZCV 와 조건 코드 14개가 참인지 표로 출력
 *   cc -std=c11 -O2 demo_flags.c demo_flags.S -o /tmp/ARM-B05_flags && /tmp/ARM-B05_flags
 * 조건식은 Arm ARM 의 ConditionHolds() 정의 그대로 (N Z C V 비트로만 계산).
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

uint32_t nzcv_cmp64(int64_t a, int64_t b);
uint32_t nzcv_cmp32(uint32_t a, uint32_t b);

static void row(const char *label, uint32_t f) {
    int N = f >> 3 & 1, Z = f >> 2 & 1, C = f >> 1 & 1, V = f & 1;
    int hold[14] = { Z, !Z, C, !C, N, !N, V, !V, C && !Z, !(C && !Z), N == V, N != V, !Z && N == V, !(!Z && N == V) };
    printf("%-34s N%d Z%d C%d V%d |", label, N, Z, C, V);
    for (int i = 0; i < 14; i++) printf(" %s", hold[i] ? " 1" : " .");
    printf("\n");
}

int main(void) {
    printf("%-34s %-11s | eq ne hs lo mi pl vs vc hi ls ge lt gt le\n", "cmp a, b", "flags");
    row("x: -1, 1", nzcv_cmp64(-1, 1));
    row("x: 1, -1", nzcv_cmp64(1, -1));
    row("x: 5, 5", nzcv_cmp64(5, 5));
    row("x: -5, 0   (sign_sel)", nzcv_cmp64(-5, 0));
    row("x: INT64_MIN, 1", nzcv_cmp64(INT64_MIN, 1));
    row("x: INT64_MAX, -1", nzcv_cmp64(INT64_MAX, -1));
    row("w: 5, 0xFFFFFFF0   (tick wrap)", nzcv_cmp32(5, 0xFFFFFFF0u));
    row("w: 0x7FFFFFF0, 0x80000010", nzcv_cmp32(0x7FFFFFF0u, 0x80000010u));
    return 0;
}
