/* ENV: linux */
/* BUILD: -pthread */
/* OS-M15 추가 연습 — 한 번에 검증 (Lima VM elinux)
 *
 *   연습 1  fork 한 자식의 mq_setattr(O_NONBLOCK) 는 부모 디스크립터에도 보인다 (열린 큐 설명 공유)
 *           — 같은 이름을 mq_open 으로 따로 연 디스크립터에는 안 보인다
 *   연습 2  /dev/mqueue/<name> 읽기: QSIZE(= 메시지 바이트 합) · NOTIFY · SIGNO · NOTIFY_PID
 *   연습 3  RLIMIT_MSGQUEUE (사용자별 바이트 한도) 를 낮추면 mq_open 이 EMFILE
 *   연습 4  mq_receive 로 막혀 있는 수신자가 있으면 mq_notify 알림은 오지 않는다
 *   연습 5  mq_timedreceive 의 잘못된 timespec: 리눅스는 메시지가 있어도 EINVAL, 지난 시각 + 메시지 = 그냥 받음
 *
 * build: gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int g_pass, g_total;
#define CHECK(e)                                                                    \
    do {                                                                            \
        g_total++;                                                                  \
        if (e) { g_pass++; printf("PASS  %s\n", #e); }                              \
        else printf("FAIL  %s  (line %d)\n", #e, __LINE__);                         \
    } while (0)

#define MAXQ 16
static char g_names[MAXQ][64];
static int g_nq;
static pid_t g_main_pid;
static void cleanup_names(void) {
    if (getpid() != g_main_pid) return;
    for (int i = 0; i < g_nq; i++) mq_unlink(g_names[i]);
}
static mqd_t q_new(const char *tag, long maxmsg, long msgsize, char *name_out) {
    char name[64];
    snprintf(name, sizeof name, "/OS-M15-%d-%s", (int)g_main_pid, tag);
    struct mq_attr a = { .mq_maxmsg = maxmsg, .mq_msgsize = msgsize };
    mq_unlink(name);
    mqd_t q = mq_open(name, O_CREAT | O_EXCL | O_RDWR, 0600, &a);
    if (q != (mqd_t)-1 && g_nq < MAXQ) snprintf(g_names[g_nq++], sizeof g_names[0], "%s", name);
    if (name_out) snprintf(name_out, 64, "%s", name);
    return q;
}
static long q_flags(mqd_t q) { struct mq_attr a; return mq_getattr(q, &a) == 0 ? a.mq_flags : -1; }
static long q_cur(mqd_t q) { struct mq_attr a; return mq_getattr(q, &a) == 0 ? a.mq_curmsgs : -1; }
static void sleep_ms(long ms) {
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&t, &t) != 0 && errno == EINTR) { }
}
static int wait_sig(int signo, long ms) {
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, signo);
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    int r;
    while ((r = sigtimedwait(&s, NULL, &t)) < 0 && errno == EINTR) { }
    return r;
}
static int reap(pid_t pid) {
    int st;
    for (int i = 0; i < 3000; i++) {
        if (waitpid(pid, &st, WNOHANG) == pid) return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
        sleep_ms(1);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &st, 0);
    return -1;
}

/* ---- 연습 2 의 답: /dev/mqueue/<name> 한 줄 파싱 ---- */
typedef struct { long qsize; int notify, signo, notify_pid; } mqfs_t;
static int mqfs_read(const char *name, mqfs_t *o) { /* name 은 "/..." — /dev/mqueue 에는 앞 '/' 없이 */
    char path[128], line[256];
    snprintf(path, sizeof path, "/dev/mqueue/%s", name + 1);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int ok = fgets(line, sizeof line, f) != NULL &&
             sscanf(line, "QSIZE:%ld NOTIFY:%d SIGNO:%d NOTIFY_PID:%d", &o->qsize, &o->notify, &o->signo,
                    &o->notify_pid) == 4;
    fclose(f);
    return ok ? 0 : -1;
}

/* ---- 연습 4 의 답: 자식이 지금 mq_timedreceive 안에서 자고 있나 (/proc/<pid>/syscall 첫 숫자) ---- */
static int in_syscall(pid_t pid, long nr) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/syscall", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    long got = -1;
    int ok = fscanf(f, "%ld", &got) == 1; /* 실행 중이면 "running" → 숫자 아님 */
    fclose(f);
    return ok && got == nr;
}

static void ex1_shared_description(void) {
    printf("== 연습 1: fork 는 열린 큐 설명(open description)을 공유한다 ==\n");
    char name[64];
    mqd_t q = q_new("ex1", 4, 16, name);
    mqd_t other = mq_open(name, O_RDWR); /* 같은 큐, 다른 open description */
    fflush(stdout);
    pid_t c = fork();
    if (c == 0) {
        struct mq_attr nb = { .mq_flags = O_NONBLOCK };
        _exit(mq_setattr(q, &nb, NULL) == 0 ? 0 : 1);
    }
    int st = reap(c);
    long fq = q_flags(q), fo = q_flags(other);
    printf("  자식이 O_NONBLOCK 켠 뒤: 부모 q flags %#lx, 따로 연 other flags %#lx\n", fq, fo);
    CHECK(st == 0 && (fq & O_NONBLOCK) && fo == 0);
    char b[16];
    errno = 0;
    CHECK(mq_receive(q, b, sizeof b, NULL) == -1 && errno == EAGAIN); /* 부모도 이제 안 막힌다 */
    mq_close(other);
    mq_close(q);
}

static void ex2_mqueue_fs(void) {
    printf("== 연습 2: /dev/mqueue/<name> ==\n");
    char name[64];
    mqd_t q = q_new("ex2", 4, 64, name);
    mq_send(q, "0123456789", 10, 0);
    mq_send(q, "abcdefghijklmnopqrst", 20, 5);
    mqfs_t s0 = { -1, -1, -1, -1 }, s1 = { -1, -1, -1, -1 };
    int r0 = mqfs_read(name, &s0);
    struct sigevent sev;
    memset(&sev, 0, sizeof sev);
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGUSR1;
    mq_notify(q, &sev);
    int r1 = mqfs_read(name, &s1);
    printf("  등록 전 QSIZE %ld NOTIFY %d SIGNO %d PID %d / 등록 후 SIGNO %d PID %d (나 %d)\n", s0.qsize,
           s0.notify, s0.signo, s0.notify_pid, s1.signo, s1.notify_pid, (int)getpid());
    CHECK(r0 == 0 && s0.qsize == 30 && s0.notify_pid == 0);
    CHECK(r1 == 0 && s1.notify == SIGEV_SIGNAL && s1.signo == SIGUSR1 && s1.notify_pid == (int)getpid());
    mq_notify(q, NULL);
    mq_close(q);
}

static void ex3_rlimit(void) {
    printf("== 연습 3: RLIMIT_MSGQUEUE → EMFILE ==\n");
    struct rlimit rl;
    getrlimit(RLIMIT_MSGQUEUE, &rl);
    int fds[2];
    if (pipe(fds) != 0) return;
    fflush(stdout);
    pid_t c = fork();
    if (c == 0) { /* 자식에서만 한도를 낮춘다 (rlimit 는 상속, 바이트 계산은 사용자별) */
        close(fds[0]);
        struct rlimit low = { 16384, rl.rlim_max };
        setrlimit(RLIMIT_MSGQUEUE, &low);
        int n = 0, err = 0;
        char nm[64];
        for (; n < 64; n++) {
            snprintf(nm, sizeof nm, "/OS-M15-%d-ex3-%d", (int)getpid(), n);
            struct mq_attr a = { .mq_maxmsg = 4, .mq_msgsize = 1024 }; /* 큐당 4 KiB + 커널 부대 비용 */
            if (mq_open(nm, O_CREAT | O_EXCL | O_RDWR, 0600, &a) == (mqd_t)-1) { err = errno; break; }
        }
        for (int i = 0; i < n; i++) { /* 만든 것 정리 */
            snprintf(nm, sizeof nm, "/OS-M15-%d-ex3-%d", (int)getpid(), i);
            mq_unlink(nm);
        }
        int out[2] = { n, err };
        _exit(write(fds[1], out, sizeof out) == (ssize_t)sizeof out ? 0 : 1);
    }
    close(fds[1]);
    int out[2] = { -1, 0 };
    ssize_t rd = read(fds[0], out, sizeof out);
    close(fds[0]);
    int st = reap(c);
    printf("  RLIMIT_MSGQUEUE 기본 %ld B → 자식 16384 B: 4×1024 B 큐 %d 개 만든 뒤 %s\n", (long)rl.rlim_cur, out[0],
           strerror(out[1]));
    CHECK(st == 0 && rd == (ssize_t)sizeof out && out[1] == EMFILE);
    CHECK(out[0] >= 0 && out[0] <= 3); /* 4 KiB × 4 = 16 KiB 인데 부대 비용 때문에 4 개는 못 만든다 */
}

static void ex4_receiver_wins(void) {
    printf("== 연습 4: 막힌 수신자가 알림보다 먼저 ==\n");
    mqd_t q = q_new("ex4", 4, 16, NULL);
    struct sigevent sev;
    memset(&sev, 0, sizeof sev);
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGUSR1;
    CHECK(mq_notify(q, &sev) == 0);
    fflush(stdout);
    pid_t c = fork();
    if (c == 0) {
        alarm(3);
        char b[16];
        _exit(mq_receive(q, b, sizeof b, NULL) == 3 ? 0 : 1);
    }
    int blocked = 0;
    for (int i = 0; i < 2000 && !blocked; i++) { /* 자식이 커널 안에서 잠들 때까지 */
        blocked = in_syscall(c, SYS_mq_timedreceive);
        if (!blocked) sleep_ms(1);
    }
    mq_send(q, "abc", 3, 0);
    int st = reap(c);
    int sig = wait_sig(SIGUSR1, 200);
    printf("  자식이 mq_timedreceive(%d) 에서 잠듦 %d, 자식 exit %d, 부모 알림 %d, 큐 %ld 개\n",
           (int)SYS_mq_timedreceive, blocked, st, sig, q_cur(q));
    CHECK(blocked && st == 0 && q_cur(q) == 0);
    CHECK(sig == -1); /* 메시지는 수신자에게 바로, 알림은 없음 — 등록은 그대로 남는다 */
    mq_send(q, "z", 1, 0);
    CHECK(wait_sig(SIGUSR1, 1000) == SIGUSR1); /* 이번엔 막힌 수신자가 없으니 알림 */
    mq_close(q);
}

static void ex5_bad_timeout(void) {
    printf("== 연습 5: abs_timeout 이 잘못됐을 때 ==\n");
    mqd_t q = q_new("ex5", 4, 16, NULL);
    char b[16];
    struct timespec bad = { 0, -1 }, past = { 1, 0 }; /* tv_nsec 범위 밖 / 1970-01-01 00:00:01 */
    mq_send(q, "m", 1, 0);
    errno = 0;
    ssize_t r1 = mq_timedreceive(q, b, sizeof b, NULL, &bad);
    int e1 = errno;
    errno = 0;
    ssize_t r2 = mq_timedreceive(q, b, sizeof b, NULL, &past);
    errno = 0;
    ssize_t r3 = mq_timedreceive(q, b, sizeof b, NULL, &past);
    int e3 = errno;
    printf("  메시지 있음+잘못된 ts → %zd (%s), 메시지 있음+지난 시각 → %zd, 빈 큐+지난 시각 → %zd (%s)\n", r1,
           strerror(e1), r2, r3, strerror(e3));
    CHECK(r1 == -1 && e1 == EINVAL); /* POSIX 는 "검사 안 해도 된다" — 리눅스는 먼저 검사한다 */
    CHECK(r2 == 1);                  /* 지난 시각이어도 꺼낼 게 있으면 받는다 */
    CHECK(r3 == -1 && e3 == ETIMEDOUT);
    mq_close(q);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    g_main_pid = getpid();
    atexit(cleanup_names);
    signal(SIGPIPE, SIG_IGN);
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGUSR1);
    sigprocmask(SIG_BLOCK, &s, NULL);

    ex1_shared_description();
    ex2_mqueue_fs();
    ex3_rlimit();
    ex4_receiver_wins();
    ex5_bad_timeout();

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
