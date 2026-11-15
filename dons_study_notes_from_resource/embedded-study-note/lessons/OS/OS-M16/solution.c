/* OS-M16 Unix 소켓 심화: fd 전달(SCM_RIGHTS)과 피어 자격 확인   (macOS 전용 — LOCAL_PEERCRED/LOCAL_PEERPID)
 *
 *   Part A  send_fds / recv_fds — sendmsg/recvmsg 의 보조 데이터(cmsg) SCM_RIGHTS 로 열린 fd 를 넘긴다.
 *           socketpair + fork, 이름 있는 소켓(/tmp/OS-M16_work/run.<pid>/), 파일 · pipe 끝 · fd 3개 한 번에.
 *   Part B  cmsg 버퍼 실수 재현: CMSG_LEN vs CMSG_SPACE 를 ABI(Darwin / Linux LP64)별로 계산하는 모델,
 *           MSG_CTRUNC 검출, cmsg 를 무시하면 fd 가 새는 것(fd 표 개수), 스트림 vs 데이터그램 경계 + 길이 프레이밍.
 *   Part C  peer_identity (getpeereid + LOCAL_PEERCRED xucred + LOCAL_PEERPID) → allow_peer(acl, uid) 로
 *           접근 제어 → serve_one: 허가된 클라이언트에게만 "장치 fd" 를 건네는 브로커 데몬.
 *
 * 모든 소켓에 SO_RCVTIMEO/SO_SNDTIMEO, 모든 자식에 alarm() 워치독, 부모는 waitpid(WNOHANG) 폴링 + SIGKILL
 * → 틀린 코드는 멈추지 않고 FAIL. 임시 파일은 mkstemp 직후 unlink, 소켓 경로는 테스트 끝에 unlink,
 * 마지막에 run 디렉터리를 지운다. 시간 측정은 없다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ucred.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_FDS 8            /* 이 프로토콜이 한 메시지에 허용하는 fd 최대 개수 */

/* ---- 하네스 제공: 짧은 읽기/쓰기 루프 (OS-B14 와 같다) ---- */
static ssize_t write_all(int fd, const void *buf, size_t n) {
    const char *p = buf; size_t done = 0;
    while (done < n) {
        ssize_t w = write(fd, p + done, n - done);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        done += (size_t)w;
    }
    return (ssize_t)done;
}
static ssize_t read_all(int fd, void *buf, size_t n) {
    char *p = buf; size_t done = 0;
    while (done < n) {
        ssize_t r = read(fd, p + done, n - done);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) break;
        done += (size_t)r;
    }
    return (ssize_t)done;
}

/* ======================= Part A — SCM_RIGHTS ======================= */

/* fds[0..n) 를 data(len ≥ 1 바이트)에 실어 보낸다. 성공: 보낸 바이트 수, 실패: -1 (errno).
 * 스트림 소켓은 데이터 0 바이트에 fd 만 실을 수 없다 (macOS 실측: EMSGSIZE) → len ≥ 1 을 요구한다. */
static ssize_t send_fds(int sock, const int *fds, int n, const void *data, size_t len) {
    if (n < 0 || n > MAX_FDS || (n > 0 && fds == NULL) || data == NULL || len == 0) { errno = EINVAL; return -1; }
    union { struct cmsghdr align; char buf[CMSG_SPACE(MAX_FDS * sizeof(int))]; } ctl;   /* 정렬 보장 */
    memset(&ctl, 0, sizeof ctl);
    struct iovec iov = { (void *)data, len };
    struct msghdr msg;
    memset(&msg, 0, sizeof msg);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (n > 0) {
        msg.msg_control = ctl.buf;
        msg.msg_controllen = (socklen_t)CMSG_SPACE((size_t)n * sizeof(int));   /* 버퍼 = SPACE */
        struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = (socklen_t)CMSG_LEN((size_t)n * sizeof(int));          /* 헤더 = LEN */
        memcpy(CMSG_DATA(c), fds, (size_t)n * sizeof(int));                  /* 캐스트 대신 memcpy */
    }
    ssize_t r;
    do r = sendmsg(sock, &msg, 0); while (r < 0 && errno == EINTR);
    return r;
}

/* 최대 len 바이트 데이터 + 최대 max_fds 개 fd 를 받는다. 반환: 데이터 바이트 수 (0 = EOF), 실패 -1.
 * 받은 fd 는 FD_CLOEXEC 를 켠다 (macOS 에는 MSG_CMSG_CLOEXEC 가 없다).
 * MSG_CTRUNC 이거나 SCM_RIGHTS 가 아닌 cmsg 가 오면: 보이는 fd 를 전부 닫고 *nfds = 0, errno = EMSGSIZE, -1. */
static ssize_t recv_fds(int sock, void *data, size_t len, int *fds, int max_fds, int *nfds) {
    *nfds = 0;
    if (max_fds < 0 || max_fds > MAX_FDS || data == NULL || len == 0) { errno = EINVAL; return -1; }
    union { struct cmsghdr align; char buf[CMSG_SPACE(MAX_FDS * sizeof(int))]; } ctl;
    memset(&ctl, 0, sizeof ctl);
    struct iovec iov = { data, len };
    struct msghdr msg;
    memset(&msg, 0, sizeof msg);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = ctl.buf;
    msg.msg_controllen = (socklen_t)CMSG_SPACE((size_t)max_fds * sizeof(int));
    ssize_t r;
    do r = recvmsg(sock, &msg, 0); while (r < 0 && errno == EINTR);
    if (r < 0) return -1;
    const unsigned char *end = (const unsigned char *)msg.msg_control + msg.msg_controllen;  /* 커널이 실제로 쓴 끝 */
    int got = 0;
    bool bad = false;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c != NULL; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_len < CMSG_LEN(0)) { bad = true; break; }
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS) { bad = true; continue; }
        const unsigned char *p = CMSG_DATA(c);
        if (p > end) { bad = true; break; }
        size_t claimed = c->cmsg_len - CMSG_LEN(0);          /* 헤더가 주장하는 길이 */
        size_t avail = (size_t)(end - p);                     /* 실제로 버퍼에 들어온 길이 */
        size_t bytes = claimed < avail ? claimed : avail;     /* 잘렸으면 cmsg_len 을 믿지 않는다 */
        for (size_t i = 0; i + sizeof(int) <= bytes; i += sizeof(int)) {
            int fd;
            memcpy(&fd, p + i, sizeof fd);
            if (got < max_fds) fds[got++] = fd;
            else { close(fd); bad = true; }
        }
    }
    if ((msg.msg_flags & MSG_CTRUNC) || bad) {
        for (int i = 0; i < got; i++) close(fds[i]);
        errno = EMSGSIZE;
        return -1;
    }
    for (int i = 0; i < got; i++) (void)fcntl(fds[i], F_SETFD, FD_CLOEXEC);
    *nfds = got;
    return r;
}

/* ======================= Part B — cmsg 크기 모델 + 프레이밍 ======================= */

/* struct cmsghdr 의 ABI: 헤더 크기와 정렬 단위.
 *   Darwin (이 맥): { socklen_t cmsg_len; int level; int type; } = 12 B, __DARWIN_ALIGN32 → 4 B 정렬
 *   Linux LP64 (aarch64/x86-64 glibc): { size_t cmsg_len; int level; int type; } = 16 B, sizeof(size_t) = 8 B 정렬 */
typedef struct { const char *name; size_t hdr; size_t align; } cmsg_abi_t;
static const cmsg_abi_t ABI_DARWIN  = { "darwin",     12, 4 };
static const cmsg_abi_t ABI_LINUX64 = { "linux-lp64", 16, 8 };

static size_t align_up(size_t x, size_t a) { return (x + a - 1) & ~(a - 1); }   /* a 는 2의 거듭제곱 */

/* CMSG_LEN(payload): cmsg_len 필드에 넣는 값 = 정렬된 헤더 + payload (끝 패딩 없음) */
static size_t cmsg_len_abi(const cmsg_abi_t *a, size_t payload) {
    return align_up(a->hdr, a->align) + payload;
}
/* CMSG_SPACE(payload): 버퍼에서 차지하는 자리 = 정렬된 헤더 + 정렬된 payload (끝 패딩 포함) */
static size_t cmsg_space_abi(const cmsg_abi_t *a, size_t payload) {
    return align_up(a->hdr, a->align) + align_up(payload, a->align);
}
/* 커널이 buflen 바이트 control 버퍼에 cmsg 들을 차례로 넣을 때 "온전히" 들어가는 개수.
 * (Linux put_cmsg 규칙: 남은 자리 ≥ LEN 이면 들어가고, 남은 자리에서 min(SPACE, 남은 자리)를 뺀다.) */
static int cmsg_fit_count(const cmsg_abi_t *a, size_t buflen, const size_t *payloads, int n) {
    size_t left = buflen;
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (left < cmsg_len_abi(a, payloads[i])) break;
        size_t sp = cmsg_space_abi(a, payloads[i]);
        left -= sp < left ? sp : left;
        k++;
    }
    return k;
}

/* 스트림 위 메시지 경계: [4 B 빅엔디언 길이][payload] */
static int send_record(int sock, const void *p, uint32_t len) {
    unsigned char h[4] = { (unsigned char)(len >> 24), (unsigned char)(len >> 16),
                           (unsigned char)(len >> 8), (unsigned char)len };
    if (write_all(sock, h, 4) != 4) return -1;
    if (len > 0 && write_all(sock, p, len) != (ssize_t)len) return -1;
    return 0;
}
/* 반환: 1 = 레코드 하나 받음(*len), 0 = 레코드 경계에서 깨끗한 EOF, -1 = 오류
 * (len > cap 이면 EMSGSIZE, 레코드 중간 EOF 면 EPIPE) */
static int recv_record(int sock, void *buf, uint32_t cap, uint32_t *len) {
    unsigned char h[4];
    ssize_t r = read_all(sock, h, 4);
    if (r == 0) return 0;
    if (r != 4) { if (r >= 0) errno = EPIPE; return -1; }
    uint32_t n = (uint32_t)h[0] << 24 | (uint32_t)h[1] << 16 | (uint32_t)h[2] << 8 | (uint32_t)h[3];
    if (n > cap) { errno = EMSGSIZE; return -1; }
    if (n > 0) {
        r = read_all(sock, buf, n);
        if (r != (ssize_t)n) { if (r >= 0) errno = EPIPE; return -1; }
    }
    *len = n;
    return 1;
}

/* ======================= Part C — 피어 자격 + 접근 제어 ======================= */

typedef struct { uid_t uid; gid_t gid; pid_t pid; int ngroups; } peer_t;

/* 연결된 AF_UNIX SOCK_STREAM 소켓의 상대: getpeereid(uid, gid) + LOCAL_PEERCRED(xucred) + LOCAL_PEERPID.
 * xucred 는 cr_version == XUCRED_VERSION, 길이 == sizeof, cr_uid == getpeereid 의 uid 여야 한다. 실패: -1 */
static int peer_identity(int sock, peer_t *out) {
    uid_t uid;
    gid_t gid;
    if (getpeereid(sock, &uid, &gid) < 0) return -1;
    struct xucred xc;
    socklen_t l = sizeof xc;
    memset(&xc, 0, sizeof xc);
    if (getsockopt(sock, SOL_LOCAL, LOCAL_PEERCRED, &xc, &l) < 0) return -1;
    if (l != sizeof xc || xc.cr_version != XUCRED_VERSION || xc.cr_uid != uid) { errno = EINVAL; return -1; }
    pid_t pid = 0;
    l = sizeof pid;
    if (getsockopt(sock, SOL_LOCAL, LOCAL_PEERPID, &pid, &l) < 0) return -1;
    out->uid = uid;
    out->gid = gid;
    out->pid = pid;
    out->ngroups = xc.cr_ngroups;
    return 0;
}

/* 허용 목록. root(0)는 목록이 아니라 allow_root 로만 허용, (uid_t)-1(모름)은 항상 거부. */
typedef struct { const uid_t *uids; int n; bool allow_root; } acl_t;

static bool allow_peer(const acl_t *acl, uid_t uid) {
    if (uid == (uid_t)-1) return false;
    if (uid == 0) return acl->allow_root;
    for (int i = 0; i < acl->n; i++)
        if (acl->uids[i] == uid) return true;
    return false;
}

/* 브로커: 연결 하나를 accept → 자격 확인 → 허가면 "OK" + dev_fd 전달, 아니면 "NO"(fd 없음) → close.
 * 반환 1 = 허가, 0 = 거부, -1 = 오류. who 가 NULL 이 아니고 자격을 읽었으면 채운다. */
static int serve_one(int lfd, const acl_t *acl, int dev_fd, peer_t *who) {
    int c;
    do c = accept(lfd, NULL, NULL); while (c < 0 && errno == EINTR);
    if (c < 0) return -1;
    peer_t p;
    int rc;
    if (peer_identity(c, &p) == 0) {
        if (who) *who = p;
        if (allow_peer(acl, p.uid)) rc = send_fds(c, &dev_fd, 1, "OK", 2) == 2 ? 1 : -1;
        else                        rc = send_fds(c, NULL, 0, "NO", 2) == 2 ? 0 : -1;
    } else {
        rc = send_fds(c, NULL, 0, "NO", 2) == 2 ? 0 : -1;    /* 자격을 못 읽으면 거부 */
    }
    close(c);
    return rc;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static char g_run[96];       /* /tmp/OS-M16_work/run.<pid> */
static int  g_sock_id;

typedef struct { unsigned char open[256]; } fdset_t;
static void fd_snap(fdset_t *s) { for (int i = 0; i < 256; i++) s->open[i] = fcntl(i, F_GETFD) != -1; }
static int fd_count(void) { int n = 0; for (int i = 0; i < 256; i++) n += fcntl(i, F_GETFD) != -1; return n; }
static int fd_sweep(const fdset_t *s) {      /* 스냅숏 뒤에 새로 생긴 fd 를 닫고 개수를 센다 */
    int n = 0;
    for (int i = 0; i < 256; i++)
        if (!s->open[i] && fcntl(i, F_GETFD) != -1) { close(i); n++; }
    return n;
}

static void set_timeouts(int s, int ms) {
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}
static int sp_stream(int sv[2]) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) return -1;
    set_timeouts(sv[0], 1500); set_timeouts(sv[1], 1500);
    return 0;
}
/* 자식 종료 대기: 5 초 안에 안 끝나면 SIGKILL. 반환: 종료 코드, 시그널이면 128+sig */
static int reap(pid_t pid) {
    int st = 0;
    for (int ms = 0; ms < 5000; ms++) {
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
        struct timespec t = { 0, 1000000 };
        nanosleep(&t, NULL);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &st, 0);
    return 255;
}
static int make_tmpfile(const char *content) {    /* 내용을 쓰고 오프셋 0 으로, 이름은 바로 unlink */
    char path[128];
    snprintf(path, sizeof path, "%s/f.XXXXXX", g_run);
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    unlink(path);
    size_t n = strlen(content);
    if (write_all(fd, content, n) != (ssize_t)n) { close(fd); return -1; }
    lseek(fd, 0, SEEK_SET);
    return fd;
}
static int listen_path(char *path, size_t cap, struct sockaddr_un *sa) {
    snprintf(path, cap, "%s/s%d.sock", g_run, g_sock_id++);
    memset(sa, 0, sizeof *sa);
    sa->sun_family = AF_UNIX;
    if (strlen(path) >= sizeof sa->sun_path) return -1;          /* macOS sun_path = 104 바이트 */
    strcpy(sa->sun_path, path);
    sa->sun_len = (unsigned char)SUN_LEN(sa);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return -1;
    if (bind(s, (struct sockaddr *)sa, sizeof *sa) < 0 || listen(s, 8) < 0) { close(s); return -1; }
    return s;
}
static int connect_path(const struct sockaddr_un *sa) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return -1;
    set_timeouts(s, 1500);
    if (connect(s, (const struct sockaddr *)sa, sizeof *sa) < 0) { close(s); return -1; }
    return s;
}
static bool path_gone(const char *p) { struct stat st; return stat(p, &st) != 0 && errno == ENOENT; }

/* ---------- Part A ---------- */
static void test_pass_file(void) {           /* A1: socketpair + fork, 파일 fd 전달, 오프셋 공유 */
    int sv[2];
    if (sp_stream(sv) < 0) { CHECK(!"socketpair"); return; }
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        alarm(4);
        close(sv[0]);
        char d[4]; int fds[MAX_FDS], n = 0;
        ssize_t r = recv_fds(sv[1], d, sizeof d, fds, MAX_FDS, &n);
        if (r != 1 || d[0] != 'F' || n != 1) _exit(1);
        char got[8] = { 0 };
        if (read_all(fds[0], got, 7) != 7) _exit(2);
        unsigned char cloexec = (fcntl(fds[0], F_GETFD) & FD_CLOEXEC) ? 1 : 0;
        if (write_all(sv[1], got, 7) != 7 || write_all(sv[1], &cloexec, 1) != 1) _exit(3);
        _exit(0);
    }
    close(sv[1]);
    int f = make_tmpfile("HEADERpayload!");     /* fork 뒤에 연다 → 자식은 상속받지 않았다 */
    lseek(f, 6, SEEK_SET);
    ssize_t s = send_fds(sv[0], &f, 1, "F", 1);
    char reply[8] = { 0 }; unsigned char cloexec = 9;
    ssize_t r = read_all(sv[0], reply, 7);
    if (r == 7) (void)read_all(sv[0], &cloexec, 1);
    off_t off = lseek(f, 0, SEEK_CUR);
    int code = reap(pid);
    CHECK(s == 1);
    CHECK(code == 0 && r == 7 && memcmp(reply, "payload", 7) == 0);   /* 자식이 받은 fd 로 읽었다 */
    CHECK(off == 13);                       /* 같은 open file description → 자식이 읽은 만큼 부모 오프셋도 이동 */
    CHECK(cloexec == 1);                    /* recv_fds 가 FD_CLOEXEC 를 켰다 */
    printf("[A1] child read \"%.7s\" through the passed fd; parent offset 6 -> %lld (shared)\n", reply, (long long)off);
    close(f); close(sv[0]);
}

static void test_pass_pipe_end(void) {       /* A2: pipe 쓰기 끝 전달 → 자식만 writer → 부모가 EOF 확인 */
    int sv[2];
    if (sp_stream(sv) < 0) { CHECK(!"socketpair"); return; }
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        alarm(4);
        close(sv[0]);
        char d[4]; int fds[MAX_FDS], n = 0;
        if (recv_fds(sv[1], d, sizeof d, fds, MAX_FDS, &n) != 1 || n != 1) _exit(1);
        if (write_all(fds[0], "via pipe\n", 9) != 9) _exit(2);
        close(fds[0]);
        _exit(0);
    }
    close(sv[1]);
    int p[2];
    if (pipe(p) < 0) { CHECK(!"pipe"); return; }
    ssize_t s = send_fds(sv[0], &p[1], 1, "P", 1);
    close(p[1]);                            /* 부모는 쓰기 끝을 버린다 → 남은 writer 는 자식의 사본뿐 */
    char buf[32] = { 0 };
    ssize_t r = read_all(p[0], buf, sizeof buf - 1);   /* 자식이 닫으면 EOF 로 끝난다 */
    int code = reap(pid);
    CHECK(s == 1 && code == 0);
    CHECK(r == 9 && strcmp(buf, "via pipe\n") == 0);
    close(p[0]); close(sv[0]);
}

static void test_multi_fds(void) {           /* A3: cmsg 하나에 fd 3개 */
    int sv[2];
    if (sp_stream(sv) < 0) { CHECK(!"socketpair"); return; }
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        alarm(4);
        close(sv[0]);
        char d[4]; int fds[MAX_FDS], n = 0;
        ssize_t r = recv_fds(sv[1], d, sizeof d, fds, MAX_FDS, &n);
        unsigned char rep[2] = { (unsigned char)n, 0 };
        if (r == 1 && n == 3) {
            struct stat st;
            if (fstat(fds[0], &st) == 0 && S_ISREG(st.st_mode)) rep[1] |= 1;
            if (fstat(fds[1], &st) == 0 && S_ISFIFO(st.st_mode)) rep[1] |= 2;
            if (fstat(fds[2], &st) == 0 && S_ISCHR(st.st_mode)) rep[1] |= 4;
            if (fds[0] != fds[1] && fds[1] != fds[2] && fds[0] != fds[2]) rep[1] |= 8;
        }
        _exit(write_all(sv[1], rep, 2) == 2 ? 0 : 1);
    }
    close(sv[1]);
    int f = make_tmpfile("x"), p[2], nul = open("/dev/null", O_RDONLY);
    if (pipe(p) < 0) { CHECK(!"pipe"); return; }
    int three[3] = { f, p[1], nul };
    ssize_t s = send_fds(sv[0], three, 3, "M", 1);
    unsigned char rep[2] = { 0, 0 };
    ssize_t r = read_all(sv[0], rep, 2);
    int code = reap(pid);
    CHECK(s == 1 && code == 0 && r == 2);
    CHECK(rep[0] == 3 && rep[1] == 15);    /* 3개, 종류(REG, FIFO, CHR) 그대로, 번호는 서로 다름 */
    close(f); close(p[0]); close(p[1]); close(nul); close(sv[0]);
}

static void test_named_socket(void) {        /* A4: 이름 있는 소켓 — 남남 프로세스처럼 connect 해서 받기 */
    char path[128]; struct sockaddr_un sa;
    int ls = listen_path(path, sizeof path, &sa);
    if (ls < 0) { CHECK(!"listen_path"); return; }
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        alarm(4);
        close(ls);
        int s = connect_path(&sa);
        if (s < 0) _exit(1);
        char d[4]; int fds[MAX_FDS], n = 0;
        if (recv_fds(s, d, sizeof d, fds, MAX_FDS, &n) != 1 || n != 1) _exit(2);
        char got[16] = { 0 };
        ssize_t r = read_all(fds[0], got, 8);
        _exit(r == 8 && memcmp(got, "named-ok", 8) == 0 ? 0 : 3);
    }
    int c = accept(ls, NULL, NULL);
    int f = make_tmpfile("named-ok");
    ssize_t s = -1;
    if (c >= 0) { set_timeouts(c, 1500); s = send_fds(c, &f, 1, "N", 1); close(c); }
    int code = reap(pid);
    close(ls);
    unlink(path);                            /* 소켓 파일은 close 로 지워지지 않는다 — 직접 unlink */
    CHECK(s == 1 && code == 0);
    CHECK(path_gone(path));
    close(f);
}

/* ---------- Part B ---------- */
static void test_cmsg_model(void) {          /* B1: CMSG_LEN / CMSG_SPACE 모델 */
    CHECK(align_up(13, 4) == 16 && align_up(12, 8) == 16 && align_up(16, 8) == 16);   /* 하네스 제공 도우미 */
    bool same = true;
    for (size_t l = 0; l <= 40; l++)
        if (cmsg_len_abi(&ABI_DARWIN, l) != CMSG_LEN(l) || cmsg_space_abi(&ABI_DARWIN, l) != CMSG_SPACE(l)) same = false;
    CHECK(same);                                                     /* 이 맥의 실제 매크로와 일치 */
    CHECK(cmsg_len_abi(&ABI_LINUX64, 4) == 20 && cmsg_space_abi(&ABI_LINUX64, 4) == 24);   /* VM 실측값 */
    CHECK(cmsg_len_abi(&ABI_LINUX64, 12) == 28 && cmsg_space_abi(&ABI_LINUX64, 12) == 32); /* struct ucred */
    CHECK(cmsg_len_abi(&ABI_LINUX64, 8) == 24 && cmsg_space_abi(&ABI_LINUX64, 8) == 24);
    size_t creds_then_fd[2] = { 12, sizeof(int) };     /* Linux: SCM_CREDENTIALS 다음 SCM_RIGHTS(fd 1개) */
    size_t lx_lensum = cmsg_len_abi(&ABI_LINUX64, 12) + cmsg_len_abi(&ABI_LINUX64, 4);      /* 48: 고전 버그 */
    size_t lx_spsum = cmsg_space_abi(&ABI_LINUX64, 12) + cmsg_space_abi(&ABI_LINUX64, 4);   /* 56: 정답 */
    size_t dw_lensum = cmsg_len_abi(&ABI_DARWIN, 12) + cmsg_len_abi(&ABI_DARWIN, 4);
    CHECK(lx_lensum == 48 && lx_spsum == 56);
    CHECK(cmsg_fit_count(&ABI_LINUX64, lx_lensum, creds_then_fd, 2) == 1);   /* 두 번째(fd)가 잘림 → MSG_CTRUNC */
    CHECK(cmsg_fit_count(&ABI_LINUX64, lx_spsum, creds_then_fd, 2) == 2);
    CHECK(cmsg_fit_count(&ABI_DARWIN, dw_lensum, creds_then_fd, 2) == 2);    /* 맥에선 같은 코드가 통과 — 잠복 버그 */
    size_t one_fd[1] = { sizeof(int) };
    CHECK(cmsg_fit_count(&ABI_LINUX64, cmsg_len_abi(&ABI_LINUX64, 4), one_fd, 1) == 1);  /* 마지막 cmsg 는 LEN 이면 충분 */
    size_t three_fds[1] = { 3 * sizeof(int) };
    CHECK(cmsg_fit_count(&ABI_DARWIN, CMSG_SPACE(sizeof(int)), three_fds, 1) == 0);
    printf("[B1] linux-lp64: creds+fd buffer sum-of-LEN %zu B fits %d/2, sum-of-SPACE %zu B fits %d/2; darwin sum-of-LEN %zu B fits %d/2\n",
           lx_lensum, cmsg_fit_count(&ABI_LINUX64, lx_lensum, creds_then_fd, 2), lx_spsum,
           cmsg_fit_count(&ABI_LINUX64, lx_spsum, creds_then_fd, 2), dw_lensum,
           cmsg_fit_count(&ABI_DARWIN, dw_lensum, creds_then_fd, 2));
}

/* 하네스의 "틀린 수신기" 모음 — 재현용. 데이터는 이미 와 있으므로 MSG_DONTWAIT. */
static int naive_trust_cmsg_len(int sock, int *out, int cap) {   /* 버퍼는 fd 1개용인데 cmsg_len 을 믿고 읽는다 */
    union { struct cmsghdr a; char buf[64]; } ctl;
    memset(&ctl, 0, sizeof ctl);                                  /* "지난 메시지" 의 흔적 = 0 */
    char d; struct iovec iov = { &d, 1 };
    struct msghdr m; memset(&m, 0, sizeof m);
    m.msg_iov = &iov; m.msg_iovlen = 1;
    m.msg_control = ctl.buf; m.msg_controllen = CMSG_SPACE(sizeof(int));
    if (recvmsg(sock, &m, MSG_DONTWAIT) < 0) return -1;
    struct cmsghdr *c = CMSG_FIRSTHDR(&m);
    if (c == NULL) return 0;
    int n = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));     /* MSG_CTRUNC 를 안 본다 */
    for (int i = 0; i < n && i < cap; i++) memcpy(&out[i], CMSG_DATA(c) + i * sizeof(int), sizeof(int));
    return n;
}
static int naive_sizeof_int_buf(int sock, int *flags) {        /* control 버퍼를 int 하나 크기로 (헤더를 잊음) */
    int ctl = -1;
    char d; struct iovec iov = { &d, 1 };
    struct msghdr m; memset(&m, 0, sizeof m);
    m.msg_iov = &iov; m.msg_iovlen = 1;
    m.msg_control = &ctl; m.msg_controllen = sizeof ctl;
    if (recvmsg(sock, &m, MSG_DONTWAIT) < 0) return -1;
    *flags = m.msg_flags;
    return CMSG_FIRSTHDR(&m) == NULL ? 0 : 1;                     /* 보이는 cmsg 개수 */
}

static void test_truncation_and_leaks(void) {  /* B2: MSG_CTRUNC, 잘못된 fd, cmsg 무시 = fd 누수 */
    int sv[2], p[2];
    if (sp_stream(sv) < 0 || pipe(p) < 0) { CHECK(!"setup"); return; }
    set_timeouts(sv[1], 300);
    int three[3] = { p[0], p[1], p[0] };
    char d[8]; int fds[MAX_FDS], n = -1;
    fdset_t snap;

    /* (1) 올바른 수신기에 상한(1)보다 많은 fd(3)가 온다 */
    fd_snap(&snap);
    ssize_t s = send_fds(sv[0], three, 3, "T", 1);
    errno = 0;
    ssize_t r = recv_fds(sv[1], d, sizeof d, fds, 1, &n);
    int e = errno;
    int leak1 = fd_sweep(&snap);
    CHECK(s == 1 && r == -1 && e == EMSGSIZE && n == 0);         /* 잘림을 오류로 보고, fd 를 내주지 않는다 */
    CHECK(leak1 == 2);          /* macOS(XNU) 실측: 3개 모두 설치, 1개만 보임 → recv_fds 가 1개를 닫고 2개는 안 보이는 채 남는다 */
    s = send_fds(sv[0], &p[0], 1, "U", 1);                        /* 그 뒤에도 스트림은 정상 */
    r = recv_fds(sv[1], d, sizeof d, fds, MAX_FDS, &n);
    CHECK(s == 1 && r == 1 && d[0] == 'U' && n == 1);
    if (r == 1) for (int i = 0; i < n; i++) close(fds[i]);

    /* (2) cmsg_len 을 믿는 수신기 → 잘린 뒤의 쓰레기(0 = stdin!)를 fd 로 쓴다 */
    fd_snap(&snap);
    s = send_fds(sv[0], three, 3, "W", 1);
    int wrong[4] = { -1, -1, -1, -1 };
    int nw = naive_trust_cmsg_len(sv[1], wrong, 4);
    int leak2 = fd_sweep(&snap);
    CHECK(s == 1 && nw == 3 && wrong[1] == 0 && wrong[2] == 0);  /* "fd 3개 받음" — 그중 2개가 fd 0 */
    CHECK(leak2 == 3);

    /* (3) 버퍼를 sizeof(int) 로 → 헤더도 안 들어가 MSG_CTRUNC, 보이는 fd 0개, 그런데 fd 표에는 1개 */
    fd_snap(&snap);
    s = send_fds(sv[0], &p[0], 1, "S", 1);
    int fl = 0;
    int vis = naive_sizeof_int_buf(sv[1], &fl);
    int leak3 = fd_sweep(&snap);
    CHECK(s == 1 && vis == 0 && (fl & MSG_CTRUNC));
    CHECK(leak3 == 1);

    /* (4) cmsg 를 아예 무시 (recv) → 데이터만 읽었는데 fd 가 설치되어 샌다 */
    fd_snap(&snap);
    s = send_fds(sv[0], &p[0], 1, "I", 1);
    char c = 0;
    ssize_t rr = recv(sv[1], &c, 1, MSG_DONTWAIT);
    int leak4 = fd_sweep(&snap);
    CHECK(s == 1 && rr == 1 && c == 'I' && leak4 == 1);

    /* (5) 대조군: recv_fds 로 받고 닫으면 누수 0 */
    fd_snap(&snap);
    s = send_fds(sv[0], &p[0], 1, "K", 1);
    r = recv_fds(sv[1], d, sizeof d, fds, MAX_FDS, &n);
    if (r == 1) for (int i = 0; i < n; i++) close(fds[i]);
    int leak5 = fd_sweep(&snap);
    CHECK(s == 1 && r == 1 && n == 1 && leak5 == 0);
    printf("[B2] fd-table delta on macOS: truncated(3 into 1)=%d naive-trust-len=%d (fds %d,%d,%d) sizeof-int-buf=%d ignored-cmsg=%d correct=%d\n",
           leak1, leak2, wrong[0], wrong[1], wrong[2], leak3, leak4, leak5);
    close(p[0]); close(p[1]); close(sv[0]); close(sv[1]);
}

static void test_boundaries(void) {          /* B3: STREAM 은 합치고 DGRAM 은 경계를 지킨다 + 프레이밍 */
    static const char msg[60] = "0123456789abcdefghijklmnopqrst!@#$%^&*()ABCDEFGHIJKLMNOPQRS";
    const size_t sz[3] = { 10, 20, 30 };
    char buf[128];
    int sv[2], dv[2];
    if (sp_stream(sv) < 0 || socketpair(AF_UNIX, SOCK_DGRAM, 0, dv) < 0) { CHECK(!"setup"); return; }
    set_timeouts(dv[1], 500);
    size_t off = 0;
    for (int i = 0; i < 3; i++) { send(sv[0], msg + off, sz[i], 0); send(dv[0], msg + off, sz[i], 0); off += sz[i]; }
    ssize_t st = recv(sv[1], buf, sizeof buf, 0);
    ssize_t dg[3];
    for (int i = 0; i < 3; i++) dg[i] = recv(dv[1], buf, sizeof buf, 0);
    CHECK(st == 60);                                  /* 스트림: send 3번이 recv 1번에 합쳐졌다 */
    CHECK(dg[0] == 10 && dg[1] == 20 && dg[2] == 30); /* 데이터그램: send 1번 = recv 1번 */
    send(dv[0], msg, 20, 0);
    struct msghdr m; struct iovec iov = { buf, 5 };
    memset(&m, 0, sizeof m); m.msg_iov = &iov; m.msg_iovlen = 1;
    ssize_t tr = recvmsg(dv[1], &m, 0);
    CHECK(tr == 5 && (m.msg_flags & MSG_TRUNC));      /* 작은 버퍼 → 나머지 15 B 는 버려지고 MSG_TRUNC */
    int q[2];
    errno = 0;
    int sq = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, q);
    CHECK(sq == -1 && errno == EPROTONOSUPPORT);       /* macOS: AF_UNIX SOCK_SEQPACKET 미지원 (리눅스는 지원) */

    /* 길이 프레이밍으로 스트림 위에 경계 복원 */
    off = 0;
    int okw = 0;
    for (int i = 0; i < 3; i++) { okw += send_record(sv[0], msg + off, (uint32_t)sz[i]) == 0; off += sz[i]; }
    shutdown(sv[0], SHUT_WR);
    uint32_t len = 0; int got = 0; bool same = true;
    off = 0;
    for (int i = 0; i < 3; i++) {
        if (recv_record(sv[1], buf, sizeof buf, &len) != 1) { same = false; break; }
        if (len != sz[i] || memcmp(buf, msg + off, len) != 0) same = false;
        off += sz[i]; got++;
    }
    int eof = recv_record(sv[1], buf, sizeof buf, &len);
    CHECK(okw == 3 && got == 3 && same && eof == 0);  /* 레코드 3개 그대로 + 깨끗한 EOF */
    close(sv[0]); close(sv[1]);

    /* 너무 큰 레코드 → EMSGSIZE, 레코드 중간 EOF → EPIPE */
    if (sp_stream(sv) < 0) { CHECK(!"socketpair"); return; }
    int okb = send_record(sv[0], msg, 60) == 0;
    errno = 0;
    int big = recv_record(sv[1], buf, 16, &len);
    int e_big = errno;
    unsigned char half[6] = { 0, 0, 0, 9, 'a', 'b' };    /* 9 바이트라더니 2 바이트 뒤 EOF */
    close(sv[0]); close(sv[1]);
    if (sp_stream(sv) < 0) { CHECK(!"socketpair"); return; }
    write_all(sv[0], half, sizeof half);
    shutdown(sv[0], SHUT_WR);
    errno = 0;
    int cut = recv_record(sv[1], buf, sizeof buf, &len);
    int e_cut = errno;
    CHECK(okb && big == -1 && e_big == EMSGSIZE && cut == -1 && e_cut == EPIPE);
    close(sv[0]); close(sv[1]); close(dv[0]); close(dv[1]);
}

static void test_fd_boundary(void) {         /* B4: fd 가 붙은 바이트는 어느 recvmsg 에 오나 */
    int sv[2], p[2];
    if (sp_stream(sv) < 0 || pipe(p) < 0) { CHECK(!"setup"); return; }
    set_timeouts(sv[1], 300);
    send(sv[0], "abc", 3, 0);
    send(sv[0], "def", 3, 0);
    ssize_t s = send_fds(sv[0], &p[0], 1, "G", 1);
    send(sv[0], "hij", 3, 0);
    char all[16] = { 0 }; size_t tot = 0;
    int calls = 0, fd_calls = 0, fd_call_has_G = 0, sizes[4] = { 0 };
    while (tot < 10 && calls < 4) {
        char d[16]; int fds[MAX_FDS], n = 0;
        ssize_t r = recv_fds(sv[1], d, sizeof d, fds, MAX_FDS, &n);
        if (r <= 0) break;
        sizes[calls++] = (int)r;
        if (n > 0) { fd_calls++; if (memchr(d, 'G', (size_t)r)) fd_call_has_G = 1; for (int i = 0; i < n; i++) close(fds[i]); }
        memcpy(all + tot, d, (size_t)r); tot += (size_t)r;
    }
    CHECK(s == 1 && tot == 10 && memcmp(all, "abcdefGhij", 10) == 0);
    CHECK(fd_calls == 1 && fd_call_has_G == 1);   /* fd 는 'G' 를 담은 recvmsg 에서 정확히 한 번 */
    printf("[B4] stream recvmsg sizes: %d %d %d (fd arrived with the call containing 'G')\n", sizes[0], sizes[1], sizes[2]);
    close(p[0]); close(p[1]); close(sv[0]); close(sv[1]);
}

/* ---------- Part C ---------- */
static void test_allow_peer(void) {          /* C1: 접근 제어 규칙 표 */
    uid_t me = getuid();
    uid_t list[2] = { me, 1234 };
    acl_t acl = { list, 2, false }, acl_root = { list, 2, true }, empty = { NULL, 0, false };
    CHECK(allow_peer(&acl, me) && allow_peer(&acl, 1234));
    CHECK(!allow_peer(&acl, 4321));
    CHECK(!allow_peer(&acl, 0) && allow_peer(&acl_root, 0));          /* root 는 allow_root 로만 */
    CHECK(!allow_peer(&acl, (uid_t)-1) && !allow_peer(&acl_root, (uid_t)-1));   /* 모르는 uid 는 항상 거부 */
    CHECK(!allow_peer(&empty, me));
}

static void test_peer_identity(void) {       /* C2: 양방향 자격 확인 + DGRAM 은 실패 */
    char path[128]; struct sockaddr_un sa;
    int ls = listen_path(path, sizeof path, &sa);
    if (ls < 0) { CHECK(!"listen_path"); return; }
    pid_t server = getpid();
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        alarm(4);
        close(ls);
        int s = connect_path(&sa);
        if (s < 0) _exit(1);
        peer_t p; memset(&p, 0, sizeof p);
        int code = 0;
        if (peer_identity(s, &p) != 0) code |= 2;
        else {
            if (p.uid != getuid()) code |= 4;
            if (p.pid != server) code |= 8;               /* 클라이언트가 본 서버 = listen 한 부모 */
        }
        char go;
        if (read(s, &go, 1) != 1) code |= 16;              /* 부모가 확인을 끝낼 때까지 연결 유지 */
        _exit(code);
    }
    int c = accept(ls, NULL, NULL);
    peer_t p; memset(&p, 0, sizeof p);
    int r = c >= 0 ? peer_identity(c, &p) : -1;
    if (c >= 0) { write_all(c, "g", 1); }
    int code = reap(pid);
    if (c >= 0) close(c);
    close(ls); unlink(path);
    CHECK(r == 0 && p.uid == getuid() && p.gid == getegid());
    CHECK(r == 0 && p.pid == pid && p.ngroups >= 1);
    CHECK(code == 0);
    printf("[C2] server sees peer uid=%d gid=%d pid=%d (child %d) ngroups=%d\n",
           (int)p.uid, (int)p.gid, (int)p.pid, (int)pid, p.ngroups);
    int dv[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, dv) == 0) {
        peer_t q; errno = 0;
        int rd = peer_identity(dv[0], &q);
        CHECK(rd == -1 && errno == EINVAL);           /* getpeereid 는 SOCK_STREAM 전용 */
        close(dv[0]); close(dv[1]);
    }
}

static int broker_client(const struct sockaddr_un *sa, bool expect_ok) {
    int s = connect_path(sa);
    if (s < 0) return 1;
    char d[4] = { 0 }; int fds[MAX_FDS], n = -1;
    ssize_t r = recv_fds(s, d, sizeof d, fds, 1, &n);
    if (expect_ok) {
        if (r != 2 || memcmp(d, "OK", 2) != 0 || n != 1) return 2;
        char cal[16] = { 0 };
        if (read_all(fds[0], cal, 13) != 13 || memcmp(cal, "SENSOR-CAL v3", 13) != 0) return 3;
        return 0;
    }
    if (r != 2 || memcmp(d, "NO", 2) != 0 || n != 0) return 4;
    return 0;
}

static void test_broker(void) {              /* C3: 허가된 uid 에게만 "장치 fd" 를 건넨다 */
    int dev = make_tmpfile("SENSOR-CAL v3");  /* /dev/i2c-1 대신 */
    uid_t me = getuid(), other = getuid() + 1;
    acl_t allow = { &me, 1, false }, deny = { &other, 1, false };
    for (int k = 0; k < 2; k++) {
        char path[128]; struct sockaddr_un sa;
        int ls = listen_path(path, sizeof path, &sa);
        if (ls < 0) { CHECK(!"listen_path"); return; }
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) { alarm(4); close(ls); _exit(broker_client(&sa, k == 0)); }
        peer_t who; memset(&who, 0, sizeof who);
        lseek(dev, 0, SEEK_SET);
        int rc = serve_one(ls, k == 0 ? &allow : &deny, dev, &who);
        close(ls);                            /* 스텁이 accept 를 안 했어도 클라이언트는 연결 끊김을 본다 */
        int code = reap(pid);
        unlink(path);
        if (k == 0) CHECK(rc == 1 && code == 0 && who.pid == pid);   /* 허가: OK + fd, 클라이언트가 내용 확인 */
        else        CHECK(rc == 0 && code == 0 && who.pid == pid);   /* 거부: NO, fd 0개 */
        CHECK(path_gone(path));
    }
    close(dev);
}

static void test_peerpid_is_last_user(void) {   /* C4: LOCAL_PEERPID 는 "connect 한 프로세스" 가 아니다 */
    char path[128]; struct sockaddr_un sa;
    int ls = listen_path(path, sizeof path, &sa);
    int go[2];
    if (ls < 0 || pipe(go) < 0) { CHECK(!"setup"); return; }
    fflush(stdout);
    pid_t C = fork();
    if (C == 0) {                              /* C: connect → 손자 G 를 만들고 G 의 pid 를 알린 뒤 종료 */
        alarm(4);
        close(ls); close(go[1]);
        int s = connect_path(&sa);
        if (s < 0) _exit(1);
        pid_t G = fork();
        if (G == 0) {
            alarm(4);
            char b;
            if (read(go[0], &b, 1) != 1) _exit(1);  /* 부모 신호를 기다렸다가 */
            write_all(s, "g", 1);                    /* 같은 연결로 쓴다 */
            _exit(0);
        }
        write_all(s, &G, sizeof G);
        _exit(G > 0 ? 0 : 2);
    }
    close(go[0]);
    int c = accept(ls, NULL, NULL);
    if (c >= 0) set_timeouts(c, 1500);
    pid_t G = -1; peer_t p1, p2; memset(&p1, 0, sizeof p1); memset(&p2, 0, sizeof p2);
    ssize_t r1 = c >= 0 ? read_all(c, &G, sizeof G) : -1;
    int code = reap(C);                        /* C 는 이미 끝났다 */
    int i1 = c >= 0 ? peer_identity(c, &p1) : -1;
    write_all(go[1], "x", 1);
    char b = 0;
    ssize_t r2 = c >= 0 ? read(c, &b, 1) : -1;
    int i2 = c >= 0 ? peer_identity(c, &p2) : -1;
    close(go[1]);
    if (c >= 0) close(c);
    close(ls); unlink(path);
    CHECK(r1 == (ssize_t)sizeof G && code == 0 && r2 == 1 && b == 'g');
    CHECK(i1 == 0 && p1.pid == C);             /* 아직은 connect 한 C */
    CHECK(i2 == 0 && p2.pid == G && G != C);   /* G 가 쓰고 나면 G — macOS 실측: "마지막으로 쓴 프로세스" */
    printf("[C4] LOCAL_PEERPID: after connect by C=%d -> %d; after grandchild G=%d wrote -> %d\n",
           (int)C, (int)p1.pid, (int)G, (int)p2.pid);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    mkdir("/tmp/OS-M16_work", 0700);
    snprintf(g_run, sizeof g_run, "/tmp/OS-M16_work/run.%d", (int)getpid());
    if (mkdir(g_run, 0700) < 0) { perror("mkdir run dir"); return 1; }
    int base = fd_count();

    test_pass_file();
    test_pass_pipe_end();
    test_multi_fds();
    test_named_socket();
    test_cmsg_model();
    test_truncation_and_leaks();
    test_boundaries();
    test_fd_boundary();
    test_allow_peer();
    test_peer_identity();
    test_broker();
    test_peerpid_is_last_user();

    CHECK(fd_count() == base);                 /* 하네스 전체: fd 누수 없음 */
    int rd = rmdir(g_run);                     /* 소켓·파일이 남았으면 rmdir 이 ENOTEMPTY 로 실패 */
    CHECK(rd == 0);
    rmdir("/tmp/OS-M16_work");                 /* 다른 실행이 쓰는 중이면 실패해도 된다 */
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
