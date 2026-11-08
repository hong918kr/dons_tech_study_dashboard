/* ENV: linux */
/* OS-A01 demo_semantics — futex 의 "말로만 듣던" 규칙을 직접 확인 (Lima VM elinux)
 *
 *   1. FUTEX_WAIT 중 시그널: SA_RESTART 유/무 × timeout 유/무 → -EINTR 인가, 커널이 다시 시작하나
 *   2. PRIVATE 로 잔 스레드를 shared FUTEX_WAKE 로 깨울 수 있나 (같은 프로세스)
 *   3. 두 프로세스가 MAP_SHARED 메모리의 futex 를 쓸 때: shared/shared vs PRIVATE/PRIVATE
 *   4. glibc pthread_mutex_t 의 futex 단어(__data.__lock): 0 → 1 → (대기자가 자면) 2
 *   5. PTHREAD_PRIO_INHERIT 뮤텍스: 단어 = 주인 TID, 대기자가 있으면 | FUTEX_WAITERS (strace 로 FUTEX_LOCK_PI 확인)
 *
 * build: gcc -std=c11 -Wall -Wextra -O2 -pthread demo_semantics.c -o sem && ./sem
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static long fx(void *a, int op, int val, const struct timespec *t) { return syscall(SYS_futex, a, op, val, t, NULL, 0); }
static void ms(long n) { struct timespec d = { n / 1000, (n % 1000) * 1000000L }; nanosleep(&d, NULL); }
static void on_sig(int s) { (void)s; }

/* /proc/self/task/<tid>/syscall 첫 필드가 SYS_futex 이면 그 futex 에서 자는 중 */
static int sleeping_in_futex(int tid) {
    char p[64], b[256]; long nr = -1;
    snprintf(p, sizeof p, "/proc/self/task/%d/syscall", tid);
    for (int i = 0; i < 1000; i++) {
        FILE *f = fopen(p, "r");
        if (f) { if (fgets(b, sizeof b, f)) sscanf(b, "%ld", &nr); fclose(f); }
        if (nr == SYS_futex) return 1;
        ms(1);
    }
    return 0;
}

/* ---- 1 ---- */
static atomic_int w1, tid1;
static int use_timeout;
static long ret1; static int err1;
static void *t_wait1(void *p) {
    (void)p;
    atomic_store(&tid1, (int)gettid());
    struct timespec two = { 2, 0 };
    ret1 = fx(&w1, FUTEX_WAIT_PRIVATE, 0, use_timeout ? &two : NULL);
    err1 = ret1 ? errno : 0;
    return NULL;
}
static void part1(int restart, int timeout) {
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sig; sa.sa_flags = restart ? SA_RESTART : 0; sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
    atomic_store(&w1, 0); atomic_store(&tid1, 0); use_timeout = timeout;
    pthread_t t; pthread_create(&t, NULL, t_wait1, NULL);
    while (!atomic_load(&tid1)) ms(1);
    sleeping_in_futex(atomic_load(&tid1));
    pthread_kill(t, SIGUSR1);
    ms(50);
    int still = 0;                         /* 시그널 뒤에도 다시 잠들어 있나? */
    { char p[64], b[256]; snprintf(p, sizeof p, "/proc/self/task/%d/syscall", atomic_load(&tid1));
      FILE *f = fopen(p, "r"); long nr = -1; if (f) { if (fgets(b, sizeof b, f)) sscanf(b, "%ld", &nr); fclose(f); }
      still = nr == SYS_futex; }
    atomic_store(&w1, 1);
    long n = fx(&w1, FUTEX_WAKE_PRIVATE, 1, NULL);
    pthread_join(t, NULL);
    printf("  SA_RESTART=%d timeout=%s : 시그널 50 ms 뒤 아직 futex 안? %s | 이후 wake 가 깨운 수 %ld | 스레드 반환 %ld (%s)\n",
           restart, timeout ? "2s " : "없음", still ? "예" : "아니오", n, ret1, err1 ? strerror(err1) : "깨워짐");
}

/* ---- 2 ---- */
static atomic_int w2, tid2;
static void *t_wait2(void *p) {
    (void)p; atomic_store(&tid2, (int)gettid());
    struct timespec t = { 0, 300000000L };
    fx(&w2, FUTEX_WAIT_PRIVATE, 0, &t);
    return NULL;
}

/* ---- 4, 5 ---- */
static pthread_mutex_t gm = PTHREAD_MUTEX_INITIALIZER, pim;
static atomic_int tid4;
static void *t_lock(void *p) {
    pthread_mutex_t *m = p;
    atomic_store(&tid4, (int)gettid());
    pthread_mutex_lock(m);
    pthread_mutex_unlock(m);
    return NULL;
}

int main(void) {
    puts("== 1. FUTEX_WAIT 와 시그널 ==");
    part1(0, 0); part1(1, 0); part1(0, 1); part1(1, 1);

    puts("== 2. PRIVATE 대기 vs shared wake (같은 프로세스) ==");
    pthread_t t; pthread_create(&t, NULL, t_wait2, NULL);
    while (!atomic_load(&tid2)) ms(1);
    sleeping_in_futex(atomic_load(&tid2));
    long a = fx(&w2, FUTEX_WAKE, 1, NULL);           /* shared 로 깨워 본다 */
    long b = fx(&w2, FUTEX_WAKE_PRIVATE, 1, NULL);   /* PRIVATE 로 깨워 본다 */
    pthread_join(t, NULL);
    printf("  FUTEX_WAKE(shared) 가 깨운 수 %ld, 이어서 FUTEX_WAKE_PRIVATE 가 깨운 수 %ld\n", a, b);

    puts("== 3. 두 프로세스, MAP_SHARED 메모리의 futex ==");
    for (int priv = 0; priv < 2; priv++) {
        atomic_int *sh = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        atomic_store(&sh[0], 0); atomic_store(&sh[1], 0);
        int opw = priv ? FUTEX_WAIT_PRIVATE : FUTEX_WAIT, opk = priv ? FUTEX_WAKE_PRIVATE : FUTEX_WAKE;
        pid_t c = fork();
        if (c == 0) {
            struct timespec t = { 0, 500000000L };
            atomic_store(&sh[1], 1);
            long r = fx(&sh[0], opw, 0, &t);
            _exit(r == 0 ? 0 : (errno == ETIMEDOUT ? 1 : 2));
        }
        while (!atomic_load(&sh[1])) ms(1);
        ms(100);                                       /* 자식이 잠들 시간 */
        atomic_store(&sh[0], 1);
        long n = fx(&sh[0], opk, 1, NULL);
        int st; waitpid(c, &st, 0);
        printf("  %s: 부모 wake 가 깨운 수 %ld, 자식 결과 %s\n", priv ? "PRIVATE/PRIVATE" : "shared/shared  ", n,
               WEXITSTATUS(st) == 0 ? "깨워짐" : WEXITSTATUS(st) == 1 ? "ETIMEDOUT (잃어버림)" : "기타 오류");
        munmap(sh, 4096);
    }

    puts("== 4. glibc pthread_mutex_t 의 futex 단어 ==");
    printf("  초기 __lock=%d", gm.__data.__lock);
    pthread_mutex_lock(&gm);
    printf(" | lock 후 %d (owner=%d, 내 tid=%d)", gm.__data.__lock, gm.__data.__owner, (int)gettid());
    atomic_store(&tid4, 0);
    pthread_create(&t, NULL, t_lock, &gm);
    while (!atomic_load(&tid4)) ms(1);
    sleeping_in_futex(atomic_load(&tid4));
    printf(" | 대기자가 잠든 뒤 %d", gm.__data.__lock);
    pthread_mutex_unlock(&gm);
    pthread_join(t, NULL);
    printf(" | 모두 끝난 뒤 %d\n", gm.__data.__lock);

    puts("== 5. PTHREAD_PRIO_INHERIT 뮤텍스 ==");
    pthread_mutexattr_t at; pthread_mutexattr_init(&at);
    pthread_mutexattr_setprotocol(&at, PTHREAD_PRIO_INHERIT);
    pthread_mutex_init(&pim, &at);
    pthread_mutex_lock(&pim);
    unsigned u1 = (unsigned)pim.__data.__lock;
    atomic_store(&tid4, 0);
    pthread_create(&t, NULL, t_lock, &pim);
    while (!atomic_load(&tid4)) ms(1);
    sleeping_in_futex(atomic_load(&tid4));
    unsigned u2 = (unsigned)pim.__data.__lock;
    pthread_mutex_unlock(&pim);
    pthread_join(t, NULL);
    printf("  내 tid=%d | lock 후 단어 0x%08x | 대기자가 잠든 뒤 0x%08x (FUTEX_WAITERS=0x%08x) | 끝 0x%08x\n",
           (int)gettid(), u1, u2, (unsigned)FUTEX_WAITERS, (unsigned)pim.__data.__lock);
    return 0;
}
