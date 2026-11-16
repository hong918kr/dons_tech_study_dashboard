/* TOOL-M10 demo_attach.c — 실행 중인 프로세스에 `leaks <pid>` 붙이기 (demo_tools.sh 가 실행)
 *   ./attach inline : main 안에서 malloc 후 p = NULL — 사본이 스택/레지스터에 남을 수 있음
 *                     (블록 주소를 stdout 에 찍는다 → leaks --trace=<addr> 로 누가 가리키는지 확인)
 *   ./attach scrub  : 다른 함수에서 잃어버린 뒤, 스택을 0 으로 덮는 함수를 한 번 부름
 * 두 경우 모두 3초 잠든다 → 그동안 leaks 가 붙는다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

__attribute__((noinline)) static void lose_one(void) {
    char *p = malloc(40);
    if (p) strcpy(p, "lost");
}
__attribute__((noinline)) static void scrub_stack(void) {
    volatile char buf[512];
    for (int i = 0; i < 512; i++) buf[i] = 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "scrub") == 0) {
        lose_one();
        scrub_stack();
    } else {
        char *p = malloc(40);
        if (p) strcpy(p, "lost");
        printf("%p\n", (void *)p);
        fflush(stdout);
        p = NULL;
        (void)p;
    }
    sleep(3);
    return 0;
}
