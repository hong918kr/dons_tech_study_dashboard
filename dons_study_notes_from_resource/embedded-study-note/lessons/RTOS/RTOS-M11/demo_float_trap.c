/* RTOS-M11 demo — 스케줄 가능성 판정을 double 로 하면 생기는 함정 3가지.
 * build: cc -std=c11 -Wall -Wextra -O2 demo_float_trap.c -o demo_float_trap && ./demo_float_trap
 * 정수 판정(본문 도구)과 같은 세트를 double 로 계산해 결과가 갈리는 지점을 보여 준다.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    /* 1) U <= 1 필요조건: C = 200, 400, 300, 100 µs, T = 1000 µs (조화 주기, 정확히 U = 1, RTA 통과) */
    const double c[] = { 200, 400, 300, 100 };
    double u = 0.0;
    for (int i = 0; i < 4; i++) u += c[i] / 1000.0;
    uint32_t p = 0;                                     /* 정수: sum C (T 가 같으니 분자만 더하면 된다) */
    for (int i = 0; i < 4; i++) p += (uint32_t)c[i];
    printf("1) U as double = %.17g -> (u <= 1.0) is %s | integer: sum C = %u vs T = 1000 -> %s\n", u,
           u <= 1.0 ? "true" : "FALSE", p, p <= 1000 ? "U <= 1" : "U > 1");

    /* 2) 쌍곡선 경계: (1 + 1/6)(1 + 5/7) = 7/6 * 12/7 = 2 정확히 → PASS 여야 한다 */
    double hb = (1.0 + 1.0 / 6.0) * (1.0 + 5.0 / 7.0);
    unsigned long long lhs = (1ull + 6) * (5ull + 7), rhs = 2ull * 6 * 7;
    printf("2) hyperbolic product as double = %.17g -> (<= 2.0) is %s | integer: %llu <= %llu -> %s\n", hb,
           hb <= 2.0 ? "true" : "FALSE", lhs, rhs, lhs <= rhs ? "PASS" : "FAIL");

    /* 3) ms 단위 double 로 ceil: R = 2.1 ms, T = 0.3 ms → 정확히 7 번인데 */
    double q = 2.1 / 0.3;
    printf("3) ceil(2.1 / 0.3) = ceil(%.17g) = %.0f | integer µs: ceil(2100 / 300) = %u\n", q, ceil(q),
           (2100u + 300u - 1u) / 300u);

    /* 3') 그 ceil 이 RTA 를 뒤집는 세트: hp (C=0.1 ms, T=0.3 ms), low (C=1.4 ms, D=T=2.1 ms) */
    double w = 1.4, nw = 0;
    int it = 0;
    for (;;) {
        nw = 1.4 + ceil(w / 0.3) * 0.1;
        it++;
        printf("   double iter %d: w = %.17g\n", it, nw);
        if (nw > 2.1) { printf("   double RTA: R = %.17g > D = 2.1 -> MISS\n", nw); break; }
        if (nw == w) { printf("   double RTA: R = %.17g -> ok\n", nw); break; }
        w = nw;
    }
    uint32_t wi = 1400, nwi;
    for (;;) {
        nwi = 1400 + (wi + 300 - 1) / 300 * 100;
        if (nwi > 2100 || nwi == wi) break;
        wi = nwi;
    }
    printf("   integer RTA (µs): R = %u -> %s\n", nwi, nwi <= 2100 ? "ok (R == D)" : "MISS");
    return 0;
}
