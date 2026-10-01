// C02_io.c — OSTEP Figure 2.6 (io.c) + fsync. open/write/fsync/close 4개의 시스템 콜.
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    const char *path = "/tmp/ostep_c02_file";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
    assert(fd > -1);
    ssize_t rc = write(fd, "hello world\n", 12);
    assert(rc == 12);
    // write()가 돌아와도 데이터는 아직 커널 버퍼(page cache)에 있을 수 있다.
    // fsync()를 불러야 "디스크(SSD)까지 갔다"를 요청한다.
    assert(fsync(fd) == 0);
    close(fd);

    struct stat st;
    assert(stat(path, &st) == 0);
    printf("fd=%d, wrote %zd bytes, file size on fs = %lld bytes\n", fd, rc, (long long)st.st_size);
    return 0;
}
