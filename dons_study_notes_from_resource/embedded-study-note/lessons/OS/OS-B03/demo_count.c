/* OS-B03 demo_count.c — "정말 커널에 들어갔나?" 를 센다 (macOS 판 strace -c)
 *
 * task_info(TASK_EVENTS_INFO) 의 누적 카운터: syscalls_unix (BSD 시스템 콜), syscalls_mach (Mach 트랩),
 * csw (컨텍스트 스위치). 대상마다 10만 번 부르기 전후의 차이를 찍는다.
 * 카운터를 읽는 task_info 자신이 Mach 메시지 1 회라서 mach 에 +1 이 섞인다.
 * SIP 때문에 dtruss 를 못 써도, sudo 없이 동작한다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_count.c -o count && ./count
 */
#define _DARWIN_C_SOURCE
#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define N 100000

static task_events_info_data_t ev(void) {
    task_events_info_data_t e = {0};
    mach_msg_type_number_t c = TASK_EVENTS_INFO_COUNT;
    task_info(mach_task_self(), TASK_EVENTS_INFO, (task_info_t)&e, &c);
    return e;
}

#define COUNT(name, stmt) do {                                                        \
        task_events_info_data_t a = ev();                                             \
        for (int i = 0; i < N; i++) { stmt; }                                         \
        task_events_info_data_t b = ev();                                             \
        printf("%-28s unix %+d mach %+d csw %+d\n", name, b.syscalls_unix - a.syscalls_unix, \
               b.syscalls_mach - a.syscalls_mach, b.csw - a.csw);                     \
    } while (0)

int main(void) {
    struct timespec ts;
    char c = 'x';
    volatile uint64_t sink = 0;
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    COUNT("nothing", sink++);
    COUNT("getpid", sink += (uint64_t)getpid());
    COUNT("getppid", sink += (uint64_t)getppid());
    COUNT("read0", sink += (uint64_t)read(fd, &c, 0));
    COUNT("write1 devnull", sink += (uint64_t)write(fd, &c, 1));
    COUNT("clock_gettime MONOTONIC", clock_gettime(CLOCK_MONOTONIC, &ts));
    COUNT("clock_gettime REALTIME", clock_gettime(CLOCK_REALTIME, &ts));
    COUNT("clock_gettime UPTIME_RAW", clock_gettime(CLOCK_UPTIME_RAW, &ts));
    COUNT("clock_gettime THREAD_CPU", clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts));
    COUNT("clock_gettime PROCESS_CPU", clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts));
    COUNT("clock_gettime_nsec_np", sink += clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
    COUNT("mach_absolute_time", sink += mach_absolute_time());
    COUNT("mach_thread_self+dealloc", {
        mach_port_t p = mach_thread_self();
        mach_port_deallocate(mach_task_self(), p);
    });
    close(fd);
    return 0;
}
