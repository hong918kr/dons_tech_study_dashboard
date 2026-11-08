/* OS-M02 demo_affinity.c — 맥에서 "코어 고정" 을 시도하면? + QoS 는 코어 배치를 얼마나 바꾸나?
 *
 *  1) Mach THREAD_AFFINITY_POLICY (affinity tag) — 인텔 맥에서는 "같은 L2 를 공유하게 해 달라" 는 힌트였다.
 *     Apple Silicon 에서는 지원 안 함을 돌려준다 (반환값을 직접 확인).
 *  2) sched_setaffinity 는 macOS 에 아예 없다 (리눅스 전용). 코어 번호는 pthread_cpu_number_np 로 읽을 수만 있다.
 *  3) QoS 클래스(USER_INTERACTIVE / BACKGROUND)는 클러스터 추천일 뿐 — 바쁜 스레드가 실제로 어디서 도는지 센다.
 *     (이 맥 실측: cpu 0~5 = 느린 E 코어, 6~11 = P/Super 코어 — demo 출력의 chunk 시간으로 판단)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_affinity.c -o da && ./da
 */
#define _DARWIN_C_SOURCE
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/qos.h>
#include <time.h>

typedef struct { qos_class_t qos; uint64_t ms; unsigned seen[64]; uint64_t best[64]; } arg_t;

static void *busy(void *p) {
    arg_t *a = p;
    pthread_set_qos_class_self_np(a->qos, 0);
    uint64_t end = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) + a->ms * 1000000u;
    while (clock_gettime_nsec_np(CLOCK_UPTIME_RAW) < end) {
        size_t c0 = 0, c1 = 0;
        pthread_cpu_number_np(&c0);
        uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        for (volatile unsigned i = 0; i < 20000u; i++) { }
        uint64_t d = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0;
        pthread_cpu_number_np(&c1);
        if (c0 == c1 && c0 < 64) {
            a->seen[c0]++;
            if (!a->best[c0] || d < a->best[c0]) a->best[c0] = d;
        }
    }
    return NULL;
}

int main(void) {
    thread_affinity_policy_data_t pol = { 1 };
    kern_return_t kr = thread_policy_set(mach_thread_self(), THREAD_AFFINITY_POLICY, (thread_policy_t)&pol,
                                         THREAD_AFFINITY_POLICY_COUNT);
    printf("thread_policy_set(THREAD_AFFINITY_POLICY, tag=1) -> %d (%s)\n", kr, mach_error_string(kr));

    static arg_t runs[] = { { QOS_CLASS_USER_INTERACTIVE, 1000, {0}, {0} }, { QOS_CLASS_BACKGROUND, 1000, {0}, {0} } };
    for (int k = 0; k < 2; k++) {
        pthread_t t;
        pthread_create(&t, NULL, busy, &runs[k]);
        pthread_join(t, NULL);
        printf("%-16s busy 1 s:", k ? "BACKGROUND" : "USER_INTERACTIVE");
        for (int c = 0; c < 64; c++)
            if (runs[k].seen[c]) printf("  cpu%d x%u (best %llu ns)", c, runs[k].seen[c], (unsigned long long)runs[k].best[c]);
        printf("\n");
    }
    return 0;
}
