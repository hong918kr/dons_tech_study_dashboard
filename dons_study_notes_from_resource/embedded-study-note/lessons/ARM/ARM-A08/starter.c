/* ARM-A08 Cortex-M 인터럽트 지연 분석 — 시스템 수준 최악 지연 (호스트 C, 맥)
 *
 * BM-A10 은 "예외 하나" 의 사이클(진입 12, 꼬리 물기 6, late arrival, lazy FP)과 DWT 측정을 다뤘다.
 * 이 레슨은 그 위층이다: IRQ "세트" + 크리티컬 섹션 + PRIGROUP 이 주어졌을 때
 *   Part A  NVIC 규칙(그룹/서브 우선순위, 실행 우선순위, PRIMASK/BASEPRI) 과 해석적 최악 지연
 *           L_i = E + B_i + Σ ceil(L/T_j)(C_j + O)  (고정점) 과 응답 시간 R_i.
 *           정답 기준(ground truth) = 사이클 단위 이산 사건 시뮬레이터 (펜딩, 그룹 선점, 꼬리 물기,
 *           late arrival, pop 선점, 서브 우선순위 동률 깨기, 펜딩 비트 1 개 = 놓침).
 *           검사: 작은 세트에서 모든 도착 위상을 다 돌린 시뮬 최대 ≤ 해석 상한, 구성한 경우엔 = .
 *   Part B  최적화 전후 표 (꼬리 물기 끔/켬, PRIMASK→BASEPRI, top/bottom half 분할, 서브→선점 우선순위,
 *           섹션 단축, lazy FP, 플래시 대기) — 누가 얻고 누가 잃나.
 *   Part C  모터 제어 세트 (20 kHz 전류 루프, 엔코더, CAN, UART, SysTick) — 데드라인을 지키나, 섹션 예산은.
 * 모든 사이클 값은 "모델" 이다 (ARM 문서 대표값 + 가정, 문서 확인 권장). 측정이 아니다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/ARM-A08_so && /tmp/ARM-A08_so
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(cond, msg) do { ++g_total; if (cond) { ++g_pass; } \
    else printf("FAIL  %s  (line %d)\n", msg, __LINE__); } while (0)

static uint64_t g_rng = 0xA08A08A08ull;
static uint64_t xs64(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }

/* ===================== 모델 파라미터 (BM-A10 cm_params_t 와 같은 이름) ===================== */
typedef struct {
    uint32_t entry;        /* IRQ 펜딩 → ISR 첫 명령, 0 WS (ARM: 12) */
    uint32_t tail;         /* 꼬리 물기 ISR 끝 → 다음 ISR 첫 명령 (ARM: 6) */
    uint32_t exit;         /* 언스태킹 (가정 12, 문헌 10~12) */
    uint32_t late_min;     /* late arrival: 도착 → 첫 명령 최소 (가정 6) */
    uint32_t fp_words;     /* FP 확장 프레임 추가 워드 S0–S15+FPSCR = 17, 워드당 1 사이클 가정 */
    uint32_t fetch;        /* 진입·꼬리 물기·late 마다 붙는 플래시 대기 (BM-A10 fetch_penalty(): 5 WS miss = 10) */
    uint8_t  tail_chain;   /* 1 = 꼬리 물기 + pop 선점 (모든 Cortex-M). 0 = 비교용 가상 코어 */
    uint8_t  late_arrival; /* 1 = late arrival. 0 = 비교용 가상 코어 */
} cm_params_t;
static const cm_params_t CM4 = { 12, 6, 12, 6, 17, 0, 1, 1 };

#define MAXIRQ 8
#define MAXSEC 3
#define PRIO_NONE 256                  /* 스레드, 마스크 없음 = 어떤 IRQ 든 선점 가능 */
#define NO_TIME 0xFFFFFFFFu
enum { SEC_PRIMASK, SEC_BASEPRI };
typedef struct { const char *name; uint8_t prio; uint32_t C, T, D; } irq_t;   /* prio = IPR 바이트, D = 0 이면 T */
typedef struct { const char *name; uint32_t len; uint8_t kind, basepri; } crit_t; /* 스레드 코드의 마스크 구간 */
typedef struct {
    const char *name;
    int n, nsec;
    irq_t irq[MAXIRQ];
    crit_t sec[MAXSEC];
    uint8_t prigroup;      /* AIRCR.PRIGROUP: 그룹 = prio 의 [7 : PRIGROUP+1] */
    uint8_t fp_full;       /* 스레드 FPCA=1 · LSPEN=0: 스레드를 선점하는 진입 +17, 스레드 복귀 +17 (ISR 은 FP 안 씀) */
} sys_t;

/* ===================== Part A — NVIC 규칙 (TODO A1–A3) ===================== */

/* TODO A1: 그룹(선점) 우선순위. ARMv7-M: groupvalue = 2 << PRIGROUP, group = prio − (prio mod groupvalue).
 * PRIGROUP 7 이면 모두 그룹 0 (아무도 선점 못 함). */
static int prio_group(uint8_t prio, uint8_t prigroup)
{
    (void)prio; (void)prigroup;
    return 0;                                  /* TODO A1 */
}

/* TODO A2: 실행 우선순위 (ARMv7-M ExecutionPriority 축약). active_group = 활성 핸들러 중 가장 급한 그룹
 * (스레드면 PRIO_NONE). BASEPRI ≠ 0 이면 그 "그룹" 값으로 끌어올림, PRIMASK 면 0. 셋 중 최소. */
static int exec_priority(int active_group, int primask, uint8_t basepri, uint8_t prigroup)
{
    (void)active_group; (void)primask; (void)basepri; (void)prigroup;
    return PRIO_NONE;                          /* TODO A2 */
}

/* TODO A3: 펜딩 중 그룹 < threshold (엄격히 더 급함) 인 것 가운데 승자. 동률 깨기: prio 바이트 전체
 * (그룹 → 서브), 그다음 번호 작은 쪽. 없으면 -1. */
static int nvic_select(const sys_t *s, const uint8_t *pend, int threshold)
{
    (void)s; (void)pend; (void)threshold;
    return -1;                                 /* TODO A3 */
}

/* ---------- 도우미 (주어짐) ---------- */
static int grp(const sys_t *s, int i) { return prio_group(s->irq[i].prio, s->prigroup); }
static int key_before(const sys_t *s, int j, int i)          /* j 가 i 보다 먼저 뽑힌다 (동시 펜딩 시) */
{
    return s->irq[j].prio < s->irq[i].prio || (s->irq[j].prio == s->irq[i].prio && j < i);
}
static int sec_masks(const sys_t *s, const crit_t *c, int i)
{
    if (c->kind == SEC_PRIMASK) return 1;
    return c->basepri != 0 && grp(s, i) >= prio_group(c->basepri, s->prigroup);
}
static uint32_t cost_E(const cm_params_t *p, const sys_t *s) { return p->entry + p->fetch + (s->fp_full ? p->fp_words : 0u); }
static uint32_t cost_X(const cm_params_t *p, const sys_t *s) { return p->exit + (s->fp_full ? p->fp_words : 0u); }
/* 앞 순번 job 하나의 오버헤드 O = 진입 + 복귀. 가상 코어(꼬리 물기 없음 + late arrival 있음)에선
 * late arrival 이 i 의 스태킹을 버리게 만들고 다시 처음부터 쌓으므로 late_min + fetch 를 더 센다 (시뮬로 발견). */
static uint32_t cost_O(const cm_params_t *p, const sys_t *s)
{
    return cost_E(p, s) + cost_X(p, s) + ((!p->tail_chain && p->late_arrival) ? p->late_min + p->fetch : 0u);
}
static uint32_t dl(const sys_t *s, int i) { return s->irq[i].D ? s->irq[i].D : s->irq[i].T; }
#define LIMIT (1u << 30)

/* ===================== Part A — 해석 (TODO A4–A6) ===================== */

/* TODO A4: 블로킹 B_i — i 가 펜딩된 뒤, i 보다 "뒤 순번" 인 것 때문에 기다리는 최대 시간 (하나만 걸린다).
 *  (1) i 를 막는 섹션: len − 1  (같은 사이클에 도착한 IRQ 는 cpsid 보다 먼저 받는다 → 최악은 그다음 사이클 도착)
 *  (2) 같은 그룹 · 뒤 순번 k: 스태킹 남은 것 E−1 + C_k  (+ 꼬리 물기 없으면 그 복귀 X)
 *  (3) 꼬리 물기 없는 코어: 뒤 순번 누군가의 복귀 X
 *  (4) late arrival 없는 코어: 더 낮은 그룹의 스태킹 E−1
 * 앞 순번(key_before)은 블로킹이 아니라 간섭으로 센다. */
static uint32_t blocking_bound(const cm_params_t *p, const sys_t *s, int i)
{
    (void)p; (void)s; (void)i;
    return 0;                                  /* TODO A4 */
}

/* TODO A5: 최악 지연 L_i (도착 → 첫 명령). 앞 순번 j (key_before) 의 도착 [0, L) 이 모두 먼저 처리된다:
 *     L = E + B_i + Σ_j ceil(L / T_j) · (C_j + O),   O = E + X (j 하나의 진입+복귀)
 * L0 = E + B + Σ(C_j + O) 에서 시작해 같아질 때까지. LIMIT 를 넘으면 -1. */
static int latency_bound(const cm_params_t *p, const sys_t *s, int i, uint32_t *L_out)
{
    (void)p; (void)s; (void)i;
    *L_out = 0;
    return -1;                                 /* TODO A5 */
}

/* TODO A6: 최악 응답 R_i (도착 → 본문 끝). 시작 뒤엔 "더 급한 그룹" 만 끊을 수 있다:
 *     R = E + B + C_i + Σ_{같은 그룹 앞 순번} ceil(L_i/T_j)(C_j+O) + Σ_{grp(j)<grp(i)} ceil(R/T_j)(C_j+O) */
static int response_bound(const cm_params_t *p, const sys_t *s, int i, uint32_t *R_out)
{
    (void)p; (void)s; (void)i;
    *R_out = 0;
    return -1;                                 /* TODO A6 */
}

/* 세트 전체가 괜찮은가 (주어짐): 모든 i 에 대해 R ≤ D, 그리고 R ≤ T (넘으면 펜딩 비트 하나라 도착을 놓칠 수 있다) */
static int sys_ok(const cm_params_t *p, const sys_t *s)
{
    for (int i = 0; i < s->n; ++i) {
        uint32_t R;
        if (response_bound(p, s, i, &R) || R > dl(s, i) || R > s->irq[i].T) return 0;
    }
    return 1;
}

/* ===================== 시뮬레이터 (주어짐): 이산 사건, 사이클 정수 시각 ===================== */
enum { HOW_NONE, HOW_ENTRY, HOW_NESTED, HOW_LATE, HOW_TAIL, HOW_POP };
static const char *HOW_NAME[] = { "-", "entry", "nested", "late", "tail", "pop" };
enum { ST_RUN, ST_ENTRY, ST_TAIL, ST_EXIT };
enum { TR_ARRIVE, TR_LOST, TR_START, TR_END, TR_RESUME, TR_SEC_ON, TR_SEC_OFF };
typedef struct {
    uint32_t first[MAXIRQ];   /* 첫 도착 (NO_TIME = 안 옴), 그 뒤 T 마다 */
    uint8_t  once[MAXIRQ];    /* 1 = 한 번만 */
    int      sec;             /* -1 = 섹션 없음, 아니면 sec[] 인덱스: 스레드가 sec_start 이후 처음 돌 때 진입 */
    uint32_t sec_start;
    uint32_t horizon;         /* 이 시각 뒤로는 새 도착 없음 */
} scen_t;
typedef struct { uint32_t t; uint8_t ev, irq, how; } trace_ev_t;
typedef struct {
    uint32_t lat_max[MAXIRQ], resp_max[MAXIRQ], resp_min[MAXIRQ], acts[MAXIRQ], lost[MAXIRQ];
    uint32_t n_entry, n_tail, n_late, n_pop, n_exit, max_depth, sec_on, sec_off;
    trace_ev_t *tr; uint32_t tr_cap, tr_n;
} simres_t;

static void tr_add(simres_t *o, uint32_t t, int ev, int irq, int how)
{
    if (o->tr && o->tr_n < o->tr_cap) o->tr[o->tr_n++] = (trace_ev_t){ t, (uint8_t)ev, (uint8_t)irq, (uint8_t)how };
}

static int sim_run(const cm_params_t *p, const sys_t *s, const scen_t *sc, simres_t *o)
{
    uint8_t pend[MAXIRQ] = {0};
    uint32_t t_arr[MAXIRQ] = {0}, next[MAXIRQ];
    int act[MAXIRQ + 2] = {0};
    uint32_t rem[MAXIRQ + 2] = {0}, arr_act[MAXIRQ + 2] = {0};
    int depth = 0, st = ST_RUN, tgt = -1, how = HOW_NONE, in_sec = 0, sec_done = sc->sec < 0, primask = 0;
    uint8_t basepri = 0;
    uint32_t ready = 0, t = 0, sec_rem = 0;
    trace_ev_t *tr = o->tr; uint32_t cap = o->tr_cap;
    memset(o, 0, sizeof *o); o->tr = tr; o->tr_cap = cap;
    for (int k = 0; k < MAXIRQ; ++k) o->resp_min[k] = NO_TIME;
    for (int k = 0; k < s->n; ++k) next[k] = sc->first[k] > sc->horizon ? NO_TIME : sc->first[k];
    for (uint32_t guard = 0; guard < 4000000u; ++guard) {
        for (int k = 0; k < s->n; ++k) {                          /* 1. 이 시각의 도착 */
            if (next[k] != t) continue;
            if (pend[k]) { ++o->lost[k]; tr_add(o, t, TR_LOST, k, 0); }   /* 펜딩 비트는 하나 */
            else { pend[k] = 1; t_arr[k] = t; tr_add(o, t, TR_ARRIVE, k, 0); }
            uint64_t nx = (uint64_t)t + s->irq[k].T;
            next[k] = (sc->once[k] || nx > sc->horizon) ? NO_TIME : (uint32_t)nx;
        }
        for (int step = 0; step < 64; ++step) {                   /* 2. 이 시각의 결정 */
            if (st == ST_RUN) {
                if (depth > 0 && rem[depth] == 0) {               /* ISR 끝 (BX LR) */
                    int k = act[depth];
                    if (t - arr_act[depth] > o->resp_max[k]) o->resp_max[k] = t - arr_act[depth];
                    if (t - arr_act[depth] < o->resp_min[k]) o->resp_min[k] = t - arr_act[depth];
                    tr_add(o, t, TR_END, k, 0);
                    int back = exec_priority(depth > 1 ? grp(s, act[depth - 1]) : PRIO_NONE, primask, basepri, s->prigroup);
                    int b = p->tail_chain ? nvic_select(s, pend, back) : -1;
                    if (b >= 0) { st = ST_TAIL; tgt = b; how = HOW_TAIL; ready = t + p->tail + p->fetch; ++o->n_tail; }
                    else { st = ST_EXIT; ready = t + p->exit + ((depth == 1 && s->fp_full) ? p->fp_words : 0u); }
                    continue;
                }
                int ctx = exec_priority(depth ? grp(s, act[depth]) : PRIO_NONE, primask, basepri, s->prigroup);
                int b = nvic_select(s, pend, ctx);
                if (b >= 0) {                                     /* 선점: 스태킹 시작 */
                    st = ST_ENTRY; tgt = b; how = depth ? HOW_NESTED : HOW_ENTRY;
                    ready = t + p->entry + p->fetch + ((depth == 0 && s->fp_full) ? p->fp_words : 0u);
                    ++o->n_entry;
                    continue;
                }
                if (depth == 0 && !sec_done && !in_sec && t >= sc->sec_start) {   /* 스레드: 섹션 진입 */
                    const crit_t *c = &s->sec[sc->sec];
                    in_sec = 1; sec_rem = c->len; o->sec_on = t;
                    if (c->kind == SEC_PRIMASK) primask = 1; else basepri = c->basepri;
                    tr_add(o, t, TR_SEC_ON, sc->sec, 0);
                    continue;
                }
                if (depth == 0 && in_sec && sec_rem == 0) {       /* 섹션 끝: 마스크 해제 */
                    in_sec = 0; sec_done = 1; primask = 0; basepri = 0; o->sec_off = t;
                    tr_add(o, t, TR_SEC_OFF, sc->sec, 0);
                    continue;
                }
                break;
            }
            if (st == ST_ENTRY || st == ST_TAIL) {
                if (t < ready && p->late_arrival) {               /* late arrival: 더 급한 그룹이면 벡터만 교체 */
                    int b = nvic_select(s, pend, grp(s, tgt));
                    if (b >= 0) {
                        uint32_t r = t + p->late_min + p->fetch;
                        tgt = b; how = HOW_LATE; if (r > ready) ready = r; ++o->n_late;
                    }
                }
                if (t < ready) break;
                if (st == ST_ENTRY) { ++depth; if ((uint32_t)depth > o->max_depth) o->max_depth = (uint32_t)depth; }
                act[depth] = tgt; pend[tgt] = 0; arr_act[depth] = t_arr[tgt]; rem[depth] = s->irq[tgt].C;
                if (t - t_arr[tgt] > o->lat_max[tgt]) o->lat_max[tgt] = t - t_arr[tgt];
                ++o->acts[tgt];
                tr_add(o, t, TR_START, tgt, how);
                st = ST_RUN;
                continue;
            }
            /* ST_EXIT: 언스태킹 중 */
            if (t < ready && p->tail_chain) {                     /* pop 선점 → 꼬리 물기 */
                int back = exec_priority(depth > 1 ? grp(s, act[depth - 1]) : PRIO_NONE, primask, basepri, s->prigroup);
                int b = nvic_select(s, pend, back);
                if (b >= 0) { st = ST_TAIL; tgt = b; how = HOW_POP; ready = t + p->tail + p->fetch; ++o->n_pop; continue; }
            }
            if (t < ready) break;
            --depth; ++o->n_exit; st = ST_RUN;
            if (depth == 0) tr_add(o, t, TR_RESUME, 0, 0);
        }
        uint32_t tn = NO_TIME;                                    /* 3. 다음 사건 시각 */
        for (int k = 0; k < s->n; ++k) if (next[k] < tn) tn = next[k];
        if (st != ST_RUN) { if (ready < tn) tn = ready; }
        else if (depth > 0) { if (t + rem[depth] < tn) tn = t + rem[depth]; }
        else if (in_sec) { if (t + sec_rem < tn) tn = t + sec_rem; }
        else if (!sec_done && sc->sec_start > t && sc->sec_start < tn) tn = sc->sec_start;
        if (tn == NO_TIME) return 0;                              /* 더 일어날 일이 없다 */
        uint32_t dt = tn - t;
        if (st == ST_RUN) { if (depth > 0) rem[depth] -= dt; else if (in_sec) sec_rem -= dt; }
        t = tn;
    }
    return -1;
}

/* ---------- 위상 탐색 (주어짐): i 는 A 에 한 번, 나머지는 주기 T_j, 위상 x_j (A − x_j ≡ 도착) ---------- */
typedef struct { uint32_t lat, resp, resp_min, lost, runs, fails; scen_t worst; } search_t;

static void scen_fill(const sys_t *s, int i, uint32_t A, const uint32_t *x, int sec, uint32_t sec_start,
                      uint32_t horizon, scen_t *sc)
{
    memset(sc, 0, sizeof *sc);
    for (int j = 0; j < s->n; ++j) {
        uint32_t T = s->irq[j].T;
        sc->first[j] = (j == i) ? A : ((A % T) + T - (x[j] % T)) % T;
        sc->once[j] = (j == i);
    }
    sc->sec = sec; sc->sec_start = sec_start; sc->horizon = horizon;
}

static void search_one(const cm_params_t *p, const sys_t *s, int i, const scen_t *sc, search_t *out)
{
    simres_t o; o.tr = NULL; o.tr_cap = 0;
    ++out->runs;
    if (sim_run(p, s, sc, &o) || o.acts[i] == 0) { ++out->fails; return; }
    if (o.lat_max[i] > out->lat || out->runs == 1) { out->lat = o.lat_max[i]; out->worst = *sc; }
    if (o.resp_max[i] > out->resp) out->resp = o.resp_max[i];
    if (o.resp_min[i] < out->resp_min) out->resp_min = o.resp_min[i];
    for (int k = 0; k < s->n; ++k) out->lost += o.lost[k];
}

/* nrand == 0: 다른 IRQ 위상 전부 × (섹션 없음 + 각 섹션의 시작 A−len..A) 를 다 돈다 (작은 세트용).
 * nrand > 0: 임계 순간 후보(x=0, 섹션 A−1) + 고정 시드 무작위 nrand 개 (큰 세트용 — 하한일 뿐). */
static void sim_search(const cm_params_t *p, const sys_t *s, int i, uint32_t nrand, uint32_t A, uint32_t win,
                       search_t *out)
{
    uint32_t x[MAXIRQ] = {0};
    scen_t sc;
    memset(out, 0, sizeof *out);
    out->resp_min = NO_TIME;
    uint32_t horizon = A + win;
    if (nrand == 0) {
        for (;;) {
            for (int sec = -1; sec < s->nsec; ++sec) {
                uint32_t lo = sec < 0 ? A : A - s->sec[sec].len, hi = A;
                for (uint32_t st = lo; st <= hi; ++st) {
                    scen_fill(s, i, A, x, sec, st, horizon, &sc);
                    search_one(p, s, i, &sc, out);
                }
            }
            int j = 0;                                         /* 다음 위상 조합 (오도미터) */
            for (; j < s->n; ++j) {
                if (j == i) continue;
                if (++x[j] < s->irq[j].T) break;
                x[j] = 0;
            }
            if (j == s->n) break;
        }
        return;
    }
    for (int sec = -1; sec < s->nsec; ++sec) {                 /* 임계 순간 후보 */
        scen_fill(s, i, A, x, sec, sec < 0 ? A : A - 1, horizon, &sc);
        search_one(p, s, i, &sc, out);
    }
    for (uint32_t r = 0; r < nrand; ++r) {
        for (int j = 0; j < s->n; ++j) {
            uint32_t T = s->irq[j].T, w = 2 * win;
            uint64_t v = xs64();
            if (v % 4 == 0) x[j] = (uint32_t)((v >> 8) % 4);                  /* 거의 동시 */
            else if (T <= w) x[j] = (uint32_t)((v >> 8) % T);
            else { uint32_t q = (uint32_t)((v >> 8) % w); x[j] = q < win ? q : T - (q - win) - 1; }
        }
        int sec = (int)(xs64() % (uint64_t)(s->nsec + 1)) - 1;
        uint32_t st = sec < 0 ? A : A - (uint32_t)(xs64() % (s->sec[sec].len + 1u));
        scen_fill(s, i, A, x, sec, st, horizon, &sc);
        search_one(p, s, i, &sc, out);
    }
}

/* 최악 장면 다시 돌려 시간표 찍기 (주어짐). A = 대상 IRQ 도착 시각, [A−before, A+after] 만 */
static const char *EV_NAME[] = { "arrive", "LOST", "start", "end", "resume", "sec-on", "sec-off" };
static void print_timeline(const cm_params_t *p, const sys_t *s, const scen_t *sc, uint32_t A, uint32_t before, uint32_t after)
{
    static trace_ev_t tr[4096];
    simres_t o; o.tr = tr; o.tr_cap = 4096;
    if (sim_run(p, s, sc, &o)) { printf("   (sim failed)\n"); return; }
    for (uint32_t k = 0; k < o.tr_n; ++k) {
        const trace_ev_t *e = &tr[k];
        if (e->t + before < A || e->t > A + after) continue;
        const char *who = (e->ev == TR_SEC_ON || e->ev == TR_SEC_OFF) ? s->sec[e->irq].name
                        : e->ev == TR_RESUME ? "thread" : s->irq[e->irq].name;
        printf("   A%+6lld  %-7s %-26s %s\n", (long long)e->t - (long long)A, EV_NAME[e->ev], who,
               e->ev == TR_START ? HOW_NAME[e->how] : "");
    }
}

/* ===================== Part B — 분할 (TODO B1) ===================== */

/* TODO B1: ISR k 를 top half (top_c 사이클) + PendSV 식 bottom half 로 나눈다.
 * out = s 복사, out->irq[k].C = top_c, 맨 뒤에 새 IRQ {name, prio = bh_prio, C = C_k − top_c + handoff, T = T_k, D = bh_D}.
 * 반환: 새 인덱스. top_c > C_k 이거나 자리가 없으면 -1 (out 은 건드리지 않는다). */
static int split_isr(const sys_t *s, int k, uint32_t top_c, uint32_t handoff, uint8_t bh_prio, uint32_t bh_D,
                     const char *name, sys_t *out)
{
    (void)s; (void)k; (void)top_c; (void)handoff; (void)bh_prio; (void)bh_D; (void)name; (void)out;
    return -1;                                 /* TODO B1 */
}

/* ===================== Part C — 섹션 예산 (TODO C1) ===================== */

/* TODO C1: 이 시스템에 섹션 하나(kind, basepri)를 길이 S 로 더해도 sys_ok 인 최대 S.
 * 0 도 안 되면 -1, 상한 2^24 까지 다 되면 2^24. 이분 탐색 (R 은 S 에 단조). nsec == MAXSEC 면 -1. */
static int64_t section_budget(const cm_params_t *p, const sys_t *s, uint8_t kind, uint8_t basepri)
{
    (void)p; (void)s; (void)kind; (void)basepri;
    return -1;                                 /* TODO C1 */
}

/* ===================== 테스트 ===================== */
static sys_t mk2(uint8_t prigroup, irq_t a, irq_t b)
{
    sys_t s; memset(&s, 0, sizeof s);
    s.name = "t"; s.prigroup = prigroup; s.n = 2; s.irq[0] = a; s.irq[1] = b;
    return s;
}

/* 한 장면을 직접 돌린다: i 이외는 first[] 직접 지정 */
static int run_direct(const cm_params_t *p, const sys_t *s, const uint32_t *first, int sec, uint32_t sec_start,
                      simres_t *o)
{
    scen_t sc; memset(&sc, 0, sizeof sc);
    for (int k = 0; k < s->n; ++k) { sc.first[k] = first[k]; sc.once[k] = 1; }
    sc.sec = sec; sc.sec_start = sec_start; sc.horizon = 1000000;
    o->tr = NULL; o->tr_cap = 0;
    return sim_run(p, s, &sc, o);
}

static void part_a_rules(void)
{
    /* A1: 그룹 — 정의를 다른 식(시프트)으로 전수 대조 */
    int bad = 0;
    for (int pg = 0; pg < 8; ++pg)
        for (int pr = 0; pr < 256; ++pr) {
            int sh = pg + 1, want = sh >= 8 ? 0 : ((pr >> sh) << sh);
            if (prio_group((uint8_t)pr, (uint8_t)pg) != want) ++bad;
        }
    CHECK(bad == 0, "A1 prio_group: 256 x 8 exhaustive vs shift formula");
    CHECK(prio_group(0x50, 4) == 0x40, "A1 PRIGROUP 4: 0x50 -> group 0x40 (sub 0x10)");
    CHECK(prio_group(0xF0, 7) == 0 && prio_group(0x50, 3) == 0x50, "A1 PRIGROUP 7 -> all group 0; PRIGROUP 3 keeps 0x50");

    /* A2: 실행 우선순위 */
    CHECK(exec_priority(PRIO_NONE, 0, 0, 4) == PRIO_NONE, "A2 thread, no mask -> none");
    CHECK(exec_priority(PRIO_NONE, 1, 0, 4) == 0, "A2 PRIMASK -> 0");
    CHECK(exec_priority(PRIO_NONE, 0, 0x50, 4) == 0x40, "A2 BASEPRI 0x50 @PRIGROUP 4 -> group 0x40 (sub bits ignored)");
    CHECK(exec_priority(0x20, 0, 0x40, 4) == 0x20 && exec_priority(0x80, 0, 0x40, 4) == 0x40, "A2 min(active, BASEPRI)");
    CHECK(exec_priority(0x20, 1, 0x40, 4) == 0 && exec_priority(0, 1, 0, 4) == 0, "A2 PRIMASK wins, 0 stays 0");

    /* A3: 승자 고르기 */
    sys_t s; memset(&s, 0, sizeof s); s.prigroup = 4; s.n = 4;
    s.irq[0] = (irq_t){ "a", 0x50, 1, 100, 0 }; s.irq[1] = (irq_t){ "b", 0x40, 1, 100, 0 };
    s.irq[2] = (irq_t){ "c", 0x40, 1, 100, 0 }; s.irq[3] = (irq_t){ "d", 0x20, 1, 100, 0 };
    uint8_t pd[MAXIRQ] = { 1, 1, 1, 0 };
    CHECK(nvic_select(&s, pd, PRIO_NONE) == 1, "A3 same group: sub 0x40 beats 0x50, then lower number");
    CHECK(nvic_select(&s, pd, 0x40) == -1, "A3 threshold strict: group 0x40 cannot preempt 0x40");
    pd[3] = 1;
    CHECK(nvic_select(&s, pd, 0x40) == 3, "A3 group 0x20 preempts 0x40");
    pd[0] = pd[1] = pd[2] = pd[3] = 0;
    CHECK(nvic_select(&s, pd, PRIO_NONE) == -1, "A3 nothing pending -> -1");
    CHECK(key_before(&s, 1, 0) && key_before(&s, 1, 2) && !key_before(&s, 2, 1), "helper key_before: prio byte, then number");
    crit_t bp = { "bp", 10, SEC_BASEPRI, 0x50 };
    CHECK(sec_masks(&s, &bp, 1) && sec_masks(&s, &bp, 0) && !sec_masks(&s, &bp, 3), "helper sec_masks: BASEPRI 0x50 masks group 0x40, not 0x20");
}

static void part_a_sim_rules(void)
{
    const cm_params_t *p = &CM4;
    simres_t o;
    /* 하나: 진입 12 */
    sys_t s = mk2(4, (irq_t){ "a", 0x40, 20, 1000, 0 }, (irq_t){ "b", 0x40, 20, 1000, 0 });
    uint32_t f1[MAXIRQ] = { 10, NO_TIME };
    run_direct(p, &s, f1, -1, 0, &o);
    CHECK(o.lat_max[0] == 12 && o.resp_max[0] == 32, "SIM entry 12, response 12+20");
    /* 같은 우선순위 둘 동시: 두 번째는 꼬리 물기 6 */
    uint32_t f2[MAXIRQ] = { 10, 10 };
    run_direct(p, &s, f2, -1, 0, &o);
    CHECK(o.lat_max[1] == 12 + 20 + 6 && o.n_tail == 1 && o.n_exit == 1, "SIM tail-chain: 2nd latency 12+20+6");
    cm_params_t nt = *p; nt.tail_chain = 0;
    run_direct(&nt, &s, f2, -1, 0, &o);
    CHECK(o.lat_max[1] == 12 + 20 + 12 + 12 && o.n_tail == 0, "SIM tail-chain OFF: 2nd latency 12+20+12+12");
    /* late arrival: 낮은 그룹 0x40 이 10, 높은 그룹 0x20 이 13 */
    s = mk2(4, (irq_t){ "lo", 0x40, 20, 1000, 0 }, (irq_t){ "hi", 0x20, 20, 1000, 0 });
    uint32_t f3[MAXIRQ] = { 10, 13 };
    run_direct(p, &s, f3, -1, 0, &o);
    CHECK(o.lat_max[1] == 9 && o.n_late == 1 && o.lat_max[0] == 12 + 20 + 6, "SIM late arrival: high 9 (BM-A10 d=3), low tail-chains");
    cm_params_t nl = *p; nl.late_arrival = 0;
    run_direct(&nl, &s, f3, -1, 0, &o);
    CHECK(o.lat_max[1] == 9 + 12 && o.max_depth == 2, "SIM late OFF: low starts, high nests -> 21");
    /* 서브 우선순위는 선점하지 않는다: 같은 그룹 0x40(sub 0) 이 0x50 실행 중 도착 */
    s = mk2(4, (irq_t){ "sub1", 0x50, 100, 1000, 0 }, (irq_t){ "sub0", 0x40, 20, 1000, 0 });
    uint32_t f4[MAXIRQ] = { 10, 40 };
    run_direct(p, &s, f4, -1, 0, &o);
    CHECK(o.lat_max[1] == (10 + 12 + 100) - 40 + 6 && o.max_depth == 1, "SIM sub-priority: no preemption, waits + tail");
    s.prigroup = 3;                                            /* 같은 바이트, PRIGROUP 3 → 서로 다른 그룹 */
    run_direct(p, &s, f4, -1, 0, &o);
    CHECK(o.lat_max[1] == 12 && o.max_depth == 2, "SIM PRIGROUP 3: same bytes now preempt (nested 12)");
    /* 동시 펜딩: 서브가 좋은 쪽이 먼저 */
    s.prigroup = 4;
    uint32_t f5[MAXIRQ] = { 10, 10 };
    run_direct(p, &s, f5, -1, 0, &o);
    CHECK(o.lat_max[1] == 12 && o.lat_max[0] == 12 + 20 + 6, "SIM simultaneous: better sub first, other tail-chains");
    /* pop 선점: A 끝(=42) 4 사이클 뒤 B 도착 → 꼬리 물기 6 */
    s = mk2(4, (irq_t){ "a", 0x40, 20, 1000, 0 }, (irq_t){ "b", 0x40, 20, 1000, 0 });
    uint32_t f6[MAXIRQ] = { 10, 46 };
    run_direct(p, &s, f6, -1, 0, &o);
    CHECK(o.lat_max[1] == 6 && o.n_pop == 1, "SIM pop pre-emption: latency 6");
    /* PRIMASK 섹션: 시작 다음 사이클 도착 → len−1 + 12 */
    s.nsec = 1; s.sec[0] = (crit_t){ "pm", 50, SEC_PRIMASK, 0 };
    uint32_t f7[MAXIRQ] = { 101, NO_TIME };
    run_direct(p, &s, f7, 0, 100, &o);
    CHECK(o.lat_max[0] == 49 + 12, "SIM PRIMASK section 50: arrival 1 cycle in -> 49 + 12");
    uint32_t f7b[MAXIRQ] = { 100, NO_TIME };
    run_direct(p, &s, f7b, 0, 100, &o);
    CHECK(o.lat_max[0] == 12 && o.sec_on > 100, "SIM same-cycle arrival is taken before cpsid (model rule)");
    /* BASEPRI 0x40: 0x20 은 통과, 0x40 은 막힘. 0x50 바이트도 그룹 0x40 으로 같은 효과 */
    s = mk2(4, (irq_t){ "g20", 0x20, 20, 1000, 0 }, (irq_t){ "g40", 0x40, 20, 1000, 0 });
    s.nsec = 1; s.sec[0] = (crit_t){ "bp", 50, SEC_BASEPRI, 0x50 };
    uint32_t f8[MAXIRQ] = { 101, 101 };
    run_direct(p, &s, f8, 0, 100, &o);
    CHECK(o.lat_max[0] == 12 && o.lat_max[1] > 49, "SIM BASEPRI 0x50: group 0x20 passes, group 0x40 waits");
    /* 놓침: 펜딩 중 두 번 더 도착 */
    s = mk2(4, (irq_t){ "busy", 0x20, 200, 1000, 0 }, (irq_t){ "victim", 0x40, 5, 30, 0 });
    scen_t sc; memset(&sc, 0, sizeof sc);
    sc.first[0] = 10; sc.once[0] = 1; sc.first[1] = 15; sc.sec = -1; sc.horizon = 100;
    o.tr = NULL; o.tr_cap = 0;
    sim_run(p, &s, &sc, &o);
    CHECK(o.lost[1] == 2 && o.acts[1] == 1, "SIM pending bit is one bit: arrivals 15,45,75 during busy -> 1 run, 2 lost");
    /* FP: 스레드 FPCA=1, LSPEN=0 → 진입 29 */
    s = mk2(4, (irq_t){ "a", 0x40, 20, 1000, 0 }, (irq_t){ "b", 0x40, 20, 1000, 0 });
    s.fp_full = 1;
    run_direct(p, &s, f1, -1, 0, &o);
    CHECK(o.lat_max[0] == 29, "SIM fp_full: entry 12 + 17 = 29");
    /* PRIGROUP 7: 그룹이 하나 → 중첩 없음 */
    s = mk2(7, (irq_t){ "lo", 0xE0, 100, 1000, 0 }, (irq_t){ "hi", 0x00, 20, 1000, 0 });
    run_direct(p, &s, f4, -1, 0, &o);
    CHECK(o.max_depth == 1 && o.lat_max[1] == (10 + 12 + 100) - 40 + 6, "SIM PRIGROUP 7: nothing preempts");
}

static void part_a_analysis(void)
{
    const cm_params_t *p = &CM4;
    uint32_t L = 0, R = 0;
    /* 손 계산: 세 IRQ, PRIGROUP 4. hi(0x20) C=40 T=400, mid(0x40) C=60 T=600, mid2(0x50, 같은 그룹 뒤 순번) C=80 T=900
     * PRIMASK 30, BASEPRI(0x40) 100 */
    sys_t s; memset(&s, 0, sizeof s); s.prigroup = 4; s.n = 3; s.nsec = 2;
    s.irq[0] = (irq_t){ "hi", 0x20, 40, 400, 0 };
    s.irq[1] = (irq_t){ "mid", 0x40, 60, 600, 0 };
    s.irq[2] = (irq_t){ "mid2", 0x50, 80, 900, 0 };
    s.sec[0] = (crit_t){ "pm", 30, SEC_PRIMASK, 0 };
    s.sec[1] = (crit_t){ "bp", 60, SEC_BASEPRI, 0x40 };
    CHECK(blocking_bound(p, &s, 0) == 29, "A4 B(hi) = PRIMASK 30-1 (BASEPRI 0x40 doesn't mask 0x20)");
    CHECK(blocking_bound(p, &s, 1) == 11 + 80, "A4 B(mid) = max(PRIMASK 29, BASEPRI 59, same-group mid2 11+80) = 91");
    CHECK(blocking_bound(p, &s, 2) == 59, "A4 B(mid2) = BASEPRI 60-1 (mid is before it -> interference)");
    CHECK(latency_bound(p, &s, 0, &L) == 0 && L == 12 + 29, "A5 L(hi) = 12 + 29 = 41");
    CHECK(response_bound(p, &s, 0, &R) == 0 && R == 41 + 40, "A6 R(hi) = 41 + 40 = 81 (no preemptor)");
    CHECK(latency_bound(p, &s, 1, &L) == 0 && L == 12 + 91 + 64, "A5 L(mid) = 12 + 91 + (40+24) = 167");
    CHECK(latency_bound(p, &s, 2, &L) == 0 && L == 12 + 59 + 64 + 84, "A5 L(mid2) = 12 + 59 + 64 + 84 = 219");
    CHECK(response_bound(p, &s, 1, &R) == 0 && R == 12 + 91 + 60 + 64, "A6 R(mid) = 227 (hi once)");
    CHECK(response_bound(p, &s, 2, &R) == 0 && R == 12 + 59 + 80 + 84 + 64, "A6 R(mid2) = 299 (mid only before start)");
    /* 고정점이 두 바퀴 도는 경우: hi T=100 → ceil 이 늘어난다 */
    s.irq[0].T = 100;
    CHECK(latency_bound(p, &s, 2, &L) == 0 && L == 12 + 59 + 84 + 5 * 64, "A5 fixed point 219->347->411->475: 5 hi jobs in window");
    /* 발산: 이용률 > 1 */
    s.irq[0].T = 50;
    CHECK(latency_bound(p, &s, 2, &L) == -1 && !sys_ok(p, &s), "A5 overload -> -1, sys_ok 0");
}

/* 작은 세트에서 bound ≥ 전수 시뮬 최대 — 무작위 세트 + 네 가지 가상 코어 */
static void part_a_exhaustive(void)
{
    uint32_t sets = 0, checked = 0, viol = 0, equal = 0, runs = 0, fails = 0, worst_gap = 0;
    uint64_t sum_ratio_permille = 0;
    for (int trial = 0; trial < 1500 && sets < 300; ++trial) {
        sys_t s; memset(&s, 0, sizeof s);
        s.prigroup = (uint8_t)(3 + xs64() % 3);
        s.n = 2 + (int)(xs64() % 2);
        for (int k = 0; k < s.n; ++k) {
            uint32_t C = 1 + (uint32_t)(xs64() % 24);
            s.irq[k] = (irq_t){ "r", (uint8_t)((xs64() % 8) << 4), C, 0, 0 };
        }
        s.nsec = (int)(xs64() % 3);
        for (int k = 0; k < s.nsec; ++k)
            s.sec[k] = (crit_t){ "s", 1 + (uint32_t)(xs64() % 16), (uint8_t)(xs64() % 2), (uint8_t)((xs64() % 8) << 4) };
        s.fp_full = (uint8_t)(xs64() % 4 == 0);
        cm_params_t p = CM4;
        p.fetch = (xs64() % 3 == 0) ? 4u : 0u;
        p.tail_chain = (uint8_t)(xs64() % 4 != 0);
        p.late_arrival = (uint8_t)(xs64() % 4 != 0);
        uint32_t span = (s.n == 3) ? 50 : 120;
        for (int k = 0; k < s.n; ++k) s.irq[k].T = s.irq[k].C + cost_O(&p, &s) + 10 + (uint32_t)(xs64() % span);
        if (!sys_ok(&p, &s)) continue;
        ++sets;
        uint32_t maxT = 0, maxlen = 0;
        for (int k = 0; k < s.n; ++k) if (s.irq[k].T > maxT) maxT = s.irq[k].T;
        for (int k = 0; k < s.nsec; ++k) if (s.sec[k].len > maxlen) maxlen = s.sec[k].len;
        for (int i = 0; i < s.n; ++i) {
            uint32_t L = 0, R = 0;
            latency_bound(&p, &s, i, &L); response_bound(&p, &s, i, &R);
            search_t sr;
            sim_search(&p, &s, i, 0, 2 * maxT + maxlen + 8, R + 8, &sr);
            runs += sr.runs; fails += sr.fails;
            ++checked;
            if (sr.lat > L || sr.resp > R || sr.lost) ++viol;
            if (sr.lat == L) ++equal;
            if (L - sr.lat > worst_gap && sr.lat <= L) worst_gap = L - sr.lat;
            sum_ratio_permille += (uint64_t)sr.lat * 1000u / L;
        }
    }
    printf("   exhaustive: %u random sets, %u (set, irq) pairs, %u sim runs, violations=%u, L tight=%u, "
           "mean sim/bound=%llu permille, max gap=%u cycles, sim failures=%u\n",
           (unsigned)sets, (unsigned)checked, (unsigned)runs, (unsigned)viol, (unsigned)equal,
           checked ? (unsigned long long)(sum_ratio_permille / checked) : 0ull, (unsigned)worst_gap, (unsigned)fails);
    CHECK(sets >= 300 && fails == 0, "A exhaustive: 300 schedulable random sets simulated");
    CHECK(viol == 0, "A exhaustive: simulated max latency/response <= analytic bound, no lost IRQs");
    CHECK(equal > 0, "A exhaustive: some sets hit the bound exactly");

    /* 구성한 빡빡한 경우 — 등호 */
    const cm_params_t *p = &CM4;
    cm_params_t off = CM4; off.tail_chain = 0; off.late_arrival = 0;
    struct { const char *nm; const cm_params_t *pp; sys_t s; int i; } TC[4];
    TC[0].nm = "PRIMASK 40 only"; TC[0].pp = p; TC[0].i = 0;
    TC[0].s = mk2(4, (irq_t){ "a", 0x40, 10, 200, 0 }, (irq_t){ "b", 0x80, 1, 100000, 0 }); TC[0].s.n = 1;
    TC[0].s.nsec = 1; TC[0].s.sec[0] = (crit_t){ "pm", 40, SEC_PRIMASK, 0 };
    TC[1].nm = "hp interference, no tail/late"; TC[1].pp = &off; TC[1].i = 1;
    TC[1].s = mk2(4, (irq_t){ "hi", 0x20, 30, 300, 0 }, (irq_t){ "lo", 0x40, 10, 300, 0 });
    TC[2].nm = "same-group blocker, no tail/late"; TC[2].pp = &off; TC[2].i = 0;
    TC[2].s = mk2(4, (irq_t){ "sub0", 0x40, 10, 300, 0 }, (irq_t){ "sub1", 0x50, 30, 300, 0 });
    TC[3].nm = "blocking + hp, no tail/late"; TC[3].pp = &off; TC[3].i = 1;
    TC[3].s = mk2(4, (irq_t){ "hi", 0x20, 30, 400, 0 }, (irq_t){ "lo", 0x40, 10, 400, 0 });
    TC[3].s.nsec = 1; TC[3].s.sec[0] = (crit_t){ "pm", 25, SEC_PRIMASK, 0 };
    int eq_all = 1;
    for (int k = 0; k < 4; ++k) {
        uint32_t L = 0, R = 0; search_t sr;
        latency_bound(TC[k].pp, &TC[k].s, TC[k].i, &L); response_bound(TC[k].pp, &TC[k].s, TC[k].i, &R);
        sim_search(TC[k].pp, &TC[k].s, TC[k].i, 0, 1000, R + 8, &sr);
        printf("   tight: %-32s bound L=%3u  sim max L=%3u  (R bound %3u, sim %3u)\n", TC[k].nm,
               (unsigned)L, (unsigned)sr.lat, (unsigned)R, (unsigned)sr.resp);
        if (sr.lat != L) eq_all = 0;
    }
    CHECK(eq_all, "A tight cases: sim max == analytic bound (4 constructed sets)");
    /* 꼬리 물기 켜면 같은 세트가 18 줄어든다 (복귀 12 + 진입 12 → 꼬리 6) */
    uint32_t L1 = 0; search_t s1, s2;
    latency_bound(p, &TC[1].s, 1, &L1);
    sim_search(&off, &TC[1].s, 1, 0, 1000, 200, &s1);
    sim_search(p, &TC[1].s, 1, 0, 1000, 200, &s2);
    simres_t so; uint32_t fs[MAXIRQ] = { 100, 100 };
    run_direct(p, &TC[1].s, fs, -1, 0, &so);
    printf("   same set, tail-chain ON, simultaneous arrival only: lo latency %u\n", (unsigned)so.lat_max[1]);
    CHECK(so.lat_max[1] == 12 + 30 + 6, "A simultaneous arrival: 12 + 30 + tail 6 = 48 (< 53 worst phasing)");
    printf("   tail-chain gain on 'hp interference': sim %u -> %u (bound %u)\n", (unsigned)s1.lat, (unsigned)s2.lat, (unsigned)L1);
    CHECK(L1 == s1.lat && s2.lat == 11 + 6 + 30 + 6, "A tail-chain ON: worst = hp arriving 11 cycles into lo stacking (late) = 53");
}

/* ===================== Part B/C — 모터 제어 세트 ===================== */
enum { M_CUR, M_ENC, M_CAN, M_UART, M_TICK };
static const uint32_t MHZ = 168;
static sys_t motor_baseline(void)
{
    sys_t s; memset(&s, 0, sizeof s);
    s.name = "baseline"; s.prigroup = 4; s.n = 5; s.nsec = 2; s.fp_full = 1;
    s.irq[M_CUR]  = (irq_t){ "CUR  20kHz current loop", 0x10, 1680, 8400, 4200 };   /* 10 us / 50 us, D = 반주기 */
    s.irq[M_ENC]  = (irq_t){ "ENC  encoder capture",    0x20,  150, 16800, 0 };     /* 0.9 us / 100 us */
    s.irq[M_CAN]  = (irq_t){ "CAN  RX FIFO0 (parse)",   0x00, 2520, 7896, 0 };      /* 15 us / 47 us (1 Mbit/s 최소 프레임) */
    s.irq[M_UART] = (irq_t){ "UART RX no FIFO",         0x40,  200, 14583, 0 };     /* 115200 bps 1 char = 86.8 us */
    s.irq[M_TICK] = (irq_t){ "TICK SysTick 1kHz",       0x60,  400, 168000, 0 };
    s.sec[0] = (crit_t){ "LOG  PRIMASK logger copy", 3360, SEC_PRIMASK, 0 };         /* 20 us */
    s.sec[1] = (crit_t){ "KERN BASEPRI 0x50 kernel",  600, SEC_BASEPRI, 0x50 };      /* 3.6 us */
    return s;
}
typedef struct { const char *name; cm_params_t p; sys_t s; } variant_t;

static int motor_variants(variant_t *v)
{
    int n = 0;
    cm_params_t base = CM4; base.fetch = 10;                    /* 168 MHz 5 WS, 최악 = ART miss (BM-A10) */
    sys_t s0 = motor_baseline();
    v[n++] = (variant_t){ "V0 baseline", base, s0 };
    v[n] = (variant_t){ "V1 tail-chain OFF (hypothetical)", base, s0 }; v[n].p.tail_chain = 0; ++n;
    v[n] = (variant_t){ "V2 late-arrival OFF (hypothetical)", base, s0 }; v[n].p.late_arrival = 0; ++n;
    v[n] = (variant_t){ "V3 LOG PRIMASK -> BASEPRI 0x40", base, s0 }; v[n].s.sec[0].kind = SEC_BASEPRI; v[n].s.sec[0].basepri = 0x40; ++n;
    v[n] = (variant_t){ "V4 LOG section 3360 -> 336", base, s0 }; v[n].s.sec[0].len = 336; ++n;
    v[n] = (variant_t){ "V5 CAN sub-prio 0x00 -> group 0x60", base, s0 }; v[n].s.irq[M_CAN].prio = 0x60; ++n;
    v[n] = (variant_t){ "V6 CAN split: top 336 + PendSV", base, s0 };
    split_isr(&s0, M_CAN, 336, 34, 0xF0, 3 * 7896, "PSV  CAN bottom half", &v[n].s); ++n;
    v[n] = (variant_t){ "V7 FP lazy (LSPEN=1)", base, s0 }; v[n].s.fp_full = 0; ++n;
    v[n] = (variant_t){ "V8 vectors+ISRs in SRAM (fetch 0)", base, s0 }; v[n].p.fetch = 0; ++n;
    v[n] = (variant_t){ "V9 = V3+V4+V5+V6+V7", base, s0 };
    { sys_t t = s0; t.sec[0].kind = SEC_BASEPRI; t.sec[0].basepri = 0x40; t.sec[0].len = 336; t.irq[M_CAN].prio = 0x60; t.fp_full = 0;
      split_isr(&t, M_CAN, 336, 34, 0xF0, 3 * 7896, "PSV  CAN bottom half", &v[n].s); }
    ++n;
    return n;
}

static void cyc_us(char *buf, size_t n, uint32_t cyc) { snprintf(buf, n, "%u.%02u", (unsigned)(cyc / MHZ), (unsigned)(cyc % MHZ * 100 / MHZ)); }

static void part_b_table(void)
{
    variant_t v[12];
    int nv = motor_variants(v);
    printf("== Part B: R bound per IRQ (us @168 MHz), '!' = misses deadline D (or R > T)\n");
    printf("   %-36s", "variant");
    for (int i = 0; i < 6; ++i) printf(" | %-9s", i < 5 ? (const char *[]){ "CUR", "ENC", "CAN", "UART", "TICK" }[i] : "PSV");
    printf("\n");
    uint32_t Rb[12][MAXIRQ]; int okv[12];
    memset(Rb, 0, sizeof Rb);
    for (int k = 0; k < nv; ++k) {
        printf("   %-36s", v[k].name);
        okv[k] = sys_ok(&v[k].p, &v[k].s);
        for (int i = 0; i < 6; ++i) {
            char b[24], u[16];
            if (i >= v[k].s.n) { printf(" | %-9s", "-"); continue; }
            uint32_t R = 0;
            if (response_bound(&v[k].p, &v[k].s, i, &R)) { printf(" | %-9s", "diverge"); Rb[k][i] = NO_TIME; continue; }
            Rb[k][i] = R;
            cyc_us(u, sizeof u, R);
            snprintf(b, sizeof b, "%s%s", u, (R > dl(&v[k].s, i) || R > v[k].s.irq[i].T) ? "!" : "");
            printf(" | %-9s", b);
        }
        printf("\n");
    }
    /* CUR 지연: 해석 vs 시뮬 탐색 */
    printf("== Part B: CUR latency/response, bound vs simulated max (cycles; sim = critical-instant candidates + 3000 random phasings)\n");
    printf("   %-36s | L bound | L sim | R bound | R sim | D=4200\n", "variant");
    uint32_t viol = 0, fails = 0;
    scen_t worst_cur[2];
    memset(worst_cur, 0, sizeof worst_cur);
    worst_cur[0].sec = worst_cur[1].sec = -1;
    uint32_t simL[12] = {0}, simR[12][MAXIRQ];
    memset(simR, 0, sizeof simR);
    for (int k = 0; k < nv; ++k) {
        for (int i = 0; i < v[k].s.n; ++i) {
            uint32_t L = 0, R = 0; search_t sr;
            if (latency_bound(&v[k].p, &v[k].s, i, &L) || response_bound(&v[k].p, &v[k].s, i, &R)) continue;
            g_rng = 0xC0FFEEull + (uint64_t)k * 77u + (uint64_t)i;
            sim_search(&v[k].p, &v[k].s, i, i == M_CUR ? 3000 : 400, 40000, R + 64, &sr);
            fails += sr.fails;
            if (sr.lat > L || sr.resp > R) { ++viol; printf("   VIOLATION %s irq %d\n", v[k].name, i); }
            if (i == M_CUR && (k == 0 || k == nv - 1)) worst_cur[k == 0 ? 0 : 1] = sr.worst;
            if (i == M_CUR) simL[k] = sr.lat;
            simR[k][i] = sr.resp;
            if (i == M_CUR)
                printf("   %-36s | %7u | %5u | %7u | %5u | %s\n", v[k].name, (unsigned)L, (unsigned)sr.lat,
                       (unsigned)R, (unsigned)sr.resp, R <= 4200 ? "meets" : "MISSES");
        }
    }
    printf("== Part B: every IRQ, R bound / R sim max (cycles)\n");
    for (int k = 0; k < nv; ++k) {
        printf("   %-36s", v[k].name);
        for (int i = 0; i < v[k].s.n; ++i) {
            char b[24];
            snprintf(b, sizeof b, "%u/%u", (unsigned)Rb[k][i], (unsigned)simR[k][i]);
            printf(" | %-11s", b);
        }
        printf("\n");
    }
    printf("== Part B: worst phasing found for CUR, baseline (A = CUR arrival)\n");
    print_timeline(&v[0].p, &v[0].s, &worst_cur[0], 40000, 3400, 8000);
    printf("== Part B: worst phasing found for CUR, V9\n");
    print_timeline(&v[nv - 1].p, &v[nv - 1].s, &worst_cur[1], 40000, 100, 2000);
    CHECK(viol == 0 && fails == 0, "B every variant x every IRQ: simulated max <= bound");
    CHECK(!okv[0] && Rb[0][M_CUR] > 4200, "B baseline: current loop misses its 25 us deadline");
    CHECK(Rb[3][M_CUR] < Rb[0][M_CUR] && Rb[3][M_UART] == Rb[0][M_UART], "B V3 BASEPRI: CUR gains, UART (still masked) unchanged");
    CHECK(Rb[5][M_CUR] < Rb[0][M_CUR] && Rb[5][M_CAN] > Rb[0][M_CAN], "B V5 sub->group: CUR gains, CAN loses");
    CHECK(Rb[6][M_CUR] < Rb[0][M_CUR] && v[6].s.n == 6 && Rb[6][5] > Rb[0][M_CAN], "B V6 split: CUR gains, bottom half slower than old CAN");
    CHECK(Rb[1][M_CUR] > Rb[0][M_CUR] && simL[1] > simL[0] && Rb[1][M_ENC] > Rb[0][M_ENC] + 4000, "B V1 tail-chain off: worse in sim and bound; ENC bound jumps (step)");
    CHECK(Rb[7][M_CUR] < Rb[0][M_CUR] && Rb[8][M_CUR] < Rb[0][M_CUR], "B V7 lazy FP and V8 SRAM: smaller overheads");
    CHECK(okv[nv - 1] && Rb[nv - 1][M_CUR] <= 4200, "B V9 combined: every IRQ meets its deadline");
}

static void part_c_motor(void)
{
    variant_t v[12];
    int nv = motor_variants(v);
    const variant_t *f = &v[nv - 1];
    printf("== Part C: final design (V9) at %u MHz\n", (unsigned)MHZ);
    printf("   %-24s prio grp |    C     T     D |  B     L bound   R bound  | slack(us)\n", "IRQ");
    int all = 1;
    for (int i = 0; i < f->s.n; ++i) {
        uint32_t L = 0, R = 0; char lu[16], ru[16], su[16];
        latency_bound(&f->p, &f->s, i, &L); response_bound(&f->p, &f->s, i, &R);
        cyc_us(lu, sizeof lu, L); cyc_us(ru, sizeof ru, R);
        uint32_t D = dl(&f->s, i);
        if (R <= D) cyc_us(su, sizeof su, D - R); else snprintf(su, sizeof su, "MISS");
        if (R > D) all = 0;
        printf("   %-24s 0x%02X 0x%02X | %5u %5u %5u | %5u %6u=%-6s %6u=%-6s | %s\n", f->s.irq[i].name,
               f->s.irq[i].prio, grp(&f->s, i), (unsigned)f->s.irq[i].C, (unsigned)f->s.irq[i].T, (unsigned)D,
               (unsigned)blocking_bound(&f->p, &f->s, i), (unsigned)L, lu, (unsigned)R, ru, su);
    }
    CHECK(all, "C final design: all R <= D");
    int64_t pm = section_budget(&f->p, &f->s, SEC_PRIMASK, 0);
    int64_t bp = section_budget(&f->p, &f->s, SEC_BASEPRI, 0x40);
    int64_t bp2 = section_budget(&f->p, &f->s, SEC_BASEPRI, 0x20);
    printf("   budget: one more PRIMASK section <= %lld cycles, BASEPRI 0x40 <= %lld, BASEPRI 0x20 <= %lld\n",
           (long long)pm, (long long)bp, (long long)bp2);
    const int64_t bud[3] = { pm, bp, bp2 }; const uint8_t bk[3] = { SEC_PRIMASK, SEC_BASEPRI, SEC_BASEPRI }, bb[3] = { 0, 0x40, 0x20 };
    for (int b = 0; b < 3; ++b) {                /* 예산 + 1 에서 누가 깨지나 */
        sys_t u = f->s; u.sec[u.nsec] = (crit_t){ "b", (uint32_t)bud[b] + 1, bk[b], bb[b] }; u.nsec++;
        printf("   at budget+1 (%s 0x%02X) fails:", bk[b] == SEC_PRIMASK ? "PRIMASK" : "BASEPRI", bb[b]);
        for (int i = 0; i < u.n; ++i) {
            uint32_t R = 0;
            if (response_bound(&f->p, &u, i, &R) || R > dl(&u, i) || R > u.irq[i].T)
                printf(" %.4s (R %u vs D %u, T %u)", u.irq[i].name, (unsigned)R, (unsigned)dl(&u, i), (unsigned)u.irq[i].T);
        }
        printf("\n");
    }
    /* 예산 검증: 경계에서 됨, +1 이면 안 됨 */
    sys_t t = f->s; t.sec[t.nsec] = (crit_t){ "b", (uint32_t)pm, SEC_PRIMASK, 0 }; t.nsec++;
    int at = sys_ok(&f->p, &t); t.sec[t.nsec - 1].len = (uint32_t)pm + 1;
    int over = sys_ok(&f->p, &t);
    CHECK(pm > 0 && at && !over, "C1 PRIMASK budget is exact (ok at S, fails at S+1)");
    CHECK(bp > pm && bp2 >= bp, "C1 BASEPRI budgets are larger (CUR/ENC not masked)");
    sys_t full = f->s; full.nsec = MAXSEC;
    CHECK(section_budget(&f->p, &full, SEC_PRIMASK, 0) == -1, "C1 no slot -> -1");
    sys_t bad = v[0].s;
    CHECK(section_budget(&v[0].p, &bad, SEC_PRIMASK, 0) == -1, "C1 baseline already misses -> -1");
    sys_t none = f->s; none.n = 1; none.nsec = 0; none.irq[0].prio = 0x10;   /* CUR 만, BASEPRI 0x20 은 아무도 안 막음 */
    CHECK(section_budget(&f->p, &none, SEC_BASEPRI, 0x20) == (1 << 24), "C1 section that masks nobody -> cap 2^24");
    /* B1 경계 */
    sys_t o2;
    CHECK(split_isr(&f->s, M_CAN, 99999, 0, 0xF0, 0, "x", &o2) == -1, "B1 top > C -> -1");
    sys_t fullirq = f->s; fullirq.n = MAXIRQ;
    CHECK(split_isr(&fullirq, 0, 1, 0, 0xF0, 0, "x", &o2) == -1, "B1 no slot -> -1");
    int ni = split_isr(&v[0].s, M_CAN, 336, 34, 0xF0, 100, "bh", &o2);
    CHECK(ni == 5 && o2.n == 6 && o2.irq[M_CAN].C == 336 && o2.irq[5].C == 2520 - 336 + 34 && o2.irq[5].T == 7896
          && o2.irq[5].prio == 0xF0 && o2.irq[5].D == 100, "B1 split fields");
}

int main(void)
{
    part_a_rules();
    part_a_sim_rules();
    part_a_analysis();
    part_a_exhaustive();
    part_b_table();
    part_c_motor();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
