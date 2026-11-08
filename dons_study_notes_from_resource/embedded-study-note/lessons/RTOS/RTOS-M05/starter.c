/* RTOS-M05 우선순위 천장 프로토콜(PCP/IPCP)과 교착 방지
 *
 * 호스트 모델: OS 스레드 하나에서 도는 tick 기반 미니 커널 (완전히 결정적).
 *  - 태스크 = op 스크립트. 매 tick, READY 중 유효(effective) 우선순위가 가장 높은 태스크가 op 를 실행한다.
 *      RUN(n)    n tick 동안 CPU 작업 (1 tick 에 1 단위, 선점되면 남은 만큼 나중에)
 *      LOCK(m)   0 tick: 획득하면 곧바로 다음 op, 막히면 그 tick 에 다른 태스크가 돈다
 *      UNLOCK(m) 0 tick: 곧바로 다음 op. 대기자가 있으면 소유권을 바로 넘긴다(handoff → 대기자의 LOCK 완료)
 *      (커널 호출 0 tick · 1 tick = 일 1 단위 — RTOS-M03/M04 와 같은 의미, 같은 숫자)
 *      END       0 tick: 태스크 종료
 *  - 같은 우선순위: 방금 돌던 태스크가 계속 돈다 (time slicing 없음 = SCHED_FIFO), 나머지는 READY 된 순서.
 *  - 뮤텍스 대기열: 유효 우선순위 높은 순, 같으면 먼저 막힌 순.
 *  - 프로토콜: NONE = 우선순위 변화 없음 (바이너리 세마포어를 뮤텍스처럼 쓴 것)
 *              PI   = 우선순위 상속 (연쇄 포함, RTOS-M04 의 규칙 — 여기선 주어진 코드)
 *              IPCP = 즉시 천장 (Immediate Priority Ceiling, "highest locker") ← 이 레슨의 TODO
 * 타임라인 문자: R 실행, B 뮤텍스 때문에 막힘, r 준비됐지만 못 돎, . 출시 전/종료.
 * 실행 인자 -v → 타임라인과 표를 출력 (md 에 붙인 출력은 ./solution -v 의 것).
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

enum { PROTO_NONE, PROTO_PI, PROTO_IPCP };
enum { T_UNREL, T_READY, T_BLOCKED, T_DONE };
enum { K_OK = 0, K_BLOCKED = 1, K_EINVAL = 22 };          /* 22 = POSIX EINVAL 과 같은 뜻 */
enum { SIM_RUNNING, SIM_DONE, SIM_DEADLOCK, SIM_TIMEOUT };

typedef struct {
    char name;
    int base, eff, release;            /* 기본 우선순위, 유효 우선순위, 출시 tick (클수록 높은 우선순위) */
    op_t ops[MAX_OPS];
    int pc, run_left, state, wait_m, err;
    unsigned seq;                      /* READY/BLOCKED 가 된 순서 (FIFO 판정) */
} task_t;
typedef struct { int owner, ceiling; } kmutex_t;   /* owner -1 = 비어 있음, ceiling -1 = 미계산 */

typedef struct {
    int proto, nt, nm, now, cur, status, deadlock_tick;
    int blocking_locks;                /* LOCK 이 "이미 잡힌" 뮤텍스를 만나 막힌 횟수 */
    int lock_errors;                   /* K_EINVAL 등 */
    bool ceil_manual;                  /* true 면 sim_run 이 천장을 다시 계산하지 않음 (설정 오류 실험용) */
    unsigned seqctr;
    task_t t[MAX_T];
    kmutex_t m[MAX_M];
    char tl[MAX_T][MAX_TICK + 1];      /* 타임라인 */
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

/* ================= TODO 영역 (Part A · B · C) ================= */

/* Part A-1: 각 뮤텍스의 천장 = 그 뮤텍스를 LOCK 하는 모든 태스크의 기본 우선순위 중 최댓값 (정적 분석). */
static void compute_ceilings(kernel_t *k) {
    (void)k; /* TODO: 모든 태스크 스크립트의 OP_LOCK 을 훑어 k->m[].ceiling 을 채운다 */
}

/* Part A-2: IPCP 유효 우선순위 = max(기본, 지금 보유한 모든 뮤텍스의 천장).
 * 잡는 즉시 올라가고, 놓으면 "아직 보유 중인 것" 기준으로 다시 계산된다 (LIFO 가 아니어도 맞다). */
static int ipcp_eff_prio(const kernel_t *k, int t) {
    return k->t[t].base; /* TODO: 보유 중인 뮤텍스의 천장 반영 */
}

/* Part A-3: 천장보다 높은 우선순위의 태스크가 잡으려 하면 설정 오류 → K_EINVAL
 * (POSIX pthread_mutex_lock + PTHREAD_PRIO_PROTECT 의 EINVAL 과 같은 규칙). */
static int ipcp_check_lock(const kernel_t *k, int t, int m) {
    (void)k; (void)t; (void)m;
    return K_OK; /* TODO */
}

/* Part B: 교착 판정 — 지금 돌 수 있는 태스크도, 앞으로 출시될 태스크도 없는데 막힌 태스크가 있다. */
static bool sim_deadlocked(const kernel_t *k) {
    (void)k;
    return false; /* TODO */
}

/* Part C: 설계 단계의 블로킹 상한. use[] = (태스크, 자원, 그 자원을 잡고 있는 최장 tick).
 * IPCP: B_i = max{ cs : 더 낮은 우선순위 태스크 j 의 임계 구역, 그 자원의 천장 >= prio[i] }  (임계 구역 1개) */
typedef struct { int task, res, cs; } use_t;
static int ipcp_ceiling(const int *prio, const use_t *u, int nu, int res) {   /* 주어진 코드 */
    int c = 0;
    for (int i = 0; i < nu; i++)
        if (u[i].res == res && prio[u[i].task] > c) c = prio[u[i].task];
    return c;
}
static int ipcp_blocking_bound(const int *prio, const use_t *u, int nu, int i) {
    (void)prio; (void)u; (void)nu; (void)i;
    return 0; /* TODO */
}

/* ================= 주어진 커널 (고치지 않는다) ================= */

/* PI 의 단순 상한 (비교용): 더 낮은 태스크마다 "i 를 막을 수 있는 자원" 위 최장 임계 구역 하나씩의 합. */
static int pi_blocking_bound(const int *prio, int nt, const use_t *u, int nu, int i) {
    int sum = 0;
    for (int j = 0; j < nt; j++) {
        if (prio[j] >= prio[i]) continue;
        int b = 0;
        for (int x = 0; x < nu; x++)
            if (u[x].task == j && ipcp_ceiling(prio, u, nu, u[x].res) >= prio[i] && u[x].cs > b) b = u[x].cs;
        sum += b;
    }
    return sum;
}

static void recompute(kernel_t *k) {
    for (int i = 0; i < k->nt; i++)
        k->t[i].eff = (k->proto == PROTO_IPCP) ? ipcp_eff_prio(k, i) : k->t[i].base;
    if (k->proto != PROTO_PI) return;
    for (bool changed = true; changed;) {      /* 연쇄 상속: 대기자 → 소유자 방향으로 고정점까지 */
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
        if (best == k->cur) continue;                 /* 같은 우선순위면 돌던 태스크 유지 */
        if (i == k->cur || a->seq < b->seq) best = i; /* 아니면 먼저 READY 된 쪽 */
    }
    return best;
}

static int k_lock(kernel_t *k, int t, int m) {
    if (k->proto == PROTO_IPCP) {
        int e = ipcp_check_lock(k, t, m);
        if (e != K_OK) return e;
    }
    if (k->m[m].owner < 0) { k->m[m].owner = t; recompute(k); return K_OK; }
    k->blocking_locks++;
    k->t[t].state = T_BLOCKED; k->t[t].wait_m = m; k->t[t].seq = ++k->seqctr;
    recompute(k);
    return K_BLOCKED;
}
static void k_unlock(kernel_t *k, int t, int m) {
    if (k->m[m].owner != t) { k->lock_errors++; return; }   /* 소유자만 놓을 수 있다 (RTOS-B10) */
    int w = -1;
    for (int i = 0; i < k->nt; i++) {
        if (k->t[i].state != T_BLOCKED || k->t[i].wait_m != m) continue;
        if (w < 0 || k->t[i].eff > k->t[w].eff || (k->t[i].eff == k->t[w].eff && k->t[i].seq < k->t[w].seq)) w = i;
    }
    k->m[m].owner = w;
    if (w >= 0) {                                            /* handoff: 대기자의 LOCK 이 지금 끝난다 */
        k->t[w].state = T_READY; k->t[w].wait_m = -1; k->t[w].pc++; k->t[w].seq = ++k->seqctr;
    }
    recompute(k);
}

static void sim_tick(kernel_t *k) {
    int ran = -1;
    for (int i = 0; i < k->nt; i++)
        if (k->t[i].state == T_UNREL && k->t[i].release == k->now) {
            k->t[i].state = T_READY; k->t[i].seq = ++k->seqctr;
        }
    for (int guard = 0; guard < MAX_T * MAX_OPS && ran < 0; guard++) {
        int c = pick(k);
        if (c < 0) break;                                    /* idle tick */
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
            else if (r == K_EINVAL) { p->err = r; p->state = T_DONE; k->lock_errors++; }                                         /* 획득이든 막힘이든 이 tick 을 썼다 */
            break;
        }
        case OP_UNLOCK: k_unlock(k, c, o.arg); p->pc++; break;   /* 0 tick → 다시 pick (선점 가능) */
        default: p->state = T_DONE; break;                       /* END */
        }
    }
    if (ran >= 0 && k->t[ran].run_left == 0 && k->t[ran].ops[k->t[ran].pc].op == OP_END)
        k->t[ran].state = T_DONE;                            /* 마지막 일 tick 에 바로 종료 (빈 tick 없이) */
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

/* ---------- 측정 도우미 ---------- */
static int count_ch(const kernel_t *k, int t, char c) {
    int n = 0;
    for (int x = 0; x < k->now; x++) n += (k->tl[t][x] == c);
    return n;
}
static int wait_ticks(const kernel_t *k, int t) { return count_ch(k, t, 'B') + count_ch(k, t, 'r'); }
static bool row_is(const kernel_t *k, int t, const char *s) { return strcmp(k->tl[t], s) == 0; }
static bool row_starts(const kernel_t *k, int t, const char *s) { return strncmp(k->tl[t], s, strlen(s)) == 0; }

static const char *proto_name(int p) { return p == PROTO_NONE ? "plain" : p == PROTO_PI ? "PI" : "IPCP"; }
static void print_timeline(const kernel_t *k, const char *title) {
    printf("%s  [%s]\n", title, proto_name(k->proto));
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

/* ---------- 시나리오 ---------- */
enum { R = 0 };                 /* 정규 시나리오의 공유 자원 */
enum { A = 0, B = 1 };          /* 교착 시나리오의 두 뮤텍스 */
enum { SPI = 0, PARAM = 1 };    /* Part C: SPI 버스, 파라미터 블록 */
enum { H, M, L };               /* 정규·Part C 시나리오의 태스크 인덱스 (추가 순서) */
enum { DH, DL };                /* 교착 시나리오의 태스크 인덱스 */

/* RTOS-M03 정규 시나리오: H=3, M=2, L=1. L t0 출시, t1 에 R 획득, R 안에서 4 tick 일.
 * H t2 출시, 첫 op 가 LOCK R. M t3 출시, R 없이 C_M tick CPU 일. */
static void build_canonical(kernel_t *k, int proto, int cm) {
    k_init(k, proto, 1);
    k_add(k, 'H', 3, 2, (const op_t[]){ LOCK(R), RUN(1), UNLOCK(R), RUN(1), END });
    if (cm > 0) k_add(k, 'M', 2, 3, (const op_t[]){ RUN(cm), END });
    else        k_add(k, 'M', 2, 3, (const op_t[]){ END });
    k_add(k, 'L', 1, 0, (const op_t[]){ RUN(1), LOCK(R), RUN(4), UNLOCK(R), RUN(1), END });
}
/* 교착 시나리오: L 은 A → B, H 는 B → A (반대 순서). M 없음. */
static void build_deadlock(kernel_t *k, int proto) {
    k_init(k, proto, 2);
    k_add(k, 'H', 3, 2, (const op_t[]){ LOCK(B), RUN(1), LOCK(A), RUN(1), UNLOCK(A), UNLOCK(B), RUN(1), END });
    k_add(k, 'L', 1, 0, (const op_t[]){ RUN(1), LOCK(A), RUN(2), LOCK(B), RUN(1), UNLOCK(B), UNLOCK(A), RUN(1), END });
}
/* Part C 펌웨어 태스크 세트: H=모터 제어 (SPI 엔코더 읽기 + 게인 파라미터), M=텔레메트리 (SPI CAN 컨트롤러),
 * L=로거 (파라미터 블록을 플래시 기록용으로 복사). */
static void build_fw(kernel_t *k, int proto, int h_release) {
    k_init(k, proto, 2);
    k_add(k, 'H', 3, h_release, (const op_t[]){ LOCK(SPI), RUN(1), UNLOCK(SPI), LOCK(PARAM), RUN(1), UNLOCK(PARAM), RUN(1), END });
    k_add(k, 'M', 2, 1, (const op_t[]){ RUN(1), LOCK(SPI), RUN(3), UNLOCK(SPI), RUN(1), END });
    k_add(k, 'L', 1, 0, (const op_t[]){ LOCK(PARAM), RUN(4), UNLOCK(PARAM), RUN(1), END });
}
static const int  FW_PRIO[3] = { 3, 2, 1 };
static const use_t FW_USE[4] = { { H, SPI, 1 }, { H, PARAM, 1 }, { M, SPI, 3 }, { L, PARAM, 4 } };

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(int argc, char **argv) {
    bool v = argc > 1 && strcmp(argv[1], "-v") == 0;
    static kernel_t k;
    static const int CMS[5] = { 0, 2, 4, 8, 16 };

    /* ---- 재현 (주어진 커널만으로 통과): plain 은 3 + C_M, PI 는 3 (RTOS-M03 과 같은 숫자) ---- */
    for (int i = 0; i < 5; i++) {
        build_canonical(&k, PROTO_NONE, CMS[i]); sim_run(&k);
        if (v && CMS[i] == 4) print_timeline(&k, "canonical C_M=4");
        CHECK(k.status == SIM_DONE && wait_ticks(&k, H) == 3 + CMS[i] && count_ch(&k, H, 'B') == 3 + CMS[i]);
        build_canonical(&k, PROTO_PI, CMS[i]); sim_run(&k);
        if (v && CMS[i] == 4) print_timeline(&k, "canonical C_M=4");
        CHECK(k.status == SIM_DONE && wait_ticks(&k, H) == 3 && count_ch(&k, H, 'B') == 3);
    }
    build_deadlock(&k, PROTO_NONE); sim_run(&k);
    CHECK(row_starts(&k, DH, "..RBB") && row_starts(&k, DL, "RRrRB"));  /* 둘 다 상대 뮤텍스를 기다린다 */

    /* ---- Part A: IPCP 뮤텍스 ---- */
    build_canonical(&k, PROTO_IPCP, 4);
    compute_ceilings(&k);
    CHECK(k.m[R].ceiling == 3);
    k.m[R].owner = L;
    CHECK(ipcp_eff_prio(&k, L) == 3 && ipcp_eff_prio(&k, M) == 2);       /* 잡는 순간 천장으로 */
    k.m[R].owner = -1;
    CHECK(ipcp_eff_prio(&k, L) == 1);                                    /* 놓으면 기본으로 */
    CHECK(ipcp_check_lock(&k, H, R) == K_OK && ipcp_check_lock(&k, L, R) == K_OK);

    build_canonical(&k, PROTO_IPCP, 4); sim_run(&k);
    if (v) print_timeline(&k, "canonical C_M=4");
    CHECK(k.status == SIM_DONE);
    CHECK(row_is(&k, H, "..rrrRR....."));                               /* B 가 아니라 r: 막힌 적이 없다 */
    CHECK(row_is(&k, M, "...rrrrRRRR."));
    CHECK(row_is(&k, L, "RRRRRrrrrrrR"));
    CHECK(k.blocking_locks == 0 && count_ch(&k, H, 'B') == 0);
    for (int i = 0; i < 5; i++) {                                        /* C_M 와 무관하게 3 */
        build_canonical(&k, PROTO_IPCP, CMS[i]); sim_run(&k);
        CHECK(k.status == SIM_DONE && wait_ticks(&k, H) == 3 && k.blocking_locks == 0);
    }
    if (v) {
        printf("\nC_M | H wait plain (B) | H wait PI (B) | H wait IPCP (r) | H finish tick plain/PI/IPCP\n");
        for (int i = 0; i < 5; i++) {
            int w[3], f[3];
            for (int p = 0; p < 3; p++) {
                build_canonical(&k, p, CMS[i]); sim_run(&k);
                w[p] = wait_ticks(&k, H);
                f[p] = (int)strlen(k.tl[H]) - 1;
                while (f[p] > 0 && k.tl[H][f[p]] != 'R') f[p]--;
            }
            printf("%3d | %16d | %13d | %15d | %d / %d / %d\n", CMS[i], w[0], w[1], w[2], f[0], f[1], f[2]);
        }
        printf("\n");
    }
    build_canonical(&k, PROTO_IPCP, 4);                  /* 설정 오류: 천장을 H 보다 낮게 (2) */
    compute_ceilings(&k); k.m[R].ceiling = 2; k.ceil_manual = true;
    CHECK(ipcp_check_lock(&k, H, R) == K_EINVAL);
    sim_run(&k);
    CHECK(k.lock_errors == 1 && k.t[H].err == K_EINVAL);

    /* ---- Part B: 반대 순서 두 뮤텍스 ---- */
    build_deadlock(&k, PROTO_NONE); sim_run(&k);
    if (v) print_timeline(&k, "ABBA");
    CHECK(k.status == SIM_DEADLOCK && k.deadlock_tick == 4);
    CHECK(sim_deadlocked(&k) && k.m[A].owner == DL && k.m[B].owner == DH);
    build_deadlock(&k, PROTO_PI); sim_run(&k);
    if (v) print_timeline(&k, "ABBA");
    CHECK(k.status == SIM_DEADLOCK && k.deadlock_tick == 4 && k.t[DL].eff == 3);   /* 상속돼도 교착 */
    build_deadlock(&k, PROTO_IPCP); sim_run(&k);
    if (v) print_timeline(&k, "ABBA");
    CHECK(k.status == SIM_DONE && k.blocking_locks == 0);
    CHECK(row_is(&k, DH, "..rrRRR."));
    CHECK(row_is(&k, DL, "RRRRrrrR"));
    build_deadlock(&k, PROTO_NONE);                      /* 출시 전 태스크가 있으면 아직 교착이 아니다 */
    CHECK(!sim_deadlocked(&k));
    k.t[DH].state = T_BLOCKED; k.t[DL].state = T_DONE;
    CHECK(sim_deadlocked(&k));

    /* ---- Part C: 펌웨어 태스크 세트, 블로킹 상한 = 임계 구역 1개 ---- */
    CHECK(ipcp_ceiling(FW_PRIO, FW_USE, 4, SPI) == 3 && ipcp_ceiling(FW_PRIO, FW_USE, 4, PARAM) == 3);
    int bH = ipcp_blocking_bound(FW_PRIO, FW_USE, 4, H);
    CHECK(bH == 4);
    CHECK(ipcp_blocking_bound(FW_PRIO, FW_USE, 4, M) == 4);              /* M 은 PARAM 을 안 써도 막힐 수 있다 */
    CHECK(ipcp_blocking_bound(FW_PRIO, FW_USE, 4, L) == 0);
    CHECK(pi_blocking_bound(FW_PRIO, 3, FW_USE, 4, H) == 7);
    int ipcp_max = 0, pi_max = 0, pi_arg = -1, ipcp_blk = 0;
    if (v) printf("H release | H wait PI | H wait IPCP\n");
    for (int o = 0; o <= 12; o++) {
        build_fw(&k, PROTO_PI, o); sim_run(&k);
        int wp = wait_ticks(&k, H);
        if (wp > pi_max) { pi_max = wp; pi_arg = o; }
        build_fw(&k, PROTO_IPCP, o); sim_run(&k);
        int wi = wait_ticks(&k, H);
        if (wi > ipcp_max) ipcp_max = wi;
        ipcp_blk += k.blocking_locks;
        if (v) printf("%9d | %9d | %11d\n", o, wp, wi);
    }
    CHECK(ipcp_max == 3 && ipcp_max <= bH && ipcp_blk == 0);           /* tick 경계 출시라 cs-1 */
    CHECK(pi_max == 5 && pi_max > bH && pi_max <= 7);                    /* PI: 임계 구역 2개에 연달아 막힘 */
    if (v) {
        build_fw(&k, PROTO_PI, pi_arg); sim_run(&k);   print_timeline(&k, "fw worst PI");
        build_fw(&k, PROTO_IPCP, pi_arg); sim_run(&k); print_timeline(&k, "fw same release");
    }
    build_fw(&k, PROTO_IPCP, 40); sim_run(&k);           /* H 가 없는 구간: IPCP 의 대가 */
    int m_ipcp = wait_ticks(&k, M);
    build_fw(&k, PROTO_PI, 40); sim_run(&k);
    int m_pi = wait_ticks(&k, M);
    CHECK(m_ipcp == 3 && m_pi == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
