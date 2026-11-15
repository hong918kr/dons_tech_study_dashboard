/* ALGO-B06 demo: 퀵소트 재귀가 실제로 쓰는 스택 — 칠하기(high-water mark) · 작은 스택에서 죽기 · 가드 없이 조용히 덮기
 * solution.c 의 함수를 그대로 쓴다 (main 만 이름을 바꿔 포함).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_stack.c -o demo_stack
 * run:   ./demo_stack                     # 1) 칠하기 측정  2) 64 KiB pthread 에서 크래시  3) 가드 없는 스택 덮어쓰기
 *        (ulimit -s 1024; ./demo_stack main 30000)   # 메인 스레드 + 작은 ulimit 에서 교과서 Lomuto
 */
#define _DARWIN_C_SOURCE
#define main b06_solution_main
#include "solution.c"
#undef main
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

enum { PAINT = 0xA5 };
static int g_buf[1 << 20];

typedef struct { sort_fn f; int kind; int n; qs_stats_t st; int sorted_ok; } job_t;

static void *run_job(void *p) {
    job_t *j = p;
    fill(g_buf, j->n, j->kind);
    j->f(g_buf, j->n, &j->st);
    j->sorted_ok = 1;
    for (int i = 1; i < j->n; i++) j->sorted_ok &= g_buf[i - 1] <= g_buf[i];
    return NULL;
}

/* 직접 준비한 스택(낮은 주소 = 끝)을 PAINT 로 칠하고 스레드를 돌린 뒤, 아래에서부터 칠이 남은 바이트를 센다.
 * FreeRTOS 의 uxTaskGetStackHighWaterMark() 와 같은 원리. */
static size_t painted_run(job_t *j, size_t stack_size) {
    unsigned char *stk = mmap(NULL, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (stk == MAP_FAILED) { perror("mmap"); exit(2); }
    memset(stk, PAINT, stack_size);
    pthread_attr_t at; pthread_t th;
    pthread_attr_init(&at);
    pthread_attr_setstack(&at, stk, stack_size);
    pthread_create(&th, &at, run_job, j);
    pthread_join(th, NULL);
    size_t untouched = 0;
    while (untouched < stack_size && stk[untouched] == PAINT) untouched++;
    munmap(stk, stack_size);
    return stack_size - untouched;
}

static void paint_table(void) {
    static const struct { const char *name; sort_fn f; int kind; int n; } R[] = {
        {"lomuto (sorted)", quicksort_lomuto, K_SORTED, 1},
        {"lomuto (sorted)", quicksort_lomuto, K_SORTED, 100},
        {"lomuto (sorted)", quicksort_lomuto, K_SORTED, 1000},
        {"lomuto (sorted)", quicksort_lomuto, K_SORTED, 10000},
        {"hoare  (sorted)", quicksort_hoare,  K_SORTED, 10000},
        {"safe   (sorted)", quicksort_safe,   K_SORTED, 1000},
        {"safe   (sorted)", quicksort_safe,   K_SORTED, 10000},
        {"safe   (random)", quicksort_safe,   K_RANDOM, 10000},
        {"safe   (sorted)", quicksort_safe,   K_SORTED, 1000000},
        {"safe   (random)", quicksort_safe,   K_RANDOM, 1000000},
        {"safe   (equal) ", quicksort_safe,   K_EQUAL,  1000000},
    };
    printf("== 1) painted 4 MiB pthread stack: high-water mark ==\n");
    printf("%-16s %8s %6s %10s\n", "sort", "n", "depth", "stack B");
    for (size_t i = 0; i < sizeof R / sizeof R[0]; i++) {
        job_t j = {R[i].f, R[i].kind, R[i].n, {0}, 0};
        size_t used = painted_run(&j, 4u << 20);
        printf("%-16s %8d %6d %10zu%s\n", R[i].name, R[i].n, j.st.max_depth, used, j.sorted_ok ? "" : "  NOT SORTED");
    }
    printf("(n=1: no recursion = thread start overhead. bytes per level = slope between rows)\n\n");
}

/* 시스템이 만든 pthread 스택(가드 페이지 있음)에서 돌린다. 자식 프로세스에서 → 죽어도 부모는 보고만. */
static void crash_row(const char *name, sort_fn f, int kind, int n, size_t stack_size) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        job_t j = {f, kind, n, {0}, 0};
        pthread_attr_t at; pthread_t th;
        pthread_attr_init(&at);
        pthread_attr_setstacksize(&at, stack_size);
        pthread_create(&th, &at, run_job, &j);
        pthread_join(th, NULL);
        printf("  %-16s n=%7d stack=%3zu KiB: ok, depth=%d, sorted=%d\n", name, n, stack_size / 1024, j.st.max_depth, j.sorted_ok);
        fflush(stdout);
        _exit(0);
    }
    int ws = 0;
    waitpid(pid, &ws, 0);
    if (WIFSIGNALED(ws))
        printf("  %-16s n=%7d stack=%3zu KiB: CRASH signal %d (%s)\n", name, n, stack_size / 1024, WTERMSIG(ws),
               WTERMSIG(ws) == SIGSEGV ? "SIGSEGV" : WTERMSIG(ws) == SIGBUS ? "SIGBUS" : "other");
}

/* 가드 페이지 없는 스택: [이웃 메모리 64 KiB][스택 64 KiB] 를 한 번에 mmap.
 * 스택이 넘치면 아래쪽 "이웃"(MCU 라면 .bss / 힙 / 다른 태스크의 TCB)을 조용히 덮는다. */
static void silent_overflow(int n) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        const size_t NB = 64u << 10, SS = 64u << 10;
        unsigned char *m = mmap(NULL, NB + SS, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        memset(m, PAINT, NB + SS);
        job_t j = {quicksort_lomuto, K_SORTED, n, {0}, 0};
        pthread_attr_t at; pthread_t th;
        pthread_attr_init(&at);
        pthread_attr_setstack(&at, m + NB, SS);
        pthread_create(&th, &at, run_job, &j);
        pthread_join(th, NULL);
        size_t dirty = 0;
        for (size_t i = 0; i < NB; i++) dirty += m[i] != PAINT;
        size_t low = 0;
        while (low < NB && m[low] == PAINT) low++;
        printf("  lomuto sorted n=%d, 64 KiB stack without guard: finished, sorted=%d, depth=%d,\n"
               "  neighbour bytes changed: %zu of %zu (overflowed %zu B below the stack)\n",
               n, j.sorted_ok, j.st.max_depth, dirty, NB, NB - low);
        fflush(stdout);
        _exit(0);
    }
    int ws = 0;
    waitpid(pid, &ws, 0);
    if (WIFSIGNALED(ws)) printf("  silent overflow child died: signal %d\n", WTERMSIG(ws));
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "main") == 0) {
        struct rlimit rl; getrlimit(RLIMIT_STACK, &rl);
        int n = atoi(argv[2]);
        printf("main thread, RLIMIT_STACK soft = %llu KiB, lomuto sorted n=%d ...\n", (unsigned long long)rl.rlim_cur / 1024, n);
        fflush(stdout);
        qs_stats_t st;
        fill(g_buf, n, K_SORTED);
        quicksort_lomuto(g_buf, n, &st);
        printf("survived: depth=%d\n", st.max_depth);
        return 0;
    }
    paint_table();
    printf("== 2) system pthread stack (guard page), child process ==\n");
    static const int Ns[] = {500, 1000, 1500, 2000, 4000};
    for (size_t i = 0; i < sizeof Ns / sizeof Ns[0]; i++) crash_row("lomuto sorted", quicksort_lomuto, K_SORTED, Ns[i], 64u << 10);
    crash_row("safe sorted", quicksort_safe, K_SORTED, 1000000, 16u << 10);
    crash_row("safe random", quicksort_safe, K_RANDOM, 1000000, 16u << 10);
    printf("\n== 3) no guard page ==\n");
    silent_overflow(1500);
    return 0;
}
