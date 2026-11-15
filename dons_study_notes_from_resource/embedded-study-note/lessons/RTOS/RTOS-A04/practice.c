/* RTOS-A04 추가 연습 — 확장 RTA 의 민감도, 버스트 ISR, 오버헤드, PI 블로킹 상한
 *
 * 본문 solution.c 의 분석기·시뮬레이터를 그대로 복사해 쓴다 (레슨 파일은 각자 독립).
 * 다른 점: ent_t 에 버스트 도착 필드 (bk, bP) 를 더했고 rta_ext() 가 그것을 쓴다 (연습 2).
 *  연습 1  max_spi_chunk / max_kernel_crit : 이분 탐색으로 "임계 구역을 얼마나 길게 해도 되나"
 *  연습 2  eta_burst  : CAN 버스트 도착 (4 프레임 / 10 ms, 최소 간격 100 µs) — 산발적 vs 버스트 vs 평균율
 *  연습 3  문맥 전환 비용 cs (태스크 작업당 2회) + ISR 진입/복귀 1 µs 를 넣었을 때 견딜 수 있는 최대 cs
 *  연습 4  blocking_mixed : FreeRTOS 처럼 뮤텍스는 PI, 커널 임계 구역은 천장일 때의 보수적 B
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice [-v]
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
    uint32_t bk, bP;             /* 연습 2: 0 이 아니면 "길이 bP 창마다 최대 bk 번" 버스트 도착 (T = 최소 간격) */
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
/* ================= 본문 TODO 1: 자원 천장 ================= */
static void compute_ceilings(sys_t *s) {
    for (int r = 0; r < s->nres; r++) s->ceil[r] = s->rmin[r];
    for (int i = 0; i < s->n; i++)
        for (int k = 0; k < s->e[i].ncs; k++) {
            int r = s->e[i].cs[k].res;
            if (s->e[i].prio > s->ceil[r]) s->ceil[r] = s->e[i].prio;
        }
}

/* ================= 본문 TODO 2: IPCP 블로킹 항 ================= */
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

/* ================= 본문 TODO 3: 확장 RTA ================= */
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
            uint64_t cnt = (w + ej->J + ej->T - 1) / ej->T;
            if (ej->bk) {                                       /* 연습 2: 버스트 상한과 비교해 작은 쪽 */
                uint64_t cb = (uint64_t)ej->bk * ((w + ej->J + ej->bP - 1) / ej->bP);
                if (cb < cnt) cnt = cb;
            }
            nw += cnt * ej->C;
        }
        if (nw + ei->J > ei->D) { r.R = (uint32_t)(nw + ei->J); return r; }   /* 멈춘 값: 하한일 뿐 */
        if (nw == w) { r.ok = 1; r.R = (uint32_t)(nw + ei->J); return r; }
        w = nw;
    }
}

/* ================= 본문 TODO 4: 이산 시간 시뮬레이터 =================
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

/* ================= 본문 TODO 5: 최악 출시 패턴 =================
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

/* ---------------- 연습 하네스 ---------------- */
static int g_pass, g_total, g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static pattern_t g_pat;
static void analyze(sys_t *s) { compute_ceilings(s); blocking_ipcp(s); }
static int all_ext_ok(const sys_t *s) {
    for (int i = 0; i < s->n; i++) if (!rta_ext(s, i).ok) return 0;
    return 1;
}
static int count_misses(const sys_t *s, int nrand, int32_t horizon, uint32_t *maxobs) {
    obs_t o; int m = 0;
    for (int i = 0; i < s->n; i++) maxobs[i] = 0;
    for (int run = 0; run < nrand + s->n; run++) {
        if (run < nrand) gen_random(s, 77u + (uint32_t)run * 7919u, horizon, &g_pat);
        else gen_critical(s, run - nrand, (int32_t)(s->e[run - nrand].T + s->e[run - nrand].D), &g_pat);
        sim_run(s, &g_pat, &o, NULL);
        for (int i = 0; i < s->n; i++) {
            m += o.misses[i] + (o.ipcp_conflicts ? 1000 : 0);
            if (o.max_resp[i] > maxobs[i]) maxobs[i] = o.max_resp[i];
        }
    }
    return m;
}

/* 본문 Part C 의 고친 시스템. log 의 SPI 조각 길이 spi_len (조각 1개), log 의 짧은 구간을
 * kern_len > 0 이면 taskENTER_CRITICAL(KERNEL) 로, 0 이면 vTaskSuspendAll(SCHED) 45 µs 로.
 * cs_ovh = 문맥 전환 1회 비용 (태스크 C 에 2회), isr_ovh = 예외 진입+복귀 비용 (ISR C 에 1회). */
static void build_c(sys_t *s, uint32_t spi_len, uint32_t kern_len, uint32_t cs_ovh, uint32_t isr_ovh) {
    sys_init(s);
    int kern = add_res(s, "KERNEL", 12), sched = add_res(s, "SCHED", 4);
    int spi = add_res(s, "SPI", 0), param = add_res(s, "PARAM", 0);
    uint32_t t2 = 2 * cs_ovh;
    add_ent(s, "adc", 1, 15, 6 + isr_ovh, 50, 50, 0);
    int can = add_ent(s, "can", 1, 12, 9 + isr_ovh, 160, 40, 0);
    int tick = add_ent(s, "systick", 1, 11, 5 + isr_ovh, 1000, 1000, 0);
    int ctrl = add_ent(s, "ctrl", 0, 4, 120 + t2, 1000, 400, 30);
    int imu = add_ent(s, "imu", 0, 3, 200 + t2, 2000, 1000, 30);
    int comms = add_ent(s, "comms", 0, 2, 300 + t2, 5000, 3000, 0);
    int lg = add_ent(s, "log", 0, 1, 1500 + t2, 20000, 20000, 0);
    add_cs(s, can, kern, 2, 3);
    add_cs(s, tick, kern, 0, 5);
    add_cs(s, ctrl, param, 10, 10);
    add_cs(s, ctrl, kern, 100, 4);
    add_cs(s, imu, spi, 20, 60);
    add_cs(s, comms, param, 50, 15);
    add_cs(s, comms, kern, 100, 6);
    if (kern_len) add_cs(s, lg, kern, 100, kern_len); else add_cs(s, lg, sched, 100, 45);
    add_cs(s, lg, spi, 200, spi_len);
}

/* ===== 연습 1: 민감도 — 임계 구역을 얼마나 길게 해도 되나 (이분 탐색) ===== */
static uint32_t max_spi_chunk(void) {
    sys_t s; uint32_t lo = 0, hi = 1300;            /* lo = 통과하는 최대 (0 = 없음), hi 는 실패 가정 */
    while (hi - lo > 1) {
        uint32_t mid = lo + (hi - lo) / 2;
        build_c(&s, mid, 0, 0, 0); analyze(&s);
        if (all_ext_ok(&s)) lo = mid; else hi = mid;
    }
    return lo;
}
static uint32_t max_kernel_crit(void) {
    sys_t s; uint32_t lo = 0, hi = 200;
    while (hi - lo > 1) {
        uint32_t mid = lo + (hi - lo) / 2;
        build_c(&s, 100, mid, 0, 0); analyze(&s);
        if (all_ext_ok(&s)) lo = mid; else hi = mid;
    }
    return lo;
}

/* ===== 연습 2: 버스트 도착 ISR — eta(w) = min(ceil(w/t_min), k*ceil(w/P)) ===== */
static uint32_t eta_burst(uint32_t w, uint32_t tmin, uint32_t k, uint32_t P) {
    uint32_t a = (w + tmin - 1) / tmin, b = k * ((w + P - 1) / P);
    return a < b ? a : b;
}
/* model 0: 버스트 모델 (can 의 T = t_min, k, P), 1: 산발적 T = t_min, 2: 평균율 T = P/k (위험) */
static void build_burst(sys_t *s, int model) {
    sys_init(s);
    uint32_t T = model == 2 ? 2500 : 100;
    int can = add_ent(s, "can", 1, 12, 30, T, 100, 0);
    if (model == 0) { s->e[can].bk = 4; s->e[can].bP = 10000; }
    add_ent(s, "ctrl", 0, 4, 300, 1000, 500, 0);
    add_ent(s, "proto", 0, 3, 400, 5000, 900, 0);
}
/* 실제 도착: 연속 간격 >= 100, 그리고 길이 10000 창마다 최대 4개 */
static void gen_burst_random(const sys_t *s, uint32_t seed, int32_t horizon, pattern_t *p) {
    gen_random(s, seed, horizon, p);
    jobs_t *jb = &p->j[0]; jb->n = 0;
    int32_t a = (int32_t)(rnd() % 3000);
    while (a < horizon && jb->n < MAXJOB) {
        if (jb->n >= 4 && a < jb->arr[jb->n - 4] + 10000) a = jb->arr[jb->n - 4] + 10000;
        push_job(jb, a, a);
        a += 100 + ((rnd() & 1u) ? 0 : (int32_t)(rnd() % 400));
    }
}
static void gen_burst_critical(const sys_t *s, int i, pattern_t *p) {
    gen_critical(s, i, 12000, p);                   /* B = 0 이라 shift = 0 */
    jobs_t *jb = &p->j[0]; jb->n = 0;
    for (int b = 0; b < 2; b++)
        for (int k = 0; k < 4; k++) push_job(jb, b * 10000 + k * 100, b * 10000 + k * 100);
}

/* ===== 연습 4: FreeRTOS 현실 — 뮤텍스는 PI, 커널 임계 구역/스케줄러 잠금은 천장 =====
 * 보수적 상한: B_i = (천장형 구간 중 최대 1개) + min( 하위 entity 마다 PI 구간 최대값의 합,
 *                                                    PI 자원마다 하위 구간 최대값의 합 )   (Sha et al. 1990) */
static uint32_t blocking_mixed(const sys_t *s, int i, const int *is_pi) {
    uint32_t bceil = 0, sum_l = 0, sum_r = 0;
    for (int l = 0; l < s->n; l++) {
        if (s->e[l].prio >= s->e[i].prio) continue;
        uint32_t ml = 0;
        for (int k = 0; k < s->e[l].ncs; k++) {
            const cs_t *c = &s->e[l].cs[k];
            if (s->ceil[c->res] < s->e[i].prio) continue;
            if (is_pi[c->res]) { if (c->len > ml) ml = c->len; }
            else if (c->len > bceil) bceil = c->len;
        }
        sum_l += ml;
    }
    for (int r = 0; r < s->nres; r++) {
        if (!is_pi[r] || s->ceil[r] < s->e[i].prio) continue;
        uint32_t mr = 0;
        for (int l = 0; l < s->n; l++) {
            if (s->e[l].prio >= s->e[i].prio) continue;
            for (int k = 0; k < s->e[l].ncs; k++)
                if (s->e[l].cs[k].res == r && s->e[l].cs[k].len > mr) mr = s->e[l].cs[k].len;
        }
        sum_r += mr;
    }
    return bceil + (sum_l < sum_r ? sum_l : sum_r);
}

int main(int argc, char **argv) {
    g_verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);
    sys_t s; obs_t o; uint32_t mo[MAXE];

    /* 연습 1 */
    uint32_t L = max_spi_chunk(), K = max_kernel_crit();
    build_c(&s, L, 0, 0, 0); analyze(&s);
    CHECK(sys_check(&s) == 0 && all_ext_ok(&s));
    int imu = find_ent(&s, "imu"), can = find_ent(&s, "can");
    uint32_t r_imu_L = rta_ext(&s, imu).R;
    CHECK(count_misses(&s, 20, 40000, mo) == 0);
    build_c(&s, L + 1, 0, 0, 0); analyze(&s);
    CHECK(!all_ext_ok(&s) && !rta_ext(&s, imu).ok);
    gen_critical(&s, imu, 3000, &g_pat); sim_run(&s, &g_pat, &o, NULL);
    uint32_t imu_L1 = o.first_resp[imu];
    build_c(&s, L + 2, 0, 0, 0); analyze(&s);
    gen_critical(&s, imu, 3000, &g_pat); sim_run(&s, &g_pat, &o, NULL);
    uint32_t imu_L2 = o.first_resp[imu];
    CHECK(imu_L1 == s.e[imu].D && imu_L2 > s.e[imu].D);                     /* 2 µs 넘기면 시뮬레이션도 미스 */
    CHECK(L == 462 && K == 25);
    build_c(&s, 100, K, 0, 0); analyze(&s);
    CHECK(all_ext_ok(&s) && rta_ext(&s, can).R <= 40);
    build_c(&s, 100, K + 1, 0, 0); analyze(&s);
    CHECK(!rta_ext(&s, can).ok);
    if (g_verbose)
        printf("ex1 max SPI chunk %u us (imu R=%u, D=1000); chunk+1 -> imu crit sim %u, chunk+2 -> %u | "
               "max taskENTER_CRITICAL in log %u us\n", L, r_imu_L, imu_L1, imu_L2, K);

    /* 연습 2 */
    CHECK(eta_burst(1, 100, 4, 10000) == 1 && eta_burst(301, 100, 4, 10000) == 4 &&
          eta_burst(9999, 100, 4, 10000) == 4 && eta_burst(10001, 100, 4, 10000) == 8);
    rta_t rb, rs, ra;
    build_burst(&s, 0); analyze(&s); rb = rta_ext(&s, 2);
    CHECK(sys_check(&s) == 0 && rta_naive(&s, 2).R == 700);  /* ISR 을 빼면 700 */
    build_burst(&s, 1); analyze(&s); rs = rta_ext(&s, 2);
    build_burst(&s, 2); analyze(&s); ra = rta_ext(&s, 2);
    CHECK(rb.ok && rb.R == 820);                    /* 버스트 모델: 통과 */
    CHECK(!rs.ok);                                  /* 산발적 T = t_min: 너무 비관적 → 거부 */
    CHECK(ra.ok && ra.R == 730);                    /* 평균율: 통과라고 하지만... */
    build_burst(&s, 0); analyze(&s);
    gen_burst_critical(&s, 2, &g_pat); sim_run(&s, &g_pat, &o, NULL);
    CHECK(o.first_resp[2] == 820 && o.first_resp[2] > ra.R);   /* 실제 최악 820 > 평균율 730: 위험 */
    uint32_t worst = 0; int bmiss = 0;
    for (uint32_t seed = 1; seed <= 60; seed++) {
        gen_burst_random(&s, seed, 60000, &g_pat); sim_run(&s, &g_pat, &o, NULL);
        for (int i = 0; i < s.n; i++) bmiss += o.misses[i];
        if (o.max_resp[2] > worst) worst = o.max_resp[2];
    }
    CHECK(bmiss == 0 && worst <= rb.R);
    if (g_verbose)
        printf("ex2 proto R: burst model %u ok | sporadic t_min %s (stopped %u) | average-rate %u ok (UNSAFE) | "
               "critical burst sim %u, random 60 runs max %u\n",
               rb.R, rs.ok ? "ok" : "MISS", rs.R, ra.R, 820u, worst);

    /* 연습 3: 문맥 전환 + ISR 진입/복귀 오버헤드 */
    uint32_t best = 0;
    for (uint32_t cs = 0; cs <= 100; cs++) {
        build_c(&s, 100, 0, cs, 1); analyze(&s);
        if (all_ext_ok(&s)) best = cs; else break;
    }
    build_c(&s, 100, 0, best, 1); analyze(&s);
    CHECK(all_ext_ok(&s) && count_misses(&s, 10, 40000, mo) == 0);
    build_c(&s, 100, 0, best + 1, 1); analyze(&s);
    int which = -1;
    for (int i = 0; i < s.n; i++) if (!rta_ext(&s, i).ok) { which = i; break; }
    CHECK(best == 56 && which == find_ent(&s, "ctrl"));
    if (g_verbose) printf("ex3 max context-switch cost %u us (2 per job, ISR +1 us); first to fail at +1: %s\n",
                          best, which >= 0 ? s.e[which].name : "-");

    /* 연습 4: PI 혼합 상한 */
    build_c(&s, 100, 0, 0, 0); analyze(&s);
    int is_pi[MAXR] = {0, 0, 1, 1};                 /* KERNEL, SCHED = 천장형; SPI, PARAM = PI 뮤텍스 */
    uint32_t bi[MAXE]; int all_pi_ok = 1;
    for (int i = 0; i < s.n; i++) bi[i] = blocking_mixed(&s, i, is_pi);
    int ctrl = find_ent(&s, "ctrl"), comms = find_ent(&s, "comms");
    CHECK(bi[ctrl] == 45 + 15 && bi[imu] == 45 + 115 && bi[comms] == 45 + 100 && bi[can] == 6);
    for (int i = 0; i < s.n; i++) {
        uint32_t ipcp = s.e[i].B;
        s.e[i].B = bi[i];
        rta_t r = rta_ext(&s, i);
        if (!r.ok) all_pi_ok = 0;
        if (g_verbose) printf("ex4 %-7s B ipcp %3u -> PI-mixed %3u, R %4u %s\n", s.e[i].name, ipcp, bi[i], r.R,
                              r.ok ? "ok" : "MISS");
        s.e[i].B = ipcp;
    }
    CHECK(all_pi_ok);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
