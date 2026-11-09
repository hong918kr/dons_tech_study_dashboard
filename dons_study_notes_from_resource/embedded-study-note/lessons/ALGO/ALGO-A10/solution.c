/* ALGO-A10 강한 연결 요소: 락 의존 그래프 데드락 검출
 *
 * 오프라인 분석기: 스레드별 락 이벤트 로그 → 락 순서 그래프(lock-order graph) → SCC → 보고서.
 *   로그 한 줄:  t=<thread> acq <lock> @<site>     (락 획득, site = 소스 위치)
 *                t=<thread> rel <lock>             (락 해제)      '#' 뒤는 주석
 *   Part A  반복(iterative) Tarjan SCC + Kosaraju SCC, 락 순서 그래프 만들기(held → new 간선)
 *   Part B  SCC 마다 증거 사이클(witness) 1개 + 판정(DEADLOCK / GATED / SINGLE-THREAD / SELF-LOOP)
 *   Part C  펌웨어 드라이버 로그(SPI·DMA·전원 도메인·로거, 4 스레드) 보고서 + 축약 그래프 위상 순서 = 락 계층 제안
 *
 * 스레드는 쓰지 않는다 (로그를 읽는 오프라인 도구). 모든 테스트는 결정적(고정 로그, xorshift32 시드).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st   (solution.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================= 공통: CSR 그래프 ======================= */
/* 정점 v 의 나가는 간선 = adj[off[v] .. off[v+1]-1].  eid[k] = 그 간선의 원래 입력 번호 (주석 찾기용). */
typedef struct {
    int n, m;
    int *off, *adj, *eid;
} graph_t;

static void graph_free(graph_t *g) {
    free(g->off); free(g->adj); free(g->eid);
    memset(g, 0, sizeof *g);
}

/* 간선 목록 → CSR (계수 정렬, 같은 출발점 안에서는 입력 순서 유지 → 결과가 결정적). 0 = 성공 */
static int graph_build(graph_t *g, int n, int m, const int *src, const int *dst) {
    memset(g, 0, sizeof *g);
    g->n = n; g->m = m;
    g->off = calloc((size_t)n + 1, sizeof(int));
    g->adj = malloc(((size_t)m + 1) * sizeof(int));
    g->eid = malloc(((size_t)m + 1) * sizeof(int));
    int *pos = malloc(((size_t)n + 1) * sizeof(int));
    if (!g->off || !g->adj || !g->eid || !pos) { free(pos); graph_free(g); return -1; }
    for (int k = 0; k < m; k++) g->off[src[k] + 1]++;
    for (int v = 0; v < n; v++) g->off[v + 1] += g->off[v];
    memcpy(pos, g->off, (size_t)n * sizeof(int));
    for (int k = 0; k < m; k++) { int p = pos[src[k]]++; g->adj[p] = dst[k]; g->eid[p] = k; }
    free(pos);
    return 0;
}

/* ======================= Part A — SCC 두 가지 ======================= */
/* comp[v] 에 0..ncomp-1 의 성분 번호를 쓰고 ncomp 를 반환 (메모리 부족 = -1).
 * 재귀 금지: 정점 10^6 개짜리 사슬에서도 호출 스택을 쓰지 않아야 한다 (5. 풀이, demo_recursion.c).
 * Tarjan 의 성질: 성분이 "역 위상 순서"로 완성된다 → 성분 사이 간선 u→v 는 comp[u] > comp[v]. */
static int scc_tarjan(const graph_t *g, int *comp) {
    int n = g->n, counter = 0, sp = 0, ncomp = 0;
    int *idx = malloc(((size_t)n + 1) * sizeof(int));   /* 방문 순서 (-1 = 미방문) */
    int *low = malloc(((size_t)n + 1) * sizeof(int));   /* low-link */
    int *stk = malloc(((size_t)n + 1) * sizeof(int));   /* Tarjan 스택 (아직 성분이 안 정해진 정점) */
    int *cs  = malloc(((size_t)n + 1) * sizeof(int));   /* 명시적 호출 스택: 정점 */
    int *it  = malloc(((size_t)n + 1) * sizeof(int));   /* 명시적 호출 스택: 다음에 볼 간선 위치 */
    bool *on = calloc((size_t)n + 1, sizeof(bool));
    if (!idx || !low || !stk || !cs || !it || !on) { ncomp = -1; goto out; }
    for (int v = 0; v < n; v++) idx[v] = -1;
    for (int s = 0; s < n; s++) {
        if (idx[s] != -1) continue;
        int cp = 0;
        idx[s] = low[s] = counter++; stk[sp++] = s; on[s] = true;
        cs[cp] = s; it[cp] = g->off[s]; cp++;
        while (cp > 0) {
            int v = cs[cp - 1];
            if (it[cp - 1] < g->off[v + 1]) {           /* 아직 볼 간선이 남았다 */
                int w = g->adj[it[cp - 1]++];
                if (idx[w] == -1) {                     /* 트리 간선: "재귀 호출" = push */
                    idx[w] = low[w] = counter++; stk[sp++] = w; on[w] = true;
                    cs[cp] = w; it[cp] = g->off[w]; cp++;
                } else if (on[w] && idx[w] < low[v]) {  /* 스택 위 정점으로 가는 간선 */
                    low[v] = idx[w];
                }
            } else {                                    /* v 의 간선 끝: "return" = pop */
                if (low[v] == idx[v]) {                 /* v 가 성분의 뿌리 → 스택에서 v 까지 꺼낸다 */
                    int w;
                    do { w = stk[--sp]; on[w] = false; comp[w] = ncomp; } while (w != v);
                    ncomp++;
                }
                cp--;
                if (cp > 0) {                           /* 부모의 low 갱신 (재귀판의 low[u]=min(low[u],low[v])) */
                    int u = cs[cp - 1];
                    if (low[v] < low[u]) low[u] = low[v];
                }
            }
        }
    }
out:
    free(idx); free(low); free(stk); free(cs); free(it); free(on);
    return ncomp;
}

/* Kosaraju: (1) G 에서 DFS 끝난 순서(post-order) 기록 (2) 역그래프 G^T 에서 끝난 순서의 역순으로 DFS.
 * 같은 반환 규약. 성분은 "위상 순서"로 나온다 → 성분 사이 간선 u→v 는 comp[u] < comp[v]. 재귀 금지. */
static int scc_kosaraju(const graph_t *g, int *comp) {
    int n = g->n, m = g->m, ncomp = 0, nord = 0;
    int *order = malloc(((size_t)n + 1) * sizeof(int));
    int *cs    = malloc(((size_t)n + 1) * sizeof(int));
    int *it    = malloc(((size_t)n + 1) * sizeof(int));
    bool *seen = calloc((size_t)n + 1, sizeof(bool));
    int *src   = malloc(((size_t)m + 1) * sizeof(int));
    int *dst   = malloc(((size_t)m + 1) * sizeof(int));
    graph_t t = {0};
    if (!order || !cs || !it || !seen || !src || !dst) { ncomp = -1; goto out; }
    for (int s = 0; s < n; s++) {                       /* 1단계: 반복 DFS, 끝나는 순서대로 order[] */
        if (seen[s]) continue;
        int cp = 0;
        seen[s] = true; cs[cp] = s; it[cp] = g->off[s]; cp++;
        while (cp > 0) {
            int v = cs[cp - 1];
            if (it[cp - 1] < g->off[v + 1]) {
                int w = g->adj[it[cp - 1]++];
                if (!seen[w]) { seen[w] = true; cs[cp] = w; it[cp] = g->off[w]; cp++; }
            } else {
                order[nord++] = v; cp--;
            }
        }
    }
    for (int v = 0; v < n; v++)                         /* 2단계: 역그래프 */
        for (int k = g->off[v]; k < g->off[v + 1]; k++) { src[k] = g->adj[k]; dst[k] = v; }
    if (graph_build(&t, n, m, src, dst) != 0) { ncomp = -1; goto out; }
    for (int v = 0; v < n; v++) comp[v] = -1;
    for (int i = nord - 1; i >= 0; i--) {               /* 늦게 끝난 정점부터 = 원래 그래프의 "소스" 성분부터 */
        int s = order[i];
        if (comp[s] != -1) continue;
        int top = 0;
        cs[top++] = s; comp[s] = ncomp;
        while (top > 0) {                               /* 방문 순서는 상관없다: 단순 스택 */
            int v = cs[--top];
            for (int k = t.off[v]; k < t.off[v + 1]; k++) {
                int w = t.adj[k];
                if (comp[w] == -1) { comp[w] = ncomp; cs[top++] = w; }
            }
        }
        ncomp++;
    }
out:
    graph_free(&t);
    free(order); free(cs); free(it); free(seen); free(src); free(dst);
    return ncomp;
}

/* 두 알고리즘의 번호는 다르다 → 비교용 정규형: canon[v] = v 가 속한 성분의 가장 작은 정점 번호.
 * comp 값이 범위 밖이면(스텁 등) 0 반환. */
static int scc_canon(int n, const int *comp, int ncomp, int *canon) {
    if (ncomp <= 0 || ncomp > n) return 0;
    int *minv = malloc((size_t)ncomp * sizeof(int));
    if (!minv) return 0;
    for (int c = 0; c < ncomp; c++) minv[c] = n;
    for (int v = 0; v < n; v++) {
        if (comp[v] < 0 || comp[v] >= ncomp) { free(minv); return 0; }
        if (v < minv[comp[v]]) minv[comp[v]] = v;
    }
    for (int v = 0; v < n; v++) canon[v] = minv[comp[v]];
    free(minv);
    return 1;
}

/* ======================= 락 이벤트 로그 → 락 순서 그래프 ======================= */
#define NAME_LEN 48
#define MAX_THREADS 64            /* 스레드 집합을 uint64_t 비트마스크로 */
#define MAX_HELD 16               /* 스레드 하나가 동시에 쥐는 락 수 상한 */

typedef struct {                  /* 문자열 → 번호 (처음 나온 순서대로 0,1,2,...) */
    char (*name)[NAME_LEN];
    int n, cap;
    int *h; int hcap;             /* 열린 주소법 해시: 이름 번호, -1 = 빈칸 */
} strtab_t;

static uint32_t fnv1a(const char *s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return h;
}
static int st_find(const strtab_t *t, const char *s, size_t len) {
    if (t->hcap == 0) return -1;
    for (uint32_t i = fnv1a(s, len) & (uint32_t)(t->hcap - 1);; i = (i + 1) & (uint32_t)(t->hcap - 1)) {
        int id = t->h[i];
        if (id < 0) return -1;
        if (strlen(t->name[id]) == len && memcmp(t->name[id], s, len) == 0) return id;
    }
}
static int st_add(strtab_t *t, const char *s, size_t len) {     /* 있으면 그 번호, 없으면 추가. -1 = 실패 */
    int id = st_find(t, s, len);
    if (id >= 0) return id;
    if (len == 0 || len >= NAME_LEN) return -1;
    if ((t->n + 1) * 2 > t->hcap) {                              /* 적재율 50% 넘으면 해시 두 배 */
        int nh = t->hcap ? t->hcap * 2 : 64;
        int *h = malloc((size_t)nh * sizeof(int));
        if (!h) return -1;
        for (int i = 0; i < nh; i++) h[i] = -1;
        for (int k = 0; k < t->n; k++) {
            uint32_t i = fnv1a(t->name[k], strlen(t->name[k])) & (uint32_t)(nh - 1);
            while (h[i] >= 0) i = (i + 1) & (uint32_t)(nh - 1);
            h[i] = k;
        }
        free(t->h); t->h = h; t->hcap = nh;
    }
    if (t->n == t->cap) {
        int nc = t->cap ? t->cap * 2 : 32;
        void *p = realloc(t->name, (size_t)nc * NAME_LEN);
        if (!p) return -1;
        t->name = p; t->cap = nc;
    }
    memcpy(t->name[t->n], s, len); t->name[t->n][len] = '\0';
    uint32_t i = fnv1a(s, len) & (uint32_t)(t->hcap - 1);
    while (t->h[i] >= 0) i = (i + 1) & (uint32_t)(t->hcap - 1);
    t->h[i] = t->n;
    return t->n++;
}
static void st_free(strtab_t *t) { free(t->name); free(t->h); memset(t, 0, sizeof *t); }

typedef struct { int lock, site; } held_t;

typedef struct {                  /* 락 순서 간선 from → to = "from 을 쥔 채 to 를 잡았다" */
    int from, to;
    int thread, site_from, site_to, line;   /* 처음 본 증거: 누가, 어디서 from 을 잡았고, 어디서 to 를 잡았나 */
    int count;                              /* 몇 번 관찰됐나 */
    uint64_t threads;                       /* 이 간선을 만든 스레드 집합 */
    int ngate, gate[MAX_HELD];              /* 게이트 후보: "매번" 같이 쥐고 있던 다른 락 (교집합) */
} ledge_t;

typedef struct {
    strtab_t locks, threads, sites;
    ledge_t *e; int ne, ecap;
    int *eh; int ehcap;                     /* (from,to) → 간선 번호 해시 */
    held_t held[MAX_THREADS][MAX_HELD];     /* 스레드별 "지금 쥐고 있는 락" 스택 */
    int nheld[MAX_THREADS];
    int warnings;                           /* 안 쥔 락 해제 등 */
    char err[160];
} lockgraph_t;

static void lg_init(lockgraph_t *lg) { memset(lg, 0, sizeof *lg); }
static void lg_free(lockgraph_t *lg) {
    st_free(&lg->locks); st_free(&lg->threads); st_free(&lg->sites);
    free(lg->e); free(lg->eh); memset(lg, 0, sizeof *lg);
}
static uint32_t edge_hash(int from, int to) {
    uint64_t k = ((uint64_t)(uint32_t)from << 32) | (uint32_t)to;
    k ^= k >> 33; k *= 0xff51afd7ed558ccdull; k ^= k >> 33;
    return (uint32_t)k;
}
static int lg_find_edge(const lockgraph_t *lg, int from, int to) {
    if (lg->ehcap == 0) return -1;
    for (uint32_t i = edge_hash(from, to) & (uint32_t)(lg->ehcap - 1);; i = (i + 1) & (uint32_t)(lg->ehcap - 1)) {
        int k = lg->eh[i];
        if (k < 0) return -1;
        if (lg->e[k].from == from && lg->e[k].to == to) return k;
    }
}
/* 간선 하나 기록: 새 간선이면 추가(증거 저장), 있던 간선이면 count/threads 누적 + 게이트 교집합. 0 = 성공 */
static int lg_add_edge(lockgraph_t *lg, int from, int to, int thread, int site_from, int site_to,
                       int line, const int *gates, int ngates) {
    int k = lg_find_edge(lg, from, to);
    if (k >= 0) {
        ledge_t *e = &lg->e[k];
        e->count++;
        e->threads |= 1ull << thread;
        int keep = 0;
        for (int i = 0; i < e->ngate; i++)
            for (int j = 0; j < ngates; j++)
                if (e->gate[i] == gates[j]) { e->gate[keep++] = e->gate[i]; break; }
        e->ngate = keep;
        return 0;
    }
    if ((lg->ne + 1) * 2 > lg->ehcap) {
        int nh = lg->ehcap ? lg->ehcap * 2 : 64;
        int *h = malloc((size_t)nh * sizeof(int));
        if (!h) return -1;
        for (int i = 0; i < nh; i++) h[i] = -1;
        for (int j = 0; j < lg->ne; j++) {
            uint32_t i = edge_hash(lg->e[j].from, lg->e[j].to) & (uint32_t)(nh - 1);
            while (h[i] >= 0) i = (i + 1) & (uint32_t)(nh - 1);
            h[i] = j;
        }
        free(lg->eh); lg->eh = h; lg->ehcap = nh;
    }
    if (lg->ne == lg->ecap) {
        int nc = lg->ecap ? lg->ecap * 2 : 32;
        void *p = realloc(lg->e, (size_t)nc * sizeof(ledge_t));
        if (!p) return -1;
        lg->e = p; lg->ecap = nc;
    }
    ledge_t *e = &lg->e[lg->ne];
    memset(e, 0, sizeof *e);
    e->from = from; e->to = to; e->thread = thread; e->site_from = site_from; e->site_to = site_to;
    e->line = line; e->count = 1; e->threads = 1ull << thread;
    e->ngate = ngates;
    memcpy(e->gate, gates, (size_t)ngates * sizeof(int));
    uint32_t i = edge_hash(from, to) & (uint32_t)(lg->ehcap - 1);
    while (lg->eh[i] >= 0) i = (i + 1) & (uint32_t)(lg->ehcap - 1);
    lg->eh[i] = lg->ne++;
    return 0;
}

/* Part A: 스레드 t 가 site 에서 lock 을 잡으려 한다 (아직 held 스택에 넣기 전).
 * 지금 쥐고 있는 모든 락 h 에 대해 간선 h → lock 을 lg_add_edge 로 기록한다.
 * 그 간선의 게이트 후보 = 그 순간 쥔 락 중 h 자신을 뺀 나머지 전부. 0 = 성공, -1 = 실패 */
static int lg_acquire_edges(lockgraph_t *lg, int t, int lock, int site, int line) {
    const held_t *h = lg->held[t];
    int nh = lg->nheld[t];
    for (int i = 0; i < nh; i++) {
        int gates[MAX_HELD], ng = 0;
        for (int j = 0; j < nh; j++)
            if (j != i && h[j].lock != h[i].lock) gates[ng++] = h[j].lock;
        if (lg_add_edge(lg, h[i].lock, lock, t, h[i].site, site, line, gates, ng) != 0) return -1;
    }
    return 0;
}

static int lg_fail(lockgraph_t *lg, int line, const char *msg) {
    snprintf(lg->err, sizeof lg->err, "line %d: %s", line, msg);
    return -1;
}
/* 로그 전체를 읽는다. 0 = 성공, -1 = 형식 오류 (lg->err 에 줄 번호와 이유) */
static int lg_parse(lockgraph_t *lg, const char *text) {
    int line = 0;
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char buf[256];
        line++;
        if (len >= sizeof buf) return lg_fail(lg, line, "line too long");
        memcpy(buf, p, len); buf[len] = '\0';
        p += len + (eol ? 1 : 0);
        char *hash = strchr(buf, '#');
        if (hash) *hash = '\0';
        char *tok[5]; int nt = 0;
        for (char *q = buf; *q && nt < 5;) {               /* 공백으로 토큰 나누기 */
            while (*q == ' ' || *q == '\t' || *q == '\r') *q++ = '\0';
            if (!*q) break;
            tok[nt++] = q;
            while (*q && *q != ' ' && *q != '\t' && *q != '\r') q++;
        }
        if (nt == 0) continue;
        if (strncmp(tok[0], "t=", 2) != 0) return lg_fail(lg, line, "expected t=<thread>");
        int t = st_add(&lg->threads, tok[0] + 2, strlen(tok[0] + 2));
        if (t < 0 || t >= MAX_THREADS) return lg_fail(lg, line, "bad thread name or too many threads");
        if (nt == 4 && strcmp(tok[1], "acq") == 0 && tok[3][0] == '@') {
            int lock = st_add(&lg->locks, tok[2], strlen(tok[2]));
            int site = st_add(&lg->sites, tok[3] + 1, strlen(tok[3] + 1));
            if (lock < 0 || site < 0) return lg_fail(lg, line, "bad lock/site name");
            if (lg->nheld[t] == MAX_HELD) return lg_fail(lg, line, "held-lock stack overflow");
            if (lg_acquire_edges(lg, t, lock, site, line) != 0) return lg_fail(lg, line, "out of memory");
            lg->held[t][lg->nheld[t]++] = (held_t){lock, site};
        } else if (nt == 3 && strcmp(tok[1], "rel") == 0) {
            int lock = st_find(&lg->locks, tok[2], strlen(tok[2]));
            int i = lg->nheld[t] - 1;
            while (i >= 0 && lg->held[t][i].lock != lock) i--;   /* 위에서부터: 순서 밖 해제도 허용 */
            if (lock < 0 || i < 0) { lg->warnings++; continue; } /* 안 쥔 락 해제 = 경고만 */
            memmove(&lg->held[t][i], &lg->held[t][i + 1], (size_t)(lg->nheld[t] - 1 - i) * sizeof(held_t));
            lg->nheld[t]--;
        } else {
            return lg_fail(lg, line, "expected 'acq <lock> @<site>' or 'rel <lock>'");
        }
    }
    return 0;
}

/* 락 순서 간선 → CSR 그래프 (정점 = 락 번호, eid = lg->e 번호) */
static int lg_graph(const lockgraph_t *lg, graph_t *g) {
    int m = lg->ne;
    int *src = malloc(((size_t)m + 1) * sizeof(int)), *dst = malloc(((size_t)m + 1) * sizeof(int));
    int rc = -1;
    if (src && dst) {
        for (int k = 0; k < m; k++) { src[k] = lg->e[k].from; dst[k] = lg->e[k].to; }
        rc = graph_build(g, lg->locks.n, m, src, dst);
    }
    free(src); free(dst);
    return rc;
}

/* ======================= Part B — 증거 사이클과 판정 ======================= */
static int csr_from(const graph_t *g, int k) {             /* CSR 간선 위치 k → 출발 정점 (이진 탐색) */
    int lo = 0, hi = g->n - 1;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (g->off[mid] <= k) lo = mid; else hi = mid - 1; }
    return lo;
}

/* 성분 comp[s] 안에서만 움직이며 s 로 돌아오는 가장 짧은 사이클을 BFS 로 찾는다.
 * cyc[0..L-1] 에 CSR 간선 위치(g->adj 인덱스)를 사이클 순서대로 (첫 간선은 s 에서 출발), L 반환.
 * 자기 루프 s→s 면 L = 1. 못 찾거나 maxlen 을 넘으면 0.  (간선 위치 → 출발 정점은 csr_from) */
static int scc_witness(const graph_t *g, const int *comp, int s, int *cyc, int maxlen) {
    int n = g->n, L = 0;
    int *par = malloc(((size_t)n + 1) * sizeof(int));     /* par[w] = w 로 들어온 CSR 간선 위치, -1 = 미방문 */
    int *q = malloc(((size_t)n + 1) * sizeof(int));
    if (!par || !q) goto out;
    for (int v = 0; v < n; v++) par[v] = -1;
    int qh = 0, qt = 0;
    q[qt++] = s; par[s] = -2;
    while (qh < qt && L == 0) {
        int v = q[qh++];
        for (int k = g->off[v]; k < g->off[v + 1]; k++) {
            int w = g->adj[k];
            if (comp[w] != comp[s]) continue;               /* 성분 밖으로 나가는 간선은 사이클에 못 낀다 */
            if (w == s) {                                   /* 첫 귀환 = BFS 라서 s 를 지나는 가장 짧은 사이클 */
                int len = 1;
                for (int x = v; x != s; x = csr_from(g, par[x])) len++;
                if (len > maxlen) goto out;
                cyc[len - 1] = k;
                int i = len - 2;
                for (int x = v; x != s; x = csr_from(g, par[x])) cyc[i--] = par[x];
                L = len;
                break;
            }
            if (par[w] == -1) { par[w] = k; q[qt++] = w; }
        }
    }
out:
    free(par); free(q);
    return L;
}

typedef enum { V_DEADLOCK, V_GATED, V_SINGLE_THREAD, V_SELF } verdict_t;
static const char *const verdict_name[] = {"DEADLOCK", "GATED", "SINGLE-THREAD", "SELF-LOOP"};

/* 성분 c (정점 수 size) 의 판정. 성분 "안쪽" 간선(양 끝이 모두 c) 전부를 본다:
 *   size == 1          → V_SELF  (자기 루프가 있는 성분만 호출된다)
 *   게이트 교집합 ≠ ∅   → V_GATED (*gate_out = 교집합 중 가장 작은 락 번호)
 *   스레드 합집합 1개   → V_SINGLE_THREAD
 *   그 밖              → V_DEADLOCK
 * *threads_out = 안쪽 간선들을 만든 스레드 합집합. */
static verdict_t scc_verdict(const lockgraph_t *lg, const graph_t *g, const int *comp, int c, int size,
                             int *gate_out, uint64_t *threads_out) {
    uint64_t th = 0;
    int ng = -1, gate[MAX_HELD];                           /* ng = -1: 아직 첫 간선 전 */
    for (int u = 0; u < g->n; u++) {
        if (comp[u] != c) continue;
        for (int k = g->off[u]; k < g->off[u + 1]; k++) {
            if (comp[g->adj[k]] != c) continue;
            const ledge_t *e = &lg->e[g->eid[k]];
            th |= e->threads;
            if (ng < 0) { ng = e->ngate; memcpy(gate, e->gate, (size_t)ng * sizeof(int)); continue; }
            int keep = 0;
            for (int i = 0; i < ng; i++)
                for (int j = 0; j < e->ngate; j++)
                    if (gate[i] == e->gate[j]) { gate[keep++] = gate[i]; break; }
            ng = keep;
        }
    }
    *threads_out = th;
    *gate_out = -1;
    if (size == 1) return V_SELF;
    if (ng > 0) {
        int best = gate[0];
        for (int i = 1; i < ng; i++) if (gate[i] < best) best = gate[i];
        *gate_out = best;
        return V_GATED;
    }
    if (th != 0 && (th & (th - 1)) == 0) return V_SINGLE_THREAD;
    return V_DEADLOCK;
}

/* ======================= Part C — 락 계층 제안 ======================= */
/* 축약 그래프(condensation: 성분 = 점, 성분 사이 간선)를 Kahn 알고리즘으로 위상 정렬해
 * rank[c] = 0..ncomp-1 (작을수록 먼저 잡아야 할 락) 을 쓴다. 대기열은 성분 번호 순으로 시작(결정적).
 * 성분 사이 간선 u→v 마다 rank[comp[u]] < rank[comp[v]] 이어야 한다. 0 = 성공 */
static int cond_order(const graph_t *g, const int *comp, int ncomp, int *rank) {
    int *indeg = calloc((size_t)ncomp + 1, sizeof(int));
    int *q = malloc(((size_t)ncomp + 1) * sizeof(int));
    int *coff = calloc((size_t)ncomp + 2, sizeof(int));    /* 성분별 정점 목록 (CSR) */
    int *cv = malloc(((size_t)g->n + 1) * sizeof(int));
    int rc = -1;
    if (!indeg || !q || !coff || !cv) goto out;
    for (int u = 0; u < g->n; u++) {
        coff[comp[u] + 1]++;
        for (int k = g->off[u]; k < g->off[u + 1]; k++)
            if (comp[g->adj[k]] != comp[u]) indeg[comp[g->adj[k]]]++;   /* 중복 간선도 그대로 센다(일관되면 OK) */
    }
    for (int c = 0; c < ncomp; c++) coff[c + 1] += coff[c];
    {
        int *pos = q;                                       /* 잠깐 q 를 위치 배열로 빌려 쓴다 */
        memcpy(pos, coff, (size_t)ncomp * sizeof(int));
        for (int u = 0; u < g->n; u++) cv[pos[comp[u]]++] = u;
    }
    int qh = 0, qt = 0, r = 0;
    for (int c = 0; c < ncomp; c++) if (indeg[c] == 0) q[qt++] = c;
    while (qh < qt) {
        int c = q[qh++];
        rank[c] = r++;
        for (int i = coff[c]; i < coff[c + 1]; i++) {
            int u = cv[i];
            for (int k = g->off[u]; k < g->off[u + 1]; k++) {
                int d = comp[g->adj[k]];
                if (d != c && --indeg[d] == 0) q[qt++] = d;
            }
        }
    }
    rc = (r == ncomp) ? 0 : -1;                             /* 다 못 꺼냈으면 축약 그래프에 사이클 = comp 가 틀렸다 */
out:
    free(indeg); free(q); free(coff); free(cv);
    return rc;
}

/* ======================= 분석기 본체 (주어진 코드) ======================= */
#define MAX_CYC 64
#define MAX_REP 64
typedef struct {
    int comp, size, minv;
    verdict_t v; int gate; uint64_t threads;
    int cyclen, cyc[MAX_CYC];      /* 증거 사이클: CSR 간선 위치 */
} sccrep_t;

typedef struct {
    graph_t g;
    int ncomp; int *comp; int *rank;
    int nrep; sccrep_t rep[MAX_REP];
    int count[4];                  /* verdict 별 개수 */
} analysis_t;

static void analysis_free(analysis_t *a) { graph_free(&a->g); free(a->comp); free(a->rank); memset(a, 0, sizeof *a); }

/* 0 = 성공. SCC 가 비정상(스텁 등)이면 -1 */
static int analyze(const lockgraph_t *lg, analysis_t *a) {
    memset(a, 0, sizeof *a);
    if (lg_graph(lg, &a->g) != 0) return -1;
    int n = a->g.n;
    a->comp = malloc(((size_t)n + 1) * sizeof(int));
    if (!a->comp) return -1;
    for (int v = 0; v < n; v++) a->comp[v] = -1;
    a->ncomp = scc_tarjan(&a->g, a->comp);
    if (a->ncomp <= 0 || a->ncomp > n) return -1;
    for (int v = 0; v < n; v++) if (a->comp[v] < 0 || a->comp[v] >= a->ncomp) return -1;
    int *size = calloc((size_t)a->ncomp, sizeof(int)), *minv = malloc((size_t)a->ncomp * sizeof(int));
    bool *self = calloc((size_t)a->ncomp, sizeof(bool));
    a->rank = calloc((size_t)a->ncomp, sizeof(int));
    if (!size || !minv || !self || !a->rank) { free(size); free(minv); free(self); return -1; }
    for (int c = 0; c < a->ncomp; c++) minv[c] = n;
    for (int v = 0; v < n; v++) {
        int c = a->comp[v];
        size[c]++;
        if (v < minv[c]) minv[c] = v;
        for (int k = a->g.off[v]; k < a->g.off[v + 1]; k++) if (a->g.adj[k] == v) self[c] = true;
    }
    for (int v = 0; v < n && a->nrep < MAX_REP; v++) {     /* 보고 순서: 성분의 가장 작은 락 번호 순 */
        int c = a->comp[v];
        if (minv[c] != v || (size[c] == 1 && !self[c])) continue;
        sccrep_t *r = &a->rep[a->nrep++];
        r->comp = c; r->size = size[c]; r->minv = v;
        r->v = scc_verdict(lg, &a->g, a->comp, c, size[c], &r->gate, &r->threads);
        r->cyclen = scc_witness(&a->g, a->comp, v, r->cyc, MAX_CYC);
        a->count[r->v]++;
    }
    free(size); free(minv); free(self);
    if (cond_order(&a->g, a->comp, a->ncomp, a->rank) != 0) memset(a->rank, 0, (size_t)a->ncomp * sizeof(int));
    return 0;
}

static void report_print(FILE *f, const lockgraph_t *lg, const analysis_t *a) {
    fprintf(f, "== lock-order report: %d locks, %d edges, %d threads, %d warnings ==\n",
            lg->locks.n, lg->ne, lg->threads.n, lg->warnings);
    for (int i = 0; i < a->nrep; i++) {
        const sccrep_t *r = &a->rep[i];
        fprintf(f, "[%s", verdict_name[r->v]);
        if (r->v == V_GATED) fprintf(f, " by %s", lg->locks.name[r->gate]);
        fprintf(f, "] SCC {");
        for (int v = 0, first = 1; v < a->g.n; v++)
            if (a->comp[v] == r->comp) { fprintf(f, "%s%s", first ? "" : ", ", lg->locks.name[v]); first = 0; }
        fprintf(f, "}  threads:");
        for (int t = 0; t < lg->threads.n; t++) if (r->threads >> t & 1u) fprintf(f, " %s", lg->threads.name[t]);
        fprintf(f, "\n");
        if (r->cyclen == 0) { fprintf(f, "  (no witness cycle)\n"); continue; }
        for (int j = 0; j < r->cyclen; j++) {
            const ledge_t *e = &lg->e[a->g.eid[r->cyc[j]]];
            fprintf(f, "  %-10s -> %-10s  t=%-8s holds @%s, takes @%s  (log line %d, seen %dx)\n",
                    lg->locks.name[e->from], lg->locks.name[e->to], lg->threads.name[e->thread],
                    lg->sites.name[e->site_from], lg->sites.name[e->site_to], e->line, e->count);
        }
    }
    fprintf(f, "suggested lock order:");
    for (int r = 0; r < a->ncomp; r++)
        for (int v = 0; v < a->g.n; v++)
            if (a->rank[a->comp[v]] == r) {
                bool cyc = false;
                for (int i = 0; i < a->nrep; i++) if (a->rep[i].comp == a->comp[v] && a->rep[i].v == V_DEADLOCK) cyc = true;
                fprintf(f, " %s%s", lg->locks.name[v], cyc ? "*" : "");
            }
    fprintf(f, "   (* = in a DEADLOCK cycle: break it first)\n");
    fprintf(f, "summary: deadlock=%d gated=%d single-thread=%d self=%d\n",
            a->count[V_DEADLOCK], a->count[V_GATED], a->count[V_SINGLE_THREAD], a->count[V_SELF]);
}

/* ======================= 테스트 하네스 ======================= */
#ifndef ALGO_A10_NO_MAIN
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 두 알고리즘을 돌려 정규형을 want(또는 서로)와 비교하고 성분 번호 순서 성질도 확인. 반환: 비트0 Tarjan OK, 비트1 Kosaraju OK */
static int run_both(const graph_t *g, const int *want, int *ord_ok) {
    int n = g->n, res = 0;
    int *c1 = malloc(((size_t)n + 1) * sizeof(int)), *c2 = malloc(((size_t)n + 1) * sizeof(int));
    int *k1 = malloc(((size_t)n + 1) * sizeof(int)), *k2 = malloc(((size_t)n + 1) * sizeof(int));
    for (int v = 0; v < n; v++) c1[v] = c2[v] = -1;
    int n1 = scc_tarjan(g, c1), n2 = scc_kosaraju(g, c2);
    int ok1 = n == 0 ? n1 == 0 : scc_canon(n, c1, n1, k1), ok2 = n == 0 ? n2 == 0 : scc_canon(n, c2, n2, k2);
    if (ok1 && (n == 0 || memcmp(k1, want, (size_t)n * sizeof(int)) == 0)) res |= 1;
    if (ok2 && (n == 0 || memcmp(k2, want, (size_t)n * sizeof(int)) == 0)) res |= 2;
    if (ord_ok && ok1 && ok2) {                       /* Tarjan: 역 위상(c[u] > c[v]), Kosaraju: 위상(c[u] < c[v]) */
        for (int u = 0; u < n; u++)
            for (int k = g->off[u]; k < g->off[u + 1]; k++) {
                int w = g->adj[k];
                if (c1[u] != c1[w] && !(c1[u] > c1[w])) *ord_ok = 0;
                if (c2[u] != c2[w] && !(c2[u] < c2[w])) *ord_ok = 0;
            }
    } else if (ord_ok) *ord_ok = 0;
    free(c1); free(c2); free(k1); free(k2);
    return res;
}

/* 정답 기준(oracle): 비트셋 전이 폐포(Floyd–Warshall). n ≤ 64. u,v 가 서로 도달 가능하면 같은 SCC */
static void oracle_canon(int n, int m, const int *src, const int *dst, int *canon) {
    uint64_t r[64];
    for (int i = 0; i < n; i++) r[i] = 1ull << i;
    for (int k = 0; k < m; k++) r[src[k]] |= 1ull << dst[k];
    for (int k = 0; k < n; k++)
        for (int i = 0; i < n; i++) if (r[i] >> k & 1u) r[i] |= r[k];
    for (int v = 0; v < n; v++) {
        canon[v] = v;
        for (int u = 0; u < v; u++) if ((r[u] >> v & 1u) && (r[v] >> u & 1u)) { canon[v] = u; break; }
    }
}

typedef struct { const char *name; int n, m; int e[8][2]; int canon[8]; } hcase_t;
static const hcase_t hcases[] = {
    {"DAG",            6, 7, {{0,1},{0,2},{1,3},{2,3},{3,4},{4,5},{2,5}}, {0,1,2,3,4,5}},
    {"one big cycle",  6, 6, {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0}},       {0,0,0,0,0,0}},
    {"nested cycles",  6, 7, {{0,1},{1,2},{2,0},{1,3},{3,1},{3,4},{4,5}}, {0,0,0,0,4,5}},
    {"self-loop",      3, 3, {{0,0},{0,1},{1,2}},                         {0,1,2}},
    {"disconnected",   7, 5, {{0,1},{1,0},{3,4},{4,5},{5,3}},             {0,0,2,3,3,3,6}},
    {"two SCC chain",  4, 5, {{0,1},{1,0},{1,2},{2,3},{3,2}},             {0,0,2,2}},
};

static int lock_id(const lockgraph_t *lg, const char *s) { return st_find(&lg->locks, s, strlen(s)); }
static int has_edge(const lockgraph_t *lg, const char *a, const char *b) {
    return lock_id(lg, a) >= 0 && lock_id(lg, b) >= 0 && lg_find_edge(lg, lock_id(lg, a), lock_id(lg, b)) >= 0;
}
/* 증거 사이클이 진짜 사이클인가: minv 에서 출발, 간선이 이어지고, minv 로 돌아온다 */
static int witness_ok(const analysis_t *a, const sccrep_t *r) {
    if (r->cyclen <= 0) return 0;
    int at = r->minv;
    for (int j = 0; j < r->cyclen; j++) {
        int k = r->cyc[j];
        if (csr_from(&a->g, k) != at || a->comp[a->g.adj[k]] != r->comp) return 0;
        at = a->g.adj[k];
    }
    return at == r->minv;
}
static int popcount64(uint64_t x) { int c = 0; while (x) { x &= x - 1; c++; } return c; }

/* 로그 하나를 분석해 a 에 결과. 0 = 성공 */
static int run_log(lockgraph_t *lg, analysis_t *a, const char *text) {
    lg_init(lg);
    memset(a, 0, sizeof *a);
    if (lg_parse(lg, text) != 0) return -1;
    return analyze(lg, a);
}

static const char LOG_ABBA[] =
    "t=T1 acq A @a.c:10\n" "t=T1 acq B @a.c:11\n" "t=T1 rel B\n" "t=T1 rel A\n"
    "t=T2 acq B @b.c:20\n" "t=T2 acq A @b.c:21\n" "t=T2 rel A\n" "t=T2 rel B\n";
static const char LOG_GATED[] =
    "t=T1 acq G @a.c:9\n" "t=T1 acq A @a.c:10\n" "t=T1 acq B @a.c:11\n" "t=T1 rel B\n" "t=T1 rel A\n" "t=T1 rel G\n"
    "t=T2 acq G @b.c:19\n" "t=T2 acq B @b.c:20\n" "t=T2 acq A @b.c:21\n" "t=T2 rel A\n" "t=T2 rel B\n" "t=T2 rel G\n";
static const char LOG_HALF_GATED[] =      /* G 는 한쪽 간선에만 → 게이트 아님 */
    "t=T1 acq G @a.c:9\n" "t=T1 acq A @a.c:10\n" "t=T1 acq B @a.c:11\n" "t=T1 rel B\n" "t=T1 rel A\n" "t=T1 rel G\n"
    "t=T2 acq B @b.c:20\n" "t=T2 acq A @b.c:21\n" "t=T2 rel A\n" "t=T2 rel B\n";
static const char LOG_SINGLE[] =
    "t=T1 acq A @init.c:5\n" "t=T1 acq B @init.c:6\n" "t=T1 rel B\n" "t=T1 rel A\n"
    "t=T1 acq B @run.c:40\n" "t=T1 acq A @run.c:41\n" "t=T1 rel A\n" "t=T1 rel B\n";
static const char LOG_SELF[] = "t=T1 acq A @x.c:1\n" "t=T1 acq A @x.c:2   # 같은 락(클래스) 재획득\n";
static const char LOG_NESTED[] =          /* A→B→C→A 와 B↔D : 성분 하나 {A,B,C,D} */
    "t=T1 acq A @n.c:1\n t=T1 acq B @n.c:2\n t=T1 rel B\n t=T1 rel A\n"
    "t=T2 acq B @n.c:3\n t=T2 acq C @n.c:4\n t=T2 rel C\n t=T2 rel B\n"
    "t=T3 acq C @n.c:5\n t=T3 acq A @n.c:6\n t=T3 rel A\n t=T3 rel C\n"
    "t=T4 acq B @n.c:7\n t=T4 acq D @n.c:8\n t=T4 rel D\n t=T4 rel B\n"
    "t=T5 acq D @n.c:9\n t=T5 acq B @n.c:10\n t=T5 rel B\n t=T5 rel D\n";
static const char LOG_MIXED[] =           /* A↔B 는 G 아래, B↔C 는 게이트 없음 → 같은 성분, 보수적으로 DEADLOCK */
    "t=T1 acq G @m.c:1\n t=T1 acq A @m.c:2\n t=T1 acq B @m.c:3\n t=T1 rel B\n t=T1 rel A\n t=T1 rel G\n"
    "t=T2 acq G @m.c:4\n t=T2 acq B @m.c:5\n t=T2 acq A @m.c:6\n t=T2 rel A\n t=T2 rel B\n t=T2 rel G\n"
    "t=T3 acq B @m.c:7\n t=T3 acq C @m.c:8\n t=T3 rel C\n t=T3 rel B\n"
    "t=T4 acq C @m.c:9\n t=T4 acq B @m.c:10\n t=T4 rel B\n t=T4 rel C\n";

/* Part C: 펌웨어 드라이버 트레이스 (4 태스크). 3-락 사이클 1개 + 게이트로 막힌 사이클 1개 */
static const char LOG_FW[] =
    "# SPI 플래시 + DMA + 전원 관리 + 로거 — 부팅 후 1 초 트레이스 (요약)\n"
    "t=spi_task acq spi_bus    @spi.c:88    # 전송 시작: 버스 소유\n"
    "t=spi_task acq dma_ch2    @spi.c:95    # TX DMA 채널 예약\n"
    "t=spi_task acq log_buf    @log.c:20\n"
    "t=spi_task rel log_buf\n"
    "t=spi_task rel dma_ch2\n"
    "t=spi_task rel spi_bus\n"
    "t=dma_task acq dma_ch2    @dma.c:140   # 완료 처리(bottom half)\n"
    "t=dma_task acq pwr_domain @dma.c:152   # 전원 도메인 참조 카운트 감소\n"
    "t=dma_task rel pwr_domain\n"
    "t=dma_task rel dma_ch2\n"
    "t=pm_task  acq pwr_domain @pm.c:61     # 서스펜드 시작\n"
    "t=pm_task  acq spi_bus    @pm.c:77     # SPI 플래시에 deep power-down 명령\n"
    "t=pm_task  acq log_buf    @log.c:20\n"
    "t=pm_task  rel log_buf\n"
    "t=pm_task  rel spi_bus\n"
    "t=pm_task  rel pwr_domain\n"
    "t=pm_task  acq board_cfg  @board.c:30\n"
    "t=pm_task  acq clk_tree   @clk.c:12    # 저전력 클럭으로\n"
    "t=pm_task  acq pinmux     @pinmux.c:40 # 핀을 아날로그로\n"
    "t=pm_task  rel pinmux\n"
    "t=pm_task  rel clk_tree\n"
    "t=pm_task  rel board_cfg\n"
    "t=log_task acq board_cfg  @board.c:30\n"
    "t=log_task acq pinmux     @pinmux.c:55 # UART TX 핀 재설정\n"
    "t=log_task acq clk_tree   @clk.c:19    # UART 클럭 켜기\n"
    "t=log_task acq log_buf    @log.c:31\n"
    "t=log_task rel log_buf\n"
    "t=log_task rel clk_tree\n"
    "t=log_task rel pinmux\n"
    "t=log_task rel board_cfg\n";

static const sccrep_t *find_rep(const analysis_t *a, verdict_t v) {
    for (int i = 0; i < a->nrep; i++) if (a->rep[i].v == v) return &a->rep[i];
    return NULL;
}
static int comp_is(const lockgraph_t *lg, const analysis_t *a, int c, const char *const *names, int k) {
    int cnt = 0;
    for (int v = 0; v < a->g.n; v++) if (a->comp[v] == c) cnt++;
    if (cnt != k) return 0;
    for (int i = 0; i < k; i++) { int id = lock_id(lg, names[i]); if (id < 0 || a->comp[id] != c) return 0; }
    return 1;
}

int main(void) {
    /* ---------- Part A: SCC 정확성 ---------- */
    for (size_t i = 0; i < sizeof hcases / sizeof hcases[0]; i++) {
        const hcase_t *h = &hcases[i];
        int src[8], dst[8], ord = 1;
        for (int k = 0; k < h->m; k++) { src[k] = h->e[k][0]; dst[k] = h->e[k][1]; }
        graph_t g; graph_build(&g, h->n, h->m, src, dst);
        int r = run_both(&g, h->canon, &ord);
        if (r != 3 || !ord) printf("  case '%s': tarjan=%s kosaraju=%s order=%s\n", h->name,
                                   r & 1 ? "ok" : "BAD", r & 2 ? "ok" : "BAD", ord ? "ok" : "BAD");
        CHECK(r == 3);                                      /* 두 알고리즘 모두 손 계산과 같은 분할 */
        CHECK(ord);                                         /* Tarjan 역 위상, Kosaraju 위상 번호 */
        graph_free(&g);
    }
    {   /* 무작위 그래프 500개: Tarjan == Kosaraju == 전이 폐포 */
        uint32_t seed = 0xA10u;
        int bad1 = 0, bad2 = 0, badord = 0, total_v = 0, total_e = 0;
        for (int it = 0; it < 500; it++) {
            int n = 1 + (int)(xs32(&seed) % 60u), m = (int)(xs32(&seed) % (uint32_t)(3 * n + 1));
            int src[200], dst[200], want[64], ord = 1;
            for (int k = 0; k < m; k++) { src[k] = (int)(xs32(&seed) % (uint32_t)n); dst[k] = (int)(xs32(&seed) % (uint32_t)n); }
            oracle_canon(n, m, src, dst, want);
            graph_t g; graph_build(&g, n, m, src, dst);
            int r = run_both(&g, want, &ord);
            bad1 += !(r & 1); bad2 += !(r & 2); badord += !ord; total_v += n; total_e += m;
            graph_free(&g);
        }
        printf("random: 500 graphs, %d vertices, %d edges: tarjan mismatches=%d kosaraju mismatches=%d order violations=%d\n",
               total_v, total_e, bad1, bad2, badord);
        CHECK(bad1 == 0);
        CHECK(bad2 == 0);
        CHECK(badord == 0);
    }
    {   /* 정점 10^6 사슬(깊이 10^6)과 고리: 재귀판이면 스택이 터지는 크기 */
        const int N = 1000000;
        int *src = malloc((size_t)N * sizeof(int)), *dst = malloc((size_t)N * sizeof(int));
        int *c = malloc((size_t)N * sizeof(int));
        for (int k = 0; k < N; k++) { src[k] = k; dst[k] = (k + 1) % N; }
        graph_t chain, ring;
        graph_build(&chain, N, N - 1, src, dst);            /* 마지막 간선(N-1 → 0) 빼면 사슬 */
        graph_build(&ring, N, N, src, dst);
        CHECK(scc_tarjan(&chain, c) == N);
        CHECK(scc_kosaraju(&chain, c) == N);
        CHECK(scc_tarjan(&ring, c) == 1);
        CHECK(scc_kosaraju(&ring, c) == 1);
        graph_free(&chain); graph_free(&ring);
        free(src); free(dst); free(c);
    }
    /* ---------- Part A: 로그 → 락 순서 그래프 ---------- */
    lockgraph_t lg; analysis_t an;
    {
        lg_init(&lg);
        CHECK(lg_parse(&lg, LOG_ABBA) == 0 && lg.ne == 2 && has_edge(&lg, "A", "B") && has_edge(&lg, "B", "A"));
        const ledge_t *e = lg.ne ? &lg.e[0] : NULL;      /* 첫 간선 A→B 의 증거: T1, a.c:10 에서 A, a.c:11 에서 B */
        CHECK(e && strcmp(lg.threads.name[e->thread], "T1") == 0 && strcmp(lg.sites.name[e->site_from], "a.c:10") == 0
              && strcmp(lg.sites.name[e->site_to], "a.c:11") == 0 && e->line == 2);
        lg_free(&lg);
        lg_init(&lg);                                       /* A, B, C 를 차례로: A→B, A→C, B→C. A→C 게이트 {B}, B→C 게이트 {A} */
        lg_parse(&lg, "t=T1 acq A @f:1\nt=T1 acq B @f:2\nt=T1 acq C @f:3\n");
        int ac = has_edge(&lg, "A", "C") ? lg_find_edge(&lg, lock_id(&lg, "A"), lock_id(&lg, "C")) : -1;
        int bc = has_edge(&lg, "B", "C") ? lg_find_edge(&lg, lock_id(&lg, "B"), lock_id(&lg, "C")) : -1;
        CHECK(lg.ne == 3 && has_edge(&lg, "A", "B"));
        CHECK(ac >= 0 && bc >= 0 && lg.e[ac].ngate == 1 && lg.e[ac].gate[0] == lock_id(&lg, "B")
              && lg.e[bc].ngate == 1 && lg.e[bc].gate[0] == lock_id(&lg, "A"));
        lg_free(&lg);
        lg_init(&lg);                                       /* 순서 밖 해제: A 를 먼저 풀면 C 는 B 아래에서만 */
        lg_parse(&lg, "t=T1 acq A @f:1\nt=T1 acq B @f:2\nt=T1 rel A\nt=T1 acq C @f:3\n");
        CHECK(has_edge(&lg, "B", "C") && !has_edge(&lg, "A", "C") && lg.ne == 2);
        lg_free(&lg);
    }
    /* ---------- Part B: 판정과 증거 ---------- */
    {
        const sccrep_t *r;
        CHECK(run_log(&lg, &an, LOG_ABBA) == 0 && an.nrep == 1 && an.rep[0].v == V_DEADLOCK
              && an.rep[0].cyclen == 2 && witness_ok(&an, &an.rep[0]) && popcount64(an.rep[0].threads) == 2);
        analysis_free(&an); lg_free(&lg);
        CHECK(run_log(&lg, &an, LOG_GATED) == 0 && an.nrep == 1 && an.rep[0].v == V_GATED
              && an.rep[0].gate == lock_id(&lg, "G"));
        analysis_free(&an); lg_free(&lg);
        CHECK(run_log(&lg, &an, LOG_HALF_GATED) == 0 && an.nrep == 1 && an.rep[0].v == V_DEADLOCK);
        analysis_free(&an); lg_free(&lg);
        CHECK(run_log(&lg, &an, LOG_SINGLE) == 0 && an.nrep == 1 && an.rep[0].v == V_SINGLE_THREAD
              && witness_ok(&an, &an.rep[0]));
        analysis_free(&an); lg_free(&lg);
        CHECK(run_log(&lg, &an, LOG_SELF) == 0 && an.nrep == 1 && an.rep[0].v == V_SELF && an.rep[0].cyclen == 1
              && witness_ok(&an, &an.rep[0]));
        analysis_free(&an); lg_free(&lg);
        static const char *const abcd[] = {"A", "B", "C", "D"};
        r = run_log(&lg, &an, LOG_NESTED) == 0 && an.nrep == 1 ? &an.rep[0] : NULL;
        CHECK(r && r->size == 4 && comp_is(&lg, &an, r->comp, abcd, 4) && r->v == V_DEADLOCK);
        CHECK(r && r->cyclen == 3 && witness_ok(&an, r));   /* A 를 지나는 가장 짧은 사이클 A→B→C→A */
        analysis_free(&an); lg_free(&lg);
        r = run_log(&lg, &an, LOG_MIXED) == 0 && an.nrep == 1 ? &an.rep[0] : NULL;
        CHECK(r && r->size == 3 && r->v == V_DEADLOCK);     /* 성분 단위 판정은 보수적: 게이트 없는 B↔C 때문에 */
        analysis_free(&an); lg_free(&lg);
        lg_init(&lg);
        CHECK(lg_parse(&lg, "t=T1 acq A @f:1\nt=T1 grab B @f:2\n") == -1 && strncmp(lg.err, "line 2:", 7) == 0);
        lg_free(&lg);
        lg_init(&lg);
        CHECK(lg_parse(&lg, "t=T1 rel A\nt=T1 acq A @f:1\nt=T1 rel A\nt=T1 rel A\n") == 0 && lg.warnings == 2);
        lg_free(&lg);
    }
    /* ---------- Part C: 펌웨어 트레이스 ---------- */
    {
        int rc = run_log(&lg, &an, LOG_FW);
        CHECK(rc == 0 && lg.locks.n == 7 && lg.threads.n == 4);
        CHECK(rc == 0 && an.count[V_DEADLOCK] == 1 && an.count[V_GATED] == 1
              && an.count[V_SINGLE_THREAD] == 0 && an.count[V_SELF] == 0);
        static const char *const cyc3[] = {"spi_bus", "dma_ch2", "pwr_domain"};
        static const char *const gated[] = {"clk_tree", "pinmux"};
        const sccrep_t *d = rc == 0 ? find_rep(&an, V_DEADLOCK) : NULL, *gq = rc == 0 ? find_rep(&an, V_GATED) : NULL;
        CHECK(d && comp_is(&lg, &an, d->comp, cyc3, 3) && d->cyclen == 3 && witness_ok(&an, d)
              && popcount64(d->threads) == 3);
        CHECK(gq && comp_is(&lg, &an, gq->comp, gated, 2) && gq->gate == lock_id(&lg, "board_cfg"));
        int perm = rc == 0, respect = rc == 0;
        if (rc == 0) {
            int seen[16] = {0};
            for (int c = 0; c < an.ncomp; c++) {
                if (an.rank[c] < 0 || an.rank[c] >= an.ncomp || seen[an.rank[c]]++) perm = 0;
            }
            for (int u = 0; u < an.g.n; u++)
                for (int k = an.g.off[u]; k < an.g.off[u + 1]; k++) {
                    int cu = an.comp[u], cw = an.comp[an.g.adj[k]];
                    if (cu != cw && an.rank[cu] >= an.rank[cw]) respect = 0;
                }
        }
        CHECK(perm && respect);                             /* 락 계층: 모든 성분 사이 간선이 순서를 지킨다 */
        CHECK(rc == 0 && an.rank[an.comp[lock_id(&lg, "log_buf")]] == an.ncomp - 1);  /* 로거 락은 맨 마지막(leaf) */
        if (rc == 0) report_print(stdout, &lg, &an);
        analysis_free(&an); lg_free(&lg);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* ALGO_A10_NO_MAIN */
