/* RTOS-B11 데모 3 — 복사 큐의 비용: 항목 크기 vs send+receive 1쌍의 시간 (호스트, 비율만 의미 있음)
 *
 * FreeRTOS 큐처럼 item_size 가 "실행 시간 값" 인 memcpy 로 넣고 꺼낸다 (prvCopyDataToQueue / FromQueue 와 같은 모양).
 * 락·임계 구역은 없다 — 순수 복사 비용 + 고정 비용(인덱스·분기)만 잰다.
 * 측정: 깊이 8 큐에 send 1번 + receive 1번을 ITER 번. 준비 운동 1번, 7번 재서 중앙값. 체크섬을 출력해 최적화로 지워지지 않게.
 * 이 맥 (Apple M-series, -O2) 의 숫자다. 절대값이 아니라 "포인터(8 B) 대비 몇 배" 를 본다.
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_copy_cost.c -o demo_copy_cost && ./demo_copy_cost
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { DEPTH = 8, ITER = 2000000, RUNS = 7, MAXSZ = 4096 };
typedef struct { unsigned char *buf; size_t item_size; unsigned len, count, rd, wr; } cq_t;

static int cq_send(cq_t *q, const void *item) {
    if (q->count == q->len) return 0;
    memcpy(q->buf + (size_t)q->wr * q->item_size, item, q->item_size);
    q->wr = (q->wr + 1 == q->len) ? 0 : q->wr + 1;
    q->count++;
    return 1;
}
static int cq_recv(cq_t *q, void *out) {
    if (q->count == 0) return 0;
    memcpy(out, q->buf + (size_t)q->rd * q->item_size, q->item_size);
    q->rd = (q->rd + 1 == q->len) ? 0 : q->rd + 1;
    q->count--;
    return 1;
}
static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}
static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static unsigned char g_store[DEPTH * MAXSZ], g_in[MAXSZ], g_out[MAXSZ];
static volatile size_t g_item_size;            /* 실행 시간 값 (컴파일러가 상수로 못 접게) */

static double measure(size_t sz, uint64_t *sink) {
    cq_t q = { g_store, sz, DEPTH, 0, 0, 0 };
    double t[RUNS];
    for (int r = -1; r < RUNS; r++) {          /* r = -1: 준비 운동 */
        double t0 = now_ns();
        for (int i = 0; i < ITER; i++) {
            g_in[0] = (unsigned char)i;
            cq_send(&q, g_in);
            cq_recv(&q, g_out);
            *sink += g_out[0] + g_out[sz - 1];
        }
        double dt = (now_ns() - t0) / ITER;
        if (r >= 0) t[r] = dt;
    }
    qsort(t, RUNS, sizeof t[0], cmp_d);
    return t[RUNS / 2];
}

int main(void) {
    static const size_t sizes[] = { 4, 8, 16, 64, 256, 1024, 4096 };
    uint64_t sink = 0;
    double base = 0;
    memset(g_in, 0x5A, sizeof g_in);
    printf("item B   ns per send+recv   x vs 8 B (pointer)   bytes copied per item\n");
    for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        g_item_size = sizes[k];
        double ns = measure(g_item_size, &sink);
        if (sizes[k] == 8) base = ns;
        printf("%5zu   %8.2f", sizes[k], ns);
        if (base > 0) printf("           %6.1f", ns / base); else printf("              -");
        printf("               %6zu\n", 2 * sizes[k]);
    }
    printf("(checksum %llu)\n", (unsigned long long)sink);
    return 0;
}
