// C13_address_space.c — 내 프로세스의 주소 공간 지도 그리기 + "모든 주소는 가상" 확인
// build: cc -Wall -Wextra -O0 code/C13_address_space.c -o .work/bin/C13_address_space
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

int g_init = 42;        // .data  (초기값 있는 전역)
int g_zero;             // .bss   (0으로 초기화되는 전역)
const char *g_str = "hi"; // 문자열 리터럴은 읽기 전용 영역(__TEXT,__cstring)

static void deeper(int depth) {
    int local;
    printf("  stack  depth %d : %p\n", depth, (void *)&local);
    if (depth < 2) deeper(depth + 1);   // 재귀할수록 주소가 작아진다 = 스택은 아래로 자람
}

int main(void) {
    void *h1 = malloc(16);
    void *h2 = malloc(16);
    void *big = malloc(1 << 20);            // 1MB: macOS malloc은 큰 요청을 별도 영역에서 준다
    void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    int x = 3;

    printf("[1] 주소 공간 지도 (pid %d)\n", getpid());
    printf("  code   main()   : %p\n", (void *)main);
    printf("  rodata \"hi\"     : %p\n", (void *)g_str);
    printf("  data   g_init   : %p\n", (void *)&g_init);
    printf("  bss    g_zero   : %p\n", (void *)&g_zero);
    printf("  heap   malloc#1 : %p\n", h1);
    printf("  heap   malloc#2 : %p\n", h2);
    printf("  heap   1MB      : %p\n", big);
    printf("  mmap   4KB anon : %p\n", m);
    printf("  stack  x        : %p\n", (void *)&x);
    deeper(0);

    // [2] 같은 가상 주소, 다른 값: fork 후 부모·자식이 &g_init 을 각각 고친다
    printf("\n[2] fork 후 같은 가상 주소 %p 에 서로 다른 값\n", (void *)&g_init);
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        g_init = 1111;
        printf("  child : &g_init=%p value=%d\n", (void *)&g_init, g_init);
        exit(0);
    }
    waitpid(pid, NULL, 0);
    g_init = 2222;
    printf("  parent: &g_init=%p value=%d\n", (void *)&g_init, g_init);

    free(h1); free(h2); free(big);
    munmap(m, 4096);
    return 0;
}
