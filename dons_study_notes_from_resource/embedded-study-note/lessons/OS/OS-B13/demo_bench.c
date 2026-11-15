/* OS-B13 demo_bench.c — 4 KB 저장 한 번에 드는 시간: write / fsync / F_BARRIERFSYNC / F_FULLFSYNC / atomic_save
 *
 * 방법마다 워밍업 20 회 뒤 N 회 측정 × RUNS 회. 출력은 run 별 중앙값들의 중앙값과, 전체 표본의 p90/p99 (µs).
 * 절대값은 기계·APFS 상태·디스크 사용량에 따라 크게 바뀐다. 비율을 볼 것.
 * 파일은 mkdtemp("/tmp/OS-B13_b.XXXXXX") 안에 만들고 끝나면 지운다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o db && ./db [N=200] [RUNS=5]
 */
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { SZ = 4096, WARM = 20, MAXN = 2000, MAXRUNS = 9 };
static char dir[64], path[128], tpath[128];
static unsigned char buf[SZ];
static int dirfd_g;

typedef enum { SYNC_NONE, SYNC_FSYNC, SYNC_BARRIER, SYNC_FULL } sync_t;
static int do_sync(int fd, sync_t s) {
    switch (s) {
    case SYNC_NONE:    return 0;
    case SYNC_FSYNC:   return fsync(fd);
    case SYNC_BARRIER: return fcntl(fd, F_BARRIERFSYNC);
    case SYNC_FULL:    return fcntl(fd, F_FULLFSYNC);
    }
    return -1;
}
static void die(const char *what) { perror(what); exit(1); }

/* 방법 1~4: 같은 파일 0 번 오프셋에 4 KB 덮어쓰기 + sync */
static int g_fd;
static void m_overwrite(sync_t s) {
    if (pwrite(g_fd, buf, SZ, 0) != SZ) die("pwrite");
    if (do_sync(g_fd, s) != 0) die("sync");
}
/* 방법 5~7: tmp 쓰기 → sync(file) → close → rename → sync(dir) */
static void m_atomic(sync_t file_s, sync_t dir_s) {
    int fd = open(tpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) die("open tmp");
    if (write(fd, buf, SZ) != SZ) die("write");
    if (do_sync(fd, file_s) != 0) die("sync file");
    if (close(fd) != 0) die("close");
    if (rename(tpath, path) != 0) die("rename");
    if (do_sync(dirfd_g, dir_s) != 0) die("sync dir");
}

typedef struct { const char *name; int kind; sync_t a, b; } method_t;
static const method_t M[] = {
    { "pwrite only (no sync)",                  0, SYNC_NONE,    SYNC_NONE },
    { "pwrite + fsync",                         0, SYNC_FSYNC,   SYNC_NONE },
    { "pwrite + F_BARRIERFSYNC",                0, SYNC_BARRIER, SYNC_NONE },
    { "pwrite + F_FULLFSYNC",                   0, SYNC_FULL,    SYNC_NONE },
    { "atomic_save fsync / fsync(dir)",         1, SYNC_FSYNC,   SYNC_FSYNC },
    { "atomic_save BARRIER / FULLFSYNC(dir)",   1, SYNC_BARRIER, SYNC_FULL },
    { "atomic_save FULLFSYNC / FULLFSYNC(dir)", 1, SYNC_FULL,    SYNC_FULL },
};

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}
static uint64_t pct(uint64_t *v, int n, int p) {         /* v 는 정렬되어 있어야 함, nearest-rank */
    int i = (p * n + 99) / 100 - 1;
    if (i < 0) i = 0;
    return v[i];
}

int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 200, runs = argc > 2 ? atoi(argv[2]) : 5;
    if (n < 10 || n > MAXN || runs < 1 || runs > MAXRUNS) { fprintf(stderr, "usage: %s [N 10..2000] [RUNS 1..9]\n", argv[0]); return 2; }
    for (int i = 0; i < SZ; i++) buf[i] = (unsigned char)(i * 31u);
    snprintf(dir, sizeof dir, "/tmp/OS-B13_b.XXXXXX");
    if (!mkdtemp(dir)) die("mkdtemp");
    snprintf(path, sizeof path, "%s/cfg", dir);
    snprintf(tpath, sizeof tpath, "%s/cfg.tmp", dir);
    if ((dirfd_g = open(dir, O_RDONLY | O_DIRECTORY)) < 0) die("open dir");
    if ((g_fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0) die("open");
    m_overwrite(SYNC_FSYNC);

    static uint64_t all[MAXN * MAXRUNS], one[MAXN];
    printf("4 KB, N=%d x RUNS=%d, unit = microseconds (median of run medians | pooled p90 | pooled p99)\n", n, runs);
    for (size_t m = 0; m < sizeof M / sizeof M[0]; m++) {
        uint64_t meds[MAXRUNS];
        int na = 0;
        for (int r = 0; r < runs; r++) {
            for (int i = 0; i < WARM + n; i++) {
                uint64_t t0 = now_ns();
                if (M[m].kind == 0) m_overwrite(M[m].a); else m_atomic(M[m].a, M[m].b);
                uint64_t t1 = now_ns();
                if (i >= WARM) { one[i - WARM] = t1 - t0; all[na++] = t1 - t0; }
            }
            qsort(one, (size_t)n, sizeof one[0], cmp_u64);
            meds[r] = pct(one, n, 50);
        }
        qsort(meds, (size_t)runs, sizeof meds[0], cmp_u64);
        qsort(all, (size_t)na, sizeof all[0], cmp_u64);
        printf("  %-40s %9.1f | %9.1f | %9.1f\n", M[m].name,
               meds[runs / 2] / 1000.0, pct(all, na, 90) / 1000.0, pct(all, na, 99) / 1000.0);
        fflush(stdout);
    }
    close(g_fd);
    close(dirfd_g);
    unlink(path);
    unlink(tpath);
    if (rmdir(dir) != 0) perror("rmdir");
    return 0;
}
