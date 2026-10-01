// C02_cpu.c — OSTEP Figure 2.1 (cpu.c) 변형: 1초 돌다가 문자열 출력, N번 반복 후 종료.
// 사용법: ./C02_cpu <문자열> <반복횟수>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

static double now(void) {
    struct timeval t;
    gettimeofday(&t, NULL);
    return (double)t.tv_sec + (double)t.tv_usec / 1e6;
}

// Spin: 시스템 콜 gettimeofday()를 계속 부르며 'howlong' 초 동안 CPU를 태운다.
static void spin(double howlong) {
    double t = now();
    while (now() - t < howlong)
        ; // busy-wait
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <string> <loops>\n", argv[0]);
        return 1;
    }
    int loops = atoi(argv[2]);
    for (int i = 0; i < loops; i++) {
        spin(0.5);
        printf("%s (pid %d) #%d\n", argv[1], (int)getpid(), i);
        fflush(stdout);
    }
    // 벽시계 시간 vs 실제로 CPU를 받은 시간 비교 (가상 CPU 착시의 증거)
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    double cpu = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 +
                 ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
    printf("%s done: wall %.2f s, cpu %.2f s (%.0f%%)\n", argv[1], loops * 0.5, cpu,
           100.0 * cpu / (loops * 0.5));
    return 0;
}
