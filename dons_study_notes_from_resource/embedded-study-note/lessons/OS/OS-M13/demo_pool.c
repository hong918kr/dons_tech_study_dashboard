/* ENV: linux */
/* BUILD: -pthread */
/* OS-M13 demo_pool.c — 워커 4 개가 epfd 하나를 공유할 때 EPOLLONESHOT 이 없으면 무슨 일이 생기나 (관찰용, CHECK 없음).
 *   ① 강제 스케줄: 워커 W1 을 "처리 중" 에 붙잡고 같은 fd 에 데이터를 더 보낸다 → 다른 워커가 그 fd 를 받나?
 *   ② 자연 부하: 32 연결 × 200 왕복 (64..4096 B) → 한 연결을 이미 다른 워커가 쥔 상태에서 배달된 이벤트 수
 * ONESHOT 이 없는 풀은 LT 로 등록한다. pool_handle 은 busy 를 보고 "겹친 배달" 을 세고 그냥 돌아간다
 * (안 그러면 두 워커가 같은 출력 버퍼를 동시에 건드린다). 연결은 서버를 멈춘 뒤에만 닫는다 (해제 경합 방지).
 * build (Lima VM): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_pool.c -o pool && ./pool
 */
#define OSM13_LIB_ONLY
#pragma GCC diagnostic ignored "-Wunused-function"
#include "solution.c"

static atomic_int g_release, g_done;
typedef struct { int port, id, ok, fd; } lcli_t;
static void *load_cli(void *p) {
    lcli_t *a = p;
    unsigned char tx[4096];
    int good = 0;
    a->fd = cli_connect(a->port);
    for (int r = 0; a->fd >= 0 && r < 200; r++) {
        size_t len = 64u << (r % 7); /* 64, 128, ..., 4096 */
        for (size_t i = 0; i < len; i++) tx[i] = pat(a->id + r, i);
        good += xfer(a->fd, tx, len, 0) == (long)len;
    }
    a->ok = good == 200;
    atomic_fetch_add(&g_done, 1);
    while (!atomic_load(&g_release)) sleep_ms(1); /* 서버가 멈춘 뒤에 닫는다 */
    if (a->fd >= 0) close(a->fd);
    return NULL;
}

static double secs(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("① 강제 스케줄 — W1 이 A(1000 B) 를 읽고 붙잡힌 100 ms 동안 같은 fd 에 B(1000 B) 도착\n");
    for (int oneshot = 0; oneshot < 2; oneshot++) {
        atomic_store(&g_gate_at, 0);
        atomic_store(&g_gate_go, 0);
        atomic_store(&g_gate_used, 0);
        server_t *s = server_create(M_POOL, 4, 16384);
        if (!s) return 1;
        s->oneshot = oneshot;
        s->hook = gate_hook;
        server_run(s);
        unsigned char tx[2000], rx[2000];
        for (int i = 0; i < 2000; i++) tx[i] = pat(5, (size_t)i);
        int cfd = cli_connect(s->port);
        send_all(cfd, tx, 1000);
        int at = wait_flag(&g_gate_at, 1000);
        long e0 = STV(s, events), o0 = STV(s, overlaps), w0 = STV(s, wakeups);
        send_all(cfd, tx + 1000, 1000);
        sleep_ms(100);
        long e1 = STV(s, events), o1 = STV(s, overlaps), w1 = STV(s, wakeups);
        atomic_store(&g_gate_go, 1);
        size_t n = recv_upto(cfd, rx, 2000, PROGRESS_MS);
        printf("  %-12s gate=%d  붙잡힌 동안: 이벤트 %ld, 깨어남 %ld, 겹친 배달(overlaps) %ld | 놓아준 뒤 에코 %zu/2000 %s\n",
               oneshot ? "ONESHOT" : "ONESHOT 없음", at, e1 - e0, w1 - w0, o1 - o0, n,
               n == 2000 && memcmp(rx, tx, 2000) == 0 ? "일치" : "불일치");
        server_stop(s);
        close(cfd);
        free(s);
    }

    printf("② 자연 부하 — 32 연결 × 200 왕복 (64..4096 B), 워커 4\n");
    for (int oneshot = 0; oneshot < 2; oneshot++) {
        server_t *s = server_create(M_POOL, 4, 0);
        if (!s) return 1;
        s->oneshot = oneshot;
        server_run(s);
        atomic_store(&g_release, 0);
        atomic_store(&g_done, 0);
        pthread_t th[32];
        lcli_t a[32];
        double t0 = secs();
        for (int i = 0; i < 32; i++) {
            a[i] = (lcli_t){ s->port, i, 0, -1 };
            pthread_create(&th[i], NULL, load_cli, &a[i]);
        }
        int ok = 0;
        while (atomic_load(&g_done) < 32 && secs() - t0 < 20) sleep_ms(1); /* 모두 왕복을 끝낼 때까지 */
        double t1 = secs();
        server_stop(s);
        atomic_store(&g_release, 1);
        for (int i = 0; i < 32; i++) { pthread_join(th[i], NULL); ok += a[i].ok; }
        printf("  %-12s 성공 %d/32, %.2f s, wakeups %ld, events %ld, 겹친 배달 %ld (events 의 %.1f%%), reads %ld\n",
               oneshot ? "ONESHOT" : "ONESHOT 없음", ok, t1 - t0, STV(s, wakeups), STV(s, events), STV(s, overlaps),
               100.0 * (double)STV(s, overlaps) / (double)(STV(s, events) ? STV(s, events) : 1), STV(s, reads));
        free(s);
    }
    return 0;
}
