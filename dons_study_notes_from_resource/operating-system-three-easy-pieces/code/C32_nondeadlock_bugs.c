// C32_nondeadlock_bugs.c — Ch.32.2 비교착 버그 2종을 "크래시 없이" 관찰 가능하게 재현한다.
//  (A) atomicity violation (MySQL proc_info 패턴):
//      T1: if (p != NULL) { ...; use(p); }      T2: p = NULL; ...; p = buf;
//      check 와 use 사이에 p 가 NULL 로 바뀌면 진짜 코드는 fputs(NULL) 로 죽는다.
//      여기선 use 직전에 p 를 다시 읽어 NULL 이면 "죽었을 횟수" 로 센다.
//  (B) order violation (Mozilla mThread 패턴):
//      init() 이 스레드를 만들고 나서 mThread 에 대입하는데, 새 스레드가 먼저 mThread->State 를 읽는다.
//      여기선 mThread 가 아직 NULL 인 걸 본 횟수를 센다. 고친 버전은 CV 로 순서를 강제.
//
// build: cc -Wall -Wextra -O0 -pthread code/C32_nondeadlock_bugs.c -o .work/bin/C32_nondeadlock_bugs
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// ---------------- (A) atomicity violation ----------------
#define A_LOOPS 2000000
static char info_buf[] = "running query";
static char *volatile proc_info = info_buf;          // 공유 포인터 (volatile: 컴파일러가 재읽기를 없애지 않도록)
static pthread_mutex_t proc_info_lock = PTHREAD_MUTEX_INITIALIZER;
static int use_lock_A = 0;
static long would_crash = 0, used_ok = 0;
static volatile int a_stop = 0;

static void *t1_reader(void *arg) {
    (void)arg;
    for (long i = 0; i < A_LOOPS; i++) {
        if (use_lock_A) pthread_mutex_lock(&proc_info_lock);
        if (proc_info != NULL) {                     // check
            __asm__ volatile("" ::: "memory");       // "..." (다른 일)
            char *p = proc_info;                     // use: 원래는 fputs(thd->proc_info, ...)
            if (p == NULL) would_crash++; else used_ok++;
        }
        if (use_lock_A) pthread_mutex_unlock(&proc_info_lock);
    }
    a_stop = 1;
    return NULL;
}
static void *t2_writer(void *arg) {
    (void)arg;
    while (!a_stop) {
        if (use_lock_A) pthread_mutex_lock(&proc_info_lock);
        proc_info = NULL;                            // thd->proc_info = NULL;
        proc_info = info_buf;                        // (다음 쿼리 시작)
        if (use_lock_A) pthread_mutex_unlock(&proc_info_lock);
    }
    return NULL;
}
static void run_A(int lock) {
    use_lock_A = lock; would_crash = used_ok = 0; a_stop = 0;
    pthread_t a, b;
    pthread_create(&b, NULL, t2_writer, NULL);
    pthread_create(&a, NULL, t1_reader, NULL);
    pthread_join(a, NULL); pthread_join(b, NULL);
    printf("(A) atomicity %-9s: checks passed=%ld, NULL at use (would crash)=%ld\n",
           lock ? "LOCKED" : "unlocked", used_ok + would_crash, would_crash);
}

// ---------------- (B) order violation ----------------
typedef struct { int State; } thr_t;
static thr_t *volatile mThread = NULL;
static thr_t thr_storage = { 42 };
static pthread_mutex_t mtLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  mtCond = PTHREAD_COND_INITIALIZER;
static int mtInit = 0;
static int fix_B = 0;
static long saw_null = 0, saw_ok = 0;

static void *mMain(void *arg) {
    (void)arg;
    if (fix_B) {
        pthread_mutex_lock(&mtLock);
        while (mtInit == 0) pthread_cond_wait(&mtCond, &mtLock);
        pthread_mutex_unlock(&mtLock);
    }
    thr_t *t = mThread;                              // mState = mThread->State;
    if (t == NULL) saw_null++; else { saw_ok++; (void)t->State; }
    return NULL;
}
static void spin_us(int us) {                        // init() 의 "..." 부분 (다른 초기화 작업) 흉내
    struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
    do { clock_gettime(CLOCK_MONOTONIC, &b); }
    while ((b.tv_sec - a.tv_sec) * 1000000L + (b.tv_nsec - a.tv_nsec) / 1000 < us);
}
static void run_B(int fix, int trials, int widen_us) {
    fix_B = fix; saw_null = saw_ok = 0;
    for (int i = 0; i < trials; i++) {
        mThread = NULL; mtInit = 0;
        pthread_t th;
        pthread_create(&th, NULL, mMain, NULL);      // mThread = PR_CreateThread(mMain, ...);
        if (widen_us) spin_us(widen_us);             // create 와 대입 사이에 다른 일이 끼어 있다면?
        mThread = &thr_storage;                      // 대입은 create 가 "리턴한 뒤"에야 일어난다
        if (fix) {
            pthread_mutex_lock(&mtLock);
            mtInit = 1;
            pthread_cond_signal(&mtCond);
            pthread_mutex_unlock(&mtLock);
        }
        pthread_join(th, NULL);
    }
    printf("(B) order     %-9s: window +%2dus, %d trials, child saw mThread==NULL (would crash) %ld times\n",
           fix ? "CV-fixed" : "unfixed", widen_us, trials, saw_null);
}

int main(void) {
    run_A(0);
    run_A(1);
    run_B(0, 2000, 0);
    run_B(0, 2000, 50);
    run_B(1, 2000, 50);
    return 0;
}
