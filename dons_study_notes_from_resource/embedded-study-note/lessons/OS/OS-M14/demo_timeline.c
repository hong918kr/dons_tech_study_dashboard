/* OS-M14 데모: OSTEP 그림 40.3 (읽기) · 40.4 (생성 + 쓰기) 의 블록 I/O 타임라인을 우리 VSFS 로 다시 찍는다.
 * 캐시 없음(g_cache=0, OSTEP 처럼 매번 디스크) 과 연산 안 재사용(g_cache=1) 두 번.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_timeline.c -o demo_timeline && ./demo_timeline
 */
#define VSFS_NO_MAIN
#include "solution.c"

static void row(const char *op, long r0, long w0) {
    printf("  %-18s R=%-2ld W=%-2ld | %s\n", op, D.nread - r0, D.nwrite - w0, D.tbuf);
}
int main(void) {
    static uint8_t buf[BSIZE];
    for (int cache = 0; cache <= 1; cache++) {
        if (disk_open(64) != 0 || fs_mkfs(80) != 0 || fs_mkdir("/foo") != 1) return 1;
        g_cache = cache;
        printf("== cache=%d  (I0 = inode block 0 holds inodes 0..15, D0 = root dir, D1 = /foo dir)\n", cache);
        long tr = 0, tw = 0;
        io_reset(); int bar = fs_create("/foo/bar"); row("create /foo/bar", 0, 0); tr += D.nread; tw += D.nwrite;
        for (int k = 0; k < 3; k++) {
            io_reset(); fs_write((uint32_t)bar, (uint32_t)k * BSIZE, buf, BSIZE);
            char l[32]; snprintf(l, sizeof l, "write() #%d", k + 1); row(l, 0, 0); tr += D.nread; tw += D.nwrite;
        }
        printf("  %-18s R=%-2ld W=%-2ld   (OSTEP Fig 40.4: 10 + 3*5 = 25 I/O)\n", "create+3 writes", tr, tw);
        tr = tw = 0;
        io_reset(); fs_namei("/foo/bar", NULL); row("open /foo/bar", 0, 0); tr += D.nread;
        for (int k = 0; k < 3; k++) {
            io_reset(); fs_read((uint32_t)bar, (uint32_t)k * BSIZE, buf, BSIZE);
            char l[32]; snprintf(l, sizeof l, "read() #%d", k + 1); row(l, 0, 0); tr += D.nread; tw += D.nwrite;
        }
        printf("  %-18s R=%-2ld W=%-2ld   (OSTEP Fig 40.3: 5 + 3*(2R+1W atime) = 14 I/O)\n", "open+3 reads", tr, tw);
    }
    disk_close();
    return 0;
}
