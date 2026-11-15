/* RTOS-A04 확장 RTA: 블로킹 시간·릴리즈 지터·ISR 부하 반영
 *
 * RTOS-M11 의 RTA 도구를 "실제 펌웨어" 쪽으로 넓힌다. 분석 대상(entity) = 태스크 + ISR.
 *   - ISR 은 모든 태스크보다 높은 우선순위의 산발적(sporadic) 태스크: C = 최악 실행 시간, T = 최소 도착 간격.
 *   - 자원(resource) = IPCP 뮤텍스, 또는 천장(ceiling)이 정해진 "커널 임계 구역"
 *     (taskENTER_CRITICAL = BASEPRI 를 configMAX_SYSCALL_INTERRUPT_PRIORITY 로 → 천장 = 그 ISR 레벨).
 *   - 릴리즈 지터 J: 도착(arrival)부터 실행 가능(release)까지 0..J 늦어질 수 있다. 데드라인·응답은 도착 기준.
 * 분석식 (Audsley, Burns, Richardson, Tindell, Wellings 1993):
 *     w = C_i + B_i + sum_{j: prio_j > prio_i} ceil((w + J_j) / T_j) * C_j ,   R_i = w + J_i
 *     B_i = IPCP 에서 한 번 막힐 수 있는 가장 긴 임계 구역 (Sha, Rajkumar, Lehoczky 1990)
 * 그리고 이 분석과 코드 한 줄도 공유하지 않는 이산 시간 시뮬레이터로 "분석 >= 관측" (안전) 을 확인한다.
 *
 * 이 레슨에서 채우는 것 (TODO 1~5):
 *  1. compute_ceilings() : 자원 천장 = max(선언된 최소 천장, 그 자원을 쓰는 entity 우선순위 최댓값)
 *  2. blocking_ipcp()    : B_i = max{ len(cs) : cs 의 주인 prio < prio_i, 그 자원 천장 >= prio_i }
 *  3. rta_ext()          : 위 식의 고정점 반복. w + J_i > D_i 가 되는 순간 멈춘다.
 *  4. sim_run()          : 1 단위 시간씩 도는 고정 우선순위 선점 + IPCP 시뮬레이터 (ISR 포함)
 *  5. gen_critical()     : entity i 의 최악 출시 패턴 (임계 순간 + 최대 지터 + 가장 긴 블로커)
 * 주어진 코드: entity/자원 구성 함수, 검사기, M11 식 순진한 RTA(rta_naive), PRNG, 무작위 패턴, 출력.
 * 단위는 tick 또는 µs, 전부 정수. 우선순위는 클수록 높다 (FreeRTOS 방향, NVIC 숫자와 Zephyr 는 반대).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXE    12               /* entity (ISR + 태스크) 최대 수 */
#define MAXR    4                /* 자원 최대 수 */
#define MAXCS   3                /* entity 하나의 임계 구역 최대 수 (겹치지 않게, 순서대로) */
#define NAMELEN 12
#define MAXJOB  2048             /* 패턴 하나에서 entity 당 작업 수 상한 */
#define TLW     40               /* 타임라인 출력 폭 */

typedef struct { int res; uint32_t off, len; } cs_t;   /* 실행 off 단위 뒤에 res 를 잡고 len 단위 동안 쥔다 */
typedef struct {
    char name[NAMELEN];
    int is_isr, prio;            /* ISR 은 모든 태스크보다 높아야 한다 */
    uint32_t C, T, D, J;         /* T = 주기 또는 최소 도착 간격, C <= D <= T, J < T */
    int ncs; cs_t cs[MAXCS];
    uint32_t B;                  /* blocking_ipcp() 결과 */
} ent_t;
typedef struct {
    int n, nres;
    ent_t e[MAXE];
    char rname[MAXR][NAMELEN];
    int rmin[MAXR];              /* 선언된 최소 천장 (커널 임계 구역 = SYSCALL 레벨, 스케줄러 잠금 = 최상위 태스크) */
    int ceil[MAXR];              /* compute_ceilings() 결과 */
} sys_t;
typedef struct { int ok; uint32_t R; int iters; } rta_t;

/* ---------------- 주어진 코드: 구성 함수와 검사기 ---------------- */
static void sys_init(sys_t *s) { memset(s, 0, sizeof *s); }
static int add_ent(sys_t *s, const char *name, int is_isr, int prio,
                   uint32_t C, uint32_t T, uint32_t D, uint32_t J) {
    ent_t *e = &s->e[s->n];
    memset(e, 0, sizeof *e);
    snprintf(e->name, NAMELEN, "%s", name);
    e->is_isr = is_isr; e->prio = prio; e->C = C; e->T = T; e->D = D; e->J = J;
    return s->n++;
}
static int add_res(sys_t *s, const char *name, int min_ceiling) {
    snprintf(s->rname[s->nres], NAMELEN, "%s", name);
    s->rmin[s->nres] = min_ceiling;
    return s->nres++;
}
static void add_cs(sys_t *s, int ei, int res, uint32_t off, uint32_t len) {
    ent_t *e = &s->e[ei];
    e->cs[e->ncs].res = res; e->cs[e->ncs].off = off; e->cs[e->ncs].len = len;
    e->ncs++;
}
static int find_ent(const sys_t *s, const char *name) {
    for (int i = 0; i < s->n; i++) if (strcmp(s->e[i].name, name) == 0) return i;
    return -1;
}
/* 0 = OK. 우선순위 서로 다름, ISR > 모든 태스크, C <= D <= T, J < T, cs 는 [0, C) 안에서 겹치지 않게 오름차순. */
static int sys_check(const sys_t *s) {
    int max_task = -1000000, min_isr = 1000000;
    for (int i = 0; i < s->n; i++) {
        const ent_t *e = &s->e[i];
        if (e->C == 0 || e->C > e->D || e->D > e->T || e->J >= e->T) return -1;
        for (int j = 0; j < i; j++) if (s->e[j].prio == e->prio) return -2;
        if (e->is_isr) { if (e->prio < min_isr) min_isr = e->prio; }
        else if (e->prio > max_task) max_task = e->prio;
        uint32_t end = 0;
        for (int k = 0; k < e->ncs; k++) {
            const cs_t *c = &e->cs[k];
            if (c->res < 0 || c->res >= s->nres || c->len == 0 || c->off < end || c->off + c->len > e->C) return -3;
            end = c->off + c->len;
        }
    }
    return (max_task < min_isr) ? 0 : -4;
}

/* RTOS-M11 Part A 의 기본 RTA 그대로: 태스크만, B 없음, J 없음, ISR 없음. "순진한(naive)" 비교 기준. */
static rta_t rta_naive(const sys_t *s, int i) {
    rta_t r = {0, 0, 0};
    const ent_t *ei = &s->e[i];
    uint64_t w = ei->C;
    for (;;) {
        r.iters++;
        uint64_t nw = ei->C;
        for (int j = 0; j < s->n; j++)
            if (j != i && !s->e[j].is_isr && s->e[j].prio > ei->prio)
                nw += ((w + s->e[j].T - 1) / s->e[j].T) * s->e[j].C;
        if (nw > ei->D) { r.R = (uint32_t)nw; return r; }
        if (nw == w) { r.ok = 1; r.R = (uint32_t)nw; return r; }
        w = nw;
    }
}

/* ---------------- 주어진 코드: 출시 패턴, 관측 결과, PRNG ---------------- */
typedef struct { int n; int32_t arr[MAXJOB], rel[MAXJOB]; } jobs_t;   /* 도착 시각, 출시 시각 (오름차순) */
typedef struct { jobs_t j[MAXE]; } pattern_t;
typedef struct {
    uint32_t max_resp[MAXE];     /* 도착부터 완료까지, 최댓값 */
    uint32_t first_resp[MAXE];   /* 첫 작업의 응답 */
    uint32_t max_block[MAXE];    /* 작업 하나가 "더 낮은 entity 가 도는 동안" 기다린 시간, 최댓값 */
    int done[MAXE], misses[MAXE];
    int unfinished, ipcp_conflicts;
} obs_t;
typedef struct { int32_t t0; char row[MAXE][TLW + 1]; } tl_t;          /* 타임라인 (선택) */

static uint32_t rng_state;
static uint32_t rnd(void) {                      /* xorshift32 */
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
static void push_job(jobs_t *jb, int32_t arr, int32_t rel) {
    if (jb->n < MAXJOB) { jb->arr[jb->n] = arr; jb->rel[jb->n] = rel; jb->n++; }
}
/* 무작위 산발적 출시: 첫 도착은 [0, T), 간격은 T (절반 확률) 또는 T + [0, T/2],
 * 지터는 0 / J / [0, J] 균등 중 하나 (극단값을 자주 뽑아 최악에 가까운 경우를 늘린다). */
static void gen_random(const sys_t *s, uint32_t seed, int32_t horizon, pattern_t *p) {
    memset(p, 0, sizeof *p);
    rng_state = seed ? seed : 1u;
    for (int e = 0; e < s->n; e++) {
        const ent_t *x = &s->e[e];
        int32_t a = (int32_t)(rnd() % x->T);
        while (a < horizon && p->j[e].n < MAXJOB) {
            uint32_t pick = rnd() % 3, jit = pick == 0 ? 0 : pick == 1 ? x->J : rnd() % (x->J + 1);
            push_job(&p->j[e], a, a + (int32_t)jit);
            a += (int32_t)x->T + ((rnd() & 1u) ? 0 : (int32_t)(rnd() % (x->T / 2 + 1)));
        }
    }
}
static void tl_mark(tl_t *tl, int32_t t, int e, char ch) {
    if (tl && t >= tl->t0 && t < tl->t0 + TLW) tl->row[e][t - tl->t0] = ch;
}
static void tl_print(const sys_t *s, const tl_t *tl) {
    printf(" tick   :");
    for (int k = 0; k < TLW; k++) printf(" %d", (int)((tl->t0 + k) % 10));
    printf("\n");
    for (int e = 0; e < s->n; e++) {
        printf(" %-7s:", s->e[e].name);
        for (int k = 0; k < TLW; k++) printf(" %c", tl->row[e][k] ? tl->row[e][k] : '.');
        printf("\n");
    }
}

/* ================= TODO 1: 자원 천장 ================= */
static void compute_ceilings(sys_t *s) {
    for (int r = 0; r < s->nres; r++) s->ceil[r] = s->rmin[r];
    for (int i = 0; i < s->n; i++)
        for (int k = 0; k < s->e[i].ncs; k++) {
            int r = s->e[i].cs[k].res;
            if (s->e[i].prio > s->ceil[r]) s->ceil[r] = s->e[i].prio;
        }
}

/* ================= TODO 2: IPCP 블로킹 항 ================= */
static void blocking_ipcp(sys_t *s) {
    for (int i = 0; i < s->n; i++) {
        uint32_t b = 0;
        for (int l = 0; l < s->n; l++) {
            if (s->e[l].prio >= s->e[i].prio) continue;          /* 더 낮은 entity 의 임계 구역만 */
            for (int k = 0; k < s->e[l].ncs; k++) {
                const cs_t *c = &s->e[l].cs[k];
                if (s->ceil[c->res] >= s->e[i].prio && c->len > b) b = c->len;
            }
        }
        s->e[i].B = b;                                          /* 합이 아니라 max: 한 번만 막힌다 */
    }
}

/* ================= TODO 3: 확장 RTA ================= */
static rta_t rta_ext(const sys_t *s, int i) {
    rta_t r = {0, 0, 0};
    const ent_t *ei = &s->e[i];
    uint64_t w = (uint64_t)ei->C + ei->B;
    for (;;) {
        r.iters++;
        uint64_t nw = (uint64_t)ei->C + ei->B;
        for (int j = 0; j < s->n; j++) {
            const ent_t *ej = &s->e[j];
            if (j == i || ej->prio <= ei->prio) continue;      /* ISR 도 그냥 더 높은 entity */
            nw += ((w + ej->J + ej->T - 1) / ej->T) * ej->C;
        }
        if (nw + ei->J > ei->D) { r.R = (uint32_t)(nw + ei->J); return r; }   /* 멈춘 값: 하한일 뿐 */
        if (nw == w) { r.ok = 1; r.R = (uint32_t)(nw + ei->J); return r; }
        w = nw;
    }
}

/* ================= TODO 4: 이산 시간 시뮬레이터 =================
 * 매 시각 t: (1) 출시 시각 <= t 인 작업을 대기열에 넣는다 (entity 별 FIFO = 작업 목록의 [done, rel) 구간).
 * (2) 대기 작업이 있는 entity 중 "활성 우선순위" 가 가장 높은 것을 고른다.
 *     활성 = 자원을 쥐고 있으면 그 천장, 아니면 prio. 동점(쥔 쪽의 천장 == 다른 쪽 prio)이면 쥔 쪽이 이긴다
 *     (BASEPRI 는 "같은 레벨까지" 막는다).
 * (3) 고른 작업을 1 단위 실행: 실행량 == 다음 cs.off 면 먼저 자원을 잡고(IPCP: 이미 잡혀 있으면 위반 카운트),
 *     실행량 == off + len 이면 놓고, 실행량 == C 면 완료 (응답 = t + 1 - 도착, > D 면 미스).
 * (4) 대기 중인데 prio 가 실행 중인 entity 의 prio 보다 높은 작업은 그 1 단위를 "블로킹" 으로 센다.
 * 타임라인 기호: R 실행, C 자원 쥐고 실행, r 선점당해 대기, B 더 낮은 entity 때문에 대기, . 작업 없음. */
static void sim_run(const sys_t *s, const pattern_t *p, obs_t *o, tl_t *tl) {
    memset(o, 0, sizeof *o);
    int rel[MAXE] = {0}, nextcs[MAXE] = {0}, hold[MAXE], owner[MAXR];
    uint32_t exec[MAXE] = {0}, blk[MAXE] = {0};
    int32_t t = INT32_MAX, last = INT32_MIN;
    int total = 0, completed = 0;
    uint32_t maxT = 1;
    for (int e = 0; e < s->n; e++) {
        hold[e] = -1;
        if (s->e[e].T > maxT) maxT = s->e[e].T;
        total += p->j[e].n;
        if (p->j[e].n) {
            if (p->j[e].rel[0] < t) t = p->j[e].rel[0];
            if (p->j[e].rel[p->j[e].n - 1] > last) last = p->j[e].rel[p->j[e].n - 1];
        }
    }
    for (int r = 0; r < MAXR; r++) owner[r] = -1;
    if (total == 0) return;
    const int32_t cap = last + 10 * (int32_t)maxT;
    while (completed < total && t <= cap) {
        int32_t next_rel = INT32_MAX;
        for (int e = 0; e < s->n; e++) {
            const jobs_t *jb = &p->j[e];
            while (rel[e] < jb->n && jb->rel[rel[e]] <= t) rel[e]++;
            if (rel[e] < jb->n && jb->rel[rel[e]] < next_rel) next_rel = jb->rel[rel[e]];
        }
        int best = -1, bkey = 0;
        for (int e = 0; e < s->n; e++) {
            if (rel[e] <= o->done[e]) continue;                 /* 대기 작업 없음 */
            int act = hold[e] >= 0 ? s->ceil[s->e[e].cs[hold[e]].res] : s->e[e].prio;
            int key = 2 * act + (hold[e] >= 0);
            if (best < 0 || key > bkey) { best = e; bkey = key; }
        }
        if (best < 0) { t = next_rel; continue; }                /* 유휴: 다음 출시로 건너뛴다 */
        for (int e = 0; e < s->n; e++) {
            if (e == best || rel[e] <= o->done[e]) continue;
            if (s->e[e].prio > s->e[best].prio) { blk[e]++; tl_mark(tl, t, e, 'B'); }
            else tl_mark(tl, t, e, 'r');
        }
        const ent_t *x = &s->e[best];
        if (hold[best] < 0 && nextcs[best] < x->ncs && exec[best] == x->cs[nextcs[best]].off) {
            int r = x->cs[nextcs[best]].res;
            if (owner[r] >= 0) o->ipcp_conflicts++;
            owner[r] = best; hold[best] = nextcs[best];
        }
        tl_mark(tl, t, best, hold[best] >= 0 ? 'C' : 'R');
        exec[best]++;
        if (hold[best] >= 0 && exec[best] == x->cs[hold[best]].off + x->cs[hold[best]].len) {
            owner[x->cs[hold[best]].res] = -1; hold[best] = -1; nextcs[best]++;
        }
        if (exec[best] == x->C) {
            int k = o->done[best];
            uint32_t resp = (uint32_t)(t + 1 - p->j[best].arr[k]);
            if (k == 0) o->first_resp[best] = resp;
            if (resp > o->max_resp[best]) o->max_resp[best] = resp;
            if (resp > x->D) o->misses[best]++;
            if (blk[best] > o->max_block[best]) o->max_block[best] = blk[best];
            o->done[best]++; completed++;
            exec[best] = 0; nextcs[best] = 0; blk[best] = 0;
        }
        t++;
    }
    o->unfinished = total - completed;
}

/* ================= TODO 5: 최악 출시 패턴 =================
 * entity i 의 첫 작업에 대해 (모든 시각에 shift 를 더해 0 이상으로):
 *  - 블로커: prio < prio_i 이고 천장 >= prio_i 인 cs 중 가장 긴 것의 주인 l. l 의 작업 1개를
 *    도착 = 출시 = 0 에 두어, off 단위를 돌고 t = off 에 자원을 잡게 한다. 그러면 shift = off + 1.
 *  - i 와 모든 상위 entity j: 첫 도착 = shift - J, 출시 = shift (최대 지터),
 *    k 번째 도착 = shift - J + k*T, 출시 = 도착 (지터 0) → 출시 간격이 T - J 로 가장 촘촘.
 *  - 나머지 하위 entity: 작업 없음.  작업은 출시 시각 < shift + horizon 까지. */
static void gen_critical(const sys_t *s, int i, int32_t horizon, pattern_t *p) {
    memset(p, 0, sizeof *p);
    int bl = -1; uint32_t blen = 0, boff = 0;
    for (int l = 0; l < s->n; l++) {
        if (s->e[l].prio >= s->e[i].prio) continue;
        for (int k = 0; k < s->e[l].ncs; k++) {
            const cs_t *c = &s->e[l].cs[k];
            if (s->ceil[c->res] >= s->e[i].prio && c->len > blen) { bl = l; blen = c->len; boff = c->off; }
        }
    }
    int32_t shift = 0;
    if (bl >= 0) { shift = (int32_t)boff + 1; push_job(&p->j[bl], 0, 0); }
    for (int j = 0; j < s->n; j++) {
        if (s->e[j].prio < s->e[i].prio) continue;
        int32_t a0 = shift - (int32_t)s->e[j].J;
        push_job(&p->j[j], a0, shift);
        for (int32_t a = a0 + (int32_t)s->e[j].T; a < shift + horizon; a += (int32_t)s->e[j].T)
            push_job(&p->j[j], a, a);
    }
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total, g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static pattern_t g_pat;                              /* 큰 구조체라 전역 (~200 KB) */

static void analyze(sys_t *s) { compute_ceilings(s); blocking_ipcp(s); }
static int all_ext_ok(const sys_t *s) {
    for (int i = 0; i < s->n; i++) if (!rta_ext(s, i).ok) return 0;
    return 1;
}
/* 이산 시간에서 블로커는 i 출시 1 단위 전에 자원을 잡아야 하므로 실제 최대 블로킹은 B - 1 (연속 시간 B - ε). */
static rta_t rta_discrete(const sys_t *s, int i) {
    sys_t c = *s;
    if (c.e[i].B > 0) c.e[i].B--;
    return rta_ext(&c, i);
}
/* entity i 가 자기 우선순위보다 높은 천장의 자원을 쥐는 구간이 있나 (그 동안 일부 상위 entity 가 마스크된다) */
static int self_masks(const sys_t *s, int i) {
    for (int k = 0; k < s->e[i].ncs; k++) if (s->ceil[s->e[i].cs[k].res] > s->e[i].prio) return 1;
    return 0;
}
static void print_table(const char *title, const sys_t *s) {
    printf("== %s\n   %-7s %3s %4s %6s %6s %6s %4s %5s | %6s %3s %-4s | naive\n", title,
           "name", "isr", "prio", "C", "T", "D", "J", "B", "R", "it", "");
    for (int i = 0; i < s->n; i++) {
        const ent_t *e = &s->e[i];
        rta_t r = rta_ext(s, i);
        printf("   %-7s %3s %4d %6u %6u %6u %4u %5u | %6u %3d %-4s |", e->name, e->is_isr ? "isr" : "",
               e->prio, e->C, e->T, e->D, e->J, e->B, r.R, r.iters, r.ok ? "ok" : "MISS");
        if (e->is_isr) printf("   (ISR 무시)\n");
        else { rta_t n = rta_naive(s, i); printf(" %6u %s\n", n.R, n.ok ? "ok" : "MISS"); }
    }
}

/* ---- Part A 예제: 손으로 따라갈 수 있는 작은 시스템 (단위 tick) ---- */
static void build_a(sys_t *s) {
    sys_init(s);
    int kern = add_res(s, "KERNEL", 9);              /* 커널 임계 구역: 천장 = SYSCALL 레벨 9 */
    int r1 = add_res(s, "R1", 0);
    add_ent(s, "isr1", 1, 10, 1, 10, 10, 0);          /* SYSCALL 위: API 못 부르고, 임계 구역에 안 막힌다 */
    int i2 = add_ent(s, "isr2", 1, 9, 2, 20, 10, 0);  /* SYSCALL 레벨: FromISR API 사용 */
    int th = add_ent(s, "tH", 0, 3, 4, 40, 20, 2);
    int tm = add_ent(s, "tM", 0, 2, 6, 60, 40, 0);
    int tl = add_ent(s, "tL", 0, 1, 10, 120, 120, 0);
    add_cs(s, i2, kern, 0, 1);
    add_cs(s, th, r1, 1, 2);
    add_cs(s, tm, kern, 2, 2);
    add_cs(s, tl, r1, 3, 5);
    add_cs(s, tl, kern, 9, 1);
}

/* ---- Part B: 무작위 시스템 생성기 (분석이 "통과" 라고 한 시스템만 시뮬레이션으로 검사) ---- */
static void gen_system(sys_t *s, uint32_t seed) {
    static const uint32_t tmenu[] = {20, 24, 30, 40, 48, 60, 80, 100, 120};
    static const uint32_t imenu[] = {8, 10, 12, 15};
    sys_init(s);
    rng_state = seed * 2654435761u + 1u;
    int kern = add_res(s, "KERNEL", 20);
    int r1 = add_res(s, "R1", 0), r2 = add_res(s, "R2", 0);
    int nisr = 1 + (int)(rnd() % 2), ntask = 3 + (int)(rnd() % 3);
    for (int k = 0; k < nisr; k++) {                 /* prio 20 (SYSCALL 레벨, API 사용) / 21 (그 위) */
        uint32_t T = imenu[rnd() % 4], C = 1 + rnd() % 2;
        char nm[NAMELEN]; snprintf(nm, sizeof nm, "isr%d", k);
        int e = add_ent(s, nm, 1, 20 + k, C, T, T, rnd() % 2);
        if (k == 0 && (rnd() & 1u)) add_cs(s, e, kern, 0, 1);
    }
    for (int k = 0; k < ntask; k++) {
        uint32_t T = tmenu[rnd() % 9];
        uint32_t C = 2 + rnd() % (T / 6);
        uint32_t D = T - rnd() % (T / 3);
        if (D < C) D = C;
        uint32_t J = rnd() % (T / 5);
        char nm[NAMELEN]; snprintf(nm, sizeof nm, "t%d", k);
        int e = add_ent(s, nm, 0, 1 + k, C, T, D, J);
        uint32_t pos = rnd() % 2;
        int ncs = (int)(rnd() % 3);
        for (int c = 0; c < ncs && pos < C; c++) {
            uint32_t len = 1 + rnd() % 5;
            if (pos + len > C) len = C - pos;
            int which = (int)(rnd() % 4);
            int res = which == 0 ? kern : which == 1 ? r2 : r1;
            if (len) add_cs(s, e, res, pos, len);
            pos += len + rnd() % 3;
        }
    }
}

/* ---- Part C: STM32 모터 드라이브 (단위 µs) ----
 * ISR 논리 우선순위(클수록 급함): adc 15 (SYSCALL 위), can 12 (= SYSCALL), systick 11 (KERNEL_INTERRUPT = 최저).
 * 태스크: ctrl 4 > imu 3 > comms 2 > log 1.  자원: KERNEL(천장 12), SCHED(천장 4 = vTaskSuspendAll), SPI, PARAM. */
static void build_c(sys_t *s, int fixed) {
    sys_init(s);
    int kern = add_res(s, "KERNEL", 12);
    int sched = add_res(s, "SCHED", 4);
    int spi = add_res(s, "SPI", 0), param = add_res(s, "PARAM", 0);
    add_ent(s, "adc", 1, 15, 6, 50, 50, 0);           /* 20 kHz 전류 샘플 ISR, 커널 API 없음 */
    int can = add_ent(s, "can", 1, 12, 9, 160, 40, 0);/* CAN RX: FIFO 넘치기 전 40 µs 안에 비워야 */
    int tick = add_ent(s, "systick", 1, 11, 5, 1000, 1000, 0);
    int ctrl = add_ent(s, "ctrl", 0, 4, 120, 1000, 400, 30);
    int imu = add_ent(s, "imu", 0, 3, 200, 2000, 1000, 30);
    int comms = add_ent(s, "comms", 0, 2, 300, 5000, 3000, 0);
    int lg = add_ent(s, "log", 0, 1, 1500, 20000, 20000, 0);
    add_cs(s, can, kern, 2, 3);                       /* xQueueSendFromISR 내부 임계 구역 */
    add_cs(s, tick, kern, 0, 5);                      /* xPortSysTickHandler: 핸들러 전체가 BASEPRI 마스크 */
    add_cs(s, ctrl, param, 10, 10);
    add_cs(s, ctrl, kern, 100, 4);
    add_cs(s, imu, spi, 20, 60);
    add_cs(s, comms, param, 50, 15);
    add_cs(s, comms, kern, 100, 6);
    if (!fixed) {
        add_cs(s, lg, kern, 100, 45);                 /* 버그 1: 링 버퍼 memcpy 를 taskENTER_CRITICAL 로 */
        add_cs(s, lg, spi, 400, 500);                 /* 버그 2: 플래시 페이지 쓰기 동안 SPI 를 쥔다 */
    } else {
        add_cs(s, lg, sched, 100, 45);                /* 고침 1: vTaskSuspendAll 로 (ISR 은 안 막는다) */
        add_cs(s, lg, spi, 400, 100);                 /* 고침 2: 페이지 쓰기를 100 µs 조각으로 */
        add_cs(s, lg, spi, 600, 100);
    }
}

static void sim_all(const sys_t *s, int nrand, uint32_t seed0, int32_t horizon,
                    uint32_t *maxobs, uint32_t *maxblk, int *misses, int *conflicts, int *unfinished) {
    obs_t o;
    for (int i = 0; i < s->n; i++) { maxobs[i] = 0; maxblk[i] = 0; }
    *misses = *conflicts = *unfinished = 0;
    for (int run = 0; run < nrand + s->n; run++) {
        if (run < nrand) gen_random(s, seed0 + (uint32_t)run * 7919u, horizon, &g_pat);
        else gen_critical(s, run - nrand, horizon, &g_pat);
        sim_run(s, &g_pat, &o, NULL);
        for (int i = 0; i < s->n; i++) {
            if (o.max_resp[i] > maxobs[i]) maxobs[i] = o.max_resp[i];
            if (o.max_block[i] > maxblk[i]) maxblk[i] = o.max_block[i];
            *misses += o.misses[i];
        }
        *conflicts += o.ipcp_conflicts; *unfinished += o.unfinished;
    }
}

static void sim_random_max(const sys_t *s, int nrand, uint32_t seed0, int32_t horizon, uint32_t *mx) {
    obs_t o;
    for (int i = 0; i < s->n; i++) mx[i] = 0;
    for (int run = 0; run < nrand; run++) {
        gen_random(s, seed0 + (uint32_t)run * 7919u, horizon, &g_pat);
        sim_run(s, &g_pat, &o, NULL);
        for (int i = 0; i < s->n; i++) if (o.max_resp[i] > mx[i]) mx[i] = o.max_resp[i];
    }
}

int main(int argc, char **argv) {
    g_verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);
    sys_t s; obs_t o;

    /* ===== Part A: 천장, B, 확장 RTA, 최악 패턴 ===== */
    build_a(&s);
    CHECK(sys_check(&s) == 0);
    analyze(&s);
    int i1 = find_ent(&s, "isr1"), i2 = find_ent(&s, "isr2"), th = find_ent(&s, "tH"),
        tm = find_ent(&s, "tM"), tlw = find_ent(&s, "tL");
    CHECK(s.ceil[0] == 9 && s.ceil[1] == 3);                         /* KERNEL 9, R1 = max(tH 3, tL 1) */
    CHECK(s.e[i1].B == 0);                                            /* SYSCALL 위 ISR: 아무도 못 막는다 */
    CHECK(s.e[i2].B == 2);                                            /* tM 의 taskENTER_CRITICAL 2 */
    CHECK(s.e[th].B == 5 && s.e[tm].B == 5 && s.e[tlw].B == 0);      /* tL 의 R1 구간 5 (tM 은 push-through) */
    rta_t r;
    r = rta_ext(&s, i1);  CHECK(r.ok && r.R == 1);
    r = rta_ext(&s, i2);  CHECK(r.ok && r.R == 5 && r.iters == 2);
    r = rta_ext(&s, th);  CHECK(r.ok && r.R == 15 && r.iters == 3);
    r = rta_ext(&s, tm);  CHECK(r.ok && r.R == 19);
    r = rta_ext(&s, tlw); CHECK(r.ok && r.R == 27 && r.iters == 3);
    CHECK(rta_naive(&s, th).R == 4 && rta_naive(&s, tm).R == 10 && rta_naive(&s, tlw).R == 20);
    if (g_verbose) print_table("A: small system (ticks)", &s);

    tl_t tl; memset(&tl, 0, sizeof tl); tl.t0 = 0;
    gen_critical(&s, th, 40, &g_pat);
    CHECK(g_pat.j[tlw].n == 1 && g_pat.j[tlw].rel[0] == 0);          /* 블로커 = tL, off 3 → shift 4 */
    CHECK(g_pat.j[th].n >= 1 && g_pat.j[th].arr[0] == 2 && g_pat.j[th].rel[0] == 4);
    CHECK(g_pat.j[i1].n >= 2 && g_pat.j[i1].rel[1] == 14);
    CHECK(g_pat.j[tm].n == 0);                                        /* 다른 하위 entity: 작업 없음 */
    sim_run(&s, &g_pat, &o, &tl);
    CHECK(o.first_resp[th] == 14);                                    /* = rta_discrete (B-1 = 4) */
    CHECK(o.first_resp[th] == rta_discrete(&s, th).R);
    CHECK(o.done[th] >= 1 && o.first_resp[th] <= rta_ext(&s, th).R); /* 안전: 14 <= 15 */
    CHECK(o.first_resp[th] > rta_naive(&s, th).R);                   /* 순진한 RTA 4 는 틀렸다 */
    CHECK(o.max_block[th] == 4 && o.ipcp_conflicts == 0 && o.unfinished == 0);
    if (g_verbose) { printf("-- A: critical pattern for tH (shift 4)\n"); tl_print(&s, &tl); }

    int adv_eq = 0;
    for (int i = 0; i < s.n; i++) {
        gen_critical(&s, i, 2 * (int32_t)s.e[i].T, &g_pat);
        sim_run(&s, &g_pat, &o, NULL);
        if (o.done[i] >= 1 && o.first_resp[i] == rta_discrete(&s, i).R && o.ipcp_conflicts == 0) adv_eq++;
    }
    CHECK(adv_eq == s.n);                                             /* 모든 entity 에서 분석과 정확히 일치 */
    uint32_t mo[MAXE], mb[MAXE]; int ms, cf, uf;
    sim_all(&s, 200, 11, 600, mo, mb, &ms, &cf, &uf);
    int safe = 1, blk_ok = 1;
    for (int i = 0; i < s.n; i++) {
        if (mo[i] > rta_ext(&s, i).R) safe = 0;
        if (mb[i] > s.e[i].B) blk_ok = 0;
    }
    CHECK(safe && blk_ok && ms == 0 && cf == 0 && uf == 0 && mo[i1] == 1);
    CHECK(mo[th] == 14);                                              /* 무작위 200 + 최악 패턴 중 최대 */
    uint32_t rx[MAXE];
    sim_random_max(&s, 200, 11, 600, rx);
    CHECK(rx[th] == 13 && rx[th] < mo[th]);                           /* 무작위만으로는 최악을 못 만난다 */
    if (g_verbose) {
        printf("   A sim: random-only 200 runs max");
        for (int i = 0; i < s.n; i++) printf(" %s=%u", s.e[i].name, rx[i]);
        printf("\n   A sim: + critical patterns   max");
        for (int i = 0; i < s.n; i++) printf(" %s=%u", s.e[i].name, mo[i]);
        printf("\n");
    }

    /* ===== Part B: 경계 사례 ===== */
    /* B1 지터만으로 순진한 분석이 깨진다: 같은 우선순위 구조, J 만 다르다 */
    sys_init(&s);
    add_ent(&s, "hi", 0, 2, 2, 10, 10, 6);
    add_ent(&s, "lo", 0, 1, 5, 20, 9, 0);
    analyze(&s);
    CHECK(rta_naive(&s, 1).ok && rta_naive(&s, 1).R == 7);           /* 순진: 7 <= 9 */
    r = rta_ext(&s, 1); CHECK(r.ok && r.R == 9);                     /* 지터: hi 두 작업이 4 tick 간격 */
    gen_critical(&s, 1, 20, &g_pat); sim_run(&s, &g_pat, &o, NULL);
    CHECK(o.first_resp[1] == 9 && o.misses[1] == 0);                 /* hi 첫 작업 시작 0, 둘째 4 → 9 */
    r = rta_ext(&s, 0); CHECK(r.ok && r.R == 8);                      /* 자기 지터는 끝에 더한다: 2 + 6 */
    /* lo 의 D 를 8 로 → 실제 미스 */
    s.e[1].D = 8; analyze(&s);
    CHECK(rta_naive(&s, 1).ok && !rta_ext(&s, 1).ok);
    gen_critical(&s, 1, 20, &g_pat); sim_run(&s, &g_pat, &o, NULL);
    CHECK(o.misses[1] == 1 && o.first_resp[1] == 9);

    /* B2 ISR 이 SYSCALL 위냐 아래냐: 커널 임계 구역 200 tick 이 있는 태스크 */
    sys_init(&s);
    int k = add_res(&s, "KERNEL", 5);
    add_ent(&s, "fast", 1, 6, 1, 50, 5, 0);
    add_ent(&s, "slow", 1, 5, 1, 50, 5, 0);
    int bg = add_ent(&s, "bg", 0, 1, 300, 1000, 1000, 0);
    add_cs(&s, bg, k, 10, 200);
    CHECK(sys_check(&s) == 0);
    analyze(&s);
    CHECK(s.e[0].B == 0 && s.e[1].B == 200);
    CHECK(rta_ext(&s, 0).ok && !rta_ext(&s, 1).ok);
    gen_critical(&s, 1, 300, &g_pat); sim_run(&s, &g_pat, &o, NULL);
    CHECK(o.first_resp[1] == 205 && o.misses[1] >= 1);               /* 블로킹 199 + fast 5번 + 자기 1 */
    gen_critical(&s, 0, 300, &g_pat); sim_run(&s, &g_pat, &o, NULL);
    CHECK(o.first_resp[0] == 1);

    /* B3 과부하: 상위 이용률 1 → 멈춘다 (무한 루프 없음) */
    sys_init(&s);
    add_ent(&s, "irq", 1, 9, 5, 10, 10, 0);
    add_ent(&s, "h", 0, 2, 5, 10, 10, 0);
    add_ent(&s, "l", 0, 1, 1, 100, 100, 0);
    analyze(&s);
    r = rta_ext(&s, 2); CHECK(!r.ok && r.R > 100 && r.iters < 100);
    CHECK(rta_naive(&s, 2).ok);                                       /* ISR 을 빼면 "여유 많음" */

    /* B4 검사기 */
    sys_init(&s); add_ent(&s, "a", 0, 5, 1, 10, 10, 0); add_ent(&s, "i", 1, 4, 1, 10, 10, 0);
    CHECK(sys_check(&s) == -4);                                       /* ISR 이 태스크보다 낮음 */
    sys_init(&s); k = add_res(&s, "R", 0); int a = add_ent(&s, "a", 0, 1, 4, 10, 10, 0);
    add_cs(&s, a, k, 2, 3);
    CHECK(sys_check(&s) == -3);                                       /* cs 가 C 를 넘는다 */

    /* B5 무작위 시스템: 분석 통과 → 무작위 출시 + 최악 패턴 시뮬레이션이 절대 넘지 않는다 */
    int nsys = 0, nviol = 0, nnaive_bad = 0, nadv_exact = 0, nadv = 0, nmiss_ok_naive = 0, tried = 0;
    int nself = 0, nself_less = 0, nself_over = 0;
    for (uint32_t seed = 1; nsys < 150 && seed < 3000; seed++) {
        gen_system(&s, seed);
        tried++;
        if (sys_check(&s) != 0) continue;
        analyze(&s);
        if (!all_ext_ok(&s)) {
            /* 확장 분석이 거부한 시스템 중 순진한 분석은 통과시키는데 실제로 미스가 나는가? */
            int naive_all = 1;
            for (int i = 0; i < s.n; i++) if (!s.e[i].is_isr && !rta_naive(&s, i).ok) naive_all = 0;
            if (naive_all) {
                int missed = 0;
                for (int i = 0; i < s.n && !missed; i++) {
                    gen_critical(&s, i, 2 * (int32_t)s.e[i].T, &g_pat); sim_run(&s, &g_pat, &o, NULL);
                    if (o.misses[i]) missed = 1;
                }
                nmiss_ok_naive += missed;
            }
            continue;
        }
        nsys++;
        uint32_t maxT = 0;
        for (int i = 0; i < s.n; i++) if (s.e[i].T > maxT) maxT = s.e[i].T;
        sim_all(&s, 10, seed * 31u, 6 * (int32_t)maxT, mo, mb, &ms, &cf, &uf);
        int bad = (ms != 0 || cf != 0 || uf != 0);
        for (int i = 0; i < s.n; i++) {
            if (mo[i] > rta_ext(&s, i).R || mb[i] > s.e[i].B) bad = 1;
            if (!s.e[i].is_isr && mo[i] > rta_naive(&s, i).R) nnaive_bad++;
        }
        nviol += bad;
        for (int i = 0; i < s.n; i++) {
            gen_critical(&s, i, 2 * (int32_t)s.e[i].T, &g_pat); sim_run(&s, &g_pat, &o, NULL);
            if (self_masks(&s, i)) {                         /* 자기 cs 천장이 자기보다 높다 → 간섭이 밀린다 */
                nself++;
                if (o.first_resp[i] < rta_discrete(&s, i).R) nself_less++;
                if (o.first_resp[i] > rta_discrete(&s, i).R) nself_over++;
                continue;
            }
            nadv++;
            if (o.first_resp[i] == rta_discrete(&s, i).R) nadv_exact++;
        }
    }
    CHECK(nsys == 150);
    CHECK(nviol == 0);                                                /* 안전: 관측 <= R, 블로킹 <= B */
    CHECK(nadv > 0 && nadv_exact == nadv);                            /* 최악 패턴 == 이산 시간 분석 */
    CHECK(nself > 0 && nself_over == 0 && nself_less > 0);            /* 자기 천장이 높으면 분석이 비관적 */
    CHECK(nnaive_bad > 0);                                            /* 순진한 RTA 는 자주 낙관적 */
    CHECK(nmiss_ok_naive > 0);                                        /* "순진 통과" 인데 실제 미스 */
    if (g_verbose)
        printf("random systems: tried %d, ext-pass %d, violations %d, critical==analysis %d/%d, "
               "tasks with obs > naive R %d, naive-pass but real miss %d\n"
               "self-ceiling targets %d: obs < analysis in %d, obs > analysis in %d\n",
               tried, nsys, nviol, nadv_exact, nadv, nnaive_bad, nmiss_ok_naive, nself, nself_less, nself_over);

    /* ===== Part C: 펌웨어 시스템 ===== */
    build_c(&s, 0);
    CHECK(sys_check(&s) == 0);
    analyze(&s);
    int can = find_ent(&s, "can"), ctrl = find_ent(&s, "ctrl"), imu = find_ent(&s, "imu"),
        lg = find_ent(&s, "log"), adc = find_ent(&s, "adc"), tick = find_ent(&s, "systick");
    int naive_all = 1;
    for (int i = 0; i < s.n; i++) if (!s.e[i].is_isr && !rta_naive(&s, i).ok) naive_all = 0;
    CHECK(naive_all);                                                  /* 순진한 분석: 전부 통과 */
    CHECK(s.e[can].B == 45 && s.e[adc].B == 0 && s.e[imu].B == 500 && s.e[ctrl].B == 45);
    CHECK(!rta_ext(&s, can).ok && !rta_ext(&s, imu).ok && rta_ext(&s, ctrl).ok);
    if (g_verbose) print_table("C1 firmware, as written (us)", &s);
    gen_critical(&s, can, 400, &g_pat); sim_run(&s, &g_pat, &o, NULL);
    CHECK(o.misses[can] >= 1 && o.first_resp[can] > s.e[can].D);
    uint32_t can_obs = o.first_resp[can];
    gen_critical(&s, imu, 2000, &g_pat); sim_run(&s, &g_pat, &o, NULL);
    CHECK(o.misses[imu] >= 1 && o.first_resp[imu] > s.e[imu].D);
    uint32_t imu_obs = o.first_resp[imu];
    if (g_verbose) printf("   C1 critical-pattern sim: can first response %u (D=%u), imu %u (D=%u)\n",
                          can_obs, s.e[can].D, imu_obs, s.e[imu].D);

    build_c(&s, 1);
    CHECK(sys_check(&s) == 0);
    analyze(&s);
    CHECK(s.e[can].B == 6 && s.e[imu].B == 100 && s.e[ctrl].B == 45 && s.e[tick].B == 6);
    CHECK(all_ext_ok(&s));
    if (g_verbose) print_table("C2 fixed: SCHED lock + 100 us flash chunks (us)", &s);
    sim_all(&s, 40, 2024, 40000, mo, mb, &ms, &cf, &uf);
    safe = 1; int exact = 1;
    for (int i = 0; i < s.n; i++) {
        if (mo[i] > rta_ext(&s, i).R || mb[i] > s.e[i].B) safe = 0;
        gen_critical(&s, i, (int32_t)(s.e[i].T + s.e[i].D), &g_pat); sim_run(&s, &g_pat, &o, NULL);
        if (o.done[i] == 0 || o.first_resp[i] != rta_discrete(&s, i).R) exact = 0;
    }
    CHECK(safe && ms == 0 && cf == 0 && uf == 0 && mo[lg] > 0);
    CHECK(exact);
    sim_random_max(&s, 40, 2024, 40000, rx);
    CHECK(rx[imu] < mo[imu] && rx[ctrl] < mo[ctrl]);                  /* 무작위 40 회 (각 40 ms) 도 못 만난다 */
    if (g_verbose) {
        printf("   C2 random-only max:");
        for (int i = 0; i < s.n; i++) printf(" %s %u", s.e[i].name, rx[i]);
        printf("\n");
        printf("   C2 sim (40 random x 40 ms + critical): max obs / R:");
        for (int i = 0; i < s.n; i++) printf(" %s %u/%u", s.e[i].name, mo[i], rta_ext(&s, i).R);
        printf("\n");
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
