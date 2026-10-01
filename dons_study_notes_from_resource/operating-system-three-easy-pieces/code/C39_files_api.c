// C39_files_api.c — OSTEP Ch.39 파일/디렉터리 API 를 실제 syscall 로 하나씩 확인
//
//  (strace 대신) 각 syscall 의 반환값을 직접 출력한다. macOS / Linux 모두 동작.
//  1) open/write/read/close, fd 번호가 3부터 시작하는 이유
//  2) lseek: SEEK_SET/CUR/END, 파일 끝 너머(1GiB) 쓰기 → sparse file(st_size vs st_blocks)
//  3) stat: inode 번호, link count
//  4) hard link vs symbolic link, unlink, dangling symlink, 열린 fd 는 unlink 후에도 읽힘
//  5) mkdir / readdir / rmdir(비어 있지 않으면 ENOTEMPTY)
//  6) atomic update 패턴: tmp 에 write → fsync → rename → 디렉터리 fsync
//  7) fsync vs F_FULLFSYNC(macOS) 지연 측정
//
// build: cc -Wall -Wextra -O0 code/C39_files_api.c -o .work/bin/C39_files_api
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do { if ((x) < 0) { perror(#x); exit(1); } } while (0)

static void show_stat(const char *path, int follow) {
    struct stat st;
    int rc = follow ? stat(path, &st) : lstat(path, &st);
    if (rc < 0) { printf("    %-6s %-10s -> -1 (%s)\n", follow ? "stat" : "lstat", path, strerror(errno)); return; }
    const char *type = S_ISREG(st.st_mode) ? "regular" : S_ISDIR(st.st_mode) ? "directory"
                     : S_ISLNK(st.st_mode) ? "symlink" : "other";
    printf("    %-6s %-10s ino=%-10llu links=%u size=%-8lld blocks(512B)=%-4lld %s\n",
           follow ? "stat" : "lstat", path, (unsigned long long)st.st_ino, (unsigned)st.st_nlink,
           (long long)st.st_size, (long long)st.st_blocks, type);
}

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(void) {
    char dir[] = "/tmp/ostep39.XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    CHECK(chdir(dir));
    printf("work dir: (temp dir)\n");

    // ---- 1) open/write/read
    printf("\n[1] open/write/read/close\n");
    int fd = open("foo", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    printf("    open(\"foo\", O_CREAT|O_WRONLY|O_TRUNC) = %d   (0,1,2 = stdin/stdout/stderr)\n", fd);
    printf("    write(%d, \"hello\\n\", 6) = %zd\n", fd, write(fd, "hello\n", 6));
    printf("    close(%d) = %d\n", fd, close(fd));
    char buf[4096];
    fd = open("foo", O_RDONLY);
    ssize_t n = read(fd, buf, sizeof buf);
    printf("    open(\"foo\", O_RDONLY) = %d ; read(%d, buf, 4096) = %zd ; ", fd, fd, n);
    printf("read again = %zd (EOF)\n", read(fd, buf, sizeof buf));
    close(fd);

    // ---- 2) lseek
    printf("\n[2] lseek & sparse file\n");
    fd = open("sparse", O_CREAT | O_RDWR | O_TRUNC, 0644);
    printf("    write 4 bytes -> offset now %lld (SEEK_CUR,0)\n", (write(fd, "ABCD", 4), (long long)lseek(fd, 0, SEEK_CUR)));
    printf("    lseek(fd, 1 GiB, SEEK_SET) = %lld  (디스크 I/O 없음: 커널 변수만 바뀜)\n", (long long)lseek(fd, 1 << 30, SEEK_SET));
    CHECK(write(fd, "Z", 1));
    printf("    lseek(fd, 0, SEEK_END) = %lld\n", (long long)lseek(fd, 0, SEEK_END));
    CHECK(lseek(fd, 2, SEEK_SET));
    n = read(fd, buf, 4);
    printf("    pread-like: lseek(2) + read(4) = %zd bytes \"%.2s\" + hole bytes %d %d\n", n, buf, buf[2], buf[3]);
    close(fd);
    show_stat("sparse", 1);
    struct stat sp; stat("sparse", &sp);
    printf("    -> st_size = %lld B, 실제 할당 = st_blocks*512 = %lld B  (hole 은 할당 없이 0 으로 읽힘)\n",
           (long long)sp.st_size, (long long)sp.st_blocks * 512);

    // ---- 3,4) links
    printf("\n[3] hard link: 같은 inode 에 이름을 하나 더\n");
    show_stat("foo", 1);
    CHECK(link("foo", "foo2"));   printf("    link(\"foo\",\"foo2\") = 0\n");
    CHECK(link("foo2", "foo3"));  printf("    link(\"foo2\",\"foo3\") = 0\n");
    show_stat("foo", 1); show_stat("foo3", 1);

    printf("\n[4] symlink: 경로 문자열을 데이터로 가진 별도 파일\n");
    CHECK(symlink("foo", "sym"));   printf("    symlink(\"foo\",\"sym\") = 0\n");
    show_stat("sym", 0); show_stat("sym", 1);
    n = readlink("sym", buf, sizeof buf); buf[n] = 0;
    printf("    readlink(\"sym\") = \"%s\" (%zd bytes = lstat size)\n", buf, n);

    int keep = open("foo3", O_RDONLY);           // 열린 fd 하나 유지
    printf("    unlink(\"foo\") = %d\n", unlink("foo"));
    show_stat("foo2", 1);
    fd = open("sym", O_RDONLY);
    printf("    open(\"sym\") after unlink(foo) = %d (%s)  <- dangling symlink\n", fd, fd < 0 ? strerror(errno) : "ok");
    printf("    unlink(\"foo2\") = %d, unlink(\"foo3\") = %d  -> link count 0, 이름은 모두 사라짐\n",
           unlink("foo2"), unlink("foo3"));
    n = read(keep, buf, sizeof buf);
    printf("    but read(keep_fd) = %zd \"%.5s\"  <- 열린 fd 가 있으면 inode 는 close 때까지 살아 있음\n", n, buf);
    close(keep);

    // ---- 5) directories
    printf("\n[5] mkdir / readdir / rmdir\n");
    CHECK(mkdir("d", 0755)); show_stat("d", 1);
    fd = open("d/a", O_CREAT | O_WRONLY, 0644); close(fd);
    printf("    + file d/a\n"); show_stat("d", 1);
    CHECK(mkdir("d/sub", 0755));
    printf("    + dir  d/sub\n"); show_stat("d", 1);
    printf("    -> ext4 등 전통 UNIX FS: 2 + 하위 '디렉터리' 수. APFS 는 파일 엔트리까지 세어 보고한다\n");
    DIR *dp = opendir("d"); struct dirent *de;
    while ((de = readdir(dp)) != NULL)
        printf("    readdir: ino=%-10llu type=%-3s name=%s\n", (unsigned long long)de->d_ino,
               de->d_type == DT_DIR ? "dir" : de->d_type == DT_REG ? "reg" : "?", de->d_name);
    closedir(dp);
    printf("    rmdir(\"d\") = %d (%s)\n", rmdir("d"), strerror(errno));
    unlink("d/a"); rmdir("d/sub");
    printf("    after emptying: rmdir(\"d\") = %d\n", rmdir("d"));

    // ---- 6) atomic update
    printf("\n[6] atomic file update: write tmp -> fsync -> rename -> fsync(dir)\n");
    fd = open("conf", O_CREAT | O_WRONLY | O_TRUNC, 0644); CHECK(write(fd, "version=1\n", 10)); fsync(fd); close(fd);
    struct stat before; stat("conf", &before);
    fd = open("conf.tmp", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    CHECK(write(fd, "version=2\n", 10));
    printf("    fsync(tmp) = %d\n", fsync(fd)); close(fd);
    printf("    rename(\"conf.tmp\",\"conf\") = %d\n", rename("conf.tmp", "conf"));
    int dfd = open(".", O_RDONLY);
    printf("    fsync(dir fd) = %d   (rename 이라는 '디렉터리 변경'을 영속화)\n", fsync(dfd)); close(dfd);
    struct stat after; stat("conf", &after);
    fd = open("conf", O_RDONLY); n = read(fd, buf, sizeof buf); close(fd);
    printf("    conf now: \"%.9s\", inode %llu -> %llu (이름은 같고 inode 가 통째로 바뀜)\n", buf,
           (unsigned long long)before.st_ino, (unsigned long long)after.st_ino);

    // ---- 7) fsync cost
    printf("\n[7] durability cost: 4KB write + sync, 50 rounds\n");
    memset(buf, 'x', sizeof buf);
    fd = open("log", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    double t0 = now_ms();
    for (int i = 0; i < 50; i++) CHECK(write(fd, buf, 4096));
    double t_none = (now_ms() - t0) / 50;
    t0 = now_ms();
    for (int i = 0; i < 50; i++) { CHECK(write(fd, buf, 4096)); CHECK(fsync(fd)); }
    double t_fsync = (now_ms() - t0) / 50;
#ifdef F_FULLFSYNC
    t0 = now_ms();
    for (int i = 0; i < 50; i++) { CHECK(write(fd, buf, 4096)); CHECK(fcntl(fd, F_FULLFSYNC)); }
    double t_full = (now_ms() - t0) / 50;
    printf("    write only        : %8.3f ms/op\n    write + fsync     : %8.3f ms/op\n    write + F_FULLFSYNC: %7.3f ms/op\n",
           t_none, t_fsync, t_full);
#else
    printf("    write only : %8.3f ms/op\n    write+fsync: %8.3f ms/op\n", t_none, t_fsync);
#endif
    close(fd); unlink("log"); unlink("conf"); unlink("sparse"); unlink("sym");
    CHECK(chdir("/")); CHECK(rmdir(dir));
    printf("\ncleanup done\n");
    return 0;
}
