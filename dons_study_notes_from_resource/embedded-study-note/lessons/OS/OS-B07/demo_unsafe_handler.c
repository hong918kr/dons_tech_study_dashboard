/* OS-B07 demo_unsafe_handler.c — 핸들러에서 락을 잡는 함수(printf, malloc)를 부르면 무슨 일이 생기나
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_unsafe_handler.c -o demo_unsafe_handler
 *   ./demo_unsafe_handler            # 전부: mutex(결정적) → printf 스트레스 → malloc 스트레스
 *   ./demo_unsafe_handler mutex | printf | malloc [시도 횟수]
 *
 * 모든 실험은 자식 프로세스에서 돌고, 부모가 공유 메모리의 진행 카운터를 0.25 s 마다 본다.
 *   - 자식이 시그널로 죽음      → "died by signal N"
 *   - 1 s 동안 카운터가 그대로  → "NO PROGRESS (deadlock)" 후 SIGKILL
 *   - 제한 시간까지 계속 진행   → "survived" (운이 좋았을 뿐, 안전하다는 뜻이 아니다)
 */
#define _DARWIN_C_SOURCE                    /* MAP_ANON, setitimer, pthread_mutexattr_settype */
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct { volatile unsigned long main_it, hits; } shm_t;
static shm_t *S;

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void install(int sig, void (*fn)(int), int flags) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;
    sigaction(sig, &sa, NULL);
}

/* 부모: 자식을 지켜보고 결과 한 줄 */
static void watch(pid_t c, int limit_ms) {
    unsigned long lm = 0, lh = 0;
    int stuck = 0, st;
    long t0 = now_ms();
    for (;;) {
        struct timespec ts = { 0, 250000000L };
        nanosleep(&ts, NULL);
        long el = now_ms() - t0;
        if (waitpid(c, &st, WNOHANG) == c) {
            if (WIFSIGNALED(st))
                printf("  %5ld ms  died by signal %d (%s)   main_it=%lu handler_runs=%lu\n",
                       el, WTERMSIG(st), strsignal(WTERMSIG(st)), S->main_it, S->hits);
            else
                printf("  %5ld ms  exited %d\n", el, WEXITSTATUS(st));
            return;
        }
        unsigned long m = S->main_it, h = S->hits;
        if (m == lm && h == lh) {
            if (++stuck >= 4) {
                printf("  %5ld ms  NO PROGRESS for 1 s (deadlock)  main_it=%lu handler_runs=%lu  -> SIGKILL pid %d\n",
                       el, m, h, (int)c);
                kill(c, SIGKILL);
                waitpid(c, &st, 0);
                return;
            }
        } else {
            stuck = 0;
        }
        lm = m; lh = h;
        if (el > limit_ms) {
            printf("  %5ld ms  survived (still progressing)  main_it=%lu handler_runs=%lu\n", el, m, h);
            kill(c, SIGKILL);
            waitpid(c, &st, 0);
            return;
        }
    }
}

/* ---------- 1) 결정적 재현: "스레드 안전" 로거 + 핸들러가 같은 로거를 부름 ---------- */
static pthread_mutex_t lk;
static char logbuf[64];
static size_t loglen;
static volatile sig_atomic_t interrupt_here;
static volatile sig_atomic_t lock_err;                 /* 핸들러 안에서 본 lock 에러 (출력은 main 에서) */

static void log_append(const char *s) {               /* stdio 의 FILE 처럼: 락 → 버퍼에 복사 → 길이 갱신 → 해제 */
    int e = pthread_mutex_lock(&lk);
    if (e != 0) { lock_err = e; return; }
    size_t n = strlen(s);
    memcpy(logbuf + loglen, s, n);
    if (interrupt_here) { interrupt_here = 0; raise(SIGUSR1); }   /* 시그널이 하필 여기서 온다 */
    loglen += n;
    pthread_mutex_unlock(&lk);
}
static void h_log(int sig) { (void)sig; log_append("SIG"); }

static void mutex_case(int type, const char *name) {
    printf("[mutex %s]\n", name);
    fflush(stdout);
    S->main_it = S->hits = 0;
    pid_t c = fork();
    if (c == 0) {
        pthread_mutexattr_t a;
        pthread_mutexattr_init(&a);
        pthread_mutexattr_settype(&a, type);
        pthread_mutex_init(&lk, &a);
        install(SIGUSR1, h_log, 0);
        interrupt_here = 1;
        log_append("hello ");
        S->main_it = 1;
        if (lock_err) printf("    handler's pthread_mutex_lock -> %s\n", strerror(lock_err));
        printf("    main: log_append returned. loglen=%zu buf=\"", loglen);
        for (size_t i = 0; i < loglen; i++) putchar(logbuf[i] ? logbuf[i] : '.');
        printf("\"  (expected \"hello SIG\" or \"SIGhello \")\n");
        fflush(stdout);
        _exit(0);
    }
    watch(c, 3000);
}

/* ---------- 2) 3) 진짜 libc: printf / malloc 스트레스 ---------- */
static void * volatile sink_main, * volatile sink_h;
static void h_printf(int sig) { (void)sig; printf("[h%lu]", S->hits); S->hits++; }
static void h_malloc(int sig) { (void)sig; sink_h = malloc(100000); free(sink_h); S->hits++; }

static void stress(int mode, int trials) {
    printf("[%s in handler, SIGALRM every 100 us, main loop also calls %s]\n",
           mode ? "malloc(100000)" : "printf", mode ? "malloc(100000)/free" : "printf");
    for (int t = 0; t < trials; t++) {
        S->main_it = S->hits = 0;
        fflush(stdout);
        pid_t c = fork();
        if (c == 0) {
            int dn = open("/dev/null", O_WRONLY);
            dup2(dn, 1);
            install(SIGALRM, mode ? h_malloc : h_printf, SA_RESTART);
            struct itimerval it = { { 0, 100 }, { 0, 100 } };
            setitimer(ITIMER_REAL, &it, NULL);
            for (;;) {
                if (mode) { sink_main = malloc(100000); free(sink_main); }
                else      printf("main %lu\n", S->main_it);
                S->main_it++;
            }
        }
        watch(c, 4000);
    }
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    S = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    if (S == MAP_FAILED) { perror("mmap"); return 1; }
    const char *what = argc > 1 ? argv[1] : "all";
    int trials = argc > 2 ? atoi(argv[2]) : 5;
    if (!strcmp(what, "all") || !strcmp(what, "mutex")) {
        mutex_case(PTHREAD_MUTEX_NORMAL, "NORMAL");
        mutex_case(PTHREAD_MUTEX_ERRORCHECK, "ERRORCHECK");
        mutex_case(PTHREAD_MUTEX_RECURSIVE, "RECURSIVE");
    }
    if (!strcmp(what, "all") || !strcmp(what, "printf")) stress(0, trials);
    if (!strcmp(what, "all") || !strcmp(what, "malloc")) stress(1, trials);
    return 0;
}
