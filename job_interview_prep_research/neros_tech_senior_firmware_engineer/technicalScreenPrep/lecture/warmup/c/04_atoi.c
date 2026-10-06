/* 04 · atoi 직접 구현
 * prompt: int my_atoi(const char *s) — 앞 공백 무시, + / - 부호, 숫자가 아닌 글자에서 멈춤, int 범위를 넘으면 INT_MAX / INT_MIN.
 * note: 순서: 공백 건너뛰기 → 부호 → 숫자 누적 (v * 10 + (c - '0'))
 * note: 오버플로는 long long 으로 누적하며 매 자리 범위 검사 (int 로 누적하면 UB)
 */
// --- solution ---
#include <assert.h>
#include <limits.h>
#include <stdio.h>

int my_atoi(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    int sign = 1;
    if (*s == '+' || *s == '-')
        sign = (*s++ == '-') ? -1 : 1;
    long long v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s++ - '0');
        if (sign * v > INT_MAX)
            return INT_MAX;
        if (sign * v < INT_MIN)
            return INT_MIN;
    }
    return (int)(sign * v);
}

static void test_atoi(void)
{
    assert(my_atoi("  -17x") == -17 && my_atoi("abc") == 0);
    assert(my_atoi("99999999999") == INT_MAX && my_atoi("-2147483648") == INT_MIN);
}

int main(void)
{
    test_atoi();
    puts("PASS");
    return 0;
}
// --- end ---
