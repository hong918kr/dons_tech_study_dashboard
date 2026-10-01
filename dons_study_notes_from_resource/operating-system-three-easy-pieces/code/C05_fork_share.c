// C05_fork_share.c — Homework 1, 2: fork 후 무엇이 복사되고 무엇이 공유되나?
//  (1) 변수 x: 주소 공간이 복사되므로 각자 따로 바뀐다.
//  (2) fork 전에 open()한 fd: "열린 파일 항목(offset 포함)"을 공유 → 쓰기가 덮어쓰지 않고 이어 붙는다.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    int x = 100;
    const char *path = "/tmp/ostep_c05_shared.txt";
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);

    int rc = fork();
    if (rc == 0) {
        x += 1;
        printf("child : x=%d (&x=%p)\n", x, (void *)&x);
        fflush(stdout);   // _exit()는 stdio 버퍼를 비우지 않으므로 직접 flush
        for (int i = 0; i < 3; i++) {
            write(fd, "child\n", 6);
            usleep(1000);
        }
        _exit(0);
    }
    x += 1000;
    printf("parent: x=%d (&x=%p)\n", x, (void *)&x);
    for (int i = 0; i < 3; i++) {
        write(fd, "PARENT\n", 7);
        usleep(1000);
    }
    wait(NULL);
    off_t end = lseek(fd, 0, SEEK_CUR);
    printf("parent: shared offset after both wrote = %lld (3*6 + 3*7 = 39)\n", (long long)end);
    close(fd);
    fflush(stdout);
    system("cat /tmp/ostep_c05_shared.txt");
    return 0;
}
