/* TOOL-M10 demo_cost.c — 메모리 도구 비용 측정용 부하 (malloc 이 많은 "메시지 큐" 흉내)
 *   ring[1024] 에 32~95 바이트 메시지를 넣고, 꽉 차면 가장 오래된 것을 free.
 *   매 메시지를 채우고 체크섬을 낸다 → load/store + malloc/free 가 섞인 부하.
 * build: cc -std=c11 -g -O1 demo_cost.c -o cost && ./cost [N]
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RING 1024

int main(int argc, char **argv) {
    long n = argc > 1 ? atol(argv[1]) : 200000;
    unsigned char *ring[RING] = {0};
    unsigned long sum = 0, x = 12345;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (long i = 0; i < n; i++) {
        x = x * 1103515245u + 12345u;                     /* 결정적 의사 난수 */
        size_t len = 32 + (x >> 16) % 64;
        unsigned char *m = malloc(len);
        if (!m) return 1;
        for (size_t k = 0; k < len; k++) m[k] = (unsigned char)(i + k);
        for (size_t k = 0; k < len; k++) sum += m[k];
        int slot = (int)(i % RING);
        free(ring[slot]);
        ring[slot] = m;
    }
    for (int s = 0; s < RING; s++) free(ring[s]);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
    printf("n=%ld sum=%lu loop_ms=%.1f\n", n, sum, ms);
    return 0;
}
