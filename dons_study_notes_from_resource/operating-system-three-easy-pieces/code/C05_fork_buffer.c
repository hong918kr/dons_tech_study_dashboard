// C05_fork_buffer.c — 면접 단골: printf 한 줄이 왜 두 번 찍히나?
// stdio 버퍼는 "유저 공간 메모리"라서 fork 때 주소 공간과 함께 복사된다.
//  - 터미널(tty)로 출력: 줄 단위 버퍼링 → '\n' 있는 줄은 fork 전에 이미 나감
//  - 파이프/파일로 출력: 블록(4KB+) 버퍼링 → '\n'이 있어도 버퍼에 남아 둘 다 출력
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    printf("line with newline\n");
    printf("line without newline... ");
    if (fork() == 0) {
        printf("[child]\n");
        return 0;              // return/exit() 시 남은 버퍼가 flush 된다
    }
    wait(NULL);
    printf("[parent]\n");
    return 0;
}
