/* ALGO-A10 demo: 재귀 Tarjan 은 깊은 그래프에서 스택이 터진다 — 반복판(solution.c)은 괜찮다.
 * 정점 N 개 사슬 0→1→…→N-1 은 DFS 깊이가 N. 자식 프로세스에서 재귀판을 돌려 시그널로 죽는지 본다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_recursion.c -o demo_rec && ./demo_rec
 *        (-O0 로도 빌드해 프레임 크기 차이를 본다)
 */
#define _POSIX_C_SOURCE 200809L
#define ALGO_A10_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"   /* solution.c 의 다른 함수는 여기서 안 쓴다 */
#include "solution.c"
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static const graph_t *r_g;
static int *r_idx, *r_low, *r_stk, *r_comp, r_sp, r_cnt, r_ncomp;
static bool *r_on;
static uintptr_t r_deep;

static void strong(int v) {                       /* 교과서 재귀 Tarjan (Tarjan 1972 의 형태) */
    char probe;
    uintptr_t a = (uintptr_t)&probe;
    if (a < r_deep) r_deep = a;                    /* 스택은 아래로 자란다 (arm64 macOS) */
    r_idx[v] = r_low[v] = r_cnt++; r_stk[r_sp++] = v; r_on[v] = true;
    for (int k = r_g->off[v]; k < r_g->off[v + 1]; k++) {
        int w = r_g->adj[k];
        if (r_idx[w] == -1) { strong(w); if (r_low[w] < r_low[v]) r_low[v] = r_low[w]; }
        else if (r_on[w] && r_idx[w] < r_low[v]) r_low[v] = r_idx[w];
    }
    if (r_low[v] == r_idx[v]) {
        int w;
        do { w = r_stk[--r_sp]; r_on[w] = false; r_comp[w] = r_ncomp; } while (w != v);
        r_ncomp++;
    }
}

static graph_t make_chain(int n) {
    int *s = malloc((size_t)n * sizeof(int)), *d = malloc((size_t)n * sizeof(int));
    for (int k = 0; k + 1 < n; k++) { s[k] = k; d[k] = k + 1; }
    graph_t g; graph_build(&g, n, n - 1, s, d);
    free(s); free(d);
    return g;
}

int main(void) {
    struct rlimit rl;
    getrlimit(RLIMIT_STACK, &rl);
    printf("RLIMIT_STACK soft = %llu KiB (main thread)\n", (unsigned long long)rl.rlim_cur / 1024u);
    static const int Ns[] = {10000, 50000, 100000, 200000, 500000, 1000000};
    for (size_t i = 0; i < sizeof Ns / sizeof Ns[0]; i++) {
        int n = Ns[i];
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            graph_t g = make_chain(n);
            r_g = &g;
            r_idx = malloc((size_t)n * sizeof(int)); r_low = malloc((size_t)n * sizeof(int));
            r_stk = malloc((size_t)n * sizeof(int)); r_comp = malloc((size_t)n * sizeof(int));
            r_on = calloc((size_t)n, sizeof(bool));
            for (int v = 0; v < n; v++) r_idx[v] = -1;
            char top; uintptr_t t0 = (uintptr_t)&top; r_deep = t0;
            for (int v = 0; v < n; v++) if (r_idx[v] == -1) strong(v);
            printf("N=%7d recursive: ok, ncomp=%d, stack used ~%lu KiB (%lu B/frame)\n", n, r_ncomp,
                   (unsigned long)((t0 - r_deep) / 1024u), (unsigned long)((t0 - r_deep) / (uintptr_t)n));
            fflush(stdout);
            _exit(0);
        }
        int st = 0;
        waitpid(pid, &st, 0);
        if (WIFSIGNALED(st))
            printf("N=%7d recursive: CRASH, killed by signal %d (%s)\n", n, WTERMSIG(st),
                   WTERMSIG(st) == SIGSEGV ? "SIGSEGV" : WTERMSIG(st) == SIGBUS ? "SIGBUS" : "other");
        graph_t g = make_chain(n);                  /* 같은 그래프를 반복판으로 */
        int *c = malloc((size_t)n * sizeof(int));
        printf("N=%7d iterative: ncomp=%d\n", n, scc_tarjan(&g, c));
        free(c); graph_free(&g);
    }
    return 0;
}
