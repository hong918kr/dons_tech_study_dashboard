/* C-M09 demo — atoi 의 UB 와 "int 로 누적하는" 순진한 구현
 *   cc -std=c11 -O0 demo_atoi_ub.c -o d0 && ./d0
 *   cc -std=c11 -O2 demo_atoi_ub.c -o d2 && ./d2
 *   cc -std=c11 -O1 -fsanitize=undefined demo_atoi_ub.c -o du && ./du
 * starter/solution 과 달리 이 파일은 일부러 UB 를 일으킨다 (관찰용).
 */
#include <stdio.h>
#include <stdlib.h>

/* 흔한 면접 답안: int 로 바로 누적 → 큰 입력에서 signed overflow (C11 6.5p5, UB) */
__attribute__((noinline)) static int naive_atoi(const char *s) {
    int r = 0, neg = 0;
    if (*s == '-' || *s == '+') neg = (*s++ == '-');
    while (*s >= '0' && *s <= '9') r = r * 10 + (*s++ - '0');
    return neg ? -r : r;                       /* "2147483648" 을 누적하는 순간 이미 UB */
}

/* "넘친 뒤에 검사" — 넘침 자체가 UB 라서 컴파일러는 이 검사를 믿지 않아도 된다 */
__attribute__((noinline)) static int after_the_fact(const char *s, int *ovf) {
    int r = 0;
    *ovf = 0;
    while (*s >= '0' && *s <= '9') {
        int t = r * 10 + (*s++ - '0');
        if (t < r) *ovf = 1;                   /* wrap 했으면 작아졌겠지? */
        r = t;
    }
    return r;
}

/* "곱한 뒤 나눠서 되돌아오나" 검사 — signed 곱셈은 넘치지 않는다고 가정해도 되므로
 * 컴파일러는 r * 10 / 10 을 r 로 바꿔도 된다 → 검사가 통째로 사라질 수 있다 */
__attribute__((noinline)) static int div_check(const char *s, int *ovf) {
    int r = 0;
    *ovf = 0;
    while (*s >= '0' && *s <= '9') {
        if (r * 10 / 10 != r) *ovf = 1;
        r = r * 10 + (*s++ - '0');
    }
    return r;
}

int main(void) {
    static const char *in[] = { "2147483647", "2147483648", "-2147483648", "4294967296", "10000000000", "99999999999" };
    for (int i = 0; i < 6; i++) {
        int ovf, ovf2;
        int a = atoi(in[i]);
        int n = naive_atoi(in[i]);
        int c = after_the_fact(in[i][0] == '-' ? in[i] + 1 : in[i], &ovf);
        (void)div_check(in[i][0] == '-' ? in[i] + 1 : in[i], &ovf2);
        printf("%-12s atoi=%-11d naive=%-11d after_the_fact=%-11d ovf=%d  div_check ovf=%d\n",
               in[i], a, n, c, ovf, ovf2);
    }
    return 0;
}
