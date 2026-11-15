/* DS-A02 demo_smp.c — 같은 bip 알고리즘을 진짜 두 코어에서: 컴파일러 배리어만으로 되는가 (관찰용, CHECK 아님)
 *
 *   MODE=0  relaxed 인덱스 + signal fence (단일 코어 ISR 코드 그대로)
 *   MODE=1  acquire/release (w 는 release 게시 / acquire 관찰, r 도 마찬가지)
 *
 *   cc -std=c11 -O2 -pthread -DMODE=0 demo_smp.c -o ds0 && ./ds0
 *   cc -std=c11 -O2 -pthread -DMODE=1 demo_smp.c -o ds1 && ./ds1
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread -DMODE=1 -DMSGS=200000u demo_smp.c -o dst && ./dst
 *
 * 메시지: [seq u32][len u32][payload = seq 의 함수]. 소비자는 내용 전체를 검사하고 어긋난 메시지를 센다.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MODE
#define MODE 1
#endif
#ifndef MSGS
#define MSGS 20000000u
#endif
#define CAP 4096u

#if MODE == 1
#define ACQ memory_order_acquire
#define REL memory_order_release
#define FENCE_ACQ() ((void)0)
#define FENCE_REL() ((void)0)
#else
#define ACQ memory_order_relaxed
#define REL memory_order_relaxed
#define FENCE_ACQ() atomic_signal_fence(memory_order_acquire)
#define FENCE_REL() atomic_signal_fence(memory_order_release)
#endif

static uint8_t g_buf[CAP];
static _Alignas(128) _Atomic uint32_t g_r;
static _Alignas(128) _Atomic uint32_t g_w;
static _Alignas(128) _Atomic uint32_t g_wm;
static _Alignas(128) atomic_uint g_stop;

static uint32_t mix(uint32_t s, uint32_t i) { return (s * 2654435761u) ^ (i * 40503u); }

static void *producer(void *a) {
    (void)a;
    uint32_t rng = 0xC001D00Du, seq = 0;
    uint32_t w = 0;
    while (seq < MSGS) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        uint32_t n = 8u + (rng % 40u) * 4u;
        uint32_t r = atomic_load_explicit(&g_r, ACQ);
        FENCE_ACQ();
        uint32_t start; bool wrap = false;
        if (w < r) { if (n >= r - w) continue; start = w; }
        else if (CAP - w >= n) start = w;
        else if (n < r) { start = 0; wrap = true; }
        else continue;
        uint32_t *p = (uint32_t *)(void *)(g_buf + start);
        p[0] = seq; p[1] = n;
        for (uint32_t i = 2; i < n / 4u; i++) p[i] = mix(seq, i);
        if (wrap) atomic_store_explicit(&g_wm, w, memory_order_relaxed);
        FENCE_REL();
        w = start + n;
        atomic_store_explicit(&g_w, w, REL);
        seq++;
    }
    return NULL;
}
static uint64_t g_bad;
static void *consumer(void *a) {
    (void)a;
    uint32_t r = 0, expect = 0;
    uint64_t bad = 0;
    while (expect < MSGS) {
        uint32_t w = atomic_load_explicit(&g_w, ACQ);
        FENCE_ACQ();
        uint32_t limit = w;
        if (w < r) {
            uint32_t wm = atomic_load_explicit(&g_wm, memory_order_relaxed);
            if (r == wm) { r = 0; atomic_store_explicit(&g_r, 0u, REL); continue; }
            limit = wm;
        }
        if (limit == r) continue;
        const uint32_t *p = (const uint32_t *)(const void *)(g_buf + r);
        uint32_t s = p[0], n = p[1];
        bool ok = (s == expect) && n >= 8u && n <= 200u && r + n <= limit;
        if (ok) for (uint32_t i = 2; i < n / 4u; i++) if (p[i] != mix(s, i)) { ok = false; break; }
        if (!ok) {
            bad++;
            if (bad >= 1000u) { atomic_store(&g_stop, 1u); printf("  (첫 %llu 개 어긋남에서 중단, expect=%u)\n", (unsigned long long)bad, expect); g_bad = bad; return NULL; }
            n = (n >= 8u && n <= 200u && (n & 3u) == 0u && r + n <= limit) ? n : (limit - r);   /* 재동기화 시도 */
        }
        FENCE_REL();
        r += n;
        atomic_store_explicit(&g_r, r, REL);
        expect++;
    }
    g_bad = bad;
    return NULL;
}
int main(void) {
    pthread_t c, p;
    pthread_create(&c, NULL, consumer, NULL);
    pthread_create(&p, NULL, producer, NULL);
    pthread_join(c, NULL);
    /* 소비자가 일찍 멈췄을 때 생산자가 안 끝날 수 있으므로 stop 을 본 뒤 정리 */
    if (atomic_load(&g_stop)) { printf("MODE=%d messages checked until stop: bad %llu\n", MODE, (unsigned long long)g_bad); fflush(stdout); _Exit(0); }
    pthread_join(p, NULL);
    printf("MODE=%d %u messages: bad %llu\n", MODE, MSGS, (unsigned long long)g_bad);
    return 0;
}
