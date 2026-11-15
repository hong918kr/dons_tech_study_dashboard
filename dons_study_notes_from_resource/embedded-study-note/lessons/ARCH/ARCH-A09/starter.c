/* ARCH-A09 파워·클럭·리셋 도메인 시퀀싱 설계
 *
 * SoC 쪽(설계 쪽) 전원 시퀀서 모델. 순서는 코드가 아니라 **데이터**(의존성 그래프)다.
 *   노드  RAIL(레일 enable → ramp → power-good) · PLL(enable → lock) · CLK(클럭 게이트 enable, 즉시) ·
 *         RST(리셋 해제, 즉시) · INIT(주변장치 init, 시간이 걸림)
 *   간선  u → v, min_ns = "u 가 완료된 뒤 최소 min_ns 지나야 v 를 시작".  CLK → RST 간선은 SYNC 로 적으면
 *         "그 클럭이 K 사이클 돈 뒤" (리셋 동기화기) 로 자동 계산된다.
 *   도메인 rail/clk/rst 묶음 + isolation 여부 + retention 여부.
 *
 *   Part A  위상 정렬(사이클 = 설정 오류) · 준비 시각 계산 · 이벤트 구동 power-up FSM(병렬/직렬) ·
 *           역순 power-down (isolation clamp 먼저, retention 도메인은 save 후 rail off) · sleep/wake
 *   Part B  체커: 어떤 이벤트 로그든 받아 위반을 간선·타임스탬프와 함께 정확히 한 번씩 보고
 *   Part C  결함 주입: PG 영원히 안 옴 / PLL lock 실패 / 동작 중 unlock / brown-out / PG 글리치 / 늦은 PG
 *           → 안전 상태 + AON 원인 레지스터 + 체커가 시퀀서 출력을 통과시키는지
 *
 * 시간 = int64 ns. 모든 지연·ramp·lock 시간은 대표값(가정)이다. 실제 값은 SoC 데이터시트/PMIC OTP/전원 설계서.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 *        ./so show <up|serial|down|sleep|pg_never|lock_never|unlock|brownout|glitch|late_pg|bo_up|init_never|pllrail>
 *        ./so gantt                      (power-up 노드별 시작/완료 시각 표)
 *        ./so check <log.txt> <soc|mini>  (손으로 쓴 로그 파일 검사)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXN 24
#define MAXE 64
#define MAXD 8
#define MAXLOG 256
#define MAXQ 128
#define MAXV 64
#define US(x) ((int64_t)(x) * 1000)        /* µs → ns */
#define T_INF INT64_MAX
#define SYNC (-1)                          /* CLK→RST 간선: K 사이클 자동 계산 */
#define NEVER (-1)                         /* 결함 주입: 완료 이벤트가 영원히 안 옴 */
#define T_DEGLITCH US(2)                   /* PG 글리치 필터 (대표값) */
#define T_REACT US(5)                      /* 체커: 손실 후 하위 자원을 내려야 하는 시한 (대표값) */

/* ============================== 그래프 (설계 데이터) ============================== */
typedef enum { K_RAIL, K_PLL, K_CLK, K_RST, K_INIT, NKIND } kind_t;
typedef struct {
    char name[12];
    kind_t kind;
    int dom;
    int64_t dur;      /* RAIL: ramp→PG, PLL: lock, INIT: 실행 시간. CLK/RST = 0 (즉시) */
    int64_t timeout;  /* RAIL/PLL/INIT: 완료를 기다리는 최대 시간 */
    int64_t hz;       /* CLK: 주파수 */
} node_t;
typedef struct { int from, to; int64_t min_ns; } edge_t;
typedef struct { char name[8]; int rail, clk, rst; bool iso, ret; } domain_t;
typedef struct {
    node_t n[MAXN]; int nn;
    edge_t e[MAXE]; int ne;
    domain_t d[MAXD]; int nd;      /* 도메인 0 = AON (항상 켜짐, 원인 레지스터가 사는 곳) */
    int k_sync;                    /* 리셋 동기화기가 요구하는 클럭 사이클 수 */
} graph_t;

static int g_find(const graph_t *g, const char *s) {
    for (int i = 0; i < g->nn; i++) if (strcmp(g->n[i].name, s) == 0) return i;
    return -1;
}
static int g_findd(const graph_t *g, const char *s) {
    for (int i = 0; i < g->nd; i++) if (strcmp(g->d[i].name, s) == 0) return i;
    return -1;
}
static int g_dom(graph_t *g, const char *name, bool iso, bool ret) {
    domain_t *d = &g->d[g->nd];
    snprintf(d->name, sizeof d->name, "%s", name);
    d->rail = d->clk = d->rst = -1; d->iso = iso; d->ret = ret;
    return g->nd++;
}
static int g_node(graph_t *g, const char *name, kind_t k, int dom, int64_t dur, int64_t to, int64_t hz) {
    node_t *n = &g->n[g->nn];
    snprintf(n->name, sizeof n->name, "%s", name);
    n->kind = k; n->dom = dom; n->dur = dur; n->timeout = to; n->hz = hz;
    domain_t *d = &g->d[dom];                     /* 도메인의 첫 RAIL/CLK/RST 가 그 도메인의 대표 */
    if (k == K_RAIL && d->rail < 0) d->rail = g->nn;
    if (k == K_CLK && d->clk < 0) d->clk = g->nn;
    if (k == K_RST && d->rst < 0) d->rst = g->nn;
    return g->nn++;
}
static int64_t sync_ns(int64_t hz, int k) { return ((int64_t)k * 1000000000LL + hz - 1) / hz; }  /* 올림 */
static void g_edge(graph_t *g, const char *a, const char *b, int64_t min_ns) {
    int u = g_find(g, a), v = g_find(g, b);
    if (u < 0 || v < 0 || g->ne >= MAXE) { fprintf(stderr, "bad edge %s->%s\n", a, b); exit(2); }
    if (min_ns == SYNC) min_ns = sync_ns(g->n[u].hz, g->k_sync);
    g->e[g->ne++] = (edge_t){u, v, min_ns};
}

/* 가상의 edge-AI SoC "EDGE-1" — 레일 5 · PLL 2 · 클럭 4 · 리셋 4 · init 2. 숫자는 전부 대표값. */
static void build_soc(graph_t *g) {
    memset(g, 0, sizeof *g);
    g->k_sync = 4;
    int AON = g_dom(g, "AON", false, false), ANA = g_dom(g, "ANA", false, false);
    int CORE = g_dom(g, "CORE", true, true), IO = g_dom(g, "IO", true, false), DDR = g_dom(g, "DDR", true, false);
    g_node(g, "VDD_AON", K_RAIL, AON, US(300), US(1000), 0);
    g_node(g, "VDD_PLL", K_RAIL, ANA, US(100), US(1000), 0);
    g_node(g, "VDD_CORE", K_RAIL, CORE, US(250), US(1000), 0);
    g_node(g, "VDD_IO", K_RAIL, IO, US(400), US(1000), 0);
    g_node(g, "VDD_DDR", K_RAIL, DDR, US(200), US(1000), 0);
    g_node(g, "CLK_AON", K_CLK, AON, 0, 0, 32768);            /* 32.768 kHz 저속 클럭 */
    g_node(g, "PLL_SYS", K_PLL, ANA, US(50), US(200), 0);
    g_node(g, "PLL_DDR", K_PLL, ANA, US(80), US(200), 0);
    g_node(g, "CLK_CORE", K_CLK, CORE, 0, 0, 800000000);
    g_node(g, "CLK_IO", K_CLK, IO, 0, 0, 100000000);
    g_node(g, "CLK_DDR", K_CLK, DDR, 0, 0, 400000000);
    g_node(g, "RST_AON", K_RST, AON, 0, 0, 0);
    g_node(g, "RST_CORE", K_RST, CORE, 0, 0, 0);
    g_node(g, "RST_IO", K_RST, IO, 0, 0, 0);
    g_node(g, "RST_DDR", K_RST, DDR, 0, 0, 0);
    g_node(g, "INIT_UART", K_INIT, IO, US(20), US(100), 0);
    g_node(g, "INIT_DDRC", K_INIT, DDR, US(150), US(500), 0);  /* DDR 컨트롤러 init + training */
    g_edge(g, "VDD_AON", "VDD_PLL", 0);
    g_edge(g, "VDD_AON", "VDD_CORE", US(50));
    g_edge(g, "VDD_AON", "VDD_IO", US(100));
    g_edge(g, "VDD_AON", "VDD_DDR", US(150));
    g_edge(g, "VDD_AON", "CLK_AON", US(200));                 /* 저속 발진기 기동 시간 */
    g_edge(g, "VDD_PLL", "PLL_SYS", US(10));
    g_edge(g, "VDD_PLL", "PLL_DDR", US(10));
    g_edge(g, "VDD_DDR", "PLL_DDR", 0);
    g_edge(g, "PLL_SYS", "CLK_CORE", 0);
    g_edge(g, "VDD_CORE", "CLK_CORE", 0);
    g_edge(g, "PLL_SYS", "CLK_IO", 0);
    g_edge(g, "VDD_IO", "CLK_IO", 0);
    g_edge(g, "PLL_DDR", "CLK_DDR", 0);
    g_edge(g, "VDD_DDR", "CLK_DDR", 0);
    g_edge(g, "CLK_AON", "RST_AON", SYNC);
    g_edge(g, "CLK_CORE", "RST_CORE", SYNC);
    g_edge(g, "RST_AON", "RST_CORE", 0);
    g_edge(g, "CLK_IO", "RST_IO", SYNC);
    g_edge(g, "RST_AON", "RST_IO", 0);
    g_edge(g, "CLK_DDR", "RST_DDR", SYNC);
    g_edge(g, "RST_CORE", "RST_DDR", 0);
    g_edge(g, "RST_IO", "INIT_UART", US(1));
    g_edge(g, "RST_DDR", "INIT_DDRC", US(10));
    g_edge(g, "RST_CORE", "INIT_DDRC", 0);
}

/* 체커 시험용 작은 그래프: 도메인 A(iso+ret) + 아날로그 P. reversed = 노드를 역순으로 선언 */
static void build_mini(graph_t *g, bool reversed) {
    memset(g, 0, sizeof *g);
    g->k_sync = 4;
    int A = g_dom(g, "A", true, true), P = g_dom(g, "P", false, false);
    static const struct { const char *nm; kind_t k; int d; int64_t dur, to, hz; } N[6] = {
        {"VDD_A", K_RAIL, 0, US(100), US(500), 0}, {"VDD_P", K_RAIL, 1, US(40), US(500), 0},
        {"PLL0", K_PLL, 1, US(20), US(100), 0},    {"CLK_A", K_CLK, 0, 0, 0, 1000000},
        {"RST_A", K_RST, 0, 0, 0, 0},              {"INIT_A", K_INIT, 0, US(10), US(50), 0}};
    (void)A; (void)P;
    for (int i = 0; i < 6; i++) {
        int j = reversed ? 5 - i : i;
        g_node(g, N[j].nm, N[j].k, N[j].d, N[j].dur, N[j].to, N[j].hz);
    }
    g_edge(g, "VDD_A", "VDD_P", US(10));
    g_edge(g, "VDD_P", "PLL0", US(5));
    g_edge(g, "VDD_A", "PLL0", 0);
    g_edge(g, "PLL0", "CLK_A", 0);
    g_edge(g, "VDD_A", "CLK_A", 0);
    g_edge(g, "CLK_A", "RST_A", SYNC);        /* 4 사이클 @ 1 MHz = 4 µs */
    g_edge(g, "RST_A", "INIT_A", US(2));
}

/* ============================== 이벤트 로그 ============================== */
typedef enum {
    EV_RAIL_EN, EV_RAIL_PG, EV_RAIL_PG_LOST, EV_RAIL_OFF, EV_PLL_EN, EV_PLL_LOCK, EV_PLL_UNLOCK, EV_PLL_OFF,
    EV_CLK_ON, EV_CLK_OFF, EV_RST_DEASSERT, EV_RST_ASSERT, EV_INIT_START, EV_INIT_DONE,
    EV_ISO_ON, EV_ISO_OFF, EV_RET_SAVE, EV_RET_RESTORE, EV_FAULT, NEV
} evk_t;
static const char *const EV_NAME[NEV] = {
    "RAIL_EN", "RAIL_PG", "RAIL_PG_LOST", "RAIL_OFF", "PLL_EN", "PLL_LOCK", "PLL_UNLOCK", "PLL_OFF",
    "CLK_ON", "CLK_OFF", "RST_DEASSERT", "RST_ASSERT", "INIT_START", "INIT_DONE",
    "ISO_ON", "ISO_OFF", "RET_SAVE", "RET_RESTORE", "FAULT"};
static const int EV_KIND[NEV] = {K_RAIL, K_RAIL, K_RAIL, K_RAIL, K_PLL, K_PLL, K_PLL, K_PLL, K_CLK, K_CLK,
                                 K_RST, K_RST, K_INIT, K_INIT, -1, -1, -1, -1, -1};
typedef enum { FR_NONE, FR_PG_TIMEOUT, FR_LOCK_TIMEOUT, FR_INIT_TIMEOUT, FR_BROWNOUT, FR_PLL_UNLOCK, NFR } fr_t;
static const char *const FR_NAME[NFR] = {"NONE", "PG_TIMEOUT", "LOCK_TIMEOUT", "INIT_TIMEOUT", "BROWNOUT", "PLL_UNLOCK"};

typedef struct { int64_t t; evk_t k; int x; int arg; } event_t;   /* x = 노드 (ISO/RET = 도메인), arg = fault 원인 */
static bool ev_is_dom(evk_t k) { return k >= EV_ISO_ON && k <= EV_RET_RESTORE; }

static const char *fmt_t(int64_t t, char *b) {           /* ns → "123.456us" */
    snprintf(b, 24, "%lld.%03lldus", (long long)(t / 1000), (long long)(t % 1000));
    return b;
}
static void fmt_ev(const graph_t *g, const event_t *e, char *out, size_t sz) {
    char tb[24];
    snprintf(out, sz, "%12s  %-12s %s%s%s", fmt_t(e->t, tb), EV_NAME[e->k],
             ev_is_dom(e->k) ? g->d[e->x].name : g->n[e->x].name, e->k == EV_FAULT ? " " : "",
             e->k == EV_FAULT ? FR_NAME[e->arg] : "");
}

/* "123.456" (µs, 소수 3자리까지) → ns. 부동소수점 없이 */
static bool parse_us(const char *s, int64_t *out) {
    int64_t ip = 0, fp = 0; int nf = 0;
    if (!*s) return false;
    for (; *s >= '0' && *s <= '9'; s++) ip = ip * 10 + (*s - '0');
    if (*s == '.') for (s++; *s >= '0' && *s <= '9'; s++) { if (nf == 3) return false; fp = fp * 10 + (*s - '0'); nf++; }
    if (*s) return false;
    while (nf < 3) { fp *= 10; nf++; }
    *out = ip * 1000 + fp;
    return true;
}
/* 줄 형식: "<t_us> <EVENT> <노드|도메인> [FAULT 원인]", '#' 주석. 반환 = 이벤트 수, 오류면 -줄번호 */
static int parse_log(const graph_t *g, const char *text, event_t *out, int max) {
    int n = 0, line = 0;
    for (const char *p = text; *p;) {
        line++;
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char buf[128];
        if (len >= sizeof buf) return -line;
        memcpy(buf, p, len); buf[len] = '\0';
        p += len + (eol ? 1 : 0);
        char *h = strchr(buf, '#'); if (h) *h = '\0';
        char ts[32], en[32], tg[32], rs[32];
        int f = sscanf(buf, "%31s %31s %31s %31s", ts, en, tg, rs);
        if (f <= 0) continue;
        if (f < 3 || n >= max) return -line;
        int64_t t; int k = -1, x, arg = 0;
        if (!parse_us(ts, &t)) return -line;
        for (int i = 0; i < NEV; i++) if (strcmp(en, EV_NAME[i]) == 0) k = i;
        if (k < 0) return -line;
        x = ev_is_dom((evk_t)k) ? g_findd(g, tg) : g_find(g, tg);
        if (x < 0) return -line;
        if (k == EV_FAULT) { arg = -1; for (int i = 0; f == 4 && i < NFR; i++) if (strcmp(rs, FR_NAME[i]) == 0) arg = i; if (arg < 0) return -line; }
        out[n++] = (event_t){t, (evk_t)k, x, arg};
    }
    return n;
}

/* ============================== 시퀀서 (power-up/down FSM) ============================== */
typedef enum { NS_OFF, NS_BUSY, NS_ON } nst_t;                       /* 노드 상태 */
typedef enum { SQ_OFF, SQ_UP, SQ_ON, SQ_PART, SQ_FAULT } sqs_t;       /* 시퀀서 전체 상태 */
static const char *const SQ_NAME[] = {"OFF", "UP", "ON", "PART", "FAULT"};
typedef enum { Q_DONE, Q_TIMEOUT, Q_WAKE, Q_LOST, Q_BACK, Q_DEGLITCH, Q_UNLOCK } qk_t;
typedef struct { int64_t t; long seq; qk_t k; int v; unsigned gen; int64_t arg; } qev_t;

typedef struct {
    int64_t done_delay[MAXN];   /* 0 = 정상(dur), >0 = 이 시간 뒤 완료, NEVER = 영원히 안 옴 */
    struct { int64_t t; evk_t k; int v; int64_t width; } ext[8];   /* 외부 사건: RAIL_PG_LOST(width 뒤 복귀) / PLL_UNLOCK */
    int next;
} inj_t;

enum { BUG_NO_ISO = 1, BUG_NO_SAVE = 2, BUG_IGNORE_UNLOCK = 4 };   /* Part C: 일부러 틀린 시퀀서 */

typedef struct {
    const graph_t *g;
    inj_t inj;
    unsigned bug;
    bool serial;                 /* true = 한 번에 한 노드 (위상 순서대로) */
    int topo[MAXN], nt;
    int64_t now;
    sqs_t state;
    nst_t st[MAXN];
    bool lost[MAXN];             /* PG/lock 이 외부 사건으로 빠진 상태 */
    int64_t done_at[MAXN], wake_at[MAXN];
    unsigned gen[MAXN];          /* 노드를 켜고 끌 때마다 증가 → 옛 타이머/완료 이벤트 무시 */
    bool iso[MAXD], dirty[MAXD], saved[MAXD];
    qev_t q[MAXQ]; int nq; long qseq;
    event_t log[MAXLOG]; int nlog;
    int fault, fault_node;       /* AON 원인 레지스터 (sticky: 첫 원인 유지) */
    int64_t fault_t, t_up;
    int faults_total, ignored, filtered;
} seq_t;

static void q_push(seq_t *sq, int64_t t, qk_t k, int v, unsigned gen, int64_t arg) {
    if (sq->nq >= MAXQ) { fprintf(stderr, "queue full\n"); exit(2); }
    sq->q[sq->nq++] = (qev_t){t, sq->qseq++, k, v, gen, arg};
}
static int q_min(const seq_t *sq) {
    int b = 0;
    for (int i = 1; i < sq->nq; i++)
        if (sq->q[i].t < sq->q[b].t || (sq->q[i].t == sq->q[b].t && sq->q[i].seq < sq->q[b].seq)) b = i;
    return b;
}
static void lg(seq_t *sq, evk_t k, int x, int arg) {
    if ((sq->bug & BUG_NO_ISO) && k == EV_ISO_ON) return;
    if ((sq->bug & BUG_NO_SAVE) && k == EV_RET_SAVE) return;
    if (sq->nlog >= MAXLOG) { fprintf(stderr, "log full\n"); exit(2); }
    sq->log[sq->nlog++] = (event_t){sq->now, k, x, arg};
}
/* root 와 root 에 (간접적으로) 의존하는 모든 노드 */
static void closure(const graph_t *g, int root, bool *mask) {
    memset(mask, 0, sizeof(bool) * MAXN);
    mask[root] = true;
    for (bool ch = true; ch;) {
        ch = false;
        for (int i = 0; i < g->ne; i++)
            if (mask[g->e[i].from] && !mask[g->e[i].to]) { mask[g->e[i].to] = true; ch = true; }
    }
}
/* 노드 하나를 끈다: 종류별 off 이벤트(INIT 은 이벤트 없음 = 리셋으로 무효화) + 상태 정리 */
static void emit_off(seq_t *sq, int v) {
    static const evk_t OFFK[NKIND] = {EV_RAIL_OFF, EV_PLL_OFF, EV_CLK_OFF, EV_RST_ASSERT, NEV};
    const node_t *n = &sq->g->n[v];
    if (OFFK[n->kind] != NEV) lg(sq, OFFK[n->kind], v, 0);
    sq->st[v] = NS_OFF; sq->lost[v] = false; sq->gen[v]++; sq->wake_at[v] = -1;
    if (n->kind == K_RAIL && sq->g->d[n->dom].rail == v) sq->dirty[n->dom] = false;
}

/* ---------------- Part A-1: 위상 정렬 ----------------
 * 간선 u→v 이면 u 가 v 보다 앞. 동률은 **작은 노드 번호 먼저** (결정적 = 로그가 매번 같다).
 * 사이클이 있으면 -1 (설정 오류: 그 스펙은 어떤 순서로도 만족 불가). 반환 = 정렬된 노드 수. */
static int topo_order(const graph_t *g, int *out) {
    /* TODO(Part A-1): Kahn 위상 정렬 (동률 = 작은 번호 먼저), 사이클이면 -1 */
    (void)g; (void)out;
    return 0;
}

/* ---------------- Part A-2: 준비 시각 ----------------
 * 노드 v 를 시작할 수 있는 가장 이른 시각 = max over 간선(u→v) of done_at[u] + min_ns.
 * 선행 노드 중 하나라도 완료(NS_ON)가 아니거나 lost 면 -1 (아직 모름). 선행이 없으면 0. */
static int64_t ready_time(const seq_t *sq, int v) {
    /* TODO(Part A-2): max(done_at[u] + min_ns), 선행 미완료/lost 면 -1, 선행 없으면 0 */
    (void)sq; (void)v;
    return -1;
}

/* ---------------- Part A-3: 역순 power-down ----------------
 * mask 안의 켜진 노드를 위상 순서의 **역순**으로 끈다 (emit_off). 어떤 도메인의 노드를 처음 끄기 직전에
 * 그 도메인을 한 번 quiesce 한다:
 *   (1) isolation 도메인이고 clamp 가 풀려 있으면 ISO_ON
 *   (2) retention 도메인이고, 이번에 그 rail 이 꺼지며(mask), 저장할 상태가 있고(dirty), rail 이 아직 good 이면
 *       → 클럭이 돌고 있으면 먼저 CLK_OFF, 그다음 RET_SAVE (dirty=false, saved=true)
 * 이미 꺼진 노드(quiesce 에서 끈 클럭 포함)는 건너뛴다. */
static void seq_shutdown(seq_t *sq, const bool *mask) {
    /* TODO(Part A-3): 위상 역순으로 emit_off, 도메인마다 처음 한 번 quiesce (ISO_ON, CLK_OFF + RET_SAVE) */
    (void)sq; (void)mask; (void)emit_off;
}

/* ---------------- Part C-1: fault 처리 ----------------
 * (1) faults_total++, FAULT 이벤트 기록 (x = 원인 노드, arg = reason)
 * (2) 원인 레지스터는 sticky: fault == FR_NONE 일 때만 {fault, fault_node, fault_t=now} 기록
 * (3) 안전 상태: PLL_UNLOCK 이면 그 PLL 과 그 하위(closure)만 내린다 (레일은 유지).
 *     그 밖의 fault 는 AON 도메인(0)을 뺀 전부를 내린다 — 단, 원인 노드 자신이 AON 이면 전부.
 * (4) seq_shutdown(mask) 후 state = SQ_FAULT (이후 자동 재시작 없음) */
static void seq_fault(seq_t *sq, int reason, int v) {
    /* TODO(Part C-1): FAULT 기록, sticky 원인, 안전 상태 mask 계산, seq_shutdown, state = SQ_FAULT */
    (void)sq; (void)reason; (void)v;
}

/* ---------------- 시퀀서 본체 (하네스 제공) ---------------- */
static int seq_init(seq_t *sq, const graph_t *g, const inj_t *inj) {
    memset(sq, 0, sizeof *sq);
    sq->g = g;
    if (inj) sq->inj = *inj;
    sq->fault_node = -1;
    for (int v = 0; v < MAXN; v++) { sq->wake_at[v] = -1; sq->done_at[v] = -1; }
    for (int d = 0; d < g->nd; d++) sq->iso[d] = g->d[d].iso;       /* 전원 인가 직후 clamp 상태 */
    int n = topo_order(g, sq->topo);
    if (n != g->nn) { sq->nt = 0; return -1; }                       /* 사이클 = 설정 오류 */
    sq->nt = n;
    for (int i = 0; i < sq->inj.next; i++)
        q_push(sq, sq->inj.ext[i].t, sq->inj.ext[i].k == EV_PLL_UNLOCK ? Q_UNLOCK : Q_LOST, sq->inj.ext[i].v, 0,
               sq->inj.ext[i].width);
    return 0;
}
static void issue(seq_t *sq, int v) {
    const node_t *n = &sq->g->n[v];
    int d = n->dom;
    const domain_t *D = &sq->g->d[d];
    sq->wake_at[v] = -1;
    if (n->kind == K_RAIL || n->kind == K_PLL || n->kind == K_INIT) {
        lg(sq, n->kind == K_RAIL ? EV_RAIL_EN : n->kind == K_PLL ? EV_PLL_EN : EV_INIT_START, v, 0);
        sq->st[v] = NS_BUSY; sq->gen[v]++;
        int64_t dly = sq->inj.done_delay[v] ? sq->inj.done_delay[v] : n->dur;
        if (dly >= 0) q_push(sq, sq->now + dly, Q_DONE, v, sq->gen[v], 0);
        if (n->timeout > 0) q_push(sq, sq->now + n->timeout, Q_TIMEOUT, v, sq->gen[v], 0);
    } else if (n->kind == K_CLK) {
        if (D->ret && D->clk == v && sq->saved[d]) { lg(sq, EV_RET_RESTORE, d, 0); sq->saved[d] = false; }
        lg(sq, EV_CLK_ON, v, 0);
        sq->st[v] = NS_ON; sq->done_at[v] = sq->now;
        if (D->clk == v) sq->dirty[d] = true;
    } else {                                                          /* K_RST */
        lg(sq, EV_RST_DEASSERT, v, 0);
        sq->st[v] = NS_ON; sq->done_at[v] = sq->now;
        if (D->iso && D->rst == v && sq->iso[d]) { lg(sq, EV_ISO_OFF, d, 0); sq->iso[d] = false; }
    }
}
static void issue_ready(seq_t *sq) {
    if (sq->state != SQ_UP) return;
    const graph_t *g = sq->g;
    for (bool again = true; again;) {
        again = false;
        for (int i = 0; i < sq->nt; i++) {
            int v = sq->topo[i];
            if (sq->st[v] != NS_OFF) continue;
            if (sq->serial) for (int u = 0; u < g->nn; u++) if (sq->st[u] == NS_BUSY) return;
            int64_t rt = ready_time(sq, v);
            if (rt >= 0 && rt <= sq->now) { issue(sq, v); again = true; if (sq->serial) break; continue; }
            if (rt > sq->now && sq->wake_at[v] != rt) { q_push(sq, rt, Q_WAKE, v, 0, 0); sq->wake_at[v] = rt; }
            if (sq->serial) return;
        }
    }
    for (int v = 0; v < g->nn; v++) if (sq->st[v] != NS_ON || sq->lost[v]) return;
    sq->state = SQ_ON; sq->t_up = sq->now;
}
static void handle(seq_t *sq, const qev_t *q) {
    int v = q->v;
    kind_t k = sq->g->n[v].kind;
    switch (q->k) {
    case Q_DONE:
        if (q->gen != sq->gen[v] || sq->st[v] != NS_BUSY) { sq->ignored++; break; }   /* 늦은 PG 등 */
        sq->st[v] = NS_ON; sq->done_at[v] = sq->now;
        lg(sq, k == K_RAIL ? EV_RAIL_PG : k == K_PLL ? EV_PLL_LOCK : EV_INIT_DONE, v, 0);
        break;
    case Q_TIMEOUT:
        if (q->gen != sq->gen[v] || sq->st[v] != NS_BUSY) break;
        seq_fault(sq, k == K_RAIL ? FR_PG_TIMEOUT : k == K_PLL ? FR_LOCK_TIMEOUT : FR_INIT_TIMEOUT, v);
        break;
    case Q_WAKE: break;
    case Q_LOST:
        if (k != K_RAIL || sq->st[v] != NS_ON || sq->lost[v]) { sq->ignored++; break; }
        sq->lost[v] = true;
        lg(sq, EV_RAIL_PG_LOST, v, 0);
        q_push(sq, sq->now + T_DEGLITCH, Q_DEGLITCH, v, sq->gen[v], 0);
        if (q->arg >= 0) q_push(sq, sq->now + q->arg, Q_BACK, v, sq->gen[v], 0);
        break;
    case Q_BACK:
        if (q->gen != sq->gen[v] || !sq->lost[v]) { sq->ignored++; break; }
        sq->lost[v] = false; sq->done_at[v] = sq->now;         /* 복귀 시각부터 min 지연을 다시 잰다 */
        lg(sq, EV_RAIL_PG, v, 0);
        break;
    case Q_DEGLITCH:
        if (q->gen != sq->gen[v]) break;
        if (sq->lost[v]) seq_fault(sq, FR_BROWNOUT, v); else sq->filtered++;
        break;
    case Q_UNLOCK:
        if (k != K_PLL || sq->st[v] != NS_ON) { sq->ignored++; break; }
        sq->lost[v] = true;
        lg(sq, EV_PLL_UNLOCK, v, 0);
        if (!(sq->bug & BUG_IGNORE_UNLOCK)) seq_fault(sq, FR_PLL_UNLOCK, v);
        break;
    }
}
static void seq_run(seq_t *sq, int64_t until) {
    while (sq->nq) {
        int b = q_min(sq);
        if (sq->q[b].t > until) break;
        qev_t q = sq->q[b];
        sq->q[b] = sq->q[--sq->nq];
        if (q.t > sq->now) sq->now = q.t;
        handle(sq, &q);
        issue_ready(sq);
    }
    if (until != T_INF && until > sq->now) sq->now = until;
}
static int seq_up(seq_t *sq) {
    if (sq->nt != sq->g->nn || sq->state == SQ_FAULT) return -1;     /* 설정 오류 / fault 후 자동 재시작 금지 */
    sq->state = SQ_UP;
    issue_ready(sq);
    seq_run(sq, T_INF);
    return sq->state == SQ_ON ? 0 : -1;
}
static void seq_down(seq_t *sq, int root) {                           /* root < 0 = 전체 */
    bool mask[MAXN];
    if (root < 0) for (int v = 0; v < MAXN; v++) mask[v] = true;
    else closure(sq->g, root, mask);
    seq_shutdown(sq, mask);
    sq->state = root < 0 ? SQ_OFF : SQ_PART;
}

/* ============================== 체커 ============================== */
typedef enum {
    V_ORDER, V_MIN_DELAY, V_CLK_BEFORE_RAIL, V_PLL_NOT_LOCKED, V_RST_BEFORE_CLK,
    V_OFF_ORDER, V_OFF_BEFORE_ISO, V_OFF_BEFORE_SAVE, V_ISO_EARLY, V_UNHANDLED_LOSS, V_BAD_EVENT, NVT
} vt_t;
static const char *const VT_NAME[NVT] = {
    "ORDER", "MIN_DELAY", "CLK_BEFORE_RAIL", "PLL_NOT_LOCKED", "RST_BEFORE_CLK",
    "OFF_ORDER", "OFF_BEFORE_ISO", "OFF_BEFORE_SAVE", "ISO_EARLY", "UNHANDLED_LOSS", "BAD_EVENT"};
typedef struct { vt_t ty; int idx; int64_t t; int node, dom, dep; int64_t t_dep, need; } viol_t;
typedef struct {
    const graph_t *g;
    int64_t react;
    bool act[MAXN], good[MAXN];   /* act = 켜라는 명령이 나간 상태, good = PG/lock/동작/해제/완료 */
    int64_t t_good[MAXN];
    bool iso[MAXD], dirty[MAXD];
    struct { int v, idx; int64_t t; } loss[MAXN]; int nloss;
    viol_t vio[MAXV]; int nv;
} chk_t;

static void add_v(chk_t *c, vt_t ty, int idx, int64_t t, int node, int dom, int dep, int64_t t_dep, int64_t need) {
    if (c->nv < MAXV) c->vio[c->nv++] = (viol_t){ty, idx, t, node, dom, dep, t_dep, need};
}
static bool is_res(kind_t k) { return k == K_RAIL || k == K_PLL || k == K_CLK; }

/* ---------------- Part B-1: 켜는 동작의 의존성 검사 ----------------
 * 이벤트 i (시각 t) 가 노드 v 를 켠다 (RAIL_EN / PLL_EN / CLK_ON / RST_DEASSERT / INIT_START).
 * v 로 들어오는 간선 u→v 마다, 위반이면 정확히 하나를 add_v:
 *   u 가 good 이 아님 →  RAIL→(PLL|CLK): V_CLK_BEFORE_RAIL · PLL→CLK: V_PLL_NOT_LOCKED ·
 *                        CLK→RST: V_RST_BEFORE_CLK · 그 밖: V_ORDER          (t_dep = -1)
 *   good 이지만 t - t_good[u] < min_ns →  CLK→RST: V_RST_BEFORE_CLK · 그 밖: V_MIN_DELAY (t_dep = t_good[u])
 * add_v(c, ty, i, t, v, -1, u, t_dep, e->min_ns) */
static void chk_action(chk_t *c, int v, int i, int64_t t) {
    /* TODO(Part B-1): v 로 들어오는 간선마다 good / min_ns 검사 → 종류별 위반 */
    (void)c; (void)v; (void)i; (void)t;
}

/* ---------------- Part B-2: 끄는 동작의 검사 ----------------
 * 이벤트 i 가 자원 u 를 끈다 (RAIL_OFF / PLL_OFF / CLK_OFF). 위반마다 add_v(c, ty, i, t, u, dom, dep, -1, 0):
 *   - 간선 u→w 의 w 가 자원(RAIL/PLL/CLK)이고 아직 act → V_OFF_ORDER (dep = w, dom = -1)
 *   - u 가 도메인 d 의 대표 rail 이면:
 *       d 의 리셋 노드가 아직 act(해제 상태)        → V_OFF_ORDER      (dep = rst, dom = -1)
 *       d.iso 인데 clamp 가 안 걸림 (!c->iso[d])     → V_OFF_BEFORE_ISO (dep = -1, dom = d)
 *       d.ret 이고 dirty 이고 rail 이 아직 good       → V_OFF_BEFORE_SAVE (dep = -1, dom = d)
 *         (good 이 아니면 = 이미 brown-out — 저장할 수 없으니 면제) */
static void chk_off(chk_t *c, int u, int i, int64_t t) {
    /* TODO(Part B-2): 하위 자원이 켜져 있으면 OFF_ORDER, 도메인 rail 이면 리셋/iso/save 검사 */
    (void)c; (void)u; (void)i; (void)t; (void)is_res;
}

/* ---------------- Part B-3: 손실 대응 시한 ----------------
 * 외부 손실(RAIL_PG_LOST / PLL_UNLOCK)은 loss[] 에 {v, idx, t} 로 쌓인다 (복귀하면 하네스가 지운다).
 * 시각 t 의 이벤트를 처리하기 **전에** 호출된다 (끝에서는 t = T_INF): 시한 L.t + react < t 인 항목마다
 *   v 의 직접 하위 자원(간선 v→w, w 는 RAIL/PLL/CLK) 중 아직 act 인 것, 없으면 (v 가 도메인 rail 일 때)
 *   그 도메인 리셋이 아직 act 인지 → 하나라도 있으면 V_UNHANDLED_LOSS 를 **한 번** add_v(c, ty, L.idx,
 *   L.t + react, v, -1, 처음 찾은 w, L.t, react). 평가한 항목은 순서를 유지하며 목록에서 지운다. */
static void chk_loss_due(chk_t *c, int64_t t) {
    /* TODO(Part B-3): 시한이 지난 손실마다 하위 자원/리셋이 아직 켜져 있으면 UNHANDLED_LOSS 한 번, 목록에서 제거 */
    (void)c; (void)t;
}

/* 체커 본체 (하네스 제공): 상태 추적 + 상태와 맞지 않는 이벤트(BAD_EVENT) + ISO_EARLY */
static void loss_clear(chk_t *c, int v) {
    for (int j = 0; j < c->nloss;)
        if (c->loss[j].v == v) { for (int m = j; m + 1 < c->nloss; m++) c->loss[m] = c->loss[m + 1]; c->nloss--; }
        else j++;
}
static int check_log(const graph_t *g, const event_t *L, int n, int64_t react, chk_t *c) {
    memset(c, 0, sizeof *c);
    c->g = g; c->react = react;
    for (int d = 0; d < g->nd; d++) c->iso[d] = g->d[d].iso;
    for (int i = 0; i < n; i++) {
        const event_t *e = &L[i];
        int x = e->x;
        int64_t t = e->t;
        chk_loss_due(c, t);
        if (i > 0 && t < L[i - 1].t) add_v(c, V_BAD_EVENT, i, t, -1, -1, -1, L[i - 1].t, 0);
        bool bad = !ev_is_dom(e->k) && e->k != EV_FAULT && (int)g->n[x].kind != EV_KIND[e->k];
        if (bad) { add_v(c, V_BAD_EVENT, i, t, x, -1, -1, -1, 0); continue; }
        const domain_t *D = ev_is_dom(e->k) ? &g->d[x] : &g->d[g->n[x].dom];
        switch (e->k) {
        case EV_RAIL_EN: case EV_PLL_EN: case EV_CLK_ON: case EV_RST_DEASSERT: case EV_INIT_START:
            if (c->act[x]) { bad = true; break; }
            chk_action(c, x, i, t);
            c->act[x] = true;
            if (e->k == EV_CLK_ON || e->k == EV_RST_DEASSERT) { c->good[x] = true; c->t_good[x] = t; }
            if (e->k == EV_CLK_ON && D->clk == x) c->dirty[g->n[x].dom] = true;
            break;
        case EV_RAIL_PG: case EV_PLL_LOCK: case EV_INIT_DONE:
            if (!c->act[x] || c->good[x]) { bad = true; break; }
            c->good[x] = true; c->t_good[x] = t;
            loss_clear(c, x);
            break;
        case EV_RAIL_PG_LOST: case EV_PLL_UNLOCK:
            if (!c->good[x]) { bad = true; break; }
            c->good[x] = false;
            loss_clear(c, x);
            if (c->nloss < MAXN) { c->loss[c->nloss].v = x; c->loss[c->nloss].idx = i; c->loss[c->nloss].t = t; c->nloss++; }
            break;
        case EV_RAIL_OFF: case EV_PLL_OFF: case EV_CLK_OFF:
            if (!c->act[x]) { bad = true; break; }
            chk_off(c, x, i, t);
            c->act[x] = c->good[x] = false;
            if (e->k == EV_RAIL_OFF && D->rail == x) c->dirty[g->n[x].dom] = false;
            break;
        case EV_RST_ASSERT:
            if (!c->act[x]) { bad = true; break; }
            c->act[x] = c->good[x] = false;
            for (int k = 0; k < g->ne; k++)                       /* 리셋은 그 위의 init 을 무효화 */
                if (g->e[k].from == x && g->n[g->e[k].to].kind == K_INIT) c->act[g->e[k].to] = c->good[g->e[k].to] = false;
            break;
        case EV_ISO_ON:
            if (!D->iso || c->iso[x]) { bad = true; break; }
            c->iso[x] = true;
            break;
        case EV_ISO_OFF:
            if (!D->iso || !c->iso[x]) { bad = true; break; }
            if (D->rst < 0 || !c->good[D->rst]) add_v(c, V_ISO_EARLY, i, t, -1, x, D->rst, -1, 0);
            else if (!c->good[D->rail]) add_v(c, V_ISO_EARLY, i, t, -1, x, D->rail, -1, 0);
            c->iso[x] = false;
            break;
        case EV_RET_SAVE:
            if (!D->ret) { bad = true; break; }
            c->dirty[x] = false;
            break;
        case EV_RET_RESTORE:
            if (!D->ret) bad = true;
            break;
        default: break;                                           /* FAULT = 정보용 */
        }
        if (bad) add_v(c, V_BAD_EVENT, i, t, ev_is_dom(e->k) ? -1 : x, ev_is_dom(e->k) ? x : -1, -1, -1, 0);
    }
    chk_loss_due(c, T_INF);
    return c->nv;
}

static void fmt_v(const graph_t *g, const viol_t *v, char *out, size_t sz) {
    char a[24], b[24], cc[24];
    const char *nn = v->node >= 0 ? g->n[v->node].name : v->dom >= 0 ? g->d[v->dom].name : "-";
    const char *dn = v->dep >= 0 ? g->n[v->dep].name : "-";
    int p = snprintf(out, sz, "#%-3d t=%-12s %-16s ", v->idx, fmt_t(v->t, a), VT_NAME[v->ty]);
    if (p < 0 || (size_t)p >= sz) return;
    out += p; sz -= (size_t)p;
    switch (v->ty) {
    case V_ORDER: case V_MIN_DELAY: case V_CLK_BEFORE_RAIL: case V_PLL_NOT_LOCKED: case V_RST_BEFORE_CLK:
        if (v->t_dep < 0) snprintf(out, sz, "edge %s->%s: %s not done", dn, nn, dn);
        else snprintf(out, sz, "edge %s->%s: need %s after %s@%s, got %s", dn, nn, fmt_t(v->need, a), dn,
                      fmt_t(v->t_dep, b), fmt_t(v->t - v->t_dep, cc));
        break;
    case V_OFF_ORDER: snprintf(out, sz, "%s off while %s still on", nn, dn); break;
    case V_OFF_BEFORE_ISO: snprintf(out, sz, "%s off, domain %s not isolated", nn, g->d[v->dom].name); break;
    case V_OFF_BEFORE_SAVE: snprintf(out, sz, "%s off, domain %s state not saved", nn, g->d[v->dom].name); break;
    case V_ISO_EARLY: snprintf(out, sz, "ISO_OFF %s while %s not up", nn, dn); break;
    case V_UNHANDLED_LOSS: snprintf(out, sz, "%s lost @%s, %s still on at deadline", nn, fmt_t(v->t_dep, a), dn); break;
    default: snprintf(out, sz, "event inconsistent with state (or time went backwards)"); break;
    }
}
static void print_viols(const graph_t *g, const chk_t *c) {
    char buf[200];
    for (int i = 0; i < c->nv; i++) { fmt_v(g, &c->vio[i], buf, sizeof buf); printf("    %s\n", buf); }
}
static void print_log(const graph_t *g, const event_t *L, int from, int to) {
    char buf[96];
    for (int i = from; i < to; i++) { fmt_ev(g, &L[i], buf, sizeof buf); printf("  %3d %s\n", i, buf); }
}
/* 로그 [from, to) 를 "t EVENT target" 줄로 이어 붙임 (기대 문자열 비교용) */
static const char *log_text(const graph_t *g, const event_t *L, int from, int to) {
    static char out[8192];
    size_t p = 0;
    out[0] = '\0';
    for (int i = from; i < to && p < sizeof out - 100; i++) {
        char tb[24];
        p += (size_t)snprintf(out + p, sizeof out - p, "%s %s %s\n", fmt_t(L[i].t, tb), EV_NAME[L[i].k],
                              ev_is_dom(L[i].k) ? g->d[L[i].x].name : g->n[L[i].x].name);
    }
    return out;
}

/* ============================== 테스트 하네스 ============================== */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static graph_t G_SOC, G_MINI;
static seq_t SQ;
static chk_t CK;

static int check_seq(const seq_t *sq) { return check_log(sq->g, sq->log, sq->nlog, T_REACT, &CK); }
static bool order_valid(const graph_t *g, const int *ord, int n) {
    int pos[MAXN];
    if (n != g->nn) return false;
    for (int i = 0; i < n; i++) pos[ord[i]] = i;
    for (int i = 0; i < g->ne; i++) if (pos[g->e[i].from] >= pos[g->e[i].to]) return false;
    return true;
}
static int count_ev(const seq_t *sq, evk_t k) { int n = 0; for (int i = 0; i < sq->nlog; i++) n += sq->log[i].k == k; return n; }
static bool safe_off(const seq_t *sq) {             /* AON 레일만 켜져 있고 AON 밖은 전부 꺼짐 */
    for (int v = 0; v < sq->g->nn; v++)
        if (sq->g->n[v].dom != 0 && sq->st[v] != NS_OFF) return false;
    return sq->st[sq->g->d[0].rail] == NS_ON;
}

static void part_a(void) {
    printf("== Part A: 의존성 그래프 → 위상 정렬 → power-up/down FSM ==\n");
    int ord[MAXN];
    CHECK(topo_order(&G_SOC, ord) == 17 && order_valid(&G_SOC, ord, 17));
    bool ident = true;
    for (int i = 0; i < 17; i++) ident = ident && ord[i] == i;
    CHECK(ident);                                            /* 노드를 위상 순서로 선언했고 동률은 작은 번호 먼저 */
    graph_t gr; build_mini(&gr, true);                       /* 역순 선언해도 */
    int n = topo_order(&gr, ord);
    CHECK(n == 6 && order_valid(&gr, ord, n) && strcmp(gr.n[ord[0]].name, "VDD_A") == 0);
    graph_t gc; build_mini(&gc, false);
    g_edge(&gc, "INIT_A", "VDD_A", 0);                        /* 사이클: init 이 끝나야 rail 을 켠다? */
    CHECK(topo_order(&gc, ord) == -1);
    CHECK(seq_init(&SQ, &gc, NULL) == -1 && seq_up(&SQ) == -1 && SQ.nlog == 0 && SQ.state == SQ_OFF);

    /* ready_time 단위 시험 */
    seq_init(&SQ, &G_SOC, NULL);
    int CLK_CORE = g_find(&G_SOC, "CLK_CORE"), RST_CORE = g_find(&G_SOC, "RST_CORE"), PLL_SYS = g_find(&G_SOC, "PLL_SYS");
    int VDD_CORE = g_find(&G_SOC, "VDD_CORE"), RST_AON = g_find(&G_SOC, "RST_AON");
    CHECK(ready_time(&SQ, 0) == 0);                            /* 루트 */
    CHECK(ready_time(&SQ, CLK_CORE) == -1);                    /* 선행 미완료 */
    SQ.st[PLL_SYS] = NS_ON; SQ.done_at[PLL_SYS] = US(460);
    SQ.st[VDD_CORE] = NS_ON; SQ.done_at[VDD_CORE] = US(600);
    CHECK(ready_time(&SQ, CLK_CORE) == US(600));
    SQ.lost[VDD_CORE] = true;
    CHECK(ready_time(&SQ, CLK_CORE) == -1);                    /* PG 가 빠진 레일은 완료가 아니다 */
    SQ.lost[VDD_CORE] = false;
    SQ.st[CLK_CORE] = NS_ON; SQ.done_at[CLK_CORE] = US(600);
    SQ.st[RST_AON] = NS_ON; SQ.done_at[RST_AON] = 622071;
    CHECK(ready_time(&SQ, RST_CORE) == 622071);                /* 5 ns(800 MHz×4) 가 아니라 AON 리셋이 지배 */

    /* 병렬 power-up: 끝나는 시각 = 그래프의 임계 경로 (손 계산 890.010 µs) */
    seq_init(&SQ, &G_SOC, NULL);
    CHECK(seq_up(&SQ) == 0 && SQ.state == SQ_ON);
    CHECK(SQ.t_up == 890010);
    CHECK(SQ.done_at[RST_AON] == US(500) + 122071 && SQ.done_at[RST_CORE] == 622071);
    CHECK(SQ.done_at[g_find(&G_SOC, "RST_IO")] == US(800) + 40 && SQ.done_at[g_find(&G_SOC, "PLL_DDR")] == US(730));
    CHECK(SQ.nlog == 29 && count_ev(&SQ, EV_ISO_OFF) == 3 && check_seq(&SQ) == 0);
    int64_t t_par = SQ.t_up;
    int nup = SQ.nlog;

    /* 직렬(한 번에 하나) power-up: 같은 순서, 더 느림 (손 계산 1551.040 µs) */
    seq_t *S2 = malloc(sizeof *S2);
    if (!S2) exit(2);
    seq_init(S2, &G_SOC, NULL); S2->serial = true;
    CHECK(seq_up(S2) == 0 && S2->t_up == 1551040 && check_seq(S2) == 0);
    char tb1[24], tb2[24];
    printf("  power-up: parallel %s (= critical path), serial %s, events %d\n", fmt_t(t_par, tb1), fmt_t(S2->t_up, tb2), nup);

    /* 전체 power-down at 2000 µs: 역순, ISO_ON 먼저, CORE 는 CLK_OFF → RET_SAVE */
    seq_run(&SQ, US(2000));
    seq_down(&SQ, -1);
    const char *DOWN =
        "2000.000us ISO_ON DDR\n2000.000us ISO_ON IO\n2000.000us RST_ASSERT RST_DDR\n2000.000us RST_ASSERT RST_IO\n"
        "2000.000us ISO_ON CORE\n2000.000us CLK_OFF CLK_CORE\n2000.000us RET_SAVE CORE\n2000.000us RST_ASSERT RST_CORE\n"
        "2000.000us RST_ASSERT RST_AON\n2000.000us CLK_OFF CLK_DDR\n2000.000us CLK_OFF CLK_IO\n2000.000us PLL_OFF PLL_DDR\n"
        "2000.000us PLL_OFF PLL_SYS\n2000.000us CLK_OFF CLK_AON\n2000.000us RAIL_OFF VDD_DDR\n2000.000us RAIL_OFF VDD_IO\n"
        "2000.000us RAIL_OFF VDD_CORE\n2000.000us RAIL_OFF VDD_PLL\n2000.000us RAIL_OFF VDD_AON\n";
    const char *got = log_text(&G_SOC, SQ.log, nup, SQ.nlog);
    CHECK(strcmp(got, DOWN) == 0);
    if (strcmp(got, DOWN) != 0) printf("  got down log:\n%s", got);
    bool alloff = true;
    for (int v = 0; v < G_SOC.nn; v++) alloff = alloff && SQ.st[v] == NS_OFF;
    CHECK(alloff && SQ.state == SQ_OFF && check_seq(&SQ) == 0);

    /* sleep(CORE) + wake: VDD_CORE 와 그 하위만 내리고, 다시 올리면 RESTORE → CLK_ON → RST → ISO_OFF */
    seq_init(&SQ, &G_SOC, NULL);
    seq_up(&SQ);
    seq_run(&SQ, US(2000));
    int n0 = SQ.nlog;
    seq_down(&SQ, VDD_CORE);
    const char *SLEEP =
        "2000.000us ISO_ON DDR\n2000.000us RST_ASSERT RST_DDR\n2000.000us ISO_ON CORE\n2000.000us CLK_OFF CLK_CORE\n"
        "2000.000us RET_SAVE CORE\n2000.000us RST_ASSERT RST_CORE\n2000.000us RAIL_OFF VDD_CORE\n";
    got = log_text(&G_SOC, SQ.log, n0, SQ.nlog);
    CHECK(strcmp(got, SLEEP) == 0 && SQ.state == SQ_PART);
    if (strcmp(got, SLEEP) != 0) printf("  got sleep log:\n%s", got);
    CHECK(SQ.st[g_find(&G_SOC, "CLK_DDR")] == NS_ON && SQ.st[g_find(&G_SOC, "INIT_UART")] == NS_ON);  /* 범위 밖은 그대로 */
    seq_run(&SQ, US(3000));
    n0 = SQ.nlog;
    CHECK(seq_up(&SQ) == 0 && SQ.t_up == US(3410) + 5);
    const char *WAKE =
        "3000.000us RAIL_EN VDD_CORE\n3250.000us RAIL_PG VDD_CORE\n3250.000us RET_RESTORE CORE\n3250.000us CLK_ON CLK_CORE\n"
        "3250.005us RST_DEASSERT RST_CORE\n3250.005us ISO_OFF CORE\n3250.005us RST_DEASSERT RST_DDR\n3250.005us ISO_OFF DDR\n"
        "3260.005us INIT_START INIT_DDRC\n3410.005us INIT_DONE INIT_DDRC\n";
    got = log_text(&G_SOC, SQ.log, n0, SQ.nlog);
    CHECK(strcmp(got, WAKE) == 0);
    if (strcmp(got, WAKE) != 0) printf("  got wake log:\n%s", got);
    CHECK(check_seq(&SQ) == 0);
    printf("  sleep(CORE)@2000us -> wake@3000us: wake latency %s, log %d events, checker violations %d\n",
           fmt_t(SQ.t_up - US(3000), tb1), SQ.nlog, CK.nv);
    free(S2);
}

/* ---- Part B: 손으로 쓴 로그 ---- */
static const char *const MINI_GOOD[] = {
    "0   RAIL_EN VDD_A",      "100 RAIL_PG VDD_A",       "110 RAIL_EN VDD_P",     "150 RAIL_PG VDD_P",
    "155 PLL_EN PLL0",        "175 PLL_LOCK PLL0",       "175 CLK_ON CLK_A",      "179 RST_DEASSERT RST_A",
    "179 ISO_OFF A",          "181 INIT_START INIT_A",   "191 INIT_DONE INIT_A",  "300 ISO_ON A",
    "300 CLK_OFF CLK_A",      "300 RET_SAVE A",          "300 RST_ASSERT RST_A",  "300 PLL_OFF PLL0",
    "300 RAIL_OFF VDD_P",     "300 RAIL_OFF VDD_A"};
#define NMG 18
typedef struct { int line; const char *text; } patch_t;     /* text NULL = 줄 삭제, "\n" 으로 여러 줄 */
typedef struct { const char *name; patch_t p[6]; int ty, cnt; const char *node, *dep; } badlog_t;
static const badlog_t BAD[] = {
    {"ORDER",           {{7, "178 INIT_START INIT_A\n179 RST_DEASSERT RST_A"}, {9, NULL}, {10, "188 INIT_DONE INIT_A"}}, V_ORDER, 1, "INIT_A", "RST_A"},
    {"MIN_DELAY",       {{2, "105 RAIL_EN VDD_P"}},                                         V_MIN_DELAY, 1, "VDD_P", "VDD_A"},
    {"CLK_BEFORE_RAIL", {{3, "145 PLL_EN PLL0"}, {4, "150 RAIL_PG VDD_P"}},                 V_CLK_BEFORE_RAIL, 1, "PLL0", "VDD_P"},
    {"PLL_NOT_LOCKED",  {{5, "170 CLK_ON CLK_A"}, {6, "175 PLL_LOCK PLL0"}},                V_PLL_NOT_LOCKED, 1, "CLK_A", "PLL0"},
    {"RST_BEFORE_CLK",  {{7, "177 RST_DEASSERT RST_A"}, {8, "177 ISO_OFF A"}},              V_RST_BEFORE_CLK, 1, "RST_A", "CLK_A"},
    {"OFF_ORDER",       {{16, "300 RAIL_OFF VDD_A"}, {17, "300 RAIL_OFF VDD_P"}},            V_OFF_ORDER, 1, "VDD_A", "VDD_P"},
    {"OFF_BEFORE_ISO",  {{11, NULL}, {17, "300 RAIL_OFF VDD_A\n301 ISO_ON A"}},             V_OFF_BEFORE_ISO, 1, "VDD_A", NULL},
    {"OFF_BEFORE_SAVE", {{13, NULL}},                                                       V_OFF_BEFORE_SAVE, 1, "VDD_A", NULL},
    {"ISO_EARLY",       {{7, "176 ISO_OFF A"}, {8, "179 RST_DEASSERT RST_A"}},              V_ISO_EARLY, 1, NULL, "RST_A"},
    {"UNHANDLED_LOSS",  {{10, "191 INIT_DONE INIT_A\n250 PLL_UNLOCK PLL0"}},                V_UNHANDLED_LOSS, 1, "PLL0", "CLK_A"},
    {"LONG_PG_DROP",    {{10, "191 INIT_DONE INIT_A\n250 RAIL_PG_LOST VDD_A\n280 RAIL_PG VDD_A"}}, V_UNHANDLED_LOSS, 1, "VDD_A", "VDD_P"},
    {"BAD_DOUBLE_PG",   {{1, "100 RAIL_PG VDD_A\n100 RAIL_PG VDD_A"}},                      V_BAD_EVENT, 1, "VDD_A", NULL},
    {"BAD_TIME",        {{10, "180 INIT_DONE INIT_A"}},                                     V_BAD_EVENT, 1, NULL, NULL},
    /* 아래는 위반이 아니어야 한다 (cnt 0) */
    {"handled unlock",  {{10, "191 INIT_DONE INIT_A\n250 PLL_UNLOCK PLL0\n251 ISO_ON A\n251 CLK_OFF CLK_A\n251 RET_SAVE A\n"
                              "251 RST_ASSERT RST_A\n251 PLL_OFF PLL0"}, {11, NULL}, {12, NULL}, {13, NULL}, {14, NULL}, {15, NULL}}, NVT, 0, NULL, NULL},
    {"short PG glitch", {{10, "191 INIT_DONE INIT_A\n250 RAIL_PG_LOST VDD_A\n251 RAIL_PG VDD_A"}}, NVT, 0, NULL, NULL},
};
#define NBAD ((int)(sizeof BAD / sizeof BAD[0]))

static int build_text(const badlog_t *b, char *out, size_t sz) {
    size_t p = 0;
    for (int i = 0; i < NMG; i++) {
        const char *s = MINI_GOOD[i];
        bool del = false;
        for (int k = 0; k < 6 && b && b->p[k].line; k++)
            if (b->p[k].line == i) { if (b->p[k].text) s = b->p[k].text; else del = true; }
        if (del) continue;
        int w = snprintf(out + p, sz - p, "%s\n", s);
        if (w < 0 || (size_t)w >= sz - p) return -1;
        p += (size_t)w;
    }
    return 0;
}

static void part_b(void) {
    printf("== Part B: 체커 — 손으로 쓴 로그 ==\n");
    static char text[4096];
    event_t L[64];
    build_text(NULL, text, sizeof text);
    int n = parse_log(&G_MINI, text, L, 64);
    CHECK(n == NMG && check_log(&G_MINI, L, n, T_REACT, &CK) == 0);
    for (int i = 0; i < NBAD; i++) {
        const badlog_t *b = &BAD[i];
        build_text(b, text, sizeof text);
        n = parse_log(&G_MINI, text, L, 64);
        int nv = n > 0 ? check_log(&G_MINI, L, n, T_REACT, &CK) : -1;
        bool ok = nv == b->cnt;
        if (ok && b->cnt == 1) {
            const viol_t *v = &CK.vio[0];
            ok = (int)v->ty == b->ty &&
                 (!b->node || (v->node >= 0 && strcmp(G_MINI.n[v->node].name, b->node) == 0)) &&
                 (!b->dep || (v->dep >= 0 && strcmp(G_MINI.n[v->dep].name, b->dep) == 0));
        }
        CHECK(ok);
        printf("  %-16s -> %d violation(s)%s\n", b->name, nv, ok ? "" : "   <-- 기대와 다름");
        print_viols(&G_MINI, &CK);
    }
    /* 위반 3개를 한 로그에: 각각 한 번씩, 로그 순서대로 */
    badlog_t multi = {"multi", {{2, "105 RAIL_EN VDD_P"}, {7, "177 RST_DEASSERT RST_A"}, {8, "177 ISO_OFF A"}, {13, NULL}}, NVT, 3, NULL, NULL};
    build_text(&multi, text, sizeof text);
    n = parse_log(&G_MINI, text, L, 64);
    CHECK(check_log(&G_MINI, L, n, T_REACT, &CK) == 3 && CK.vio[0].ty == V_MIN_DELAY &&
          CK.vio[1].ty == V_RST_BEFORE_CLK && CK.vio[2].ty == V_OFF_BEFORE_SAVE);
    printf("  multi            -> %d violation(s)\n", CK.nv);
    print_viols(&G_MINI, &CK);
    /* 파서 오류 */
    CHECK(parse_log(&G_MINI, "0 RAIL_EN VDD_X\n", L, 64) == -1);
    CHECK(parse_log(&G_MINI, "0 RAIL_EN VDD_A\n1.2345 RAIL_PG VDD_A\n", L, 64) == -2);
    CHECK(parse_log(&G_MINI, "# c\n\n12.5 ISO_ON A  # x\n", L, 64) == 1 && L[0].t == 12500 && L[0].x == 0);

    printf("== Part B: 시퀀서 로그 변이 (인접 이벤트 내용 교환) ==\n");
    seq_init(&SQ, &G_SOC, NULL);
    seq_up(&SQ); seq_run(&SQ, US(2000)); seq_down(&SQ, -1);
    int N = SQ.nlog, det = 0, same_t = 0, miss_dep = 0, miss_rst = 0;
    static event_t M[MAXLOG];
    for (int i = 0; i + 1 < N; i++) {
        memcpy(M, SQ.log, sizeof(event_t) * (size_t)N);
        event_t a = M[i]; M[i].k = M[i + 1].k; M[i].x = M[i + 1].x; M[i + 1].k = a.k; M[i + 1].x = a.x;
        int nv = check_log(&G_SOC, M, N, T_REACT, &CK);
        if (nv) { det++; continue; }
        if (M[i].t == M[i + 1].t) same_t++;
        /* 놓친 교환이 '직접 간선으로 묶인 두 노드' 이면 체커 구멍 — 단 리셋 assert 끼리는 규칙이 없다(설계 선택) */
        bool edge = false;
        if (!ev_is_dom(M[i].k) && !ev_is_dom(M[i + 1].k))
            for (int k = 0; k < G_SOC.ne; k++)
                if ((G_SOC.e[k].from == M[i].x && G_SOC.e[k].to == M[i + 1].x) ||
                    (G_SOC.e[k].from == M[i + 1].x && G_SOC.e[k].to == M[i].x)) edge = true;
        if (!edge) continue;
        if (M[i].k == EV_RST_ASSERT && M[i + 1].k == EV_RST_ASSERT) miss_rst++; else miss_dep++;
        printf("  undetected swap #%d: %s %s <-> %s %s\n", i, EV_NAME[M[i + 1].k], G_SOC.n[M[i + 1].x].name,
               EV_NAME[M[i].k], G_SOC.n[M[i].x].name);
    }
    printf("  %d adjacent swaps: detected %d, undetected %d (same timestamp %d), undetected with a direct edge %d (+%d RST_ASSERT pair)\n",
           N - 1, det, N - 1 - det, same_t, miss_dep, miss_rst);
    CHECK(miss_dep == 0);

    printf("== Part B: 스펙 버그는 같은 스펙으로 만든 체커가 못 본다 ==\n");
    graph_t gb = G_SOC;                                    /* VDD_DDR→PLL_DDR 간선을 빠뜨린 스펙 */
    int vd = g_find(&gb, "VDD_DDR"), pd = g_find(&gb, "PLL_DDR");
    for (int k = 0; k < gb.ne; k++)
        if (gb.e[k].from == vd && gb.e[k].to == pd) { gb.e[k] = gb.e[gb.ne - 1]; gb.ne--; break; }
    seq_init(&SQ, &gb, NULL);
    seq_up(&SQ);
    char tb[24];
    int nv_same = check_log(&gb, SQ.log, SQ.nlog, T_REACT, &CK);
    int nv_true = check_log(&G_SOC, SQ.log, SQ.nlog, T_REACT, &CK);
    printf("  buggy spec: power-up %s, checker(buggy spec) %d violations, checker(true spec) %d:\n",
           fmt_t(SQ.t_up, tb), nv_same, nv_true);
    print_viols(&G_SOC, &CK);
    CHECK(nv_same == 0 && nv_true == 1 && CK.vio[0].ty == V_CLK_BEFORE_RAIL && CK.vio[0].dep == vd);
}

/* ---- Part C: 결함 주입 ---- */
typedef struct {
    const char *name; int bug;
    const char *dly_node; int64_t dly;                  /* done_delay */
    const char *ext_node; evk_t ext_k; int64_t ext_t, width;
    int want_state, want_fault; const char *want_node; int64_t want_ft;
} scen_t;
static const scen_t SCEN[] = {
    {"pg_never",   0, "VDD_DDR", NEVER,     NULL, NEV, 0, 0,                         SQ_FAULT, FR_PG_TIMEOUT, "VDD_DDR", US(1450)},
    {"lock_never", 0, "PLL_DDR", NEVER,     NULL, NEV, 0, 0,                         SQ_FAULT, FR_LOCK_TIMEOUT, "PLL_DDR", US(850)},
    {"init_never", 0, "INIT_DDRC", NEVER,   NULL, NEV, 0, 0,                         SQ_FAULT, FR_INIT_TIMEOUT, "INIT_DDRC", US(1240) + 10},
    {"unlock",     0, NULL, 0,              "PLL_SYS", EV_PLL_UNLOCK, US(2000), 0,   SQ_FAULT, FR_PLL_UNLOCK, "PLL_SYS", US(2000)},
    {"brownout",   0, NULL, 0,              "VDD_CORE", EV_RAIL_PG_LOST, US(2000), US(50), SQ_FAULT, FR_BROWNOUT, "VDD_CORE", US(2002)},
    {"glitch",     0, NULL, 0,              "VDD_IO", EV_RAIL_PG_LOST, US(2000), US(1), SQ_ON, FR_NONE, NULL, 0},
    {"late_pg",    0, "VDD_IO", US(1500),   NULL, NEV, 0, 0,                         SQ_FAULT, FR_PG_TIMEOUT, "VDD_IO", US(1400)},
    {"bo_up",      0, NULL, 0,              "VDD_CORE", EV_RAIL_PG_LOST, US(610), US(100), SQ_FAULT, FR_BROWNOUT, "VDD_CORE", US(612)},
    {"pllrail",    0, NULL, 0,              "VDD_PLL", EV_RAIL_PG_LOST, US(405), US(1), SQ_ON, FR_NONE, NULL, 0},
};
#define NSCEN ((int)(sizeof SCEN / sizeof SCEN[0]))

static void run_scen(seq_t *sq, const graph_t *g, const scen_t *s) {
    inj_t inj;
    memset(&inj, 0, sizeof inj);
    if (s->dly_node) inj.done_delay[g_find(g, s->dly_node)] = s->dly;
    if (s->ext_node) { inj.ext[0].t = s->ext_t; inj.ext[0].k = s->ext_k; inj.ext[0].v = g_find(g, s->ext_node); inj.ext[0].width = s->width; inj.next = 1; }
    seq_init(sq, g, &inj);
    sq->bug = (unsigned)s->bug;
    seq_up(sq);
}

static void part_c(void) {
    printf("== Part C: 결함 주입 → 안전 상태 + 원인 레지스터 + 체커 ==\n");
    char tb[24];
    for (int i = 0; i < NSCEN; i++) {
        const scen_t *s = &SCEN[i];
        run_scen(&SQ, &G_SOC, s);
        int nv = check_seq(&SQ);
        bool ok = (int)SQ.state == s->want_state && SQ.fault == s->want_fault && nv == 0 &&
                  (!s->want_node || (SQ.fault_node == g_find(&G_SOC, s->want_node) && SQ.fault_t == s->want_ft));
        CHECK(ok);
        printf("  %-10s state=%-5s cause=%-12s node=%-9s t=%-12s events=%-3d ignored=%d filtered=%d violations=%d\n",
               s->name, SQ_NAME[SQ.state], FR_NAME[SQ.fault], SQ.fault_node >= 0 ? G_SOC.n[SQ.fault_node].name : "-",
               SQ.fault_node >= 0 ? fmt_t(SQ.fault_t, tb) : "-", SQ.nlog, SQ.ignored, SQ.filtered, nv);
        if (nv) print_viols(&G_SOC, &CK);
        if (i == 0 || i == 1 || i == 2 || i == 4 || i == 6 || i == 7) CHECK(safe_off(&SQ));
    }
    int PLL_SYS = g_find(&G_SOC, "PLL_SYS");
    /* unlock: 레일은 전부 유지, PLL_SYS 하위만 꺼짐, PLL_DDR 쪽은 계속 동작 */
    run_scen(&SQ, &G_SOC, &SCEN[3]);
    bool rails_on = true, sub_off = true;
    bool mk[MAXN]; closure(&G_SOC, PLL_SYS, mk);
    for (int v = 0; v < G_SOC.nn; v++) {
        if (G_SOC.n[v].kind == K_RAIL) rails_on = rails_on && SQ.st[v] == NS_ON;
        if (mk[v]) sub_off = sub_off && SQ.st[v] == NS_OFF;
    }
    CHECK(rails_on && sub_off && SQ.st[g_find(&G_SOC, "CLK_DDR")] == NS_ON && SQ.iso[2] && SQ.iso[3] && SQ.iso[4]);
    /* brownout: 끊긴 레일은 저장 불가 → RET_SAVE 없음, 늦게 돌아온 PG 는 무시 */
    run_scen(&SQ, &G_SOC, &SCEN[4]);
    CHECK(count_ev(&SQ, EV_RET_SAVE) == 0 && SQ.ignored == 1 && count_ev(&SQ, EV_RAIL_PG_LOST) == 1);
    /* 글리치: 걸러지고 계속 ON, 로그에는 LOST/PG 가 남는다 */
    run_scen(&SQ, &G_SOC, &SCEN[5]);
    CHECK(SQ.filtered == 1 && SQ.faults_total == 0 && count_ev(&SQ, EV_RAIL_PG_LOST) == 1);
    /* 늦은 PG: timeout 뒤 도착 → 무시, 되살아나지 않음 */
    run_scen(&SQ, &G_SOC, &SCEN[6]);
    CHECK(SQ.ignored == 1 && SQ.state == SQ_FAULT && seq_up(&SQ) == -1);
    /* PLL 레일 글리치: 복귀 시각부터 10 µs 를 다시 잰다 → PLL_SYS 시작 416 µs, 전체 시간은 그대로 */
    run_scen(&SQ, &G_SOC, &SCEN[8]);
    int pll_en = -1;
    for (int i = 0; i < SQ.nlog; i++) if (SQ.log[i].k == EV_PLL_EN && SQ.log[i].x == PLL_SYS) pll_en = i;
    CHECK(pll_en >= 0 && SQ.log[pll_en].t == US(416) && SQ.t_up == 890010);

    printf("== Part C: 틀린 시퀀서를 체커가 잡는가 ==\n");
    /* (1) ISO_ON 을 빼먹는 시퀀서 → 전체 power-down 에서 OFF_BEFORE_ISO ×3 */
    seq_init(&SQ, &G_SOC, NULL); SQ.bug = BUG_NO_ISO;
    seq_up(&SQ); seq_run(&SQ, US(2000)); seq_down(&SQ, -1);
    int nv = check_seq(&SQ), c1 = 0;
    for (int i = 0; i < nv; i++) c1 += CK.vio[i].ty == V_OFF_BEFORE_ISO;
    CHECK(nv == 3 && c1 == 3);
    printf("  no_iso       -> %d\n", nv); print_viols(&G_SOC, &CK);
    /* (2) RET_SAVE 를 빼먹는 시퀀서 → sleep 에서 OFF_BEFORE_SAVE ×1 */
    seq_init(&SQ, &G_SOC, NULL); SQ.bug = BUG_NO_SAVE;
    seq_up(&SQ); seq_run(&SQ, US(2000)); seq_down(&SQ, g_find(&G_SOC, "VDD_CORE"));
    nv = check_seq(&SQ);
    CHECK(nv == 1 && CK.vio[0].ty == V_OFF_BEFORE_SAVE);
    printf("  no_save      -> %d\n", nv); print_viols(&G_SOC, &CK);
    /* (3) 리셋 동기화 지연(K)을 0 으로 잘못 넣은 시퀀서 그래프 → RST_BEFORE_CLK ×4 */
    graph_t g0 = G_SOC;
    for (int k = 0; k < g0.ne; k++) if (g0.n[g0.e[k].from].kind == K_CLK && g0.n[g0.e[k].to].kind == K_RST) g0.e[k].min_ns = 0;
    seq_init(&SQ, &g0, NULL); seq_up(&SQ);
    nv = check_log(&G_SOC, SQ.log, SQ.nlog, T_REACT, &CK);
    int c3 = 0;
    for (int i = 0; i < nv; i++) c3 += CK.vio[i].ty == V_RST_BEFORE_CLK;
    CHECK(nv == 4 && c3 == 4);
    printf("  k_sync=0     -> %d (power-up %s)\n", nv, fmt_t(SQ.t_up, tb)); print_viols(&G_SOC, &CK);
    /* (4) unlock 을 무시하는 시퀀서 → UNHANDLED_LOSS ×1 */
    scen_t s4 = SCEN[3]; s4.bug = BUG_IGNORE_UNLOCK;
    run_scen(&SQ, &G_SOC, &s4);
    nv = check_seq(&SQ);
    CHECK(nv == 1 && CK.vio[0].ty == V_UNHANDLED_LOSS && SQ.state == SQ_ON);
    printf("  ignore_unlock-> %d\n", nv); print_viols(&G_SOC, &CK);
}

/* ---- 보기 모드 ---- */
static int show(const char *what) {
    seq_init(&SQ, &G_SOC, NULL);
    if (strcmp(what, "up") == 0 || strcmp(what, "serial") == 0) { SQ.serial = what[0] == 's'; seq_up(&SQ); }
    else if (strcmp(what, "down") == 0) { seq_up(&SQ); seq_run(&SQ, US(2000)); seq_down(&SQ, -1); }
    else if (strcmp(what, "sleep") == 0) { seq_up(&SQ); seq_run(&SQ, US(2000)); seq_down(&SQ, 2); seq_run(&SQ, US(3000)); seq_up(&SQ); }
    else {
        int i = 0;
        while (i < NSCEN && strcmp(SCEN[i].name, what) != 0) i++;
        if (i == NSCEN) { fprintf(stderr, "unknown scenario %s\n", what); return 2; }
        run_scen(&SQ, &G_SOC, &SCEN[i]);
    }
    print_log(&G_SOC, SQ.log, 0, SQ.nlog);
    char tb[24];
    printf("state=%s cause=%s t_up=%s checker violations=%d\n", SQ_NAME[SQ.state], FR_NAME[SQ.fault],
           SQ.state == SQ_ON ? fmt_t(SQ.t_up, tb) : "-", check_seq(&SQ));
    print_viols(&G_SOC, &CK);
    return 0;
}
static int gantt(void) {
    seq_init(&SQ, &G_SOC, NULL);
    seq_up(&SQ);
    int64_t start[MAXN];
    for (int i = 0; i < SQ.nlog; i++) {
        evk_t k = SQ.log[i].k;
        if (k == EV_RAIL_EN || k == EV_PLL_EN || k == EV_CLK_ON || k == EV_RST_DEASSERT || k == EV_INIT_START) start[SQ.log[i].x] = SQ.log[i].t;
    }
    char a[24], b[24];
    for (int v = 0; v < G_SOC.nn; v++)
        printf("  %-10s %-4s start %12s  done %12s\n", G_SOC.n[v].name,
               (const char *[]){"RAIL", "PLL", "CLK", "RST", "INIT"}[G_SOC.n[v].kind], fmt_t(start[v], a), fmt_t(SQ.done_at[v], b));
    return 0;
}
static int check_file(const char *path, const char *which) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 2; }
    static char text[1 << 16];
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = '\0';
    const graph_t *g = strcmp(which, "mini") == 0 ? &G_MINI : &G_SOC;
    static event_t L[MAXLOG];
    int m = parse_log(g, text, L, MAXLOG);
    if (m < 0) { fprintf(stderr, "%s: parse error at line %d\n", path, -m); return 2; }
    int nv = check_log(g, L, m, T_REACT, &CK);
    printf("%d events, %d violation(s)\n", m, nv);
    print_viols(g, &CK);
    return nv ? 1 : 0;
}

int main(int argc, char **argv) {
    build_soc(&G_SOC);
    build_mini(&G_MINI, false);
    if (argc > 2 && strcmp(argv[1], "show") == 0) return show(argv[2]);
    if (argc > 1 && strcmp(argv[1], "gantt") == 0) return gantt();
    if (argc > 3 && strcmp(argv[1], "check") == 0) return check_file(argv[2], argv[3]);
    part_a();
    part_b();
    part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
