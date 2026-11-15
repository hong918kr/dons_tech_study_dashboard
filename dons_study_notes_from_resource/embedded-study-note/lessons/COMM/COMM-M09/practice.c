/* ENV: linux */
/* COMM-M09 추가 연습 — 한 번에 검증 (Lima VM, vcan0 mtu 72 + vcan1 mtu 16; check.sh 가 준비)
 *
 * 연습 1: CAN_RAW_JOIN_FILTERS — 필터 OR 를 AND 로. 모델 vs 커널 랜덤 대조
 * 연습 2: cansend 문법 파서 can_parse("123#11.22.33") ↔ can_fmt 왕복, 버스 통과 후 candump -L 표기와 일치
 * 연습 3: 요청/응답 + 마감시각(deadline) — 잡음 프레임이 섞여도 총 대기시간이 늘지 않는 can_request
 * 연습 4: 수신 큐 넘침 감지 — SO_RCVBUF 최소 + SO_RXQ_OVFL 로 "몇 개 잃었나" 세기
 * 연습 5: 게이트웨이 — ifindex 0(모든 인터페이스) 소켓 + recvfrom/sendto 로 vcan0 → vcan1 ID 변환 전달
 *
 * 빌드: gcc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice [vcan0] [vcan1]
 */
#define _DEFAULT_SOURCE
#include <ctype.h>
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
#include <time.h>
#include <unistd.h>
#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>

#define T_POS 500
#define T_NEG 30

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---- 본문(solution.c)의 함수들, 축약 ---- */
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
static int can_write(int fd, const struct canfd_frame *cf, int mtu) {
    ssize_t n = write(fd, cf, (size_t)mtu);
    if (n < 0) return -errno;
    return n == mtu ? 0 : -EIO;
}
static int can_send(int fd, canid_t id, const uint8_t *data, uint8_t len) {
    struct canfd_frame cf;
    memset(&cf, 0, sizeof cf);
    cf.can_id = id;
    cf.len = len;
    if (data) memcpy(cf.data, data, len < CAN_MAX_DLEN ? len : CAN_MAX_DLEN);
    return can_write(fd, &cf, CAN_MTU);
}
static int can_recv(int fd, struct canfd_frame *cf, int timeout_ms, int *msg_flags) {
    struct pollfd p = { .fd = fd, .events = POLLIN };
    int r = poll(&p, 1, timeout_ms);
    if (r < 0) return -errno;
    if (r == 0) return 0;
    if (!(p.revents & POLLIN)) return -EIO;
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
static int can_set_filters(int fd, const struct can_filter *f, size_t n) {
    return setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FILTER, n ? f : NULL, (socklen_t)(n * sizeof *f)) < 0 ? -errno : 0;
}
static int setopt_int(int fd, int level, int opt, int val) {
    return setsockopt(fd, level, opt, &val, sizeof val) < 0 ? -errno : 0;
}
static bool filter_match(canid_t rx_id, const struct can_filter *f) {
    canid_t id = f->can_id, mask = f->can_mask;
    bool inv = (id & CAN_INV_FILTER) != 0;
    if ((mask & CAN_EFF_FLAG) && !(id & CAN_EFF_FLAG)) mask &= CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG;
    id &= mask;
    bool eq = (rx_id & mask) == id;
    return inv ? !eq : eq;
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
static canid_t next_id(int fd, int timeout_ms) {
    struct canfd_frame cf;
    return can_recv(fd, &cf, timeout_ms, NULL) > 0 ? cf.can_id : 0xFFFFFFFFu;
}

/* ================= 연습 1: JOIN_FILTERS (AND) ================= */
static bool filters_accept_join(canid_t id, const struct can_filter *f, size_t n) {
    if (n == 0) return false;
    for (size_t i = 0; i < n; i++)
        if (!filter_match(id, &f[i])) return false;
    return true;
}

static void practice1(const char *if0) {
    int tx = can_open(if0), rx = can_open(if0);
    CHECK(tx >= 0 && rx >= 0);
    CHECK(setopt_int(rx, SOL_CAN_RAW, CAN_RAW_JOIN_FILTERS, 1) == 0);
    /* 0x100..0x1FF AND 하위 니블 1 → 0x101, 0x111, ... 0x1F1 만 */
    struct can_filter f[2] = { { 0x100, 0x700 }, { 0x001, 0x00F } };
    CHECK(can_set_filters(rx, f, 2) == 0);
    canid_t ids[5] = { 0x101, 0x102, 0x201, 0x111, 0x1F1 };
    for (int i = 0; i < 5; i++) can_send(tx, ids[i], NULL, 0);
    CHECK(next_id(rx, T_POS) == 0x101 && next_id(rx, T_POS) == 0x111 && next_id(rx, T_POS) == 0x1F1 &&
          next_id(rx, T_NEG) == 0xFFFFFFFFu);

    uint32_t s = 0x5EED1u;
    int rounds_ok = 0, delivered = 0;
    for (int round = 0; round < 12; round++) {
        struct can_filter g[3];
        size_t ng = 2 + xs32(&s) % 2;
        canid_t base = xs32(&s) & CAN_SFF_MASK;
        static const canid_t nib[3] = { 0x00F, 0x0F0, 0x700 };   /* 필터 k 는 니블 k 만 본다 */
        for (size_t k = 0; k < ng; k++) {
            uint32_t a = xs32(&s);
            g[k].can_id = base & nib[k];
            g[k].can_mask = nib[k] | ((a & 1) ? CAN_EFF_FLAG : 0);
            if ((a & 0x70) == 0) g[k].can_id |= CAN_INV_FILTER;
        }
        if (can_set_filters(rx, g, ng) != 0) continue;
        canid_t want[48];
        int nw = 0, got = 0, match = 1;
        canid_t sent_ids[48];
        for (int i = 0; i < 48; i++) {
            uint32_t a = xs32(&s);
            canid_t id = base;
            for (int k = 0; k < 3; k++)                          /* 니블마다 1/3 확률로 흔든다 */
                if ((a >> (4 * k)) % 3 == 0) id ^= ((a >> 16) & nib[k]) ? ((a >> 16) & nib[k]) : (nib[k] & 0x111);
            id &= CAN_SFF_MASK;
            if ((a >> 28) == 0) id |= CAN_EFF_FLAG;
            sent_ids[i] = id;
            if (filters_accept_join(id, g, ng)) want[nw++] = id;
        }
        for (int i = 0; i < 48; i++) can_send(tx, sent_ids[i], NULL, 0);
        for (;;) {      /* AND 에는 "모두가 받는 센티널"을 만들기 어렵다 → T_NEG 동안 안 오면 끝 */
            canid_t id = next_id(rx, T_NEG);
            if (id == 0xFFFFFFFFu) break;
            if (got >= nw || want[got] != id) match = 0;
            if (++got > 48) { match = 0; break; }
        }
        if (match && got == nw) rounds_ok++;
        delivered += got;
    }
    printf("P1 join filters: 12 rounds x 48 frames, delivered %d, kernel == AND-model in %d rounds\n", delivered, rounds_ok);
    CHECK(rounds_ok == 12);
    close(tx);
    close(rx);
}

/* ================= 연습 2: cansend 문법 파서 ================= */
static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* "123#11.22.33" / "1ABCDEF0#" / "123#R" "123#R3" / "321##1AABB" → cf, 반환 CAN_MTU 또는 CANFD_MTU, 오류 -EINVAL.
 * 규칙(이 레슨의 정의): ID 3자리 = SFF(≤ 0x7FF), 8자리 = EFF(≤ 0x1FFFFFFF). 데이터 바이트 사이 '.' 허용.
 * FD 길이는 유효 길이(0..8, 12, 16, 20, 24, 32, 48, 64)만 허용. */
static int can_parse(const char *s, struct canfd_frame *cf) {
    memset(cf, 0, sizeof *cf);
    const char *h = strchr(s, '#');
    if (!h) return -EINVAL;
    size_t idlen = (size_t)(h - s);
    if (idlen != 3 && idlen != 8) return -EINVAL;
    uint32_t id = 0;
    for (size_t i = 0; i < idlen; i++) {
        int v = hexval(s[i]);
        if (v < 0) return -EINVAL;
        id = id << 4 | (uint32_t)v;
    }
    if (idlen == 3) { if (id > CAN_SFF_MASK) return -EINVAL; }
    else { if (id > CAN_EFF_MASK) return -EINVAL; id |= CAN_EFF_FLAG; }
    const char *p = h + 1;
    int mtu = CAN_MTU;
    unsigned maxlen = CAN_MAX_DLEN;
    if (*p == '#') {
        int v = hexval(p[1]);
        if (v < 0) return -EINVAL;
        cf->flags = (uint8_t)v;
        p += 2;
        mtu = CANFD_MTU;
        maxlen = CANFD_MAX_DLEN;
    } else if (*p == 'R' || *p == 'r') {
        cf->can_id = id | CAN_RTR_FLAG;
        if (p[1] == '\0') return CAN_MTU;
        if (p[1] >= '0' && p[1] <= '8' && p[2] == '\0') { cf->len = (uint8_t)(p[1] - '0'); return CAN_MTU; }
        return -EINVAL;
    }
    cf->can_id = id;
    unsigned n = 0;
    while (*p) {
        if (*p == '.') { p++; continue; }
        int hi = hexval(p[0]), lo = hi < 0 ? -1 : hexval(p[1]);
        if (hi < 0 || lo < 0) return -EINVAL;          /* 홀수 자리 / 비 hex */
        if (n >= maxlen) return -EINVAL;               /* 너무 많음 */
        cf->data[n++] = (uint8_t)(hi << 4 | lo);
        p += 2;
    }
    cf->len = (uint8_t)n;
    if (mtu == CANFD_MTU && n > 8 && n != 12 && n != 16 && n != 20 && n != 24 && n != 32 && n != 48 && n != 64)
        return -EINVAL;
    return mtu;
}

static void practice2(const char *if0) {
    struct canfd_frame cf, rcf;
    char out[160];
    CHECK(can_parse("123#11.22.33", &cf) == CAN_MTU && cf.can_id == 0x123 && cf.len == 3 && cf.data[2] == 0x33);
    CHECK(can_parse("1ABCDEF0#", &cf) == CAN_MTU && cf.can_id == (0x1ABCDEF0u | CAN_EFF_FLAG) && cf.len == 0);
    CHECK(can_parse("456#R", &cf) == CAN_MTU && cf.can_id == (0x456u | CAN_RTR_FLAG) && cf.len == 0);
    CHECK(can_parse("456#R3", &cf) == CAN_MTU && cf.len == 3);
    CHECK(can_parse("321##1AABB", &cf) == CANFD_MTU && cf.flags == CANFD_BRS && cf.len == 2 && cf.data[1] == 0xBB);
    const char *bad[] = { "123", "12#00", "1234#00", "12G#00", "800#00", "20000000#00", "123#1", "123#112233445566778899",
                          "123#R9", "123##", "123##1" "000102030405060708", "123#1.2" };
    int nbad = 0;
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) nbad += can_parse(bad[i], &cf) == -EINVAL;
    CHECK(nbad == (int)(sizeof bad / sizeof bad[0]));

    /* 랜덤 왕복: frame → fmt → parse → 같은 frame, 다시 fmt → 같은 문자열 */
    uint32_t s = 0xFACE5u;
    int rt_ok = 0;
    static const uint8_t fdl[] = { 0, 1, 5, 8, 12, 16, 20, 24, 32, 48, 64 };
    for (int i = 0; i < 3000; i++) {
        memset(&cf, 0, sizeof cf);
        uint32_t a = xs32(&s);
        int mtu = (a & 3) == 0 ? CANFD_MTU : CAN_MTU;
        cf.can_id = (a & 4) ? ((xs32(&s) & CAN_EFF_MASK) | CAN_EFF_FLAG) : (xs32(&s) & CAN_SFF_MASK);
        if (mtu == CANFD_MTU) { cf.len = fdl[xs32(&s) % sizeof fdl]; cf.flags = (uint8_t)(xs32(&s) & 0x7); }
        else if (a & 8) { cf.can_id |= CAN_RTR_FLAG; cf.len = (uint8_t)(xs32(&s) % 9); }
        else cf.len = (uint8_t)(xs32(&s) % 9);
        if (!(cf.can_id & CAN_RTR_FLAG)) for (int k = 0; k < cf.len; k++) cf.data[k] = (uint8_t)xs32(&s);
        char s1[160], s2[160];
        if (can_fmt(&cf, mtu, s1, sizeof s1) < 0) continue;
        if (can_parse(s1, &rcf) != mtu) continue;
        if (can_fmt(&rcf, mtu, s2, sizeof s2) < 0) continue;
        if (strcmp(s1, s2) == 0 && rcf.can_id == cf.can_id && rcf.len == cf.len && rcf.flags == cf.flags &&
            ((cf.can_id & CAN_RTR_FLAG) || memcmp(rcf.data, cf.data, cf.len) == 0)) rt_ok++;
    }
    CHECK(rt_ok == 3000);

    /* 버스 통과: 파싱한 프레임을 보내고, 받은 프레임을 다시 표기 → candump -L 이 찍는 문자열 */
    int tx = can_open(if0), rx = can_open(if0);
    CHECK(tx >= 0 && rx >= 0);
    CHECK(setopt_int(tx, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, 1) == 0 && setopt_int(rx, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, 1) == 0);
    const char *lines[] = { "123#112233", "1ABCDEF0#0102030405060708", "456#R2", "7FF#", "00000123#AA", "321##5112233", "321##4AA" };
    int bus_ok = 0;
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++) {
        int mtu = can_parse(lines[i], &cf);
        if (mtu < 0 || can_write(tx, &cf, mtu) != 0) continue;
        int r = can_recv(rx, &rcf, T_POS, NULL);
        if (r == mtu && can_fmt(&rcf, r, out, sizeof out) > 0 && strcmp(out, lines[i]) == 0) bus_ok++;
        else printf("P2 bus mismatch: sent %s got %s\n", lines[i], r > 0 ? out : "(none)");
    }
    CHECK(bus_ok == (int)(sizeof lines / sizeof lines[0]));
    /* 관찰: "##1"(BRS 만) 로 보내면 커널이 FDF(0x4) 를 붙여 돌려준다 (이 VM 커널에서 "##5") */
    if (can_parse("321##1AA", &cf) == CANFD_MTU && can_write(tx, &cf, CANFD_MTU) == 0 &&
        can_recv(rx, &rcf, T_POS, NULL) == CANFD_MTU && can_fmt(&rcf, CANFD_MTU, out, sizeof out) > 0)
        printf("P2 observe: sent 321##1AA -> received %s\n", out);
    CHECK((rcf.flags & 0x3) == CANFD_BRS);
    close(tx);
    close(rx);
}

/* ================= 연습 3: 요청/응답 + deadline ================= */
static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

typedef void (*sent_hook)(void *ctx);   /* 테스트용: 요청 송신 직후 "ECU" 를 한 번 돌린다 (실제론 다른 노드) */

/* req_id 로 요청을 보내고 (rx & resp_mask) == resp_id 인 첫 프레임을 deadline_ms 안에 기다린다.
 * 다른 프레임은 건너뛰고 *skipped 를 센다. 반환 0 / -ETIMEDOUT / -errno. 대기시간은 "남은 시간"으로 매번 다시 계산. */
static int can_request(int fd, canid_t req_id, const uint8_t *req, uint8_t n, canid_t resp_id, canid_t resp_mask,
                       struct canfd_frame *resp, int deadline_ms, int *skipped, sent_hook hook, void *ctx) {
    int r = can_send(fd, req_id, req, n);
    if (r) return r;
    if (hook) hook(ctx);
    long end = now_ms() + deadline_ms;
    *skipped = 0;
    for (;;) {
        long left = end - now_ms();
        if (left <= 0) return -ETIMEDOUT;
        r = can_recv(fd, resp, (int)left, NULL);
        if (r < 0) return r;
        if (r == 0) return -ETIMEDOUT;
        if ((resp->can_id & resp_mask) == (resp_id & resp_mask)) return 0;
        (*skipped)++;
    }
}

struct ecu { int fd; canid_t answer_id; int noise; int tx_noise_fd; };
static void ecu_run(void *vctx) {      /* 0x7DF 요청을 읽고 answer_id 로 응답. 앞뒤로 잡음 프레임 */
    struct ecu *e = vctx;
    struct canfd_frame cf;
    for (int i = 0; i < e->noise; i++) can_send(e->tx_noise_fd, 0x100u + (canid_t)i, NULL, 0);
    if (e->fd < 0) return;                       /* 죽은 ECU: 잡음만 있고 응답 없음 */
    if (can_recv(e->fd, &cf, T_POS, NULL) == CAN_MTU && cf.can_id == 0x7DF && cf.len >= 3 && cf.data[1] == 0x01) {
        const uint8_t rpm[5] = { 0x04, 0x41, cf.data[2], 0x1A, 0xF8 };   /* 모드 01 응답: 0x41 + PID + 값 */
        can_send(e->fd, e->answer_id, rpm, 5);
    }
}

static void practice3(const char *if0) {
    int tester = can_open(if0), ecu_fd = can_open(if0), noise = can_open(if0);
    CHECK(tester >= 0 && ecu_fd >= 0 && noise >= 0);
    struct can_filter only_req = { 0x7DF, CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG };
    CHECK(can_set_filters(ecu_fd, &only_req, 1) == 0);
    const uint8_t req[3] = { 0x02, 0x01, 0x0C };   /* OBD-II 모드 01 PID 0x0C (엔진 rpm) 요청 모양 — 예시 */
    struct canfd_frame resp;
    int skipped = -1;

    struct ecu e = { ecu_fd, 0x7E8, 20, noise };
    int r = can_request(tester, 0x7DF, req, 3, 0x7E8, 0x7F8, &resp, 200, &skipped, ecu_run, &e);
    CHECK(r == 0 && resp.can_id == 0x7E8 && resp.len == 5 && resp.data[1] == 0x41 && resp.data[2] == 0x0C);
    CHECK(skipped == 20);                            /* 필터 없음: 잡음 20 개를 사용자 공간에서 걸러냄 */

    struct can_filter resp_f = { 0x7E8, 0x7F8 | CAN_EFF_FLAG | CAN_RTR_FLAG };   /* 0x7E8..0x7EF */
    CHECK(can_set_filters(tester, &resp_f, 1) == 0);
    struct ecu e2 = { ecu_fd, 0x7E9, 20, noise };
    r = can_request(tester, 0x7DF, req, 3, 0x7E8, 0x7F8, &resp, 200, &skipped, ecu_run, &e2);
    CHECK(r == 0 && resp.can_id == 0x7E9 && skipped == 0);   /* 커널 필터: 잡음이 아예 안 깨움 */

    struct ecu dead = { -1, 0x7E8, 5, noise };      /* ECU 없음 */
    r = can_request(tester, 0x7DF, req, 3, 0x7E8, 0x7F8, &resp, 100, &skipped, ecu_run, &dead);
    CHECK(r == -ETIMEDOUT);
    struct canfd_frame cf;
    while (can_recv(ecu_fd, &cf, T_NEG, NULL) > 0) { }   /* 응답 안 한 요청 버리기 */
    close(tester);
    close(ecu_fd);
    close(noise);
}

/* ================= 연습 4: 수신 큐 넘침 (SO_RXQ_OVFL) ================= */
/* 프레임 1개 + 그 프레임이 큐에 들어가기 전까지 이 소켓이 잃은 누적 개수(*drops). 반환은 can_recv 와 같다. */
static int can_recv_drops(int fd, struct canfd_frame *cf, int timeout_ms, uint32_t *drops) {
    struct pollfd p = { .fd = fd, .events = POLLIN };
    int r = poll(&p, 1, timeout_ms);
    if (r <= 0) return r < 0 ? -errno : 0;
    char cbuf[CMSG_SPACE(sizeof(uint32_t)) + 64];
    struct iovec iov = { .iov_base = cf, .iov_len = sizeof *cf };
    struct msghdr mh;
    memset(&mh, 0, sizeof mh);
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    mh.msg_control = cbuf;
    mh.msg_controllen = sizeof cbuf;
    ssize_t n = recvmsg(fd, &mh, MSG_DONTWAIT);
    if (n < 0) return -errno;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&mh); c; c = CMSG_NXTHDR(&mh, c))
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SO_RXQ_OVFL) memcpy(drops, CMSG_DATA(c), sizeof *drops);
    return (int)n;
}

static void practice4(const char *if0) {
    int tx = can_open(if0), rx = can_open(if0);
    CHECK(tx >= 0 && rx >= 0);
    CHECK(setopt_int(rx, SOL_SOCKET, SO_RCVBUF, 0) == 0);      /* 커널이 최소값으로 올린다 */
    CHECK(setopt_int(rx, SOL_SOCKET, SO_RXQ_OVFL, 1) == 0);
    int rb = 0;
    socklen_t sl = sizeof rb;
    getsockopt(rx, SOL_SOCKET, SO_RCVBUF, &rb, &sl);
    const int N = 200;
    for (int i = 0; i < N; i++) { uint8_t b = (uint8_t)i; can_send(tx, (canid_t)i, &b, 1); }
    struct canfd_frame cf;
    uint32_t drops = 0, max_drops_seen = 0;
    int got = 0, last = -1;
    while (got < N && can_recv_drops(rx, &cf, T_NEG, &drops) > 0) {
        got++;
        last = (int)cf.can_id;
        if (drops > max_drops_seen) max_drops_seen = drops;
    }
    can_send(tx, 0x7FF, NULL, 0);                               /* 표지 프레임: 넘침 "뒤"에 들어온다 */
    uint32_t marker_drops = 0;
    int r = can_recv_drops(rx, &cf, T_POS, &marker_drops);
    printf("P4 rcvbuf=%d: sent %d, read %d (last id %d), drops stamped on those=%u, on marker=%u\n",
           rb, N, got, last, max_drops_seen, marker_drops);
    CHECK(got > 0 && got < N);
    CHECK(max_drops_seen == 0);                                  /* 넘치기 전에 들어온 프레임에는 0 */
    CHECK(r == CAN_MTU && cf.can_id == 0x7FF && got + (int)marker_drops == N);   /* 받은 + 잃은 = 보낸 */
    close(tx);
    close(rx);
}

/* ================= 연습 5: 게이트웨이 ================= */
struct gw_stats { int fwd, ignored, fd_dropped, from_other_if; };

/* gw: ifindex 0 에 bind, FD 켬. src 에서 온 0x100..0x1FF 클래식 프레임만 id + 0x400 으로 dst 에 보낸다.
 * dst 가 MTU 16 이므로 FD 프레임은 보낼 수 없다 → fd_dropped. dst 에서 온 프레임은 루프 방지로 무시. */
static int gw_poll_once(int gw, int src_idx, int dst_idx, struct gw_stats *st, int timeout_ms) {
    struct canfd_frame cf;
    struct sockaddr_can from;
    socklen_t fl = sizeof from;
    struct pollfd p = { .fd = gw, .events = POLLIN };
    if (poll(&p, 1, timeout_ms) <= 0) return 0;
    ssize_t n = recvfrom(gw, &cf, sizeof cf, MSG_DONTWAIT, (struct sockaddr *)&from, &fl);
    if (n < 0) return -errno;
    if (from.can_ifindex != src_idx) { st->from_other_if++; return 1; }
    if (n == CANFD_MTU) { st->fd_dropped++; return 1; }
    if ((cf.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG)) || (cf.can_id & 0x700) != 0x100) { st->ignored++; return 1; }
    cf.can_id += 0x400;
    struct sockaddr_can to;
    memset(&to, 0, sizeof to);
    to.can_family = AF_CAN;
    to.can_ifindex = dst_idx;
    if (sendto(gw, &cf, CAN_MTU, 0, (struct sockaddr *)&to, sizeof to) != CAN_MTU) return -errno;
    st->fwd++;
    return 1;
}

static void practice5(const char *if0, const char *if1) {
    int gw = can_open(NULL), tx0 = can_open(if0), tx1 = can_open(if1), mon1 = can_open(if1);
    CHECK(gw >= 0 && tx0 >= 0 && tx1 >= 0 && mon1 >= 0);
    CHECK(setopt_int(gw, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, 1) == 0 && setopt_int(tx0, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, 1) == 0);
    int i0 = (int)if_nametoindex(if0), i1 = (int)if_nametoindex(if1);
    struct canfd_frame fdf;
    memset(&fdf, 0, sizeof fdf);
    fdf.can_id = 0x150; fdf.len = 16; fdf.flags = CANFD_BRS;
    can_send(tx0, 0x123, NULL, 0);                  /* → 0x523 */
    can_send(tx0, 0x2AA, NULL, 0);                  /* 범위 밖 → ignored */
    can_write(tx0, &fdf, CANFD_MTU);                /* FD → fd_dropped */
    can_send(tx1, 0x1AB, NULL, 0);                  /* vcan1 에서 옴 → 전달 안 함 */
    can_send(tx0, 0x1FF | CAN_EFF_FLAG, NULL, 0);   /* EFF → ignored */
    can_send(tx0, 0x1FF, NULL, 0);                  /* → 0x5FF */
    struct gw_stats st = { 0, 0, 0, 0 };
    int steps = 0;
    while (steps < 20 && gw_poll_once(gw, i0, i1, &st, T_NEG) > 0) steps++;
    printf("P5 gateway: fwd=%d ignored=%d fd_dropped=%d from_other_if=%d\n", st.fwd, st.ignored, st.fd_dropped, st.from_other_if);
    CHECK(st.fwd == 2 && st.ignored == 2 && st.fd_dropped == 1 && st.from_other_if == 1);
    CHECK(next_id(mon1, T_POS) == 0x1AB);            /* mon1 은 vcan1 의 모든 프레임: tx1 것 먼저 */
    CHECK(next_id(mon1, T_POS) == 0x523 && next_id(mon1, T_POS) == 0x5FF && next_id(mon1, T_NEG) == 0xFFFFFFFFu);
    close(gw);
    close(tx0);
    close(tx1);
    close(mon1);
}

int main(int argc, char **argv) {
    const char *if0 = argc > 1 ? argv[1] : "vcan0";
    const char *if1 = argc > 2 ? argv[2] : "vcan1";
    practice1(if0);
    practice2(if0);
    practice3(if0);
    practice4(if0);
    practice5(if0, if1);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
