/* BM-B12 demo: STM32F4 USART BRR 표 + carry 버그 + "최근접 div 가 항상 최소 baud 오차인가?"
 * cc -std=c11 -Wall -Wextra -Werror -O2 demo_brr_table.c -o /tmp/BM-B12_demo && /tmp/BM-B12_demo
 * 공식 (RM0090 기준, 확인 권장): baud = fck / (8·(2−OVER8)·USARTDIV), BRR = mantissa<<4 | fraction
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t div_round(uint32_t fck, uint32_t baud) { return (uint32_t)((2ull * fck + baud) / (2ull * baud)); }
static double err_pct(uint32_t fck, uint32_t baud, uint32_t div) { return 100.0 * ((double)fck / div - baud) / baud; }

/* 흔한 두 단계 계산: 정수부 floor + 소수부 반올림. carry 처리 방식만 다르다. */
static uint32_t brr_two_step(uint32_t fck, uint32_t baud, int carry_mode) {
    uint32_t mant = fck / (16u * baud);
    uint32_t rem = fck - mant * 16u * baud;                       /* 소수부 × 16·baud */
    uint32_t frac = (uint32_t)((2ull * rem + baud) / (2ull * baud)); /* round(16 × 소수부), 0..16 */
    if (carry_mode == 0) return (mant << 4) | (frac & 0xFu);      /* 버그: 16 을 잘라 버림 */
    if (carry_mode == 1) return (mant << 4) | frac;               /* 버그: OR — mant 짝수일 때만 우연히 맞음 */
    return (mant << 4) + frac;                                     /* 정답: 더하면 carry 가 mantissa 로 */
}

int main(void) {
    static const uint32_t fcks[] = { 16000000u, 42000000u, 45000000u, 84000000u };
    static const uint32_t bauds[] = { 9600u, 19200u, 57600u, 115200u, 230400u, 460800u, 921600u, 2000000u };
    printf("fck(MHz)  baud     | OVER8=0: BRR    USARTDIV   err%%   | OVER8=1: BRR    err%%\n");
    for (unsigned i = 0; i < sizeof fcks / sizeof fcks[0]; i++) {
        for (unsigned j = 0; j < sizeof bauds / sizeof bauds[0]; j++) {
            uint32_t f = fcks[i], b = bauds[j], d = div_round(f, b);
            printf("%6.1f  %8u  |", f / 1e6, (unsigned)b);
            if (d >= 16 && d / 16 <= 0xFFF)
                printf("  0x%04X  %9.4f  %+7.3f |", (unsigned)(((d / 16) << 4) | (d % 16)), d / 16.0, err_pct(f, b, d));
            else printf("  %-27s|", "(불가)");
            if (d >= 8 && d / 8 <= 0xFFF)
                printf("  0x%04X  %+7.3f\n", (unsigned)(((d / 8) << 4) | (d % 8)), err_pct(f, b, d));
            else printf("  (불가)\n");
        }
    }

    printf("\ncarry 사례 (OVER8=0): 세 가지 두 단계 구현 vs 정답\n");
    static const uint32_t cf[][2] = { { 8158400u, 10000u }, { 45000000u, 9600u }, { 16000000u, 41700u }, { 42000000u, 50500u } };
    for (unsigned k = 0; k < 4; k++) {
        uint32_t f = cf[k][0], b = cf[k][1], ok = brr_two_step(f, b, 2);
        printf("  fck=%u baud=%u fck/baud=%.3f: &0xF→0x%X(%+.2f%%)  |→0x%X(%+.2f%%)  +→0x%X(%+.3f%%)\n",
               (unsigned)f, (unsigned)b, (double)f / b,
               (unsigned)brr_two_step(f, b, 0), err_pct(f, b, ((brr_two_step(f, b, 0) >> 4) << 4) + (brr_two_step(f, b, 0) & 0xF)),
               (unsigned)brr_two_step(f, b, 1), err_pct(f, b, ((brr_two_step(f, b, 1) >> 4) << 4) + (brr_two_step(f, b, 1) & 0xF)),
               (unsigned)ok, err_pct(f, b, ((ok >> 4) << 4) + (ok & 0xF)));
    }

    /* 최근접 div (|div − fck/baud| 최소) 와 baud 오차 최소 div 는 다를 수 있다:
     * 경계가 산술평균 D+0.5 가 아니라 조화평균 2D(D+1)/(2D+1) 이기 때문. 몇 번이나? */
    for (unsigned i = 0; i < sizeof fcks / sizeof fcks[0]; i++) {
        uint32_t f = fcks[i], diff = 0, carry = 0, total = 0, worst_b = 0;
        double worst = 0;
        for (uint32_t b = 1200; b <= f / 16u; b++) {
            uint32_t d = div_round(f, b);
            if (d / 16 > 0xFFF) continue;
            total++;
            uint32_t lo = f / b, hi = lo + 1;                       /* 후보 두 개 */
            /* |f/lo − b| vs |f/hi − b| 를 정수로: (f − b·lo)/lo vs (b·hi − f)/hi → 교차곱 */
            uint64_t e_lo = (uint64_t)(f - (uint64_t)b * lo) * hi, e_hi = (uint64_t)((uint64_t)b * hi - f) * lo;
            uint32_t best = (e_hi <= e_lo) ? hi : lo;
            if (f % b == 0) best = lo;
            if (best != d) {
                diff++;
                double gain = 1e6 * (fabs((double)f / d - b) - fabs((double)f / best - b)) / b;  /* 손해 ppm */
                if (gain > worst) { worst = gain; worst_b = b; }
            }
            if (brr_two_step(f, b, 0) != brr_two_step(f, b, 2)) carry++;
        }
        printf("fck=%u: baud 1200..%u (%u개) 중 최근접≠최소오차 %u개 (최대 손해 %.0f ppm @%u), &0xF carry 버그 영향 %u개\n",
               (unsigned)f, (unsigned)(f / 16u), (unsigned)total, (unsigned)diff, worst, (unsigned)worst_b, (unsigned)carry);
    }
    return 0;
}
