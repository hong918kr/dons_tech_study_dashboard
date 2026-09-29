#define _DEFAULT_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static int p[2];

static double now_ms(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}
static void *slow_writer(void *a) { (void)a; usleep(300000); write(p[1], "GPS", 3); return NULL; }

int main(void) {
    char buf[16];
    /* 1) 블로킹 read: 데이터가 올 때까지 스레드가 멈춘다 */
    pipe(p);
    pthread_t t; pthread_create(&t, NULL, slow_writer, NULL);
    double t0 = now_ms();
    ssize_t n = read(p[0], buf, sizeof buf);
    printf("블로킹 read -> %zd bytes, %.0f ms 멈춰 있었다\n", n, now_ms() - t0);
    pthread_join(t, NULL); close(p[0]); close(p[1]);

    /* 2) O_NONBLOCK read: 데이터가 없으면 즉시 -1 / EAGAIN */
    pipe(p);
    fcntl(p[0], F_SETFL, O_NONBLOCK);
    t0 = now_ms();
    n = read(p[0], buf, sizeof buf);
    printf("논블로킹 read -> %zd, errno=%d (%s), %.2f ms\n",
           n, errno, strerror(errno), now_ms() - t0);
    write(p[1], "RSSI", 4);
    n = read(p[0], buf, sizeof buf);
    printf("데이터를 넣고 다시 read -> %zd bytes '%.*s'\n", n, (int)n, buf);

    /* 3) poll: fd 하나를 최대 100 ms 기다린다 (아무도 안 쓴다) */
    struct pollfd pf = { .fd = p[0], .events = POLLIN };
    t0 = now_ms();
    int r = poll(&pf, 1, 100);
    printf("poll(타임아웃 100 ms) -> %d, %.0f ms 뒤 복귀\n", r, now_ms() - t0);

    /* 4) poll: 쓴 다음에는 바로 준비됨으로 돌아온다 */
    write(p[1], "x", 1);
    t0 = now_ms();
    r = poll(&pf, 1, 100);
    printf("poll(데이터 있음) -> %d, revents&POLLIN=%d, %.2f ms\n",
           r, (pf.revents & POLLIN) != 0, now_ms() - t0);
    return 0;
}
