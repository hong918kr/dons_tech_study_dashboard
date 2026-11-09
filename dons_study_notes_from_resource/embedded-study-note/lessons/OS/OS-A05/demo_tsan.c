/* OS-A05 demo_tsan — push 의 release / steal 의 acquire 를 빼면 TSan 이 무엇을 잡나
 *
 * 태스크 내용(job_t 의 일반 필드)은 주인이 쓰고 push, 도둑이 steal 후 읽는다.
 *   기본     : push 의 bottom store = relaxed, steal 의 bottom load = relaxed  (고장: 내용 쓰기 → 도둑 읽기 사이 happens-before 없음)
 *   -DFIXED  : release / acquire  (solution.c 와 같음)
 * 슬롯 자체는 atomic(relaxed) 이므로 TSan 이 보고하는 것은 "태스크 내용" 의 레이스다 — 진짜 버그 위치.
 *
 * build: cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_tsan.c -o demo_tsan && ./demo_tsan            (보고 → exit 66/134)
 *        cc -std=c11 -g -O1 -fsanitize=thread -pthread -DFIXED demo_tsan.c -o demo_tsan_ok && ./demo_tsan_ok (깨끗)
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef FIXED
#  define MO_PUB memory_order_release
#  define MO_SUB memory_order_acquire
#else
#  define MO_PUB memory_order_relaxed
#  define MO_SUB memory_order_relaxed
#endif

typedef struct { int64_t lo, hi; int64_t out; } job_t;     /* 일반(non-atomic) 필드 */
#define CAP 64
#define N 20000
static struct {
    _Alignas(128) _Atomic(int64_t) top;
    _Alignas(128) _Atomic(int64_t) bottom;
    _Atomic(job_t *) buf[CAP];
} q;
static job_t jobs[N];
static atomic_int finished;
static atomic_long checksum;

static int push(job_t *x) {
    int64_t b = atomic_load_explicit(&q.bottom, memory_order_relaxed);
    int64_t t = atomic_load_explicit(&q.top, memory_order_acquire);
    if (b - t >= CAP) return 0;
    atomic_store_explicit(&q.buf[b & (CAP - 1)], x, memory_order_relaxed);
    atomic_store_explicit(&q.bottom, b + 1, MO_PUB);
    return 1;
}
static job_t *steal(void) {
    int64_t t = atomic_load_explicit(&q.top, memory_order_acquire);
    atomic_thread_fence(memory_order_seq_cst);                    /* TSan 은 독립 펜스를 모델링하지 않는다 */
    int64_t b = atomic_load_explicit(&q.bottom, MO_SUB);
    if (t >= b) return NULL;
    job_t *x = atomic_load_explicit(&q.buf[t & (CAP - 1)], memory_order_relaxed);
    if (!atomic_compare_exchange_strong_explicit(&q.top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed))
        return NULL;
    return x;
}
static void *thief(void *arg) {
    (void)arg;
    for (;;) {
        job_t *j = steal();
        if (j) { atomic_fetch_add_explicit(&checksum, j->hi - j->lo, memory_order_relaxed); continue; }   /* 내용 읽기 */
        if (atomic_load_explicit(&finished, memory_order_acquire) && atomic_load(&q.top) >= atomic_load(&q.bottom)) break;
    }
    return NULL;
}
int main(void) {
    pthread_t th[2];
    for (int i = 0; i < 2; i++) pthread_create(&th[i], NULL, thief, NULL);
    for (int i = 0; i < N; i++) {
        jobs[i].lo = i; jobs[i].hi = i + 3;                       /* 내용 쓰기 */
        while (!push(&jobs[i])) { }                                /* 가득 차면 도둑이 비울 때까지 */
    }
    atomic_store_explicit(&finished, 1, memory_order_release);
    for (int i = 0; i < 2; i++) pthread_join(th[i], NULL);
    printf("checksum %ld (want %d)\n", atomic_load(&checksum), 3 * N);
    return 0;
}
