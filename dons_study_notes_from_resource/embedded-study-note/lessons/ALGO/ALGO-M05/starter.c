/* ALGO-M05 위상 정렬: 드라이버 초기화 순서
 *
 * 간선 u -> v = "u 를 먼저 초기화해야 v 를 초기화할 수 있다" (v 가 u 에 의존, u = supplier, v = consumer).
 *   Part A  Kahn 알고리즘 + 결정적 tie-break (준비된 것 중 가장 작은 id 먼저)
 *           topo_kahn_lex  : 준비 집합을 min-heap 으로   O((V+E) log V)
 *           topo_lex_scan  : 준비 집합을 매번 선형 스캔   O(V^2 + E)   (두 결과는 같아야 한다)
 *   Part B  find_cycle     : 3색(WHITE/GRAY/BLACK) DFS, 재귀 없이 명시적 스택, 실제 사이클 경로를 돌려준다
 *   Part C  init_waves     : 병렬 초기화 단계(wave) = 소스에서의 최장 경로 길이
 *           plan_report    : 드라이버 이름 목록 -> 초기화 순서 / wave, 또는 사이클 경로 + 막힌 드라이버 보고
 *
 * 테스트: n<=4 의 모든 방향 그래프(자기 루프 포함 65536개)를 순열 전수 탐색과 대조 (DAG 개수 = OEIS A003024),
 *         n=5 의 모든 그래프(2^20개)에서 Kahn/DFS 판정 일치 + DAG 29281개, xorshift32 무작위 DAG + 주입한 사이클,
 *         이름 붙은 경계 사례, 드라이버 목록 문자열 보고서 전체 비교. 동적 할당 없음.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st   (solution.c 도 같은 명령)
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_V 512
#define MAX_E 4096

typedef struct { uint16_t from, to; } edge_t;      /* from -> to : from 먼저 */

/* CSR(compressed sparse row): u 의 나가는 간선 = adj[off[u] .. off[u+1]) */
typedef struct {
    uint16_t n, m;
    uint16_t off[MAX_V + 1];
    uint16_t adj[MAX_E];
} graph_t;

/* 제공: 간선 목록 -> CSR. counting sort 라 같은 출발점 안에서는 입력 순서가 유지된다. 실패 -1 */
static int graph_build(graph_t *g, uint16_t n, const edge_t *e, size_t m) {
    static uint16_t cur[MAX_V];
    if (n > MAX_V || m > MAX_E) return -1;
    for (size_t i = 0; i < m; i++)
        if (e[i].from >= n || e[i].to >= n) return -1;
    g->n = n; g->m = (uint16_t)m;
    memset(g->off, 0, sizeof g->off);
    for (size_t i = 0; i < m; i++) g->off[e[i].from + 1]++;
    for (uint16_t u = 0; u < n; u++) g->off[u + 1] = (uint16_t)(g->off[u + 1] + g->off[u]);
    for (uint16_t u = 0; u < n; u++) cur[u] = g->off[u];
    for (size_t i = 0; i < m; i++) g->adj[cur[e[i].from]++] = e[i].to;
    return 0;
}

/* 제공: 작은 정적 min-heap (DS-B12 에서 자세히). 정점마다 최대 한 번 들어가므로 MAX_V 칸이면 충분 */
typedef struct { uint16_t a[MAX_V]; uint16_t len; } minheap_t;

static void mh_push(minheap_t *h, uint16_t x) {
    uint16_t i = h->len++;
    while (i > 0) {
        uint16_t p = (uint16_t)((i - 1) / 2);
        if (h->a[p] <= x) break;
        h->a[i] = h->a[p]; i = p;
    }
    h->a[i] = x;
}

static uint16_t mh_pop(minheap_t *h) {           /* 호출 전 len > 0 */
    uint16_t top = h->a[0], x = h->a[--h->len], i = 0;
    for (;;) {
        uint16_t c = (uint16_t)(2 * i + 1);
        if (c >= h->len) break;
        if (c + 1 < h->len && h->a[c + 1] < h->a[c]) c++;
        if (x <= h->a[c]) break;
        h->a[i] = h->a[c]; i = c;
    }
    if (h->len) h->a[i] = x;
    return top;
}

/* ======================= Part A: Kahn + 결정적 tie-break ======================= */
/* order[0..k) 에 위상 순서를 쓰고 k 를 돌려준다. k == n 이면 DAG, k < n 이면 사이클 때문에 남은 정점이 있다.
 * 준비된(진입 차수 0) 정점이 여럿이면 가장 작은 id 먼저 -> 결과는 사전순 최소 위상 순서(유일). 정적 작업 공간: 재진입 불가 */
int topo_kahn_lex(const graph_t *g, uint16_t *order) {
    (void)g; (void)order; (void)mh_push; (void)mh_pop;
    return 0; /* TODO: 진입 차수 계산 -> 0 인 정점을 min-heap 에 -> pop 한 정점의 나가는 간선을 지우며 새 0 을 push */
}

/* 같은 결과를 heap 없이: 매 단계 "아직 안 뽑았고 진입 차수 0 인 가장 작은 id" 를 선형 스캔으로 찾는다 */
int topo_lex_scan(const graph_t *g, uint16_t *order) {
    (void)g; (void)order;
    return 0; /* TODO: 매 단계 done 이 아니고 진입 차수 0 인 가장 작은 u 를 0..n-1 스캔으로 찾는다. 없으면 멈춤 */
}

/* ======================= Part B: 3색 DFS 사이클 경로 ======================= */
enum { WHITE = 0, GRAY = 1, BLACK = 2 };

/* 사이클이 있으면 cyc[0..L) 에 경로를 쓰고 L 을 돌려준다: cyc[i] -> cyc[i+1], cyc[L-1] -> cyc[0] 이 모두 간선.
 * 자기 루프면 L = 1. 사이클이 없으면 0. 시작 정점은 id 오름차순, 이웃은 CSR(입력) 순서 -> 결과가 결정적.
 * 재귀 없음: stk[0..top] 이 정확히 현재 GRAY 경로이고, it[d] 는 stk[d] 의 다음에 볼 간선 위치 */
int find_cycle(const graph_t *g, uint16_t *cyc) {
    (void)g; (void)cyc;
    return 0; /* TODO: 3색 DFS (명시적 스택). GRAY 정점으로 가는 간선을 만나면 스택에서 그 정점부터 top 까지가 사이클 */
}

/* ======================= Part C: 병렬 wave 와 보고서 ======================= */
/* order 는 완전한 위상 순서(길이 n). wave[v] = 소스에서 v 까지 최장 경로의 간선 수. 반환 = wave 개수 (n == 0 이면 0) */
int init_waves(const graph_t *g, const uint16_t *order, uint16_t *wave) {
    (void)g; (void)order; (void)wave;
    return 0; /* TODO: wave[] = 0 으로 시작, order 순서대로 u -> v 마다 wave[v] = max(wave[v], wave[u] + 1) */
}

/* 드라이버 의존성 DB (파서는 제공) */
#define MAX_DRV 64
#define MAX_DEP 512
#define NAME_LEN 16
typedef struct {
    uint16_t n;
    char name[MAX_DRV][NAME_LEN];
    size_t m;
    edge_t e[MAX_DEP];
    graph_t g;
} dep_db_t;

/* 제공: out[*len..cap) 에 printf. 넘치면 *len = cap (이후 쓰기는 무시) */
__attribute__((format(printf, 4, 5)))
static void emit(char *out, size_t cap, size_t *len, const char *fmt, ...) {
    if (*len >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(out + *len, cap - *len, fmt, ap);
    va_end(ap);
    *len = (w < 0 || (size_t)w >= cap - *len) ? cap : *len + (size_t)w;
}

/* 보고서. DAG 이면 0, 사이클이면 1, out 이 모자라면 -1.
 *   DAG:    "OK <n> drivers, <W> waves\norder: a b c\nwave 0: a\nwave 1: b c\n..."
 *           (wave 줄 안의 순서 = order 의 순서)
 *   사이클: "CYCLE: a -> b -> a\ninit ok (<k>): ...\nblocked (<n-k>): ...\n"
 *           (ok = Kahn 이 뽑은 순서, blocked = 못 뽑은 드라이버를 id 순으로) */
int plan_report(const dep_db_t *db, char *out, size_t cap) {
    (void)db; (void)emit;
    if (cap) out[0] = '\0';
    return -1; /* TODO: topo_kahn_lex -> 전부 뽑혔으면 init_waves + "OK ..." / 아니면 find_cycle + "CYCLE: ..." */
}

/* ======================= 제공: 드라이버 목록 파서 ======================= */
/* 형식: 한 줄에 "name: dep1 dep2 ..." (name 이 dep 들에 의존). '#' 부터 줄 끝은 주석, 빈 줄 허용.
 * 이름 = [a-z0-9_], 15자 이하. 선언 순서대로 id 0,1,2,... 오류면 -1 과 err 에 이유 (줄 번호 포함). */
static int is_name_ch(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }

static int find_name(const dep_db_t *db, const char *s, size_t len) {
    for (uint16_t i = 0; i < db->n; i++)
        if (strlen(db->name[i]) == len && memcmp(db->name[i], s, len) == 0) return i;
    return -1;
}

static int dep_parse(dep_db_t *db, const char *text, char *err, size_t errcap) {
    db->n = 0; db->m = 0; err[0] = '\0';
    for (int pass = 0; pass < 2; pass++) {             /* pass 0: 이름 선언, pass 1: 의존성 해석 */
        const char *p = text;
        int line = 0;
        while (*p) {
            const char *eol = p;
            while (*eol && *eol != '\n') eol++;
            line++;
            const char *q = p, *end = eol;
            for (const char *h = p; h < eol; h++) if (*h == '#') { end = h; break; }
            while (q < end && *q == ' ') q++;
            if (q < end) {
                const char *nm = q;
                while (q < end && is_name_ch(*q)) q++;
                size_t nlen = (size_t)(q - nm);
                if (nlen == 0 || nlen >= NAME_LEN || q >= end || *q != ':') {
                    snprintf(err, errcap, "line %d: expected 'name: deps'", line); return -1;
                }
                int self = find_name(db, nm, nlen);
                if (pass == 0) {
                    if (self >= 0) { snprintf(err, errcap, "line %d: duplicate '%.*s'", line, (int)nlen, nm); return -1; }
                    if (db->n >= MAX_DRV) { snprintf(err, errcap, "line %d: too many drivers", line); return -1; }
                    memcpy(db->name[db->n], nm, nlen); db->name[db->n][nlen] = '\0'; db->n++;
                } else {
                    q++;
                    for (;;) {
                        while (q < end && *q == ' ') q++;
                        if (q >= end) break;
                        const char *d = q;
                        while (q < end && is_name_ch(*q)) q++;
                        if (q == d) { snprintf(err, errcap, "line %d: bad character '%c'", line, *q); return -1; }
                        int dep = find_name(db, d, (size_t)(q - d));
                        if (dep < 0) {
                            snprintf(err, errcap, "line %d: '%s' needs undeclared '%.*s'", line, db->name[self], (int)(q - d), d);
                            return -1;
                        }
                        if (db->m >= MAX_DEP) { snprintf(err, errcap, "line %d: too many deps", line); return -1; }
                        db->e[db->m].from = (uint16_t)dep; db->e[db->m].to = (uint16_t)self; db->m++;
                    }
                }
            }
            p = *eol ? eol + 1 : eol;
        }
    }
    return graph_build(&db->g, db->n, db->e, db->m);
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}

static int has_edge(const graph_t *g, uint16_t u, uint16_t v) {
    for (uint16_t i = g->off[u]; i < g->off[u + 1]; i++) if (g->adj[i] == v) return 1;
    return 0;
}

/* order[0..k) 가 서로 다른 정점이고, 그 안의 모든 간선이 앞->뒤를 지키며, 뽑힌 정점의 선행자도 모두 뽑혔는가 */
static int valid_prefix(const graph_t *g, const uint16_t *order, int k) {
    static int pos[MAX_V];
    if (k < 0 || k > g->n) return 0;
    for (uint16_t v = 0; v < g->n; v++) pos[v] = -1;
    for (int i = 0; i < k; i++) {
        if (order[i] >= g->n || pos[order[i]] != -1) return 0;
        pos[order[i]] = i;
    }
    for (uint16_t u = 0; u < g->n; u++)
        for (uint16_t i = g->off[u]; i < g->off[u + 1]; i++) {
            uint16_t v = g->adj[i];
            if (pos[v] != -1 && (pos[u] == -1 || pos[u] >= pos[v])) return 0;   /* 모든 간선 검사 */
        }
    return 1;
}

/* 정의 그대로의 사전순 최소 검사: 매 단계 order[i] 가 "그때 준비된 정점 중 최소" 인가 */
static int is_lexmin(const graph_t *g, const uint16_t *order, int k) {
    static uint16_t indeg[MAX_V];
    static uint8_t placed[MAX_V];
    memset(indeg, 0, sizeof indeg); memset(placed, 0, sizeof placed);
    for (uint16_t i = 0; i < g->m; i++) indeg[g->adj[i]]++;
    for (int i = 0; i < k; i++) {
        for (uint16_t v = 0; v < order[i]; v++) if (!placed[v] && indeg[v] == 0) return 0;
        if (indeg[order[i]] != 0) return 0;
        placed[order[i]] = 1;
        for (uint16_t j = g->off[order[i]]; j < g->off[order[i] + 1]; j++) indeg[g->adj[j]]--;
    }
    for (uint16_t v = 0; v < g->n; v++) if (!placed[v] && indeg[v] == 0) return 0;   /* 멈춘 게 정당한가 */
    return 1;
}

/* 사이클 경로 검사: 길이 1..n, 정점 중복 없음, 연속 간선과 닫는 간선이 실제로 존재 */
static int valid_cycle(const graph_t *g, const uint16_t *c, int L) {
    static uint8_t seen[MAX_V];
    if (L < 1 || L > g->n) return 0;
    memset(seen, 0, g->n);
    for (int i = 0; i < L; i++) {
        if (c[i] >= g->n || seen[c[i]]) return 0;
        seen[c[i]] = 1;
        if (!has_edge(g, c[i], c[(i + 1) % L])) return 0;
    }
    return 1;
}

/* 기준: 모든 순열을 사전순으로 돌며 첫 유효 위상 순서 (n <= 6) */
static int brute_lexmin(const graph_t *g, uint16_t *best) {
    uint16_t p[8];
    int n = g->n;
    for (int i = 0; i < n; i++) p[i] = (uint16_t)i;
    for (;;) {
        if (valid_prefix(g, p, n)) { memcpy(best, p, (size_t)n * sizeof p[0]); return 1; }
        int i = n - 2;                                         /* 다음 순열 (사전순) */
        while (i >= 0 && p[i] > p[i + 1]) i--;
        if (i < 0) return 0;
        int j = n - 1;
        while (p[j] < p[i]) j--;
        uint16_t t = p[i]; p[i] = p[j]; p[j] = t;
        for (int a = i + 1, b = n - 1; a < b; a++, b--) { t = p[a]; p[a] = p[b]; p[b] = t; }
    }
}

/* Kahn 이 뽑지 못하는 정점 = 어떤 사이클 위의 정점에서 도달 가능한 정점 (자기 자신 포함). Warshall 로 기준 계산 */
static int blocked_matches(const graph_t *g, const uint16_t *order, int k) {
    uint8_t r[8][8] = { { 0 } }, ok[8] = { 0 };
    int n = g->n;
    for (int u = 0; u < n; u++)
        for (uint16_t i = g->off[u]; i < g->off[u + 1]; i++) r[u][g->adj[i]] = 1;
    for (int w = 0; w < n; w++) for (int a = 0; a < n; a++) for (int b = 0; b < n; b++)
        if (r[a][w] && r[w][b]) r[a][b] = 1;
    for (int i = 0; i < k; i++) ok[order[i]] = 1;
    for (int v = 0; v < n; v++) {
        int blocked = 0;
        for (int c = 0; c < n; c++) if (r[c][c] && (c == v || r[c][v])) blocked = 1;
        if (blocked == ok[v]) return 0;
    }
    return 1;
}

static graph_t G;                     /* 큰 구조체는 정적으로 */
static unsigned long g_build_fail;
#define CHECK_BUILD(call) do { if ((call) != 0) g_build_fail++; } while (0)
static edge_t E[MAX_E];

static int build_mask(int n, uint32_t mask, int self_loops) {
    size_t m = 0; int bit = 0;
    for (int u = 0; u < n; u++)
        for (int v = 0; v < n; v++) {
            if (u == v && !self_loops) continue;
            if (mask >> bit & 1u) { E[m].from = (uint16_t)u; E[m].to = (uint16_t)v; m++; }
            bit++;
        }
    return graph_build(&G, (uint16_t)n, E, m);
}

/* 무작위 DAG: 무작위 순열 rank 를 만들고 간선을 rank 가 작은 쪽 -> 큰 쪽으로만 */
static size_t make_random_dag(uint16_t n, size_t m, uint16_t *perm) {
    for (uint16_t i = 0; i < n; i++) perm[i] = i;
    for (uint16_t i = n; i > 1; i--) { uint16_t j = (uint16_t)(xorshift32() % i); uint16_t t = perm[i - 1]; perm[i - 1] = perm[j]; perm[j] = t; }
    if (n < 2) return 0;
    for (size_t i = 0; i < m; i++) {
        uint16_t a = (uint16_t)(xorshift32() % n), b = (uint16_t)(xorshift32() % n);
        if (a == b) b = (uint16_t)((a + 1) % n);
        if (a > b) { uint16_t t = a; a = b; b = t; }       /* a < b (rank 위치) */
        E[i].from = perm[a]; E[i].to = perm[b];
    }
    return m;
}

static const char *DRIVERS =
    "# supplier 가 먼저. 'imu: spi1 pmic gpio' = imu 는 spi1, pmic, gpio 뒤에\n"
    "clock:\n"
    "gpio: clock\n"
    "dma: clock\n"
    "i2c1: clock gpio\n"
    "spi1: clock gpio dma\n"
    "pmic: i2c1            # PMIC 는 I2C 로 설정한다\n"
    "imu: spi1 pmic gpio   # 센서 전원 레일 + 데이터 버스 + 인터럽트 핀\n"
    "baro: i2c1 pmic\n"
    "mag: i2c1 pmic\n"
    "flash: spi1\n"
    "fs: flash\n"
    "fusion: imu baro mag\n"
    "logger: fs fusion\n";

int main(void) {
    static uint16_t order[MAX_V], order2[MAX_V], cyc[MAX_V], ref[MAX_V], perm[MAX_V], wave[MAX_V];

    /* ---- 이름 붙은 경계 사례 ---- */
    CHECK(build_mask(0, 0, 0) == 0 && topo_kahn_lex(&G, order) == 0 && find_cycle(&G, cyc) == 0);
    CHECK(build_mask(1, 0, 1) == 0 && topo_kahn_lex(&G, order) == 1 && order[0] == 0 && find_cycle(&G, cyc) == 0);
    CHECK(build_mask(1, 1, 1) == 0 && topo_kahn_lex(&G, order) == 0 && find_cycle(&G, cyc) == 1 && cyc[0] == 0);  /* 자기 루프 */
    {   /* 다이아몬드 3->1, 3->2, 1->0, 2->0 (id 와 순서가 반대) : 사전순 최소 = 3 1 2 0 */
        const edge_t d[] = { {3,1}, {3,2}, {1,0}, {2,0} };
        CHECK_BUILD(graph_build(&G, 4, d, 4));
        int k = topo_kahn_lex(&G, order);
        CHECK(k == 4 && order[0] == 3 && order[1] == 1 && order[2] == 2 && order[3] == 0);
        CHECK(topo_lex_scan(&G, order2) == 4 && memcmp(order, order2, 4 * sizeof order[0]) == 0);
        CHECK(init_waves(&G, order, wave) == 3 && wave[3] == 0 && wave[1] == 1 && wave[2] == 1 && wave[0] == 2);
    }
    {   /* 2. Problem 의 예: 0 clock, 1 pmic, 2 gpio, 3 i2c1, 4 imu, 5 led -> 0 2 3 1 4 5 (FIFO 였다면 0 2 3 5 1 4) */
        const edge_t d[] = { {0,2}, {0,3}, {2,3}, {3,1}, {1,4}, {3,4}, {2,5} };
        CHECK_BUILD(graph_build(&G, 6, d, 7));
        static const uint16_t want[6] = { 0, 2, 3, 1, 4, 5 };
        CHECK(topo_kahn_lex(&G, order) == 6 && memcmp(order, want, sizeof want) == 0);
        CHECK(init_waves(&G, order, wave) == 5 && wave[5] == 2 && wave[4] == 4);
        const edge_t d2[] = { {0,2}, {0,3}, {2,3}, {3,1}, {1,4}, {3,4}, {2,5}, {1,3} };   /* + 1->3: i2c1 도 pmic 필요 */
        CHECK_BUILD(graph_build(&G, 6, d2, 8));
        CHECK(topo_kahn_lex(&G, order) == 3 && order[0] == 0 && order[1] == 2 && order[2] == 5);
        CHECK(find_cycle(&G, cyc) == 2 && cyc[0] == 3 && cyc[1] == 1);
    }
    {   /* 같은 간선 두 번 (의존성 중복 기재) + 고립 정점 : 2->0 두 번, 1 은 고립 -> 1 2 0 */
        const edge_t d[] = { {2,0}, {2,0} };
        CHECK_BUILD(graph_build(&G, 3, d, 2));
        CHECK(topo_kahn_lex(&G, order) == 3 && order[0] == 1 && order[1] == 2 && order[2] == 0);
        CHECK(topo_lex_scan(&G, order2) == 3 && memcmp(order, order2, 3 * sizeof order[0]) == 0);
    }
    {   /* 0 -> 1 -> 2 -> 3 -> 1 (사이클 1-2-3) + 3 -> 4 : 0 만 초기화되고 1..4 는 막힌다 */
        const edge_t d[] = { {0,1}, {1,2}, {2,3}, {3,1}, {3,4} };
        CHECK_BUILD(graph_build(&G, 5, d, 5));
        CHECK(topo_kahn_lex(&G, order) == 1 && order[0] == 0);
        CHECK(topo_lex_scan(&G, order2) == 1 && order2[0] == 0);
        int L = find_cycle(&G, cyc);
        CHECK(L == 3 && cyc[0] == 1 && cyc[1] == 2 && cyc[2] == 3);
    }

    /* ---- 전수: n = 0..4 의 모든 방향 그래프 (자기 루프 포함), 순열 전수 탐색과 대조 ---- */
    {
        unsigned long graphs = 0, dags = 0, bad_kahn = 0, bad_scan = 0, bad_cyc = 0, bad_block = 0;
        unsigned long dag_count[5] = { 0 };
        for (int n = 0; n <= 4; n++) {
            uint32_t total = 1u << (n * n);
            for (uint32_t mask = 0; mask < total; mask++) {
                build_mask(n, mask, 1);
                graphs++;
                int has = brute_lexmin(&G, ref);
                int k1 = topo_kahn_lex(&G, order), k2 = topo_lex_scan(&G, order2), L = find_cycle(&G, cyc);
                if (has) {
                    dags++; dag_count[n]++;
                    if (k1 != n || memcmp(order, ref, (size_t)n * sizeof ref[0]) != 0) bad_kahn++;
                    if (k2 != n || memcmp(order2, ref, (size_t)n * sizeof ref[0]) != 0) bad_scan++;
                    if (L != 0) bad_cyc++;
                } else {
                    if (k1 >= n || !valid_prefix(&G, order, k1) || !is_lexmin(&G, order, k1)) bad_kahn++;
                    if (k2 != k1 || memcmp(order, order2, (size_t)k1 * sizeof order[0]) != 0) bad_scan++;
                    if (!valid_cycle(&G, cyc, L)) bad_cyc++;
                }
                if (!blocked_matches(&G, order, k1)) bad_block++;
            }
        }
        printf("exhaustive n<=4: %lu graphs, DAGs per n = %lu %lu %lu %lu %lu (OEIS A003024: 1 1 3 25 543)\n",
               graphs, dag_count[0], dag_count[1], dag_count[2], dag_count[3], dag_count[4]);
        printf("  mismatches kahn=%lu scan=%lu cycle=%lu blocked-set=%lu\n", bad_kahn, bad_scan, bad_cyc, bad_block);
        CHECK(dag_count[1] == 1 && dag_count[2] == 3 && dag_count[3] == 25 && dag_count[4] == 543);
        CHECK(bad_kahn == 0);
        CHECK(bad_scan == 0);
        CHECK(bad_cyc == 0);
        CHECK(bad_block == 0);
        (void)dags;
    }

    /* ---- 전수: n = 5 의 모든 방향 그래프 (자기 루프 없음, 2^20 개): Kahn 판정 == DFS 판정, DAG 29281 개 ---- */
    {
        unsigned long dags = 0, disagree = 0, bad = 0;
        for (uint32_t mask = 0; mask < (1u << 20); mask++) {
            build_mask(5, mask, 0);
            int k = topo_kahn_lex(&G, order), L = find_cycle(&G, cyc);
            if (k == 5) { dags++; if (L != 0 || !valid_prefix(&G, order, 5)) bad++; }
            else if (L == 0 || !valid_cycle(&G, cyc, L)) bad++;
            if ((k == 5) != (L == 0)) disagree++;
        }
        printf("exhaustive n=5: 1048576 graphs, DAGs=%lu (OEIS A003024: 29281), kahn/dfs disagree=%lu, bad=%lu\n",
               dags, disagree, bad);
        CHECK(dags == 29281);
        CHECK(disagree == 0 && bad == 0);
    }

    /* ---- 무작위 DAG (n <= 300, m <= 4n) + 사이클 주입 ---- */
    {
        unsigned long inst = 0, edges = 0, bad_order = 0, bad_lex = 0, bad_scan = 0, bad_dfs = 0;
        unsigned long cyc_inst = 0, cyc_missed = 0, cyc_bad = 0, len_hist[4] = { 0 };
        for (int t = 0; t < 1500; t++) {
            uint16_t n = (uint16_t)(xorshift32() % 301);
            size_t m = n ? xorshift32() % (4u * n + 1) : 0;
            m = make_random_dag(n, m, perm);
            CHECK_BUILD(graph_build(&G, n, E, m));
            inst++; edges += m;
            int k = topo_kahn_lex(&G, order);
            if (k != n || !valid_prefix(&G, order, k)) bad_order++;
            if (!is_lexmin(&G, order, k)) bad_lex++;
            if (topo_lex_scan(&G, order2) != k || memcmp(order, order2, (size_t)k * sizeof order[0]) != 0) bad_scan++;
            if (find_cycle(&G, cyc) != 0) bad_dfs++;
            if (n < 2) continue;
            /* 사이클 주입: 무작위 정점에서 나가는 간선을 따라 걷다가 끝에서 시작으로 되돌아가는 간선 추가 */
            uint16_t s = (uint16_t)(xorshift32() % n), v = s;
            int steps = 1 + (int)(xorshift32() % 8);
            for (int st = 0; st < steps && G.off[v] < G.off[v + 1]; st++)
                v = G.adj[G.off[v] + xorshift32() % (uint32_t)(G.off[v + 1] - G.off[v])];
            E[m].from = v; E[m].to = s;                       /* v == s 면 자기 루프 */
            CHECK_BUILD(graph_build(&G, n, E, m + 1));
            cyc_inst++;
            k = topo_kahn_lex(&G, order);
            int L = find_cycle(&G, cyc);
            if (k == n || L == 0) cyc_missed++;
            else if (!valid_cycle(&G, cyc, L) || !valid_prefix(&G, order, k) || !is_lexmin(&G, order, k)) cyc_bad++;
            else len_hist[L == 1 ? 0 : L == 2 ? 1 : L <= 4 ? 2 : 3]++;
        }
        printf("random DAGs: %lu graphs, %lu edges: bad order=%lu not-lexmin=%lu scan mismatch=%lu false cycle=%lu\n",
               inst, edges, bad_order, bad_lex, bad_scan, bad_dfs);
        printf("injected cycles: %lu graphs, missed=%lu bad=%lu, reported length 1/2/3-4/5+ = %lu/%lu/%lu/%lu\n",
               cyc_inst, cyc_missed, cyc_bad, len_hist[0], len_hist[1], len_hist[2], len_hist[3]);
        CHECK(bad_order == 0 && bad_lex == 0);
        CHECK(bad_scan == 0);
        CHECK(bad_dfs == 0);
        CHECK(cyc_missed == 0 && cyc_bad == 0);
    }

    /* ---- 긴 사슬: id 역순 사슬 511 -> 510 -> ... -> 0 (DFS 깊이 = V, 재귀였다면 프레임 512개) ---- */
    {
        for (uint16_t i = 0; i + 1 < MAX_V; i++) { E[i].from = (uint16_t)(MAX_V - 1 - i); E[i].to = (uint16_t)(MAX_V - 2 - i); }
        CHECK_BUILD(graph_build(&G, MAX_V, E, MAX_V - 1));
        int k = topo_kahn_lex(&G, order);
        CHECK(k == MAX_V && order[0] == MAX_V - 1 && order[MAX_V - 1] == 0 && find_cycle(&G, cyc) == 0);
        CHECK(init_waves(&G, order, wave) == MAX_V);
        E[MAX_V - 1].from = 0; E[MAX_V - 1].to = MAX_V - 1;            /* 0 -> 511 로 닫으면 길이 512 사이클 */
        CHECK_BUILD(graph_build(&G, MAX_V, E, MAX_V));
        int L = find_cycle(&G, cyc);
        CHECK(topo_kahn_lex(&G, order) == 0 && L == MAX_V && valid_cycle(&G, cyc, L));
    }

    /* ---- init_waves: 무작위 DAG 에서 기준(벨만식 완화 반복)과 비교 ---- */
    {
        static uint16_t refw[MAX_V];
        unsigned long bad = 0;
        for (int t = 0; t < 300; t++) {
            uint16_t n = (uint16_t)(1 + xorshift32() % 120);
            size_t m = xorshift32() % (3u * n + 1);
            m = make_random_dag(n, m, perm);
            CHECK_BUILD(graph_build(&G, n, E, m));
            topo_kahn_lex(&G, order);
            int nw = init_waves(&G, order, wave);
            for (uint16_t v = 0; v < n; v++) refw[v] = 0;
            for (int changed = 1; changed; ) {                     /* 변화 없을 때까지 모든 간선 완화 */
                changed = 0;
                for (size_t i = 0; i < m; i++)
                    if (refw[E[i].to] < refw[E[i].from] + 1) { refw[E[i].to] = (uint16_t)(refw[E[i].from] + 1); changed = 1; }
            }
            int rmax = 0;
            for (uint16_t v = 0; v < n; v++) { if (refw[v] != wave[v]) bad++; if (refw[v] + 1 > rmax) rmax = refw[v] + 1; }
            if (nw != rmax) bad++;
        }
        printf("init_waves: 300 random DAGs vs relaxation reference, mismatches=%lu\n", bad);
        CHECK(bad == 0);
    }

    /* ---- Part C: 드라이버 목록 -> 보고서 ---- */
    {
        static dep_db_t db;
        static char err[96], rep[1024];
        CHECK(dep_parse(&db, DRIVERS, err, sizeof err) == 0 && db.n == 13 && db.m == 22);
        int rc = plan_report(&db, rep, sizeof rep);
        printf("--- report (drivers) ---\n%s", rep);
        CHECK(rc == 0);
        CHECK(strcmp(rep,
            "OK 13 drivers, 7 waves\n"
            "order: clock gpio dma i2c1 spi1 pmic imu baro mag flash fs fusion logger\n"
            "wave 0: clock\n"
            "wave 1: gpio dma\n"
            "wave 2: i2c1 spi1\n"
            "wave 3: pmic flash\n"
            "wave 4: imu baro mag fs\n"
            "wave 5: fusion\n"
            "wave 6: logger\n") == 0);

        /* 누군가 "I2C 풀업 전원이 PMIC LDO 에서 나온다" 며 i2c1 에 pmic 의존을 추가했다 */
        static char text[1024];
        snprintf(text, sizeof text, "%s", DRIVERS);
        char *p = strstr(text, "i2c1: clock gpio");
        CHECK(p != NULL);
        if (p) { memmove(p + 21, p + 16, strlen(p + 16) + 1); memcpy(p + 16, " pmic", 5); }
        CHECK(dep_parse(&db, text, err, sizeof err) == 0 && db.m == 23);
        rc = plan_report(&db, rep, sizeof rep);
        printf("--- report (i2c1 also needs pmic) ---\n%s", rep);
        CHECK(rc == 1);
        CHECK(strcmp(rep,
            "CYCLE: i2c1 -> pmic -> i2c1\n"
            "init ok (6): clock gpio dma spi1 flash fs\n"
            "blocked (7): i2c1 pmic imu baro mag fusion logger\n") == 0);

        /* 선언되지 않은 의존성 / 형식 오류 / 출력 버퍼 부족 */
        CHECK(dep_parse(&db, "clock:\nimu: clock irqc\n", err, sizeof err) == -1
              && strcmp(err, "line 2: 'imu' needs undeclared 'irqc'") == 0);
        CHECK(dep_parse(&db, "clock:\nclock:\n", err, sizeof err) == -1 && strcmp(err, "line 2: duplicate 'clock'") == 0);
        CHECK(dep_parse(&db, "a: b\nb: a\n", err, sizeof err) == 0 && plan_report(&db, rep, sizeof rep) == 1
              && strcmp(rep, "CYCLE: a -> b -> a\ninit ok (0):\nblocked (2): a b\n") == 0);
        CHECK(dep_parse(&db, "watchdog: watchdog\nled:\n", err, sizeof err) == 0 && plan_report(&db, rep, sizeof rep) == 1
              && strcmp(rep, "CYCLE: watchdog -> watchdog\ninit ok (1): led\nblocked (1): watchdog\n") == 0);
        CHECK(dep_parse(&db, DRIVERS, err, sizeof err) == 0 && plan_report(&db, rep, 40) == -1 && strlen(rep) < 40);
        CHECK(dep_parse(&db, "", err, sizeof err) == 0 && plan_report(&db, rep, sizeof rep) == 0
              && strcmp(rep, "OK 0 drivers, 0 waves\norder:\n") == 0);
    }

    CHECK(g_build_fail == 0);
    printf("sizeof(graph_t)=%zu B, sizeof(dep_db_t)=%zu B\n", sizeof(graph_t), sizeof(dep_db_t));
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
