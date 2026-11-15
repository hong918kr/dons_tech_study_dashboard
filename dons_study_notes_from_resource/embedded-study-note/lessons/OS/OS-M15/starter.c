/* ENV: linux */
/* BUILD: -pthread */
/* OS-M15 POSIX 메시지 큐: mq_open·우선순위·mq_notify
 *
 * POSIX 메시지 큐 = 커널이 "메시지 경계 + 우선순위" 를 지켜 주는 IPC. 리눅스에서 mqd_t 는 그냥 fd 다.
 * (macOS 는 mq_open 이 없다 — SDK 에 <mqueue.h> 자체가 없음 → Lima VM elinux 에서 빌드·실행)
 *
 *   Part A  생산자 2 (fork) → 소비자 1: 체크섬·생산자별 순서 검증, 메시지 경계, mq_unlink 뒤에도 열린 큐는 산다
 *   Part B  우선순위 순서(같은 우선순위는 FIFO) 모델 vs 커널, 가득 참: 블로킹 vs O_NONBLOCK(EAGAIN),
 *           mq_timedreceive 타임아웃(CLOCK_REALTIME 절대 시각!), mq_getattr/setattr, EMSGSIZE,
 *           /proc/sys/fs/mqueue 한도 → EINVAL
 *   Part C  mq_notify: SIGEV_SIGNAL(1회용, 빈→비어있지 않음 때만, EBUSY), poll(mqd), SIGEV_THREAD(재등록 후 drain)
 *
 * CHECK 는 매 실행 항상 성립하는 것만 본다. 지연·처리량 숫자는 demo_bench.c, 한도 탐색은 demo_limits.c.
 * 큐 이름은 "/OS-M15-<pid>-<tag>" — 끝나면 atexit 에서 전부 mq_unlink (자식은 _exit 로 끝나서 안 지운다).
 *
 * build (Lima VM elinux, glibc 2.43 은 mq_* 가 libc 안에 있어 -lrt 불필요):
 *   gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
 */
#define _GNU_SOURCE /* sigtimedwait, SIGEV_THREAD 필드, O_CLOEXEC 등을 -std=c11 에서 보이게 */
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* TSan 빌드 감지: glibc 의 mq_notify(SIGEV_THREAD) 는 내부에서 pthread_create 를 PLT 없이 직접 불러서
 * TSan 이 그 스레드를 모른다 → 콜백에서 SEGV. 그래서 TSan 빌드에서만 그 테스트 하나를 건너뛴다. */
#if defined(__SANITIZE_THREAD__)
#define OSM15_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define OSM15_TSAN 1
#endif
#endif

/* ======================= 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(e)                                                                    \
    do {                                                                            \
        g_total++;                                                                  \
        if (e) { g_pass++; printf("PASS  %s\n", #e); }                              \
        else printf("FAIL  %s  (line %d)\n", #e, __LINE__);                         \
    } while (0)

/* ---- 큐 이름 관리: 만든 이름은 전부 기록해 두고 exit 때 지운다 ---- */
#define MAXQ 32
static char g_names[MAXQ][64];
static int g_nq;
static pid_t g_main_pid;

static void cleanup_names(void) {
    if (getpid() != g_main_pid) return; /* 자식이 exit() 로 끝나도 부모 큐를 지우지 않게 */
    for (int i = 0; i < g_nq; i++) mq_unlink(g_names[i]); /* 이미 지운 이름은 ENOENT — 무시 */
}

/* 새 큐: O_CREAT|O_EXCL 로 "내가 만든 새 큐" 를 보장. extra = O_NONBLOCK 등. 실패면 (mqd_t)-1 + errno */
static mqd_t q_new(const char *tag, long maxmsg, long msgsize, int extra, char *name_out) {
    char name[64];
    snprintf(name, sizeof name, "/OS-M15-%d-%s", (int)g_main_pid, tag);
    struct mq_attr a = { .mq_maxmsg = maxmsg, .mq_msgsize = msgsize };
    mqd_t q = mq_open(name, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | extra, 0600, &a);
    if (q == (mqd_t)-1 && errno == EEXIST) { /* 같은 pid 의 이전 실행이 남긴 찌꺼기 */
        mq_unlink(name);
        q = mq_open(name, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | extra, 0600, &a);
    }
    if (q != (mqd_t)-1 && g_nq < MAXQ) snprintf(g_names[g_nq++], sizeof g_names[0], "%s", name);
    if (name_out) snprintf(name_out, 64, "%s", name);
    return q;
}

static long q_cur(mqd_t q) { struct mq_attr a; return mq_getattr(q, &a) == 0 ? a.mq_curmsgs : -1; }
static long q_flags(mqd_t q) { struct mq_attr a; return mq_getattr(q, &a) == 0 ? a.mq_flags : -1; }
/* 남은 메시지를 막히지 않고 버린다 (curmsgs > 0 일 때만 mq_receive → 빈 큐에서 막힐 일 없음) */
static void q_drain(mqd_t q) {
    static char sink[8192];
    while (q_cur(q) > 0 && mq_receive(q, sink, sizeof sink, NULL) >= 0) { }
}

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}
static void sleep_ms(long ms) {
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&t, &t) != 0 && errno == EINTR) { }
}

/* 자식 회수: ms 안에 안 끝나면 SIGKILL (시작 코드가 멈추지 않게). 정상 종료 코드, 죽였으면 -1 */
static int reap(pid_t pid, long ms) {
    int st;
    long t0 = now_ms();
    for (;;) {
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
        if (r < 0) return -2;
        if (now_ms() - t0 > ms) break;
        sleep_ms(1);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &st, 0);
    return -1;
}

/* fd 하나를 ms 동안 읽기 가능해질 때까지 기다린다: 1 = 됨, 0 = 시간 초과 */
static int wait_readable(int fd, long ms) {
    struct pollfd p = { .fd = fd, .events = POLLIN };
    int r;
    while ((r = poll(&p, 1, (int)ms)) < 0 && errno == EINTR) { }
    return r > 0;
}

/* 막아 둔 시그널을 ms 안에 받으면 signo, 아니면 -1 */
static int wait_sig(int signo, long ms, siginfo_t *si) {
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, signo);
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    int r;
    while ((r = sigtimedwait(&s, si, &t)) < 0 && errno == EINTR) { }
    return r;
}

/* xorshift32 — 무작위 우선순위 시퀀스를 매번 같게 */
static uint32_t g_rng = 2463534242u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

/* ======================= Part A: 메시지 형식 + 생산자 ======================= */
#define MAXPROD 4
typedef struct {
    uint32_t prod, seq, crc;
    uint8_t pay[52];
} msg_t; /* 64 B 고정 = 큐의 mq_msgsize */
_Static_assert(sizeof(msg_t) == 64, "msg_t must be 64 bytes");

static uint32_t crc_of(const msg_t *m) { /* FNV-1a: prod, seq, pay */
    uint32_t h = 2166136261u;
    const uint8_t *p = (const uint8_t *)m;
    for (size_t i = 0; i < 8; i++) h = (h ^ p[i]) * 16777619u;
    for (size_t i = 0; i < sizeof m->pay; i++) h = (h ^ m->pay[i]) * 16777619u;
    return h;
}
static void msg_fill(msg_t *m, uint32_t prod, uint32_t seq) {
    m->prod = prod;
    m->seq = seq;
    for (size_t i = 0; i < sizeof m->pay; i++) m->pay[i] = (uint8_t)(prod * 131u + seq * 7u + i);
    m->crc = crc_of(m);
}

/* 생산자 (자식 프로세스): n 개를 우선순위 0 으로 "막히는" mq_send. 큐가 작아서 자주 막힌다. */
static void producer_main(mqd_t q, uint32_t id, int n) {
    alarm(5); /* 소비자가 없으면 영원히 막히니 상한 */
    msg_t m;
    for (int s = 0; s < n; s++) {
        msg_fill(&m, id, (uint32_t)s);
        if (mq_send(q, (const char *)&m, sizeof m, 0) != 0) _exit(10);
    }
    _exit(0);
}

typedef struct {
    int got, bad_len, bad_crc, out_of_order, timeouts;
    uint32_t next[MAXPROD]; /* 생산자별 다음에 기대하는 seq */
} cres_t;

/* ======================= Part B: 시간 + 우선순위 + 비블로킹 ======================= */

/* TODO(B-4a): 지금부터 ms 밀리초 뒤의 "절대 시각" 을 CLOCK_REALTIME 기준으로 ts 에 넣는다.
 *             mq_timedreceive/mq_timedsend 는 상대 시간이 아니라 절대 시각(Epoch 기준)을 받는다.
 *             tv_nsec 는 반드시 [0, 1e9) 로 정규화. */
static void abs_deadline_ms(long ms, struct timespec *ts) {
    (void)ms;
    ts->tv_sec = 0; /* TODO: 지금(CLOCK_REALTIME) + ms */
    ts->tv_nsec = 0;
}

/* TODO(B-4b): 최대 ms 동안 메시지 하나를 받는다. 받은 바이트 수, 실패면 -errno (시간 초과 = -ETIMEDOUT).
 *             EINTR 이면 "같은 마감 시각" 으로 다시 기다린다 (마감을 늘리지 말 것). */
static ssize_t recv_timeout(mqd_t q, void *buf, size_t cap, unsigned *prio, long ms) {
    (void)q; (void)buf; (void)cap; (void)prio; (void)ms;
    return -ETIMEDOUT; /* TODO */
}

/* TODO(A-1): total 개를 받을 때까지 recv_timeout(ms) 로 받으며 검증한다.
 *            길이 ≠ sizeof(msg_t) → bad_len, prod 범위 밖이거나 crc 틀림 → bad_crc,
 *            seq ≠ next[prod] → out_of_order (그리고 next[prod] = seq + 1 로 맞춘다).
 *            시간 초과면 timeouts++ 하고 -1, 다 받으면 0. 버퍼는 큐의 mq_msgsize 이상이어야 한다. */
static int consume_verify(mqd_t q, int nprod, int total, long ms, cres_t *r) {
    (void)q; (void)nprod; (void)total; (void)ms;
    memset(r, 0, sizeof *r);
    return -1; /* TODO */
}

/* TODO(B-2): 커널이 돌려줄 순서의 "모델". prio[i] 로 i = 0..n-1 순서대로 보냈을 때, 받는 순서를 order[] 에
 *            인덱스로 채운다: 우선순위 높은 것 먼저, 같으면 먼저 보낸 것 먼저 (= 안정 정렬, 내림차순). */
static void prio_model(const unsigned *prio, int n, int *order) {
    (void)prio;
    for (int i = 0; i < n; i++) order[i] = i; /* TODO: 지금은 보낸 순서 그대로 */
}

/* TODO(B-3): 이 호출 한 번만 O_NONBLOCK 으로 보낸다. 성공 0, 실패 -errno (가득 참 = -EAGAIN).
 *            mq_setattr 로 플래그를 켜고, 보낸 뒤 "원래 플래그" 로 되돌린다 (errno 는 되돌리기 전에 잡는다). */
static int try_send_nb(mqd_t q, const void *buf, size_t len, unsigned prio) {
    (void)q; (void)buf; (void)len; (void)prio;
    return -ENOSYS; /* TODO */
}

/* TODO(B-5): /proc/sys/fs/mqueue/<name> 의 값을 읽는다 (msg_max, msgsize_max, queues_max ...). 실패면 -1. */
static long read_mq_limit(const char *name) {
    (void)name;
    return -1; /* TODO */
}

/* ======================= Part C: mq_notify ======================= */

/* TODO(C-1): 큐가 "비어 있다가 메시지가 들어오면" signo 시그널(si_value.sival_int = value)이 오게 등록한다.
 *            성공 0, 실패 -errno (다른 프로세스가 이미 등록 = -EBUSY). */
static int notify_arm_signal(mqd_t q, int signo, int value) {
    (void)q; (void)signo; (void)value;
    return -ENOSYS; /* TODO */
}

/* SIGEV_THREAD 소비자의 상태 (전역: 콜백 스레드가 테스트 함수보다 오래 살 수 있으니 스택에 두지 않는다) */
typedef struct {
    mqd_t q;               /* O_NONBLOCK 로 연 수신용 디스크립터 */
    atomic_int msgs;       /* 받은 메시지 수 */
    atomic_int calls;      /* 콜백 호출 수 */
    atomic_int rearm_fail; /* 재등록 실패 수 */
    atomic_int in_cb;      /* 지금 콜백 안에 있는 스레드 수 */
    atomic_int on_main;    /* 콜백이 메인 스레드에서 돈 횟수 (0 이어야 한다) */
    pthread_t main_tid;
} tctx_t;
static tctx_t g_tc;

static void on_notify(union sigval sv);

/* TODO(C-2): SIGEV_THREAD 로 등록: 알림이 오면 새 스레드에서 on_notify(sival_ptr = c) 가 불린다. 0 / -errno */
static int notify_arm_thread(tctx_t *c) {
    (void)c;
    (void)on_notify; /* 정답에서는 sev.sigev_notify_function 으로 쓴다 */
    return -ENOSYS; /* TODO */
}

/* TODO(C-3): 알림 콜백. 등록은 1회용이므로 ① 먼저 다시 등록하고 ② 그다음 큐를 비울 때까지(EAGAIN) 받는다.
 *            받은 개수는 msgs, 호출 수는 calls, 재등록 실패는 rearm_fail, 메인 스레드에서 불렸으면 on_main 에 센다.
 *            in_cb 를 시작에 +1, 끝에 -1 (테스트가 정리 전에 콜백이 다 끝났는지 본다). */
static void on_notify(union sigval sv) {
    (void)sv; /* TODO */
}

/* ======================= 테스트 ======================= */
static void test_part_a(void) {
    printf("== Part A: 생산자 2 (fork) → 소비자 1 ==\n");
    enum { PER = 500 };
    mqd_t q = q_new("pipe", 4, sizeof(msg_t), 0, NULL); /* 깊이 4: 생산자가 자주 막힌다 */
    CHECK(q != (mqd_t)-1);
    if (q == (mqd_t)-1) return;
    fflush(stdout);
    pid_t p[2];
    for (int i = 0; i < 2; i++)
        if ((p[i] = fork()) == 0) producer_main(q, (uint32_t)i, PER);
    cres_t r;
    int rc = consume_verify(q, 2, 2 * PER, 2000, &r);
    int s0 = reap(p[0], rc == 0 ? 3000 : 200), s1 = reap(p[1], rc == 0 ? 3000 : 200);
    printf("  got %d  bad_len %d  bad_crc %d  out_of_order %d  next = {%u, %u}  producers exit %d %d\n",
           r.got, r.bad_len, r.bad_crc, r.out_of_order, r.next[0], r.next[1], s0, s1);
    CHECK(rc == 0 && r.got == 2 * PER);
    CHECK(r.bad_len == 0 && r.bad_crc == 0);
    CHECK(r.out_of_order == 0 && r.next[0] == PER && r.next[1] == PER); /* 같은 우선순위 = 생산자별 FIFO */
    CHECK(s0 == 0 && s1 == 0);
    CHECK(q_cur(q) == 0);
    mq_close(q);

    printf("== Part A: 메시지 경계는 커널이 지킨다 ==\n");
    char name[64];
    mqd_t b = q_new("bnd", 8, 8192, 0, name);
    static char big[8192], rx[8192];
    memset(big, 'x', sizeof big);
    const size_t len[3] = { 1, 100, 8192 };
    for (int i = 0; i < 3; i++) mq_send(b, big, len[i], 0);
    ssize_t got[3];
    for (int i = 0; i < 3; i++) got[i] = recv_timeout(b, rx, sizeof rx, NULL, 500);
    printf("  보냄 1/100/8192 → 받음 %zd/%zd/%zd\n", got[0], got[1], got[2]);
    CHECK(got[0] == 1 && got[1] == 100 && got[2] == 8192);

    q_drain(b);
    printf("== Part A: mq_unlink 는 이름만 지운다 ==\n");
    CHECK(mq_unlink(name) == 0);
    errno = 0;
    mqd_t again = mq_open(name, O_RDWR);
    CHECK(again == (mqd_t)-1 && errno == ENOENT);
    CHECK(mq_send(b, "hi", 2, 0) == 0 && mq_receive(b, rx, sizeof rx, NULL) == 2); /* 열린 디스크립터는 산다 */
    mq_close(b);
}

static void test_part_b(void) {
    printf("== Part B: 우선순위 모델 ==\n");
    {
        unsigned p1[5] = { 1, 5, 3, 5, 0 };
        int o[5], want[5] = { 1, 3, 2, 0, 4 };
        prio_model(p1, 5, o);
        CHECK(memcmp(o, want, sizeof o) == 0);
        unsigned p2[4] = { 7, 7, 7, 7 };
        int o2[4], want2[4] = { 0, 1, 2, 3 };
        prio_model(p2, 4, o2);
        CHECK(memcmp(o2, want2, sizeof o2) == 0); /* 전부 같으면 보낸 순서 그대로 */
        unsigned p3[3] = { 0, 1, 31 };
        int o3[3], want3[3] = { 2, 1, 0 };
        prio_model(p3, 3, o3);
        CHECK(memcmp(o3, want3, sizeof o3) == 0);
    }
    printf("== Part B: 커널 순서 == 모델 순서 (무작위 300 회, 동점 많음) ==\n");
    {
        mqd_t q = q_new("prio", 10, 16, 0, NULL);
        int mism = 0, prio_mism = 0, rounds = 300;
        for (int r = 0; r < rounds; r++) {
            int n = 1 + (int)(xs32() % 10);
            unsigned pr[10];
            int order[10];
            for (int i = 0; i < n; i++) {
                pr[i] = (r & 1) ? xs32() % 4 : xs32() % 32; /* 홀수 회차는 동점 투성이 */
                int v = i;
                mq_send(q, (const char *)&v, sizeof v, pr[i]);
            }
            prio_model(pr, n, order);
            for (int k = 0; k < n; k++) {
                char b[16];
                unsigned gp = 999;
                int v = -1;
                if (recv_timeout(q, b, sizeof b, &gp, 500) == (ssize_t)sizeof v) memcpy(&v, b, sizeof v);
                if (v != order[k]) mism++;
                if (gp != pr[order[k]]) prio_mism++;
            }
            q_drain(q); /* 시작 코드에서도 다음 회차 send 가 막히지 않게 */
        }
        printf("  %d 회 중 순서 불일치 %d, 우선순위 불일치 %d\n", rounds, mism, prio_mism);
        CHECK(mism == 0 && prio_mism == 0);
        long pmax = sysconf(_SC_MQ_PRIO_MAX);
        errno = 0;
        int rc = mq_send(q, "x", 1, (unsigned)pmax); /* 허용 범위는 0 .. MQ_PRIO_MAX-1 */
        printf("  sysconf(_SC_MQ_PRIO_MAX) = %ld, prio %ld 로 send → %s\n", pmax, pmax, strerror(errno));
        CHECK(pmax >= 32 && rc == -1 && errno == EINVAL);
        mq_close(q);
    }
    printf("== Part B: 가득 참 — O_NONBLOCK 은 EAGAIN, 기본은 막힌다 ==\n");
    {
        mqd_t q = q_new("full", 2, 8, 0, NULL);
        mq_send(q, "a", 1, 0);
        mq_send(q, "b", 1, 0);
        int rc = try_send_nb(q, "c", 1, 0);
        CHECK(rc == -EAGAIN);
        CHECK(q_cur(q) == 2 && (q_flags(q) & O_NONBLOCK) == 0); /* 플래그는 원래대로 */
        int fds[2];
        if (pipe(fds) != 0) return;
        fflush(stdout);
        pid_t c = fork();
        if (c == 0) { /* 자식: 막히는 send → 끝나면 파이프에 1 바이트 */
            close(fds[0]);
            alarm(5);
            if (mq_send(q, "c", 1, 0) == 0 && write(fds[1], "S", 1) == 1) _exit(0);
            _exit(1);
        }
        close(fds[1]);
        int early = wait_readable(fds[0], 150); /* 큐가 꽉 찼으니 끝났을 리 없다 */
        char tmp[8];
        long got = mq_receive(q, tmp, sizeof tmp, NULL); /* 자리 하나 → 자식의 send 가 풀린다 */
        int late = wait_readable(fds[0], 2000);
        int st = reap(c, 2000);
        printf("  자식 send: 150 ms 안에 끝남? %d → 하나 받은 뒤 끝남? %d (exit %d)\n", early, late, st);
        CHECK(early == 0 && got == 1);
        CHECK(late == 1 && st == 0);
        close(fds[0]);
        CHECK(try_send_nb(q, "d", 1, 0) == -EAGAIN); /* 다시 2/2 */
        tmp[0] = 0;
        mq_receive(q, tmp, sizeof tmp, NULL);
        CHECK(try_send_nb(q, "e", 1, 0) == 0 && q_cur(q) == 2);
        struct timespec dl;
        abs_deadline_ms(50, &dl);
        errno = 0;
        rc = mq_timedsend(q, "f", 1, 0, &dl);
        CHECK(rc == -1 && errno == ETIMEDOUT);
        mq_close(q);
    }
    printf("== Part B: mq_timedreceive — 절대 시각 ==\n");
    {
        int okn = 1;
        for (long ms = 0; ms <= 2500; ms += 1 + ms / 3) {
            struct timespec a, d;
            clock_gettime(CLOCK_REALTIME, &a);
            abs_deadline_ms(ms, &d);
            long long diff = (d.tv_sec - a.tv_sec) * 1000LL + (d.tv_nsec - a.tv_nsec) / 1000000LL;
            if (d.tv_nsec < 0 || d.tv_nsec >= 1000000000L || diff < ms - 1 || diff > ms + 5000) okn = 0;
        }
        CHECK(okn); /* 정규화 + "지금 + ms" */
        mqd_t q = q_new("tmo", 4, 16, 0, NULL);
        char b[16];
        long t0 = now_ms();
        ssize_t r = recv_timeout(q, b, sizeof b, NULL, 30);
        printf("  빈 큐, 30 ms → %zd (%s), %ld ms 걸림\n", r, r < 0 ? strerror((int)-r) : "ok", now_ms() - t0);
        CHECK(r == -ETIMEDOUT);
        fflush(stdout);
        pid_t c = fork();
        if (c == 0) { sleep_ms(50); _exit(mq_send(q, "late", 4, 3) == 0 ? 0 : 1); }
        unsigned pr = 0;
        t0 = now_ms();
        r = recv_timeout(q, b, sizeof b, &pr, 3000); /* 상대 시간 {0, 3 s} 를 넘기면 1970 년 → 즉시 실패 */
        printf("  50 ms 뒤 도착, 3000 ms 대기 → %zd (prio %u), %ld ms 걸림\n", r, pr, now_ms() - t0);
        CHECK(r == 4 && pr == 3);
        CHECK(reap(c, 2000) == 0);
        printf("== Part B: mq_getattr / mq_setattr ==\n");
        struct mq_attr na = { .mq_flags = O_NONBLOCK, .mq_maxmsg = 99, .mq_msgsize = 999 }, old, now;
        CHECK(mq_setattr(q, &na, &old) == 0 && old.mq_flags == 0 && old.mq_maxmsg == 4);
        mq_getattr(q, &now);
        printf("  setattr(flags=O_NONBLOCK, maxmsg=99, msgsize=999) → flags=%#lx maxmsg=%ld msgsize=%ld\n",
               now.mq_flags, now.mq_maxmsg, now.mq_msgsize);
        CHECK((now.mq_flags & O_NONBLOCK) && now.mq_maxmsg == 4 && now.mq_msgsize == 16); /* 플래그만 바뀐다 */
        mq_setattr(q, &old, NULL);
        printf("== Part B: EMSGSIZE 두 방향 ==\n");
        char big[17] = { 0 };
        errno = 0;
        CHECK(mq_send(q, big, 17, 0) == -1 && errno == EMSGSIZE); /* msgsize(16) 보다 길다 */
        mq_send(q, "s", 1, 0);
        char small[8];
        errno = 0;
        CHECK(mq_receive(q, small, sizeof small, NULL) == -1 && errno == EMSGSIZE); /* 1 B 인데도! */
        CHECK(q_cur(q) == 1);                                                         /* 메시지는 남아 있다 */
        CHECK(mq_receive(q, b, sizeof b, NULL) == 1);
        mq_close(q);
    }
    printf("== Part B: /proc/sys/fs/mqueue 한도 ==\n");
    {
        long mm = read_mq_limit("msg_max"), ms = read_mq_limit("msgsize_max"), qm = read_mq_limit("queues_max");
        long md = read_mq_limit("msg_default"), sd = read_mq_limit("msgsize_default");
        printf("  msg_max %ld  msgsize_max %ld  queues_max %ld  msg_default %ld  msgsize_default %ld  euid %d\n",
               mm, ms, qm, md, sd, (int)geteuid());
        CHECK(mm > 0 && ms > 0 && qm > 0 && md > 0 && sd > 0);
        mqd_t ok = mm > 0 ? q_new("lim", mm, 64, 0, NULL) : (mqd_t)-1;
        CHECK(ok != (mqd_t)-1); /* 딱 한도 = 허용 */
        if (ok != (mqd_t)-1) mq_close(ok);
        if (geteuid() == 0) {
            printf("  (root = CAP_SYS_RESOURCE 라 msg_max 를 넘겨도 열릴 수 있음 — 아래 2개는 비특권 기준)\n");
        }
        errno = 0;
        mqd_t over = q_new("lim2", mm + 1, 64, 0, NULL);
        int e1 = errno;
        if (over != (mqd_t)-1) mq_close(over);
        errno = 0;
        mqd_t over2 = q_new("lim3", 4, ms + 1, 0, NULL);
        int e2 = errno;
        if (over2 != (mqd_t)-1) mq_close(over2);
        errno = 0;
        mqd_t zero = q_new("lim4", 0, 64, 0, NULL);
        int e3 = errno;
        if (zero != (mqd_t)-1) mq_close(zero);
        printf("  maxmsg=msg_max+1 → %s, msgsize=msgsize_max+1 → %s, maxmsg=0 → %s\n",
               strerror(e1), strerror(e2), strerror(e3));
        CHECK(mm > 0 && over == (mqd_t)-1 && e1 == EINVAL);
        CHECK(ms > 0 && over2 == (mqd_t)-1 && e2 == EINVAL);
        CHECK(zero == (mqd_t)-1 && e3 == EINVAL);
    }
}

static void test_part_c(void) {
    printf("== Part C: mq_notify SIGEV_SIGNAL ==\n");
    mqd_t q = q_new("ntf", 4, 16, 0, NULL);
    char b[16];
    siginfo_t si;
    memset(&si, 0, sizeof si);
    CHECK(notify_arm_signal(q, SIGUSR1, 42) == 0);
    fflush(stdout);
    pid_t c = fork();
    if (c == 0) _exit(mq_send(q, "m1", 2, 0) == 0 ? 0 : 1);
    int sig = wait_sig(SIGUSR1, 1000, &si);
    reap(c, 2000);
    printf("  sig %d  si_code %d (SI_MESGQ %d)  sival_int %d  si_pid %d (child %d)\n", sig, si.si_code, SI_MESGQ,
           si.si_value.sival_int, (int)si.si_pid, (int)c);
    CHECK(sig == SIGUSR1 && si.si_code == SI_MESGQ && si.si_value.sival_int == 42);
    CHECK(si.si_pid == c); /* 보낸 프로세스 */
    mq_receive(q, b, sizeof b, NULL);
    mq_send(q, "m2", 2, 0); /* 등록은 1회용: 다시 등록 안 했으니 신호 없음 */
    CHECK(wait_sig(SIGUSR1, 100, &si) == -1);
    /* 지금 큐에 1개: 이 상태로 등록하면 "비어 있다가 들어올 때" 까지 안 온다 */
    CHECK(notify_arm_signal(q, SIGUSR1, 7) == 0);
    mq_send(q, "m3", 2, 0);
    int sig_nonempty = wait_sig(SIGUSR1, 100, &si);
    mq_receive(q, b, sizeof b, NULL);
    mq_receive(q, b, sizeof b, NULL); /* 비움 */
    mq_send(q, "m4", 2, 0);
    int sig_after = wait_sig(SIGUSR1, 1000, &si);
    printf("  비어있지 않을 때 send → %d, 비운 뒤 send → %d\n", sig_nonempty, sig_after);
    CHECK(sig_nonempty == -1 && sig_after == SIGUSR1 && si.si_value.sival_int == 7);
    mq_receive(q, b, sizeof b, NULL);

    printf("== Part C: 한 큐에 등록은 한 프로세스만 (EBUSY) ==\n");
    int up[2], down[2];
    if (pipe(up) != 0 || pipe(down) != 0) return;
    fflush(stdout);
    c = fork();
    if (c == 0) { /* 자식이 먼저 등록하고, 부모가 'X' 를 보낼 때까지 버틴다 */
        alarm(5);
        close(up[0]);
        close(down[1]);
        char x;
        if (notify_arm_signal(q, SIGUSR2, 1) != 0) _exit(1);
        if (write(up[1], "R", 1) != 1) _exit(2);
        if (read(down[0], &x, 1) != 1) _exit(3);
        _exit(0); /* 종료 = 디스크립터 닫힘 = 등록 해제 */
    }
    close(up[1]);
    close(down[0]);
    char x = 0;
    int ready = wait_readable(up[0], 2000) && read(up[0], &x, 1) == 1 && x == 'R';
    int busy = notify_arm_signal(q, SIGUSR1, 0);
    CHECK(ready && busy == -EBUSY);
    if (write(down[1], "X", 1) != 1) { }
    int st = reap(c, 2000);
    int after = notify_arm_signal(q, SIGUSR1, 0);
    printf("  자식 등록 중 부모 → %d (-EBUSY = %d), 자식 종료(%d) 뒤 → %d\n", busy, -EBUSY, st, after);
    CHECK(st == 0 && after == 0);
    CHECK(mq_notify(q, NULL) == 0); /* 해제 */
    close(up[0]);
    close(down[1]);

    printf("== Part C: mqd_t 는 fd — poll 가능 (리눅스) ==\n");
    struct pollfd pf = { .fd = (int)q, .events = POLLIN | POLLOUT };
    int r0 = poll(&pf, 1, 0);
    short ev0 = pf.revents;
    mq_send(q, "p", 1, 0);
    pf.revents = 0;
    int r1 = poll(&pf, 1, 0);
    short ev1 = pf.revents;
    for (int i = 0; i < 3; i++) mq_send(q, "p", 1, 0); /* 4/4 = 가득 */
    pf.revents = 0;
    poll(&pf, 1, 0);
    short ev2 = pf.revents;
    printf("  빈 큐 revents %#x, 1개 %#x, 가득 %#x (POLLIN %#x POLLOUT %#x)\n", ev0, ev1, ev2, POLLIN, POLLOUT);
    CHECK(r0 == 1 && ev0 == POLLOUT && r1 == 1 && ev1 == (POLLIN | POLLOUT) && ev2 == POLLIN);
    while (q_cur(q) > 0) mq_receive(q, b, sizeof b, NULL);
    mq_close(q);

    printf("== Part C: SIGEV_THREAD — 재등록 후 drain ==\n");
#ifdef OSM15_TSAN
    printf("  (TSan 빌드: SIGEV_THREAD 테스트 건너뜀 — glibc 내부 스레드를 TSan 이 추적 못 함)\n");
    return;
#endif
    char name[64];
    mqd_t tx = q_new("thr", 8, 16, 0, name);
    memset(&g_tc, 0, sizeof g_tc);
    g_tc.q = mq_open(name, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    g_tc.main_tid = pthread_self();
    int arm = notify_arm_thread(&g_tc);
    CHECK(arm == 0);
    int sent = 0, rounds_ok = 0;
    for (int round = 0; arm == 0 && round < 5; round++) {
        int k = 1 + round % 3; /* 1, 2, 3, 1, 2 개씩 */
        for (int i = 0; i < k; i++) { mq_send(tx, "t", 1, (unsigned)i); sent++; }
        long t0 = now_ms();
        while (atomic_load(&g_tc.msgs) < sent && now_ms() - t0 < 1000) sleep_ms(1);
        if (atomic_load(&g_tc.msgs) == sent) rounds_ok++;
        else break;
    }
    long t0 = now_ms();
    while (atomic_load(&g_tc.in_cb) > 0 && now_ms() - t0 < 1000) sleep_ms(1);
    int msgs = atomic_load(&g_tc.msgs), calls = atomic_load(&g_tc.calls);
    printf("  보냄 %d, 콜백이 받음 %d, 콜백 호출 %d, 재등록 실패 %d, 메인 스레드에서 %d\n", sent, msgs, calls,
           atomic_load(&g_tc.rearm_fail), atomic_load(&g_tc.on_main));
    CHECK(rounds_ok == 5 && msgs == sent && sent == 9);
    CHECK(calls >= 5 && calls <= sent && atomic_load(&g_tc.rearm_fail) == 0);
    CHECK(atomic_load(&g_tc.on_main) == 0 && atomic_load(&g_tc.in_cb) == 0);
    mq_notify(g_tc.q, NULL); /* 마지막 콜백이 걸어 둔 등록 해제 */
    mq_close(g_tc.q);
    mq_close(tx);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    g_main_pid = getpid();
    atexit(cleanup_names);
    signal(SIGPIPE, SIG_IGN); /* 자식이 먼저 죽어도 파이프 write 가 부모를 죽이지 않게 */
    /* sigtimedwait 로 받을 시그널은 스레드가 생기기 전에 막는다 (이후 스레드가 마스크를 물려받는다) */
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGUSR1);
    sigaddset(&s, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &s, NULL);

    test_part_a();
    test_part_b();
    test_part_c();

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
