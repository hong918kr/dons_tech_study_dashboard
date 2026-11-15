/* ENV: linux */
/* BUILD: -pthread */
/* OS-M18 demo_bench — 깨움 지연(wake latency): "일어났다" 를 보낸 순간 → epoll 루프가 깨어난 순간
 *   스레드 → 메인 : eventfd / pipe / signalfd(tgkill SIGUSR1) / self-pipe(SIGUSR1 핸들러가 pipe 에 1 바이트)
 *   자식 종료 → 부모 : pidfd / signalfd(SIGCHLD) / self-pipe(SIGCHLD 핸들러)
 * 핑퐁: 보내는 쪽은 ack 를 받은 뒤 t0 를 찍고 보낸다 (합쳐짐 없이 한 번에 하나). 지연 = t1(epoll_wait 반환) − t0.
 * 코드량 비교: 아래 "LOC:" 표시 구간을 awk 로 센다 (md 참고). 숫자는 VM vCPU 기준 — 비율만 볼 것.
 * build: gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o bench && ./bench [N]
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/pidfd.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static long now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000L + t.tv_nsec;
}
static int cmp_long(const void *a, const void *b) {
    long x = *(const long *)a, y = *(const long *)b;
    return (x > y) - (x < y);
}
static void report(const char *name, long *lat, int n) {
    qsort(lat, (size_t)n, sizeof *lat, cmp_long);
    printf("  %-34s min %6.1f  p50 %6.1f  p99 %7.1f  (µs, n=%d)\n", name, lat[0] / 1e3, lat[n / 2] / 1e3,
           lat[(int)(n * 0.99)] / 1e3, n);
}

/* ===================== self-pipe (OS-B07 방식, 리눅스판) ===================== */
/* LOC:selfpipe:begin */
static int g_sp[2] = { -1, -1 };
static void sp_handler(int sig) {
    int saved = errno;
    unsigned char b = (unsigned char)sig;
    ssize_t w = write(g_sp[1], &b, 1);          /* 꽉 차면 EAGAIN — 버려도 된다 */
    (void)w;
    errno = saved;
}
static int sp_setup(const int *sigs, int n) {
    if (pipe2(g_sp, O_NONBLOCK | O_CLOEXEC) != 0) return -1;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = sp_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    for (int i = 0; i < n; i++) sigaddset(&sa.sa_mask, sigs[i]);
    for (int i = 0; i < n; i++)
        if (sigaction(sigs[i], &sa, NULL) != 0) return -1;
    return g_sp[0];                              /* epoll 에 넣을 fd */
}
static int sp_drain(int *last_sig) {             /* 바이트 = "무슨 시그널이 왔다" (개수 아님) */
    unsigned char buf[64];
    ssize_t r;
    int n = 0;
    while ((r = read(g_sp[0], buf, sizeof buf)) > 0) { n += (int)r; *last_sig = buf[r - 1]; }
    return n;
}
/* LOC:selfpipe:end */

/* ===================== signalfd ===================== */
/* LOC:signalfd:begin */
static int sfd_setup(const int *sigs, int n) {
    sigset_t set;
    sigemptyset(&set);
    for (int i = 0; i < n; i++) sigaddset(&set, sigs[i]);
    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) return -1;   /* 스레드 만들기 전에 */
    return signalfd(-1, &set, SFD_NONBLOCK | SFD_CLOEXEC);
}
static int sfd_drain(int sfd, int *last_sig) {   /* 레코드 = signo + 보낸 pid + 상태 */
    struct signalfd_siginfo si[8];
    ssize_t r;
    int n = 0;
    while ((r = read(sfd, si, sizeof si)) > 0)
        for (size_t i = 0; i < (size_t)r / sizeof si[0]; i++, n++) *last_sig = (int)si[i].ssi_signo;
    return n;
}
/* LOC:signalfd:end */

/* ===================== 스레드 → 메인 ===================== */
enum { V_EVENTFD, V_PIPE, V_SIGNALFD, V_SELFPIPE };
static atomic_long g_t0;
static int g_ack, g_wfd, g_variant, g_n;
static pid_t g_main_tid;

static void *sender(void *arg) {
    (void)arg;
    for (int i = 0; i < g_n; i++) {
        uint64_t k;
        while (read(g_ack, &k, sizeof k) < 0 && errno == EINTR) { }   /* 메인이 준비됐다 */
        atomic_store(&g_t0, now_ns());
        uint64_t one = 1;
        ssize_t w = 0;
        switch (g_variant) {
        case V_EVENTFD: w = write(g_wfd, &one, sizeof one); break;
        case V_PIPE:    w = write(g_wfd, "x", 1); break;
        default:        tgkill(getpid(), g_main_tid, SIGUSR1); break;  /* 메인 스레드 지정 */
        }
        (void)w;
    }
    return NULL;
}

static void run_thread_variant(int variant, const char *name, int n) {
    int rfd = -1, sig = 0;
    sigset_t saved;
    pthread_sigmask(SIG_SETMASK, NULL, &saved);
    const int us1[] = { SIGUSR1 };
    if (variant == V_EVENTFD) { rfd = g_wfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); }
    else if (variant == V_PIPE) { int p[2]; if (pipe2(p, O_NONBLOCK | O_CLOEXEC) != 0) return; rfd = p[0]; g_wfd = p[1]; }
    else if (variant == V_SIGNALFD) rfd = sfd_setup(us1, 1);
    else rfd = sp_setup(us1, 1);
    g_ack = eventfd(0, EFD_CLOEXEC);
    int ep = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event e = { .events = EPOLLIN };
    epoll_ctl(ep, EPOLL_CTL_ADD, rfd, &e);
    g_variant = variant;
    g_n = n;
    g_main_tid = gettid();
    long *lat = malloc(sizeof(long) * (size_t)n);
    pthread_t th;
    pthread_create(&th, NULL, sender, NULL);
    int eintr = 0;
    for (int i = 0; i < n; i++) {
        uint64_t one = 1;
        ssize_t w = write(g_ack, &one, sizeof one);
        (void)w;
        int r;
        while ((r = epoll_wait(ep, &e, 1, 2000)) < 0 && errno == EINTR) eintr++;   /* self-pipe: 핸들러가 깨운다 */
        long t1 = now_ns();
        if (r != 1) { fprintf(stderr, "timeout at %d\n", i); exit(1); }
        lat[i] = t1 - atomic_load(&g_t0);
        uint64_t k;
        char buf[64];
        switch (variant) {
        case V_EVENTFD:  w = read(rfd, &k, sizeof k); break;
        case V_PIPE:     w = read(rfd, buf, sizeof buf); break;
        case V_SIGNALFD: sfd_drain(rfd, &sig); break;
        default:         sp_drain(&sig); break;
        }
    }
    pthread_join(th, NULL);
    report(name, lat, n);
    if (variant == V_SELFPIPE) printf("  %-34s epoll_wait EINTR %d / %d\n", "", eintr, n);
    free(lat);
    close(ep); close(g_ack); close(rfd);
    if (variant == V_PIPE) close(g_wfd);       /* eventfd 는 rfd == g_wfd */
    if (variant == V_SELFPIPE) { close(g_sp[1]); signal(SIGUSR1, SIG_DFL); }
    pthread_sigmask(SIG_SETMASK, &saved, NULL);
}

/* ===================== 자식 종료 → 부모 ===================== */
enum { C_PIDFD, C_SIGNALFD, C_SELFPIPE };
/* LOC:pidfd:begin */
static int pidfd_watch(int ep, pid_t pid) {
    int pfd = pidfd_open(pid, 0);
    struct epoll_event e = { .events = EPOLLIN, .data.fd = pfd };
    epoll_ctl(ep, EPOLL_CTL_ADD, pfd, &e);
    return pfd;
}
static int pidfd_reap_one(int ep, int pfd) {     /* 그 자식만, 정확히 한 번 */
    siginfo_t si;
    memset(&si, 0, sizeof si);
    waitid(P_PIDFD, (id_t)pfd, &si, WEXITED);
    epoll_ctl(ep, EPOLL_CTL_DEL, pfd, NULL);
    close(pfd);
    return si.si_status;
}
/* LOC:pidfd:end */
/* LOC:sigchld:begin */
static int reap_any(void) {                      /* SIGCHLD 는 합쳐진다 → 없을 때까지 */
    int n = 0, st;
    while (waitpid(-1, &st, WNOHANG) > 0) n++;
    return n;
}
/* LOC:sigchld:end */

static void run_child_variant(int variant, const char *name, int n) {
    sigset_t saved;
    pthread_sigmask(SIG_SETMASK, NULL, &saved);
    const int chld[] = { SIGCHLD };
    long *t0 = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    int ep = epoll_create1(EPOLL_CLOEXEC), rfd = -1, sig = 0;
    if (variant == C_SIGNALFD) rfd = sfd_setup(chld, 1);
    if (variant == C_SELFPIPE) rfd = sp_setup(chld, 1);
    if (rfd >= 0) { struct epoll_event e = { .events = EPOLLIN, .data.fd = rfd }; epoll_ctl(ep, EPOLL_CTL_ADD, rfd, &e); }
    long *lat = malloc(sizeof(long) * (size_t)n);
    int eintr = 0;
    for (int i = 0; i < n; i++) {
        int gate[2];
        if (pipe2(gate, O_CLOEXEC) != 0) return;
        pid_t pid = fork();
        if (pid == 0) {
            char c;
            close(gate[1]);
            while (read(gate[0], &c, 1) < 0 && errno == EINTR) { }
            *t0 = now_ns();                      /* 이 순간부터 부모가 알아챌 때까지 */
            _exit(0);
        }
        close(gate[0]);
        int pfd = variant == C_PIDFD ? pidfd_watch(ep, pid) : -1;
        close(gate[1]);                          /* 자식 출발 */
        struct epoll_event e;
        int r;
        while ((r = epoll_wait(ep, &e, 1, 2000)) < 0 && errno == EINTR) eintr++;
        long t1 = now_ns();
        if (r != 1) { fprintf(stderr, "timeout at %d\n", i); exit(1); }
        lat[i] = t1 - *t0;
        if (variant == C_PIDFD) pidfd_reap_one(ep, pfd);
        else {
            if (variant == C_SIGNALFD) sfd_drain(rfd, &sig); else sp_drain(&sig);
            if (reap_any() == 0) waitpid(pid, NULL, 0);   /* 드물게: 시그널이 먼저, 종료 정리가 나중 */
        }
    }
    report(name, lat, n);
    if (variant == C_SELFPIPE) printf("  %-34s epoll_wait EINTR %d / %d\n", "", eintr, n);
    free(lat);
    munmap(t0, 4096);
    close(ep);
    if (rfd >= 0) close(rfd);
    if (variant == C_SELFPIPE) { close(g_sp[1]); signal(SIGCHLD, SIG_DFL); }
    pthread_sigmask(SIG_SETMASK, &saved, NULL);
}

int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 20000;
    int m = n / 10;
    char la[64] = "?";
    FILE *f = fopen("/proc/loadavg", "r");
    if (f) { if (!fgets(la, sizeof la, f)) la[0] = 0; fclose(f); la[strcspn(la, "\n")] = 0; }
    printf("OS-M18 wake latency — nproc %ld, loadavg %s\n", sysconf(_SC_NPROCESSORS_ONLN), la);
    printf("[스레드 → 메인 epoll 루프]\n");
    run_thread_variant(V_EVENTFD, "eventfd write", n);
    run_thread_variant(V_PIPE, "pipe write (reference)", n);
    run_thread_variant(V_SIGNALFD, "signalfd (tgkill SIGUSR1)", n);
    run_thread_variant(V_SELFPIPE, "self-pipe (SIGUSR1 handler)", n);
    printf("[자식 _exit → 부모 epoll 루프 (exit 처리 포함)]\n");
    run_child_variant(C_PIDFD, "pidfd", m);
    run_child_variant(C_SIGNALFD, "signalfd (SIGCHLD)", m);
    run_child_variant(C_SELFPIPE, "self-pipe (SIGCHLD handler)", m);
    return 0;
}
