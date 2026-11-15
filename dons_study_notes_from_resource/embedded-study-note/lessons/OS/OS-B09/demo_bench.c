/* OS-B09 demo_bench.c — 파일 복사 속도: read/write(버퍼 크기 3가지) vs mmap 두 가지, 그리고 msync/fsync 비용
 *
 * usage: ./bench [size_MB=256] [runs=5]
 *   - 작업 파일은 /tmp/OS-B09_work/ 아래에 만들고 끝나면 지운다.
 *   - src 는 방금 써서 페이지 캐시에 있다 = "warm cache" 측정이다. cold cache 는 `sudo purge`
 *     가 필요해서 이 데모는 하지 않는다 (sudo 를 쓰지 않음).
 *   - dst 는 매 실행 지우고 새로 만든다. 복사 시간에 fsync 는 넣지 않는다 (= 페이지 캐시까지).
 *     디스크까지의 비용은 아래 두 번째 표 (msync MS_SYNC / fsync) 에서 따로 본다.
 *   - 방법들을 한 바퀴씩 번갈아 runs 번 돌리고 중앙값을 낸다. 첫 바퀴 전에 워밍업 1회.
 *   - 각 방법의 결과 파일은 src 와 바이트 단위로 비교해 확인한다 (시간 밖에서).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench && ./bench 256 5
 */
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DIR_  "/tmp/OS-B09_work"
#define SRC   DIR_ "/bench_src"
#define DST   DIR_ "/bench_dst"

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}
static long minflt(void) { struct rusage r; getrusage(RUSAGE_SELF, &r); return r.ru_minflt; }
static long majflt(void) { struct rusage r; getrusage(RUSAGE_SELF, &r); return r.ru_majflt; }

static int copy_rw(const char *src, const char *dst, size_t bufsz) {
    int in = open(src, O_RDONLY), out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (in < 0 || out < 0) return -1;
    char *buf = malloc(bufsz);
    int rc = 0;
    for (;;) {
        ssize_t n = read(in, buf, bufsz);
        if (n == 0) break;
        if (n < 0) { if (errno == EINTR) continue; rc = -1; break; }
        for (ssize_t off = 0; off < n; ) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));
            if (w < 0) { if (errno == EINTR) continue; rc = -1; break; }
            off += w;
        }
    }
    free(buf); close(in); close(out);
    return rc;
}

/* mmap(src) + mmap(dst) + memcpy */
static int copy_mmap2(const char *src, const char *dst) {
    int in = open(src, O_RDONLY), out = open(dst, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (in < 0 || out < 0) return -1;
    struct stat st; fstat(in, &st);
    size_t n = (size_t)st.st_size;
    if (ftruncate(out, (off_t)n) != 0) return -1;
    void *s = mmap(NULL, n, PROT_READ, MAP_SHARED, in, 0);
    void *d = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, out, 0);
    if (s == MAP_FAILED || d == MAP_FAILED) return -1;
    memcpy(d, s, n);
    munmap(s, n); munmap(d, n); close(in); close(out);
    return 0;
}

/* 위와 같고 madvise 로 "순차로 곧 읽을 것" 을 커널에 알린다 */
static int copy_mmap2_advise(const char *src, const char *dst) {
    int in = open(src, O_RDONLY), out = open(dst, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (in < 0 || out < 0) return -1;
    struct stat st; fstat(in, &st);
    size_t n = (size_t)st.st_size;
    if (ftruncate(out, (off_t)n) != 0) return -1;
    void *s = mmap(NULL, n, PROT_READ, MAP_SHARED, in, 0);
    void *d = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, out, 0);
    if (s == MAP_FAILED || d == MAP_FAILED) return -1;
    madvise(s, n, MADV_SEQUENTIAL); madvise(s, n, MADV_WILLNEED);
    madvise(d, n, MADV_SEQUENTIAL);
    memcpy(d, s, n);
    munmap(s, n); munmap(d, n); close(in); close(out);
    return 0;
}

/* mmap(src) + write(dst) — 사용자 버퍼 복사가 한 번 줄어든다 (src 페이지 → 커널이 바로 읽음) */
static int copy_mmap_write(const char *src, const char *dst) {
    int in = open(src, O_RDONLY), out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (in < 0 || out < 0) return -1;
    struct stat st; fstat(in, &st);
    size_t n = (size_t)st.st_size;
    char *s = mmap(NULL, n, PROT_READ, MAP_SHARED, in, 0);
    if (s == MAP_FAILED) return -1;
    for (size_t off = 0; off < n; ) {
        size_t k = n - off < (1u << 20) ? n - off : (1u << 20);
        ssize_t w = write(out, s + off, k);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        off += (size_t)w;
    }
    munmap(s, n); close(in); close(out);
    return 0;
}

static int same_file(const char *a, const char *b, size_t n) {
    int fa = open(a, O_RDONLY), fb = open(b, O_RDONLY);
    struct stat sb; fstat(fb, &sb);
    if ((size_t)sb.st_size != n) return 0;
    void *x = mmap(NULL, n, PROT_READ, MAP_SHARED, fa, 0), *y = mmap(NULL, n, PROT_READ, MAP_SHARED, fb, 0);
    int eq = memcmp(x, y, n) == 0;
    munmap(x, n); munmap(y, n); close(fa); close(fb);
    return eq;
}

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

enum { NM = 6, MAXR = 31 };
static const char *mname[NM] = {"read/write  4 KiB buf", "read/write 64 KiB buf", "read/write  1 MiB buf",
                                "mmap src + mmap dst + memcpy", "mmap src + write() 1 MiB",
                                "mmap+madvise(SEQ|WILLNEED)+memcpy"};

static int run_method(int k) {
    switch (k) {
    case 0: return copy_rw(SRC, DST, 4096);
    case 1: return copy_rw(SRC, DST, 65536);
    case 2: return copy_rw(SRC, DST, 1u << 20);
    case 3: return copy_mmap2(SRC, DST);
    case 4: return copy_mmap_write(SRC, DST);
    default: return copy_mmap2_advise(SRC, DST);
    }
}

int main(int argc, char **argv) {
    size_t mb = argc > 1 ? (size_t)atoi(argv[1]) : 256;
    int runs = argc > 2 ? atoi(argv[2]) : 5;
    if (mb < 1 || runs < 1 || runs > MAXR) { fprintf(stderr, "usage: %s [MB>=1] [runs 1..%d]\n", argv[0], MAXR); return 2; }
    size_t n = mb << 20;
    mkdir(DIR_, 0700);

    /* src 만들기 (xorshift 패턴 — 0 페이지가 아니라 진짜 데이터) */
    int fd = open(SRC, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    uint32_t s = 2463534242u;
    uint32_t *blk = malloc(1u << 20);
    for (size_t off = 0; off < n; off += 1u << 20) {
        for (size_t i = 0; i < (1u << 20) / 4; i++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; blk[i] = s; }
        if (write(fd, blk, 1u << 20) != (1 << 20)) { perror("write"); return 1; }
    }
    close(fd);
    free(blk);
    printf("file %zu MiB, %d runs (median), page size %ld, warm page cache (src just written), no fsync in copy\n",
           mb, runs, sysconf(_SC_PAGESIZE));

    double t[NM][MAXR];
    long flt[NM] = {0}, mj[NM] = {0};
    int ok[NM];
    for (int k = 0; k < NM; k++) {            /* 워밍업 + 결과 검증 (시간 밖) */
        unlink(DST);
        ok[k] = run_method(k) == 0 && same_file(SRC, DST, n);
    }
    for (int r = 0; r < runs; r++)
        for (int k = 0; k < NM; k++) {
            unlink(DST);
            long f0 = minflt(), m0 = majflt();
            double t0 = now_ms();
            run_method(k);
            t[k][r] = now_ms() - t0;
            flt[k] += minflt() - f0;
            mj[k] += majflt() - m0;
        }
    printf("%-34s %10s %10s %12s %8s %s\n", "method", "median ms", "MB/s", "minflt/run", "majflt", "verified");
    for (int k = 0; k < NM; k++) {
        qsort(t[k], (size_t)runs, sizeof(double), cmp_d);
        double med = t[k][runs / 2];
        printf("%-34s %10.1f %10.0f %12ld %8ld %s\n", mname[k], med, (double)mb / (med / 1e3),
               flt[k] / runs, mj[k] / runs, ok[k] ? "yes" : "NO");
    }
    unlink(DST);

    /* 두 번째 표: 64 MiB 를 더럽힌 뒤 저장장치로 밀어내는 비용. 매번 새 파일. */
    size_t dn = 64u << 20;
    double a_ms[MAXR], as_ms[MAXR], s_ms[MAXR], f_ms[MAXR];
    for (int r = 0; r < runs; r++) {
        for (int v = 0; v < 2; v++) {          /* v=0: ASYNC 다음 SYNC,  v=1: SYNC 만 */
            int d = open(DST, O_RDWR | O_CREAT | O_TRUNC, 0644);
            if (ftruncate(d, (off_t)dn) != 0) return 1;
            char *p = mmap(NULL, dn, PROT_READ | PROT_WRITE, MAP_SHARED, d, 0);
            memset(p, 0x5A + r + v, dn);
            double t0 = now_ms();
            if (v == 0) msync(p, dn, MS_ASYNC);
            double t1 = now_ms();
            msync(p, dn, MS_SYNC);
            double t2 = now_ms();
            if (v == 0) { a_ms[r] = t1 - t0; as_ms[r] = t2 - t1; } else s_ms[r] = t2 - t1;
            munmap(p, dn); close(d); unlink(DST);
        }
        int d = open(DST, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        char *buf = malloc(1u << 20);
        memset(buf, 0x3C + r, 1u << 20);
        for (size_t off = 0; off < dn; off += 1u << 20) if (write(d, buf, 1u << 20) != (1 << 20)) return 1;
        free(buf);
        double t3 = now_ms();
        fsync(d);
        f_ms[r] = now_ms() - t3;
        close(d); unlink(DST);
    }
    qsort(a_ms, (size_t)runs, sizeof(double), cmp_d);
    qsort(as_ms, (size_t)runs, sizeof(double), cmp_d);
    qsort(s_ms, (size_t)runs, sizeof(double), cmp_d);
    qsort(f_ms, (size_t)runs, sizeof(double), cmp_d);
    printf("\n64 MiB dirty -> storage (median of %d, fresh file each):\n", runs);
    printf("  mapping: msync(MS_ASYNC)                 : %8.2f ms\n", a_ms[runs / 2]);
    printf("           then msync(MS_SYNC)             : %8.2f ms\n", as_ms[runs / 2]);
    printf("  mapping: msync(MS_SYNC) only             : %8.2f ms\n", s_ms[runs / 2]);
    printf("  write() 64 x 1 MiB, then fsync()         : %8.2f ms\n", f_ms[runs / 2]);

    unlink(SRC);
    rmdir(DIR_);
    return 0;
}
