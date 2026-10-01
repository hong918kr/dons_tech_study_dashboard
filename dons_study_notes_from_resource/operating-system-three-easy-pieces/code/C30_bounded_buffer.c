// C30_bounded_buffer.c — Ch.30 Fig 30.11/30.12 의 "최종 정답" bounded buffer 를 실제로 돌린다.
//  - 생산자 P개, 소비자 C개, 버퍼 슬롯 MAX개, 조건변수 2개(empty, fill), while 로 재확인
//  - 모든 아이템이 정확히 한 번씩 소비됐는지 합계/개수로 검증
//  - Mesa 의미론의 증거: "깨어났는데 조건이 여전히 거짓이라 다시 잠든 횟수"(recheck_fail)를 센다
//  - 인자 "broadcast" 를 주면 signal 대신 broadcast 를 써서 헛깨움(wasted wakeup)이 얼마나 늘어나는지 비교
//
// build: cc -Wall -Wextra -O0 -pthread code/C30_bounded_buffer.c -o .work/bin/C30_bounded_buffer
// run  : .work/bin/C30_bounded_buffer            (signal)
//        .work/bin/C30_bounded_buffer broadcast  (broadcast)
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define MAX        4          // 버퍼 슬롯 수
#define NPROD      2
#define NCONS      3
#define LOOPS      200000     // 생산자 1명당 아이템 수
#define EOS        (-1)       // end-of-stream 표식 (소비자 종료용)

static int buffer[MAX];
static int fill_ptr = 0, use_ptr = 0, count = 0;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  empty = PTHREAD_COND_INITIALIZER;  // "빈 칸 생김"  — 생산자가 기다림
static pthread_cond_t  fill  = PTHREAD_COND_INITIALIZER;  // "채워진 칸 생김" — 소비자가 기다림
static int use_broadcast = 0;

// 통계 (mutex 안에서만 갱신)
static long prod_waits = 0, cons_waits = 0;           // cond_wait 호출 횟수
static long prod_recheck_fail = 0, cons_recheck_fail = 0; // 깨어났는데 조건이 또 거짓

static void put(int v) { buffer[fill_ptr] = v; fill_ptr = (fill_ptr + 1) % MAX; count++; }
static int  get(void)  { int t = buffer[use_ptr]; use_ptr = (use_ptr + 1) % MAX; count--; return t; }

static void wake(pthread_cond_t *cv) {
    if (use_broadcast) pthread_cond_broadcast(cv); else pthread_cond_signal(cv);
}

static void produce_one(int v) {
    pthread_mutex_lock(&mutex);                    // p1
    int woke = 0;
    while (count == MAX) {                         // p2  (if 가 아니라 while!)
        if (woke) prod_recheck_fail++;
        prod_waits++;
        pthread_cond_wait(&empty, &mutex);         // p3
        woke = 1;
    }
    put(v);                                        // p4
    wake(&fill);                                   // p5  생산자는 fill 만 깨운다
    pthread_mutex_unlock(&mutex);                  // p6
}

static void *producer(void *arg) {
    long id = (long)arg;
    for (int i = 0; i < LOOPS; i++)
        produce_one((int)(id * LOOPS + i + 1));    // 1..NPROD*LOOPS, 모두 다른 값
    return NULL;
}

typedef struct { long sum; long n; } cons_result_t;

static void *consumer(void *arg) {
    cons_result_t *r = arg;
    for (;;) {
        pthread_mutex_lock(&mutex);                // c1
        int woke = 0;
        while (count == 0) {                       // c2
            if (woke) cons_recheck_fail++;
            cons_waits++;
            pthread_cond_wait(&fill, &mutex);      // c3
            woke = 1;
        }
        int tmp = get();                           // c4
        wake(&empty);                              // c5  소비자는 empty 만 깨운다
        pthread_mutex_unlock(&mutex);              // c6
        if (tmp == EOS) break;
        r->sum += tmp; r->n++;
    }
    return NULL;
}

static double now_s(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec + tv.tv_usec / 1e6; }

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "broadcast") == 0) use_broadcast = 1;
    pthread_t p[NPROD], c[NCONS];
    cons_result_t res[NCONS];
    memset(res, 0, sizeof res);

    double t0 = now_s();
    for (long i = 0; i < NCONS; i++) pthread_create(&c[i], NULL, consumer, &res[i]);
    for (long i = 0; i < NPROD; i++) pthread_create(&p[i], NULL, producer, (void *)i);
    for (int i = 0; i < NPROD; i++) pthread_join(p[i], NULL);
    for (int i = 0; i < NCONS; i++) produce_one(EOS);   // 소비자마다 EOS 하나씩
    for (int i = 0; i < NCONS; i++) pthread_join(c[i], NULL);
    double t1 = now_s();

    long total_n = 0, total_sum = 0;
    for (int i = 0; i < NCONS; i++) {
        printf("consumer %d: items=%ld\n", i, res[i].n);
        total_n += res[i].n; total_sum += res[i].sum;
    }
    long N = (long)NPROD * LOOPS;
    long expect = N * (N + 1) / 2;
    printf("mode=%s MAX=%d producers=%d consumers=%d\n", use_broadcast ? "broadcast" : "signal", MAX, NPROD, NCONS);
    printf("items consumed=%ld (expected %ld), sum=%ld (expected %ld) -> %s\n",
           total_n, N, total_sum, expect, (total_n == N && total_sum == expect) ? "OK" : "MISMATCH");
    printf("producer waits=%ld  re-slept after wakeup=%ld\n", prod_waits, prod_recheck_fail);
    printf("consumer waits=%ld  re-slept after wakeup=%ld\n", cons_waits, cons_recheck_fail);
    printf("elapsed=%.3fs\n", t1 - t0);
    return 0;
}
