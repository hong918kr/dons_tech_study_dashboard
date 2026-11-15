/* ENV: linux */
/* COMM-M09 demo_tool — demo.sh 가 candump/cansend 와 나란히 돌리는 도구 (solution.c 함수의 축약 복사본)
 *   demo_tool send  <if>               고정 프레임 9개 송신, 두 번째 소켓으로 받아 can_fmt 로 한 줄씩 출력
 *   demo_tool recv  <if> <n> [id:mask] 필터를 걸고 최대 n 개(2 s 무신호면 끝) 받아 출력 ("our" 접두사)
 *   demo_tool errinj <if>              에러 프레임(CAN_ERR_FLAG | CAN_ERR_BUSOFF) 1개 주입 — vcan 실험 전용
 *   demo_tool flood <if> <N> <rcvbuf>  읽지 않는 소켓에 N 개 쏟아붓고 SO_RXQ_OVFL 로 잃은 개수 측정
 * 빌드: gcc -std=c11 -Wall -Wextra -O2 demo_tool.c -o demo_tool
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <net/if.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>

static int can_open(const char *ifname) {
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) return -errno;
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof addr);
    addr.can_family = AF_CAN;
    addr.can_ifindex = (int)if_nametoindex(ifname);
    if (addr.can_ifindex == 0 || bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) { int e = errno; close(fd); return -e; }
    return fd;
}

static int can_fmt(const struct canfd_frame *cf, int mtu, char *out, size_t cap) {
    char tmp[8 + 3 + 2 * CANFD_MAX_DLEN + 1];
    int n;
    canid_t id = cf->can_id;
    bool fd = (mtu == CANFD_MTU);
    if (id & CAN_ERR_FLAG)      n = snprintf(tmp, sizeof tmp, "%08X", (unsigned)(id & (CAN_ERR_MASK | CAN_ERR_FLAG)));
    else if (id & CAN_EFF_FLAG) n = snprintf(tmp, sizeof tmp, "%08X", (unsigned)(id & CAN_EFF_MASK));
    else                        n = snprintf(tmp, sizeof tmp, "%03X", (unsigned)(id & CAN_SFF_MASK));
    if (fd) {
        n += snprintf(tmp + n, sizeof tmp - (size_t)n, "##%X", (unsigned)(cf->flags & 0xF));
    } else {
        tmp[n++] = '#';
        tmp[n] = '\0';
        if (id & CAN_RTR_FLAG) {
            tmp[n++] = 'R';
            if (cf->len > 0 && cf->len <= CAN_MAX_DLEN) n += snprintf(tmp + n, sizeof tmp - (size_t)n, "%X", cf->len);
            tmp[n] = '\0';
            goto copy;
        }
    }
    unsigned maxlen = fd ? CANFD_MAX_DLEN : CAN_MAX_DLEN;
    unsigned len = cf->len < maxlen ? cf->len : maxlen;
    for (unsigned i = 0; i < len; i++) n += snprintf(tmp + n, sizeof tmp - (size_t)n, "%02X", cf->data[i]);
copy:
    if ((size_t)n + 1 > cap) return -ENOSPC;
    memcpy(out, tmp, (size_t)n + 1);
    return n;
}

static int recv_one(int fd, struct canfd_frame *cf, int timeout_ms, uint32_t *drops) {
    struct pollfd p = { .fd = fd, .events = POLLIN };
    int r = poll(&p, 1, timeout_ms);
    if (r <= 0) return r < 0 ? -errno : 0;
    char cbuf[64];
    struct iovec iov = { .iov_base = cf, .iov_len = sizeof *cf };
    struct msghdr mh;
    memset(&mh, 0, sizeof mh);
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    mh.msg_control = cbuf;
    mh.msg_controllen = sizeof cbuf;
    ssize_t n = recvmsg(fd, &mh, MSG_DONTWAIT);
    if (n < 0) return -errno;
    if (drops)
        for (struct cmsghdr *c = CMSG_FIRSTHDR(&mh); c; c = CMSG_NXTHDR(&mh, c))
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SO_RXQ_OVFL) memcpy(drops, CMSG_DATA(c), sizeof *drops);
    return (int)n;
}

static void on(int fd, int level, int opt, int val) {
    if (setsockopt(fd, level, opt, &val, sizeof val) < 0) perror("setsockopt");
}

static int do_send(const char *ifn) {
    int tx = can_open(ifn), rx = can_open(ifn);
    if (tx < 0 || rx < 0) { fprintf(stderr, "open %s: %s\n", ifn, strerror(tx < 0 ? -tx : -rx)); return 1; }
    on(tx, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, 1);
    on(rx, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, 1);
    struct { canid_t id; uint8_t len, flags; int mtu; } fr[9] = {
        { 0x123, 3, 0, CAN_MTU }, { 0x1ABCDEF0u | CAN_EFF_FLAG, 8, 0, CAN_MTU }, { 0x456 | CAN_RTR_FLAG, 0, 0, CAN_MTU },
        { 0x456 | CAN_RTR_FLAG, 2, 0, CAN_MTU }, { 0x7FF, 0, 0, CAN_MTU }, { 0x123 | CAN_EFF_FLAG, 1, 0, CAN_MTU },
        { 0x321, 3, CANFD_BRS, CANFD_MTU }, { 0x321, 64, CANFD_BRS | CANFD_ESI, CANFD_MTU }, { 0x18DAF110u | CAN_EFF_FLAG, 12, 0, CANFD_MTU },
    };
    for (int i = 0; i < 9; i++) {
        struct canfd_frame cf, rcf;
        memset(&cf, 0, sizeof cf);
        cf.can_id = fr[i].id;
        cf.len = fr[i].len;
        cf.flags = fr[i].flags;
        for (int k = 0; k < 64; k++) cf.data[k] = (uint8_t)(0x11 * (k + 1) + i);
        if (write(tx, &cf, (size_t)fr[i].mtu) != fr[i].mtu) { perror("write"); return 1; }
        int r = recv_one(rx, &rcf, 500, NULL);
        char s[160];
        if (r > 0 && can_fmt(&rcf, r, s, sizeof s) > 0) printf("%s\n", s);
        else printf("(no frame)\n");
    }
    return 0;
}

static int do_recv(const char *ifn, int n, const char *filt) {
    int rx = can_open(ifn);
    if (rx < 0) return 1;
    on(rx, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, 1);
    if (filt) {
        struct can_filter f;
        unsigned id, mask;
        if (sscanf(filt, "%x:%x", &id, &mask) != 2) { fprintf(stderr, "filter id:mask\n"); return 1; }
        f.can_id = id; f.can_mask = mask;
        if (setsockopt(rx, SOL_CAN_RAW, CAN_RAW_FILTER, &f, sizeof f) < 0) { perror("filter"); return 1; }
    }
    for (int i = 0; i < n; i++) {
        struct canfd_frame cf;
        int r = recv_one(rx, &cf, 2000, NULL);
        if (r <= 0) break;
        char s[160];
        if (can_fmt(&cf, r, s, sizeof s) > 0) printf("our %s\n", s);
        fflush(stdout);
    }
    return 0;
}

static int do_errinj(const char *ifn) {
    int tx = can_open(ifn);
    if (tx < 0) return 1;
    struct can_frame e;
    memset(&e, 0, sizeof e);
    e.can_id = CAN_ERR_FLAG | CAN_ERR_BUSOFF;
    e.len = CAN_ERR_DLC;
    return write(tx, &e, sizeof e) == (ssize_t)sizeof e ? 0 : 1;
}

static int do_flood(const char *ifn, int N, int rcvbuf) {
    int tx = can_open(ifn), rx = can_open(ifn);
    if (tx < 0 || rx < 0) return 1;
    if (rcvbuf >= 0) on(rx, SOL_SOCKET, SO_RCVBUF, rcvbuf);
    on(rx, SOL_SOCKET, SO_RXQ_OVFL, 1);
    int rb = 0;
    socklen_t sl = sizeof rb;
    getsockopt(rx, SOL_SOCKET, SO_RCVBUF, &rb, &sl);
    int werr = 0, sent = 0;
    for (int i = 0; i < N; i++) {
        struct can_frame cf;
        memset(&cf, 0, sizeof cf);
        cf.can_id = (canid_t)(i & 0x7FF);
        cf.len = 8;
        if (write(tx, &cf, sizeof cf) != (ssize_t)sizeof cf) { werr++; continue; }
        sent++;
    }
    struct canfd_frame cf;
    uint32_t d = 0;
    int got = 0;
    while (recv_one(rx, &cf, 50, &d) > 0) got++;
    struct can_frame m;
    memset(&m, 0, sizeof m);
    m.can_id = 0x7FF;
    if (write(tx, &m, sizeof m) != (ssize_t)sizeof m) werr++;
    uint32_t md = 0;
    int r = recv_one(rx, &cf, 500, &md);
    printf("rcvbuf=%7d  sent=%d write_err=%d  read=%5d  dropcount(marker)=%5u  read+dropped=%d%s\n",
           rb, sent, werr, got, md, got + (int)md, r > 0 ? "" : "  (marker lost)");
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "send")) return do_send(argv[2]);
    if (argc >= 4 && !strcmp(argv[1], "recv")) return do_recv(argv[2], atoi(argv[3]), argc >= 5 ? argv[4] : NULL);
    if (argc >= 3 && !strcmp(argv[1], "errinj")) return do_errinj(argv[2]);
    if (argc >= 5 && !strcmp(argv[1], "flood")) return do_flood(argv[2], atoi(argv[3]), atoi(argv[4]));
    fprintf(stderr, "usage: demo_tool send|recv|errinj|flood ...\n");
    return 2;
}
