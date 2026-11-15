/* ENV: linux */
/* COMM-M09 SocketCAN 실습: vcan, candump, C 소켓 프로그램
 *
 * 리눅스 CAN 스택(SocketCAN)의 CAN_RAW 소켓을 진짜 커널 위에서 다룬다. 한 프로세스 안에 소켓 여러 개를 열어
 * 같은 vcan 인터페이스에 bind 하고, 한쪽이 쓰고 다른 쪽이 읽는 자체 테스트다 (vcan 의 "버스" = 로컬 loopback).
 *
 * Part A: can_open (socket + bind), can_send (struct can_frame 송신), can_recv (poll + recvmsg, 타임아웃)
 * Part B: can_set_filters (CAN_RAW_FILTER), can_set_err_mask (CAN_RAW_ERR_FILTER),
 *         filter_match (커널의 can_id/can_mask 규칙을 사용자 공간에서 재현 → 실제 커널 전달과 랜덤 대조)
 * Part C: can_enable_fd (CAN_RAW_FD_FRAMES), can_send_fd (struct canfd_frame 72 B),
 *         can_fmt (candump -L / cansend 표기: 123#112233, 1ABCDEF0#.., 123#R, 321##5..)
 *
 * 준비 (check.sh 가 해 준다): vcan0 = mtu 72 (CAN FD 가능), vcan1 = mtu 16 (클래식 전용), 둘 다 up.
 * 빌드: gcc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution [vcan0] [vcan1]
 * 맥에서는 빌드되지 않는다 (linux/can.h 없음) → Lima VM 에서 check.sh 로 돌린다.
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <net/if.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>

#define T_POS 500 /* ms: 와야 하는 프레임을 기다리는 최대 시간 */
#define T_NEG 30  /* ms: 오면 안 되는 프레임 — 이 시간 동안 안 오면 "안 왔다" */

/* ---------------- Part A: 소켓 열기 · 송신 · 수신 ---------------- */

/* CAN_RAW 소켓을 만들어 ifname 에 bind. ifname 이 NULL 또는 "" 이면 ifindex 0 = 모든 CAN 인터페이스.
 * 반환: fd (>= 0) 또는 -errno (없는 인터페이스 → -ENODEV). 실패 시 fd 를 새지 않는다. */
static int can_open(const char *ifname) {
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) return -errno;
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof addr);
    addr.can_family = AF_CAN;
    if (ifname && ifname[0]) {
        unsigned idx = if_nametoindex(ifname);
        if (idx == 0) { int e = errno; close(fd); return -e; }
        addr.can_ifindex = (int)idx;
    }
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) { int e = errno; close(fd); return -e; }
    return fd;
}

/* 클래식 프레임 1개 송신. id 에 CAN_EFF_FLAG / CAN_RTR_FLAG 를 그대로 싣는다. data 는 NULL 가능.
 * len 검증은 커널 몫(> 8 → EINVAL)이지만, 내 버퍼(data[8]) 경계는 내가 지킨다.
 * 반환: 0 또는 -errno (짧은 쓰기 → -EIO). */
static int can_send(int fd, canid_t id, const uint8_t *data, uint8_t len) {
    struct can_frame cf;
    memset(&cf, 0, sizeof cf);                 /* __pad, __res0, len8_dlc 를 0 으로 */
    cf.can_id = id;
    cf.len = len;
    if (data) memcpy(cf.data, data, len < CAN_MAX_DLEN ? len : CAN_MAX_DLEN);
    ssize_t n = write(fd, &cf, sizeof cf);
    if (n < 0) return -errno;
    return n == (ssize_t)sizeof cf ? 0 : -EIO;
}

/* 프레임 1개 수신, 최대 timeout_ms 대기. cf 는 72 B 버퍼(클래식 프레임은 앞 16 B 에 같은 배치로 들어온다).
 * 반환: CAN_MTU(16) = 클래식, CANFD_MTU(72) = FD, 0 = 타임아웃, -errno.
 * msg_flags != NULL 이면 recvmsg 의 msg_flags (MSG_DONTROUTE = 이 호스트가 보낸 것, MSG_CONFIRM = 이 소켓이 보낸 것). */
static int can_recv(int fd, struct canfd_frame *cf, int timeout_ms, int *msg_flags) {
    struct pollfd p = { .fd = fd, .events = POLLIN };
    int r = poll(&p, 1, timeout_ms);
    if (r < 0) return -errno;
    if (r == 0) return 0;
    if (!(p.revents & POLLIN)) return -EIO;    /* POLLERR / POLLNVAL */
    struct iovec iov = { .iov_base = cf, .iov_len = sizeof *cf };
    struct msghdr mh;
    memset(&mh, 0, sizeof mh);
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    ssize_t n = recvmsg(fd, &mh, MSG_DONTWAIT);
    if (n < 0) return -errno;
    if (msg_flags) *msg_flags = mh.msg_flags;
    return (int)n;
}

/* ---------------- Part B: 필터 · 에러 프레임 ---------------- */

/* 수신 필터 n 개 설치 (여러 개 = OR). n == 0 → 필터 없음 = 아무것도 받지 않음. 반환 0 또는 -errno. */
static int can_set_filters(int fd, const struct can_filter *f, size_t n) {
    if (setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FILTER, n ? f : NULL, (socklen_t)(n * sizeof *f)) < 0) return -errno;
    return 0;
}

/* 에러 프레임 구독 마스크 (CAN_ERR_TX_TIMEOUT | CAN_ERR_BUSOFF ...). 0 = 안 받음(기본). */
static int can_set_err_mask(int fd, can_err_mask_t m) {
    if (setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &m, sizeof m) < 0) return -errno;
    return 0;
}

/* 커널(af_can.c)의 데이터 프레임 필터 규칙을 사용자 공간에서 재현한다 (에러 프레임·마스크의 bit29 는 모델 밖).
 *   match  ⇔  (rx_id & mask) == (filter_id & mask),  filter_id 에 CAN_INV_FILTER 가 있으면 결과 반전.
 *   단 mask 에 EFF 플래그가 있고 filter_id 에 없으면("SFF 만") mask 를 SFF_MASK|EFF|RTR 로 줄인다. */
static bool filter_match(canid_t rx_id, const struct can_filter *f) {
    canid_t id = f->can_id, mask = f->can_mask;
    bool inv = (id & CAN_INV_FILTER) != 0;
    if ((mask & CAN_EFF_FLAG) && !(id & CAN_EFF_FLAG))
        mask &= CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG;
    id &= mask;
    bool eq = (rx_id & mask) == id;
    return inv ? !eq : eq;
}

static bool filters_accept(canid_t rx_id, const struct can_filter *f, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (filter_match(rx_id, &f[i])) return true;   /* OR — 겹쳐도 한 번만 전달된다 */
    return false;
}

/* ---------------- Part C: CAN FD · candump 표기 ---------------- */

/* 이 소켓에서 CAN FD 프레임(72 B) 송수신을 켠다. 반환 0 또는 -errno. */
static int can_enable_fd(int fd) {
    int on = 1;
    if (setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &on, sizeof on) < 0) return -errno;
    return 0;
}

/* CAN FD 프레임 1개 송신 (len 0..64, flags = CANFD_BRS | CANFD_ESI). 반환 0 또는 -errno.
 * 소켓에 FD 가 꺼져 있거나 인터페이스 MTU 가 16 이면 커널이 EINVAL. */
static int can_send_fd(int fd, canid_t id, const uint8_t *data, uint8_t len, uint8_t flags) {
    struct canfd_frame cf;
    memset(&cf, 0, sizeof cf);
    cf.can_id = id;
    cf.len = len;
    cf.flags = flags;
    if (data) memcpy(cf.data, data, len < CANFD_MAX_DLEN ? len : CANFD_MAX_DLEN);
    ssize_t n = write(fd, &cf, sizeof cf);
    if (n < 0) return -errno;
    return n == (ssize_t)sizeof cf ? 0 : -EIO;
}

/* candump -L / cansend 의 한 줄 표기 (대문자 hex, 구분자 없음):
 *   SFF "123#112233"   EFF "1ABCDEF0#0102"   RTR "456#R" (len>0 이면 "456#R2")
 *   FD  "321##<flags & 0xF 한 자리><data>"     에러 프레임 "20000040#00..." (8자리, ERR_FLAG 포함)
 * mtu = can_recv 의 반환값(16/72). 반환: 쓴 글자 수(NUL 제외) 또는 cap 부족 시 -ENOSPC (out 은 손대지 않음). */
static int can_fmt(const struct canfd_frame *cf, int mtu, char *out, size_t cap) {
    char tmp[8 + 3 + 2 * CANFD_MAX_DLEN + 1];  /* id 8 + "##F" + 128 + NUL = 140 */
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

/* ---------------- 하네스 (주어짐) ---------------- */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static int setopt_int(int fd, int opt, int val) {
    return setsockopt(fd, SOL_CAN_RAW, opt, &val, sizeof val) < 0 ? -errno : 0;
}

/* 인터페이스 MTU (16 = 클래식 전용, 72 = CAN FD, 2060 = CAN XL 까지). 실패 시 -errno. cansend 도 이렇게 확인한다. */
static int can_if_mtu(int fd, const char *ifname) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", ifname);
    if (ioctl(fd, SIOCGIFMTU, &ifr) < 0) return -errno;
    return ifr.ifr_mtu;
}

/* 남은 프레임을 버리고 개수를 센다 (최대 max 개, 각 T_NEG 대기). */
static int drain(int fd, int max) {
    struct canfd_frame cf;
    int k = 0;
    while (k < max && can_recv(fd, &cf, T_NEG, NULL) > 0) k++;
    return k;
}

/* fd 로 들어오는 다음 프레임의 can_id (타임아웃이면 0xFFFFFFFF) */
static canid_t next_id(int fd, int timeout_ms) {
    struct canfd_frame cf;
    return can_recv(fd, &cf, timeout_ms, NULL) > 0 ? cf.can_id : 0xFFFFFFFFu;
}

static void close_all(int *fds, int n) {
    for (int i = 0; i < n; i++) if (fds[i] >= 0) close(fds[i]);
}

/* ---------------- 테스트 ---------------- */

static void test_layout(void) {
    CHECK(sizeof(struct can_frame) == CAN_MTU && CAN_MTU == 16);
    CHECK(sizeof(struct canfd_frame) == CANFD_MTU && CANFD_MTU == 72);
    CHECK(offsetof(struct can_frame, data) == 8 && offsetof(struct canfd_frame, data) == 8);
    CHECK(offsetof(struct can_frame, len) == offsetof(struct canfd_frame, len));  /* 클래식 = FD 의 앞 16 B */
}

static void test_part_a(const char *if0) {
    int tx = can_open(if0), rx = can_open(if0);
    CHECK(tx >= 0 && rx >= 0);
    CHECK(can_open("nosuch9") == -ENODEV);

    struct canfd_frame cf;
    memset(&cf, 0, sizeof cf);                 /* 스텁이 아무것도 안 써도 UB 없이 FAIL 하도록 */
    int mf = 0, r;
    const uint8_t d3[3] = { 0x11, 0x22, 0x33 };
    CHECK(can_send(tx, 0x123, d3, 3) == 0);
    r = can_recv(rx, &cf, T_POS, &mf);
    CHECK(r == CAN_MTU && cf.can_id == 0x123 && cf.len == 3 && memcmp(cf.data, d3, 3) == 0);
    CHECK((mf & MSG_DONTROUTE) && !(mf & MSG_CONFIRM));      /* 이 호스트가 보냄, 내 소켓은 아님 */
    CHECK(can_recv(tx, &cf, T_NEG, NULL) == 0);               /* RECV_OWN_MSGS 기본 off → 자기 프레임 안 봄 */

    const uint8_t d8[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    CHECK(can_send(tx, 0x1ABCDEF0u | CAN_EFF_FLAG, d8, 8) == 0);
    r = can_recv(rx, &cf, T_POS, NULL);
    CHECK(r == CAN_MTU && cf.can_id == (0x1ABCDEF0u | CAN_EFF_FLAG) && cf.len == 8 && memcmp(cf.data, d8, 8) == 0);

    CHECK(can_send(tx, 0x456 | CAN_RTR_FLAG, NULL, 2) == 0);
    r = can_recv(rx, &cf, T_POS, NULL);
    CHECK(r == CAN_MTU && cf.can_id == (0x456u | CAN_RTR_FLAG) && cf.len == 2);

    int ok = 0;
    for (uint8_t len = 0; len <= 8; len++) {
        uint8_t d[8];
        for (int i = 0; i < 8; i++) d[i] = (uint8_t)(0xA0 + len + i);
        if (can_send(tx, 0x200u + len, d, len) != 0) continue;
        if (can_recv(rx, &cf, T_POS, NULL) == CAN_MTU && cf.can_id == 0x200u + len && cf.len == len &&
            memcmp(cf.data, d, len) == 0) ok++;
    }
    CHECK(ok == 9);

    CHECK(can_send(tx, 0x123, d8, 9) == -EINVAL);             /* len 9: 클래식 프레임 아님 */
    CHECK(can_recv(rx, &cf, T_NEG, NULL) == 0);

    int inorder = 0;                                          /* 100 개 연속: 순서·내용 그대로 */
    for (int i = 0; i < 100; i++) { uint8_t b = (uint8_t)i; if (can_send(tx, (canid_t)i, &b, 1) != 0) break; }
    for (int i = 0; i < 100; i++)
        if (can_recv(rx, &cf, T_POS, NULL) == CAN_MTU && cf.can_id == (canid_t)i && cf.data[0] == (uint8_t)i) inorder++;
    CHECK(inorder == 100);

    CHECK(setopt_int(tx, CAN_RAW_RECV_OWN_MSGS, 1) == 0);
    CHECK(can_send(tx, 0x055, NULL, 0) == 0);
    r = can_recv(tx, &cf, T_POS, &mf);
    CHECK(r == CAN_MTU && cf.can_id == 0x055 && (mf & MSG_CONFIRM));  /* 자기 프레임 = MSG_CONFIRM */
    r = can_recv(rx, &cf, T_POS, &mf);
    CHECK(r == CAN_MTU && cf.can_id == 0x055 && !(mf & MSG_CONFIRM));
    CHECK(setopt_int(tx, CAN_RAW_RECV_OWN_MSGS, 0) == 0);

    CHECK(setopt_int(tx, CAN_RAW_LOOPBACK, 0) == 0);          /* vcan: loopback 끄면 아무도 못 듣는다 */
    CHECK(can_send(tx, 0x066, NULL, 0) == 0);
    CHECK(can_recv(rx, &cf, T_NEG, NULL) == 0);
    CHECK(setopt_int(tx, CAN_RAW_LOOPBACK, 1) == 0);

    int fds[2] = { tx, rx };
    close_all(fds, 2);
}

static void test_part_b(const char *if0) {
    int tx = can_open(if0), rx = can_open(if0), all = can_open(if0);
    CHECK(tx >= 0 && rx >= 0 && all >= 0);

    /* B1: SFF 0x123 만 원했는데 mask 에 EFF 플래그가 없으면 EFF 0x00000123 도 통과 (면접 단골) */
    struct can_filter f1 = { 0x123, CAN_SFF_MASK };
    CHECK(can_set_filters(rx, &f1, 1) == 0);
    can_send(tx, 0x123, NULL, 0);
    can_send(tx, 0x124, NULL, 0);
    can_send(tx, 0x123 | CAN_EFF_FLAG, NULL, 0);
    can_send(tx, 0x123 | CAN_RTR_FLAG, NULL, 0);
    CHECK(next_id(rx, T_POS) == 0x123);
    CHECK(next_id(rx, T_POS) == (0x123u | CAN_EFF_FLAG));
    CHECK(next_id(rx, T_POS) == (0x123u | CAN_RTR_FLAG));
    CHECK(next_id(rx, T_NEG) == 0xFFFFFFFFu);
    CHECK(filter_match(0x123, &f1) && !filter_match(0x124, &f1) && filter_match(0x123 | CAN_EFF_FLAG, &f1));

    /* B2: SFF 데이터 프레임 0x123 "만" = mask 에 EFF·RTR 플래그까지 */
    struct can_filter f2 = { 0x123, CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG };
    CHECK(can_set_filters(rx, &f2, 1) == 0);
    can_send(tx, 0x123 | CAN_EFF_FLAG, NULL, 0);
    can_send(tx, 0x123 | CAN_RTR_FLAG, NULL, 0);
    can_send(tx, 0x123, NULL, 0);
    CHECK(next_id(rx, T_POS) == 0x123);
    CHECK(next_id(rx, T_NEG) == 0xFFFFFFFFu);
    CHECK(!filter_match(0x123 | CAN_EFF_FLAG, &f2) && !filter_match(0x123 | CAN_RTR_FLAG, &f2) && filter_match(0x123, &f2));

    /* B2b: 커널의 "SFF 전용" 보정 — mask 에 EFF 플래그가 있고 id 에 없으면 mask 상위 18비트를 버린다 */
    struct can_filter f2b = { 0x1123, CAN_EFF_MASK | CAN_EFF_FLAG };
    CHECK(can_set_filters(rx, &f2b, 1) == 0);
    can_send(tx, 0x123, NULL, 0);
    can_send(tx, 0x1123 | CAN_EFF_FLAG, NULL, 0);
    CHECK(next_id(rx, T_POS) == 0x123 && next_id(rx, T_NEG) == 0xFFFFFFFFu);
    CHECK(filter_match(0x123, &f2b) && !filter_match(0x1123 | CAN_EFF_FLAG, &f2b));

    /* B3: 범위 0x100..0x1FF = {0x100, 0x700};  B4: 반전 = 0x123 SFF 데이터만 빼고 전부 */
    struct can_filter f3 = { 0x100, 0x700 };
    CHECK(can_set_filters(rx, &f3, 1) == 0);
    canid_t ids3[4] = { 0x0FF, 0x100, 0x1FF, 0x200 };
    for (int i = 0; i < 4; i++) can_send(tx, ids3[i], NULL, 0);
    CHECK(next_id(rx, T_POS) == 0x100 && next_id(rx, T_POS) == 0x1FF && next_id(rx, T_NEG) == 0xFFFFFFFFu);
    struct can_filter f4 = { 0x123 | CAN_INV_FILTER, CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG };
    CHECK(can_set_filters(rx, &f4, 1) == 0);
    can_send(tx, 0x123, NULL, 0);
    can_send(tx, 0x124, NULL, 0);
    can_send(tx, 0x123 | CAN_EFF_FLAG, NULL, 0);
    CHECK(next_id(rx, T_POS) == 0x124 && next_id(rx, T_POS) == (0x123u | CAN_EFF_FLAG) && next_id(rx, T_NEG) == 0xFFFFFFFFu);

    /* B5: 여러 필터 = OR, 겹치는 프레임도 1번만;  B6: 빈 필터 = 아무것도 안 받음, 새 소켓 = 전부 받음 */
    struct can_filter f5[2] = { { 0x123, CAN_SFF_MASK }, { 0x100, 0x700 } };
    CHECK(can_set_filters(rx, f5, 2) == 0);
    drain(all, 1000);
    can_send(tx, 0x123, NULL, 0);
    can_send(tx, 0x2AA, NULL, 0);
    CHECK(next_id(rx, T_POS) == 0x123 && next_id(rx, T_NEG) == 0xFFFFFFFFu);
    CHECK(can_set_filters(rx, NULL, 0) == 0);
    can_send(tx, 0x321, NULL, 0);
    CHECK(next_id(rx, T_NEG) == 0xFFFFFFFFu);
    CHECK(drain(all, 1000) == 3);                             /* 기본 필터 {0, 0} = 전부 */

    static struct can_filter many[CAN_RAW_FILTER_MAX + 1];
    for (int i = 0; i <= CAN_RAW_FILTER_MAX; i++) { many[i].can_id = (canid_t)i; many[i].can_mask = CAN_SFF_MASK; }
    CHECK(can_set_filters(rx, many, CAN_RAW_FILTER_MAX) == 0);
    CHECK(can_set_filters(rx, many, CAN_RAW_FILTER_MAX + 1) == -EINVAL);

    /* B7: 랜덤 필터 × 랜덤 프레임 — 커널이 실제로 전달한 순서열 == filter_match 모델의 예측 */
    uint32_t s = 0xC0FFEEu;
    int rounds_ok = 0, sent = 0, delivered = 0;
    const struct can_filter sentinel = { 0x7FF, CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG };
    for (int round = 0; round < 40; round++) {
        struct can_filter f[5];
        size_t nf = 1 + xs32(&s) % 4;
        for (size_t k = 0; k < nf; k++) {
            uint32_t t = xs32(&s) % 4, a = xs32(&s), b = xs32(&s);
            if (t == 0)      { f[k].can_id = a & 0x7FF; f[k].can_mask = CAN_SFF_MASK | (b & 1 ? CAN_EFF_FLAG : 0) | (b & 2 ? CAN_RTR_FLAG : 0); }
            else if (t == 1) { f[k].can_id = a & 0x7FF; f[k].can_mask = (b & 1) ? 0x700 : 0x7F0; }
            else if (t == 2) { f[k].can_id = (a & CAN_EFF_MASK) | CAN_EFF_FLAG; f[k].can_mask = (b & 1 ? CAN_EFF_MASK : 0x1FFFFF00u) | CAN_EFF_FLAG; }
            else             { f[k].can_id = a & ~CAN_ERR_FLAG; f[k].can_mask = b & ~CAN_ERR_FLAG; }
            if (xs32(&s) % 4 == 0) f[k].can_id |= CAN_INV_FILTER;
        }
        f[nf] = sentinel;
        if (can_set_filters(rx, f, nf + 1) != 0) continue;
        canid_t want[64];
        int nw = 0;
        canid_t ids[64];
        for (int i = 0; i < 64; i++) {
            uint32_t a = xs32(&s), kind = xs32(&s) % 4;
            canid_t base = (canid_t)(f[a % nf].can_id & CAN_EFF_MASK);
            canid_t id;
            if (kind == 0)      id = (base ^ (a >> 8 & 0xF)) & CAN_SFF_MASK;        /* 필터 근처 SFF */
            else if (kind == 1) id = ((base ^ (a >> 8 & 0xF)) & CAN_EFF_MASK) | CAN_EFF_FLAG;
            else if (kind == 2) id = (a >> 4) & CAN_SFF_MASK;
            else                id = ((a >> 3) & CAN_EFF_MASK) | CAN_EFF_FLAG;
            if (a & 0x80000000u) id |= CAN_RTR_FLAG;
            if (id == 0x7FF) id = 0x7FE;                       /* 센티널과 겹치지 않게 */
            ids[i] = id;
            if (filters_accept(id, f, nf + 1)) want[nw++] = id;
        }
        for (int i = 0; i < 64; i++) can_send(tx, ids[i], NULL, 0);
        can_send(tx, 0x7FF, NULL, 0);
        sent += 64;
        int got = 0, match = 1;
        for (;;) {
            canid_t id = next_id(rx, T_POS);
            if (id == 0x7FF || id == 0xFFFFFFFFu) { if (id != 0x7FF) match = 0; break; }
            if (got >= nw || want[got] != id) match = 0;
            if (got < 64) got++;
        }
        if (match && got == nw) rounds_ok++;
        delivered += got;
    }
    printf("B7 random filters: 40 rounds, %d frames sent, kernel delivered %d, rounds where kernel == model: %d\n",
           sent, delivered, rounds_ok);
    CHECK(rounds_ok == 40);

    /* B8: 에러 프레임 — vcan 은 스스로 만들지 않으므로 직접 주입. 에러 마스크가 있는 소켓만 받는다 */
    int erx = can_open(if0), erx2 = can_open(if0);
    CHECK(erx >= 0 && erx2 >= 0);
    CHECK(can_set_err_mask(erx, CAN_ERR_MASK) == 0);
    CHECK(can_set_filters(erx, NULL, 0) == 0);                /* 데이터 프레임은 끄고 에러만 감시 */
    CHECK(can_set_err_mask(erx2, CAN_ERR_CRTL) == 0);
    struct can_filter fall = { 0, 0 };
    CHECK(can_set_filters(rx, &fall, 1) == 0);
    uint8_t z8[8] = { 0 };
    CHECK(can_send(tx, CAN_ERR_FLAG | CAN_ERR_BUSOFF, z8, CAN_ERR_DLC) == 0);
    can_send(tx, 0x010, NULL, 0);
    CHECK(next_id(erx, T_POS) == (CAN_ERR_FLAG | CAN_ERR_BUSOFF) && next_id(erx, T_NEG) == 0xFFFFFFFFu);
    CHECK(next_id(erx2, T_POS) == 0x010 && next_id(erx2, T_NEG) == 0xFFFFFFFFu);   /* CRTL 만 구독 → BUSOFF 는 안 옴 */
    CHECK(next_id(rx, T_POS) == 0x010 && next_id(rx, T_NEG) == 0xFFFFFFFFu);  /* {0,0} 필터도 에러 프레임은 안 줌 */

    int fds[5] = { tx, rx, all, erx, erx2 };
    close_all(fds, 5);
}

static void test_part_c(const char *if0, const char *if1) {
    int ftx = can_open(if0), frx = can_open(if0), crx = can_open(if0), ctx = can_open(if0), v1 = can_open(if1);
    CHECK(ftx >= 0 && frx >= 0 && crx >= 0 && ctx >= 0 && v1 >= 0);
    int mtu0 = can_if_mtu(ftx, if0), mtu1 = can_if_mtu(ftx, if1);
    printf("MTU %s=%d %s=%d\n", if0, mtu0, if1, mtu1);
    CHECK(mtu0 == CANFD_MTU && mtu1 == CAN_MTU);
    CHECK(can_enable_fd(ftx) == 0 && can_enable_fd(frx) == 0 && can_enable_fd(v1) == 0);

    struct canfd_frame cf;
    memset(&cf, 0, sizeof cf);
    uint8_t d64[64];
    for (int i = 0; i < 64; i++) d64[i] = (uint8_t)i;
    CHECK(can_send_fd(ftx, 0x321, d64, 64, CANFD_BRS) == 0);
    int r = can_recv(frx, &cf, T_POS, NULL);
    CHECK(r == CANFD_MTU && cf.can_id == 0x321 && cf.len == 64 && memcmp(cf.data, d64, 64) == 0 && (cf.flags & CANFD_BRS));
    printf("FD rx flags = 0x%X (BRS=0x%X ESI=0x%X FDF=0x%X)\n", cf.flags, CANFD_BRS, CANFD_ESI, CANFD_FDF);
    CHECK(can_recv(crx, &cf, T_NEG, NULL) == 0);              /* FD 끈 소켓에는 FD 프레임이 오지 않는다 */

    const uint8_t d2[2] = { 0xAB, 0xCD };
    CHECK(can_send(ftx, 0x0AB, d2, 2) == 0);                   /* FD 소켓도 클래식 16 B 쓰기 가능 */
    r = can_recv(frx, &cf, T_POS, NULL);
    CHECK(r == CAN_MTU && cf.can_id == 0x0AB && cf.len == 2); /* 읽은 바이트 수 = 프레임 종류 */
    CHECK(can_recv(crx, &cf, T_POS, NULL) == CAN_MTU);

    CHECK(can_send_fd(ctx, 0x321, d64, 8, 0) == -EINVAL);     /* FD 안 켠 소켓의 72 B 쓰기 */
    CHECK(can_send_fd(v1, 0x321, d64, 8, CANFD_BRS) == -EINVAL); /* MTU 16 인터페이스 */
    CHECK(can_send(v1, 0x321, d64, 8) == 0);

    static const uint8_t fdlens[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };
    int ok = 0;
    for (size_t i = 0; i < sizeof fdlens; i++) {
        uint8_t fl = (uint8_t)((i & 1 ? CANFD_BRS : 0) | (i & 2 ? CANFD_ESI : 0));
        canid_t id = (i & 4) ? (0x1F00000u + (canid_t)i) | CAN_EFF_FLAG : 0x700u + (canid_t)i;
        if (can_send_fd(ftx, id, d64, fdlens[i], fl) != 0) continue;
        if (can_recv(frx, &cf, T_POS, NULL) == CANFD_MTU && cf.can_id == id && cf.len == fdlens[i] &&
            memcmp(cf.data, d64, fdlens[i]) == 0 && (cf.flags & (CANFD_BRS | CANFD_ESI)) == fl) ok++;
    }
    CHECK(ok == 16);
    CHECK(can_send_fd(ftx, 0x321, d64, 65, 0) == -EINVAL);
    CHECK(drain(crx, 100) == 0);

    /* C3: candump -L 표기. 손으로 쓴 기대값 + 버스를 지나 받은 프레임 */
    char s[160];
    struct canfd_frame t;
    memset(&t, 0, sizeof t);
    t.can_id = 0x123; t.len = 3; t.data[0] = 0x11; t.data[1] = 0x22; t.data[2] = 0x33;
    CHECK(can_fmt(&t, CAN_MTU, s, sizeof s) == 10 && strcmp(s, "123#112233") == 0);
    CHECK(can_fmt(&t, CAN_MTU, s, 10) == -ENOSPC && can_fmt(&t, CAN_MTU, s, 11) == 10);
    t.can_id = 0x1ABCDEF0u | CAN_EFF_FLAG; t.len = 0;
    CHECK(can_fmt(&t, CAN_MTU, s, sizeof s) == 9 && strcmp(s, "1ABCDEF0#") == 0);
    t.can_id = 0x123 | CAN_EFF_FLAG; t.len = 1; t.data[0] = 0xAA;
    CHECK(can_fmt(&t, CAN_MTU, s, sizeof s) > 0 && strcmp(s, "00000123#AA") == 0);
    t.can_id = 0x456 | CAN_RTR_FLAG; t.len = 0;
    CHECK(can_fmt(&t, CAN_MTU, s, sizeof s) == 5 && strcmp(s, "456#R") == 0);
    t.len = 2;
    CHECK(can_fmt(&t, CAN_MTU, s, sizeof s) == 6 && strcmp(s, "456#R2") == 0);
    t.can_id = CAN_ERR_FLAG | CAN_ERR_BUSOFF; t.len = 8; memset(t.data, 0, 8);
    CHECK(can_fmt(&t, CAN_MTU, s, sizeof s) > 0 && strcmp(s, "20000040#0000000000000000") == 0);
    t.can_id = 0x321; t.len = 3; t.flags = CANFD_BRS | CANFD_FDF; t.data[0] = 0x11; t.data[1] = 0x22; t.data[2] = 0x33;
    CHECK(can_fmt(&t, CANFD_MTU, s, sizeof s) == 12 && strcmp(s, "321##5112233") == 0);
    memcpy(t.data, d64, 64); t.len = 64; t.flags = CANFD_BRS | CANFD_ESI | CANFD_FDF;
    CHECK(can_fmt(&t, CANFD_MTU, s, sizeof s) == 3 + 3 + 128 && strncmp(s, "321##7000102", 12) == 0);

    CHECK(can_send_fd(ftx, 0x321, d2, 2, CANFD_BRS) == 0);
    r = can_recv(frx, &cf, T_POS, NULL);
    CHECK(r == CANFD_MTU && can_fmt(&cf, r, s, sizeof s) > 0 && strncmp(s, "321##", 5) == 0 && strcmp(s + 6, "ABCD") == 0);
    printf("C3 received FD frame formatted: %s\n", r > 0 ? s : "(none)");

    int fds[5] = { ftx, frx, crx, ctx, v1 };
    close_all(fds, 5);
}

int main(int argc, char **argv) {
    const char *if0 = argc > 1 ? argv[1] : "vcan0";   /* mtu 72 */
    const char *if1 = argc > 2 ? argv[2] : "vcan1";   /* mtu 16 */
    test_layout();
    test_part_a(if0);
    test_part_b(if0);
    test_part_c(if0, if1);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
