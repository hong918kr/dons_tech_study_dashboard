// C47_udp_rpc.c — UDP 위에 "신뢰성 + at-most-once RPC" 를 손으로 얹어 보기
//
//  - 서버/클라이언트 모두 localhost UDP 소켓 (fork 로 프로세스 2개)
//  - 클라이언트: 요청마다 sequence number, 타임아웃(recv 대기) + 재전송, 지수 백오프
//  - 서버: 비멱등 연산 add(x) (counter += x) 를 제공
//          last_seq / last_reply 를 기억해 중복 요청은 "다시 실행하지 않고" 캐시된 답만 재전송
//  - 손실은 서버가 일부러 흉내 낸다 (결정적으로):
//          seq 2 : 첫 번째 "요청" 을 버림       (Figure 47.4: dropped request)
//          seq 3 : 첫 번째 "응답" 을 안 보냄     (Figure 47.5: dropped reply → 중복 요청 도착)
//          seq 4 : 응답을 두 번 연속 안 보냄     (백오프 2번)
//  - 마지막에 dedup 을 끈 "순진한 서버" 로 같은 시나리오를 돌려 counter 가 틀어지는 것을 보여 줌
//
// build: cc -Wall -Wextra -O0 code/C47_udp_rpc.c -o .work/bin/C47_udp_rpc && .work/bin/C47_udp_rpc
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

enum { OP_ADD = 1, OP_QUIT = 2 };

// 와이어 포맷: 전부 네트워크 바이트 순서(big endian) — XDR 이 하는 일의 축소판
struct msg {
    uint32_t seq;   // sequence number (요청 ID)
    uint32_t op;    // 프로시저 ID (stub 이 넣는 "어떤 함수인가")
    int32_t  arg;   // 인자
    int32_t  ret;   // 응답일 때 반환값
};

static void marshal(struct msg *m) {
    m->seq = htonl(m->seq); m->op = htonl(m->op);
    m->arg = (int32_t)htonl((uint32_t)m->arg); m->ret = (int32_t)htonl((uint32_t)m->ret);
}
static void unmarshal(struct msg *m) {
    m->seq = ntohl(m->seq); m->op = ntohl(m->op);
    m->arg = (int32_t)ntohl((uint32_t)m->arg); m->ret = (int32_t)ntohl((uint32_t)m->ret);
}

static double now_ms(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}
static double T0;

// ---------------------------------------------------------------- server
static void server(int sd, int dedup) {
    int counter = 0;
    uint32_t last_seq = 0;        // 마지막으로 "실행한" 요청 번호 (클라이언트 1개라 변수 하나)
    struct msg last_reply = {0};
    int seen[16] = {0};           // seq 별 도착 횟수 (손실 흉내용)

    for (;;) {
        struct msg m; struct sockaddr_in from; socklen_t fl = sizeof(from);
        ssize_t n = recvfrom(sd, &m, sizeof(m), 0, (struct sockaddr *)&from, &fl);
        if (n != (ssize_t)sizeof(m)) continue;
        unmarshal(&m);
        if (m.op == OP_QUIT) break;
        int k = m.seq < 16 ? ++seen[m.seq] : 1;

        if (m.seq == 2 && k == 1) {   // 요청 자체를 잃어버린 척
            printf("  [server %6.1fms] seq=%u  (요청 DROP — 받은 적 없는 셈)\n", now_ms() - T0, m.seq);
            fflush(stdout); continue;
        }
        int drop_reply = (m.seq == 3 && k == 1) || (m.seq == 4 && k <= 2);
        if (dedup && m.seq <= last_seq) {  // 중복: 실행하지 않고 캐시된 응답만 다시 보냄
            printf("  [server %6.1fms] seq=%u  중복 요청 → 재실행 안 함, 캐시된 응답 재전송 (counter=%d)%s\n",
                   now_ms() - T0, m.seq, counter, drop_reply ? "  (응답 DROP)" : "");
            fflush(stdout);
            if (drop_reply) continue;
            struct msg r = last_reply; marshal(&r);
            sendto(sd, &r, sizeof(r), 0, (struct sockaddr *)&from, fl);
            continue;
        }
        counter += m.arg;                 // 실제 프로시저 실행 (비멱등!)
        last_seq = m.seq;
        last_reply = (struct msg){.seq = m.seq, .op = m.op, .arg = m.arg, .ret = counter};
        printf("  [server %6.1fms] seq=%u  add(%d) 실행 → counter=%d%s\n", now_ms() - T0, m.seq, m.arg,
               counter, drop_reply ? "  (응답 DROP)" : "");
        fflush(stdout);
        if (drop_reply) continue;
        struct msg r = last_reply; marshal(&r);
        sendto(sd, &r, sizeof(r), 0, (struct sockaddr *)&from, fl);
    }
    printf("  [server] 종료. 최종 counter=%d\n", counter);
    fflush(stdout);
}

// ---------------------------------------------------------------- client stub
// rpc_add(): 호출자 눈에는 평범한 함수. 안에서 marshal → send → timeout/retry → unmarshal.
static uint32_t next_seq = 1;
static int stat_sends, stat_timeouts;

static int rpc_add(int sd, struct sockaddr_in *srv, int x) {
    struct msg req = {.seq = next_seq++, .op = OP_ADD, .arg = x};
    struct msg wire = req; marshal(&wire);     // 재전송용 사본 보관 ("keep copy")
    int timeout_ms = 100;                      // 처음 타임아웃
    for (int attempt = 1; attempt <= 6; attempt++) {
        sendto(sd, &wire, sizeof(wire), 0, (struct sockaddr *)srv, sizeof(*srv));
        stat_sends++;
        printf("[client %6.1fms] seq=%u  add(%d) 전송 (시도 %d, timeout=%dms)\n",
               now_ms() - T0, req.seq, x, attempt, timeout_ms);
        fflush(stdout);
        double deadline = now_ms() + timeout_ms;
        for (;;) {
            double left = deadline - now_ms();
            if (left <= 0) break;
            fd_set rf; FD_ZERO(&rf); FD_SET(sd, &rf);
            struct timeval tv = {.tv_sec = 0, .tv_usec = (int)(left * 1000)};
            if (select(sd + 1, &rf, NULL, NULL, &tv) <= 0) break;   // 타이머 만료
            struct msg rep;
            if (recv(sd, &rep, sizeof(rep), 0) != (ssize_t)sizeof(rep)) continue;
            unmarshal(&rep);
            if (rep.seq != req.seq) {          // 예전 요청에 대한 늦은 응답 → 버림
                printf("[client] 늦게 온 옛 응답 seq=%u 무시\n", rep.seq);
                continue;
            }
            printf("[client %6.1fms] seq=%u  응답 수신 → 반환값 %d\n", now_ms() - T0, rep.seq, rep.ret);
            fflush(stdout);
            return rep.ret;
        }
        stat_timeouts++;
        printf("[client %6.1fms] seq=%u  TIMEOUT → 재전송, 백오프 %dms→%dms\n",
               now_ms() - T0, req.seq, timeout_ms, timeout_ms * 2);
        fflush(stdout);
        timeout_ms *= 2;                       // exponential back-off
    }
    fprintf(stderr, "rpc_add: 서버 응답 없음\n");
    exit(1);
}

static int run(int dedup) {
    // 서버 소켓을 먼저 bind (포트 0 → 커널이 빈 포트 배정) 한 뒤 fork
    int ssd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sa = {.sin_family = AF_INET, .sin_port = 0};
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (ssd < 0 || bind(ssd, (struct sockaddr *)&sa, sizeof(sa)) < 0) { perror("bind"); exit(1); }
    socklen_t sl = sizeof(sa);
    getsockname(ssd, (struct sockaddr *)&sa, &sl);

    pid_t pid = fork();
    if (pid == 0) { server(ssd, dedup); _exit(0); }
    close(ssd);

    int csd = socket(AF_INET, SOCK_DGRAM, 0);
    next_seq = 1; stat_sends = stat_timeouts = 0;
    int expected = 0, got = 0;
    for (int x = 10; x <= 50; x += 10) {       // add(10), add(20), ... add(50)
        expected += x;
        got = rpc_add(csd, &sa, x);
    }
    struct msg q = {.op = OP_QUIT}; marshal(&q);
    sendto(csd, &q, sizeof(q), 0, (struct sockaddr *)&sa, sizeof(sa));
    waitpid(pid, NULL, 0);
    close(csd);
    printf("=> 기대값 %d, 마지막 응답 %d, 전송 %d회, 타임아웃 %d회 %s\n\n", expected, got, stat_sends,
           stat_timeouts, expected == got ? "(OK: at-most-once 유지)" : "(틀림! 재전송된 add 가 다시 실행됨)");
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    T0 = now_ms();
    printf("=== 1) seq 번호 + 중복 제거(dedup) 서버 ===\n");
    run(1);
    T0 = now_ms();
    printf("=== 2) 중복 제거 없는 순진한 서버 (재전송 = 재실행) ===\n");
    run(0);
    return 0;
}
