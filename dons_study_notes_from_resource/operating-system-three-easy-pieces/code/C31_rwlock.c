// C31_rwlock.c — Ch.31.5 reader-writer 락 (Fig 31.13) 을 Zemaphore 로 만들고,
// "읽는 쪽이 계속 들어오면 writer 가 굶는다(starvation)" 를 실제 시간으로 잰다.
// 그리고 turnstile(회전문) 세마포어 하나를 추가한 no-starve 버전과 비교한다 (Downey, Little Book of Semaphores).
//
// build: cc -Wall -Wextra -O0 -pthread code/C31_rwlock.c -o .work/bin/C31_rwlock
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct { int value; pthread_cond_t cond; pthread_mutex_t lock; } Zem_t;
static void Zem_init(Zem_t *s, int v) { s->value = v; pthread_cond_init(&s->cond, NULL); pthread_mutex_init(&s->lock, NULL); }
static void Zem_wait(Zem_t *s) { pthread_mutex_lock(&s->lock); while (s->value <= 0) pthread_cond_wait(&s->cond, &s->lock); s->value--; pthread_mutex_unlock(&s->lock); }
static void Zem_post(Zem_t *s) { pthread_mutex_lock(&s->lock); s->value++; pthread_cond_signal(&s->cond); pthread_mutex_unlock(&s->lock); }

typedef struct {
    Zem_t lock;        // readers 카운터 보호
    Zem_t writelock;   // writer 1명 또는 reader 여러 명
    Zem_t turnstile;   // no-starve 버전에서만 사용
    int readers;
    int nostarve;
} rwlock_t;

static void rwlock_init(rwlock_t *rw, int nostarve) {
    rw->readers = 0; rw->nostarve = nostarve;
    Zem_init(&rw->lock, 1); Zem_init(&rw->writelock, 1); Zem_init(&rw->turnstile, 1);
}
static void rwlock_acquire_readlock(rwlock_t *rw) {
    if (rw->nostarve) { Zem_wait(&rw->turnstile); Zem_post(&rw->turnstile); } // writer 가 대기 중이면 여기서 막힘
    Zem_wait(&rw->lock);
    rw->readers++;
    if (rw->readers == 1) Zem_wait(&rw->writelock);   // 첫 reader 가 writelock 획득
    Zem_post(&rw->lock);
}
static void rwlock_release_readlock(rwlock_t *rw) {
    Zem_wait(&rw->lock);
    rw->readers--;
    if (rw->readers == 0) Zem_post(&rw->writelock);   // 마지막 reader 가 반납
    Zem_post(&rw->lock);
}
static void rwlock_acquire_writelock(rwlock_t *rw) {
    if (rw->nostarve) Zem_wait(&rw->turnstile);        // 회전문을 잠가 새 reader 진입 차단
    Zem_wait(&rw->writelock);
    if (rw->nostarve) Zem_post(&rw->turnstile);
}
static void rwlock_release_writelock(rwlock_t *rw) { Zem_post(&rw->writelock); }

static rwlock_t rw;
static struct timeval t0;
static double ms(void) { struct timeval t; gettimeofday(&t, NULL); return (t.tv_sec - t0.tv_sec) * 1e3 + (t.tv_usec - t0.tv_usec) / 1e3; }

#define RUN_MS 500.0
static pthread_mutex_t stat_m = PTHREAD_MUTEX_INITIALIZER;
static int inside = 0, max_inside = 0;
static long reads_done = 0;

static void *reader(void *arg) {
    (void)arg;
    while (ms() < RUN_MS) {
        rwlock_acquire_readlock(&rw);
        pthread_mutex_lock(&stat_m); inside++; if (inside > max_inside) max_inside = inside; pthread_mutex_unlock(&stat_m);
        usleep(1000);                         // 1ms 동안 "읽기"
        pthread_mutex_lock(&stat_m); inside--; reads_done++; pthread_mutex_unlock(&stat_m);
        rwlock_release_readlock(&rw);
    }
    return NULL;
}
static void *writer(void *arg) {
    (void)arg;
    usleep(10000);                            // reader 들이 자리 잡은 뒤 10ms 에 도착
    double arrive = ms();
    rwlock_acquire_writelock(&rw);
    double got = ms();
    printf("    writer: arrived %.1fms, got lock %.1fms -> waited %.1fms\n", arrive, got, got - arrive);
    rwlock_release_writelock(&rw);
    return NULL;
}

static void run(int nostarve) {
    rwlock_init(&rw, nostarve);
    inside = max_inside = 0; reads_done = 0;
    gettimeofday(&t0, NULL);
    printf("=== %s ===\n", nostarve ? "no-starve rwlock (turnstile)" : "simple rwlock (Fig 31.13)");
    pthread_t r[4], w;
    for (int i = 0; i < 4; i++) pthread_create(&r[i], NULL, reader, NULL);
    pthread_create(&w, NULL, writer, NULL);
    for (int i = 0; i < 4; i++) pthread_join(r[i], NULL);
    pthread_join(w, NULL);
    printf("    readers: max concurrently inside = %d, total reads = %ld in %.0fms\n", max_inside, reads_done, RUN_MS);
}

int main(void) {
    run(0);
    run(1);
    return 0;
}
