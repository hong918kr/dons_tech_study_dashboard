/* RTOS-M05 추가 연습 — 한 번에 검증
 *
 * solution.c 의 tick 기반 미니 커널을 그대로 가져와 네 가지를 더한다.
 *  연습 1: OPCP (원래의 Priority Ceiling Protocol, Sha·Rajkumar·Lehoczky 1990) — "시스템 천장" 규칙
 *  연습 2: 대기 그래프(wait-for graph) 사이클 찾기 → "H>A>L>B>H" 같은 교착 경로 문자열
 *  연습 3: 중첩 IPCP 에서 "잡을 때 저장한 우선순위로 복원" 하는 버그 (LIFO 가 아닌 해제 순서)
 *  연습 4: 같은 우선순위 time slicing 이 IPCP 의 "LOCK 은 절대 안 막힌다" 를 깨는 경우와 천장 +1 처방
 * 실행 인자 -v → 타임라인 출력.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MAX_T    6
#define MAX_M    4
#define MAX_OPS  24
#define MAX_TICK 64

enum { OP_END, OP_RUN, OP_LOCK, OP_UNLOCK };
typedef struct { unsigned char op, arg; } op_t;
#define RUN(n)    { OP_RUN, (n) }
#define LOCK(m)   { OP_LOCK, (m) }
#define UNLOCK(m) { OP_UNLOCK, (m) }
#define END       { OP_END, 0 }

enum { PROTO_NONE, PROTO_PI, PROTO_IPCP, PROTO_OPCP };
enum { T_UNREL, T_READY, T_BLOCKED, T_DONE };
enum { K_OK = 0, K_BLOCKED = 1, K_EINVAL = 22 };
enum { SIM_RUNNING, SIM_DONE, SIM_DEADLOCK, SIM_TIMEOUT };

typedef struct {
    char name;
    int base, eff, release;
    op_t ops[MAX_OPS];
    int pc, run_left, state, wait_m, err;
    bool retry;                        /* OPCP: 천장 규칙으로 막힘 (원하는 뮤텍스는 ops[pc].arg) */
    unsigned seq;
} task_t;
typedef struct { int owner, ceiling; } kmutex_t;

typedef struct {
    int proto, nt, nm, now, cur, status, deadlock_tick;
    int blocking_locks, lock_errors;
    bool ceil_manual, timeslice;       /* timeslice: 같은 유효 우선순위끼리 매 tick 돌아가며 (연습 4) */
    unsigned seqctr;
    task_t t[MAX_T];
    kmutex_t m[MAX_M];
    char tl[MAX_T][MAX_TICK + 1];
} kernel_t;

static void k_init(kernel_t *k, int proto, int nm) {
    memset(k, 0, sizeof *k);
    k->proto = proto; k->nm = nm; k->cur = -1; k->deadlock_tick = -1;
    for (int i = 0; i < MAX_M; i++) { k->m[i].owner = -1; k->m[i].ceiling = -1; }
}
static int k_add(kernel_t *k, char name, int prio, int release, const op_t *ops) {
    task_t *p = &k->t[k->nt];
    p->name = name; p->base = p->eff = prio; p->release = release;
    for (int i = 0; i < MAX_OPS - 1 && ops[i].op != OP_END; i++) p->ops[i] = ops[i];
    p->state = T_UNREL; p->wait_m = -1;
    return k->nt++;
}

static void compute_ceilings(kernel_t *k) {
    for (int m = 0; m < k->nm; m++) k->m[m].ceiling = 0;
    for (int t = 0; t < k->nt; t++)
        for (int pc = 0; pc < MAX_OPS && k->t[t].ops[pc].op != OP_END; pc++)
            if (k->t[t].ops[pc].op == OP_LOCK) {
                kmutex_t *mx = &k->m[k->t[t].ops[pc].arg];
                if (k->t[t].base > mx->ceiling) mx->ceiling = k->t[t].base;
            }
}
static int ipcp_eff_prio(const kernel_t *k, int t) {
    int e = k->t[t].base;
    for (int m = 0; m < k->nm; m++)
        if (k->m[m].owner == t && k->m[m].ceiling > e) e = k->m[m].ceiling;
    return e;
}
static bool sim_deadlocked(const kernel_t *k) {
    int blocked = 0;
    for (int i = 0; i < k->nt; i++) {
        if (k->t[i].state == T_READY || k->t[i].state == T_UNREL) return false;
        if (k->t[i].state == T_BLOCKED) blocked++;
    }
    return blocked > 0;
}

static void recompute(kernel_t *k) {
    for (int i = 0; i < k->nt; i++)
        k->t[i].eff = (k->proto == PROTO_IPCP) ? ipcp_eff_prio(k, i) : k->t[i].base;
    if (k->proto != PROTO_PI && k->proto != PROTO_OPCP) return;   /* OPCP 도 막힌 쪽 → 막은 쪽 상속 */
    for (bool changed = true; changed;) {
        changed = false;
        for (int w = 0; w < k->nt; w++) {
            if (k->t[w].state != T_BLOCKED) continue;
            int o = k->m[k->t[w].wait_m].owner;
            if (o >= 0 && k->t[o].eff < k->t[w].eff) { k->t[o].eff = k->t[w].eff; changed = true; }
        }
    }
}

static int pick(const kernel_t *k) {
    int best = -1;
    for (int i = 0; i < k->nt; i++) {
        if (k->t[i].state != T_READY) continue;
        if (best < 0) { best = i; continue; }
        const task_t *a = &k->t[i], *b = &k->t[best];
        if (a->eff != b->eff) { if (a->eff > b->eff) best = i; continue; }
        if (k->timeslice) { if (a->seq < b->seq) best = i; continue; }   /* 순수 FIFO 회전 */
        if (best == k->cur) continue;
        if (i == k->cur || a->seq < b->seq) best = i;
    }
    return best;
}

/* ---- 연습 1: OPCP 잠금 규칙 ----
 * 다른 태스크가 잡고 있는 뮤텍스들의 천장 중 최댓값 = 시스템 천장 S*.
 * eff(t) > S* 일 때만 잡을 수 있다 (원하는 뮤텍스가 비어 있어도!). 아니면 S* 를 만든 뮤텍스에 막히고,
 * 그 소유자가 t 의 우선순위를 상속한다. unlock 으로 S* 가 내려가 규칙을 통과하게 되면 그때 넘겨받는다. */
static int opcp_blocker(const kernel_t *k, int t) {
    int s = -1;
    for (int m = 0; m < k->nm; m++)
        if (k->m[m].owner >= 0 && k->m[m].owner != t && (s < 0 || k->m[m].ceiling > k->m[s].ceiling)) s = m;
    if (s >= 0 && k->t[t].eff > k->m[s].ceiling) s = -1;
    return s;   /* -1 = 잡아도 됨 */
}

static int k_lock(kernel_t *k, int t, int m) {
    if (k->proto == PROTO_IPCP && k->t[t].base > k->m[m].ceiling) return K_EINVAL;
    if (k->proto == PROTO_OPCP) {
        int s = opcp_blocker(k, t);
        if (s >= 0) {
            k->blocking_locks++;
            k->t[t].state = T_BLOCKED; k->t[t].wait_m = s; k->t[t].retry = true; k->t[t].seq = ++k->seqctr;
            recompute(k);
            return K_BLOCKED;
        }
    }
    if (k->m[m].owner < 0) { k->m[m].owner = t; recompute(k); return K_OK; }
    k->blocking_locks++;
    k->t[t].state = T_BLOCKED; k->t[t].wait_m = m; k->t[t].seq = ++k->seqctr;
    recompute(k);
    return K_BLOCKED;
}
static void k_unlock(kernel_t *k, int t, int m) {
    if (k->m[m].owner != t) { k->lock_errors++; return; }
    int w = -1;
    for (int i = 0; i < k->nt; i++) {
        if (k->t[i].state != T_BLOCKED || k->t[i].wait_m != m || k->t[i].retry) continue;
        if (w < 0 || k->t[i].eff > k->t[w].eff || (k->t[i].eff == k->t[w].eff && k->t[i].seq < k->t[w].seq)) w = i;
    }
    k->m[m].owner = w;
    if (w >= 0) { k->t[w].state = T_READY; k->t[w].wait_m = -1; k->t[w].pc++; k->t[w].seq = ++k->seqctr; }
    recompute(k);
    for (bool again = true; again;) {   /* OPCP: 시스템 천장이 내려가 규칙을 통과하면 높은 순서로 바로 넘겨준다 */
        again = false;
        int g = -1;
        for (int i = 0; i < k->nt; i++) {
            if (k->t[i].state != T_BLOCKED || !k->t[i].retry) continue;
            if (opcp_blocker(k, i) >= 0 || k->m[k->t[i].ops[k->t[i].pc].arg].owner >= 0) continue;
            if (g < 0 || k->t[i].eff > k->t[g].eff) g = i;
        }
        if (g >= 0) {
            k->m[k->t[g].ops[k->t[g].pc].arg].owner = g;
            k->t[g].state = T_READY; k->t[g].wait_m = -1; k->t[g].retry = false; k->t[g].pc++;
            k->t[g].seq = ++k->seqctr;
            recompute(k);
            again = true;
        }
    }
}

static void sim_tick(kernel_t *k) {
    int ran = -1;
    for (int i = 0; i < k->nt; i++)
        if (k->t[i].state == T_UNREL && k->t[i].release == k->now) {
            k->t[i].state = T_READY; k->t[i].seq = ++k->seqctr;
        }
    for (int guard = 0; guard < MAX_T * MAX_OPS && ran < 0; guard++) {
        int c = pick(k);
        if (c < 0) break;
        k->cur = c;
        task_t *p = &k->t[c];
        op_t o = p->ops[p->pc];
        switch (o.op) {
        case OP_RUN:
            if (p->run_left == 0) p->run_left = o.arg;
            if (--p->run_left == 0) p->pc++;
            ran = c;
            break;
        case OP_LOCK: {                                      /* 0 tick: 획득하면 바로 다음 op, 막히면 다시 pick */
            int r = k_lock(k, c, o.arg);
            if (r == K_OK) p->pc++;
            else if (r == K_EINVAL) { p->err = r; p->state = T_DONE; k->lock_errors++; }
            break;
        }
        case OP_UNLOCK: k_unlock(k, c, o.arg); p->pc++; break;
        default: p->state = T_DONE; break;
        }
    }
    if (ran >= 0 && k->timeslice && k->t[ran].state == T_READY) k->t[ran].seq = ++k->seqctr;  /* 줄 뒤로 */
    if (ran >= 0 && k->t[ran].run_left == 0 && k->t[ran].ops[k->t[ran].pc].op == OP_END)
        k->t[ran].state = T_DONE;
    for (int i = 0; i < k->nt; i++) {
        char ch = '.';
        if (i == ran) ch = 'R';
        else if (k->t[i].state == T_READY) ch = 'r';
        else if (k->t[i].state == T_BLOCKED) ch = 'B';
        k->tl[i][k->now] = ch;
    }
    if (ran < 0 && sim_deadlocked(k)) { k->status = SIM_DEADLOCK; k->deadlock_tick = k->now; }
    k->now++;
}
static int sim_run(kernel_t *k) {
    if (!k->ceil_manual) compute_ceilings(k);
    recompute(k);
    while (k->status == SIM_RUNNING) {
        if (k->now >= MAX_TICK) { k->status = SIM_TIMEOUT; break; }
        sim_tick(k);
        bool all = true;
        for (int i = 0; i < k->nt; i++) if (k->t[i].state != T_DONE) all = false;
        if (all && k->status == SIM_RUNNING) k->status = SIM_DONE;
    }
    return k->status;
}

static int count_ch(const kernel_t *k, int t, char c) {
    int n = 0;
    for (int x = 0; x < k->now; x++) n += (k->tl[t][x] == c);
    return n;
}
static int wait_ticks(const kernel_t *k, int t) { return count_ch(k, t, 'B') + count_ch(k, t, 'r'); }
/* 지연(delay) = 응답 시간 − 자기 일(RUN 합; LOCK/UNLOCK 은 0 tick).
 * 가장 높은 태스크에서는 B + r 개수와 같아야 한다 (아래에서 검사). */
static int delay_ticks(const kernel_t *k, int t) {
    int last = -1, own = 0;
    for (int x = 0; x < k->now; x++) if (k->tl[t][x] == 'R') last = x;
    for (int pc = 0; pc < MAX_OPS && k->t[t].ops[pc].op != OP_END; pc++)
        if (k->t[t].ops[pc].op == OP_RUN) own += k->t[t].ops[pc].arg;
    return last + 1 - k->t[t].release - own;
}
static bool row_is(const kernel_t *k, int t, const char *s) { return strcmp(k->tl[t], s) == 0; }

static const char *proto_name(int p) {
    return p == PROTO_NONE ? "plain" : p == PROTO_PI ? "PI" : p == PROTO_IPCP ? "IPCP" : "OPCP";
}
static void print_timeline(const kernel_t *k, const char *title) {
    printf("%s  [%s%s]\n", title, proto_name(k->proto), k->timeslice ? " + time slicing" : "");
    printf("tick :");
    for (int x = 0; x < k->now; x++) printf(" %d", x % 10);
    printf("\n");
    for (int i = 0; i < k->nt; i++) {
        printf("%c    :", k->t[i].name);
        for (int x = 0; x < k->now; x++) printf(" %c", k->tl[i][x]);
        printf("\n");
    }
    if (k->status == SIM_DEADLOCK) printf("-> DEADLOCK at tick %d (all tasks blocked)\n", k->deadlock_tick);
    else printf("-> %s after %d ticks, blocking locks=%d\n", k->status == SIM_DONE ? "DONE" : "TIMEOUT",
                k->now, k->blocking_locks);
}

/* ---- 연습 2: 대기 그래프 사이클 ----
 * 막힌 태스크 → 기다리는 뮤텍스 → 그 소유자 … 를 따라가 처음 태스크로 돌아오면 사이클.
 * 출력 예: "H>A>L>B>H". 사이클이 없으면 빈 문자열, 반환값 = 사이클의 태스크 수. */
static int find_cycle(const kernel_t *k, char *out, size_t cap) {
    out[0] = '\0';
    for (int s = 0; s < k->nt; s++) {
        if (k->t[s].state != T_BLOCKED) continue;
        int t = s, n = 0;
        size_t len = 0;
        char buf[64];
        while (n <= k->nt) {
            int m = k->t[t].wait_m, o = k->m[m].owner;
            len += (size_t)snprintf(buf + len, sizeof buf - len, "%c>%c>", k->t[t].name, 'A' + m);
            n++;
            if (o < 0 || k->t[o].state != T_BLOCKED) break;   /* 체인이 끝남: 사이클 아님 */
            if (o == s) {
                snprintf(buf + len, sizeof buf - len, "%c", k->t[s].name);
                snprintf(out, cap, "%s", buf);
                return n;
            }
            t = o;
        }
    }
    return 0;
}

/* ---- 연습 3: "저장해 둔 우선순위로 복원" (naive) vs "보유 중인 것으로 다시 계산" ---- */
typedef struct { int base, eff, saved[MAX_M]; } naive_t;
static void naive_lock(naive_t *n, int m, int ceil) { n->saved[m] = n->eff; if (ceil > n->eff) n->eff = ceil; }
static void naive_unlock(naive_t *n, int m) { n->eff = n->saved[m]; }

/* ---------- 시나리오 (solution.c 와 같은 숫자) ---------- */
enum { R = 0 };
enum { A = 0, B = 1, C = 2 };
enum { H, M, L };
enum { DH, DL };
static void build_canonical(kernel_t *k, int proto, int cm, int h_release) {
    k_init(k, proto, 1);
    k_add(k, 'H', 3, h_release, (const op_t[]){ LOCK(R), RUN(1), UNLOCK(R), RUN(1), END });
    k_add(k, 'M', 2, 3, (const op_t[]){ RUN(cm), END });
    k_add(k, 'L', 1, 0, (const op_t[]){ RUN(1), LOCK(R), RUN(4), UNLOCK(R), RUN(1), END });
}
static void build_deadlock(kernel_t *k, int proto) {
    k_init(k, proto, 2);
    k_add(k, 'H', 3, 2, (const op_t[]){ LOCK(B), RUN(1), LOCK(A), RUN(1), UNLOCK(A), UNLOCK(B), RUN(1), END });
    k_add(k, 'L', 1, 0, (const op_t[]){ RUN(1), LOCK(A), RUN(2), LOCK(B), RUN(1), UNLOCK(B), UNLOCK(A), RUN(1), END });
}
/* 3자 고리: X: A→B, Y: B→C, Z: C→A (낮은 것부터 출시해 각자 첫 뮤텍스를 잡은 뒤 선점당한다) */
static void build_ring(kernel_t *k, int proto) {
    k_init(k, proto, 3);
    k_add(k, 'X', 1, 0, (const op_t[]){ LOCK(A), RUN(2), LOCK(B), RUN(1), UNLOCK(B), UNLOCK(A), END });
    k_add(k, 'Y', 2, 1, (const op_t[]){ LOCK(B), RUN(2), LOCK(C), RUN(1), UNLOCK(C), UNLOCK(B), END });
    k_add(k, 'Z', 3, 2, (const op_t[]){ LOCK(C), RUN(1), LOCK(A), RUN(1), UNLOCK(A), UNLOCK(C), END });
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(int argc, char **argv) {
    bool v = argc > 1 && strcmp(argv[1], "-v") == 0;
    static kernel_t k;
    char cyc[64];

    /* ---- 연습 1: OPCP ---- */
    build_deadlock(&k, PROTO_OPCP); sim_run(&k);
    if (v) print_timeline(&k, "ABBA");
    CHECK(k.status == SIM_DONE);
    CHECK(row_is(&k, DH, "..BBRRR."));            /* t2: B 는 비어 있는데도 막힌다 (천장 블로킹) */
    CHECK(row_is(&k, DL, "RRRRrrrR"));            /* L 이 H 의 3 을 상속해 A·B 를 마저 끝낸다 */
    build_canonical(&k, PROTO_OPCP, 4, 2); sim_run(&k);
    if (v) print_timeline(&k, "canonical C_M=4");
    CHECK(k.status == SIM_DONE && delay_ticks(&k, H) == 3);   /* 상한은 IPCP 와 같다 */
    for (int p = PROTO_NONE; p <= PROTO_OPCP; p++) {          /* 네 프로토콜 모두 지연 == B + r */
        build_canonical(&k, p, 4, 2); sim_run(&k);
        CHECK(delay_ticks(&k, H) == wait_ticks(&k, H));
    }
    int md[4];                                                /* H 가 아직 없을 때 M 의 지연 */
    for (int p = PROTO_NONE; p <= PROTO_OPCP; p++) {
        build_canonical(&k, p, 4, 30); sim_run(&k);
        md[p] = delay_ticks(&k, M);
        if (v && (p == PROTO_IPCP || p == PROTO_OPCP)) print_timeline(&k, "H released late (t30)");
    }
    CHECK(md[PROTO_NONE] == 0 && md[PROTO_PI] == 0 && md[PROTO_OPCP] == 0);
    CHECK(md[PROTO_IPCP] == 2);                               /* IPCP 만: 쓰지도 않는 R 의 천장에 막힘 */

    /* ---- 연습 2: 대기 그래프 사이클 ---- */
    build_deadlock(&k, PROTO_NONE); sim_run(&k);
    CHECK(find_cycle(&k, cyc, sizeof cyc) == 2 && strcmp(cyc, "H>A>L>B>H") == 0);
    build_deadlock(&k, PROTO_PI); sim_run(&k);
    CHECK(find_cycle(&k, cyc, sizeof cyc) == 2);
    build_ring(&k, PROTO_PI); sim_run(&k);
    if (v) print_timeline(&k, "ring X:A>B Y:B>C Z:C>A");
    CHECK(k.status == SIM_DEADLOCK && find_cycle(&k, cyc, sizeof cyc) == 3 && strcmp(cyc, "X>B>Y>C>Z>A>X") == 0);
    build_ring(&k, PROTO_IPCP); sim_run(&k);
    CHECK(k.status == SIM_DONE && k.blocking_locks == 0 && find_cycle(&k, cyc, sizeof cyc) == 0 && cyc[0] == '\0');
    build_ring(&k, PROTO_OPCP); sim_run(&k);
    CHECK(k.status == SIM_DONE);

    /* ---- 연습 3: LIFO 가 아닌 해제 순서 ---- */
    naive_t n = { .base = 1, .eff = 1 };
    int ns[4];
    naive_lock(&n, A, 3); ns[0] = n.eff;
    naive_lock(&n, B, 2); ns[1] = n.eff;
    naive_unlock(&n, A);  ns[2] = n.eff;          /* B 를 아직 쥐고 있는데 1 로 떨어진다 */
    naive_unlock(&n, B);  ns[3] = n.eff;          /* 아무것도 안 쥐었는데 3 으로 돌아간다 */
    CHECK(ns[0] == 3 && ns[1] == 3 && ns[2] == 1 && ns[3] == 3);
    k_init(&k, PROTO_IPCP, 2);
    k_add(&k, 'L', 1, 0, (const op_t[]){ END });
    k.m[A].ceiling = 3; k.m[B].ceiling = 2;
    int cs[4];
    k.m[A].owner = 0; cs[0] = ipcp_eff_prio(&k, 0);
    k.m[B].owner = 0; cs[1] = ipcp_eff_prio(&k, 0);
    k.m[A].owner = -1; cs[2] = ipcp_eff_prio(&k, 0);
    k.m[B].owner = -1; cs[3] = ipcp_eff_prio(&k, 0);
    CHECK(cs[0] == 3 && cs[1] == 3 && cs[2] == 2 && cs[3] == 1);

    /* ---- 연습 4: time slicing ---- */
    build_canonical(&k, PROTO_IPCP, 4, 2); k.timeslice = true; sim_run(&k);
    if (v) print_timeline(&k, "canonical C_M=4");
    CHECK(k.status == SIM_DONE && k.blocking_locks == 1 && count_ch(&k, H, 'B') > 0);
    build_canonical(&k, PROTO_IPCP, 4, 2); k.timeslice = true;
    compute_ceilings(&k); k.m[R].ceiling += 1; k.ceil_manual = true;    /* 천장 = 최고 사용자 + 1 */
    sim_run(&k);
    if (v) print_timeline(&k, "canonical C_M=4, ceiling 4");
    CHECK(k.status == SIM_DONE && k.blocking_locks == 0 && wait_ticks(&k, H) == 3);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
