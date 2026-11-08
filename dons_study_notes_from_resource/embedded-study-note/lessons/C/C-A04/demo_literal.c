/* C-A04 demo: strtok 계열은 입력을 고친다 → 문자열 리터럴(읽기 전용)에 쓰면 UB (실제로는 크래시).
 *   cc -std=c11 -Wall -Wextra -O0 demo_literal.c -o literal && ./literal                # 경고 없음, 실행 시 죽는다
 *   cc -std=c11 -Wall -Wextra -Wwrite-strings -O0 -c demo_literal.c -o /dev/null        # 컴파일러가 잡게 하기 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>

int main(void) {
    char ok[] = "led,1";           /* 배열: 리터럴의 "사본" 이 스택에 → 고쳐도 된다 */
    char *sv;
    printf("array  : '%s'\n", strtok_r(ok, ",", &sv));
    fflush(stdout);
    char *ro = "led,1";            /* 포인터: 리터럴 자체(__TEXT,__cstring / ELF .rodata / MCU 플래시) */
    printf("literal: '%s'\n", strtok_r(ro, ",", &sv));   /* ',' 자리에 '\0' 을 쓰려다 죽는다 */
    return 0;
}
