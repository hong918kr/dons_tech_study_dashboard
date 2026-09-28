#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    printf("stdin=%d stdout=%d stderr=%d\n", STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);
    int fd = open("/dev/urandom", O_RDONLY);
    printf("open(\"/dev/urandom\") -> fd %d\n", fd);
    unsigned char buf[4];
    ssize_t n = read(fd, buf, sizeof buf);
    printf("read -> %zd bytes: %02x %02x %02x %02x\n", n, buf[0], buf[1], buf[2], buf[3]);
    int fd2 = open("/dev/urandom", O_RDONLY);
    printf("두 번째 open -> fd %d (숫자만 다르다)\n", fd2);
    close(fd); close(fd2);
    return 0;
}
