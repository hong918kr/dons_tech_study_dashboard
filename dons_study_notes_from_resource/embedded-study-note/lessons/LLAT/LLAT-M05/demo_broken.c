/* LLAT-M05 demo_broken.c — 메일박스 seq 를 relaxed 로 바꾸면? (관찰용, CHECK 없음)
 *
 *   ./broken [calls]      relaxed seq 로 calls 번 RPC → 답이 틀린 횟수 (하드웨어 재배치가 실제로 보였나)
 *   TSan 빌드             payload(평범한 변수)에 대한 data race 보고
 *
 * build: cc -std=c11 -Wall -Wextra -O2 -pthread demo_broken.c -o broken && ./broken 2000000
 *        cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_broken.c -o broken_tsan && ./broken_tsan 1000
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define LINE 128
typedef struct {
    _Alignas(LINE) struct { _Atomic uint64_t seq; uint32_t arg[4]; } req;
    _Alignas(LINE) struct { _Atomic uint64_t seq; uint32_t result; } rsp;
} mbx_t;

static mbx_t g_m;
static long g_calls;

static void *server(void *arg) {
    (void)arg;
    for (uint64_t s = 1; s <= (uint64_t)g_calls; s++) {
        while (atomic_load_explicit(&g_m.req.seq, memory_order_relaxed) != s) { }     /* 버그: acquire 아님 */
        uint32_t r = g_m.req.arg[0] + g_m.req.arg[1] + g_m.req.arg[2] + g_m.req.arg[3];
        g_m.rsp.result = r;
        atomic_store_explicit(&g_m.rsp.seq, s, memory_order_relaxed);                 /* 버그: release 아님 */
    }
    return NULL;
}

int main(int argc, char **argv) {
    g_calls = argc > 1 ? atol(argv[1]) : 2000000;
    pthread_t t; pthread_create(&t, NULL, server, NULL);
    uint32_t x = 12345; long bad = 0;
    for (uint64_t s = 1; s <= (uint64_t)g_calls; s++) {
        uint32_t sum = 0;
        for (int i = 0; i < 4; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; g_m.req.arg[i] = x & 0xFFFF; sum += x & 0xFFFF; }
        atomic_store_explicit(&g_m.req.seq, s, memory_order_relaxed);                 /* 버그: release 아님 */
        while (atomic_load_explicit(&g_m.rsp.seq, memory_order_relaxed) != s) { }     /* 버그: acquire 아님 */
        if (g_m.rsp.result != sum) bad++;
    }
    pthread_join(t, NULL);
    printf("relaxed mailbox: %ld calls, wrong results %ld\n", g_calls, bad);
    return 0;
}
