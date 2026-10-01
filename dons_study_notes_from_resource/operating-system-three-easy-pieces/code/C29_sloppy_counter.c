// C29_sloppy_counter.c — OSTEP Fig 29.2(precise) / 29.5(sloppy) 카운터를 Apple M2 에서 측정
//  실험 1: 스레드 수 1,2,4,8 — 각 스레드 1,000,000 회 증가. precise vs atomic vs sloppy(S=1024)
//  실험 2: 4 스레드, S(sloppiness) = 1 … 1024 스윕 (Fig 29.6 재현)
//  실험 3: sloppy 의 로컬 카운터를 캐시라인(128B)에 따로 두지 않으면? (false sharing)
// build: cc -Wall -Wextra -O2 -pthread code/C29_sloppy_counter.c -o .work/bin/C29_sloppy_counter
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUMCPUS 8
#define LOOPS 1000000

// ---------------- precise: 락 하나 (Fig 29.2)
typedef struct { int value; pthread_mutex_t lock; } precise_t;
static precise_t P;
static void precise_inc(void) {
    pthread_mutex_lock(&P.lock);
    P.value++;
    pthread_mutex_unlock(&P.lock);
}

// ---------------- atomic: 락 없이 하드웨어 fetch-and-add 1개
static atomic_int A;

// ---------------- sloppy (Fig 29.5). PADDED=1 이면 로컬 슬롯마다 캐시라인 하나씩
typedef struct {
    _Alignas(128) int value;
    pthread_mutex_t lock;
} padded_local_t;
typedef struct {
    int value;
    pthread_mutex_t lock;
} packed_local_t;

typedef struct {
    int global;
    pthread_mutex_t glock;
    padded_local_t local[NUMCPUS];
    int threshold;
} sloppy_t;
typedef struct {
    int global;
    pthread_mutex_t glock;
    packed_local_t local[NUMCPUS];      // 8 슬롯이 몇 개 캐시라인에 다닥다닥
    int threshold;
} sloppy_packed_t;

static sloppy_t S;
static sloppy_packed_t SP;

#define SLOPPY_UPDATE(C, tid, amt)                                  \
    do {                                                            \
        int cpu = (tid) % NUMCPUS;                                  \
        pthread_mutex_lock(&(C).local[cpu].lock);                   \
        (C).local[cpu].value += (amt);                              \
        if ((C).local[cpu].value >= (C).threshold) {                \
            pthread_mutex_lock(&(C).glock);                         \
            (C).global += (C).local[cpu].value;                     \
            pthread_mutex_unlock(&(C).glock);                       \
            (C).local[cpu].value = 0;                               \
        }                                                           \
        pthread_mutex_unlock(&(C).local[cpu].lock);                 \
    } while (0)

static int sloppy_get(void) {          // 근사값: global 만 본다
    pthread_mutex_lock(&S.glock);
    int v = S.global;
    pthread_mutex_unlock(&S.glock);
    return v;
}

static void counters_init(int threshold) {
    P.value = 0;
    pthread_mutex_init(&P.lock, NULL);
    atomic_store(&A, 0);
    S.global = 0; S.threshold = threshold;
    SP.global = 0; SP.threshold = threshold;
    pthread_mutex_init(&S.glock, NULL);
    pthread_mutex_init(&SP.glock, NULL);
    for (int i = 0; i < NUMCPUS; i++) {
        S.local[i].value = 0;  pthread_mutex_init(&S.local[i].lock, NULL);
        SP.local[i].value = 0; pthread_mutex_init(&SP.local[i].lock, NULL);
    }
}

enum { PRECISE, ATOMIC, SLOPPY, SLOPPY_PACKED };
static int kind;
static atomic_int go;

static void *worker(void *arg) {
    int tid = (int)(long)arg;
    while (!atomic_load(&go)) ;
    for (int i = 0; i < LOOPS; i++) {
        switch (kind) {
        case PRECISE: precise_inc(); break;
        case ATOMIC:  atomic_fetch_add_explicit(&A, 1, memory_order_relaxed); break;
        case SLOPPY:  SLOPPY_UPDATE(S, tid, 1); break;
        default:      SLOPPY_UPDATE(SP, tid, 1); break;
        }
    }
    return NULL;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// 한 번 돌리고 걸린 시간(초)을 돌려준다. *final 에 최종 값.
static double run(int k, int nthreads, int threshold, int *final) {
    pthread_t t[NUMCPUS];
    counters_init(threshold);
    kind = k;
    atomic_store(&go, 0);
    for (long i = 0; i < nthreads; i++) pthread_create(&t[i], NULL, worker, (void *)i);
    double t0 = now();
    atomic_store(&go, 1);
    for (int i = 0; i < nthreads; i++) pthread_join(t[i], NULL);
    double dt = now() - t0;
    if (k == PRECISE) *final = P.value;
    else if (k == ATOMIC) *final = atomic_load(&A);
    else if (k == SLOPPY) *final = sloppy_get();
    else *final = SP.global;
    return dt;
}

int main(void) {
    int v;
    run(PRECISE, 1, 1, &v);            // 워밍업

    printf("== 실험 1: 스레드당 %d 회 증가, 걸린 시간 (초)\n", LOOPS);
    printf("threads   precise(mutex)   atomic(ldadd)   sloppy(S=1024)   sloppy get() / 진짜 합\n");
    for (int n = 1; n <= 8; n *= 2) {
        double tp = run(PRECISE, n, 1024, &v);
        double ta = run(ATOMIC, n, 1024, &v);
        double ts = run(SLOPPY, n, 1024, &v);
        printf("%7d   %14.3f   %13.3f   %14.3f   %d / %d\n", n, tp, ta, ts, v, n * LOOPS);
    }

    printf("\n== 실험 2: 4 스레드, sloppiness S 스윕 (Fig 29.6)\n");
    printf("     S    time(s)   get()       오차(진짜 4000000 - get)\n");
    for (int s = 1; s <= 1024; s *= 2) {
        double ts = run(SLOPPY, 4, s, &v);
        printf("%6d   %8.3f   %8d   %8d\n", s, ts, v, 4 * LOOPS - v);
    }

    printf("\n== 실험 3: 4 스레드, S=1024, 로컬 카운터 배치에 따른 차이\n");
    double a = run(SLOPPY, 4, 1024, &v);
    double b = run(SLOPPY_PACKED, 4, 1024, &v);
    printf("padded (슬롯당 128B 캐시라인) : %.3f s\n", a);
    printf("packed (sizeof slot = %zu B)  : %.3f s   → %.1fx 느림\n",
           sizeof(packed_local_t), b, b / a);
    return 0;
}
