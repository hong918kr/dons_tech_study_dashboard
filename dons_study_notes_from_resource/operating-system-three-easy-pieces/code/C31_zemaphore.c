// C31_zemaphore.c — Ch.31 세마포어를 macOS 에서 실제로 돌리기.
//  0) macOS 의 unnamed POSIX 세마포어 sem_init() 은 미지원(-1, errno=ENOSYS) 임을 확인
//  1) 책 Fig 31.16 의 Zemaphore(mutex + cond + value) 를 직접 구현
//  2) 이진 세마포어 = 락 (초기값 1)        : 4 스레드 x 1,000,000 증가 → 정확히 4,000,000
//  3) 순서 맞추기 (초기값 0)              : parent 가 child 를 기다림
//  4) bounded buffer (Fig 31.12, 정답 버전): empty=MAX, full=0, mutex=1
//  5) 잘못된 버전 (Fig 31.11, mutex 를 바깥에서 잡음) → 교착. 감시 스레드가 진행이 멈춘 걸 감지
//
// build: cc -Wall -Wextra -O0 -pthread code/C31_zemaphore.c -o .work/bin/C31_zemaphore
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ---------------- Zemaphore (Fig 31.16) ----------------
typedef struct {
    int value;
    pthread_cond_t cond;
    pthread_mutex_t lock;
} Zem_t;

static void Zem_init(Zem_t *s, int value) {
    s->value = value;
    pthread_cond_init(&s->cond, NULL);
    pthread_mutex_init(&s->lock, NULL);
}
static void Zem_wait(Zem_t *s) {
    pthread_mutex_lock(&s->lock);
    while (s->value <= 0)                 // 값이 0 이하면 잔다 (값은 절대 음수가 되지 않음)
        pthread_cond_wait(&s->cond, &s->lock);
    s->value--;
    pthread_mutex_unlock(&s->lock);
}
static void Zem_post(Zem_t *s) {
    pthread_mutex_lock(&s->lock);
    s->value++;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->lock);
}

// ---------------- 0) sem_init on macOS ----------------
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static void check_sem_init(void) {
    sem_t s;
    errno = 0;
    int rc = sem_init(&s, 0, 1);
    printf("[0] sem_init(&s, 0, 1) = %d, errno = %d (%s)\n", rc, errno, strerror(errno));
}
#pragma clang diagnostic pop

// ---------------- 2) binary semaphore as lock ----------------
static Zem_t lock_sem;
static long counter = 0;
static void *adder(void *arg) {
    (void)arg;
    for (int i = 0; i < 1000000; i++) {
        Zem_wait(&lock_sem);
        counter++;                         // critical section
        Zem_post(&lock_sem);
    }
    return NULL;
}

// ---------------- 3) ordering (parent waits for child) ----------------
static Zem_t done_sem;
static void *child(void *arg) {
    (void)arg;
    usleep(30000);
    printf("    child\n");
    Zem_post(&done_sem);                   // "끝났다" 신호
    return NULL;
}

// ---------------- 4/5) bounded buffer ----------------
#define MAX 4
static int buffer[MAX];
static int fill_i = 0, use_i = 0;
static Zem_t empty, full, mutex;
static volatile long progress = 0;        // 감시용: 처리된 아이템 수
static int loops = 100000;

static void put(int v) { buffer[fill_i] = v; fill_i = (fill_i + 1) % MAX; }
static int  get(void)  { int t = buffer[use_i]; use_i = (use_i + 1) % MAX; return t; }

static void *producer_ok(void *arg) {
    (void)arg;
    for (int i = 1; i <= loops; i++) {
        Zem_wait(&empty);                 // p1   빈 칸 하나 예약
        Zem_wait(&mutex);                 // p1.5 put 만 짧게 보호
        put(i);                           // p2
        Zem_post(&mutex);                 // p2.5
        Zem_post(&full);                  // p3   채워진 칸 하나 생김
    }
    return NULL;
}
static void *consumer_ok(void *arg) {
    long *sum = arg;
    for (int i = 0; i < loops; i++) {
        Zem_wait(&full);                  // c1
        Zem_wait(&mutex);                 // c1.5
        int tmp = get();                  // c2
        Zem_post(&mutex);                 // c2.5
        Zem_post(&empty);                 // c3
        *sum += tmp; progress++;
    }
    return NULL;
}
// Fig 31.11: mutex 를 empty/full 보다 먼저 잡는다 → 교착
static void *producer_bad(void *arg) {
    (void)arg;
    for (int i = 1; i <= loops; i++) {
        Zem_wait(&mutex);                 // p0 (NEW LINE)
        Zem_wait(&empty);                 // p1
        put(i);
        Zem_post(&full);
        Zem_post(&mutex);
    }
    return NULL;
}
static void *consumer_bad(void *arg) {
    long *sum = arg;
    for (int i = 0; i < loops; i++) {
        Zem_wait(&mutex);                 // c0 (NEW LINE) — 락을 쥔 채로
        Zem_wait(&full);                  // c1 — 데이터가 없으면 여기서 잠듦 (락은 계속 쥠!)
        int tmp = get();
        Zem_post(&empty);
        Zem_post(&mutex);
        *sum += tmp; progress++;
    }
    return NULL;
}

static void run_bb(int bad) {
    Zem_init(&empty, MAX); Zem_init(&full, 0); Zem_init(&mutex, 1);
    fill_i = use_i = 0; progress = 0;
    long sum = 0;
    pthread_t p, c;
    // 소비자를 먼저 띄워서 "빈 버퍼에서 소비자가 먼저 mutex 를 잡는" 상황을 만든다
    pthread_create(&c, NULL, bad ? consumer_bad : consumer_ok, &sum);
    usleep(20000);
    pthread_create(&p, NULL, bad ? producer_bad : producer_ok, NULL);

    long last = -1;
    for (int tick = 0; tick < 40; tick++) {   // 최대 2초 감시 (50ms 간격)
        usleep(50000);
        long cur = progress;
        if (cur == loops) break;
        if (cur == last) {
            printf("    watchdog: no progress for 50ms at %ld/%d items, mutex.value=%d full.value=%d empty.value=%d -> DEADLOCK\n",
                   cur, loops, mutex.value, full.value, empty.value);
            return;                         // 스레드는 영원히 잠들어 있음; 데모이므로 그냥 둔다
        }
        last = cur;
    }
    pthread_join(p, NULL); pthread_join(c, NULL);
    long expect = (long)loops * (loops + 1) / 2;
    printf("    consumed %ld items, sum=%ld (expected %ld) -> %s\n",
           progress, sum, expect, sum == expect ? "OK" : "MISMATCH");
}

int main(void) {
    check_sem_init();

    Zem_init(&lock_sem, 1);
    pthread_t t[4];
    for (int i = 0; i < 4; i++) pthread_create(&t[i], NULL, adder, NULL);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    printf("[2] binary Zemaphore as lock: counter = %ld (expected 4000000)\n", counter);

    printf("[3] ordering with Zemaphore initialised to 0:\n");
    Zem_init(&done_sem, 0);
    printf("    parent: begin\n");
    pthread_t ch;
    pthread_create(&ch, NULL, child, NULL);
    Zem_wait(&done_sem);
    printf("    parent: end\n");
    pthread_join(ch, NULL);

    printf("[4] bounded buffer, mutex INSIDE (Fig 31.12):\n");
    run_bb(0);
    printf("[5] bounded buffer, mutex OUTSIDE (Fig 31.11):\n");
    run_bb(1);
    return 0;                              // 교착된 스레드는 프로세스 종료와 함께 사라진다
}
