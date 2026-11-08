/* SIV-B09 demo — 같은 대기 루프를 "진짜 시계" 에 연결하면?
 * now_us = clock_gettime(CLOCK_MONOTONIC), delay_us = nanosleep. 장치는 시작 3 ms 뒤 READY 가 된다.
 * 결과(경과시간, poll 수)는 실행마다 조금씩 다르다 → 그래서 체크되는 테스트는 가짜 시계를 쓴다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_realtime.c -o demo_realtime && ./demo_realtime
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

typedef struct {
    uint32_t (*read32)(void *ctx, uint32_t off);
    uint32_t (*now_us)(void *ctx);
    void     (*delay_us)(void *ctx, uint32_t us);
    void     *ctx;
} bus_ops_t;

static uint32_t mono_us(void *ctx) {
    (void)ctx;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);               /* 벽시계(CLOCK_REALTIME)는 NTP 로 뒤로 갈 수 있다 */
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u);   /* 하위 32비트 = wrap 카운터 */
}
static void sleep_us(void *ctx, uint32_t us) {
    (void)ctx;
    struct timespec ts = { (time_t)(us / 1000000u), (long)(us % 1000000u) * 1000L };
    nanosleep(&ts, NULL);
}
typedef struct { uint32_t t0, ready_after_us; bool never; } dev_t_;
static uint32_t dev_read32(void *ctx, uint32_t off) {
    dev_t_ *d = ctx;
    (void)off;
    return (!d->never && mono_us(NULL) - d->t0 >= d->ready_after_us) ? 1u : 0u;
}

static int wait_real(const bus_ops_t *b, uint32_t timeout_us, uint32_t interval_us, uint32_t *polls, uint32_t *el) {
    uint32_t start = b->now_us(b->ctx);
    bool last_look = false;
    *polls = 0;
    for (;;) {
        uint32_t v = b->read32(b->ctx, 0);
        (*polls)++;
        *el = b->now_us(b->ctx) - start;
        if (v & 1u) return 0;
        if (last_look) return -1;
        if (*el >= timeout_us) { last_look = true; continue; }
        uint32_t left = timeout_us - *el;
        b->delay_us(b->ctx, interval_us < left ? interval_us : left);
    }
}

int main(void) {
    dev_t_ dev = { mono_us(NULL), 3000u, false };
    bus_ops_t b = { dev_read32, mono_us, sleep_us, &dev };
    uint32_t polls, el;
    int rc = wait_real(&b, 10000u, 200u, &polls, &el);
    printf("ready@3ms   timeout=10ms interval=200us -> %s polls=%u elapsed=%uus\n",
           rc ? "TIMEOUT" : "OK", (unsigned)polls, (unsigned)el);
    dev.never = true;
    rc = wait_real(&b, 5000u, 200u, &polls, &el);
    printf("never ready timeout=5ms  interval=200us -> %s polls=%u elapsed=%uus (overshoot %dus)\n",
           rc ? "TIMEOUT" : "OK", (unsigned)polls, (unsigned)el, (int)el - 5000);
    return 0;
}
