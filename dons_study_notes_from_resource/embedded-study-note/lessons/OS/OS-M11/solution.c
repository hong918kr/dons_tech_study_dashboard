/* OS-M11 교착 탐지기: 락 순서 그래프 (mini lockdep)
 *
 * 아이디어 (Linux lockdep 의 축소판):
 *   "락 X 를 쥔 채 락 Y 를 잡았다" = 의존 간선 X -> Y. 모든 스레드의 간선을 전역 그래프 하나에 모은다.
 *   새 간선 X -> Y 를 넣을 때 이미 Y ⇝ X 경로가 있으면, 간선이 사이클을 닫는다 = 잠재적 교착.
 *   두 스레드가 실제로 동시에 돌지 않아도(차례로 돌아도) 순서 위반을 잡는다 — 이게 lockdep 의 가치.
 *
 *   Part A  ld_mutex_t (pthread_mutex_t + 락 클래스), _Thread_local 보유 락 스택, 전역 그래프(내부 락으로 보호),
 *           새 간선일 때만 DFS 로 역방향 도달성 검사, 이미 본 간선은 락 없이 건너뛰는 캐시,
 *           사이클이면 두 경로(새 간선 + 기존 사슬)를 간선마다 처음 기록할 때 잡은 호출 스택과 함께 보고.
 *   Part B  trylock(간선을 만들지 않음), 같은 인스턴스 재획득(자기 교착 → 거절), 같은 클래스 중첩(재귀 경고),
 *           subclass 주석(ld_lock_nested), 순서와 다른 해제, 안 쥔 락 해제.
 *   Part C  펌웨어 드라이버: sensor / spi_bus / log 세 락의 3-사이클을 순차 실행만으로 탐지하고 고친다.
 *
 * 체크는 결정적(스레드를 차례로 실행하거나, 같은 순서만 쓰는 스트레스)인 것만 본다. 비용 측정은 demo_bench.c.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 * 데모가 엔진만 가져다 쓸 때: #define LD_NO_MAIN 후 #include "solution.c"
 */
#define _DARWIN_C_SOURCE            /* backtrace() (<execinfo.h>) 선언 */
#define _DEFAULT_SOURCE             /* glibc(Linux)에서 pthread_mutexattr_settype 선언 */
#include <errno.h>
#include <execinfo.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LD_GIVEN __attribute__((unused))   /* 시작 코드가 아직 안 부르는 도우미의 -Wunused 경고 끄기 */

/* ======================= 엔진 (주어짐: 자료구조 · 클래스 · 출력) ======================= */
enum { LD_MAX_CLASSES = 16, LD_MAX_SUB = 4, LD_NODES = LD_MAX_CLASSES * LD_MAX_SUB,
       LD_MAX_EDGES = 256, LD_MAX_HELD = 8, LD_BT = 12 };
enum { LD_OK = 0, LD_BUSY, LD_CYCLE, LD_RECURSIVE, LD_SELF, LD_NOT_HELD, LD_TOO_DEEP };

typedef struct { pthread_mutex_t m; int cls; } ld_mutex_t;            /* 인스턴스. cls = 클래스 id */
typedef struct { ld_mutex_t *m; int node; bool trylock; const char *site; } ld_held_t;
typedef struct {                       /* 간선 from -> to 를 "처음" 본 순간의 기록 */
    int from, to;
    const char *from_site, *to_site, *thread;
    int nbt; void *bt[LD_BT];
} ld_edge_t;
typedef struct { int kind, len; int node[LD_NODES + 1]; const char *thread, *site; } ld_report_t;

static struct {
    pthread_mutex_t lock;              /* 내부 락: 아래 전부를 보호. 이 락은 절대 추적하지 않는다 */
    int nclasses; const char *cname[LD_MAX_CLASSES];
    int16_t eidx[LD_NODES][LD_NODES];  /* 0 = 간선 없음, k = edges[k-1] */
    int nedges; ld_edge_t edges[LD_MAX_EDGES];
    int ncycle, nrecursive, nself, nbad; bool full;
    ld_report_t last;                  /* 가장 최근 보고 (테스트가 읽음) */
    bool verbose;                      /* true 면 보고를 stdout 에 찍는다 */
} G = { .lock = PTHREAD_MUTEX_INITIALIZER };
static _Atomic(unsigned char) g_known[LD_NODES][LD_NODES];  /* 빠른 길: 이미 처리한 간선 (락 없이 읽음) */

LD_GIVEN static _Thread_local ld_held_t t_held[LD_MAX_HELD];          /* 이 스레드가 쥔 락, 잡은 순서대로 */
static _Thread_local int t_nheld;
static _Thread_local const char *t_name = "main";

static void ld_set_thread_name(const char *n) { t_name = n; }
static int  ld_held_count(void) { return t_nheld; }

static int ld_class(const char *name) {                     /* 클래스 이름 → id (같은 이름 = 같은 클래스) */
    pthread_mutex_lock(&G.lock);
    int id = -1;
    for (int i = 0; i < G.nclasses; i++)
        if (strcmp(G.cname[i], name) == 0) { id = i; break; }
    if (id < 0 && G.nclasses < LD_MAX_CLASSES) { id = G.nclasses++; G.cname[id] = name; }
    pthread_mutex_unlock(&G.lock);
    return id;
}
static void ld_mutex_init(ld_mutex_t *m, const char *class_name) {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_ERRORCHECK);  /* 실수로 진짜 재획득해도 멈추지 않게 */
    pthread_mutex_init(&m->m, &a);
    pthread_mutexattr_destroy(&a);
    m->cls = ld_class(class_name);
    if (m->cls < 0) { fprintf(stderr, "mini-lockdep: too many classes\n"); abort(); }
}
static void ld_mutex_destroy(ld_mutex_t *m) { pthread_mutex_destroy(&m->m); }

static const char *node_name(int node, char buf[48]) {        /* "spi_bus" 또는 "inode/1" */
    int c = node / LD_MAX_SUB, s = node % LD_MAX_SUB;
    if (s == 0) snprintf(buf, 48, "%s", G.cname[c]);
    else        snprintf(buf, 48, "%s/%d", G.cname[c], s);
    return buf;
}
static void cycle_str(const ld_report_t *r, char *out, size_t n) {   /* "B -> A -> B" */
    size_t k = 0; out[0] = '\0';
    for (int i = 0; i < r->len && k < n; i++) {
        char b[48];
        k += (size_t)snprintf(out + k, n - k, "%s%s", i ? " -> " : "", node_name(r->node[i], b));
    }
}
static void print_edge(const ld_edge_t *e, const char *tag) {        /* 호출 스택 포함 (G.lock 보유 중) */
    char a[48], b[48];
    printf("  %s %s -> %s   thread %s: holding '%s' (taken at %s), acquiring '%s' at %s\n", tag,
           node_name(e->from, a), node_name(e->to, b), e->thread, a, e->from_site, b, e->to_site);
    char **sym = backtrace_symbols(e->bt, e->nbt);
    for (int i = 0; sym && i < e->nbt; i++) printf("      %s\n", sym[i]);
    free(sym);
}
LD_GIVEN static void print_cycle(const ld_report_t *r) {
    char s[256];
    cycle_str(r, s, sizeof s);
    printf("==== mini-lockdep: possible circular locking dependency (%d classes) ====\n", r->len - 1);
    printf("cycle: %s\n", s);
    printf("[1] new dependency, recorded just now:\n");
    print_edge(&G.edges[G.eidx[r->node[0]][r->node[1]] - 1], "NEW");
    printf("[2] existing chain that closes the cycle (each edge's stack = when it was FIRST seen):\n");
    for (int i = 1; i + 1 < r->len; i++)
        print_edge(&G.edges[G.eidx[r->node[i]][r->node[i + 1]] - 1], "OLD");
    printf("==== reported at edge insert, BEFORE the real pthread_mutex_lock: order bug, not (yet) a hang ====\n");
}
LD_GIVEN static void report_simple(int kind, int node, const char *site) {
    static const char *what[] = { "", "", "", "possible recursive locking (same class held)",
                                  "self-deadlock refused (same instance already held)",
                                  "unlock of a lock that is not held" };
    pthread_mutex_lock(&G.lock);
    if (kind == LD_RECURSIVE) G.nrecursive++;
    else if (kind == LD_SELF) G.nself++;
    else G.nbad++;
    G.last = (ld_report_t){ .kind = kind, .len = 1, .node = { node }, .thread = t_name, .site = site };
    if (G.verbose) { char b[48]; printf("mini-lockdep: %s: '%s' in thread %s at %s\n",
                                        what[kind], node_name(node, b), t_name, site); }
    pthread_mutex_unlock(&G.lock);
}
static void ld_reset(void) {           /* 테스트 사이에 그래프만 비운다 (클래스는 유지). 다른 스레드가 없을 때만 */
    pthread_mutex_lock(&G.lock);
    memset(G.eidx, 0, sizeof G.eidx);
    G.nedges = G.ncycle = G.nrecursive = G.nself = G.nbad = 0; G.full = false;
    memset(&G.last, 0, sizeof G.last);
    for (int i = 0; i < LD_NODES; i++)
        for (int j = 0; j < LD_NODES; j++) atomic_store_explicit(&g_known[i][j], 0, memory_order_relaxed);
    pthread_mutex_unlock(&G.lock);
}

/* ======================= Part A — 그래프와 lock/unlock ======================= */

/* A1: from ⇝ to 경로를 반복(iterative) DFS 로 찾는다. 있으면 path[0]=from … path[n-1]=to 를 채우고 n,
 *     없으면 0. 호출자가 G.lock 을 쥐고 있다. 재귀 금지 — 명시적 스택 st[] + 노드마다 "다음에 볼 이웃" 커서 it[].
 *     st[0..sp-1] 이 언제나 from 에서 지금 노드까지의 경로라서, to 에 닿는 순간 스택이 곧 답이다. */
static int graph_path(int from, int to, int path[LD_NODES]) {
    int st[LD_NODES], it[LD_NODES], sp = 1;
    bool seen[LD_NODES] = { false };
    st[0] = from; it[0] = 0; seen[from] = true;
    if (from == to) { path[0] = from; return 1; }
    while (sp > 0) {
        int u = st[sp - 1], v = it[sp - 1];
        while (v < LD_NODES && !(G.eidx[u][v] && !seen[v])) v++;    /* 작은 id 부터 (결정적) */
        if (v == LD_NODES) { sp--; continue; }                     /* 이웃 다 봄 → 되돌아감 */
        it[sp - 1] = v + 1;
        seen[v] = true; st[sp] = v; it[sp] = 0; sp++;
        if (v == to) { memcpy(path, st, (size_t)sp * sizeof st[0]); return sp; }
    }
    return 0;
}

/* A2: 간선 h->node -> to 를 기록. 이미 본 간선이면 락 없이 바로 LD_OK (g_known, relaxed 로 충분).
 *     처음이면 G.lock 아래에서: to ⇝ from 경로 검사 → 간선 저장(호출 스택 backtrace 포함) →
 *     경로가 있었으면 G.last 에 사이클 [from, to, …, from] 저장, G.ncycle++, verbose 면 print_cycle → LD_CYCLE.
 *     간선 표가 꽉 차면 (G.full) 한 번 경고하고 더 추적하지 않는다. 끝에 g_known 을 1 로. */
LD_GIVEN static int add_edge(const ld_held_t *h, int to, const char *site) {
    int from = h->node;
    if (atomic_load_explicit(&g_known[from][to], memory_order_relaxed)) return LD_OK;
    int rc = LD_OK;
    pthread_mutex_lock(&G.lock);
    if (G.eidx[from][to] == 0) {
        if (G.nedges == LD_MAX_EDGES) {
            if (!G.full) fprintf(stderr, "mini-lockdep: edge table full, tracking off\n");
            G.full = true;
        } else {
            int path[LD_NODES];
            int n = graph_path(to, from, path);           /* 넣기 전에 검사: 이미 to ⇝ from ? */
            ld_edge_t *e = &G.edges[G.nedges++];
            *e = (ld_edge_t){ .from = from, .to = to, .from_site = h->site, .to_site = site, .thread = t_name };
            e->nbt = backtrace(e->bt, LD_BT);
            G.eidx[from][to] = (int16_t)G.nedges;
            if (n > 0) {
                rc = LD_CYCLE; G.ncycle++;
                G.last = (ld_report_t){ .kind = LD_CYCLE, .len = n + 1, .thread = t_name, .site = site };
                G.last.node[0] = from;
                for (int i = 0; i < n; i++) G.last.node[i + 1] = path[i];
                if (G.verbose) print_cycle(&G.last);
            }
        }
    }
    atomic_store_explicit(&g_known[from][to], 1, memory_order_relaxed);
    pthread_mutex_unlock(&G.lock);
    return rc;
}

/* A3: 검사 → 잠금 → 스택에 push. 검사는 진짜 pthread_mutex_lock 보다 "먼저" 한다 (막히기 전에 보고).
 *     node = cls * LD_MAX_SUB + sub.
 *     ① 같은 인스턴스를 이미 쥐고 있으면 report_simple(LD_SELF) 후 잠그지 않고 LD_SELF (진짜 자기 교착).
 *     ② 스택이 꽉 찼으면 LD_TOO_DEEP.
 *     ③ 쥔 락마다: 같은 node(같은 클래스·같은 subclass)면 report_simple(LD_RECURSIVE), 아니면 add_edge.
 *     ④ pthread_mutex_lock, push(trylock=false). 반환: 보고가 있었으면 그 종류(LD_CYCLE 우선), 없으면 LD_OK. */
static int ld_lock_nested(ld_mutex_t *m, int sub, const char *site) {
    int node = m->cls * LD_MAX_SUB + sub, rc = LD_OK;
    for (int i = 0; i < t_nheld; i++)
        if (t_held[i].m == m) { report_simple(LD_SELF, node, site); return LD_SELF; }
    if (t_nheld == LD_MAX_HELD) return LD_TOO_DEEP;
    for (int i = 0; i < t_nheld; i++) {
        if (t_held[i].node == node) {
            report_simple(LD_RECURSIVE, node, site);
            if (rc == LD_OK) rc = LD_RECURSIVE;
        } else if (add_edge(&t_held[i], node, site) == LD_CYCLE) {
            rc = LD_CYCLE;
        }
    }
    pthread_mutex_lock(&m->m);
    t_held[t_nheld++] = (ld_held_t){ .m = m, .node = node, .trylock = false, .site = site };
    return rc;
}
static int ld_lock(ld_mutex_t *m, const char *site) { return ld_lock_nested(m, 0, site); }

/* A4: 스택 어디에 있든 찾아서 빼고(순서와 다른 해제 허용) 푼다. 안 쥔 락이면 report_simple(LD_NOT_HELD),
 *     pthread_mutex_unlock 은 부르지 않고 LD_NOT_HELD. */
static int ld_unlock(ld_mutex_t *m) {
    for (int i = t_nheld - 1; i >= 0; i--) {
        if (t_held[i].m != m) continue;
        memmove(&t_held[i], &t_held[i + 1], (size_t)(t_nheld - 1 - i) * sizeof t_held[0]);
        t_nheld--;
        pthread_mutex_unlock(&m->m);
        return LD_OK;
    }
    report_simple(LD_NOT_HELD, m->cls * LD_MAX_SUB, "unlock");
    return LD_NOT_HELD;
}

/* ======================= Part B — trylock ======================= */

/* B1: pthread_mutex_trylock 이 성공하면 push(trylock=true) 하고 LD_OK, 실패면 LD_BUSY.
 *     간선은 만들지 않는다 — trylock 은 기다리지 않으므로 순환 대기의 고리가 될 수 없다.
 *     (이 락을 쥔 뒤 다른 락을 "잡으면" 그 간선은 A3 가 정상적으로 만든다.) 스택이 꽉 찼으면 LD_TOO_DEEP. */
static int ld_trylock(ld_mutex_t *m, const char *site) {
    if (t_nheld == LD_MAX_HELD) return LD_TOO_DEEP;
    if (pthread_mutex_trylock(&m->m) != 0) return LD_BUSY;
    t_held[t_nheld++] = (ld_held_t){ .m = m, .node = m->cls * LD_MAX_SUB, .trylock = true, .site = site };
    return LD_OK;
}

/* ======================= Part C — 펌웨어 드라이버 (sensor / spi_bus / log) ======================= */
typedef struct { ld_mutex_t lock; uint32_t last, reads; } sensor_t;
typedef struct { ld_mutex_t bus; uint32_t data_reg, crc_err; } spi_t;     /* data_reg = 가짜 SPI DR 레지스터 */
typedef struct { ld_mutex_t lock; char buf[128]; size_t len; } logbuf_t;
static sensor_t g_sensor; static spi_t g_spi; static logbuf_t g_log;

static uint32_t spi_xfer(uint32_t cmd) { g_spi.data_reg = cmd * 3u + 1u; return g_spi.data_reg; }  /* bus 보유 중 */
static void log_event(const char *msg) {
    ld_lock(&g_log.lock, "log_event");
    int k = snprintf(g_log.buf + g_log.len, sizeof g_log.buf - g_log.len, "%s\n", msg);
    if (k > 0) g_log.len += (size_t)k;
    ld_unlock(&g_log.lock);
}
static void *sensor_task(void *arg) {                  /* 주기 태스크: sensor -> spi_bus */
    (void)arg; ld_set_thread_name("sensor_task");
    ld_lock(&g_sensor.lock, "sensor_task");
    ld_lock(&g_spi.bus, "sensor_task:spi_xfer");
    uint32_t v = spi_xfer(0x10);
    ld_unlock(&g_spi.bus);
    g_sensor.last = v; g_sensor.reads++;
    ld_unlock(&g_sensor.lock);
    return NULL;
}
static void *spi_err_worker(void *arg) {               /* SPI 오류 지연 처리: spi_bus -> log */
    (void)arg; ld_set_thread_name("spi_err_worker");
    ld_lock(&g_spi.bus, "spi_err_worker");
    g_spi.crc_err++;
    log_event("spi crc error");
    ld_unlock(&g_spi.bus);
    return NULL;
}
static char g_flush_out[192];
static void *log_flush_buggy(void *arg) {              /* 로그 비우기: log -> sensor  (사이클을 닫는 간선) */
    (void)arg; ld_set_thread_name("log_flush");
    ld_lock(&g_log.lock, "log_flush");
    ld_lock(&g_sensor.lock, "log_flush:sensor_status");
    uint32_t s = g_sensor.last;
    ld_unlock(&g_sensor.lock);
    snprintf(g_flush_out, sizeof g_flush_out, "%s| sensor=%u", g_log.buf, (unsigned)s);
    g_log.len = 0; g_log.buf[0] = '\0';
    ld_unlock(&g_log.lock);
    return NULL;
}
/* C1: 같은 출력(g_flush_out 형식 동일)을 내되 log 를 쥔 채 sensor 를 잡지 않는다.
 *     sensor 값을 먼저 짧게 스냅샷(sensor 락 안에서 복사 → 풀기) 한 뒤 log 락을 잡는다. */
static void *log_flush_fixed(void *arg) {
    (void)arg; ld_set_thread_name("log_flush");
    ld_lock(&g_sensor.lock, "log_flush:snapshot");
    uint32_t s = g_sensor.last;
    ld_unlock(&g_sensor.lock);
    ld_lock(&g_log.lock, "log_flush");
    snprintf(g_flush_out, sizeof g_flush_out, "%s| sensor=%u", g_log.buf, (unsigned)s);
    g_log.len = 0; g_log.buf[0] = '\0';
    ld_unlock(&g_log.lock);
    return NULL;
}

#ifndef LD_NO_MAIN
/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

typedef struct { const char *name; ld_mutex_t *m[4]; const char *site[4]; int n; int rc[4]; } script_t;
static void *run_script(void *arg) {                   /* m[0..n-1] 을 차례로 잡고 거꾸로 푼다 */
    script_t *s = arg;
    ld_set_thread_name(s->name);
    for (int i = 0; i < s->n; i++) s->rc[i] = ld_lock(s->m[i], s->site[i]);
    for (int i = s->n - 1; i >= 0; i--) ld_unlock(s->m[i]);
    return NULL;
}
static void run_thread(void *(*fn)(void *), void *arg) {   /* 스레드 하나를 돌리고 끝날 때까지 기다림 = 차례로 실행 */
    pthread_t t;
    pthread_create(&t, NULL, fn, arg);
    pthread_join(t, NULL);
}
static bool has_edge(ld_mutex_t *a, int sa, ld_mutex_t *b, int sb) {
    return G.eidx[a->cls * LD_MAX_SUB + sa][b->cls * LD_MAX_SUB + sb] != 0;
}
static bool last_cycle_is(const char *want) {
    char s[256]; cycle_str(&G.last, s, sizeof s);
    if (strcmp(s, want) != 0) printf("     got cycle \"%s\"\n", s);
    return G.last.kind == LD_CYCLE && strcmp(s, want) == 0;
}

static ld_mutex_t A, B, C, D, I1, I2;
static long g_counter;
enum { ST_THREADS = 4, ST_ITERS = 20000 };
static void *stress_body(void *arg) {                  /* 모두 같은 순서 A -> B -> C : 사이클 없음 */
    (void)arg; ld_set_thread_name("stress");
    for (int i = 0; i < ST_ITERS; i++) {
        ld_lock(&A, "stress:A"); ld_lock(&B, "stress:B"); ld_lock(&C, "stress:C");
        g_counter++;
        ld_unlock(&C); ld_unlock(&B); ld_unlock(&A);
    }
    return NULL;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    ld_mutex_init(&A, "A"); ld_mutex_init(&B, "B"); ld_mutex_init(&C, "C"); ld_mutex_init(&D, "D");
    ld_mutex_init(&I1, "inode"); ld_mutex_init(&I2, "inode");

    printf("-- Part A: classes, graph search, ABBA found without a real deadlock\n");
    CHECK(A.cls != B.cls && I1.cls == I2.cls);
    {   /* 손으로 만든 그래프: A->B, B->C */
        ld_reset();
        int a = A.cls * LD_MAX_SUB, b = B.cls * LD_MAX_SUB, c = C.cls * LD_MAX_SUB, path[LD_NODES];
        G.eidx[a][b] = 1; G.eidx[b][c] = 2;
        int n = graph_path(a, c, path);
        CHECK(n == 3 && path[0] == a && path[1] == b && path[2] == c);
        CHECK(graph_path(c, a, path) == 0);
        CHECK(graph_path(b, b, path) == 1);
    }
    ld_reset();
    CHECK(ld_lock(&A, "main:A") == LD_OK && ld_lock(&B, "main:B") == LD_OK && ld_held_count() == 2);
    CHECK(ld_unlock(&B) == LD_OK && ld_unlock(&A) == LD_OK && ld_held_count() == 0);
    CHECK(G.nedges == 1 && has_edge(&A, 0, &B, 0) && G.ncycle == 0);

    ld_reset();
    script_t t1 = { "T1", { &A, &B }, { "t1:A", "t1:B" }, 2, { -1, -1 } };
    script_t t2 = { "T2", { &B, &A }, { "t2:B", "t2:A" }, 2, { -1, -1 } };
    run_thread(run_script, &t1);                        /* T1 이 완전히 끝난 "뒤에" T2 가 시작한다 */
    run_thread(run_script, &t2);
    if (G.last.kind == LD_CYCLE) print_cycle(&G.last);  /* 보고 전문 (호출 스택 포함) */
    CHECK(t1.rc[0] == LD_OK && t1.rc[1] == LD_OK && t2.rc[0] == LD_OK && t2.rc[1] == LD_CYCLE);
    CHECK(G.ncycle == 1 && last_cycle_is("B -> A -> B"));
    {
        int k = G.eidx[A.cls * LD_MAX_SUB][B.cls * LD_MAX_SUB];   /* 간선 A->B 를 처음 본 기록 */
        const ld_edge_t *old = k ? &G.edges[k - 1] : NULL;
        CHECK(old && strcmp(old->thread, "T1") == 0 && strcmp(old->to_site, "t1:B") == 0 && old->nbt > 0);
    }
    script_t t2b = { "T2b", { &B, &A }, { "t2:B", "t2:A" }, 2, { -1, -1 } };
    run_thread(run_script, &t2b);                       /* 같은 위반 반복 → 이미 아는 간선: 보고 1 번뿐 */
    CHECK(t2b.rc[1] == LD_OK && G.ncycle == 1 && G.nedges == 2);

    ld_reset();                                         /* 3-사이클: A->B, B->C, C->A 를 세 스레드가 차례로 */
    script_t u1 = { "U1", { &A, &B }, { "u1:A", "u1:B" }, 2, { -1, -1 } };
    script_t u2 = { "U2", { &B, &C }, { "u2:B", "u2:C" }, 2, { -1, -1 } };
    script_t u3 = { "U3", { &C, &A }, { "u3:C", "u3:A" }, 2, { -1, -1 } };
    run_thread(run_script, &u1); run_thread(run_script, &u2); run_thread(run_script, &u3);
    CHECK(u1.rc[1] == LD_OK && u2.rc[1] == LD_OK && u3.rc[1] == LD_CYCLE);
    CHECK(G.ncycle == 1 && last_cycle_is("C -> A -> B -> C"));
    script_t u4 = { "U4", { &A, &D, &C }, { "u4:A", "u4:D", "u4:C" }, 3, { -1, -1, -1 } };
    run_thread(run_script, &u4);                        /* A->D, D->C 는 새 간선, A->C 도 새 간선 → C ⇝ A 있음 */
    CHECK(u4.rc[1] == LD_OK && u4.rc[2] == LD_CYCLE && G.ncycle == 3);

    printf("-- Part B: trylock, self/recursive, subclass, out-of-order release, bad unlock\n");
    ld_reset();
    CHECK(ld_lock(&A, "b:A") == LD_OK && ld_trylock(&B, "b:tryB") == LD_OK && ld_held_count() == 2);
    CHECK(G.nedges == 0);                               /* A 를 쥔 채 trylock(B): 간선 없음 */
    CHECK(ld_lock(&C, "b:C") == LD_OK && has_edge(&A, 0, &C, 0) && has_edge(&B, 0, &C, 0));
    ld_unlock(&C); ld_unlock(&B); ld_unlock(&A);
    script_t tb = { "TB", { &B, &A }, { "tb:B", "tb:A" }, 2, { -1, -1 } };
    run_thread(run_script, &tb);                        /* B -> A: 기존 A ⇝ B 경로가 없으므로 사이클 아님 */
    CHECK(tb.rc[1] == LD_OK && G.ncycle == 0);
    CHECK(ld_lock(&A, "b:A2") == LD_OK && ld_trylock(&A, "b:tryA") == LD_BUSY && ld_held_count() == 1);
    CHECK(ld_lock(&A, "b:A3") == LD_SELF && G.nself == 1 && ld_held_count() == 1);   /* 멈추지 않고 거절 */
    CHECK(ld_unlock(&A) == LD_OK && ld_held_count() == 0);

    ld_reset();
    CHECK(ld_lock(&I1, "b:inode1") == LD_OK && ld_lock(&I2, "b:inode2") == LD_RECURSIVE && G.nrecursive == 1);
    ld_unlock(&I2); ld_unlock(&I1);
    CHECK(ld_lock(&I1, "b:parent") == LD_OK && ld_lock_nested(&I2, 1, "b:child") == LD_OK);
    CHECK(G.nrecursive == 1 && has_edge(&I1, 0, &I2, 1));
    ld_unlock(&I2); ld_unlock(&I1);

    ld_reset();                                         /* 순서와 다른 해제: A, B 를 잡고 A 먼저 푼 뒤 C */
    ld_lock(&A, "o:A"); ld_lock(&B, "o:B");
    CHECK(ld_unlock(&A) == LD_OK && ld_held_count() == 1);
    CHECK(ld_lock(&C, "o:C") == LD_OK && has_edge(&B, 0, &C, 0) && !has_edge(&A, 0, &C, 0));
    ld_unlock(&C); ld_unlock(&B);
    CHECK(ld_unlock(&D) == LD_NOT_HELD && G.nbad == 1 && ld_held_count() == 0);

    ld_reset();                                         /* 멀티스레드 스트레스: 순서가 같으면 보고 0 */
    g_counter = 0;
    pthread_t th[ST_THREADS];
    for (int i = 0; i < ST_THREADS; i++) pthread_create(&th[i], NULL, stress_body, NULL);
    for (int i = 0; i < ST_THREADS; i++) pthread_join(th[i], NULL);
    CHECK(g_counter == (long)ST_THREADS * ST_ITERS);
    CHECK(G.nedges == 3 && G.ncycle == 0 && G.nrecursive == 0 && G.nbad == 0);

    printf("-- Part C: firmware driver sensor / spi_bus / log\n");
    ld_mutex_init(&g_sensor.lock, "sensor"); ld_mutex_init(&g_spi.bus, "spi_bus"); ld_mutex_init(&g_log.lock, "log");
    ld_reset();
    run_thread(sensor_task, NULL); run_thread(spi_err_worker, NULL); run_thread(log_flush_buggy, NULL);
    CHECK(G.ncycle == 1 && last_cycle_is("log -> sensor -> spi_bus -> log"));
    CHECK(strcmp(g_flush_out, "spi crc error\n| sensor=49") == 0);   /* 기능은 멀쩡 = 테스트로는 안 보이는 버그 */
    ld_reset();
    g_sensor.last = 0; g_log.len = 0; g_log.buf[0] = '\0'; g_flush_out[0] = '\0';
    run_thread(sensor_task, NULL); run_thread(spi_err_worker, NULL); run_thread(log_flush_fixed, NULL);
    CHECK(G.ncycle == 0 && G.nedges == 2);
    CHECK(strcmp(g_flush_out, "spi crc error\n| sensor=49") == 0);

    ld_mutex_destroy(&A); ld_mutex_destroy(&B); ld_mutex_destroy(&C); ld_mutex_destroy(&D);
    ld_mutex_destroy(&I1); ld_mutex_destroy(&I2);
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
#endif
