/* OS-M16 추가 연습 — 한 번에 검증 (macOS)
 *
 *   연습 1  남은 소켓 파일: bind → EADDRINUSE, connect → ECONNREFUSED, 안전한 bind_unix_fresh() + sun_path 길이
 *   연습 2  listen 소켓 넘겨주기: fork 뒤에 만든 listen fd 를 자식에게 SCM_RIGHTS 로 → 자식이 accept
 *   연습 3  전송 중인 fd: 보낸 쪽이 fd·소켓을 다 닫고 파일도 unlink 했어도 받은 쪽은 읽는다 + FD_CLOEXEC 는 안 넘어온다
 *   연습 4  받은 fd 검증: 읽기 전용 일반 파일만 받는다 (pipe·쓰기 가능·장치 fd 거부)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static char g_run[96];

/* ---- 본문 send_fds / recv_fds 축약판 (recv 는 FD_CLOEXEC 를 켜지 않는다 — 연습 3 에서 원래 플래그를 본다) ---- */
static ssize_t send_fds(int sock, const int *fds, int n, const void *data, size_t len) {
    union { struct cmsghdr a; char buf[CMSG_SPACE(4 * sizeof(int))]; } ctl;
    memset(&ctl, 0, sizeof ctl);
    struct iovec iov = { (void *)data, len };
    struct msghdr m; memset(&m, 0, sizeof m);
    m.msg_iov = &iov; m.msg_iovlen = 1;
    if (n > 0) {
        m.msg_control = ctl.buf; m.msg_controllen = (socklen_t)CMSG_SPACE((size_t)n * sizeof(int));
        struct cmsghdr *c = CMSG_FIRSTHDR(&m);
        c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = (socklen_t)CMSG_LEN((size_t)n * sizeof(int));
        memcpy(CMSG_DATA(c), fds, (size_t)n * sizeof(int));
    }
    return sendmsg(sock, &m, 0);
}
static ssize_t recv_fd(int sock, void *data, size_t len, int *fd) {   /* fd 0~1개 */
    union { struct cmsghdr a; char buf[CMSG_SPACE(sizeof(int))]; } ctl;
    memset(&ctl, 0, sizeof ctl);
    struct iovec iov = { data, len };
    struct msghdr m; memset(&m, 0, sizeof m);
    m.msg_iov = &iov; m.msg_iovlen = 1;
    m.msg_control = ctl.buf; m.msg_controllen = sizeof ctl.buf;
    *fd = -1;
    ssize_t r = recvmsg(sock, &m, 0);
    if (r < 0) return -1;
    struct cmsghdr *c = CMSG_FIRSTHDR(&m);
    if (c && c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS && c->cmsg_len == CMSG_LEN(sizeof(int)))
        memcpy(fd, CMSG_DATA(c), sizeof(int));
    if (m.msg_flags & MSG_CTRUNC) { if (*fd >= 0) close(*fd); *fd = -1; errno = EMSGSIZE; return -1; }
    return r;
}
static void set_timeouts(int s, int ms) {
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}
static int reap(pid_t pid) {
    int st = 0;
    for (int ms = 0; ms < 5000; ms++) {
        if (waitpid(pid, &st, WNOHANG) == pid) return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
        struct timespec t = { 0, 1000000 };
        nanosleep(&t, NULL);
    }
    kill(pid, SIGKILL); waitpid(pid, &st, 0);
    return 255;
}
static int fill_sa(struct sockaddr_un *sa, const char *path) {
    memset(sa, 0, sizeof *sa);
    sa->sun_family = AF_UNIX;
    if (strlen(path) >= sizeof sa->sun_path) { errno = ENAMETOOLONG; return -1; }
    strcpy(sa->sun_path, path);
    sa->sun_len = (unsigned char)SUN_LEN(sa);
    return 0;
}

/* ---- 연습 1: 남은 소켓 파일을 안전하게 치우고 bind ---- */
static int bind_unix_fresh(const char *path) {
    struct sockaddr_un sa;
    if (fill_sa(&sa, path) < 0) return -1;
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return -1;
    if (bind(s, (struct sockaddr *)&sa, sizeof sa) == 0) return s;
    if (errno != EADDRINUSE) { close(s); return -1; }
    int probe = socket(AF_UNIX, SOCK_STREAM, 0);        /* 누가 살아서 듣고 있나? */
    int alive = probe >= 0 && connect(probe, (struct sockaddr *)&sa, sizeof sa) == 0;
    int cerr = errno;
    if (probe >= 0) close(probe);
    if (alive) { close(s); errno = EADDRINUSE; return -1; }     /* 살아 있는 서버의 파일은 지우면 안 된다 */
    if (cerr != ECONNREFUSED) { close(s); errno = cerr; return -1; }
    unlink(path);                                        /* 주인이 죽은 파일만 지운다 */
    if (bind(s, (struct sockaddr *)&sa, sizeof sa) < 0) { close(s); return -1; }
    return s;
}

static void ex1(void) {
    char path[128]; struct sockaddr_un sa;
    snprintf(path, sizeof path, "%s/stale.sock", g_run);
    fill_sa(&sa, path);
    int a = socket(AF_UNIX, SOCK_STREAM, 0);
    bind(a, (struct sockaddr *)&sa, sizeof sa);
    close(a);                                            /* unlink 없이 종료한 서버 흉내 */
    int b = socket(AF_UNIX, SOCK_STREAM, 0);
    errno = 0;
    int rb = bind(b, (struct sockaddr *)&sa, sizeof sa);
    int eb = errno;
    close(b);
    int c = socket(AF_UNIX, SOCK_STREAM, 0);
    errno = 0;
    int rc = connect(c, (struct sockaddr *)&sa, sizeof sa);
    int ec = errno;
    close(c);
    CHECK(rb == -1 && eb == EADDRINUSE);                 /* 파일이 남아 있으면 bind 실패 */
    CHECK(rc == -1 && ec == ECONNREFUSED);               /* 듣는 소켓이 없다 */
    int s = bind_unix_fresh(path);
    CHECK(s >= 0 && listen(s, 4) == 0);                  /* 죽은 파일은 치우고 bind */
    errno = 0;
    int s2 = bind_unix_fresh(path);
    CHECK(s2 == -1 && errno == EADDRINUSE);              /* 살아 있는 서버는 건드리지 않는다 */
    struct stat st;
    CHECK(stat(path, &st) == 0 && S_ISSOCK(st.st_mode));
    if (s >= 0) close(s);
    unlink(path);
    char longp[200];
    memset(longp, 'x', sizeof longp); longp[0] = '/'; longp[sizeof longp - 1] = '\0';
    errno = 0;
    CHECK(bind_unix_fresh(longp) == -1 && errno == ENAMETOOLONG);   /* sun_path 104 B 를 넘으면 거절 */
    printf("ex1 stale path: bind -> EADDRINUSE, connect -> ECONNREFUSED; fresh bind ok; sizeof sun_path = %zu\n",
           sizeof sa.sun_path);
}

/* ---- 연습 2: listen 소켓 넘겨주기 ---- */
static void ex2(void) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) { CHECK(!"socketpair"); return; }
    set_timeouts(sv[1], 2000);
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {                                      /* 새 워커: listen fd 를 받아 accept */
        alarm(4);
        close(sv[0]);
        char d; int lfd;
        if (recv_fd(sv[1], &d, 1, &lfd) != 1 || lfd < 0) _exit(1);
        int c = accept(lfd, NULL, NULL);
        if (c < 0) _exit(2);
        char buf[8] = { 0 };
        if (read(c, buf, 2) != 2) _exit(3);
        buf[0] = (char)(buf[0] - 32); buf[1] = (char)(buf[1] - 32);
        _exit(write(c, buf, 2) == 2 ? 0 : 4);
    }
    close(sv[1]);
    char path[128]; struct sockaddr_un sa;
    snprintf(path, sizeof path, "%s/handoff.sock", g_run);
    fill_sa(&sa, path);
    int l = socket(AF_UNIX, SOCK_STREAM, 0);             /* fork 뒤에 만든다 → 자식은 상속하지 않았다 */
    bind(l, (struct sockaddr *)&sa, sizeof sa);
    listen(l, 4);
    ssize_t s = send_fds(sv[0], &l, 1, "L", 1);
    close(l);                                            /* 부모는 더 이상 듣지 않는다 — 듣는 쪽은 자식뿐 */
    int c = socket(AF_UNIX, SOCK_STREAM, 0);
    set_timeouts(c, 2000);
    char buf[4] = { 0 };
    int ok = connect(c, (struct sockaddr *)&sa, sizeof sa) == 0 && write(c, "hi", 2) == 2 && read(c, buf, 2) == 2;
    close(c);
    int code = reap(pid);
    unlink(path);
    close(sv[0]);
    CHECK(s == 1 && ok && memcmp(buf, "HI", 2) == 0 && code == 0);
    printf("ex2 listen-fd handoff: worker answered \"%.2s\"\n", buf);
}

/* ---- 연습 3: 전송 중인 fd 는 보낸 쪽이 다 닫아도 산다 ---- */
static void ex3(void) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) { CHECK(!"socketpair"); return; }
    set_timeouts(sv[1], 1000);
    char path[128];
    snprintf(path, sizeof path, "%s/inflight.XXXXXX", g_run);
    int f = mkstemp(path);
    write(f, "still here", 10);
    lseek(f, 0, SEEK_SET);
    fcntl(f, F_SETFD, FD_CLOEXEC);                       /* 보낸 쪽 fd 에는 FD_CLOEXEC */
    ssize_t s = send_fds(sv[0], &f, 1, "X", 1);
    close(f); unlink(path); close(sv[0]);                /* 이름도, 보낸 쪽 fd 도, 보낸 쪽 소켓도 없다 */
    char d = 0; int g = -1;
    ssize_t r = recv_fd(sv[1], &d, 1, &g);
    char buf[16] = { 0 };
    ssize_t n = g >= 0 ? read(g, buf, 10) : -1;
    int flags = g >= 0 ? fcntl(g, F_GETFD) : -1;
    CHECK(s == 1 && r == 1 && g >= 0 && n == 10 && memcmp(buf, "still here", 10) == 0);
    CHECK(flags == 0);                                   /* fd 플래그(FD_CLOEXEC)는 프로세스별 — 전달되지 않는다 */
    printf("ex3 in-flight fd: read \"%s\", FD_CLOEXEC on received fd = %d\n", buf, flags);
    if (g >= 0) close(g);
    close(sv[1]);
}

/* ---- 연습 4: 받은 fd 를 믿기 전에 검사 ---- */
static int accept_ro_regular(int fd) {
    struct stat st;
    if (fstat(fd, &st) < 0) return -1;
    if (!S_ISREG(st.st_mode)) { errno = EINVAL; return -1; }       /* pipe/FIFO 는 read 가 영원히 막힐 수 있다 */
    int fl = fcntl(fd, F_GETFL);
    if (fl < 0) return -1;
    if ((fl & O_ACCMODE) != O_RDONLY) { errno = EACCES; return -1; } /* 쓰기 권한까지 받을 이유가 없다 */
    return 0;
}

static void ex4(void) {
    int sv[2], p[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0 || pipe(p) < 0) { CHECK(!"setup"); return; }
    set_timeouts(sv[1], 1000);
    char path[128];
    snprintf(path, sizeof path, "%s/ro.XXXXXX", g_run);
    int rw = mkstemp(path);
    int ro = open(path, O_RDONLY);
    unlink(path);
    int nul = open("/dev/null", O_RDONLY);
    int cand[4] = { ro, rw, p[0], nul };
    int want_err[4] = { 0, EACCES, EINVAL, EINVAL };
    int ok = 0;
    for (int i = 0; i < 4; i++) {
        char d; int g = -1;
        send_fds(sv[0], &cand[i], 1, "V", 1);
        if (recv_fd(sv[1], &d, 1, &g) != 1 || g < 0) continue;
        errno = 0;
        int r = accept_ro_regular(g);
        if ((want_err[i] == 0 && r == 0) || (want_err[i] != 0 && r == -1 && errno == want_err[i])) ok++;
        close(g);
    }
    CHECK(ok == 4);
    printf("ex4 validate received fd: %d/4 classified (ro-regular ok, rw-file EACCES, pipe EINVAL, /dev/null EINVAL)\n", ok);
    close(ro); close(rw); close(nul); close(p[0]); close(p[1]); close(sv[0]); close(sv[1]);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    mkdir("/tmp/OS-M16_work", 0700);
    snprintf(g_run, sizeof g_run, "/tmp/OS-M16_work/pr.%d", (int)getpid());
    if (mkdir(g_run, 0700) < 0) { perror("mkdir"); return 1; }
    ex1(); ex2(); ex3(); ex4();
    CHECK(rmdir(g_run) == 0);                            /* 아무 파일도 남기지 않았다 */
    rmdir("/tmp/OS-M16_work");
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
