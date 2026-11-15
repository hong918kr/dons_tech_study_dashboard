/* ALGO-B11 demo: 재귀 flood fill 로 진짜 스택 오버플로를 낸다.
 *
 *  ./demo_overflow            스택 크기를 정한 pthread 에서 뱀 모양 복도(깊이 ≈ 칸 수)를 재귀로 칠한다.
 *                             시행마다 fork 한 자식에서 돌리고, 부모가 종료 시그널을 표로 찍는다.
 *  ./demo_overflow main W     메인 스레드에서 W×W 뱀 복도를 재귀로 칠한다 (ulimit -s 확인용):
 *                             (ulimit -s 512; ./demo_overflow main 256); echo "exit=$?"
 *  ./demo_overflow raise W    위와 같지만 먼저 setrlimit(RLIMIT_STACK) 소프트 한계를 하드 한계로 올린다
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O0 demo_overflow.c -o demo_overflow_O0
 *        cc -std=c11 -Wall -Wextra -Werror -O2 demo_overflow.c -o demo_overflow_O2
 */
#define _DARWIN_C_SOURCE
#define ALGO_B11_NO_MAIN
#include "solution.c"
#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

/* 계측 없는 교과서 재귀판. 컴파일러가 마지막 호출을 루프로 바꿀 수 있다 (꼬리 재귀 제거) */
static uintptr_t g_fp_min;
static int ff_plain(grid_t *g, int r, int c, uint8_t from, uint8_t to) {
    uintptr_t fp = (uintptr_t)__builtin_frame_address(0);
    if (fp < g_fp_min) g_fp_min = fp;
    if (from == to || !in_grid(g, r, c) || g->cell[r * g->w + c] != from) return 0;
    g->cell[r * g->w + c] = to;
    int n = 1;
    n += ff_plain(g, r - 1, c, from, to);
    n += ff_plain(g, r + 1, c, from, to);
    n += ff_plain(g, r, c - 1, from, to);
    n += ff_plain(g, r, c + 1, from, to);
    return n;
}

static grid_t mk_serp(int w, int h) {
    grid_t g = { calloc((size_t)w * (size_t)h, 1), w, h };
    for (int r = 1; r < h; r += 2) {
        for (int c = 0; c < w; c++) g.cell[r * w + c] = 1;
        g.cell[r * w + ((r / 2) % 2 == 0 ? w - 1 : 0)] = 0;
    }
    return g;
}

typedef struct { int which, w; int filled; unsigned long kib; } job_t;

static void *worker(void *arg) {
    job_t *j = arg;
    grid_t g = mk_serp(j->w, j->w);
    char top;
    if (j->which == 0) {                               /* solution.c 의 ff_rec (계측판) */
        depth_t d;
        depth_reset(&d);
        j->filled = ff_rec(&g, 0, 0, 0, 2, &d);
        j->kib = (unsigned long)((d.sp_first - d.sp_min) / 1024u);
    } else if (j->which == 1) {                        /* 계측 없는 재귀판 */
        g_fp_min = (uintptr_t)&top;
        j->filled = ff_plain(&g, 0, 0, 0, 2);
        j->kib = (unsigned long)(((uintptr_t)&top - g_fp_min) / 1024u);
    } else {                                           /* 명시적 스택 (힙 버퍼) */
        int pk;
        int32_t *buf = malloc(sizeof(int32_t) * 16);
        j->filled = ff_dfs_iter(&g, 0, 0, 0, 2, buf, 16, &pk);
        j->kib = 0;
        free(buf);
    }
    free(g.cell);
    return NULL;
}

static void *report_size(void *arg) {               /* 요청한 크기 vs 실제로 받은 크기 */
    *(size_t *)arg = pthread_get_stacksize_np(pthread_self());
    return NULL;
}

/* 자식 프로세스에서 stack_kib 짜리 pthread 로 실행. 부모에게 결과 문자열을 돌려준다 */
static void trial(int which, int w, size_t stack_kib, char *res, size_t rn) {
    int fd[2];
    if (pipe(fd) != 0) { snprintf(res, rn, "pipe?"); return; }
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        close(fd[0]);
        job_t j = { which, w, 0, 0 };
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setstacksize(&at, stack_kib * 1024u);
        pthread_t th;
        pthread_create(&th, &at, worker, &j);
        pthread_join(th, NULL);
        char msg[64];
        int len = snprintf(msg, sizeof msg, "ok %luK", j.kib);
        if (write(fd[1], msg, (size_t)len) < 0) _exit(3);
        _exit(0);
    }
    close(fd[1]);
    int st = 0;
    waitpid(pid, &st, 0);
    ssize_t k = read(fd[0], res, rn - 1);
    close(fd[0]);
    res[k > 0 ? k : 0] = '\0';
    if (WIFSIGNALED(st)) {
        int s = WTERMSIG(st);
        snprintf(res, rn, "%s", s == SIGSEGV ? "SIGSEGV" : s == SIGBUS ? "SIGBUS" : "signal?");
    }
}

int main(int argc, char **argv) {
#ifdef __OPTIMIZE__
    const char *build = "-O2";
#else
    const char *build = "-O0";
#endif
    struct rlimit rl;
    getrlimit(RLIMIT_STACK, &rl);
    bool raise_lim = argc == 3 && strcmp(argv[1], "raise") == 0;
    if (raise_lim) {                                   /* 실행 중에 소프트 한계 올리기 */
        rl.rlim_cur = rl.rlim_max;
        int rc = setrlimit(RLIMIT_STACK, &rl);
        getrlimit(RLIMIT_STACK, &rl);
        printf("setrlimit(RLIMIT_STACK, hard) rc=%d\n", rc);
    }
    if (argc == 3 && (strcmp(argv[1], "main") == 0 || raise_lim)) {   /* 메인 스레드 모드 */
        int w = atoi(argv[2]);
        printf("[%s] main thread, RLIMIT_STACK=%llu KiB, serpentine %dx%d\n", build,
               (unsigned long long)rl.rlim_cur / 1024u, w, w);
        fflush(stdout);
        grid_t g = mk_serp(w, w);
        depth_t d;
        depth_reset(&d);
        int n = ff_rec(&g, 0, 0, 0, 2, &d);
        printf("ok: filled %d, depth %d, stack used %lu KiB\n", n, d.max_depth,
               (unsigned long)((d.sp_first - d.sp_min) / 1024u));
        free(g.cell);
        return 0;
    }
    static const int W[] = { 64, 128, 256, 512 };
    static const size_t S[] = { 64, 256, 512, 1024, 8192 };
    static const char *const NAME[] = { "ff_rec (solution.c)", "ff_plain (no probe)" };
    printf("[%s] pthread stack size vs serpentine grid (recursion depth ~ cells)\n", build);
    printf("pthread_get_stacksize_np:");
    for (size_t s = 0; s < sizeof S / sizeof S[0]; s++) {
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setstacksize(&at, S[s] * 1024u);
        pthread_t th;
        size_t got = 0;
        pthread_create(&th, &at, report_size, &got);
        pthread_join(th, NULL);
        printf("  %zuK->%zuK", S[s], got / 1024u);
    }
    printf("\n");
    for (int which = 0; which < 2; which++) {
        printf("\n%s\n%-22s", NAME[which], "grid (cells)");
        for (size_t s = 0; s < sizeof S / sizeof S[0]; s++) printf(" %9zuK", S[s]);
        printf("\n");
        for (size_t i = 0; i < sizeof W / sizeof W[0]; i++) {
            grid_t g = mk_serp(W[i], W[i]);
            int cells = 0;
            for (int k = 0; k < W[i] * W[i]; k++) cells += g.cell[k] == 0;
            free(g.cell);
            char lbl[40];
            snprintf(lbl, sizeof lbl, "%dx%d (%d)", W[i], W[i], cells);
            printf("%-22s", lbl);
            for (size_t s = 0; s < sizeof S / sizeof S[0]; s++) {
                char res[64];
                trial(which, W[i], S[s], res, sizeof res);
                printf(" %10s", res);
            }
            printf("\n");
        }
    }
    char res[64];
    trial(2, 1024, 64, res, sizeof res);
    printf("\nff_dfs_iter, serpentine 1024x1024 (524800 cells), 64K thread stack, 16-entry heap stack: %s\n", res);
    return 0;
}
