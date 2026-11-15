/* OS-M16 demo_cmsg_bugs — macOS 에서 cmsg 버퍼 실수가 실제로 어떻게 보이나 (관찰용, 체크 없음)
 *
 *   1. 이 맥의 CMSG_LEN / CMSG_SPACE 값 (Darwin: 헤더 12 B, 4 B 정렬)
 *   2. 잘린 수신 뒤 fd 표: 어떤 번호가 "보이지 않게" 설치됐나 (번호 + 종류), 수신 소켓을 닫은 뒤에도 남나
 *   3. sendmsg 쪽 사실: msg_controllen 을 버퍼 전체로 / SPACE / LEN, 닫힌 fd, 데이터 0 바이트 + fd
 *   4. 자격 관련 사실: listen 소켓·socketpair·DGRAM 에서 getpeereid / LOCAL_PEERCRED / LOCAL_PEERPID,
 *      sun_path 104 바이트를 NUL 없이 꽉 채운 bind
 *   5. 정렬 안 된 char 배열을 control 버퍼로 쓰기 — 커널은 받아 주지만 cmsghdr 접근이 정렬 위반(UB).
 *      -fsanitize=undefined 로 빌드하면 UBSan 이 그 줄을 짚는다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_cmsg_bugs.c -o bugs && ./bugs
 *        cc -std=c11 -g -O1 -fsanitize=undefined demo_cmsg_bugs.c -o bugs_ub && ./bugs_ub
 */
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ucred.h>
#include <sys/un.h>
#include <unistd.h>

static ssize_t send_fds(int s, const int *fds, int n, char tag) {
    union { struct cmsghdr a; char b[CMSG_SPACE(8 * sizeof(int))]; } u;
    memset(&u, 0, sizeof u);
    struct iovec io = { &tag, 1 };
    struct msghdr m; memset(&m, 0, sizeof m);
    m.msg_iov = &io; m.msg_iovlen = 1;
    m.msg_control = u.b; m.msg_controllen = (socklen_t)CMSG_SPACE((size_t)n * sizeof(int));
    struct cmsghdr *c = CMSG_FIRSTHDR(&m);
    c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = (socklen_t)CMSG_LEN((size_t)n * sizeof(int));
    memcpy(CMSG_DATA(c), fds, (size_t)n * sizeof(int));
    return sendmsg(s, &m, 0);
}
static void list_fds(const char *tag) {
    printf("  %-28s", tag);
    for (int i = 0; i < 32; i++) {
        struct stat st;
        if (fcntl(i, F_GETFD) == -1 || fstat(i, &st) < 0) continue;
        printf(" %d%s", i, S_ISFIFO(st.st_mode) ? "p" : S_ISSOCK(st.st_mode) ? "s" : S_ISCHR(st.st_mode) ? "c" :
                           S_ISREG(st.st_mode) ? "f" : "?");
    }
    printf("\n");
}

int main(void) {
    printf("1) Darwin: sizeof(struct cmsghdr) = %zu\n   payload:   ", sizeof(struct cmsghdr));
    for (size_t l = 0; l <= 16; l += (l < 4 ? 1 : 4)) printf("%4zu", l);
    printf("\n   CMSG_LEN:  ");
    for (size_t l = 0; l <= 16; l += (l < 4 ? 1 : 4)) printf("%4zu", (size_t)CMSG_LEN(l));
    printf("\n   CMSG_SPACE:");
    for (size_t l = 0; l <= 16; l += (l < 4 ? 1 : 4)) printf("%4zu", (size_t)CMSG_SPACE(l));
    printf("\n");

    int sv[2], p[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    pipe(p);
    printf("2) fd table (p=pipe s=socket c=chr f=file):\n");
    list_fds("start");
    int three[3] = { p[0], p[1], p[0] };
    send_fds(sv[0], three, 3, 'T');
    {
        union { struct cmsghdr a; char b[CMSG_SPACE(sizeof(int))]; } u;   /* fd 1개 자리 */
        memset(&u, 0, sizeof u);
        char d; struct iovec io = { &d, 1 };
        struct msghdr m; memset(&m, 0, sizeof m);
        m.msg_iov = &io; m.msg_iovlen = 1; m.msg_control = u.b; m.msg_controllen = sizeof u.b;
        ssize_t r = recvmsg(sv[1], &m, 0);
        struct cmsghdr *c = CMSG_FIRSTHDR(&m);
        int vis = -1;
        if (c) memcpy(&vis, CMSG_DATA(c), sizeof vis);
        printf("  recvmsg r=%zd flags=%#x (MSG_CTRUNC=%#x) controllen=%u cmsg_len=%u visible fd=%d\n",
               r, m.msg_flags, MSG_CTRUNC, (unsigned)m.msg_controllen, c ? (unsigned)c->cmsg_len : 0u, vis);
        list_fds("after truncated recvmsg");
    }
    send_fds(sv[0], &p[0], 1, 'R');
    { char d; ssize_t r = read(sv[1], &d, 1); printf("  read() got %zd byte '%c' and no cmsg\n", r, d); }
    list_fds("after plain read()");
    {
        int sv2[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv2);
        close(sv[1]);                                  /* 받는 소켓을 닫아도 */
        sv[1] = sv2[1]; close(sv[0]); sv[0] = sv2[0];
        list_fds("after closing receiver sock");       /* 설치된 누수 fd 는 남는다 */
    }

    printf("3) sendmsg facts:\n");
    {
        union { struct cmsghdr a; char b[CMSG_SPACE(8 * sizeof(int))]; } u;
        memset(&u, 0, sizeof u);
        char d = 'x'; struct iovec io = { &d, 1 };
        struct msghdr m; memset(&m, 0, sizeof m);
        m.msg_iov = &io; m.msg_iovlen = 1; m.msg_control = u.b;
        struct cmsghdr *c = (struct cmsghdr *)(void *)u.b;
        c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = (socklen_t)CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c), &p[0], sizeof(int));
        socklen_t lens[3] = { (socklen_t)sizeof u.b, (socklen_t)CMSG_SPACE(sizeof(int)), (socklen_t)CMSG_LEN(sizeof(int)) };
        const char *nm[3] = { "whole buffer", "CMSG_SPACE(4)", "CMSG_LEN(4)" };
        for (int i = 0; i < 3; i++) {
            m.msg_controllen = lens[i];
            errno = 0;
            ssize_t r = sendmsg(sv[0], &m, 0);
            printf("  msg_controllen = %-13s (%2u B): sendmsg=%zd errno=%d\n", nm[i], (unsigned)lens[i], r, errno);
        }
        int bad = 1000;
        errno = 0;
        ssize_t r = send_fds(sv[0], &bad, 1, 'b');
        printf("  closed fd number 1000: sendmsg=%zd errno=%d (%s)\n", r, errno, strerror(errno));
        m.msg_iov = NULL; m.msg_iovlen = 0; m.msg_controllen = CMSG_SPACE(sizeof(int));
        errno = 0;
        r = sendmsg(sv[0], &m, 0);
        printf("  STREAM, 0 data bytes + fd: sendmsg=%zd errno=%d (%s)\n", r, errno, strerror(errno));
        char sink[8];
        while (recv(sv[1], sink, sizeof sink, MSG_DONTWAIT) > 0) {}   /* 위에서 보낸 것 비우기 (fd 는 또 샌다 — 무시) */
    }

    printf("4) peer credential facts (sizeof(struct xucred) = %zu):\n", sizeof(struct xucred));
    {
        uid_t u; gid_t g; pid_t pp = 0; socklen_t l = sizeof pp;
        char path[160];
        mkdir("/tmp/OS-M16_work", 0700);
        snprintf(path, sizeof path, "/tmp/OS-M16_work/demo.%d.sock", (int)getpid());
        struct sockaddr_un sa; memset(&sa, 0, sizeof sa);
        sa.sun_family = AF_UNIX; strcpy(sa.sun_path, path);
        int ls = socket(AF_UNIX, SOCK_STREAM, 0);
        bind(ls, (struct sockaddr *)&sa, sizeof sa); listen(ls, 1);
        errno = 0;
        int r = getpeereid(ls, &u, &g);
        printf("  getpeereid(listening socket) = %d errno=%d (%s)\n", r, errno, strerror(errno));
        close(ls); unlink(path);
        getsockopt(sv[0], SOL_LOCAL, LOCAL_PEERPID, &pp, &l);
        printf("  socketpair LOCAL_PEERPID = %d (me %d)\n", (int)pp, (int)getpid());
        int dv[2];
        socketpair(AF_UNIX, SOCK_DGRAM, 0, dv);
        errno = 0; r = getpeereid(dv[0], &u, &g); int e1 = errno;
        struct xucred x; l = sizeof x;
        errno = 0; int r2 = getsockopt(dv[0], SOL_LOCAL, LOCAL_PEERCRED, &x, &l); int e2 = errno;
        l = sizeof pp; pp = 0;
        errno = 0; int r3 = getsockopt(dv[0], SOL_LOCAL, LOCAL_PEERPID, &pp, &l);
        printf("  DGRAM socketpair: getpeereid=%d errno=%d, LOCAL_PEERCRED=%d errno=%d, LOCAL_PEERPID=%d pid=%d\n",
               r, e1, r2, e2, r3, (int)pp);
        close(dv[0]); close(dv[1]);
        struct sockaddr_un full; memset(&full, 0, sizeof full);
        full.sun_family = AF_UNIX;
        int pre = snprintf(full.sun_path, sizeof full.sun_path, "/tmp/OS-M16_work/");
        memset(full.sun_path + pre, 'a', sizeof full.sun_path - (size_t)pre);   /* 104 바이트, NUL 없음 */
        char name[sizeof full.sun_path + 1];
        memcpy(name, full.sun_path, sizeof full.sun_path); name[sizeof full.sun_path] = '\0';
        int fs = socket(AF_UNIX, SOCK_STREAM, 0);
        errno = 0;
        r = bind(fs, (struct sockaddr *)&full, sizeof full);
        struct stat st;
        printf("  bind with 104 non-NUL path bytes = %d errno=%d, file created = %s\n", r, errno,
               stat(name, &st) == 0 ? "yes" : "no");
        close(fs); unlink(name);
        rmdir("/tmp/OS-M16_work");
    }

    printf("5) misaligned control buffer:\n");
    struct { char tag; char buf[CMSG_SPACE(sizeof(int))]; } bad;   /* buf 는 1 바이트 어긋난 주소 */
    memset(&bad, 0, sizeof bad);
    send_fds(sv[0], &p[1], 1, 'M');
    char d; struct iovec io = { &d, 1 };
    struct msghdr m; memset(&m, 0, sizeof m);
    m.msg_iov = &io; m.msg_iovlen = 1; m.msg_control = bad.buf; m.msg_controllen = sizeof bad.buf;
    ssize_t r = recvmsg(sv[1], &m, 0);
    printf("  control buffer at %p (addr %% 4 = %d), recvmsg r=%zd flags=%#x\n",
           (void *)bad.buf, (int)((unsigned long)bad.buf % 4), r, m.msg_flags);
    struct cmsghdr *c = CMSG_FIRSTHDR(&m);                         /* 어긋난 포인터 */
    if (c) {
        unsigned len = c->cmsg_len;                                /* UB: misaligned member access */
        int fd = *(int *)CMSG_DATA(c);                             /* UB: misaligned load */
        printf("  cmsg_len=%u fd=%d (arm64 hardware tolerates this; the C standard does not)\n", len, fd);
    }
    return 0;
}
