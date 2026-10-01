// C06_ctxsw_cost.c — OSTEP 6장 측정 숙제 (2): lmbench lat_ctx 방식 컨텍스트 스위치 비용.
// 두 프로세스가 파이프 두 개로 1바이트를 핑퐁한다. 한 번 왕복 = 스위치 2번 (+ 파이프 read/write 2쌍).
// 기준선: 한 프로세스가 자기 파이프에 write→read (스위치 없이 파이프 비용만).
// 주의: macOS에는 sched_setaffinity가 없다 → 두 프로세스가 같은 코어에 있다고 보장 못 함.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define N 200000
#define RUNS 5

static uint64_t ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

static double baseline(void) {
    int p[2];
    char c = 'x';
    pipe(p);
    double best = 1e30;
    for (int r = 0; r < RUNS; r++) {
        uint64_t t0 = ns();
        for (int i = 0; i < N; i++) {
            if (write(p[1], &c, 1) != 1 || read(p[0], &c, 1) != 1) exit(1);
        }
        double per = (double)(ns() - t0) / N;
        if (per < best) best = per;
    }
    close(p[0]); close(p[1]);
    return best;   // write 1번 + read 1번 (블록 없음)
}

static double pingpong(void) {
    int ab[2], ba[2];   // a→b, b→a
    char c = 'x';
    pipe(ab); pipe(ba);
    pid_t pid = fork();
    if (pid == 0) {     // B: 받으면 돌려준다
        close(ab[1]); close(ba[0]);
        while (read(ab[0], &c, 1) == 1)
            if (write(ba[1], &c, 1) != 1) break;
        _exit(0);
    }
    close(ab[0]); close(ba[1]);
    double best = 1e30;
    for (int r = 0; r < RUNS; r++) {
        uint64_t t0 = ns();
        for (int i = 0; i < N; i++) {
            if (write(ab[1], &c, 1) != 1 || read(ba[0], &c, 1) != 1) exit(1);
        }
        double per = (double)(ns() - t0) / N;
        if (per < best) best = per;
    }
    close(ab[1]);       // B의 read가 EOF를 보고 끝남
    waitpid(pid, NULL, 0);
    close(ba[0]);
    return best;        // 왕복 1회
}

int main(void) {
    double base = baseline();
    double rt = pingpong();
    printf("pipe write+read in one process (no switch): %7.1f ns\n", base);
    printf("ping-pong round trip between 2 processes  : %7.1f ns\n", rt);
    printf("=> per switch estimate (rt - 2*base) / 2  : %7.1f ns\n", (rt - 2 * base) / 2);
    printf("=> naive per switch (rt / 2)              : %7.1f ns\n", rt / 2);
    return 0;
}
