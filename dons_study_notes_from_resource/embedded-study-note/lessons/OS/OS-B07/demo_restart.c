/* OS-B07 demo_restart.c — 어떤 blocking 호출이 SA_RESTART 로 "재시작" 되고 어떤 것이 EINTR 인가 (macOS 실측)
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_restart.c -o demo_restart && ./demo_restart
 *
 * 각 호출을 막히게 해 두고 30 ms 뒤 SIGALRM 을 보낸다. 막힘은 200~300 ms 뒤 저절로 풀리게
 * 만들어 두었으므로(자식이 쓰기/종료, 타임아웃) 어떤 결과든 멈추지 않는다.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t hits;
static void h(int s) { (void)s; hits = hits + 1; }

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void arm(int ms) {
    struct itimerval it;
    memset(&it, 0, sizeof it);
    it.it_value.tv_usec = ms * 1000;
    setitimer(ITIMER_REAL, &it, NULL);
}
static void set_alrm(int flags) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = h;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;
    sigaction(SIGALRM, &sa, NULL);
}
static void report(const char *what, int restart, long r, int e, long t0) {
    printf("%-28s %-11s -> ret=%-7ld %-28s %4ld ms  handler=%d\n", what, restart ? "SA_RESTART" : "0",
           r, r < 0 ? strerror(e) : "", now_ms() - t0, (int)hits);
}

/* 자식: delay_ms 뒤에 p[1] 에 1바이트 쓰거나(mode 0), 그냥 종료(mode 1), 또는 p[0] 에서 64 KB 읽기(mode 2) */
static pid_t helper(int p[2], int mode, int delay_ms) {
    pid_t c = fork();
    if (c == 0) {
        struct timespec ts = { 0, (long)delay_ms * 1000000L };
        nanosleep(&ts, NULL);
        if (mode == 0) (void)!write(p[1], "x", 1);
        if (mode == 2) { char b[65536]; (void)!read(p[0], b, sizeof b); }
        _exit(7);
    }
    return c;
}

int main(void) {
    { struct sigaction o; signal(SIGUSR1, h); sigaction(SIGUSR1, NULL, &o);
      printf("signal(3) on this OS installs with sa_flags=0x%x (SA_RESTART=0x%x) -> %s\n\n",
             (unsigned)o.sa_flags, (unsigned)SA_RESTART, (o.sa_flags & SA_RESTART) ? "BSD semantics" : "no restart"); }

    for (int restart = 0; restart <= 1; restart++) {
        int fl = restart ? SA_RESTART : 0;
        set_alrm(fl);
        int p[2]; long t0, r; int e; pid_t c;

        /* read(pipe) */
        pipe(p); c = helper(p, 0, 200); hits = 0; t0 = now_ms(); arm(30);
        char b; r = read(p[0], &b, 1); e = errno; report("read(pipe, 1)", restart, r, e, t0);
        waitpid(c, NULL, 0); close(p[0]); close(p[1]);

        /* write(full pipe) */
        pipe(p); fcntl(p[1], F_SETFL, O_NONBLOCK); while (write(p[1], "x", 1) == 1) { }
        fcntl(p[1], F_SETFL, 0); c = helper(p, 2, 200); hits = 0; t0 = now_ms(); arm(30);
        r = write(p[1], "y", 1); e = errno; report("write(full pipe, 1)", restart, r, e, t0);
        waitpid(c, NULL, 0); close(p[0]); close(p[1]);

        /* waitpid */
        pipe(p); c = helper(p, 1, 200); hits = 0; t0 = now_ms(); arm(30);
        r = waitpid(c, NULL, 0); e = errno; report("waitpid(child)", restart, r > 0 ? 0 : r, e, t0);
        if (r < 0) waitpid(c, NULL, 0);
        close(p[0]); close(p[1]);

        /* poll / select / nanosleep: 300 ms 타임아웃 */
        pipe(p); struct pollfd pf = { p[0], POLLIN, 0 }; hits = 0; t0 = now_ms(); arm(30);
        r = poll(&pf, 1, 300); e = errno; report("poll(300 ms)", restart, r, e, t0);
        fd_set rs; FD_ZERO(&rs); FD_SET(p[0], &rs); struct timeval tv = { 0, 300000 };
        hits = 0; t0 = now_ms(); arm(30);
        r = select(p[0] + 1, &rs, NULL, NULL, &tv); e = errno; report("select(300 ms)", restart, r, e, t0);
        close(p[0]); close(p[1]);
        struct timespec ts = { 0, 300000000L }, rem = { 0, 0 };
        hits = 0; t0 = now_ms(); arm(30);
        r = nanosleep(&ts, &rem); e = errno; report("nanosleep(300 ms)", restart, r, e, t0);
        if (r < 0) printf("%-28s %-11s    remaining = %ld ms (caller must sleep again)\n", "", "", rem.tv_nsec / 1000000);
        printf("\n");
    }
    return 0;
}
