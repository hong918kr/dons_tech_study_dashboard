/* ENV: linux */
/* OS-M16 demo_linux — 같은 실험을 리눅스(Lima VM elinux, aarch64)에서: 맥과 무엇이 다른가 (관찰용, 체크 없음)
 *
 *   1. CMSG_LEN / CMSG_SPACE (LP64: 헤더 16 B, 8 B 정렬) — 맥과 달리 LEN != SPACE
 *   2. 잘림·무시: 리눅스는 못 담은 fd 를 닫는다 (fd 표 개수), read() 는 fd 를 버린다
 *   3. 고전 버그: SCM_CREDENTIALS + SCM_RIGHTS 를 sum-of-CMSG_LEN 버퍼로 받기 → MSG_CTRUNC
 *   4. 스트림에서 fd 가 붙은 바이트의 위치
 *   5. SO_PEERCRED: socketpair / DGRAM / connect 한 C 가 손자 G 에게 넘긴 뒤 / 연결 안 된 소켓
 *   6. SCM_CREDENTIALS, SOCK_SEQPACKET, MSG_CMSG_CLOEXEC, 추상 이름공간(abstract namespace)
 *
 * build (VM): gcc -std=c11 -Wall -Wextra -Werror -O2 demo_linux.c -o demo_linux && ./demo_linux
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static int nfds(void) { int n = 0; for (int i = 0; i < 256; i++) n += fcntl(i, F_GETFD) != -1; return n; }

static ssize_t send_fds(int s, const int *fds, int n, const char *d, size_t dl) {
    union { struct cmsghdr h; char b[CMSG_SPACE(16 * sizeof(int))]; } u;
    memset(&u, 0, sizeof u);
    struct iovec io = { (void *)d, dl };
    struct msghdr m; memset(&m, 0, sizeof m);
    m.msg_iov = dl ? &io : NULL; m.msg_iovlen = dl ? 1 : 0;
    if (n) {
        m.msg_control = u.b; m.msg_controllen = CMSG_SPACE(n * sizeof(int));
        struct cmsghdr *c = CMSG_FIRSTHDR(&m);
        c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(n * sizeof(int));
        memcpy(CMSG_DATA(c), fds, n * sizeof(int));
    }
    return sendmsg(s, &m, 0);
}
static void recv_dump(int s, size_t clen, const char *tag) {
    union { struct cmsghdr h; char b[256]; } u;
    memset(&u, 0, sizeof u);
    char d[100];
    struct iovec io = { d, sizeof d };
    struct msghdr m; memset(&m, 0, sizeof m);
    m.msg_iov = &io; m.msg_iovlen = 1; m.msg_control = clen ? u.b : NULL; m.msg_controllen = clen;
    int before = nfds();
    ssize_t r = recvmsg(s, &m, 0);
    int after = nfds();
    printf("  %-34s r=%zd data=\"%.*s\" flags=%#x controllen=%zu fds %d->%d", tag, r, r > 0 ? (int)r : 0, d,
           m.msg_flags, (size_t)m.msg_controllen, before, after);
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c))
        printf(" [cmsg len=%zu %s]", (size_t)c->cmsg_len,
               c->cmsg_type == SCM_RIGHTS ? "RIGHTS" : c->cmsg_type == SCM_CREDENTIALS ? "CREDS" : "?");
    printf("\n");
}
static void close_from(int lo) { for (int i = lo; i < 256; i++) if (fcntl(i, F_GETFD) != -1) close(i); }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("1) sizeof(struct cmsghdr)=%zu  CMSG_LEN(4)=%zu SPACE(4)=%zu  LEN(8)=%zu SPACE(8)=%zu  LEN(ucred)=%zu SPACE(ucred)=%zu\n",
           sizeof(struct cmsghdr), CMSG_LEN(4), CMSG_SPACE(4), CMSG_LEN(8), CMSG_SPACE(8),
           CMSG_LEN(sizeof(struct ucred)), CMSG_SPACE(sizeof(struct ucred)));

    int sv[2], p[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    if (pipe(p) < 0) return 1;
    int base = sv[1] > p[1] ? sv[1] : p[1];
    int fds[3] = { p[0], p[1], p[0] };
    printf("2) truncation / ignoring cmsg (MSG_CTRUNC=%#x on Linux):\n", MSG_CTRUNC);
    send_fds(sv[0], fds, 3, "T", 1); recv_dump(sv[1], CMSG_SPACE(sizeof(int)), "3 fds into CMSG_SPACE(4)=24 B buf");
    close_from(base + 1);
    send_fds(sv[0], fds, 1, "S", 1); recv_dump(sv[1], sizeof(int), "1 fd into sizeof(int) buf");
    send_fds(sv[0], fds, 1, "N", 1); recv_dump(sv[1], 0, "1 fd, recvmsg without control");
    send_fds(sv[0], fds, 1, "R", 1);
    { int b = nfds(); char c; ssize_t r = read(sv[1], &c, 1); printf("  %-34s r=%zd fds %d->%d\n", "1 fd, plain read()", r, b, nfds()); }
    send_fds(sv[0], fds, 1, "L", 1); recv_dump(sv[1], CMSG_LEN(sizeof(int)), "1 fd into CMSG_LEN(4)=20 B buf");
    close_from(base + 1);
    errno = 0;
    ssize_t z = send_fds(sv[0], fds, 1, "", 0);
    printf("  stream sendmsg with 0 data bytes + fd: r=%zd errno=%d\n", z, errno);

    printf("3) two cmsgs (SO_PASSCRED on + 1 fd):\n");
    int on = 1;
    setsockopt(sv[1], SOL_SOCKET, SO_PASSCRED, &on, sizeof on);
    send_fds(sv[0], fds, 1, "a", 1);
    recv_dump(sv[1], CMSG_LEN(sizeof(struct ucred)) + CMSG_LEN(sizeof(int)), "sum of CMSG_LEN = 48 B");
    send_fds(sv[0], fds, 1, "b", 1);
    recv_dump(sv[1], CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(sizeof(int)), "sum of CMSG_SPACE = 56 B");
    close_from(base + 1);
    on = 0;
    setsockopt(sv[1], SOL_SOCKET, SO_PASSCRED, &on, sizeof on);

    printf("4) fd position in a stream: send abc, def, G+fd, hij\n");
    send(sv[0], "abc", 3, 0); send(sv[0], "def", 3, 0); send_fds(sv[0], fds, 1, "G", 1); send(sv[0], "hij", 3, 0);
    recv_dump(sv[1], CMSG_SPACE(4 * sizeof(int)), "recvmsg #1");
    recv_dump(sv[1], CMSG_SPACE(4 * sizeof(int)), "recvmsg #2");
    close_from(base + 1);

    printf("5) SO_PEERCRED (me: pid=%d uid=%d):\n", (int)getpid(), (int)getuid());
    struct ucred uc; socklen_t l = sizeof uc;
    getsockopt(sv[0], SOL_SOCKET, SO_PEERCRED, &uc, &l);
    printf("  stream socketpair: pid=%d uid=%d gid=%d\n", (int)uc.pid, (int)uc.uid, (int)uc.gid);
    int dv[2];
    socketpair(AF_UNIX, SOCK_DGRAM, 0, dv);
    l = sizeof uc; errno = 0;
    int r = getsockopt(dv[0], SOL_SOCKET, SO_PEERCRED, &uc, &l);
    printf("  dgram socketpair: r=%d pid=%d\n", r, (int)uc.pid);
    char path[108];
    snprintf(path, sizeof path, "/tmp/OS-M16/s.%d", (int)getpid());
    struct sockaddr_un a; memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX; strcpy(a.sun_path, path);
    int ls = socket(AF_UNIX, SOCK_STREAM, 0);
    bind(ls, (struct sockaddr *)&a, sizeof a); listen(ls, 4);
    int sy[2];
    if (pipe(sy) < 0) return 1;
    pid_t C = fork();
    if (C == 0) {
        int s = socket(AF_UNIX, SOCK_STREAM, 0);
        if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) _exit(1);
        pid_t G = fork();
        if (G == 0) { char c; if (read(sy[0], &c, 1) == 1 && write(s, "G", 1) == 1) _exit(0); _exit(1); }
        if (write(s, &G, sizeof G) != sizeof G) _exit(1);
        _exit(0);
    }
    int cs = accept(ls, NULL, NULL);
    unlink(path);
    pid_t G = 0;
    if (read(cs, &G, sizeof G) != sizeof G) return 1;
    waitpid(C, NULL, 0);
    l = sizeof uc; getsockopt(cs, SOL_SOCKET, SO_PEERCRED, &uc, &l);
    printf("  named, connector C=%d exited: pid=%d\n", (int)C, (int)uc.pid);
    if (write(sy[1], "x", 1) != 1) return 1;
    char b;
    if (read(cs, &b, 1) != 1) return 1;
    l = sizeof uc; getsockopt(cs, SOL_SOCKET, SO_PEERCRED, &uc, &l);
    printf("  after grandchild G=%d wrote '%c':  pid=%d   (macOS LOCAL_PEERPID would say G)\n", (int)G, b, (int)uc.pid);
    { int u0 = socket(AF_UNIX, SOCK_STREAM, 0); l = sizeof uc; errno = 0;
      r = getsockopt(u0, SOL_SOCKET, SO_PEERCRED, &uc, &l);
      printf("  unconnected socket: r=%d pid=%d uid=%d  (no error!)\n", r, (int)uc.pid, (int)uc.uid); close(u0); }

    printf("6) misc:\n");
    on = 1;
    setsockopt(sv[1], SOL_SOCKET, SO_PASSCRED, &on, sizeof on);
    send(sv[0], "c", 1, 0);
    {
        union { struct cmsghdr h; char b[CMSG_SPACE(sizeof(struct ucred))]; } u;
        char d; struct iovec io = { &d, 1 };
        struct msghdr m; memset(&m, 0, sizeof m);
        m.msg_iov = &io; m.msg_iovlen = 1; m.msg_control = u.b; m.msg_controllen = sizeof u.b;
        if (recvmsg(sv[1], &m, 0) == 1 && CMSG_FIRSTHDR(&m)) {
            struct ucred x; memcpy(&x, CMSG_DATA(CMSG_FIRSTHDR(&m)), sizeof x);
            printf("  SCM_CREDENTIALS without sender doing anything: pid=%d uid=%d gid=%d\n", (int)x.pid, (int)x.uid, (int)x.gid);
        }
    }
    on = 0;
    setsockopt(sv[1], SOL_SOCKET, SO_PASSCRED, &on, sizeof on);
    send_fds(sv[0], fds, 1, "x", 1);
    {
        union { struct cmsghdr h; char b[CMSG_SPACE(sizeof(int))]; } u;
        char d; struct iovec io = { &d, 1 };
        struct msghdr m; memset(&m, 0, sizeof m);
        m.msg_iov = &io; m.msg_iovlen = 1; m.msg_control = u.b; m.msg_controllen = sizeof u.b;
        if (recvmsg(sv[1], &m, MSG_CMSG_CLOEXEC) == 1 && CMSG_FIRSTHDR(&m)) {
            int f; memcpy(&f, CMSG_DATA(CMSG_FIRSTHDR(&m)), sizeof f);
            printf("  MSG_CMSG_CLOEXEC: received fd %d has FD_CLOEXEC=%d\n", f, fcntl(f, F_GETFD) & FD_CLOEXEC);
            close(f);
        }
    }
    int q[2];
    r = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, q);
    send(q[0], "0123456789", 10, 0); send(q[0], "abcdefghijklmnopqrst", 20, 0);
    char bb[100];
    ssize_t r1 = recv(q[1], bb, sizeof bb, 0);
    struct msghdr m; struct iovec io = { bb, 5 };
    memset(&m, 0, sizeof m); m.msg_iov = &io; m.msg_iovlen = 1;
    ssize_t r2 = recvmsg(q[1], &m, 0);
    printf("  SOCK_SEQPACKET socketpair r=%d: recv -> %zd, recv(5) of 20 B -> %zd flags=%#x (MSG_TRUNC=%#x)\n",
           r, r1, r2, m.msg_flags, MSG_TRUNC);
    {
        struct sockaddr_un ab; memset(&ab, 0, sizeof ab);
        ab.sun_family = AF_UNIX;
        int nl = snprintf(ab.sun_path + 1, sizeof ab.sun_path - 1, "OS-M16.%d", (int)getpid());   /* sun_path[0] = '\0' */
        socklen_t al = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + (size_t)nl);
        int as = socket(AF_UNIX, SOCK_STREAM, 0);
        int rb = bind(as, (struct sockaddr *)&ab, al);
        int cl = socket(AF_UNIX, SOCK_STREAM, 0);
        listen(as, 1);
        int rc = connect(cl, (struct sockaddr *)&ab, al);
        printf("  abstract socket \"@%s\": bind=%d connect=%d, file in /tmp? none — vanishes with the last fd\n", ab.sun_path + 1, rb, rc);
        close(cl); close(as);
    }
    while (wait(NULL) > 0) {}
    return 0;
}
