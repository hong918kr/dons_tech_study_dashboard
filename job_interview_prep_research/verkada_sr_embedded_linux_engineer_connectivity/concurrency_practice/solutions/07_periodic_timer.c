/* 07. 주기 실행 + 취소 가능한 대기 (timed wait)
 * ------------------------------------------------------------------
 * "센서를 100ms마다 샘플링하되, 종료 신호가 오면 즉시 깨어나라."
 * sleep(100ms) 루프의 문제:
 *   (1) 작업 시간이 누적돼 주기가 밀린다(drift)
 *   (2) 종료 신호를 받아도 최대 100ms 늦게 반응한다
 * 해결: 절대 시각(deadline) 기준으로 cond_timedwait → drift 없음 + 즉시 취소.
 *
 * 이식성 함정(인터뷰에서 가산점):
 *   pthread_cond_timedwait의 기본 시계는 CLOCK_REALTIME이다. NTP가 시각을
 *   되돌리면 대기가 늘어진다 → Linux에서는 condattr로 CLOCK_MONOTONIC 지정.
 *   macOS는 setclock을 지원하지 않아 *_relative_np를 쓴다.
 * 빌드: cc -std=c11 -Wall -Wextra -pthread 07_periodic_timer.c
 */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define NS_PER_MS 1000000LL
#define NS_PER_S  1000000000LL

typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  cv;
    bool            stop;
    unsigned long   ticks, late_ticks;
    int64_t         period_ns;
} Periodic;

static int64_t now_mono_ns(void);
void periodic_init(Periodic *p, int64_t period_ns);
void periodic_destroy(Periodic *p);
/* deadline까지 대기. 중간에 stop이 오면 즉시 true 반환 */
bool periodic_wait_until(Periodic *p, int64_t deadline_ns);
void periodic_stop(Periodic *p);

/*@impl-begin*/
static int64_t now_mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * NS_PER_S + ts.tv_nsec;
}

void periodic_init(Periodic *p, int64_t period_ns)
{
    p->stop = false;
    p->ticks = p->late_ticks = 0;
    p->period_ns = period_ns;
    pthread_mutex_init(&p->m, NULL);
#if defined(__linux__)
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);  /* 시각 점프 방어 */
    pthread_cond_init(&p->cv, &attr);
    pthread_condattr_destroy(&attr);
#else
    pthread_cond_init(&p->cv, NULL);                     /* macOS: 아래 relative 사용 */
#endif
}

void periodic_destroy(Periodic *p)
{
    pthread_mutex_destroy(&p->m);
    pthread_cond_destroy(&p->cv);
}

bool periodic_wait_until(Periodic *p, int64_t deadline_ns)
{
    bool stopped = false;
    pthread_mutex_lock(&p->m);
    while (!p->stop) {
        int64_t remain = deadline_ns - now_mono_ns();
        if (remain <= 0) break;                 /* 시간 다 됨 → 다음 주기 실행 */
#if defined(__APPLE__)
        struct timespec rel = { .tv_sec  = (time_t)(remain / NS_PER_S),
                                .tv_nsec = (long)(remain % NS_PER_S) };
        int rc = pthread_cond_timedwait_relative_np(&p->cv, &p->m, &rel);
#else
        struct timespec abs = { .tv_sec  = (time_t)(deadline_ns / NS_PER_S),
                                .tv_nsec = (long)(deadline_ns % NS_PER_S) };
        int rc = pthread_cond_timedwait(&p->cv, &p->m, &abs);
#endif
        if (rc == ETIMEDOUT) break;
        /* rc == 0 이어도 spurious wakeup일 수 있다 → while로 다시 검사 */
    }
    stopped = p->stop;
    pthread_mutex_unlock(&p->m);
    return stopped;
}

void periodic_stop(Periodic *p)
{
    pthread_mutex_lock(&p->m);
    p->stop = true;
    pthread_mutex_unlock(&p->m);
    pthread_cond_broadcast(&p->cv);             /* 대기 중인 모든 스레드 즉시 기상 */
}
/*@impl-end*/

/* ------------------------------- self test ------------------------------- */
static Periodic g_p;
static _Atomic int64_t g_first_ns, g_last_ns;

static void *sampler(void *arg)
{
    (void)arg;
    int64_t next = now_mono_ns() + g_p.period_ns;
    g_first_ns = now_mono_ns();
    for (;;) {
        if (periodic_wait_until(&g_p, next)) break;   /* stop 신호 */
        g_p.ticks++;
        g_last_ns = now_mono_ns();
        /* 작업 시간 흉내: 주기의 일부를 소모 */
        struct timespec work = { 0, 2 * NS_PER_MS };
        nanosleep(&work, NULL);
        /* drift 방지: '지금 + period'가 아니라 '이전 deadline + period' */
        next += g_p.period_ns;
        if (next < now_mono_ns()) {                   /* 주기를 놓쳤다 */
            g_p.late_ticks++;
            next = now_mono_ns() + g_p.period_ns;     /* 따라잡기 포기(catch-up 방지) */
        }
    }
    return NULL;
}

int main(void)
{
    pthread_t t;
    periodic_init(&g_p, 10 * NS_PER_MS);              /* 10ms 주기 */
    pthread_create(&t, NULL, sampler, NULL);

    struct timespec run = { 0, 200 * NS_PER_MS };     /* 200ms 동안 → 약 20틱 */
    nanosleep(&run, NULL);

    int64_t t0 = now_mono_ns();
    periodic_stop(&g_p);
    pthread_join(t, NULL);
    int64_t stop_latency_us = (now_mono_ns() - t0) / 1000;

    double elapsed_ms = (double)(g_last_ns - g_first_ns) / 1e6;
    printf("ticks=%lu late=%lu elapsed=%.1fms avg_period=%.2fms stop_latency=%lldus\n",
           g_p.ticks, g_p.late_ticks, elapsed_ms,
           g_p.ticks ? elapsed_ms / (double)g_p.ticks : 0.0, (long long)stop_latency_us);

    assert(g_p.ticks >= 15 && g_p.ticks <= 25);       /* 10ms × 200ms ≈ 20틱 */
    assert(stop_latency_us < 5000);                   /* 즉시 취소: 5ms 이내 */
    periodic_destroy(&g_p);
    puts("07 PASS");
    return 0;
}
