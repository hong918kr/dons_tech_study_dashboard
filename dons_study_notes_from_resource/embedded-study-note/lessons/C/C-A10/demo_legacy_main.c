/* demo_legacy_main.c — 레거시 드라이버의 자체 시험(selftest)을 돌린다 (C-A10)
 *
 *   cc -std=c11 -Wall -Wextra -Werror legacy_driver.c demo_legacy_main.c -o legacy && ./legacy
 *
 * -Wall -Wextra -Werror 로도 경고 0 으로 빌드되고, selftest 도 통과한다 — 그런데도 버그가 가득하다.
 */
#include <stdio.h>

int ls_selftest(void);

int main(void)
{
    int ok = ls_selftest();
    printf("selftest ok=%d\n", ok);
    return ok ? 0 : 1;
}
