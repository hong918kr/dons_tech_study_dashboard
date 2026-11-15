/* ENV: linux */
/* BUILD: -pthread */
/* OS-M18 알림용 fd IPC: eventfd·signalfd·pidfd를 epoll 하나로
 *
 * "데이터" 가 아니라 "일어났다" 만 전달하는 fd 세 가지를 epoll 루프 하나에 꽂는다.
 *   Part A  eventfd: 커널 안의 u64 카운터. write = 더하기, read = 값을 읽고 0 으로 (EFD_SEMAPHORE 면 1 씩).
 *           작업 스레드 완료 알림: 완료 큐에 push → eventfd 에 +1, 메인은 epoll 로 깨어나 카운터를 읽고 큐를 비운다.
 *   Part B  signalfd: 시그널을 "읽을 수 있는 fd" 로. 모든 스레드에서 먼저 막아야 한다 (안 막은 스레드가 하나라도
 *           있으면 그 스레드가 기본 동작으로 받는다 → SIGTERM 이면 프로세스가 죽는다).
 *           pidfd: 자식 하나당 fd 하나. 종료하면 EPOLLIN, waitid(P_PIDFD) 로 그 자식만 회수.
 *           자식 5 명이 같이 죽으면 SIGCHLD 는 합쳐지지만(표준 시그널 = pending 비트 하나) pidfd 는 정확히 5 개.
 *   Part C  데몬 뼈대: epoll 하나 = eventfd(작업 완료) + signalfd(SIGTERM/SIGINT/SIGCHLD) + pidfd(자식들).
 *           SIGTERM → 새 작업 거부, 남은 작업 drain, 워커 join, 자식에게 pidfd_send_signal(SIGTERM),
 *           유예 시간이 지나면 SIGKILL, 전부 회수, fd·시그널 마스크 원상복구.
 *
 * CHECK 는 매 실행 항상 성립하는 것만 본다 (카운터 값, 기록 수, 종료 코드, fd 수). 지연·코드량 비교는 demo_bench.c.
 * build (Lima VM elinux): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
 */
#define _GNU_SOURCE /* eventfd/signalfd/pidfd_open/P_PIDFD/sigqueue 를 -std=c11 에서 보이게 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/pidfd.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int g_pass, g_total;
#define CHECK(c) do { g_total++; if (c) { g_pass++; printf("PASS  %s\n", #c); } \
                      else printf("FAIL  %s  (line %d)\n", #c, __LINE__); } while (0)

/* ======================= 공용 도우미 (주어진 코드) ======================= */
static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}
static int count_open_fds(void) { /* fd 누수 검사: 열린 fd 개수 */
    int n = 0;
    for (int fd = 0; fd < 4096; fd++) if (fcntl(fd, F_GETFD) != -1) n++;
    return n;
}
static int blocked_now(int sig) { /* 호출 스레드의 마스크에서 sig 가 막혀 있나 */
    sigset_t cur;
    pthread_sigmask(SIG_BLOCK, NULL, &cur);
    return sigismember(&cur, sig) == 1;
}
/* 막혀 있는 동안 쌓인 시그널을 먹고 마스크 복구 — 풀자마자 SIGTERM 기본 동작으로 하네스가 죽지 않게 */
static void mask_restore(const sigset_t *old) {
    const int eat[] = { SIGTERM, SIGINT, SIGUSR1, SIGCHLD };
    sigset_t w;
    sigemptyset(&w);
    for (size_t i = 0; i < sizeof eat / sizeof eat[0]; i++) if (blocked_now(eat[i])) sigaddset(&w, eat[i]);
    if (blocked_now(SIGRTMIN)) sigaddset(&w, SIGRTMIN);
    struct timespec zero = { 0, 0 };
    while (sigtimedwait(&w, NULL, &zero) > 0) { }
    pthread_sigmask(SIG_SETMASK, old, NULL);
}
/* 자식 하나를 timeout 안에 회수 (넘으면 SIGKILL). 반환: wait status, 실패 -1 */
static int wait_child(pid_t pid, int timeout_ms) {
    long end = now_ms() + timeout_ms;
    int st;
    for (;;) {
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) return st;
        if (r < 0 && errno != EINTR) return -1;
        if (now_ms() > end) { kill(pid, SIGKILL); waitpid(pid, &st, 0); return -1; }
        struct timespec d = { 0, 2000000L };
        nanosleep(&d, NULL);
    }
}

/* epoll 의 data.u64 = (종류 << 32) | 번호 — 디스패치 키 */
enum { TAG_EVENT = 1, TAG_SIGNAL = 2, TAG_CHILD = 3 };
static uint64_t tag_make(uint32_t tag, uint32_t idx) { return ((uint64_t)tag << 32) | idx; }
static uint32_t tag_of(uint64_t u) { return (uint32_t)(u >> 32); }
static uint32_t idx_of(uint64_t u) { return (uint32_t)u; }
static int ep_add(int epfd, int fd, uint64_t u) {
    struct epoll_event e = { .events = EPOLLIN, .data.u64 = u };
    return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &e) == 0 ? 0 : -errno;
}

static long job_compute(int j) { /* 작업 = 의미 없는 계산 (결과 검증용) */
    long x = j;
    for (int k = 0; k < 200; k++) x = (x * 1103515245L + 12345L) & 0x7fffffffL;
    return x;
}

/* ======================= Part A — eventfd ======================= */
static int ev_open(unsigned init, int flags) { /* 주어진 코드 */
    int fd = eventfd(init, flags | EFD_CLOEXEC);
    return fd >= 0 ? fd : -errno;
}

/* TODO A-1: 카운터에 n 을 더한다 (정확히 8 바이트 write). 0 또는 -errno.
 *   n == UINT64_MAX → -EINVAL, 넘치면 EFD_NONBLOCK 에서 -EAGAIN (blocking 이면 잠듦), EINTR 은 다시. */
static int ev_post(int efd, uint64_t n) {
    for (;;) {
        ssize_t w = write(efd, &n, sizeof n);
        if (w == (ssize_t)sizeof n) return 0;
        if (w < 0 && errno == EINTR) continue;
        return w < 0 ? -errno : -EIO;
    }
}

/* TODO A-2: 카운터를 읽는다 (정확히 8 바이트 read). 보통 = 값 전체를 받고 0 으로, EFD_SEMAPHORE = 1 을 받고 -1.
 *   0 이면 EFD_NONBLOCK 에서 -EAGAIN. 0 또는 -errno. */
static int ev_take(int efd, uint64_t *out) {
    for (;;) {
        uint64_t v = 0;
        ssize_t r = read(efd, &v, sizeof v);
        if (r == (ssize_t)sizeof v) { *out = v; return 0; }
        if (r < 0 && errno == EINTR) continue;
        return r < 0 ? -errno : -EIO;
    }
}

/* 작업 스레드 풀: 결과는 완료 큐(뮤텍스)로, "끝났다" 는 eventfd 로 */
#define POOL_MAX 1024
typedef struct {
    int efd;                                   /* EFD_NONBLOCK 완료 카운터 */
    pthread_mutex_t mu;                        /* 완료 큐 보호 */
    int done_job[POOL_MAX];
    long done_val[POOL_MAX];
    int ndone;
    atomic_int next;                           /* 다음 작업 번호 */
    int njobs;
    atomic_long posts;                         /* 성공한 ev_post 수 (관찰용) */
} pool_t;

typedef struct {
    long wakeups, counter_sum, collected;      /* epoll 깨어남 수, 읽은 카운터 합, 모은 결과 수 */
    int seen[POOL_MAX];
    long val[POOL_MAX];
} collect_t;

/* TODO A-3: 결과를 완료 큐에 넣고 eventfd 에 +1. 순서: push → post (거꾸로면 메인이 빈 큐를 본다). */
static void pool_complete(pool_t *p, int job, long val) {
    pthread_mutex_lock(&p->mu);
    p->done_job[p->ndone] = job;
    p->done_val[p->ndone] = val;
    p->ndone++;
    pthread_mutex_unlock(&p->mu);
    if (ev_post(p->efd, 1) == 0) atomic_fetch_add(&p->posts, 1);
}

static void *pool_worker(void *arg) { /* 주어진 코드 */
    pool_t *p = arg;
    for (;;) {
        int j = atomic_fetch_add(&p->next, 1);
        if (j >= p->njobs) break;
        pool_complete(p, j, job_compute(j));
    }
    return NULL;
}

/* TODO A-4: epoll 로 기다렸다 깨어나면 카운터를 읽어(합산) 완료 큐를 통째로 비운다.
 *   "카운터 합 == njobs" 가 될 때까지 (카운터가 진실, 큐는 짐). epoll_wait 은 2 s 상한 → 넘으면 -ETIMEDOUT. */
static int pool_collect(pool_t *p, int epfd, collect_t *c) {
    struct epoll_event ev;
    while (c->counter_sum < p->njobs) {
        int n = epoll_wait(epfd, &ev, 1, 2000);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -ETIMEDOUT;
        uint64_t k;
        if (ev_take(p->efd, &k) != 0) continue;   /* 이미 읽힌 뒤면 EAGAIN → 다시 기다림 */
        c->wakeups++;
        c->counter_sum += (long)k;
        pthread_mutex_lock(&p->mu);
        for (int i = 0; i < p->ndone; i++) {
            int j = p->done_job[i];
            c->seen[j]++;
            c->val[j] = p->done_val[i];
            c->collected++;
        }
        p->ndone = 0;
        pthread_mutex_unlock(&p->mu);
    }
    return 0;
}

static int pool_run(int nworkers, int njobs, collect_t *c, long *posts) { /* 주어진 코드 */
    pool_t *p = calloc(1, sizeof *p);
    if (!p) return -ENOMEM;
    p->njobs = njobs;
    pthread_mutex_init(&p->mu, NULL);
    atomic_init(&p->next, 0);
    atomic_init(&p->posts, 0);
    p->efd = ev_open(0, EFD_NONBLOCK);
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    int rc = (p->efd < 0 || epfd < 0) ? -EMFILE : ep_add(epfd, p->efd, tag_make(TAG_EVENT, 0));
    pthread_t th[8];
    int nt = 0;
    for (; rc == 0 && nt < nworkers && nt < 8; nt++)
        if (pthread_create(&th[nt], NULL, pool_worker, p) != 0) break;
    if (rc == 0) rc = pool_collect(p, epfd, c);
    for (int i = 0; i < nt; i++) pthread_join(th[i], NULL);
    *posts = atomic_load(&p->posts);
    if (p->efd >= 0) close(p->efd);
    if (epfd >= 0) close(epfd);
    pthread_mutex_destroy(&p->mu);
    free(p);
    return rc;
}

/* ======================= Part B — signalfd + pidfd ======================= */
typedef struct { uint32_t signo; int32_t code; uint32_t pid; int32_t status; int32_t ival; } sigrec_t;

/* TODO B-1: sigs[0..n) 를 호출 스레드에서 막고(pthread_sigmask, 이전 마스크는 *oldmask) 그 집합의 signalfd 를 연다.
 *   SFD_NONBLOCK | SFD_CLOEXEC. 반환 fd 또는 -errno (실패하면 마스크 되돌림).
 *   ★ 스레드를 만들기 "전에" 불러야 한다 — 마스크는 pthread_create 때 상속된다. */
static int sigfd_open(const int *sigs, int n, sigset_t *oldmask) {
    sigset_t set;
    sigemptyset(&set);
    for (int i = 0; i < n; i++) sigaddset(&set, sigs[i]);
    int e = pthread_sigmask(SIG_BLOCK, &set, oldmask);   /* 실패 시 errno 가 아니라 반환값 */
    if (e != 0) return -e;
    int fd = signalfd(-1, &set, SFD_NONBLOCK | SFD_CLOEXEC);
    if (fd < 0) { int er = errno; pthread_sigmask(SIG_SETMASK, oldmask, NULL); return -er; }
    return fd;
}

/* TODO B-2: signalfd 를 EAGAIN 까지 읽어 레코드 수를 돌려준다 (앞 max 개는 out 에 복사). 오류는 -errno.
 *   read 단위 = struct signalfd_siginfo (128 바이트) 의 배수. */
static int sigfd_drain(int sfd, sigrec_t *out, int max) {
    int n = 0;
    for (;;) {
        struct signalfd_siginfo buf[8];
        ssize_t r = read(sfd, buf, sizeof buf);
        if (r < 0) {
            if (errno == EINTR) continue;
            return errno == EAGAIN ? n : -errno;
        }
        for (size_t i = 0; i < (size_t)r / sizeof buf[0]; i++, n++)
            if (n < max)
                out[n] = (sigrec_t){ buf[i].ssi_signo, buf[i].ssi_code, buf[i].ssi_pid,
                                     buf[i].ssi_status, buf[i].ssi_int };
    }
}

/* TODO B-3: 자식 pid 의 pidfd 를 열어 epoll 에 tag 로 등록. 반환 pidfd 또는 -errno. */
static int watch_child(int epfd, pid_t pid, uint64_t tag) {
    int pfd = pidfd_open(pid, 0);          /* glibc 2.36+ 래퍼 (없으면 syscall(SYS_pidfd_open, pid, 0)) */
    if (pfd < 0) return -errno;
    int e = ep_add(epfd, pfd, tag);
    if (e != 0) { close(pfd); return e; }
    return pfd;
}

/* TODO B-4: pidfd 하나를 waitid(P_PIDFD) 로 회수. 정상 종료면 *code = 종료 코드, *sig = 0;
 *   시그널로 죽었으면 *code = -1, *sig = 시그널 번호. 0 또는 -errno. */
static int pidfd_reap(int pidfd, int *code, int *sig) {
    siginfo_t si;
    memset(&si, 0, sizeof si);
    while (waitid(P_PIDFD, (id_t)pidfd, &si, WEXITED) != 0)
        if (errno != EINTR) return -errno;
    *code = -1;
    *sig = 0;
    if (si.si_code == CLD_EXITED) *code = si.si_status;
    else *sig = si.si_status;              /* CLD_KILLED / CLD_DUMPED: si_status = 시그널 번호 */
    return 0;
}

/* 자식 프로세스 안에서: 막고→스레드 (block_first=1, 정답) 또는 스레드→막고 (0, 버그) 순서로 준비한 뒤
 * 프로세스 전체에 SIGTERM. signalfd 로 받으면 exit 0. 주어진 코드. */
static void *idle_thread(void *arg) {
    int *hold = arg;                       /* hold[0] = 영원히 잠들 pipe, hold[3] = "시작했다" 알림 pipe 쓰기 끝 */
    char ch = 'r';
    ssize_t r = write(hold[3], &ch, 1);    /* glibc 는 새 스레드를 "전부 막힌" 마스크로 시작해 start 직전에 복구한다 → */
    r = read(hold[0], &ch, 1);             /* 여기까지 왔으면 물려받은 마스크로 돌고 있다. 이후 영원히 잠 */
    (void)r;
    return NULL;
}
static int thread_case(int block_first) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        alarm(3);
        int hold[4];
        char ch;
        if (pipe(hold) != 0 || pipe(hold + 2) != 0) _exit(4);
        sigset_t old;
        int sfd = -1;
        pthread_t t;
        if (block_first) sfd = sigfd_open((const int[]){ SIGTERM }, 1, &old);
        if (pthread_create(&t, NULL, idle_thread, hold) != 0) _exit(5);
        if (read(hold[2], &ch, 1) != 1) _exit(5); /* 스레드가 정말 돌기 시작할 때까지 */
        if (!block_first) sfd = sigfd_open((const int[]){ SIGTERM }, 1, &old); /* 버그: 스레드는 이미 안 막힌 채 */
        if (sfd < 0) _exit(6);
        kill(getpid(), SIGTERM);           /* 프로세스로 보낸 시그널: 막지 않은 스레드 아무나 받는다 */
        struct pollfd p = { sfd, POLLIN, 0 };
        if (poll(&p, 1, 1000) == 1) {
            sigrec_t r[4];
            if (sigfd_drain(sfd, r, 4) >= 1 && r[0].signo == SIGTERM) _exit(0);
        }
        _exit(3);
    }
    return wait_child(pid, 4000);
}

/* ======================= Part C — 데몬 뼈대 ======================= */
#define D_MAXW 8
#define D_QCAP 1024
#define D_MAXCH 8
typedef struct { pid_t pid; int pidfd, alive, code, sig, killed_hard; } child_t;
typedef struct {
    int epfd, efd, sfd;
    sigset_t oldmask;                          /* 원래 마스크: 자식에게 돌려주고, 닫을 때 복구 */
    pthread_t th[D_MAXW];
    int nw, workers_joined;
    pthread_mutex_t mu;                        /* 아래 두 큐와 stopping 보호 */
    pthread_cond_t cv;
    int q[D_QCAP], qhead, qtail;               /* 작업 큐 (입력) */
    int dq_job[D_QCAP];                        /* 완료 큐 (출력) */
    long dq_val[D_QCAP];
    int ndq, stopping;
    long submitted, completed, result_sum, rejected;   /* 메인 스레드만 쓴다 (submitted 는 mu 안에서) */
    child_t ch[D_MAXCH];
    int nch, alive;
    long wakeups_event, sigchld_records, sigterm_seen, kill_deadline_ms;
    int sigkill_sent;
} daemon_t;

static void *d_worker(void *arg) { /* 주어진 코드 */
    daemon_t *d = arg;
    for (;;) {
        pthread_mutex_lock(&d->mu);
        while (d->qhead == d->qtail && !d->stopping) pthread_cond_wait(&d->cv, &d->mu);
        if (d->qhead == d->qtail) { pthread_mutex_unlock(&d->mu); break; }   /* stopping + 큐 빔 → 퇴근 */
        int job = d->q[d->qhead++ % D_QCAP];
        pthread_mutex_unlock(&d->mu);
        long v = job_compute(job);
        pthread_mutex_lock(&d->mu);
        d->dq_job[d->ndq] = job;
        d->dq_val[d->ndq] = v;
        d->ndq++;
        pthread_mutex_unlock(&d->mu);
        ev_post(d->efd, 1);                    /* push → post */
    }
    return NULL;
}

/* TODO C-1: 순서가 전부다 — ① SIGTERM·SIGINT·SIGCHLD 막기 + signalfd (sigfd_open, 이전 마스크는 d->oldmask)
 *   ② eventfd(EFD_NONBLOCK) ③ epoll 에 efd(TAG_EVENT) · sfd(TAG_SIGNAL) 등록 ④ 그 다음에야 워커 nworkers 개.
 *   0 또는 -errno. (memset·뮤텍스 초기화는 주어짐) */
static int daemon_init(daemon_t *d, int nworkers) {
    memset(d, 0, sizeof *d);
    d->epfd = d->efd = d->sfd = -1;
    pthread_mutex_init(&d->mu, NULL);
    pthread_cond_init(&d->cv, NULL);
    pthread_sigmask(SIG_SETMASK, NULL, &d->oldmask);
    d->sfd = sigfd_open((const int[]){ SIGTERM, SIGINT, SIGCHLD }, 3, &d->oldmask);   /* ① 스레드보다 먼저 */
    if (d->sfd < 0) return d->sfd;
    d->efd = ev_open(0, EFD_NONBLOCK);                                             /* ② */
    d->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (d->efd < 0 || d->epfd < 0) return -EMFILE;
    int e = ep_add(d->epfd, d->efd, tag_make(TAG_EVENT, 0));                      /* ③ */
    if (e == 0) e = ep_add(d->epfd, d->sfd, tag_make(TAG_SIGNAL, 0));
    if (e != 0) return e;
    for (int i = 0; i < nworkers && i < D_MAXW; i++) {                             /* ④ 마스크를 물려받는다 */
        if (pthread_create(&d->th[i], NULL, d_worker, d) != 0) return -EAGAIN;
        d->nw++;
    }
    return 0;
}

/* 자식 생성 (주어진 코드). mode 0 = gate 가 EOF 가 되면 exit(code), 1 = SIGTERM 올 때까지 대기,
 * 2 = SIGTERM 무시 (→ 유예 후 SIGKILL 이 필요). gate 는 mode 0 에서만 (자식은 쓰기 끝을 닫아야 EOF 를 본다). */
static int daemon_spawn(daemon_t *d, int mode, int code, const int *gate) {
    if (d->nch >= D_MAXCH) return -ENOSPC;
    pid_t pid = fork();
    if (pid < 0) return -errno;
    if (pid == 0) {                                    /* 자식: async-signal-safe 함수만 */
        if (mode == 2) signal(SIGTERM, SIG_IGN);       /* 아직 막혀 있을 때 무시 설정 → 경쟁 없음 */
        sigprocmask(SIG_SETMASK, &d->oldmask, NULL);   /* 막힌 마스크는 fork 로 상속된다 → 원래대로 */
        alarm(5);                                      /* 안전망: 무슨 일이 있어도 5 s 안에 죽는다 */
        if (mode == 0) {
            char c;
            close(gate[1]);
            while (read(gate[0], &c, 1) < 0 && errno == EINTR) { }
            _exit(code);
        }
        for (;;) pause();
    }
    int pfd = watch_child(d->epfd, pid, tag_make(TAG_CHILD, (uint32_t)d->nch));
    if (pfd < 0) { kill(pid, SIGKILL); waitpid(pid, NULL, 0); return pfd; }
    d->ch[d->nch++] = (child_t){ .pid = pid, .pidfd = pfd, .alive = 1, .code = -1 };
    d->alive++;
    return 0;
}

static int daemon_submit(daemon_t *d, int job) { /* 주어진 코드: 종료 중이거나 가득 차면 거부 */
    pthread_mutex_lock(&d->mu);
    int ok = !d->stopping && d->submitted - d->completed < D_QCAP;
    if (ok) { d->q[d->qtail++ % D_QCAP] = job; d->submitted++; pthread_cond_signal(&d->cv); }
    else d->rejected++;
    pthread_mutex_unlock(&d->mu);
    return ok ? 0 : -EAGAIN;
}

static void d_on_event(daemon_t *d) { /* 주어진 코드: Part A 의 pool_collect 한 바퀴와 같다 */
    uint64_t k;
    if (ev_take(d->efd, &k) != 0) return;
    d->wakeups_event++;
    pthread_mutex_lock(&d->mu);
    for (int i = 0; i < d->ndq; i++) { d->completed++; d->result_sum += d->dq_val[i]; }
    d->ndq = 0;
    pthread_mutex_unlock(&d->mu);
}

/* TODO C-3a: 종료 시작 — stopping=1 + 워커 전부 깨우기, 살아 있는 자식에게 pidfd_send_signal(SIGTERM),
 *   SIGKILL 마감 시각 = 지금 + grace_ms. */
static void d_begin_shutdown(daemon_t *d, int grace_ms) {
    pthread_mutex_lock(&d->mu);
    d->stopping = 1;
    pthread_cond_broadcast(&d->cv);
    pthread_mutex_unlock(&d->mu);
    for (int i = 0; i < d->nch; i++)
        if (d->ch[i].alive) pidfd_send_signal(d->ch[i].pidfd, SIGTERM, NULL, 0);   /* pid 재사용 경쟁 없음 */
    d->kill_deadline_ms = now_ms() + grace_ms;
}

/* TODO C-3b: signalfd 를 비우고 처리. SIGCHLD → 세기만 (회수는 pidfd 가). SIGTERM/SIGINT → sigterm_seen++,
 *   처음이면 d_begin_shutdown. */
static void d_on_signal(daemon_t *d, int grace_ms) {
    sigrec_t r[16];
    int n = sigfd_drain(d->sfd, r, 16);
    for (int i = 0; i < n && i < 16; i++) {
        if (r[i].signo == SIGCHLD) { d->sigchld_records++; continue; }
        if (r[i].signo == SIGTERM || r[i].signo == SIGINT) {
            d->sigterm_seen++;
            if (!d->stopping) d_begin_shutdown(d, grace_ms);
        }
    }
}

/* TODO C-4: 자식 i 의 pidfd 가 readable → pidfd_reap 으로 code/sig 기록, epoll 에서 빼고 close, alive 감소. */
static void d_on_child(daemon_t *d, uint32_t i) {
    if (i >= (uint32_t)d->nch || !d->ch[i].alive) return;
    child_t *c = &d->ch[i];
    if (pidfd_reap(c->pidfd, &c->code, &c->sig) != 0) return;
    epoll_ctl(d->epfd, EPOLL_CTL_DEL, c->pidfd, NULL);
    close(c->pidfd);
    c->pidfd = -1;
    c->alive = 0;
    d->alive--;
}

/* TODO C-2: 이벤트 루프 하나.
 *   매 바퀴: stopping 이면 (a) completed == submitted 일 때 워커 join (한 번) (b) 워커 join 됐고 alive == 0 이면 return 0
 *            (c) 자식이 남았고 마감이 지났으면 남은 자식에 SIGKILL (한 번, killed_hard = 1).
 *   epoll_wait 타임아웃 = 테스트 상한(max_ms)까지 남은 시간, 단 SIGKILL 마감이 더 가까우면 그때까지.
 *   상한을 넘으면 -ETIMEDOUT. EINTR 은 다시. 이벤트는 tag 로 d_on_event / d_on_signal / d_on_child 에 분배. */
static int daemon_run(daemon_t *d, int grace_ms, int max_ms) {
    long t_end = now_ms() + max_ms;
    struct epoll_event ev[16];
    for (;;) {
        if (d->stopping) {
            if (!d->workers_joined && d->completed == d->submitted) {
                for (int i = 0; i < d->nw; i++) pthread_join(d->th[i], NULL);
                d->workers_joined = 1;
            }
            if (d->workers_joined && d->alive == 0) return 0;
            if (d->alive > 0 && !d->sigkill_sent && now_ms() >= d->kill_deadline_ms) {
                for (int i = 0; i < d->nch; i++)
                    if (d->ch[i].alive) { pidfd_send_signal(d->ch[i].pidfd, SIGKILL, NULL, 0); d->ch[i].killed_hard = 1; }
                d->sigkill_sent = 1;
            }
        }
        long now = now_ms();
        if (now >= t_end) return -ETIMEDOUT;
        long to = t_end - now;
        if (d->stopping && d->alive > 0 && !d->sigkill_sent && d->kill_deadline_ms - now < to)
            to = d->kill_deadline_ms - now;
        if (to < 0) to = 0;
        int n = epoll_wait(d->epfd, ev, 16, (int)to);
        if (n < 0) { if (errno == EINTR) continue; return -errno; }
        for (int i = 0; i < n; i++) {
            uint64_t u = ev[i].data.u64;
            switch (tag_of(u)) {
            case TAG_EVENT:  d_on_event(d); break;
            case TAG_SIGNAL: d_on_signal(d, grace_ms); break;
            case TAG_CHILD:  d_on_child(d, idx_of(u)); break;
            default: break;
            }
        }
    }
}

static void daemon_close(daemon_t *d) { /* 주어진 코드: 무슨 상태로 끝났든 전부 정리 */
    pthread_mutex_lock(&d->mu);
    d->stopping = 1;
    pthread_cond_broadcast(&d->cv);
    pthread_mutex_unlock(&d->mu);
    if (!d->workers_joined) for (int i = 0; i < d->nw; i++) pthread_join(d->th[i], NULL);
    d->workers_joined = 1;
    for (int i = 0; i < d->nch; i++) {
        if (!d->ch[i].alive) continue;
        kill(d->ch[i].pid, SIGKILL);
        waitpid(d->ch[i].pid, NULL, 0);
        if (d->ch[i].pidfd >= 0) close(d->ch[i].pidfd);
        d->ch[i].alive = 0;
    }
    if (d->efd >= 0) close(d->efd);
    if (d->sfd >= 0) close(d->sfd);
    if (d->epfd >= 0) close(d->epfd);
    mask_restore(&d->oldmask);
    pthread_mutex_destroy(&d->mu);
    pthread_cond_destroy(&d->cv);
}

/* ======================= 테스트 ======================= */
static void test_part_a(void) {
    printf("== Part A: eventfd 카운터 의미 ==\n");
    uint64_t v = 0, a = 0, b = 0, c = 0;
    int fd = ev_open(0, EFD_NONBLOCK);
    CHECK(fd >= 0);
    CHECK(ev_post(fd, 3) == 0 && ev_post(fd, 4) == 0);
    CHECK(ev_take(fd, &v) == 0 && v == 7);                 /* 두 번 쓰고 한 번 읽으면 합, 그리고 0 으로 */
    CHECK(ev_take(fd, &v) == -EAGAIN);                     /* 0 + EFD_NONBLOCK → EAGAIN */
    CHECK(ev_post(fd, UINT64_MAX) == -EINVAL);             /* 0xffff...ffff 는 쓸 수 없는 값 */
    CHECK(ev_post(fd, UINT64_MAX - 1) == 0);               /* 최대 0xffff...fffe 까지 */
    CHECK(ev_post(fd, 1) == -EAGAIN);                      /* 넘침 → NONBLOCK 이면 EAGAIN (blocking 이면 잠듦) */
    CHECK(ev_take(fd, &v) == 0 && v == UINT64_MAX - 1);
    uint32_t small = 0;
    CHECK(read(fd, &small, sizeof small) == -1 && errno == EINVAL);   /* 8 바이트 미만 버퍼는 EINVAL */

    int sfd = ev_open(3, EFD_NONBLOCK | EFD_SEMAPHORE);
    CHECK(ev_take(sfd, &a) == 0 && ev_take(sfd, &b) == 0 && ev_take(sfd, &c) == 0 && a == 1 && b == 1 && c == 1);
    CHECK(ev_take(sfd, &a) == -EAGAIN);                    /* 세마포어: 3 → 1,1,1 → 비었음 */
    CHECK(ev_post(sfd, 2) == 0 && ev_take(sfd, &a) == 0 && a == 1);   /* 2 를 더해도 한 번에 1 씩 */
    close(sfd);

    printf("== Part A: epoll 준비 상태 (level-triggered) ==\n");
    int ep = epoll_create1(EPOLL_CLOEXEC);
    ep_add(ep, fd, tag_make(TAG_EVENT, 7));
    struct epoll_event e;
    CHECK(epoll_wait(ep, &e, 1, 0) == 0);                  /* 카운터 0 → 안 깨움 */
    ev_post(fd, 1);
    CHECK(epoll_wait(ep, &e, 1, 0) == 1 && tag_of(e.data.u64) == TAG_EVENT && idx_of(e.data.u64) == 7 &&
          (e.events & EPOLLIN) && (e.events & EPOLLOUT) == 0);   /* EPOLLIN 만 등록했다 */
    CHECK(epoll_wait(ep, &e, 1, 0) == 1);                  /* LT: 읽기 전엔 계속 준비 */
    ev_take(fd, &v);
    CHECK(epoll_wait(ep, &e, 1, 0) == 0);                  /* 읽어서 0 → 조용 */
    close(ep);
    close(fd);

    printf("== Part A: 작업 스레드 4 개 → eventfd 완료 알림 → epoll 메인 ==\n");
    collect_t *col = calloc(1, sizeof *col);
    long posts = 0;
    int rc = col ? pool_run(4, 800, col, &posts) : -ENOMEM;
    int all_once = col != NULL;
    for (int j = 0; col && j < 800; j++) if (col->seen[j] != 1 || col->val[j] != job_compute(j)) all_once = 0;
    CHECK(rc == 0);
    CHECK(posts == 800 && col && col->counter_sum == 800);  /* 카운터 합 == post 수: 하나도 안 잃는다 */
    CHECK(col && col->collected == 800 && all_once);        /* 결과는 정확히 한 번씩 */
    CHECK(col && col->wakeups >= 1 && col->wakeups <= 800);
    if (col) printf("  info: post 800 번 → epoll 깨어남 %ld 번 (카운터가 합쳐서 전달)\n", col->wakeups);
    free(col);
}

#define NKIDS 5
static void test_part_b(void) {
    sigset_t saved, old;
    pthread_sigmask(SIG_SETMASK, NULL, &saved);
    sigrec_t r[16];
    memset(r, 0, sizeof r);

    printf("== Part B: signalfd — 표준 시그널은 합쳐지고, 실시간 시그널은 쌓인다 ==\n");
    int sfd = sigfd_open((const int[]){ SIGUSR1 }, 1, &old);
    CHECK(sfd >= 0 && blocked_now(SIGUSR1));
    if (blocked_now(SIGUSR1)) { kill(getpid(), SIGUSR1); kill(getpid(), SIGUSR1); }   /* 안 막혔으면 보내지 않는다 */
    int n = sfd >= 0 ? sigfd_drain(sfd, r, 16) : -1;
    CHECK(n == 1 && r[0].signo == SIGUSR1 && r[0].code == SI_USER && r[0].pid == (uint32_t)getpid());
    CHECK(sfd >= 0 && sigfd_drain(sfd, r, 16) == 0);       /* 비었으면 0 (EAGAIN 은 오류가 아님) */
    if (sfd >= 0) close(sfd);
    mask_restore(&saved);

    int rt = SIGRTMIN;
    sfd = sigfd_open(&rt, 1, &old);
    if (blocked_now(rt))
        for (int v = 1; v <= 3; v++) sigqueue(getpid(), rt, (union sigval){ .sival_int = v });
    n = sfd >= 0 ? sigfd_drain(sfd, r, 16) : -1;
    CHECK(n == 3 && r[0].ival == 1 && r[1].ival == 2 && r[2].ival == 3 && r[0].code == SI_QUEUE);
    if (sfd >= 0) close(sfd);
    mask_restore(&saved);

    printf("== Part B: 마스크는 스레드마다 — 막기 전에 만든 스레드가 SIGTERM 을 받는다 ==\n");
    int st_ok = thread_case(1), st_bug = thread_case(0);
    CHECK(st_ok >= 0 && WIFEXITED(st_ok) && WEXITSTATUS(st_ok) == 0);          /* 막고 → 스레드: signalfd 로 받음 */
    CHECK(st_bug >= 0 && WIFSIGNALED(st_bug) && WTERMSIG(st_bug) == SIGTERM);   /* 스레드 → 막고: 기본 동작으로 죽음 */
    printf("  info: block→thread: %s | thread→block: %s %d\n",
           st_ok >= 0 && WIFEXITED(st_ok) ? "exit" : "signaled/timeout", st_bug >= 0 && WIFSIGNALED(st_bug) ? "killed by" : "exit",
           st_bug >= 0 && WIFSIGNALED(st_bug) ? WTERMSIG(st_bug) : (st_bug >= 0 ? WEXITSTATUS(st_bug) : -1));

    printf("== Part B: 자식 %d 명 동시 종료 — SIGCHLD 레코드 vs pidfd ==\n", NKIDS);
    sfd = sigfd_open((const int[]){ SIGCHLD }, 1, &old);
    int ep = epoll_create1(EPOLL_CLOEXEC);
    int gate[2];
    if (pipe(gate) != 0) { perror("pipe"); return; }
    pid_t pid[NKIDS];
    int pfd[NKIDS];
    for (int i = 0; i < NKIDS; i++) {
        pid[i] = fork();
        if (pid[i] == 0) {
            char ch;
            close(gate[1]);
            alarm(5);
            while (read(gate[0], &ch, 1) < 0 && errno == EINTR) { }
            _exit(10 + i);
        }
        pfd[i] = pid[i] > 0 ? watch_child(ep, pid[i], tag_make(TAG_CHILD, (uint32_t)i)) : -1;
    }
    close(gate[0]);
    close(gate[1]);                                        /* 5 명이 같은 순간 EOF → 같이 종료 */
    unsigned ready = 0;
    long end = now_ms() + (pfd[0] >= 0 ? 3000 : 0);     /* pidfd 가 없으면 기다릴 것도 없다 */
    while (ready != (1u << NKIDS) - 1 && now_ms() < end) { /* pidfd 는 회수 전까지 계속 readable (LT) */
        struct epoll_event ev[NKIDS];
        int k = epoll_wait(ep, ev, NKIDS, 200);
        for (int j = 0; j < k; j++)
            if (tag_of(ev[j].data.u64) == TAG_CHILD) ready |= 1u << idx_of(ev[j].data.u64);
    }
    int nready = __builtin_popcount(ready);
    int nsig = -1;
    if (sfd >= 0) {                                        /* 이제야 읽는다: 5 번 생성, pending 비트는 하나 */
        struct pollfd p = { sfd, POLLIN, 0 };
        poll(&p, 1, 1000);
        nsig = sigfd_drain(sfd, r, 16);
    }
    int reaped = 0;
    for (int i = 0; i < NKIDS; i++) {
        int code = -1, sig = 0;
        if (pfd[i] >= 0) {
            if (pidfd_reap(pfd[i], &code, &sig) == 0 && code == 10 + i && sig == 0) reaped++;
            close(pfd[i]);
        } else if (pid[i] > 0) {
            wait_child(pid[i], 3000);                      /* 시작 코드: pidfd 가 없으면 그냥 치운다 */
        }
    }
    printf("  info: SIGCHLD 레코드 %d 개 (첫 ssi_pid=%u) vs pidfd 준비 %d 개, pidfd 회수 %d 개\n",
           nsig, nsig > 0 ? r[0].pid : 0u, nready, reaped);
    CHECK(nready == NKIDS);                                /* pidfd: 자식마다 하나 → 정확히 5 */
    CHECK(nsig >= 1 && nsig < NKIDS);                      /* signalfd: 합쳐짐 → 5 보다 적다 (보통 1) */
    CHECK(reaped == NKIDS);                                /* 각 pidfd 로 그 자식의 종료 코드 10+i */
    if (sfd >= 0) { sigfd_drain(sfd, r, 16); close(sfd); }
    close(ep);
    mask_restore(&saved);
}

static void test_part_c(void) {
    printf("== Part C: 데몬 — epoll 하나에 eventfd + signalfd + pidfd, SIGTERM 으로 깔끔한 종료 ==\n");
    int fds0 = count_open_fds();
    daemon_t *d = calloc(1, sizeof *d);
    if (!d) return;
    int rc = daemon_init(d, 4);
    CHECK(rc == 0 && d->nw == 4 && blocked_now(SIGTERM) && blocked_now(SIGCHLD));
    int ok = rc == 0, gate[2] = { -1, -1 };
    if (ok) ok = daemon_spawn(d, 1, 0, NULL) == 0 && daemon_spawn(d, 2, 0, NULL) == 0;   /* ch0: TERM 에 죽음, ch1: TERM 무시 */
    if (ok && pipe(gate) == 0)
        ok = daemon_spawn(d, 0, 7, gate) == 0 && daemon_spawn(d, 0, 9, gate) == 0;      /* ch2: exit 7, ch3: exit 9 */
    CHECK(ok && d->nch == 4 && d->alive == 4);
    for (int j = 0; j < 300; j++) daemon_submit(d, j);
    if (gate[0] >= 0) { close(gate[0]); close(gate[1]); }
    for (int i = 2; ok && i < 4; i++) {                    /* exit 7/9 자식이 좀비가 될 때까지 (아직 회수 안 함) */
        struct pollfd p = { d->ch[i].pidfd, POLLIN, 0 };
        poll(&p, 1, 2000);
    }
    if (ok && blocked_now(SIGTERM)) kill(getpid(), SIGTERM);   /* systemctl stop 처럼: 프로세스에 SIGTERM */
    long t0 = now_ms();
    rc = ok ? daemon_run(d, 300, 4000) : -1;
    long ms = now_ms() - t0;
    long expect = 0;
    for (int j = 0; j < 300; j++) expect += job_compute(j);
    CHECK(rc == 0);
    CHECK(d->submitted == 300 && d->completed == 300 && d->result_sum == expect);   /* 큐에 있던 작업은 다 끝냄 */
    CHECK(d->sigterm_seen == 1 && d->stopping && daemon_submit(d, 999) == -EAGAIN);  /* 종료 중 새 작업 거부 */
    CHECK(d->ch[2].code == 7 && d->ch[3].code == 9);                                 /* 먼저 끝난 자식: 종료 코드 그대로 */
    CHECK(d->ch[0].sig == SIGTERM && !d->ch[0].killed_hard);                         /* pidfd_send_signal(SIGTERM) */
    CHECK(d->ch[1].sig == SIGKILL && d->ch[1].killed_hard);                          /* 유예 300 ms 뒤 SIGKILL */
    CHECK(d->alive == 0 && d->workers_joined);
    printf("  info: run %ld ms, eventfd 깨어남 %ld 번 (작업 300), SIGCHLD 레코드 %ld 개 (자식 4)\n",
           ms, d->wakeups_event, d->sigchld_records);
    daemon_close(d);
    free(d);
    CHECK(count_open_fds() == fds0 && !blocked_now(SIGTERM));    /* fd·마스크 원상복구 */
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30); /* 전체 안전망: 무엇이 잘못돼도 30 s 안에 끝난다 */
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
