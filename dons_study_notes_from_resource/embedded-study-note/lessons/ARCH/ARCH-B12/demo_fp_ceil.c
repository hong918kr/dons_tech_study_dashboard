/* ARCH-B12 demo — WS 를 double 로 계산하면 경계에서 틀린다 (같은 수학, 다른 반올림).
 * build: cc -std=c11 -O2 demo_fp_ceil.c -o fp && ./fp
 */
#include <math.h>
#include <stdio.h>
int main(void) {
    volatile double t_ns = 30, f = 100e6;          /* volatile: 상수 접기 방지 */
    double a = t_ns * 1e-9 * f, b = 30e-9 * f;
    printf("t_ns*1e-9*f = %.17g  ceil-1 = %g\n", a, ceil(a) - 1);
    printf("30e-9*f     = %.17g  ceil-1 = %g\n", b, ceil(b) - 1);
    return 0;
}
