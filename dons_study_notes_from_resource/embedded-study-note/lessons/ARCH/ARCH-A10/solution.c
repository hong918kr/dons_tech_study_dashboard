/* ARCH-A10 버스 중재: 고정 우선순위 vs 라운드로빈
 *
 * 사이클 단위 모델: 공유 자원(버스 / 메모리 포트) 하나, 마스터 3~4 개(CPU · DMA · NPU (+ CAM)).
 *   - 마스터마다 요청 큐(FIFO, QCAP 개). 요청 = {도착 사이클, 버스트 길이 L}.
 *   - 그랜트를 받으면 자원을 L 사이클 연속으로 쓴다 (비선점, non-preemptive). 1 beat = 1 사이클.
 *   - 한 사이클의 순서: (1) 도착 생성 → (2) 토큰 버킷 충전 → (3) 자원이 비었고 요청이 있으면 중재 + 그랜트
 *                     → (4) 자원 점유/유휴 기록. 도착한 사이클에 바로 그랜트될 수 있다 (중재 지연 0 가정).
 *
 *   Part A  arb_fixed (고정 우선순위) · arb_rr (라운드로빈) 구현 (TODO).
 *           검사: 지시형 트레이스 문자열, work-conserving (요청이 있는데 자원이 노는 사이클 = 0),
 *           RR 공정성 상한 (HOL 대기 ≤ 다른 마스터 최대 버스트의 합, 그리고 그 상한이 빡빡함).
 *   Part B  arb_drr (가중 RR = beat 단위 Deficit Round Robin) 구현 (TODO).
 *           검사: RR 은 "그랜트" 공정이라 대역폭은 L 비례, DRR 은 대역폭이 가중치 비례 (허용오차 유도),
 *           부하 스윕 표 (가벼움 → 포화), 고정 우선순위의 기아.
 *   Part C  토큰 버킷(tb_*) + arb_qos (데드라인 긴급 승격 + 대역폭 규제기) 구현 (TODO).
 *           검사: 카메라형 실시간 마스터(데드라인) + NPU 대량 전송에서 정책별 데드라인 미스 · 기아 비교.
 *
 * 지표 정의 (요청 하나 기준):
 *   wait  = grant − arrival                     (도착 → 그랜트 시작)
 *   total = wait + L                            (도착 → 완료 = 마지막 beat 다음 사이클)
 *   HOL   = grant − max(arrival, 자기 직전 버스트 끝)   (큐 머리에서, 자기 전송 중이 아닐 때 기다린 시간)
 *   starve = 어떤 요청의 HOL 이 STARVE_CYC 이상 (끝까지 못 받은 요청은 시뮬레이션 끝 시점까지로 계산)
 *   offered / achieved = 도착한 beat / [0,T) 안에서 받은 beat, 둘 다 ÷ T (beat/cycle)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so      (starter.c 도 같은 명령)
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXM 4
#define QCAP 512                 /* 마스터당 요청 큐 (넘치면 drop = 마스터가 멈춘 것으로 본다) */
#define HMAX 65536               /* 지연 히스토그램 칸 (그 이상은 마지막 칸에 포화, max 는 따로 정확히) */
#define TRACE_N 64               /* 처음 64 사이클의 점유자를 문자열로 남긴다 ('.' = 유휴) */
#define STARVE_CYC 4096

typedef enum { TG_OFF, TG_BERN, TG_STREAM, TG_BACKLOG, TG_SCRIPT } tg_t;
typedef struct { int64_t t; int len; } ev_t;
typedef struct {
    const char *name;
    tg_t kind;
    double rate;        /* BERN: 사이클당 도착 확률 · STREAM: 사이클당 beat (일정한 속도) */
    int batch;          /* BERN: 도착 한 번에 들어오는 요청 수 (DMA 디스크립터 = 버스트 여러 개) */
    int len;            /* 요청 하나의 버스트 길이 L (beat) */
    double phase;       /* STREAM: 누적기 초기값 (beat) */
    int deadline;       /* 0 = 없음. total > deadline 이면 miss */
    const ev_t *script; int nscript;   /* SCRIPT: {t, len} 목록 (t 오름차순) */
} mcfg_t;

typedef struct { int64_t arr; int len; } req_t;
typedef struct { double tokens, rate, cap; } tb_t;   /* 토큰 단위 = beat */

typedef struct {
    req_t q[QCAP]; int qh, qn;
    double acc; int sp;
    int64_t last_end;
    int64_t arrivals, dropped, offered, grants, served, lmax;
    int64_t sum_wait, max_wait, max_tot, max_hol, misses;
    uint32_t hw[HMAX];
} mst_t;

typedef enum { ARB_FIXED, ARB_RR, ARB_DRR, ARB_QOS, ARB_EXT } arb_t;
static const char *const ARB_NAME[] = {"FP", "RR", "DRR", "QOS", "EXT"};

typedef struct sim {
    int nm; mcfg_t cfg[MAXM]; mst_t m[MAXM];
    arb_t arb;
    int order[MAXM];                           /* FIXED: order[0] 이 최고 우선순위 */
    int rr_last;                               /* RR / QOS: 마지막으로 RR 그랜트를 받은 마스터 */
    int drr_cur; bool drr_turn;                /* DRR: 지금 차례인 마스터, 이번 차례에 quantum 을 이미 받았나 */
    int64_t quantum[MAXM], dc[MAXM];           /* DRR: 차례마다 더하는 beat 수, deficit counter */
    int qos[MAXM];                             /* QOS: 기본 레벨 (클수록 높음, AxQOS 처럼 0..15) */
    int urg_after[MAXM];                       /* QOS: 머리 요청 나이 ≥ 이 값이면 긴급 (-1 = 승격 없음) */
    bool regulated[MAXM]; tb_t tb[MAXM];       /* QOS: 대역폭 규제 대상과 그 토큰 버킷 */
    bool hard_reg;                             /* QOS: true = 토큰 없으면 자원이 놀아도 안 줌 */
    int (*ext)(struct sim *);                  /* ARB_EXT: 연습용 중재기 */
    int64_t t, T, busy_until; int owner;
    int64_t busy, idle, wc_viol, bad_grant, q_area;
    uint32_t rng;
    char trace[TRACE_N + 1];
} sim_t;

static bool pending(const sim_t *s, int m) { return s->m[m].qn > 0; }
static const req_t *head(const sim_t *s, int m) { return &s->m[m].q[s->m[m].qh]; }
static int head_len(const sim_t *s, int m) { return head(s, m)->len; }
static int64_t head_age(const sim_t *s, int m) { return s->t - head(s, m)->arr; }

/* ======================= Part A: 고정 우선순위 · 라운드로빈 ======================= */

/* 고정 우선순위: s->order[0] 이 가장 높다. 요청이 있는 마스터 중 가장 높은 것. 없으면 -1. */
static int arb_fixed(const sim_t *s) {
    for (int k = 0; k < s->nm; k++)
        if (pending(s, s->order[k])) return s->order[k];
    return -1;
}

/* 라운드로빈: 마지막 그랜트(s->rr_last) 다음 번호부터 한 바퀴 돌며 요청이 있는 첫 마스터.
 * 그랜트하면 s->rr_last 를 갱신한다. 없으면 -1. */
static int arb_rr(sim_t *s) {
    for (int k = 1; k <= s->nm; k++) {
        int m = (s->rr_last + k) % s->nm;
        if (pending(s, m)) { s->rr_last = m; return m; }
    }
    return -1;
}

/* ======================= Part B: 가중 RR = beat 단위 DRR ======================= */

/* Deficit Round Robin (Shreedhar & Varghese 1995 을 비선점 버스 그랜트에 맞춤).
 *   - 차례(turn)는 s->drr_cur. 차례가 시작될 때(drr_turn == false) dc[cur] += quantum[cur], drr_turn = true.
 *   - 머리 요청 L ≤ dc[cur] 이면 그 마스터에게 그랜트, dc[cur] -= L, 차례는 유지 (다음 중재에서 또 볼 수 있음).
 *   - 아니면 차례 끝: 큐가 비었으면 dc[cur] = 0 (안 쓴 몫은 저축 불가), drr_turn = false, cur = 다음 마스터.
 *   - 중재기는 요청이 하나라도 있을 때만 불린다 → 반드시 누군가를 찾을 때까지 돈다 (work-conserving). */
static int arb_drr(sim_t *s) {
    for (int guard = 0; guard < 1000000; guard++) {
        int m = s->drr_cur;
        if (!s->drr_turn) { s->dc[m] += s->quantum[m]; s->drr_turn = true; }
        if (pending(s, m) && head_len(s, m) <= s->dc[m]) { s->dc[m] -= head_len(s, m); return m; }
        if (!pending(s, m)) s->dc[m] = 0;
        s->drr_turn = false;
        s->drr_cur = (m + 1) % s->nm;
    }
    return -1;
}

/* ======================= Part C: 토큰 버킷 규제기 + QoS 중재 ======================= */

/* 매 사이클: tokens += rate, 단 cap 을 넘지 않는다. */
static void tb_tick(tb_t *b) {
    b->tokens += b->rate;
    if (b->tokens > b->cap) b->tokens = b->cap;
}
/* 버스트 len 을 보낼 토큰이 있나. */
static bool tb_ok(const tb_t *b, int len) { return b->tokens >= (double)len; }
/* 그랜트되면 len 만큼 뺀다. 모자라면 0 으로 (soft 모드에서 토큰 없이 받은 경우 — 빚은 지지 않는다). */
static void tb_charge(tb_t *b, int len) { b->tokens = (b->tokens >= (double)len) ? b->tokens - len : 0.0; }

/* QoS 중재 (AXI QoS 값처럼 "레벨" + 동적 조정):
 *   1) 긴급 승격: deadline > 0 이고 urg_after[m] ≥ 0 이며 머리 요청 나이 ≥ urg_after[m] 인 마스터들 중
 *      절대 마감(arrival + deadline)이 가장 이른 것 (EDF, 같으면 번호 작은 것). RR 포인터는 안 움직인다.
 *   2) 그 외: 마스터마다 유효 레벨 = qos[m]. 규제 대상(regulated)인데 !tb_ok 이면
 *      hard_reg ? 후보에서 제외 : 레벨 0 으로 강등 (soft — 아무도 원하지 않는 대역폭만 쓴다).
 *      요청이 있는 후보 중 유효 레벨이 가장 높은 것, 같은 레벨은 rr_last 다음부터 RR. 그랜트하면 rr_last 갱신.
 *   후보가 없으면 -1 (hard_reg 일 때만 생긴다). */
static int arb_qos(sim_t *s) {
    int best = -1; int64_t best_dl = 0;
    for (int m = 0; m < s->nm; m++) {
        if (!pending(s, m) || s->cfg[m].deadline <= 0 || s->urg_after[m] < 0) continue;
        if (head_age(s, m) < s->urg_after[m]) continue;
        int64_t dl = head(s, m)->arr + s->cfg[m].deadline;
        if (best < 0 || dl < best_dl) { best = m; best_dl = dl; }
    }
    if (best >= 0) return best;
    int best_lvl = -1;
    for (int k = 1; k <= s->nm; k++) {
        int m = (s->rr_last + k) % s->nm;
        if (!pending(s, m)) continue;
        int lvl = s->qos[m];
        if (s->regulated[m] && !tb_ok(&s->tb[m], head_len(s, m))) {
            if (s->hard_reg) continue;
            lvl = 0;
        }
        if (lvl > best_lvl) { best_lvl = lvl; best = m; }    /* 엄격히 클 때만 → 같은 레벨은 RR 순서상 먼저 것 */
    }
    if (best >= 0) s->rr_last = best;
    return best;
}

/* ======================= 시뮬레이터 (주어짐) ======================= */

static uint32_t xs32(uint32_t *x) { uint32_t v = *x; v ^= v << 13; v ^= v >> 17; v ^= v << 5; return *x = v; }
static double urand(sim_t *s) { return xs32(&s->rng) / 4294967296.0; }

static void sim_init(sim_t *s, int nm, const mcfg_t *cfg, arb_t arb, int64_t T, uint32_t seed) {
    memset(s, 0, sizeof *s);
    s->nm = nm; s->arb = arb; s->T = T; s->rng = seed ? seed : 1u;
    s->rr_last = nm - 1; s->owner = -1;
    for (int m = 0; m < nm; m++) {
        s->cfg[m] = cfg[m]; s->order[m] = m; s->quantum[m] = 16; s->urg_after[m] = -1;
        s->m[m].acc = cfg[m].phase;
    }
    memset(s->trace, '.', TRACE_N); s->trace[TRACE_N] = 0;
}

static void enqueue(sim_t *s, int m, int len) {
    mst_t *M = &s->m[m];
    M->arrivals++; M->offered += len;
    if (len > M->lmax) M->lmax = len;
    if (M->qn == QCAP) { M->dropped++; return; }
    M->q[(M->qh + M->qn) % QCAP] = (req_t){ s->t, len };
    M->qn++;
}

static void gen_arrivals(sim_t *s) {
    for (int m = 0; m < s->nm; m++) {
        const mcfg_t *c = &s->cfg[m]; mst_t *M = &s->m[m];
        switch (c->kind) {
        case TG_BERN:
            if (urand(s) < c->rate) for (int b = 0; b < c->batch; b++) enqueue(s, m, c->len);
            break;
        case TG_STREAM:
            M->acc += c->rate;
            while (M->acc >= c->len) { enqueue(s, m, c->len); M->acc -= c->len; }
            break;
        case TG_BACKLOG:
            if (M->qn == 0) enqueue(s, m, c->len);
            break;
        case TG_SCRIPT:
            while (M->sp < c->nscript && c->script[M->sp].t == s->t) enqueue(s, m, c->script[M->sp++].len);
            break;
        default: break;
        }
    }
}

static int arbitrate(sim_t *s) {
    switch (s->arb) {
    case ARB_FIXED: return arb_fixed(s);
    case ARB_RR:    return arb_rr(s);
    case ARB_DRR:   return arb_drr(s);
    case ARB_QOS:   return arb_qos(s);
    case ARB_EXT:   return s->ext ? s->ext(s) : -1;
    }
    return -1;
}

static void grant(sim_t *s, int g) {
    mst_t *M = &s->m[g];
    req_t r = M->q[M->qh];
    M->qh = (M->qh + 1) % QCAP; M->qn--;
    int64_t wait = s->t - r.arr, tot = wait + r.len;
    int64_t hs = r.arr > M->last_end ? r.arr : M->last_end;
    int64_t hol = s->t - hs;
    M->grants++; M->sum_wait += wait;
    M->hw[wait < HMAX ? wait : HMAX - 1]++;
    if (wait > M->max_wait) M->max_wait = wait;
    if (tot > M->max_tot) M->max_tot = tot;
    if (hol > M->max_hol) M->max_hol = hol;
    if (s->cfg[g].deadline > 0 && tot > s->cfg[g].deadline) M->misses++;
    M->last_end = s->t + r.len;
    s->busy_until = s->t + r.len; s->owner = g;
    if (s->regulated[g]) tb_charge(&s->tb[g], r.len);
}

static bool any_pending(const sim_t *s) {
    for (int m = 0; m < s->nm; m++) if (pending(s, m)) return true;
    return false;
}

static void sim_step(sim_t *s, bool arrivals_on) {
    if (arrivals_on) gen_arrivals(s);
    for (int m = 0; m < s->nm; m++) if (s->regulated[m]) tb_tick(&s->tb[m]);
    bool any = any_pending(s);
    if (s->t >= s->busy_until && any) {
        int g = arbitrate(s);
        if (g >= 0 && g < s->nm && pending(s, g)) grant(s, g);
        else { if (g >= 0) s->bad_grant++; s->wc_viol++; }   /* 요청이 있는데 아무도 못 받음 */
    }
    bool busy = s->t < s->busy_until;
    if (busy) { s->busy++; if (s->t < s->T) s->m[s->owner].served++; } else s->idle++;
    if (s->t < TRACE_N) s->trace[s->t] = busy ? (char)('0' + s->owner) : '.';
    for (int m = 0; m < s->nm; m++) s->q_area += s->m[m].qn;
    s->t++;
}

/* T 사이클 실행. drain 이면 그 뒤 도착을 끊고 모든 큐가 비고 자원이 놀 때까지 더 돈다. */
static void sim_run(sim_t *s, bool drain) {
    while (s->t < s->T) sim_step(s, true);
    if (drain) while (any_pending(s) || s->t < s->busy_until) sim_step(s, false);
    for (int m = 0; m < s->nm; m++) {                 /* 끝까지 못 받은 요청: HOL · 데드라인을 끝 시점까지로 */
        mst_t *M = &s->m[m];
        for (int k = 0; k < M->qn; k++) {
            const req_t *r = &M->q[(M->qh + k) % QCAP];
            if (s->cfg[m].deadline > 0 && s->t - r->arr + r->len > s->cfg[m].deadline) M->misses++;
        }
        if (M->qn > 0) {
            const req_t *r = head(s, m);
            int64_t hs = r->arr > M->last_end ? r->arr : M->last_end;
            if (s->t - hs > M->max_hol) M->max_hol = s->t - hs;
        }
    }
}

/* 통계 */
static double offered(const sim_t *s, int m) { return (double)s->m[m].offered / (double)s->T; }
static double achieved(const sim_t *s, int m) { return (double)s->m[m].served / (double)s->T; }
static double mean_wait(const sim_t *s, int m) { return s->m[m].grants ? (double)s->m[m].sum_wait / s->m[m].grants : 0; }
static int64_t pct_wait(const sim_t *s, int m, int p) {        /* nearest-rank: 누적 ≥ ⌈p·n/100⌉ 인 최소 값 */
    int64_t n = s->m[m].grants, need = (p * n + 99) / 100, c = 0;
    if (n == 0) return 0;
    for (int i = 0; i < HMAX; i++) { c += s->m[m].hw[i]; if (c >= need) return i; }
    return HMAX - 1;
}
static bool starved(const sim_t *s, int m) { return s->m[m].max_hol >= STARVE_CYC; }

static void print_masters(const sim_t *s) {
    printf("    %-4s %7s %7s %6s | %6s %5s %6s | %6s %5s %4s %5s\n",
           "", "offered", "achvd", "grants", "w.mean", "w.p99", "w.max", "maxHOL", "starv", "miss", "drop");
    for (int m = 0; m < s->nm; m++) {
        const mst_t *M = &s->m[m];
        printf("    %-4s %7.3f %7.3f %6lld | %6.1f %5lld %6lld | %6lld %5s %4lld %5lld\n",
               s->cfg[m].name, offered(s, m), achieved(s, m), (long long)M->grants, mean_wait(s, m),
               (long long)pct_wait(s, m, 99), (long long)M->max_wait, (long long)M->max_hol,
               starved(s, m) ? "YES" : "-", (long long)M->misses, (long long)M->dropped);
    }
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static sim_t *S;
enum { CPU = 0, DMA = 1, NPU = 2 };

/* 기본 트래픽 혼합 (부하 배율 x): CPU 0.15x · DMA 0.35x · NPU 0.5x beat/cycle → 합 = x */
static void make_mix(mcfg_t c[3], double x) {
    c[CPU] = (mcfg_t){ "CPU", TG_BERN, 0.075 * x, 1, 2, 0, 0, NULL, 0 };          /* 짧고 드문드문: 캐시 라인 2 beat */
    c[DMA] = (mcfg_t){ "DMA", TG_BERN, 0.35 * x / 64.0, 4, 16, 0, 0, NULL, 0 };   /* 몰려옴: 16 beat × 4 */
    c[NPU] = (mcfg_t){ "NPU", TG_STREAM, 0.5 * x, 0, 8, 3.0, 0, NULL, 0 };        /* 일정한 흐름: 8 beat */
}
static void cfg_policy(sim_t *s, arb_t a) {
    if (a == ARB_FIXED) { s->order[0] = CPU; s->order[1] = NPU; s->order[2] = DMA; }   /* CPU > NPU > DMA */
    if (a == ARB_DRR) { s->quantum[CPU] = 48; s->quantum[DMA] = 48; s->quantum[NPU] = 64; } /* 가중치 3:3:4 × 16 */
}
static void run_mix(sim_t *s, arb_t a, double x, uint32_t seed, int64_t T) {
    mcfg_t c[3]; make_mix(c, x);
    sim_init(s, 3, c, a, T, seed); cfg_policy(s, a); sim_run(s, false);
}
static int64_t lsum_others(const sim_t *s, int i) {
    int64_t z = 0; for (int j = 0; j < s->nm; j++) if (j != i) z += s->m[j].lmax; return z;
}
static int64_t lmax_others(const sim_t *s, int i) {
    int64_t z = 0; for (int j = 0; j < s->nm; j++) if (j != i && s->m[j].lmax > z) z = s->m[j].lmax; return z;
}

/* ---------- Part A ---------- */
static const ev_t SC0[] = {{0, 2}, {3, 2}}, SC1[] = {{0, 4}}, SC2[] = {{0, 3}};
static const ev_t SD0[] = {{0, 2}, {0, 2}, {0, 2}};
static const char *script_trace(arb_t a, const int *order, const ev_t *s0, int n0) {
    mcfg_t c[3] = {
        { "m0", TG_SCRIPT, 0, 0, 0, 0, 0, s0, n0 },
        { "m1", TG_SCRIPT, 0, 0, 0, 0, 0, SC1, 1 },
        { "m2", TG_SCRIPT, 0, 0, 0, 0, 0, SC2, 1 },
    };
    sim_init(S, 3, c, a, 16, 1);
    if (order) for (int k = 0; k < 3; k++) S->order[k] = order[k];
    for (int m = 0; m < 3; m++) S->quantum[m] = 4;
    sim_run(S, false);
    S->trace[16] = 0;
    return S->trace;
}

static void part_a(void) {
    printf("== Part A: 지시형 트레이스 (m0: t0 L2, t3 L2 / m1: t0 L4 / m2: t0 L3) ==\n");
    static const int o012[3] = {0, 1, 2}, o210[3] = {2, 1, 0};
    const char *tr;
    tr = script_trace(ARB_FIXED, o012, SC0, 2); printf("  FP 0>1>2  %s\n", tr);
    CHECK(strcmp(tr, "00111100222.....") == 0);
    CHECK(S->m[2].max_wait == 8 && S->m[0].max_wait == 3 && S->wc_viol == 0);
    tr = script_trace(ARB_FIXED, o210, SC0, 2); printf("  FP 2>1>0  %s\n", tr);
    CHECK(strcmp(tr, "22211110000.....") == 0);
    tr = script_trace(ARB_RR, NULL, SC0, 2);    printf("  RR        %s\n", tr);
    CHECK(strcmp(tr, "00111122200.....") == 0);
    printf("  (m0: t0 L2 ×3 / m1: t0 L4 / m2: t0 L3, DRR quantum 4)\n");
    tr = script_trace(ARB_RR, NULL, SD0, 3);    printf("  RR        %s\n", tr);
    CHECK(strcmp(tr, "0011112220000...") == 0);
    tr = script_trace(ARB_DRR, NULL, SD0, 3);   printf("  DRR q=4   %s\n", tr);
    CHECK(strcmp(tr, "0000111122200...") == 0);

    printf("== Part A: 무작위 혼합 트래픽 — work-conserving · 지연 상한 (부하 4 × 시드 3) ==\n");
    static const double loads[4] = {0.3, 0.7, 0.95, 1.2};
    for (int a = ARB_FIXED; a <= ARB_DRR; a++) {
        bool wc = true, hol_rr = true, hol_fp = true; int64_t worst = 0, bound = 0;
        for (int li = 0; li < 4; li++)
            for (uint32_t seed = 1; seed <= 3; seed++) {
                run_mix(S, (arb_t)a, loads[li], seed * 7919u, 50000);
                int64_t sv = 0; for (int m = 0; m < 3; m++) sv += S->m[m].served;
                if (S->wc_viol || S->bad_grant || S->busy + S->idle != S->t || sv != S->busy) wc = false;
                for (int m = 0; m < 3; m++) {
                    if (a == ARB_RR) {
                        if (S->m[m].max_hol > lsum_others(S, m)) hol_rr = false;
                        if (S->m[m].max_hol > worst) { worst = S->m[m].max_hol; bound = lsum_others(S, m); }
                    }
                }
                if (a == ARB_FIXED) {
                    int top = S->order[0];
                    if (S->m[top].max_hol > lmax_others(S, top) - 1) hol_fp = false;
                    if (S->m[top].max_hol > worst) { worst = S->m[top].max_hol; bound = lmax_others(S, top) - 1; }
                }
            }
        printf("  %-3s  wc_viol=0 on all 12 runs: %s", ARB_NAME[a], wc ? "yes" : "NO");
        if (a == ARB_RR) printf("   worst HOL %lld <= bound %lld (sum of others' Lmax)", (long long)worst, (long long)bound);
        if (a == ARB_FIXED) printf("   top(CPU) worst HOL %lld <= bound %lld (max other L - 1)", (long long)worst, (long long)bound);
        printf("\n");
        CHECK(wc);
        if (a == ARB_RR) CHECK(hol_rr);
        if (a == ARB_FIXED) CHECK(hol_fp);
    }

    printf("== Part A: RR 상한은 빡빡하다 — 셋 다 backlog (L = 2 / 16 / 8) ==\n");
    mcfg_t c[3] = {
        { "CPU", TG_BACKLOG, 0, 0, 2, 0, 0, NULL, 0 },
        { "DMA", TG_BACKLOG, 0, 0, 16, 0, 0, NULL, 0 },
        { "NPU", TG_BACKLOG, 0, 0, 8, 0, 0, NULL, 0 },
    };
    sim_init(S, 3, c, ARB_RR, 10000, 1); sim_run(S, false);
    printf("  maxHOL CPU %lld (bound %lld)  DMA %lld (bound %lld)  NPU %lld (bound %lld)\n",
           (long long)S->m[0].max_hol, (long long)lsum_others(S, 0), (long long)S->m[1].max_hol,
           (long long)lsum_others(S, 1), (long long)S->m[2].max_hol, (long long)lsum_others(S, 2));
    printf("  trace %s\n", S->trace);
    for (int m = 0; m < 3; m++) CHECK(S->m[m].max_hol == lsum_others(S, m));
    CHECK(strncmp(S->trace, "00111111111111111122222222001111111111111111222222220011", 56) == 0);
}

/* ---------- Part B ---------- */
static void part_b(void) {
    const int64_t T = 200000;
    printf("== Part B: 셋 다 backlog — RR 은 그랜트 공정, 대역폭은 L 에 비례 ==\n");
    mcfg_t c[3] = {
        { "CPU", TG_BACKLOG, 0, 0, 2, 0, 0, NULL, 0 },
        { "DMA", TG_BACKLOG, 0, 0, 16, 0, 0, NULL, 0 },
        { "NPU", TG_BACKLOG, 0, 0, 8, 0, 0, NULL, 0 },
    };
    sim_init(S, 3, c, ARB_RR, T, 1); sim_run(S, false);
    {
        bool ok = true;
        for (int m = 0; m < 3; m++) {
            double want = c[m].len / 26.0;
            printf("  RR  %-3s share %.4f  (L/sum L = %.4f)\n", c[m].name, achieved(S, m), want);
            if (fabs(achieved(S, m) - want) > 2.0 * 26 / T) ok = false;
        }
        CHECK(ok);
    }

    printf("== Part B: DRR (beat 단위 가중 RR) — 대역폭 몫 = 가중치 비율, 허용오차 = 유도한 상한 / T ==\n");
    static const int W[4][3] = {{1, 2, 4}, {4, 2, 1}, {1, 1, 1}, {3, 1, 1}};
    static const int QU[4] = {16, 16, 16, 4};       /* 마지막은 quantum 단위 4 < Lmax 16: 여러 차례 저축 필요 */
    printf("  weights   qunit | CPU share (want)   DMA share (want)   NPU share (want) | max|err|  tol\n");
    for (int k = 0; k < 4; k++) {
        sim_init(S, 3, c, ARB_DRR, T, 1);
        int64_t sq = 0, sl = 26;
        for (int m = 0; m < 3; m++) { S->quantum[m] = (int64_t)W[k][m] * QU[k]; sq += S->quantum[m]; }
        sim_run(S, false);
        double wsum = W[k][0] + W[k][1] + W[k][2], maxerr = 0; bool ok = S->wc_viol == 0;
        printf("  %d:%d:%d    %5d |", W[k][0], W[k][1], W[k][2], QU[k]);
        double tolmax = 0;
        for (int m = 0; m < 3; m++) {
            double want = W[k][m] / wsum, err = fabs(achieved(S, m) - want);
            /* |S_i − (Q_i/ΣQ)·T| < 2Q_i + L_i + Q_i·ΣL/ΣQ + Lmax  (풀이 참고) */
            double tol = (2.0 * S->quantum[m] + c[m].len + (double)S->quantum[m] * sl / sq + 16) / T;
            if (err > maxerr) maxerr = err;
            if (tol > tolmax) tolmax = tol;
            if (err > tol) ok = false;
            printf(" %.4f (%.4f)   ", achieved(S, m), want);
        }
        printf("| %.5f  %.5f\n", maxerr, tolmax);
        CHECK(ok);
    }

    printf("== Part B: DRR 3:3:4, CPU 만 가벼움 (0.10) — 남는 몫은 백로그끼리 3:4 로 ==\n");
    {
        mcfg_t d[3] = {
            { "CPU", TG_BERN, 0.05, 1, 2, 0, 0, NULL, 0 },
            { "DMA", TG_BACKLOG, 0, 0, 16, 0, 0, NULL, 0 },
            { "NPU", TG_BACKLOG, 0, 0, 8, 0, 0, NULL, 0 },
        };
        sim_init(S, 3, d, ARB_DRR, T, 99); cfg_policy(S, ARB_DRR); sim_run(S, false);
        print_masters(S);
        double r = achieved(S, DMA) / achieved(S, NPU);
        printf("  DMA/NPU = %.4f (want 0.75)\n", r);
        CHECK(fabs(achieved(S, CPU) - offered(S, CPU)) < 0.002);
        CHECK(fabs(r - 0.75) < 0.01);
        CHECK(S->wc_viol == 0 && S->busy == S->t);
    }

    printf("== Part B: 고정 우선순위의 기아 — CPU 0.1 · NPU backlog · DMA 0.2 (CPU > NPU > DMA) ==\n");
    {
        mcfg_t d[3] = {
            { "CPU", TG_BERN, 0.05, 1, 2, 0, 0, NULL, 0 },
            { "DMA", TG_BERN, 0.2 / 64.0, 4, 16, 0, 0, NULL, 0 },
            { "NPU", TG_BACKLOG, 0, 0, 8, 0, 0, NULL, 0 },
        };
        sim_init(S, 3, d, ARB_FIXED, T, 5); cfg_policy(S, ARB_FIXED); sim_run(S, false);
        printf("  FP:\n"); print_masters(S);
        CHECK(S->m[DMA].grants == 0 && starved(S, DMA) && S->m[DMA].dropped > 0);
        sim_init(S, 3, d, ARB_RR, T, 5); sim_run(S, false);
        printf("  RR:\n"); print_masters(S);
        CHECK(!starved(S, DMA) && fabs(achieved(S, DMA) - offered(S, DMA)) < 0.005);
        CHECK(S->m[DMA].max_hol <= lsum_others(S, DMA));
    }

    printf("== Part B: 부하 스윕 (CPU 0.15x · DMA 0.35x · NPU 0.5x, T = %lld, 시드 1) ==\n", (long long)T);
    printf("  x    pol | CPU ach/off  p99   max | DMA ach/off  p99   maxHOL st | NPU ach/off  p99\n");
    static const double X[8] = {0.2, 0.4, 0.6, 0.8, 0.9, 1.0, 1.2, 1.6};
    bool light_ok = true, fp_starve = false, rr_ok = true, drr_ok = true, wc_all = true;
    for (int xi = 0; xi < 8; xi++)
        for (int a = ARB_FIXED; a <= ARB_DRR; a++) {
            run_mix(S, (arb_t)a, X[xi], 1, T);
            printf("  %.1f  %-3s | %.3f/%.3f %4lld %5lld | %.3f/%.3f %5lld %7lld %-2s | %.3f/%.3f %4lld\n",
                   X[xi], ARB_NAME[a], achieved(S, CPU), offered(S, CPU), (long long)pct_wait(S, CPU, 99),
                   (long long)S->m[CPU].max_wait, achieved(S, DMA), offered(S, DMA), (long long)pct_wait(S, DMA, 99),
                   (long long)S->m[DMA].max_hol, starved(S, DMA) ? "S" : "-", achieved(S, NPU), offered(S, NPU),
                   (long long)pct_wait(S, NPU, 99));
            if (S->wc_viol) wc_all = false;
            if (X[xi] <= 0.6)
                for (int m = 0; m < 3; m++) if (fabs(achieved(S, m) - offered(S, m)) > 0.01) light_ok = false;
            if (a == ARB_FIXED && X[xi] == 1.6 && starved(S, DMA)) fp_starve = true;
            for (int m = 0; m < 3; m++) {
                if (a == ARB_RR && (starved(S, m) || S->m[m].max_hol > lsum_others(S, m))) rr_ok = false;
                if (a == ARB_DRR && starved(S, m)) drr_ok = false;
            }
        }
    CHECK(light_ok);       /* 가벼운 부하에선 정책과 무관하게 모두 원하는 만큼 받는다 */
    CHECK(fp_starve);      /* 포화에선 고정 우선순위의 최하위가 굶는다 */
    CHECK(rr_ok);          /* RR: 모든 부하에서 기아 없음 + HOL 상한 */
    CHECK(drr_ok);
    CHECK(wc_all);
}

/* ---------- Part C ---------- */
enum { CAM = 0, CCPU = 1, CDMA = 2, CNPU = 3 };
static void make_soc(mcfg_t c[4], int npu_len, bool dma_backlog) {
    c[CAM]  = (mcfg_t){ "CAM", TG_STREAM, 0.125, 0, 8, 0, 48, NULL, 0 };      /* 64 사이클마다 8 beat, 48 안에 완료 */
    c[CCPU] = (mcfg_t){ "CPU", TG_BERN, 0.05, 1, 2, 0, 0, NULL, 0 };
    c[CDMA] = (mcfg_t){ "DMA", TG_BERN, 0.2 / 64.0, 4, 16, 0, 0, NULL, 0 };
    c[CNPU] = (mcfg_t){ "NPU", TG_BACKLOG, 0, 0, npu_len, 0, 0, NULL, 0 };     /* 대량: 항상 줄 서 있음 */
    if (dma_backlog) c[CDMA] = (mcfg_t){ "DMA", TG_BACKLOG, 0, 0, 16, 0, 0, NULL, 0 };
}
typedef enum { POL_RR, POL_DRR, POL_FP_A, POL_FP_B, POL_QOS0, POL_QOS, NPOL } pol_t;
static const char *const POL_NAME[NPOL] = {"RR", "DRR 1:1:2:4", "FP CAM>CPU>DMA>NPU", "FP CAM>CPU>NPU>DMA",
                                           "QOS all lvl 4", "QOS CPU lvl 8"};
static void run_soc(sim_t *s, pol_t p, int npu_len, bool dma_backlog, bool hard, uint32_t seed, int64_t T) {
    mcfg_t c[4]; make_soc(c, npu_len, dma_backlog);
    static const arb_t A[NPOL] = {ARB_RR, ARB_DRR, ARB_FIXED, ARB_FIXED, ARB_QOS, ARB_QOS};
    sim_init(s, 4, c, A[p], T, seed);
    if (p == POL_DRR) { s->quantum[CAM] = 16; s->quantum[CCPU] = 16; s->quantum[CDMA] = 32; s->quantum[CNPU] = 64; }
    if (p == POL_FP_A) { s->order[0] = CAM; s->order[1] = CCPU; s->order[2] = CDMA; s->order[3] = CNPU; }
    if (p == POL_FP_B) { s->order[0] = CAM; s->order[1] = CCPU; s->order[2] = CNPU; s->order[3] = CDMA; }
    if (p == POL_QOS0 || p == POL_QOS) {
        for (int m = 0; m < 4; m++) s->qos[m] = 4;
        if (p == POL_QOS) s->qos[CCPU] = 8;                 /* CPU: 지연 민감 → 기본 레벨을 높게 */
        s->urg_after[CAM] = 8;                              /* 대기 ≤ (8−1) + 32 → 총 ≤ 47 ≤ 48 (풀이) */
        s->regulated[CNPU] = true; s->tb[CNPU] = (tb_t){ 64, 0.5, 64 };
        s->hard_reg = hard;
    }
    sim_run(s, false);
}

static void part_c(void) {
    printf("== Part C: 토큰 버킷 단위 테스트 ==\n");
    tb_t b = { 10, 0.5, 12 };
    tb_tick(&b); tb_tick(&b); tb_tick(&b);
    CHECK(b.tokens == 11.5);
    tb_tick(&b); tb_tick(&b);
    CHECK(b.tokens == 12.0);                                /* cap 에서 멈춤 */
    CHECK(tb_ok(&b, 12) && !tb_ok(&b, 13));
    tb_charge(&b, 8); CHECK(b.tokens == 4.0);
    tb_charge(&b, 8); CHECK(b.tokens == 0.0);               /* 빚 없음 */

    const int64_t T = 200000;
    printf("== Part C: SoC — CAM(64 사이클마다 8 beat, 마감 48) · CPU 0.1 · DMA 0.2 · NPU backlog L=32, T = %lld ==\n",
           (long long)T);
    printf("  policy               | CAM miss max.tot | CPU ach/off  w.p99 | DMA ach/off maxHOL st | NPU ach | wc\n");
    int64_t miss[NPOL], camtot[NPOL], camwait[NPOL], cpu99[NPOL]; bool dstarve[NPOL]; double npu[NPOL], cpuach[NPOL];
    for (int p = 0; p < NPOL; p++) {
        run_soc(S, (pol_t)p, 32, false, false, 2024, T);
        miss[p] = S->m[CAM].misses; camtot[p] = S->m[CAM].max_tot; camwait[p] = S->m[CAM].max_wait;
        dstarve[p] = starved(S, CDMA); npu[p] = achieved(S, CNPU); cpu99[p] = pct_wait(S, CCPU, 99);
        cpuach[p] = achieved(S, CCPU) / offered(S, CCPU);
        printf("  %-20s | %8lld %7lld | %.3f/%.3f %6lld | %.3f/%.3f %6lld %-2s | %.3f   | %lld\n", POL_NAME[p],
               (long long)miss[p], (long long)camtot[p], achieved(S, CCPU), offered(S, CCPU), (long long)cpu99[p],
               achieved(S, CDMA), offered(S, CDMA), (long long)S->m[CDMA].max_hol, dstarve[p] ? "S" : "-", npu[p],
               (long long)S->wc_viol);
        CHECK(S->wc_viol == 0 && S->bad_grant == 0);
    }
    CHECK(miss[POL_RR] > 0 && camtot[POL_RR] > 48);          /* RR: 최악 대기 = 2 + 16 + 32 → 총 58 까지 */
    CHECK(cpuach[POL_RR] < 0.9);                            /* RR: CPU 는 한 바퀴에 2 beat → 0.1 을 못 받는다 */
    CHECK(miss[POL_DRR] > 0);                               /* DRR: 몫은 공정해도 마감은 모른다 */
    CHECK(miss[POL_FP_A] == 0 && camtot[POL_FP_A] <= 31 + 8 && !dstarve[POL_FP_A]);  /* 대기 ≤ Lmax_other − 1 */
    CHECK(miss[POL_FP_B] == 0 && dstarve[POL_FP_B]);        /* NPU 를 DMA 위에 두면 DMA 가 굶는다 */
    CHECK(miss[POL_QOS0] == 0 && miss[POL_QOS] == 0 && !dstarve[POL_QOS0] && !dstarve[POL_QOS]);
    CHECK(camwait[POL_QOS0] <= 8 - 1 + 32 && camwait[POL_QOS] <= 8 - 1 + 32);   /* 승격 후 ≤ (urg−1) + Lmax_other */
    CHECK(cpu99[POL_QOS] * 10 < cpu99[POL_QOS0] && cpuach[POL_QOS] > 0.99);     /* CPU 레벨이 p99 를 살린다 */
    CHECK(npu[POL_QOS] >= 0.45);                            /* 규제 0.5 + 남는 몫 (soft) */

    printf("== Part C: 같은 SoC, DMA 도 backlog (대량 마스터 둘) ==\n");
    printf("  policy               | CAM miss max.tot | CPU w.p99 | DMA ach maxHOL st | NPU ach maxHOL st\n");
    static const pol_t P2[3] = {POL_FP_A, POL_FP_B, POL_QOS};
    bool st2[3][4]; int64_t miss2[3];
    for (int k = 0; k < 3; k++) {
        run_soc(S, P2[k], 32, true, false, 2024, T);
        for (int m = 0; m < 4; m++) st2[k][m] = starved(S, m);
        miss2[k] = S->m[CAM].misses;
        printf("  %-20s | %8lld %7lld | %9lld | %.3f %6lld %-2s | %.3f %6lld %-2s\n", POL_NAME[P2[k]],
               (long long)miss2[k], (long long)S->m[CAM].max_tot, (long long)pct_wait(S, CCPU, 99),
               achieved(S, CDMA), (long long)S->m[CDMA].max_hol, st2[k][CDMA] ? "S" : "-",
               achieved(S, CNPU), (long long)S->m[CNPU].max_hol, st2[k][CNPU] ? "S" : "-");
    }
    CHECK(st2[0][CNPU] && st2[1][CDMA]);                    /* 고정 우선순위: 대량 둘 중 아래쪽이 굶는다 */
    CHECK(miss2[2] == 0 && !st2[2][CAM] && !st2[2][CCPU] && !st2[2][CDMA] && !st2[2][CNPU]);
}

int main(void) {
    S = calloc(1, sizeof *S);
    if (!S) return 1;
    part_a();
    part_b();
    part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    free(S);
    return g_pass == g_total ? 0 : 1;
}
