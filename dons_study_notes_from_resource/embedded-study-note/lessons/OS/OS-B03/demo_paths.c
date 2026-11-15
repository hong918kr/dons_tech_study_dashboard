/* OS-B03 demo_paths.c — 같은 "시스템 콜 1 회" 라도 커널 안 경로에 따라 값이 다르다
 *
 * read/write 를 대상(파일 종류)·바이트 수만 바꿔 20만 번씩, 두 바퀴 잰다 (두 번째 바퀴가 덜 흔들린다).
 * 루프 오버헤드는 빼지 않았다 (≈ 0.2 ns 로 여기선 무시할 수준).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_paths.c -o paths && ./paths
 */
#define _DARWIN_C_SOURCE
#include <fcntl.h>
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#define N 200000

static mach_timebase_info_data_t g_tb;
static volatile int64_t g_sink;

#define M(label, stmt) do {                                                           \
        int64_t acc = 0;                                                              \
        uint64_t t0 = mach_absolute_time();                                           \
        for (int i = 0; i < N; i++) { stmt; }                                         \
        uint64_t t1 = mach_absolute_time();                                           \
        g_sink += acc;                                                                \
        printf("%-26s %8.1f ns\n", label, (double)(t1 - t0) * g_tb.numer / g_tb.denom / N); \
    } while (0)

int main(int argc, char **argv) {
    (void)argc;
    mach_timebase_info(&g_tb);
    int p[2];
    if (pipe(p) != 0) return 1;
    int dn = open("/dev/null", O_RDWR), dz = open("/dev/zero", O_RDONLY);
    int rf = open(argv[0], O_RDONLY);                 /* 정규 파일: 이 실행 파일 자신 */
    if (dn < 0 || dz < 0 || rf < 0) { perror("open"); return 1; }
    char buf[64] = {0};
    for (int round = 1; round <= 2; round++) {
        printf("-- round %d\n", round);
        M("getppid", acc += getppid());
        M("read0 pipe", acc += read(p[0], buf, 0));
        M("read0 /dev/null", acc += read(dn, buf, 0));
        M("read0 regular file", acc += read(rf, buf, 0));
        M("read1 /dev/zero", acc += read(dz, buf, 1));
        M("pread1 regular file", acc += pread(rf, buf, 1, 0));
        M("write0 /dev/null", acc += write(dn, buf, 0));
        M("write1 /dev/null", acc += write(dn, buf, 1));
        M("write1+read1 pipe (pair)", { acc += write(p[1], buf, 1); acc += read(p[0], buf, 1); });
    }
    close(p[0]); close(p[1]); close(dn); close(dz); close(rf);
    return 0;
}
