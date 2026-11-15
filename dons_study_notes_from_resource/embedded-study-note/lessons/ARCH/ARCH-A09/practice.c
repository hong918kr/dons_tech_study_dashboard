/* ARCH-A09 추가 연습 — 한 번에 검증
 *   연습 1  부팅 시간 예산: 임계 경로(CPM)와 노드별 여유(slack)
 *   연습 2  설정 오류 메시지: 사이클을 경로로 보고
 *   연습 3  스펙 lint: 체커가 못 보는 "스펙 누락" 일부를 구조 규칙으로 잡기
 *   연습 4  wake 지연: 일부 도메인만 다시 켤 때의 임계 경로
 * 본문 solution.c 의 EDGE-1 그래프를 축소해 그대로 넣었다 (같은 대표값).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXN 24
#define MAXE 64
#define US(x) ((int64_t)(x) * 1000)
#define SYNC (-1)

typedef enum { K_RAIL, K_PLL, K_CLK, K_RST, K_INIT } kind_t;
typedef struct { char name[12]; kind_t kind; int dom; int64_t dur, hz; } node_t;
typedef struct { int from, to; int64_t min_ns; } edge_t;
typedef struct { node_t n[MAXN]; int nn; edge_t e[MAXE]; int ne; int k_sync; bool dom_iso[8]; } graph_t;

static int g_find(const graph_t *g, const char *s) {
    for (int i = 0; i < g->nn; i++) if (strcmp(g->n[i].name, s) == 0) return i;
    return -1;
}
static void g_node(graph_t *g, const char *nm, kind_t k, int dom, int64_t dur, int64_t hz) {
    node_t *n = &g->n[g->nn++];
    snprintf(n->name, sizeof n->name, "%s", nm);
    n->kind = k; n->dom = dom; n->dur = dur; n->hz = hz;
}
static int64_t sync_ns(int64_t hz, int k) { return ((int64_t)k * 1000000000LL + hz - 1) / hz; }
static void g_edge(graph_t *g, const char *a, const char *b, int64_t m) {
    int u = g_find(g, a), v = g_find(g, b);
    if (u < 0 || v < 0) { fprintf(stderr, "bad edge %s->%s\n", a, b); exit(2); }
    if (m == SYNC) m = sync_ns(g->n[u].hz, g->k_sync);
    g->e[g->ne++] = (edge_t){u, v, m};
}
static void g_del(graph_t *g, const char *a, const char *b) {
    int u = g_find(g, a), v = g_find(g, b);
    for (int k = 0; k < g->ne; k++) if (g->e[k].from == u && g->e[k].to == v) { g->e[k] = g->e[--g->ne]; return; }
}
static void build_soc(graph_t *g) {          /* 도메인: 0 AON, 1 ANA, 2 CORE(iso), 3 IO(iso), 4 DDR(iso) */
    memset(g, 0, sizeof *g);
    g->k_sync = 4;
    g->dom_iso[2] = g->dom_iso[3] = g->dom_iso[4] = true;
    g_node(g, "VDD_AON", K_RAIL, 0, US(300), 0);  g_node(g, "VDD_PLL", K_RAIL, 1, US(100), 0);
    g_node(g, "VDD_CORE", K_RAIL, 2, US(250), 0); g_node(g, "VDD_IO", K_RAIL, 3, US(400), 0);
    g_node(g, "VDD_DDR", K_RAIL, 4, US(200), 0);  g_node(g, "CLK_AON", K_CLK, 0, 0, 32768);
    g_node(g, "PLL_SYS", K_PLL, 1, US(50), 0);    g_node(g, "PLL_DDR", K_PLL, 1, US(80), 0);
    g_node(g, "CLK_CORE", K_CLK, 2, 0, 800000000); g_node(g, "CLK_IO", K_CLK, 3, 0, 100000000);
    g_node(g, "CLK_DDR", K_CLK, 4, 0, 400000000); g_node(g, "RST_AON", K_RST, 0, 0, 0);
    g_node(g, "RST_CORE", K_RST, 2, 0, 0);        g_node(g, "RST_IO", K_RST, 3, 0, 0);
    g_node(g, "RST_DDR", K_RST, 4, 0, 0);         g_node(g, "INIT_UART", K_INIT, 3, US(20), 0);
    g_node(g, "INIT_DDRC", K_INIT, 4, US(150), 0);
    g_edge(g, "VDD_AON", "VDD_PLL", 0);       g_edge(g, "VDD_AON", "VDD_CORE", US(50));
    g_edge(g, "VDD_AON", "VDD_IO", US(100));  g_edge(g, "VDD_AON", "VDD_DDR", US(150));
    g_edge(g, "VDD_AON", "CLK_AON", US(200)); g_edge(g, "VDD_PLL", "PLL_SYS", US(10));
    g_edge(g, "VDD_PLL", "PLL_DDR", US(10));  g_edge(g, "VDD_DDR", "PLL_DDR", 0);
    g_edge(g, "PLL_SYS", "CLK_CORE", 0);      g_edge(g, "VDD_CORE", "CLK_CORE", 0);
    g_edge(g, "PLL_SYS", "CLK_IO", 0);        g_edge(g, "VDD_IO", "CLK_IO", 0);
    g_edge(g, "PLL_DDR", "CLK_DDR", 0);       g_edge(g, "VDD_DDR", "CLK_DDR", 0);
    g_edge(g, "CLK_AON", "RST_AON", SYNC);    g_edge(g, "CLK_CORE", "RST_CORE", SYNC);
    g_edge(g, "RST_AON", "RST_CORE", 0);      g_edge(g, "CLK_IO", "RST_IO", SYNC);
    g_edge(g, "RST_AON", "RST_IO", 0);        g_edge(g, "CLK_DDR", "RST_DDR", SYNC);
    g_edge(g, "RST_CORE", "RST_DDR", 0);      g_edge(g, "RST_IO", "INIT_UART", US(1));
    g_edge(g, "RST_DDR", "INIT_DDRC", US(10)); g_edge(g, "RST_CORE", "INIT_DDRC", 0);
}
static int topo(const graph_t *g, int *out) {
    int indeg[MAXN] = {0}, k = 0;
    bool used[MAXN] = {false};
    for (int i = 0; i < g->ne; i++) indeg[g->e[i].to]++;
    while (k < g->nn) {
        int p = -1;
        for (int v = 0; v < g->nn && p < 0; v++) if (!used[v] && indeg[v] == 0) p = v;
        if (p < 0) return -1;
        used[p] = true; out[k++] = p;
        for (int i = 0; i < g->ne; i++) if (g->e[i].from == p) indeg[g->e[i].to]--;
    }
    return k;
}

/* ---- 연습 1: CPM. ES = max(EF[u] + lag), EF = ES + dur; 역방향 LF = min(LS[w] - lag), LS = LF - dur; slack = LS - ES ---- */
static int64_t cpm(const graph_t *g, int64_t *es, int64_t *slack) {
    int ord[MAXN];
    int64_t ef[MAXN], lf[MAXN], T = 0;
    if (topo(g, ord) != g->nn) return -1;
    for (int i = 0; i < g->nn; i++) {
        int v = ord[i];
        es[v] = 0;
        for (int k = 0; k < g->ne; k++)
            if (g->e[k].to == v && ef[g->e[k].from] + g->e[k].min_ns > es[v]) es[v] = ef[g->e[k].from] + g->e[k].min_ns;
        ef[v] = es[v] + g->n[v].dur;
        if (ef[v] > T) T = ef[v];
    }
    for (int i = g->nn - 1; i >= 0; i--) {
        int v = ord[i];
        lf[v] = T;
        for (int k = 0; k < g->ne; k++)
            if (g->e[k].from == v) {
                int w = g->e[k].to;
                int64_t ls_w = lf[w] - g->n[w].dur;
                if (ls_w - g->e[k].min_ns < lf[v]) lf[v] = ls_w - g->e[k].min_ns;
            }
        slack[v] = lf[v] - g->n[v].dur - es[v];
    }
    return T;
}

/* ---- 연습 2: 사이클 경로. Kahn 이 멈춘 뒤 남은 노드에서 선행자를 따라가다 다시 만난 곳이 사이클 ---- */
static int find_cycle(const graph_t *g, int *cyc) {
    int indeg[MAXN] = {0};
    bool used[MAXN] = {false};
    for (int i = 0; i < g->ne; i++) indeg[g->e[i].to]++;
    for (bool ch = true; ch;) {
        ch = false;
        for (int v = 0; v < g->nn; v++)
            if (!used[v] && indeg[v] == 0) {
                used[v] = true; ch = true;
                for (int i = 0; i < g->ne; i++) if (g->e[i].from == v) indeg[g->e[i].to]--;
            }
    }
    int start = -1;
    for (int v = 0; v < g->nn && start < 0; v++) if (!used[v]) start = v;
    if (start < 0) return 0;
    int seen[MAXN], walk[MAXN + 1], n = 0, v = start;
    for (int i = 0; i < MAXN; i++) seen[i] = -1;
    while (seen[v] < 0) {                      /* 남은 노드는 모두 남은 선행자를 가진다 */
        seen[v] = n; walk[n++] = v;
        int p = -1;
        for (int i = 0; i < g->ne && p < 0; i++) if (g->e[i].to == v && !used[g->e[i].from]) p = g->e[i].from;
        v = p;
    }
    int len = n - seen[v];                     /* walk[seen[v] .. n-1] 이 역방향 사이클 */
    for (int i = 0; i < len; i++) cyc[i] = walk[n - 1 - i];
    return len;
}
static bool has_edge(const graph_t *g, int u, int v) {
    for (int i = 0; i < g->ne; i++) if (g->e[i].from == u && g->e[i].to == v) return true;
    return false;
}

/* ---- 연습 3: 스펙 lint. 반환 = 지적 수, msgs 에 한 줄씩 ---- */
static int lint(const graph_t *g, char msgs[][96], int max) {
    int n = 0;
#define NOTE(...) do { if (n < max) snprintf(msgs[n], 96, __VA_ARGS__); n++; } while (0)
    for (int v = 0; v < g->nn; v++) {
        const node_t *x = &g->n[v];
        bool rail_in = false, pll_in = false, clk_ok = false, clk_in = false;
        for (int k = 0; k < g->ne; k++) {
            if (g->e[k].to != v) continue;
            const node_t *u = &g->n[g->e[k].from];
            if (u->kind == K_RAIL && u->dom == x->dom) rail_in = true;
            if (u->kind == K_RAIL) pll_in = true;
            if (u->kind == K_CLK && u->dom == x->dom) { clk_in = true; clk_ok = g->e[k].min_ns >= sync_ns(u->hz, g->k_sync); }
        }
        if (x->kind == K_CLK && !rail_in) NOTE("L1 %s: no edge from its domain rail", x->name);
        if (x->kind == K_PLL && !pll_in) NOTE("L2 %s: no supply rail edge", x->name);
        if (x->kind == K_RST && !clk_in) NOTE("L3 %s: no edge from its domain clock", x->name);
        if (x->kind == K_RST && clk_in && !clk_ok) NOTE("L4 %s: clock->reset delay < %d cycles", x->name, g->k_sync);
    }
    for (int d = 0; d < 8; d++) {
        bool rst = false;
        for (int v = 0; v < g->nn; v++) rst = rst || (g->n[v].dom == d && g->n[v].kind == K_RST);
        if (g->dom_iso[d] && !rst) NOTE("L5 domain %d: isolation without a reset to gate ISO_OFF", d);
    }
#undef NOTE
    return n;
}

/* ---- 연습 4: wake 지연. closure(root) 밖 노드는 이미 켜져 있다 (완료 = -무한대 취급) ---- */
static int64_t wake_latency(const graph_t *g, int root) {
    bool m[MAXN] = {false};
    m[root] = true;
    for (bool ch = true; ch;) {
        ch = false;
        for (int i = 0; i < g->ne; i++) if (m[g->e[i].from] && !m[g->e[i].to]) { m[g->e[i].to] = true; ch = true; }
    }
    int ord[MAXN];
    int64_t ef[MAXN], T = 0;
    if (topo(g, ord) != g->nn) return -1;
    for (int i = 0; i < g->nn; i++) {
        int v = ord[i];
        if (!m[v]) continue;
        int64_t es = 0;                                   /* wake 명령 시각 = 0 */
        for (int k = 0; k < g->ne; k++)
            if (g->e[k].to == v && m[g->e[k].from] && ef[g->e[k].from] + g->e[k].min_ns > es) es = ef[g->e[k].from] + g->e[k].min_ns;
        ef[v] = es + g->n[v].dur;
        if (ef[v] > T) T = ef[v];
    }
    return T;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    graph_t g;
    build_soc(&g);
    int64_t es[MAXN], sl[MAXN];
    /* 연습 1 */
    int64_t T = cpm(&g, es, sl);
    CHECK(T == 890010);
    printf("ex1: T=%lld ns, critical:", (long long)T);
    const char *crit_want[] = {"VDD_AON", "VDD_DDR", "PLL_DDR", "CLK_DDR", "RST_DDR", "INIT_DDRC"};
    int ncrit = 0;
    for (int v = 0; v < g.nn; v++) if (sl[v] == 0) { printf(" %s", g.n[v].name); ncrit++; }
    printf("\n     slack: VDD_IO=%lld VDD_CORE=%lld RST_CORE=%lld PLL_SYS=%lld ns\n", (long long)sl[g_find(&g, "VDD_IO")],
           (long long)sl[g_find(&g, "VDD_CORE")], (long long)sl[g_find(&g, "RST_CORE")], (long long)sl[g_find(&g, "PLL_SYS")]);
    bool cw = ncrit == 6;
    for (int i = 0; i < 6; i++) cw = cw && sl[g_find(&g, crit_want[i])] == 0;
    CHECK(cw);
    CHECK(sl[g_find(&g, "VDD_IO")] == 68970 && sl[g_find(&g, "RST_CORE")] == 107939);
    for (int v = 0; v < g.nn; v++) CHECK(sl[v] >= 0);                 /* 여유는 음수가 될 수 없다 */
    graph_t g2 = g;                                                    /* DDR rail 을 50 µs 빠르게 → T 도 50 µs 감소 */
    g2.n[g_find(&g2, "VDD_DDR")].dur = US(150);
    CHECK(cpm(&g2, es, sl) == 840010);
    g2 = g;                                                            /* IO rail 을 50 µs 빠르게 → T 그대로 (여유 68.97 µs) */
    g2.n[g_find(&g2, "VDD_IO")].dur = US(350);
    CHECK(cpm(&g2, es, sl) == 890010);

    /* 연습 2 */
    int cyc[MAXN], tmp[MAXN];
    CHECK(find_cycle(&g, cyc) == 0);
    g2 = g;
    g_edge(&g2, "INIT_DDRC", "VDD_DDR", 0);                            /* "training 이 끝나야 DDR 레일을 켠다" — 모순 */
    int L = find_cycle(&g2, cyc);
    bool okc = L >= 2;
    for (int i = 0; i < L && okc; i++) okc = has_edge(&g2, cyc[i], cyc[(i + 1) % L]);
    CHECK(okc && topo(&g2, tmp) == -1);
    L = find_cycle(&g2, cyc);
    printf("ex2: cycle (%d):", L);
    for (int i = 0; i < L; i++) printf(" %s ->", g2.n[cyc[i]].name);
    printf(" %s\n", g2.n[cyc[0]].name);

    /* 연습 3 */
    char msgs[16][96];
    CHECK(lint(&g, msgs, 16) == 0);
    g2 = g; g_del(&g2, "VDD_CORE", "CLK_CORE");
    int nl = lint(&g2, msgs, 16);
    CHECK(nl == 1 && strncmp(msgs[0], "L1 CLK_CORE", 11) == 0);
    g2 = g;
    for (int k = 0; k < g2.ne; k++) if (g2.n[g2.e[k].from].kind == K_CLK && g2.n[g2.e[k].to].kind == K_RST) g2.e[k].min_ns = 0;
    CHECK(lint(&g2, msgs, 16) == 4);
    g2 = g; g_del(&g2, "VDD_DDR", "PLL_DDR");                          /* 본문 Part B 의 스펙 버그 */
    nl = lint(&g2, msgs, 16);
    CHECK(nl == 0);                                                    /* lint 도 못 본다: PLL 은 VDD_PLL 간선이 있다 */
    printf("ex3: lint(soc)=0, lint(no VDD_CORE->CLK_CORE)=1, lint(k_sync=0)=4, lint(no VDD_DDR->PLL_DDR)=%d\n", nl);

    /* 연습 4 */
    int64_t wc = wake_latency(&g, g_find(&g, "VDD_CORE")), wd = wake_latency(&g, g_find(&g, "VDD_DDR")),
            wi = wake_latency(&g, g_find(&g, "VDD_IO"));
    CHECK(wc == US(410) + 5 && wd == US(440) + 10 && wi == US(421) + 40);
    printf("ex4: wake latency CORE=%lld DDR=%lld IO=%lld ns\n", (long long)wc, (long long)wd, (long long)wi);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
