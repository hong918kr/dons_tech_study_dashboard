/* demo_policy_bench.c — 세 정책을 실제 시간으로 비교한다 (제품 숫자 아님, 비율만 볼 것).
 * 생산자 ~1 ms 주기(nanosleep), 소비자 프레임당 3 ms + 50 프레임마다 30 ms 멈춤, nbuf=4.
 * age = 프레임 시작 시각 ~ 앱이 dqbuf 로 받은 시각.
 * 빌드: cc -std=c11 -Wall -Wextra -O2 -pthread demo_policy_bench.c -o demo_policy_bench && ./demo_policy_bench
 */
#define CAM_NO_MAIN
#include "solution.c"

#define FRAMES 1800u
static uint64_t g_ts[FRAMES];

static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

typedef struct { cap_t *c; int done; pthread_mutex_t m; } arg_t;

static void *prod(void *a) {
    arg_t *p = a;
    for (unsigned i = 0; i < FRAMES; i++) {
        g_ts[i] = now_ns();     /* seq == i: 스레드가 hw_frame 전에 기록 → dqbuf 쪽은 락으로 HB */
        cap_hw_frame(p->c);
        sleep_ns(1000000);
    }
    pthread_mutex_lock(&p->m); p->done = 1; pthread_mutex_unlock(&p->m);
    return NULL;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static void run(policy_t pol) {
    static uint64_t ages[FRAMES];
    unsigned nage = 0;
    cap_t c; arg_t a; pthread_t th; consumer_t u; cap_info_t info;
    memset(&a, 0, sizeof a); memset(&u, 0, sizeof u);
    a.c = &c; pthread_mutex_init(&a.m, NULL);
    cap_init(&c, pol, 4);
    for (int i = 0; i < 4; i++) (void)cap_qbuf(&c, i);
    (void)cap_streamon(&c);
    pthread_create(&th, NULL, prod, &a);
    int idle = 0;
    unsigned n = 0;
    while (idle < 3) {
        int idx = cap_dqbuf(&c, &info, 20);
        int d;
        pthread_mutex_lock(&a.m); d = a.done; pthread_mutex_unlock(&a.m);
        if (idx < 0) { if (d) idle++; continue; }
        idle = 0;
        uint64_t age = now_ns() - g_ts[info.seq];
        if (nage < FRAMES) ages[nage++] = age;
        (void)gap_update(&u.gap, info.seq);
        sleep_ns(3000000);
        if (++n % 50 == 0) sleep_ns(30000000);
        (void)cap_qbuf(&c, idx);
    }
    pthread_join(th, NULL);
    (void)cap_streamoff(&c, 200);
    qsort(ages, nage, sizeof ages[0], cmp_u64);
    double p50 = nage ? (double)ages[nage / 2] / 1e6 : 0, p99 = nage ? (double)ages[nage * 99 / 100] / 1e6 : 0,
           mx = nage ? (double)ages[nage - 1] / 1e6 : 0;
    printf("%-18s %6llu %8llu %8llu %7llu %6llu %8.1f %8.1f %8.1f\n", POLICY_NAME[pol],
           (unsigned long long)c.frames, (unsigned long long)c.dequeued, (unsigned long long)c.dropped,
           (unsigned long long)c.flushed, (unsigned long long)u.gap.missed, p50, p99, mx);
    pthread_mutex_destroy(&a.m);
    cap_destroy(&c);
}

int main(void) {
    printf("%-18s %6s %8s %8s %7s %6s %8s %8s %8s\n", "policy", "frames", "delivered", "dropped", "flushed",
           "gaps", "p50_ms", "p99_ms", "max_ms");
    for (int p = 0; p < POLICY_COUNT; p++) run((policy_t)p);
    return 0;
}
