/* OS-M11 추가 연습 — 한 번에 검증 (mini lockdep 확장 5개)
 *   연습 1  BFS 로 "가장 짧은" 사이클 경로 찾기 (Linux lockdep 이 보고에 쓰는 방식) vs DFS
 *   연습 2  락 순위(rank) 검사기: 반대 순서를 한 번도 안 봐도 첫 위반에서 바로 보고
 *   연습 3  IRQ 안전성: ISR 에서 쓰는 락을 태스크가 IRQ 를 켠 채 잡으면 보고 (lockdep 의 irq 상태 검사 축소판)
 *   연습 4  기록된 그래프 → 위상 정렬(Kahn)로 "문서용 락 순서" 뽑기, 사이클이면 실패
 *   연습 5  게이트 락(G 아래의 A->B / B->A) = mini-lockdep 의 오탐(false positive) 확인
 * 단일 스레드에서 결정적으로 돈다 (엔진은 solution.c 의 축소판을 이 파일 안에 다시 둔다).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* ---------- 공통: 클래스 이름 + 인접 행렬 그래프 ---------- */
enum { N = 16 };
static const char *cname[N];
static int ncls;
static bool adj[N][N];
static int cls(const char *name) {
    for (int i = 0; i < ncls; i++) if (strcmp(cname[i], name) == 0) return i;
    cname[ncls] = name; return ncls++;
}
static void graph_clear(void) { memset(adj, 0, sizeof adj); }
static void path_str(const int *p, int n, char *out, size_t cap) {
    size_t k = 0; out[0] = '\0';
    for (int i = 0; i < n && k < cap; i++) k += (size_t)snprintf(out + k, cap - k, "%s%s", i ? " -> " : "", cname[p[i]]);
}

/* 연습 1: DFS(작은 id 먼저) 와 BFS. 둘 다 from ⇝ to 경로를 path 에 채우고 노드 수를 돌려준다 (없으면 0). */
static int rebuild(int to, const int *parent, int *path) {
    int n = 0;
    for (int v = to; v != -1; v = parent[v]) path[n++] = v;
    for (int i = 0; i < n / 2; i++) { int t = path[i]; path[i] = path[n - 1 - i]; path[n - 1 - i] = t; }
    return n;
}
static int path_dfs(int from, int to, int *path) {             /* solution.c A1 과 같은 커서식 DFS */
    int st[N], it[N], sp = 1; bool seen[N] = { false };
    st[0] = from; it[0] = 0; seen[from] = true;
    if (from == to) { path[0] = from; return 1; }
    while (sp > 0) {
        int u = st[sp - 1], v = it[sp - 1];
        while (v < N && !(adj[u][v] && !seen[v])) v++;
        if (v == N) { sp--; continue; }
        it[sp - 1] = v + 1; seen[v] = true; st[sp] = v; it[sp] = 0; sp++;
        if (v == to) { memcpy(path, st, (size_t)sp * sizeof st[0]); return sp; }
    }
    return 0;
}
static int path_bfs(int from, int to, int *path) {
    int parent[N], q[N], head = 0, tail = 0; bool seen[N] = { false };
    q[tail++] = from; seen[from] = true; parent[from] = -1;
    while (head < tail) {
        int u = q[head++];
        if (u == to) return rebuild(to, parent, path);          /* 처음 꺼낸 순간 = 간선 수 최소 */
        for (int v = 0; v < N; v++) if (adj[u][v] && !seen[v]) { seen[v] = true; parent[v] = u; q[tail++] = v; }
    }
    return 0;
}

/* 연습 2: 순위 검사기. 쥔 락 중 가장 높은 순위보다 "높은" 순위만 잡을 수 있다. 위반이면 false. */
typedef struct { int rank; const char *name; } rlock_t;
static int r_held[8], r_n, r_viol;
static bool rank_lock(const rlock_t *l) {
    bool ok = true;
    for (int i = 0; i < r_n; i++) if (r_held[i] >= l->rank) ok = false;
    if (!ok) r_viol++;
    r_held[r_n++] = l->rank;
    return ok;
}
static void rank_unlock(const rlock_t *l) {
    for (int i = r_n - 1; i >= 0; i--)
        if (r_held[i] == l->rank) { memmove(&r_held[i], &r_held[i + 1], (size_t)(r_n - 1 - i) * sizeof(int)); r_n--; return; }
}

/* 연습 3: IRQ 안전성. 클래스마다 두 비트: ISR 안에서 잡힘 / IRQ 가 켜진 태스크 문맥에서 잡힘. 둘 다면 보고. */
enum { USED_IN_IRQ = 1, ENABLED_IRQ = 2 };
static unsigned irq_state[N];
static bool in_irq, irq_off;           /* 지금 ISR 안인가 / 태스크가 IRQ 를 껐나 (spin_lock_irqsave 같은 것) */
static int irq_reports;
static void irq_track(int c) {
    unsigned before = irq_state[c];
    if (in_irq) irq_state[c] |= USED_IN_IRQ;
    else if (!irq_off) irq_state[c] |= ENABLED_IRQ;
    if (irq_state[c] == (USED_IN_IRQ | ENABLED_IRQ) && before != irq_state[c]) {
        irq_reports++;
        printf("  irq-check: '%s' is taken in ISR AND in task context with IRQs enabled -> ISR can spin/block on it\n",
               cname[c]);
    }
}

/* 연습 4: Kahn 위상 정렬. 그래프에 쓰인 클래스(used[]) 만. 성공하면 개수, 사이클이면 -1. 같은 단계는 작은 id 먼저. */
static int topo_order(const bool *used, int *out) {
    int indeg[N] = { 0 }, n = 0, total = 0;
    for (int u = 0; u < N; u++) if (used[u]) { total++; for (int v = 0; v < N; v++) if (adj[u][v]) indeg[v]++; }
    bool done[N] = { false };
    for (;;) {
        int pick = -1;
        for (int u = 0; u < N; u++) if (used[u] && !done[u] && indeg[u] == 0) { pick = u; break; }
        if (pick < 0) break;
        done[pick] = true; out[n++] = pick;
        for (int v = 0; v < N; v++) if (adj[pick][v]) indeg[v]--;
    }
    return n == total ? n : -1;
}

/* 연습 5: 보유 스택 + 새 간선일 때 역방향 검사 (solution.c 의 A2/A3 축소판, 단일 스레드) */
static int h_stack[8], h_n, cycles;
static void dl_lock(int c) {
    for (int i = 0; i < h_n; i++) {
        int f = h_stack[i], p[N];
        if (f == c || adj[f][c]) continue;
        if (path_dfs(c, f, p)) cycles++;
        adj[f][c] = true;
    }
    h_stack[h_n++] = c;
}
static void dl_unlock(void) { h_n--; }

int main(void) {
    int a = cls("A"), b = cls("B"), c = cls("C"), d = cls("D"), p[N]; char s[128];

    printf("-- 연습 1: DFS vs BFS path for the report\n");
    graph_clear();
    adj[a][b] = adj[b][c] = adj[c][d] = adj[a][d] = true;      /* A->B->C->D 와 지름길 A->D */
    int nd = path_dfs(a, d, p); path_str(p, nd, s, sizeof s); printf("  dfs: %s\n", s);
    CHECK(nd == 4 && strcmp(s, "A -> B -> C -> D") == 0);
    int nb = path_bfs(a, d, p); path_str(p, nb, s, sizeof s); printf("  bfs: %s\n", s);
    CHECK(nb == 2 && strcmp(s, "A -> D") == 0);
    CHECK(path_bfs(d, a, p) == 0 && path_dfs(d, a, p) == 0);

    printf("-- 연습 2: rank checker catches log->sensor on its FIRST run\n");
    const rlock_t sensor = { 10, "sensor" }, spi = { 20, "spi_bus" }, logl = { 30, "log" };
    CHECK(rank_lock(&sensor) && rank_lock(&spi));            /* sensor_task: 10 -> 20 OK */
    rank_unlock(&spi); rank_unlock(&sensor);
    CHECK(rank_lock(&logl) && !rank_lock(&sensor) && r_viol == 1);   /* log_flush: 30 -> 10 위반, 첫 실행에서 */
    rank_unlock(&sensor); rank_unlock(&logl);
    CHECK(r_n == 0 && rank_lock(&spi) && rank_lock(&logl) && r_viol == 1);
    rank_unlock(&logl); rank_unlock(&spi);

    printf("-- 연습 3: IRQ-safe vs IRQ-unsafe use of one lock class\n");
    int uart = cls("uart_lock"), stats = cls("stats_lock");
    in_irq = true;  irq_track(uart); in_irq = false;             /* UART RX ISR 가 uart_lock 을 잡는다 */
    irq_off = true; irq_track(uart); irq_off = false;            /* 태스크가 irqsave 로 잡음: 안전 */
    CHECK(irq_reports == 0);
    irq_track(uart);                                             /* 태스크가 IRQ 를 켠 채 잡음: 위험 */
    CHECK(irq_reports == 1 && irq_state[uart] == (USED_IN_IRQ | ENABLED_IRQ));
    irq_track(stats); irq_track(stats);                          /* 태스크에서만 쓰는 락: 문제없음 */
    CHECK(irq_reports == 1 && irq_state[stats] == ENABLED_IRQ);

    printf("-- 연습 4: documented lock order from the recorded graph\n");
    int sen = cls("sensor"), sp = cls("spi_bus"), lg = cls("log");
    bool used[N] = { false }; used[sen] = used[sp] = used[lg] = true;
    graph_clear();
    adj[sen][sp] = adj[sp][lg] = adj[sen][lg] = true;
    int ord[N], n = topo_order(used, ord); path_str(ord, n, s, sizeof s); printf("  order: %s\n", s);
    CHECK(n == 3 && strcmp(s, "sensor -> spi_bus -> log") == 0);
    adj[lg][sen] = true;                                         /* log_flush_buggy 의 간선 */
    CHECK(topo_order(used, ord) == -1);

    printf("-- 연습 5: gate lock G makes A/B order irrelevant, but the graph still has a cycle\n");
    int g = cls("G");
    graph_clear(); cycles = 0;
    dl_lock(g); dl_lock(a); dl_lock(b); dl_unlock(); dl_unlock(); dl_unlock();   /* G, A, B */
    dl_lock(g); dl_lock(b); dl_lock(a); dl_unlock(); dl_unlock(); dl_unlock();   /* G, B, A */
    CHECK(cycles == 1);                                          /* 오탐: G 가 있어 실제로는 못 멈춘다 */
    graph_clear(); cycles = 0;
    dl_lock(a); dl_lock(b); dl_unlock(); dl_unlock();
    dl_lock(b); dl_lock(a); dl_unlock(); dl_unlock();
    CHECK(cycles == 1 && h_n == 0);                              /* 게이트 없음: 진짜 위험, 같은 보고 */

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
