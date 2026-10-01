// C04_layout.c — 프로세스 = 실행 중인 프로그램. OS가 만들어 준 "기계 상태"를 들여다본다.
// code / static data / heap / stack 주소, argc/argv 위치, 기본으로 열려 있는 fd 0/1/2.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int initialized_global = 42;   // static data (.data)
int zero_global;               // static data (.bss, 0으로 채워짐)

int main(int argc, char *argv[]) {
    int local = 7;                          // stack
    int *heap = malloc(16);                 // heap
    printf("pid %d, parent pid %d\n", (int)getpid(), (int)getppid());
    printf("code   main()             at %p\n", (void *)main);
    printf("data   initialized_global at %p (= %d)\n", (void *)&initialized_global, initialized_global);
    printf("bss    zero_global        at %p (= %d)\n", (void *)&zero_global, zero_global);
    printf("heap   malloc(16)         at %p\n", (void *)heap);
    printf("stack  local              at %p\n", (void *)&local);
    printf("stack  argv[]             at %p (argc=%d, argv[0]=\"%s\")\n", (void *)argv, argc, argv[0]);
    // OS가 미리 열어 준 표준 입출력 fd 0, 1, 2 확인
    for (int fd = 0; fd < 5; fd++) {
        int flags = fcntl(fd, F_GETFL);
        printf("fd %d: %s\n", fd, flags == -1 ? "closed" : "open");
    }
    free(heap);
    return 0;
}
