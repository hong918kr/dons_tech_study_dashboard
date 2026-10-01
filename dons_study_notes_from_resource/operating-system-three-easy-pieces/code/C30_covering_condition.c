// C30_covering_condition.c — Ch.30.3 Fig 30.13 메모리 할당기 예제.
//  Ta: allocate(100), Tb: allocate(10) 가 둘 다 잠든 뒤 Tc 가 free(50) 을 한다.
//  - signal 버전: Ta 가 깨어나면 50<100 이라 다시 잠들고, 정작 깨어나야 할 Tb 는 계속 잔다.
//  - broadcast 버전(covering condition): 둘 다 깨우고, 각자 조건을 재확인 → Tb 성공, Ta 는 다시 잠듦.
//  영원히 자는 걸 데모에서 보이기 위해 allocate 의 wait 는 1초 timedwait 로 "포기"를 감지한다.
//
// build: cc -Wall -Wextra -O0 -pthread code/C30_covering_condition.c -o .work/bin/C30_covering_condition
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static int bytes_left = 0;                     // 처음엔 빈 힙
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  c = PTHREAD_COND_INITIALIZER;
static int use_broadcast = 0;
static struct timeval t0;

static double ms(void) {
    struct timeval t; gettimeofday(&t, NULL);
    return (t.tv_sec - t0.tv_sec) * 1e3 + (t.tv_usec - t0.tv_usec) / 1e3;
}

static int allocate(const char *who, int size) {      // 1=성공, 0=1초 안에 못 받음
    struct timeval now; struct timespec dl;
    pthread_mutex_lock(&m);
    gettimeofday(&now, NULL);
    dl.tv_sec = now.tv_sec + 1; dl.tv_nsec = now.tv_usec * 1000;
    while (bytes_left < size) {
        printf("%7.1fms %s: need %d, have %d -> sleep\n", ms(), who, size, bytes_left);
        if (pthread_cond_timedwait(&c, &m, &dl) == ETIMEDOUT) {
            printf("%7.1fms %s: gave up after 1s (would sleep FOREVER)\n", ms(), who);
            pthread_mutex_unlock(&m);
            return 0;
        }
        printf("%7.1fms %s: woke up, re-check\n", ms(), who);
    }
    bytes_left -= size;
    printf("%7.1fms %s: got %d bytes, left %d\n", ms(), who, size, bytes_left);
    pthread_mutex_unlock(&m);
    return 1;
}

static void free_bytes(const char *who, int size) {
    pthread_mutex_lock(&m);
    bytes_left += size;
    printf("%7.1fms %s: free(%d) -> %s\n", ms(), who, size, use_broadcast ? "broadcast" : "signal");
    if (use_broadcast) pthread_cond_broadcast(&c); else pthread_cond_signal(&c);
    pthread_mutex_unlock(&m);
}

static void *ta(void *a) { (void)a; allocate("Ta", 100); return NULL; }
static void *tb(void *a) { (void)a; usleep(20000); allocate("Tb", 10); return NULL; }
static void *tc(void *a) { (void)a; usleep(50000); free_bytes("Tc", 50); return NULL; }

static void run(int bcast) {
    use_broadcast = bcast; bytes_left = 0;
    gettimeofday(&t0, NULL);
    printf("=== %s ===\n", bcast ? "pthread_cond_broadcast (covering condition)" : "pthread_cond_signal");
    pthread_t a, b, c3;
    pthread_create(&a, NULL, ta, NULL);
    pthread_create(&b, NULL, tb, NULL);
    pthread_create(&c3, NULL, tc, NULL);
    pthread_join(a, NULL); pthread_join(b, NULL); pthread_join(c3, NULL);
}

int main(void) {
    run(0);
    run(1);
    return 0;
}
