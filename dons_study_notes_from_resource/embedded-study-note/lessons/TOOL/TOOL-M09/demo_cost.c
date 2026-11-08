/* TOOL-M09 demo_cost.c — TSan 비용: 같은 작업을 TSan 없이/있이 돌려 시간·메모리 비교
 *
 * 작업: 4 스레드가 각자 8 MB 샘플 버퍼를 채우고(일반 메모리 접근) 필터링한 뒤,
 *       1024 샘플마다 뮤텍스로 보호된 공유 히스토그램에 합친다 (레이스 없음).
 *
 *   cc -std=c11 -O1 -g -pthread demo_cost.c -o dcost && /usr/bin/time -l ./dcost
 *   cc -std=c11 -O1 -g -fsanitize=thread -pthread demo_cost.c -o dcost_t && /usr/bin/time -l ./dcost_t
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NT       4
#define NSAMP    (2u * 1024u * 1024u)          /* 스레드당 2M 샘플 × 4 B = 8 MB */
#define PASSES   8
#define NBIN     256

static pthread_mutex_t g_hist_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_hist[NBIN];

static void *worker(void *arg) {
    uint32_t seed = (uint32_t)(uintptr_t)arg * 2654435761u + 1u;
    uint32_t *buf = malloc(NSAMP * sizeof *buf);
    if (!buf) return NULL;
    for (unsigned p = 0; p < PASSES; p++) {
        for (uint32_t i = 0; i < NSAMP; i++) {             /* "ADC 샘플" 생성 */
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            buf[i] = seed;
        }
        uint32_t local[NBIN] = {0};
        for (uint32_t i = 1; i < NSAMP; i++) {             /* 간단한 IIR 필터 + 로컬 히스토그램 */
            buf[i] = (buf[i] >> 1) + (buf[i - 1] >> 1);
            local[buf[i] >> 24]++;
            if ((i & 1023u) == 0) {                        /* 1024 샘플마다 공유 히스토그램에 합침 */
                pthread_mutex_lock(&g_hist_lock);
                for (unsigned b = 0; b < NBIN; b++) { g_hist[b] += local[b]; local[b] = 0; }
                pthread_mutex_unlock(&g_hist_lock);
            }
        }
    }
    free(buf);
    return NULL;
}

int main(void) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    pthread_t t[NT];
    for (uintptr_t i = 0; i < NT; i++) pthread_create(&t[i], NULL, worker, (void *)(i + 1));
    for (int i = 0; i < NT; i++) pthread_join(t[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t total = 0;
    for (unsigned b = 0; b < NBIN; b++) total += g_hist[b];
    double ms = (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
    printf("merged=%llu wall=%.1f ms\n", (unsigned long long)total, ms);
    return 0;
}
