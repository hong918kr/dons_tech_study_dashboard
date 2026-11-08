/* C-M10 demo — INT_MIN 에서 깨지는 "교과서" itoa 두 가지 (일부러 UB 를 포함한 관찰용 코드)
 *   cc -std=c11 -O0 demo_intmin.c -o d0 && ./d0
 *   cc -std=c11 -O2 demo_intmin.c -o d2 && ./d2
 *   cc -std=c11 -g -O1 -fsanitize=undefined demo_intmin.c -o du && ./du
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/* ① 부호를 떼고 -v 로 양수를 만든다 → v == INT_MIN 이면 -v 가 오버플로 (C11 6.5p5 UB) */
static char *itoa_negate(int v, char *buf) {
    char tmp[16], *p = buf;
    int n = 0;
    if (v < 0) { *p++ = '-'; v = -v; }
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v > 0);
    while (n > 0) *p++ = tmp[--n];
    *p = '\0';
    return buf;
}
/* ② abs() 도 같다: abs(INT_MIN) 은 표현 불가 → UB (C11 7.22.6.1p2) */
static char *itoa_abs(int v, char *buf) {
    char tmp[16], *p = buf;
    int n = 0;
    unsigned m = (unsigned)abs(v);
    if (v < 0) *p++ = '-';
    do { tmp[n++] = (char)('0' + m % 10); m /= 10; } while (m > 0);
    while (n > 0) *p++ = tmp[--n];
    *p = '\0';
    return buf;
}
/* ③ 안전: unsigned 로 바꾼 뒤 0u - m (mod 2^32 연산, 잘 정의됨) */
static char *itoa_ok(int v, char *buf) {
    char tmp[16], *p = buf;
    int n = 0;
    unsigned m = (unsigned)v;
    if (v < 0) { *p++ = '-'; m = 0u - m; }
    do { tmp[n++] = (char)('0' + m % 10); m /= 10; } while (m > 0);
    while (n > 0) *p++ = tmp[--n];
    *p = '\0';
    return buf;
}

int main(void) {
    char b[24];
    volatile int x = INT_MIN;                 /* 상수 접기 방지 */
    printf("negate: \"%s\"\n", itoa_negate(x, b));
    printf("abs   : \"%s\"\n", itoa_abs(x, b));
    printf("ok    : \"%s\"\n", itoa_ok(x, b));
    printf("-7 %% 10 = %d, -7 / 10 = %d   (C99+: 0 쪽으로 버림)\n", -7 % 10, -7 / 10);
    return 0;
}
