// C0D_monitor.c — 부록 D "모니터" 를 C(pthread)로 흉내 내기
//
//  [1] Figure D.2: mutex 하나 = 모니터 락. deposit/withdraw 를 4 스레드가 동시에 호출
//  [2] Figure D.4/D.5: Mesa semantics — signal 로 깨어났는데 조건이 이미 거짓인 경우가
//      실제로 몇 번 생기는지 센다. (if 로 썼다면 그 횟수만큼 빈 버퍼에서 꺼냈을 것)
//  [3] Figure D.7: 메모리 할당기 — signal() 하나로는 엉뚱한 스레드를 깨울 수 있다 → broadcast()
//  [4] Figure D.8: 모니터(mutex+cond)로 세마포어 만들기 → 그 세마포어를 락으로 사용
//
// build: cc -Wall -Wextra -O0 -pthread code/C0D_monitor.c -o .work/bin/C0D_monitor && .work/bin/C0D_monitor
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// ------------------------------------------------------------ [1] account monitor
typedef struct { int balance; pthread_mutex_t monitor; } account_t;
static account_t the_acct = {0, PTHREAD_MUTEX_INITIALIZER};
static int racy_balance = 0;

static void deposit(account_t *a, int amt)  { pthread_mutex_lock(&a->monitor); a->balance += amt; pthread_mutex_unlock(&a->monitor); }
static void withdraw(account_t *a, int amt) { pthread_mutex_lock(&a->monitor); a->balance -= amt; pthread_mutex_unlock(&a->monitor); }

static void *acct_worker(void *arg) {
    long id = (long)arg;
    for (int i = 0; i < 1000000; i++) {
        if (id % 2) { deposit(&the_acct, 2); racy_balance += 2; }   // racy_balance: 모니터 없이 같은 일
        else        { withdraw(&the_acct, 1); racy_balance -= 1; }
    }
    return NULL;
}

// ------------------------------------------------------------ [2] bounded buffer (Mesa)
#define MAX 1
static int buffer[MAX], fill_i, use_i, fullEntries;
static pthread_mutex_t bb = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t empty = PTHREAD_COND_INITIALIZER, full = PTHREAD_COND_INITIALIZER;
static long woke_but_empty, woke_but_full, consumed_sum;
#define ITEMS 200000
#define NCONS 4

static void produce(int x) {
    pthread_mutex_lock(&bb);
    int waited = 0;
    while (fullEntries == MAX) {                 // P0 (if → while)
        if (waited) woke_but_full++;
        pthread_cond_wait(&empty, &bb);          // P1
        waited = 1;
    }
    buffer[fill_i] = x; fill_i = (fill_i + 1) % MAX; fullEntries++;   // P2-P4
    pthread_cond_signal(&full);                  // P5
    pthread_mutex_unlock(&bb);
}
static int consume(void) {
    pthread_mutex_lock(&bb);
    int waited = 0;
    while (fullEntries == 0) {                   // C0 (if → while)
        if (waited) woke_but_empty++;            // 깨어났는데 또 비어 있음 = Figure D.4 상황
        pthread_cond_wait(&full, &bb);           // C1
        waited = 1;
    }
    int tmp = buffer[use_i]; use_i = (use_i + 1) % MAX; fullEntries--;  // C2-C4
    pthread_cond_signal(&empty);                 // C5
    pthread_mutex_unlock(&bb);
    return tmp;                                  // C6
}
static void *producer(void *arg) {
    (void)arg;
    for (int i = 1; i <= ITEMS; i++) produce(i);
    for (int i = 0; i < NCONS; i++) produce(-1);  // 종료 신호
    return NULL;
}
static void *consumer(void *arg) {
    (void)arg;
    long s = 0;
    for (int v; (v = consume()) != -1;) s += v;
    pthread_mutex_lock(&bb); consumed_sum += s; pthread_mutex_unlock(&bb);
    return NULL;
}

// ------------------------------------------------------------ [3] allocator: signal vs broadcast
static int available = 0, use_broadcast = 0;
static pthread_mutex_t am = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ac = PTHREAD_COND_INITIALIZER;

static void *alloc_thread(void *arg) {
    int size = (int)(long)arg;
    pthread_mutex_lock(&am);
    while (size > available) {
        pthread_cond_wait(&ac, &am);
        if (size > available) printf("    allocate(%d): 깨어났지만 available=%d → 다시 잠듦\n", size, available);
    }
    available -= size;
    printf("    allocate(%d): 성공 (남은 available=%d)\n", size, available);
    pthread_mutex_unlock(&am);
    return NULL;
}
static void my_free(int size) {
    pthread_mutex_lock(&am);
    available += size;
    if (use_broadcast) pthread_cond_broadcast(&ac); else pthread_cond_signal(&ac);
    pthread_mutex_unlock(&am);
}
static void allocator_demo(int bcast) {
    use_broadcast = bcast; available = 0;
    printf("  -- free() 가 %s 사용 --\n", bcast ? "broadcast()" : "signal()");
    pthread_t t20, t10;
    pthread_create(&t20, NULL, alloc_thread, (void *)20L); usleep(50000);   // 20 이 먼저 대기
    pthread_create(&t10, NULL, alloc_thread, (void *)10L); usleep(50000);
    printf("    free(15)\n");
    my_free(15);
    usleep(200000);
    pthread_mutex_lock(&am);
    printf("    200ms 뒤 available=%d %s\n", available,
           available == 15 ? "← 15바이트가 있는데 allocate(10) 이 아직 자고 있음!" : "");
    pthread_mutex_unlock(&am);
    use_broadcast = 1; my_free(100);   // 정리: 남은 스레드 모두 풀어 줌
    pthread_join(t20, NULL); pthread_join(t10, NULL);
}

// ------------------------------------------------------------ [4] semaphore from a monitor
typedef struct { int s; pthread_mutex_t m; pthread_cond_t c; } msem_t;
static void msem_init(msem_t *x, int v) { x->s = v; pthread_mutex_init(&x->m, NULL); pthread_cond_init(&x->c, NULL); }
static void msem_wait(msem_t *x) { pthread_mutex_lock(&x->m); while (x->s <= 0) pthread_cond_wait(&x->c, &x->m); x->s--; pthread_mutex_unlock(&x->m); }
static void msem_post(msem_t *x) { pthread_mutex_lock(&x->m); x->s++; pthread_cond_signal(&x->c); pthread_mutex_unlock(&x->m); }
static msem_t lock_sem;
static long shared_counter;
static void *sem_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 200000; i++) { msem_wait(&lock_sem); shared_counter++; msem_post(&lock_sem); }
    return NULL;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    pthread_t t[NCONS + 1];

    printf("[1] 모니터 계좌: 2 스레드 deposit(2)x1e6, 2 스레드 withdraw(1)x1e6 → 기대 2000000\n");
    for (long i = 0; i < 4; i++) pthread_create(&t[i], NULL, acct_worker, (void *)i);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    printf("  monitor balance = %d,  락 없는 racy balance = %d\n", the_acct.balance, racy_balance);

    printf("\n[2] bounded buffer (MAX=%d), 생산자 1 / 소비자 %d, %d개 아이템\n", MAX, NCONS, ITEMS);
    pthread_create(&t[0], NULL, producer, NULL);
    for (int i = 1; i <= NCONS; i++) pthread_create(&t[i], NULL, consumer, NULL);
    for (int i = 0; i <= NCONS; i++) pthread_join(t[i], NULL);
    long expect = (long)ITEMS * (ITEMS + 1) / 2;
    printf("  합계 %ld (기대 %ld) %s\n", consumed_sum, expect, consumed_sum == expect ? "OK" : "WRONG");
    printf("  소비자: 깨어났는데 버퍼가 비어 있던 횟수   = %ld  (if 였다면 빈 버퍼에서 꺼냈을 횟수)\n", woke_but_empty);
    printf("  생산자: 깨어났는데 버퍼가 꽉 차 있던 횟수 = %ld\n", woke_but_full);

    printf("\n[3] 메모리 할당기 (Figure D.7): allocate(20), allocate(10) 대기 중 free(15)\n");
    allocator_demo(0);
    allocator_demo(1);

    printf("\n[4] 모니터로 만든 세마포어(초기값 1)를 락으로: 4 스레드 x 200000 증가\n");
    msem_init(&lock_sem, 1);
    for (int i = 0; i < 4; i++) pthread_create(&t[i], NULL, sem_worker, NULL);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    printf("  shared_counter = %ld (기대 800000)\n", shared_counter);
    return 0;
}
