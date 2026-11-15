/* OS-M03 demo_bench.c — 큰 부모에서 프로세스 만들기: fork vs vfork vs posix_spawn
 *
 * usage: ./bench [max_MB=1024] [runs=15]
 *   부모가 0 MB / max/4 MB / max MB 익명 메모리를 실제로 써서(memset) 상주시킨 뒤,
 *   방법마다 "자식 만들기 → 자식 종료 → waitpid" 를 runs 번 재고 중앙값과 최솟값을 낸다.
 *     fork+_exit      자식은 아무것도 안 하고 _exit(0)          = 주소 공간 복제 비용만
 *     fork+exec       자식이 execv("/usr/bin/true")            = 셸·데몬의 전통적 방법
 *     vfork+exec      vfork 후 execv (macOS 12+ 에서는 fork 와 같다고 man vfork 가 말한다)
 *     posix_spawn     posix_spawn("/usr/bin/true")
 *     fork+dirty all  자식이 영역 전체에 쓰고 _exit             = COW 가 미뤄 둔 복사를 다 치르면?
 *   "create" = 부모에서 fork()/vfork()/posix_spawn() 가 돌아올 때까지, "total" = waitpid 까지.
 *   시간은 체크하지 않는다(데모). 공유·부하 걸린 맥에서 재므로 비율만 본다. 첫 줄에 load average.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench && ./bench 1024 15
 *   (Linux: gcc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench — 같은 소스)
 */
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e6 + (double)t.tv_nsec / 1e3;
}
static long minflt(void) { struct rusage r; getrusage(RUSAGE_SELF, &r); return r.ru_minflt; }
static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

enum { M_FORK_EXIT, M_FORK_EXEC, M_VFORK_EXEC, M_SPAWN, M_FORK_DIRTY, NM };
static const char *mname[NM] = { "fork+_exit", "fork+exec", "vfork+exec", "posix_spawn", "fork+dirty all" };

static char *g_buf;
static size_t g_len;
static long g_pg;

/* fork 뒤에는 부모 쪽 페이지도 쓰기 보호(COW)된다. 매 측정이 "막 일하던 큰 프로세스" 와 같은 조건이 되도록
   페이지마다 1바이트를 다시 써서 부모의 사본을 전용·쓰기 가능으로 되돌린다 (자식은 이미 끝났으므로 복사 없이 풀린다). */
static void redirty(void) {
    for (size_t off = 0; off < g_len; off += (size_t)g_pg) g_buf[off] = 1;
}

/* 한 번 재기: create_us(부모로 돌아온 시각) 와 total_us(reap 시각). 실패하면 -1. */
static int one(int m, double *create_us, double *total_us) {
    static char *const argv[] = { "true", NULL };
    const char *path = "/usr/bin/true";
    pid_t pid = -1;
    double t0 = now_us();
    switch (m) {
    case M_FORK_EXIT:
        pid = fork();
        if (pid == 0) _exit(0);
        break;
    case M_FORK_EXEC:
        pid = fork();
        if (pid == 0) { execv(path, argv); _exit(127); }
        break;
    case M_VFORK_EXEC:
#if defined(__APPLE__)            /* macOS SDK: vfork 에 __deprecated_msg("Use posix_spawn or fork") — 일부러 써 보므로 이 줄만 끈다 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        pid = vfork();
#pragma clang diagnostic pop
#else
        pid = vfork();
#endif
        if (pid == 0) { execv(path, argv); _exit(127); }
        break;
    case M_SPAWN:
        if (posix_spawn(&pid, path, NULL, NULL, argv, environ) != 0) pid = -1;
        break;
    case M_FORK_DIRTY:
        pid = fork();
        if (pid == 0) {
            for (size_t off = 0; off < g_len; off += (size_t)g_pg) g_buf[off] = 7;   /* 페이지마다 1바이트 = 페이지마다 COW */
            _exit(0);
        }
        break;
    }
    double t1 = now_us();
    if (pid < 0) return -1;
    int st;
    if (waitpid(pid, &st, 0) != pid || !WIFEXITED(st) || WEXITSTATUS(st) != 0) return -1;
    *create_us = t1 - t0;
    *total_us = now_us() - t0;
    return 0;
}

int main(int argc, char **argv) {
    long max_mb = argc > 1 ? atol(argv[1]) : 1024;
    int runs = argc > 2 ? atoi(argv[2]) : 15;
    if (runs < 3) runs = 3;
    if (runs > 101) runs = 101;
    g_pg = sysconf(_SC_PAGESIZE);
    double la[3] = {0, 0, 0};
    getloadavg(la, 3);
    printf("pagesize %ld, runs %d, load avg %.2f %.2f %.2f\n", g_pg, runs, la[0], la[1], la[2]);

    long sizes[3] = { 0, max_mb / 4, max_mb };
    double *cr = malloc(sizeof(double) * (size_t)runs), *tt = malloc(sizeof(double) * (size_t)runs);
    printf("%-8s %-15s %12s %12s %12s %12s\n", "parent", "method", "create med", "create min", "total med", "total min");
    for (int si = 0; si < 3; si++) {
        g_len = (size_t)sizes[si] << 20;
        g_buf = NULL;
        if (g_len) {
            g_buf = mmap(NULL, g_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (g_buf == MAP_FAILED) { perror("mmap"); return 1; }
            memset(g_buf, 1, g_len);                         /* 전부 상주 + 쓰기 가능 상태로 */
        }
        for (int m = 0; m < NM; m++) {
            if (m == M_FORK_DIRTY && g_len == 0) continue;
            int r = (m == M_FORK_DIRTY) ? 3 : runs;            /* 전부 복사는 비싸니 3번만 */
            double c, t;
            if (one(m, &c, &t) != 0) { printf("%s failed\n", mname[m]); return 1; }   /* 워밍업 */
            redirty();
            for (int i = 0; i < r; i++) {
                if (one(m, &cr[i], &tt[i]) != 0) { printf("%s failed\n", mname[m]); return 1; }
                redirty();                                     /* 시간 밖: 부모 페이지를 다시 "쓰기 가능·전용" 으로 */
            }
            qsort(cr, (size_t)r, sizeof(double), cmp_d);
            qsort(tt, (size_t)r, sizeof(double), cmp_d);
            printf("%5ld MB %-15s %9.0f us %9.0f us %9.0f us %9.0f us\n", sizes[si], mname[m],
                   cr[r / 2], cr[0], tt[r / 2], tt[0]);
        }
        if (g_len) munmap(g_buf, g_len);
    }

    /* 2부: fork 는 부모도 느리게 만든다. 자식이 끝난 뒤에도 부모의 첫 쓰기는 페이지마다 COW 폴트다. */
    g_len = (size_t)max_mb << 20;
    g_buf = mmap(NULL, g_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (g_buf == MAP_FAILED) { perror("mmap"); return 1; }
    memset(g_buf, 1, g_len);
    printf("\nparent write pass over %ld MB (1 byte/page), median of 5:\n", max_mb);
    for (int after_fork = 0; after_fork < 2; after_fork++) {
        double ms[5];
        long fl = 0;
        for (int i = 0; i < 5; i++) {
            if (after_fork) { double c, t; if (one(M_FORK_EXIT, &c, &t) != 0) return 1; }   /* 자식은 이미 reap 됨 */
            long f0 = minflt();
            double t0 = now_us();
            redirty();
            ms[i] = (now_us() - t0) / 1e3;
            fl = minflt() - f0;
        }
        qsort(ms, 5, sizeof(double), cmp_d);
        printf("  %-28s %8.1f ms  minflt %ld\n", after_fork ? "after fork (child exited)" : "no fork (baseline)", ms[2], fl);
    }
    munmap(g_buf, g_len);
    getloadavg(la, 3);
    printf("load avg after %.2f %.2f %.2f\n", la[0], la[1], la[2]);
    free(cr);
    free(tt);
    return 0;
}
