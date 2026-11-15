/* ALGO-B15 demo: strtoul 을 콘솔 인자 검증에 그대로 쓰면 생기는 함정들 (이 맥의 실제 동작).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_strtoul.c -o demo_strtoul && ./demo_strtoul
 * Cortex-M 쪽 폭 확인: clang --target=thumbv7em-none-eabi -dM -E -x c /dev/null | grep -E '__SIZEOF_LONG__|__LONG_MAX__'
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static void show(const char *s, int base) {
    char *end;
    errno = 0;
    unsigned long v = strtoul(s, &end, base);
    int e = errno;
    printf("  %-26s base %2d -> %20lu (0x%016lx)  end=+%-2td '%s'  errno=%s\n",
           s, base, v, v, end - s, end, e == 0 ? "0" : e == ERANGE ? "ERANGE" : e == EINVAL ? "EINVAL" : "?");
}

int main(void) {
    printf("sizeof(unsigned long) = %zu, ULONG_MAX = %lu\n", sizeof(unsigned long), ULONG_MAX);
    show("4", 0);
    show("-1", 0);                     /* 음수가 조용히 ULONG_MAX 로 */
    show("-4", 10);
    show("", 0);                       /* 숫자 없음: 0, end == s */
    show("   42", 0);                  /* 앞 공백 허용 */
    show("+42", 0);                    /* + 부호 허용 */
    show("42abc", 0);                  /* 찌꺼기: end 로만 알 수 있다 */
    show("0x", 0);                     /* "0" 만 변환, end=+1 */
    show("010", 0);                    /* base 0 → 8진수 8 */
    show("0x4000_0010", 0);            /* '_' 에서 멈춤 */
    show("4294967296", 0);             /* LP64: 정상 / ILP32(Cortex-M): ERANGE */
    show("99999999999999999999999", 0);/* 넘침: ULONG_MAX + ERANGE */
    show("-99999999999999999999999", 0);

    /* errno 는 성공한 호출이 0 으로 지워 주지 않는다 */
    errno = 0;
    (void)strtoul("99999999999999999999999", NULL, 0);
    unsigned long ok = strtoul("7", NULL, 0);          /* errno = 0 을 빼먹은 두 번째 호출 */
    printf("stale errno: strtoul(\"7\") = %lu but errno is still %s\n", ok, errno == ERANGE ? "ERANGE" : "0");
    return 0;
}
