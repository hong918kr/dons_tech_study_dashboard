/* SIV-A04 열 특성화와 Throttling 검증
 *
 * 검증 대상(DUT)은 펌웨어 스타일 thermal governor(주어진 코드)이고, 여러분이 쓰는 것은 "그 governor 를 믿지 않는 쪽"이다:
 * 3노드 열 RC 플랜트 + 온도 센서 오차 모델(주어짐) 위에서 governor 를 돌려 trace 를 만들고,
 * 그 trace 를 스펙과 대조하는 checker · 위험도 정량화 · 판정 리포트를 작성한다.
 *
 *   die(C0) --R0-- package(C1) --R1-- heatsink(C2) --R2-- ambient
 *    ^ P(t) = a * Ceff * V^2 * f + Pleak(T, V)
 *
 * 모든 파라미터는 대표값(가정)이다. Forward Euler 고정 dt = 1 ms (안정 조건 dt < 0.1 s, Part A 에서 확인).
 * 시뮬레이션 전체가 결정적이다(난수·벽시계 시간 없음). 열 RC 모델의 전체 이론은 SIM-M08 몫이고 여기엔 축약본만 넣었다.
 * mock MMIO 는 SIV-B02 를, 판정 리포트 형식은 SIV-B14 를 축약해 넣었다.
 */
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================= 하네스 ======================= */
static int g_total, g_pass;
#define CHECK(cond, ...) do { g_total++; if (cond) { g_pass++; printf("PASS  "); } else printf("FAIL  "); \
                              printf(__VA_ARGS__); printf("\n"); } while (0)

/* ======================= 주어진 부분: 플랜트 파라미터 · OPP ======================= */
#define DT_S       0.001            /* 플랜트 적분 간격 1 ms */
#define POLL_MS    10u              /* governor 폴링 주기 */
#define CONV_MS    5u               /* 센서 변환 주기 */
#define TQ_MC      250              /* 센서 양자화 0.25 ℃ */

typedef struct { double c[3]; double r[3]; double t_amb; } therm_par_t;   /* c[J/K], r[K/W]: r[0]=die-pkg, r[1]=pkg-sink, r[2]=sink-amb */
typedef struct { double t[3]; } therm_t;                                  /* ℃ */

static const therm_par_t PLANT_NOM = { .c = {0.2, 3.0, 50.0}, .r = {0.5, 0.5, 1.5}, .t_amb = 45.0 };

#define NOPP 6
#define LV_MIN_PERF (NOPP - 1)      /* 가장 느린 OPP (level 숫자가 클수록 느림) */
typedef struct { uint32_t f_mhz, v_mv; } opp_t;
static const opp_t OPP[NOPP] = { {2000, 1000}, {1700, 925}, {1400, 850}, {1100, 775}, {800, 700}, {500, 650} };

#define CEFF_NF   15.0
#define LEAK_P0_W 0.8               /* V=1.0 V, 25 ℃ 에서의 누설 */
#define LEAK_K    0.02              /* 1/K */
static double soc_power_w(uint32_t v_mv, uint32_t f_mhz, double activity, double t_die) {
    double v = v_mv / 1000.0;
    double dyn = activity * CEFF_NF * 1e-9 * v * v * (f_mhz * 1e6);
    return dyn + LEAK_P0_W * v * exp(LEAK_K * (t_die - 25.0));
}

/* V 가 f 를 지탱할 수 있나: 같은 f 이상의 OPP 중 전압이 v 이하인 것이 있으면 안전 */
static bool vf_safe(uint32_t v_mv, uint32_t f_mhz) {
    for (int i = NOPP - 1; i >= 0; i--)
        if (OPP[i].f_mhz >= f_mhz) return v_mv >= OPP[i].v_mv;
    return false;
}

/* ======================= Part A-1: 열 RC 적분 ======================= */
static double therm_dt_limit(const therm_par_t *p) {
    double g0 = 1.0 / p->r[0], g1 = 1.0 / p->r[1], g2 = 1.0 / p->r[2];
    double m0 = g0 / p->c[0], m1 = (g0 + g1) / p->c[1], m2 = (g1 + g2) / p->c[2];
    double m = m0 > m1 ? m0 : m1;
    if (m2 > m) m = m2;
    return 1.0 / m;
}
static void therm_step(const therm_par_t *p, therm_t *s, double p_w, double dt) {
    double q01 = (s->t[0] - s->t[1]) / p->r[0];
    double q12 = (s->t[1] - s->t[2]) / p->r[1];
    double q2a = (s->t[2] - p->t_amb) / p->r[2];
    s->t[0] += dt * (p_w - q01) / p->c[0];
    s->t[1] += dt * (q01 - q12) / p->c[1];
    s->t[2] += dt * (q12 - q2a) / p->c[2];
}

/* ======================= 주어진 부분: mock SoC (MMIO) · 센서 ======================= */
enum { R_TSENS_DATA = 0x00, R_TSENS_SEQ = 0x04, R_PMU_VOLT = 0x10, R_PMU_FREQ = 0x14, R_PMU_SHDN = 0x20 };
#define VF_LOG_CAP 8192
typedef struct { uint32_t t_ms, v_mv, f_mhz; } vf_ev_t;

typedef enum { STUCK_NONE, STUCK_VALUE, STUCK_FROZEN } stuck_t;   /* VALUE: 값만 고정(seq 는 증가), FROZEN: 변환 자체 정지 */
typedef struct { int32_t offset_mc; uint32_t lag_ms; stuck_t stuck; uint32_t stuck_at_ms; } sens_err_t;

typedef struct {
    int32_t data_q; uint32_t seq;                 /* TSENS_DATA (LSB=0.25 ℃, signed), TSENS_SEQ */
    uint32_t v_mv, f_mhz; bool shut;
    double lp;                                    /* 센서 lag 필터 상태 */
    uint32_t now_ms;
    vf_ev_t vf[VF_LOG_CAP]; uint32_t nvf;
} soc_t;

static uint32_t reg_read(const soc_t *s, uint32_t off) {
    switch (off) {
    case R_TSENS_DATA: return (uint32_t)s->data_q;
    case R_TSENS_SEQ:  return s->seq;
    case R_PMU_VOLT:   return s->v_mv;
    case R_PMU_FREQ:   return s->f_mhz;
    default:           return 0;
    }
}
static void reg_write(soc_t *s, uint32_t off, uint32_t val) {
    if (off == R_PMU_SHDN) { s->shut = true; return; }
    if (off == R_PMU_VOLT) s->v_mv = val;
    else if (off == R_PMU_FREQ) s->f_mhz = val;
    else return;
    if (s->nvf < VF_LOG_CAP) { s->vf[s->nvf] = (vf_ev_t){ s->now_ms, s->v_mv, s->f_mhz }; s->nvf++; }
}
/* 1 ms 마다 호출: true 온도 → lag(1차 저역통과) → 오프셋 → 양자화 → TSENS_DATA */
static void tsens_tick(soc_t *s, const sens_err_t *e, double t_true, uint32_t ms) {
    s->lp = e->lag_ms ? s->lp + (t_true - s->lp) * (1.0 / e->lag_ms) : t_true;
    if (ms % CONV_MS) return;
    if (e->stuck == STUCK_FROZEN && ms >= e->stuck_at_ms) return;      /* 변환 정지: seq 도 안 변함 */
    s->seq++;
    if (e->stuck == STUCK_VALUE && ms >= e->stuck_at_ms) return;       /* seq 는 돌지만 값은 고정 */
    double mc = s->lp * 1000.0 + e->offset_mc;
    s->data_q = (int32_t)floor(mc / TQ_MC + 0.5);
}

/* ======================= 주어진 부분: DUT — 펌웨어 thermal governor (버그 스위치 포함) ======================= */
#define BUG_HYST_SIGN (1u << 0)   /* release 를 thr-hyst 가 아니라 thr+hyst 로 계산 */
#define BUG_SKIP_STEP (1u << 1)   /* L2 에서 내릴 때 L3 를 건너뛰고 L4 로 */
#define BUG_DROP_STEP (1u << 2)   /* L2 에서 내리는 요청이 사라짐 */
#define BUG_NO_STALE  (1u << 3)   /* TSENS_SEQ 감시 없음 */
#define BUG_VF_ORDER  (1u << 4)   /* 올릴 때 주파수를 전압보다 먼저 변경 */
#define BUG_NO_SD     (1u << 5)   /* shutdown 경로 누락 */

typedef struct { int32_t thr_mc, rel_mc, emg_mc, sd_mc; uint32_t down_ms, up_ms, stale_ms; } gov_cfg_t;
static const gov_cfg_t GOV_NOM = { 90000, 84000, 98000, 105000, 100, 1000, 100 };

typedef struct { gov_cfg_t c; int level; bool shut, failsafe; uint32_t last_ms, seq_seen, seq_ms, bugs; } gov_t;

static void gov_apply(soc_t *s, gov_t *g, int nl, uint32_t now) {
    if (nl == g->level) return;
    const opp_t *o = &OPP[nl];
    bool faster = nl < g->level;
    if (faster && !(g->bugs & BUG_VF_ORDER)) {        /* 빨라질 때: 전압 먼저, 그다음 주파수 */
        reg_write(s, R_PMU_VOLT, o->v_mv);
        reg_write(s, R_PMU_FREQ, o->f_mhz);
    } else if (faster) {
        reg_write(s, R_PMU_FREQ, o->f_mhz);
        reg_write(s, R_PMU_VOLT, o->v_mv);
    } else {                                          /* 느려질 때: 주파수 먼저, 그다음 전압 */
        reg_write(s, R_PMU_FREQ, o->f_mhz);
        reg_write(s, R_PMU_VOLT, o->v_mv);
    }
    g->level = nl;
    g->last_ms = now;
}
static void gov_step(soc_t *s, gov_t *g, uint32_t now) {
    if (g->shut) return;
    uint32_t seq = reg_read(s, R_TSENS_SEQ);
    if (seq != g->seq_seen) { g->seq_seen = seq; g->seq_ms = now; }
    if (!(g->bugs & BUG_NO_STALE) && (g->failsafe || now - g->seq_ms >= g->c.stale_ms)) {
        g->failsafe = true;                           /* 센서를 못 믿음 → 최저 OPP 로 고정 (latch) */
        gov_apply(s, g, LV_MIN_PERF, now);
        return;
    }
    int32_t t = (int32_t)reg_read(s, R_TSENS_DATA) * TQ_MC;
    int32_t rel = (g->bugs & BUG_HYST_SIGN) ? g->c.thr_mc + (g->c.thr_mc - g->c.rel_mc) : g->c.rel_mc;
    if (t >= g->c.sd_mc && !(g->bugs & BUG_NO_SD)) {
        reg_write(s, R_PMU_SHDN, 1);
        g->shut = true;
        return;
    }
    if (t >= g->c.emg_mc) { gov_apply(s, g, LV_MIN_PERF, now); return; }
    if (t >= g->c.thr_mc) {
        if (g->level < LV_MIN_PERF && now - g->last_ms >= g->c.down_ms) {
            int nl = g->level + 1;
            if ((g->bugs & BUG_SKIP_STEP) && g->level == 2) nl = 4;
            if ((g->bugs & BUG_DROP_STEP) && g->level == 2) return;
            gov_apply(s, g, nl, now);
        }
    } else if (t < rel && g->level > 0 && now - g->last_ms >= g->c.up_ms) {
        gov_apply(s, g, g->level - 1, now);
    }
}

/* ======================= 주어진 부분: 시뮬레이션 러너 + trace ======================= */
#define TRACE_CAP 40000
typedef struct { uint32_t t_ms, seq_age_ms; int32_t true_mc, read_mc; int8_t lv_before, lv_after; bool shut_before, shut_after; } row_t;
typedef struct { row_t row[TRACE_CAP]; uint32_t n; vf_ev_t vf[VF_LOG_CAP]; uint32_t nvf; double work; } trace_t;   /* work = Σ f·a·dt */

typedef struct { uint32_t until_ms; double activity; } seg_t;
typedef struct {
    const seg_t *wl; int nseg; uint32_t dur_ms;
    therm_t init; double t_amb;
    gov_cfg_t gov; uint32_t bugs;
    sens_err_t sens;
    uint32_t fan_fail_ms; double fan_factor;          /* fan_fail_ms 시점에 r[2] *= fan_factor */
} run_cfg_t;

static double wl_activity(const seg_t *w, int n, uint32_t ms) {
    for (int i = 0; i < n; i++) if (ms < w[i].until_ms) return w[i].activity;
    return w[n - 1].activity;
}
static soc_t g_soc;
static void sim_run(const run_cfg_t *rc, trace_t *tr) {
    soc_t *s = &g_soc;
    memset(s, 0, sizeof *s);
    therm_par_t par = PLANT_NOM;
    par.t_amb = rc->t_amb;
    therm_t th = rc->init;
    s->v_mv = OPP[0].v_mv; s->f_mhz = OPP[0].f_mhz;
    s->lp = th.t[0]; s->seq = 1;
    s->data_q = (int32_t)floor((th.t[0] * 1000.0 + rc->sens.offset_mc) / TQ_MC + 0.5);
    gov_t g; memset(&g, 0, sizeof g);
    g.c = rc->gov; g.bugs = rc->bugs; g.seq_seen = 1;
    tr->n = 0; tr->nvf = 0; tr->work = 0.0;
    uint32_t seq_last = s->seq, seq_ms = 0;
    for (uint32_t ms = 0; ms < rc->dur_ms; ms++) {
        s->now_ms = ms;
        if (rc->fan_fail_ms && ms == rc->fan_fail_ms) par.r[2] *= rc->fan_factor;
        double a = wl_activity(rc->wl, rc->nseg, ms);
        double p = s->shut ? 0.0 : soc_power_w(s->v_mv, s->f_mhz, a, th.t[0]);
        if (!s->shut) tr->work += s->f_mhz * a * DT_S;
        therm_step(&par, &th, p, DT_S);
        tsens_tick(s, &rc->sens, th.t[0], ms);
        if (s->seq != seq_last) { seq_last = s->seq; seq_ms = ms; }
        if (ms % POLL_MS == 0 && tr->n < TRACE_CAP) {
            row_t r;
            memset(&r, 0, sizeof r);
            r.t_ms = ms; r.seq_age_ms = ms - seq_ms;
            r.true_mc = (int32_t)(th.t[0] * 1000.0);
            r.read_mc = (int32_t)reg_read(s, R_TSENS_DATA) * TQ_MC;
            r.lv_before = (int8_t)g.level; r.shut_before = g.shut;
            gov_step(s, &g, ms);
            r.lv_after = (int8_t)g.level; r.shut_after = g.shut;
            tr->row[tr->n++] = r;
        }
    }
    tr->nvf = s->nvf;
    memcpy(tr->vf, s->vf, s->nvf * sizeof s->vf[0]);
}

/* ======================= 공통: 스펙 · 위반 기록 ======================= */
typedef struct { int32_t thr_mc, rel_mc, emg_mc, sd_mc, tj_max_mc; uint32_t down_ms, up_ms, stale_ms, poll_ms; } spec_t;
static const spec_t SPEC_NOM = { 90000, 84000, 98000, 105000, 105000, 100, 1000, 100, POLL_MS };

#define CHAT_WIN_MS 10000u
#define CHAT_MAX    6

typedef enum { V_TRIP_EARLY, V_TRIP_LATE, V_HYST_UP_ABOVE_REL, V_UP_DWELL, V_DOWN_DWELL, V_UP_LATE, V_STEP_SKIP, V_VF_ORDER,
               V_EMG_LATE, V_SD_MISSED, V_SD_EARLY, V_CHATTER, V_STALE_NOT_DETECTED, V_TJ_EXCEEDED, V_COUNT } vcode_t;
static const char *const VNAME[V_COUNT] = {
    "TRIP_EARLY", "TRIP_LATE", "HYST_UP_ABOVE_REL", "UP_DWELL", "DOWN_DWELL", "UP_LATE", "STEP_SKIP", "VF_ORDER",
    "EMG_LATE", "SD_MISSED", "SD_EARLY", "CHATTER", "STALE_NOT_DETECTED", "TJ_EXCEEDED" };

typedef struct { uint32_t t_ms; int from, to; int32_t value_mc, limit_mc; } viol_t;
typedef struct { uint32_t count[V_COUNT]; viol_t first[V_COUNT]; } report_t;

static void rep_init(report_t *r) { memset(r, 0, sizeof *r); }
/* 코드별 첫 발생만 저장, count 는 전부 센다 */
static void viol(report_t *r, vcode_t c, uint32_t t_ms, int from, int to, int32_t value_mc, int32_t limit_mc) {
    if (r->count[c]++ == 0) r->first[c] = (viol_t){ t_ms, from, to, value_mc, limit_mc };
}
static uint32_t rep_total(const report_t *r) { uint32_t n = 0; for (int i = 0; i < V_COUNT; i++) n += r->count[i]; return n; }
/* 시간상 가장 먼저 발생한 위반 코드. 없으면 -1 */
static int rep_first_code(const report_t *r) {
    int best = -1;
    for (int i = 0; i < V_COUNT; i++)
        if (r->count[i] && (best < 0 || r->first[i].t_ms < r->first[best].t_ms)) best = i;
    return best;
}

/* ======================= Part A-2: 정책 checker (센서값 기준) ======================= */
/* 규칙 (각 위반은 viol() 로 기록, read = 그 폴링에서 governor 가 읽은 값, d = lv_after - lv_before, d>0 이 느려짐):
 *  shut_before 인 행은 건너뛴다. stale = (seq_age_ms >= sp->stale_ms) 이면 read 를 믿을 수 없으므로 SD/EMG/TRIP_* 조건에서 제외.
 *  SD_MISSED   read >= sd 인데 shut_after 아님            SD_EARLY  shut_after 인데 read < sd
 *  EMG_LATE    read >= emg 인데 lv_after != LV_MIN_PERF
 *  d > 0 : read < thr → TRIP_EARLY; emergency/stale 가 아니면 d > 1 → STEP_SKIP, 직전 변경 후 down_ms 미만 → DOWN_DWELL
 *  d < 0 : read >= rel → HYST_UP_ABOVE_REL, d < -1 → STEP_SKIP, 직전 변경 후 up_ms 미만 → UP_DWELL
 *  방향 전환(reversal) 이 CHAT_WIN_MS 안에 CHAT_MAX 초과 → CHATTER
 *  마감(deadline): read >= thr && lv_after < 최저 인 상태가 d>0 없이 (down_ms + poll_ms) 초과 지속 → TRIP_LATE (재무장)
 *                  read < rel && lv_after > 0 가 d<0 없이 (up_ms + poll_ms) 초과 지속 → UP_LATE (한 번이라도 stale 이었으면 제외)
 *  VF 로그: 모든 항목에서 vf_safe(v,f) 가 아니면 VF_ORDER.
 */
static void check_policy(const trace_t *tr, const spec_t *sp, report_t *rep) {
    const uint32_t NONE = UINT32_MAX;
    uint32_t last_chg = NONE, pend_dn = NONE, pend_up = NONE;
    uint32_t rev[CHAT_MAX + 1];
    int nrev = 0, last_dir = 0;
    bool stale_seen = false;
    for (uint32_t i = 0; i < tr->n; i++) {
        const row_t *r = &tr->row[i];
        if (r->shut_before) continue;
        int d = r->lv_after - r->lv_before;
        bool stale = r->seq_age_ms >= sp->stale_ms;
        if (stale) stale_seen = true;
        if (!stale && r->read_mc >= sp->sd_mc && !r->shut_after) viol(rep, V_SD_MISSED, r->t_ms, r->lv_before, r->lv_after, r->read_mc, sp->sd_mc);
        if (r->shut_after) {
            if (r->read_mc < sp->sd_mc) viol(rep, V_SD_EARLY, r->t_ms, r->lv_before, r->lv_after, r->read_mc, sp->sd_mc);
            continue;
        }
        bool emg = !stale && r->read_mc >= sp->emg_mc;
        if (emg && r->lv_after != LV_MIN_PERF) viol(rep, V_EMG_LATE, r->t_ms, r->lv_before, r->lv_after, r->read_mc, sp->emg_mc);
        if (d > 0) {
            if (!stale && r->read_mc < sp->thr_mc) viol(rep, V_TRIP_EARLY, r->t_ms, r->lv_before, r->lv_after, r->read_mc, sp->thr_mc);
            if (!emg && !stale && d > 1) viol(rep, V_STEP_SKIP, r->t_ms, r->lv_before, r->lv_after, r->read_mc, 0);
            if (!emg && !stale && last_chg != NONE && r->t_ms - last_chg < sp->down_ms)
                viol(rep, V_DOWN_DWELL, r->t_ms, r->lv_before, r->lv_after, (int32_t)(r->t_ms - last_chg), (int32_t)sp->down_ms);
        } else if (d < 0) {
            if (r->read_mc >= sp->rel_mc) viol(rep, V_HYST_UP_ABOVE_REL, r->t_ms, r->lv_before, r->lv_after, r->read_mc, sp->rel_mc);
            if (d < -1) viol(rep, V_STEP_SKIP, r->t_ms, r->lv_before, r->lv_after, r->read_mc, 0);
            if (last_chg != NONE && r->t_ms - last_chg < sp->up_ms)
                viol(rep, V_UP_DWELL, r->t_ms, r->lv_before, r->lv_after, (int32_t)(r->t_ms - last_chg), (int32_t)sp->up_ms);
        }
        if (d != 0) {
            int dir = d > 0 ? 1 : -1;
            if (last_dir && dir != last_dir) {
                if (nrev == CHAT_MAX + 1) { memmove(rev, rev + 1, sizeof rev[0] * CHAT_MAX); nrev = CHAT_MAX; }
                rev[nrev++] = r->t_ms;
                if (nrev == CHAT_MAX + 1 && r->t_ms - rev[0] <= CHAT_WIN_MS)
                    viol(rep, V_CHATTER, r->t_ms, r->lv_before, r->lv_after, CHAT_MAX + 1, CHAT_MAX);
            }
            last_dir = dir;
            last_chg = r->t_ms;
        }
        if (!stale && r->read_mc >= sp->thr_mc && r->lv_after < LV_MIN_PERF) {
            if (d > 0 || pend_dn == NONE) pend_dn = r->t_ms;
            else if (r->t_ms - pend_dn > sp->down_ms + sp->poll_ms) {
                viol(rep, V_TRIP_LATE, r->t_ms, r->lv_before, r->lv_after, r->read_mc, sp->thr_mc);
                pend_dn = r->t_ms;
            }
        } else pend_dn = NONE;
        if (!stale_seen && r->read_mc < sp->rel_mc && r->lv_after > 0) {
            if (d < 0 || pend_up == NONE) pend_up = r->t_ms;
            else if (r->t_ms - pend_up > sp->up_ms + sp->poll_ms) {
                viol(rep, V_UP_LATE, r->t_ms, r->lv_before, r->lv_after, r->read_mc, sp->rel_mc);
                pend_up = r->t_ms;
            }
        } else pend_up = NONE;
    }
    for (uint32_t i = 0; i < tr->nvf; i++)
        if (!vf_safe(tr->vf[i].v_mv, tr->vf[i].f_mhz))
            viol(rep, V_VF_ORDER, tr->vf[i].t_ms, 0, 0, (int32_t)tr->vf[i].f_mhz, (int32_t)tr->vf[i].v_mv);
}

/* ======================= Part B: 위험도 정량화 ======================= */
typedef struct {
    bool throttled; uint32_t first_down_ms; int32_t true_at_down_mc, read_at_down_mc;
    int32_t peak_true_mc; uint32_t peak_t_ms; uint32_t over_ms;    /* over_ms: true > tj_max 인 시간 */
    bool shut; uint32_t shut_ms;
} metrics_t;

static void metrics_from_trace(const trace_t *tr, int32_t tj_max_mc, metrics_t *m) {
    memset(m, 0, sizeof *m);
    m->peak_true_mc = INT32_MIN;
    for (uint32_t i = 0; i < tr->n; i++) {
        const row_t *r = &tr->row[i];
        if (r->true_mc > m->peak_true_mc) { m->peak_true_mc = r->true_mc; m->peak_t_ms = r->t_ms; }
        if (!r->shut_before && r->true_mc > tj_max_mc) m->over_ms += POLL_MS;
        if (!m->throttled && r->lv_after > r->lv_before) {
            m->throttled = true; m->first_down_ms = r->t_ms;
            m->true_at_down_mc = r->true_mc; m->read_at_down_mc = r->read_mc;
        }
        if (!m->shut && r->shut_after) { m->shut = true; m->shut_ms = r->t_ms; }
    }
}
static bool guard_safe(run_cfg_t rc, trace_t *tr, int32_t tj_max_mc, int32_t g_mc) {
    int32_t thr = tj_max_mc - g_mc;
    for (int k = 0; k < 3; k++) rc.init.t[k] += (thr - rc.gov.thr_mc) / 1000.0;   /* 초기 상태도 thr 를 따라 이동 */
    rc.gov.thr_mc = thr; rc.gov.rel_mc = thr - 6000; rc.gov.emg_mc = thr + 8000; rc.gov.sd_mc = thr + 15000;
    metrics_t m;
    sim_run(&rc, tr);
    metrics_from_trace(tr, tj_max_mc, &m);
    return m.peak_true_mc <= tj_max_mc;
}
static int32_t guard_search(run_cfg_t rc, trace_t *tr, int32_t tj_max_mc) {
    int32_t lo = 0, hi = 40000;
    if (guard_safe(rc, tr, tj_max_mc, lo)) return 0;
    if (!guard_safe(rc, tr, tj_max_mc, hi)) return -1;
    while (hi - lo > TQ_MC) {
        int32_t mid = lo + ((hi - lo) / 2 / TQ_MC) * TQ_MC;
        if (mid == lo) mid += TQ_MC;
        if (guard_safe(rc, tr, tj_max_mc, mid)) hi = mid; else lo = mid;
    }
    return hi;
}

/* ======================= Part C: 안전 checker · 판정 · 리포트 ======================= */
typedef enum { VERDICT_PASS, VERDICT_FAIL_POLICY, VERDICT_FAIL_SAFETY } verdict_t;
static const char *const VERDICT_NAME[] = { "PASS", "FAIL_POLICY", "FAIL_SAFETY" };

/* 센서값을 믿지 않는 규칙:
 *  STALE_NOT_DETECTED: seq_age_ms >= stale_ms + poll_ms 인데 shut_after 도 아니고 lv_after != LV_MIN_PERF (첫 발생 시각 기록)
 *  TJ_EXCEEDED: shut_before 아닌 행에서 true_mc > tj_max_mc (기준 온도, 실칩이면 외부 reference) */
static void check_safety(const trace_t *tr, const spec_t *sp, report_t *rep) {
    for (uint32_t i = 0; i < tr->n; i++) {
        const row_t *r = &tr->row[i];
        if (r->seq_age_ms >= sp->stale_ms + sp->poll_ms && !r->shut_after && r->lv_after != LV_MIN_PERF)
            viol(rep, V_STALE_NOT_DETECTED, r->t_ms, r->lv_before, r->lv_after, (int32_t)r->seq_age_ms, (int32_t)(sp->stale_ms + sp->poll_ms));
        if (!r->shut_before && r->true_mc > sp->tj_max_mc)
            viol(rep, V_TJ_EXCEEDED, r->t_ms, r->lv_before, r->lv_after, r->true_mc, sp->tj_max_mc);
    }
}
/* TJ_EXCEEDED / SD_MISSED / STALE_NOT_DETECTED 중 하나라도 있으면 FAIL_SAFETY, 그 외 위반이 있으면 FAIL_POLICY, 없으면 PASS */
static verdict_t verdict_of(const report_t *r) {
    if (r->count[V_TJ_EXCEEDED] || r->count[V_SD_MISSED] || r->count[V_STALE_NOT_DETECTED]) return VERDICT_FAIL_SAFETY;
    return rep_total(r) ? VERDICT_FAIL_POLICY : VERDICT_PASS;
}
/* 한 줄 렌더링 (buf 에 snprintf):
 * "<name> <verdict> peakTj=<xx.x>C throttle@<x.xx>s n_viol=<n> first=<CODE> t=<x.xx>s L<from>->L<to> val=<xx.xx> lim=<xx.xx>"
 * 위반이 없으면 " first=-" 로 끝낸다. name 은 왼쪽 정렬 폭 16, verdict 는 폭 11. throttled 가 아니면 throttle@none */
static int render_row(char *buf, size_t cap, const char *name, verdict_t v, const metrics_t *m, const report_t *r) {
    char thr[24];
    if (m->throttled) snprintf(thr, sizeof thr, "%.2fs", m->first_down_ms / 1000.0);
    else snprintf(thr, sizeof thr, "none");
    int k = snprintf(buf, cap, "%-16s %-11s peakTj=%.1fC throttle@%s n_viol=%u", name, VERDICT_NAME[v],
                     m->peak_true_mc / 1000.0, thr, rep_total(r));
    if (k < 0) return k;
    int c = rep_first_code(r);
    size_t room = (size_t)k < cap ? cap - (size_t)k : 0;
    char *p = (size_t)k < cap ? buf + k : NULL;
    int k2;
    if (c < 0) k2 = snprintf(p, room, " first=-");
    else k2 = snprintf(p, room, " first=%s t=%.2fs L%d->L%d val=%.2f lim=%.2f", VNAME[c], r->first[c].t_ms / 1000.0,
                       r->first[c].from, r->first[c].to, r->first[c].value_mc / 1000.0, r->first[c].limit_mc / 1000.0);
    return k2 < 0 ? k2 : k + k2;
}

/* ======================= 테스트 ======================= */
static trace_t g_tr;

static const seg_t WL_SUSTAIN[] = { {120000, 1.0}, {180000, 0.05} };  /* 120 s 풀로드 → 60 s 유휴 */
static const seg_t WL_HOT[]     = { {20000, 1.3} };                   /* 달궈진 상태에서 power virus (a=1.3 > 일상 최대 1.0) */

static run_cfg_t cfg_sustain(void) {
    run_cfg_t c; memset(&c, 0, sizeof c);
    c.wl = WL_SUSTAIN; c.nseg = 2; c.dur_ms = 180000;
    c.init = (therm_t){ {45, 45, 45} }; c.t_amb = 45; c.gov = GOV_NOM;
    return c;
}
static run_cfg_t cfg_hot(void) {
    run_cfg_t c; memset(&c, 0, sizeof c);
    c.wl = WL_HOT; c.nseg = 1; c.dur_ms = 20000;
    c.init = (therm_t){ {84, 83, 80} }; c.t_amb = 55; c.gov = GOV_NOM;
    return c;
}
static run_cfg_t cfg_warm(void) {                                    /* 뜨거운 주변온도: L3/L4 사이 limit cycle */
    static const seg_t WL_WARM[] = { {120000, 1.0} };
    run_cfg_t c; memset(&c, 0, sizeof c);
    c.wl = WL_WARM; c.nseg = 1; c.dur_ms = 120000;
    c.init = (therm_t){ {60, 60, 60} }; c.t_amb = 65; c.gov = GOV_NOM;
    return c;
}
static run_cfg_t cfg_fan(void) {                                     /* 팬 고장: 5 s 후 r[2] 20배 (공기 흐름 거의 없음) */
    static const seg_t WL_FAN[] = { {400000, 1.0} };
    run_cfg_t c; memset(&c, 0, sizeof c);
    c.wl = WL_FAN; c.nseg = 1; c.dur_ms = 400000;
    c.init = (therm_t){ {88, 86, 84} }; c.t_amb = 55; c.gov = GOV_NOM;
    c.fan_fail_ms = 5000; c.fan_factor = 20.0;
    return c;
}

static bool has_only(const report_t *r, vcode_t c) { return r->count[c] > 0 && rep_total(r) == r->count[c]; }

/* --- 손으로 만든 trace 로 checker 자체를 시험 --- */
static trace_t g_hand;
static void hand_reset(void) { g_hand.n = 0; g_hand.nvf = 0; }
static void hand(uint32_t t, int32_t true_c10, int32_t read_c10, int b, int a, bool sb, bool sa, uint32_t age) {
    g_hand.row[g_hand.n++] = (row_t){ t, age, true_c10 * 100, read_c10 * 100, (int8_t)b, (int8_t)a, sb, sa };
}
typedef struct { const char *name; void (*build)(void); vcode_t want; uint32_t want_t; } hand_case_t;

static void h_trip_early(void) { hand(0, 880, 880, 0, 1, false, false, 0); }
static void h_trip_late(void) {
    for (uint32_t t = 0; t <= 200; t += 10) hand(t, 910, 910, 0, 0, false, false, 0);
}
static void h_hyst(void) { hand(0, 860, 860, 0, 0, false, false, 0); hand(2000, 860, 860, 1, 0, false, false, 0); }
static void h_up_dwell(void) { hand(0, 830, 800, 2, 1, false, false, 0); hand(500, 830, 800, 1, 0, false, false, 0); }
static void h_down_dwell(void) { hand(0, 910, 910, 0, 1, false, false, 0); hand(50, 910, 910, 1, 2, false, false, 0); }
static void h_up_late(void) {
    for (uint32_t t = 0; t <= 1100; t += 10) hand(t, 800, 800, 2, 2, false, false, 0);
}
static void h_skip(void) { hand(0, 910, 910, 2, 4, false, false, 0); }
static void h_vf(void) {
    g_hand.vf[g_hand.nvf++] = (vf_ev_t){ 123, 700, 1100 };
    hand(0, 500, 500, 0, 0, false, false, 0);
}
static void h_emg(void) { hand(0, 985, 985, 3, 4, false, false, 0); }
static void h_sd_missed(void) { hand(0, 1055, 1055, 5, 5, false, false, 0); }
static void h_sd_early(void) { hand(0, 1000, 1000, 5, 5, false, true, 0); }
static void h_chatter(void) {
    uint32_t t = 0;
    for (int i = 0; i < 10; i++) { hand(t, 910, 910, 1, 2, false, false, 0); t += 1000; hand(t, 800, 800, 2, 1, false, false, 0); t += 1000; }
}
static void h_tj(void) { hand(0, 1062, 900, 0, 0, false, false, 0); }
static void h_stale(void) { hand(100, 700, 700, 0, 0, false, false, 100); hand(110, 700, 700, 0, 0, false, false, 110); }

static void test_part_a(void) {
    printf("-- Part A: plant + policy checker --\n");
    const therm_par_t *P = &PLANT_NOM;
    CHECK(fabs(therm_dt_limit(P) - 0.1) < 1e-12, "dt limit (Gershgorin) = 0.1 s");
    CHECK(DT_S * 50 <= therm_dt_limit(P), "DT_S=1 ms is at least 50x inside the limit");

    /* 정상상태: Ta + P*ΣR, 허용오차 0.01 ℃ (2000 s ≫ 최장 시정수 ~90 s, 지수 잔차 < 1e-9) */
    therm_t s = { {45, 45, 45} };
    for (int i = 0; i < 2000000; i++) therm_step(P, &s, 20.0, DT_S);
    CHECK(fabs(s.t[0] - 95.0) < 0.01 && fabs(s.t[1] - 85.0) < 0.01 && fabs(s.t[2] - 75.0) < 0.01,
          "steady state 20 W: die 95, pkg 85, sink 75 (got %.3f %.3f %.3f)", s.t[0], s.t[1], s.t[2]);

    /* 에너지 보존: forward Euler 는 내부 열류가 망원합으로 사라져 이산 수준에서 정확히 보존 (허용 1e-6 J = 부동소수 누적오차) */
    therm_t e = { {60, 55, 50} };
    double e0 = P->c[0]*e.t[0] + P->c[1]*e.t[1] + P->c[2]*e.t[2], in = 0, out = 0;
    for (int i = 0; i < 100000; i++) {
        double p = 10.0 + 25.0 * ((i / 1000) % 2);
        in += p * DT_S; out += (e.t[2] - P->t_amb) / P->r[2] * DT_S;
        therm_step(P, &e, p, DT_S);
    }
    double e1 = P->c[0]*e.t[0] + P->c[1]*e.t[1] + P->c[2]*e.t[2];
    CHECK(fabs((e1 - e0) - (in - out)) < 1e-6, "energy balance over 100 s (residual %.2e J)", (e1 - e0) - (in - out));

    /* 시간격자 수렴: dt 1 ms vs 0.25 ms, die 차이 < 0.05 ℃ (Euler 전역오차 O(dt)) */
    therm_t a = { {45, 45, 45} }, b = a;
    for (int i = 0; i < 5000; i++) therm_step(P, &a, 35.0, 0.001);
    for (int i = 0; i < 20000; i++) therm_step(P, &b, 35.0, 0.00025);
    CHECK(fabs(a.t[0] - b.t[0]) < 0.05 && fabs(a.t[0] - 45.0) > 1.0, "dt convergence at 5 s (1 ms vs 0.25 ms, diff %.4f C)", fabs(a.t[0] - b.t[0]));

    /* 정상 governor 3 시나리오: 깨끗해야 한다 */
    report_t r; metrics_t m;
    run_cfg_t rc = cfg_sustain();
    sim_run(&rc, &g_tr);
    rep_init(&r); check_policy(&g_tr, &SPEC_NOM, &r);
    metrics_from_trace(&g_tr, SPEC_NOM.tj_max_mc, &m);
    CHECK(rep_total(&r) == 0, "sustain: good governor has 0 policy violations (got %u)", rep_total(&r));
    /* 느린 램프에서의 trip 정확도: 허용 ±0.5 ℃ = 양자화 2 LSB (기울기×폴링 지연 ≪ 0.01 ℃) */
    CHECK(m.throttled && m.read_at_down_mc >= 90000 && m.read_at_down_mc <= 90250, "sustain: first step at reading 90 C (read %.2f)", m.read_at_down_mc / 1000.0);
    CHECK(m.throttled && labs(m.true_at_down_mc - 90000) <= 500, "sustain: true temp at first step within 0.5 C of 90 (true %.3f)", m.true_at_down_mc / 1000.0);
    CHECK(m.peak_true_mc < SPEC_NOM.emg_mc, "sustain: peak below emergency (peak %.2f)", m.peak_true_mc / 1000.0);
    /* DVFS 전이: 내려갈 때 인접 단계, 끝에는 다시 L0 */
    int maxl = 0; for (uint32_t i = 0; i < g_tr.n; i++) if (g_tr.row[i].lv_after > maxl) maxl = g_tr.row[i].lv_after;
    CHECK(maxl >= 1 && g_tr.row[g_tr.n - 1].lv_after == 0, "sustain: throttled to L%d, released back to L0 after workload", maxl);

    rc = cfg_hot();
    sim_run(&rc, &g_tr);
    rep_init(&r); check_policy(&g_tr, &SPEC_NOM, &r);
    CHECK(rep_total(&r) == 0, "hot step: good governor has 0 policy violations (got %u)", rep_total(&r));

    rc = cfg_warm();
    sim_run(&rc, &g_tr);
    rep_init(&r); check_policy(&g_tr, &SPEC_NOM, &r);
    maxl = 0; for (uint32_t i = 0; i < g_tr.n; i++) if (g_tr.row[i].lv_after > maxl) maxl = g_tr.row[i].lv_after;
    CHECK(rep_total(&r) == 0 && maxl >= 3, "warm ambient: limit cycle visits L%d with 0 policy violations (got %u)", maxl, rep_total(&r));

    rc = cfg_fan();
    sim_run(&rc, &g_tr);
    rep_init(&r); check_policy(&g_tr, &SPEC_NOM, &r);
    metrics_from_trace(&g_tr, SPEC_NOM.tj_max_mc, &m);
    CHECK(rep_total(&r) == 0 && m.shut, "fan failure: 0 violations and shutdown fires (viol %u)", rep_total(&r));
    CHECK(labs(m.peak_true_mc - 105000) <= 500, "fan failure: shutdown stops the climb at 105 C (peak %.2f, tolerance 0.5 = 2 LSB)", m.peak_true_mc / 1000.0);

    /* checker 단위 테스트: 위반 하나짜리 합성 trace → 정확히 그 코드 + 그 시각 */
    static const hand_case_t HC[] = {
        {"TRIP_EARLY", h_trip_early, V_TRIP_EARLY, 0},
        {"TRIP_LATE", h_trip_late, V_TRIP_LATE, 120},
        {"HYST_UP_ABOVE_REL", h_hyst, V_HYST_UP_ABOVE_REL, 2000},
        {"UP_DWELL", h_up_dwell, V_UP_DWELL, 500},
        {"DOWN_DWELL", h_down_dwell, V_DOWN_DWELL, 50},
        {"UP_LATE", h_up_late, V_UP_LATE, 1020},
        {"STEP_SKIP", h_skip, V_STEP_SKIP, 0},
        {"VF_ORDER", h_vf, V_VF_ORDER, 123},
        {"EMG_LATE", h_emg, V_EMG_LATE, 0},
        {"SD_MISSED", h_sd_missed, V_SD_MISSED, 0},
        {"SD_EARLY", h_sd_early, V_SD_EARLY, 0},
    };
    for (size_t i = 0; i < sizeof HC / sizeof HC[0]; i++) {
        hand_reset(); HC[i].build();
        rep_init(&r); check_policy(&g_hand, &SPEC_NOM, &r);
        CHECK(has_only(&r, HC[i].want) && r.first[HC[i].want].t_ms == HC[i].want_t,
              "checker unit: %s -> only that code at t=%u ms (total %u)", HC[i].name, HC[i].want_t, rep_total(&r));
    }
    hand_reset(); h_chatter();
    rep_init(&r); check_policy(&g_hand, &SPEC_NOM, &r);
    CHECK(r.count[V_CHATTER] > 0, "checker unit: CHATTER fires on 1 Hz toggling (count %u)", r.count[V_CHATTER]);
}

static void test_part_b(void) {
    printf("-- Part B: sensor errors --\n");
    report_t r; metrics_t m, m0;
    /* B1: 느린 램프에서 trip 시점 true 온도로 주입 오프셋 복원. 허용 ±0.5 ℃ (양자화 2 LSB) */
    static const int32_t OFS[] = { 0, -2000, -5000, -8000, 3000 };
    run_cfg_t rc = cfg_sustain();
    sim_run(&rc, &g_tr); metrics_from_trace(&g_tr, 105000, &m0);
    uint32_t prev = m0.first_down_ms;
    for (size_t i = 0; i < sizeof OFS / sizeof OFS[0]; i++) {
        rc = cfg_sustain(); rc.sens.offset_mc = OFS[i];
        sim_run(&rc, &g_tr); metrics_from_trace(&g_tr, 105000, &m);
        int32_t rec = 90000 - m.true_at_down_mc;      /* 관측: thr - true@trip = 복원 오프셋 */
        CHECK(m.throttled && labs(rec - OFS[i]) <= 500, "offset recovered from trip: inj %+.1f C, got %+.2f C", OFS[i] / 1000.0, rec / 1000.0);
        if (i >= 1 && i <= 3) { CHECK(m.first_down_ms > prev, "reads-low %+.0f C throttles later (%.2f s > %.2f s)", OFS[i] / 1000.0, m.first_down_ms / 1000.0, prev / 1000.0); prev = m.first_down_ms; }
        if (i == 4) CHECK(m.first_down_ms < m0.first_down_ms, "reads-high +3 C throttles earlier (%.2f s < %.2f s)", m.first_down_ms / 1000.0, m0.first_down_ms / 1000.0);
    }
    /* B2 한계: 일정 오프셋은 센서값만 보는 정책 checker 에 보이지 않는다 */
    rc = cfg_hot(); rc.sens.offset_mc = -16000;
    sim_run(&rc, &g_tr);
    rep_init(&r); check_policy(&g_tr, &SPEC_NOM, &r);
    CHECK(rep_total(&r) == 0, "LIMIT: reads-low 16 C is invisible to the reading-only policy checker");
    report_t r2; rep_init(&r2); check_safety(&g_tr, &SPEC_NOM, &r2);
    CHECK(r2.count[V_TJ_EXCEEDED] > 0, "with reference temperature the same run shows TJ_EXCEEDED (first at %.2f s)", r2.first[V_TJ_EXCEEDED].t_ms / 1000.0);

    /* B3: 센서 lag → hot step 에서 trip 지연과 overshoot 증가 (lag 0/20/100/300 ms 에서 단조) */
    static const uint32_t LAG[] = { 0, 20, 100, 300 };
    int32_t last_peak = INT32_MIN; uint32_t last_dn = 0; bool mono = true;
    for (size_t i = 0; i < 4; i++) {
        rc = cfg_hot(); rc.sens.lag_ms = LAG[i];
        sim_run(&rc, &g_tr); metrics_from_trace(&g_tr, 105000, &m);
        if (i && (m.peak_true_mc < last_peak || m.first_down_ms < last_dn)) mono = false;
        last_peak = m.peak_true_mc; last_dn = m.first_down_ms;
    }
    CHECK(mono, "lag increases hot-step peak and trip delay monotonically (peak at 300 ms: %.2f C)", last_peak / 1000.0);

    /* B4: stuck 두 종류 */
    rc = cfg_sustain(); rc.sens.stuck = STUCK_FROZEN; rc.sens.stuck_at_ms = 5000;
    sim_run(&rc, &g_tr);
    rep_init(&r); check_policy(&g_tr, &SPEC_NOM, &r); check_safety(&g_tr, &SPEC_NOM, &r);
    metrics_from_trace(&g_tr, 105000, &m);
    uint32_t fs_t = UINT32_MAX;
    for (uint32_t i = 0; i < g_tr.n; i++) if (g_tr.row[i].lv_after == LV_MIN_PERF) { fs_t = g_tr.row[i].t_ms; break; }
    CHECK(rep_total(&r) == 0 && fs_t <= 5000 + 100 + 2 * POLL_MS, "frozen sensor at 5.000 s: failsafe to L5 at %.2f s, clean verdict", fs_t / 1000.0);

    rc = cfg_sustain(); rc.sens.stuck = STUCK_VALUE; rc.sens.stuck_at_ms = 5000;
    sim_run(&rc, &g_tr);
    rep_init(&r); check_policy(&g_tr, &SPEC_NOM, &r);
    metrics_from_trace(&g_tr, 105000, &m);
    CHECK(rep_total(&r) == 0 && !m.throttled, "LIMIT: value-stuck with live SEQ is invisible to the governor and to the policy checker");
    rep_init(&r2); check_safety(&g_tr, &SPEC_NOM, &r2);
    CHECK(r2.count[V_TJ_EXCEEDED] > 0 && m.peak_true_mc > 110000, "...reference temperature shows it: peak %.1f C, %.1f s above Tjmax", m.peak_true_mc / 1000.0, m.over_ms / 1000.0);

    /* B5: guard band 는 bisect 와 선형 탐색이 같아야 하고, 오프셋과 lag 가 키운다. 선형 탐색 격자 0.25 ℃ 이므로 일치는 정확히 */
    static const struct { int32_t ofs; uint32_t lag; } GB[] = { {0, 20}, {-5000, 20}, {-10000, 100} };
    int32_t prev_g = -1;
    for (size_t i = 0; i < 3; i++) {
        rc = cfg_hot(); rc.sens.offset_mc = GB[i].ofs; rc.sens.lag_ms = GB[i].lag; rc.dur_ms = 5000;
        int32_t g = guard_search(rc, &g_tr, 105000);
        int32_t scan = -1;
        for (int32_t x = 0; x <= 40000; x += TQ_MC) { if (guard_safe(rc, &g_tr, 105000, x)) { scan = x; break; } }
        CHECK(g == scan && g > prev_g, "guard band ofs %+.0f C lag %u ms: bisect %.2f C == linear %.2f C", GB[i].ofs / 1000.0, GB[i].lag, g / 1000.0, scan / 1000.0);
        prev_g = g;
    }
    /* 느린 램프에서는 오프셋이 guard band 로 1:1 전달된다 (허용 ±0.5 ℃ = 양자화 2 LSB). hot step 은 1:1 이 아니다 — 풀이 참고 */
    rc = cfg_sustain(); rc.dur_ms = 60000;
    int32_t g0 = guard_search(rc, &g_tr, 105000);
    rc.sens.offset_mc = -7000;
    int32_t g7 = guard_search(rc, &g_tr, 105000);
    CHECK(g0 >= 0 && labs((g7 - g0) - 7000) <= 500, "slow ramp: reads-low 7 C costs 7 C of guard band (%.2f -> %.2f)", g0 / 1000.0, g7 / 1000.0);
}

typedef struct { const char *name; run_cfg_t rc; verdict_t want; int want_code; int want_from, want_to; } scen_t;

static void test_part_c(void) {
    printf("-- Part C: planted bugs, Orion-S1 / EVB-07 campaign --\n");
    report_t ru;
    hand_reset(); h_tj();
    rep_init(&ru); check_safety(&g_hand, &SPEC_NOM, &ru);
    CHECK(has_only(&ru, V_TJ_EXCEEDED) && ru.first[V_TJ_EXCEEDED].t_ms == 0, "checker unit: TJ_EXCEEDED on reference temp alone (reading was 90)");
    hand_reset(); h_stale();
    rep_init(&ru); check_safety(&g_hand, &SPEC_NOM, &ru);
    CHECK(has_only(&ru, V_STALE_NOT_DETECTED) && ru.first[V_STALE_NOT_DETECTED].t_ms == 110, "checker unit: STALE_NOT_DETECTED fires at age >= stale + poll");
    scen_t sc[10]; int n = 0;
    sc[n++] = (scen_t){ "S0_good", cfg_sustain(), VERDICT_PASS, -1, 0, 0 };
    sc[n] = (scen_t){ "S1_hyst_sign", cfg_warm(), VERDICT_FAIL_POLICY, V_HYST_UP_ABOVE_REL, -1, -1 }; sc[n].rc.bugs = BUG_HYST_SIGN; n++;
    sc[n] = (scen_t){ "S2_step_skip", cfg_warm(), VERDICT_FAIL_POLICY, V_STEP_SKIP, 2, 4 }; sc[n].rc.bugs = BUG_SKIP_STEP; n++;
    sc[n] = (scen_t){ "S3_step_drop", cfg_warm(), VERDICT_FAIL_POLICY, V_TRIP_LATE, 2, 2 }; sc[n].rc.bugs = BUG_DROP_STEP; n++;
    sc[n] = (scen_t){ "S4_vf_order", cfg_sustain(), VERDICT_FAIL_POLICY, V_VF_ORDER, 0, 0 }; sc[n].rc.bugs = BUG_VF_ORDER; n++;
    sc[n] = (scen_t){ "S5_stale_undet", cfg_sustain(), VERDICT_FAIL_SAFETY, V_STALE_NOT_DETECTED, 0, 0 };
    sc[n].rc.bugs = BUG_NO_STALE; sc[n].rc.sens.stuck = STUCK_FROZEN; sc[n].rc.sens.stuck_at_ms = 5000; n++;
    sc[n] = (scen_t){ "S6_stale_ok", cfg_sustain(), VERDICT_PASS, -1, 0, 0 };
    sc[n].rc.sens.stuck = STUCK_FROZEN; sc[n].rc.sens.stuck_at_ms = 5000; n++;
    sc[n] = (scen_t){ "S7_no_shutdown", cfg_fan(), VERDICT_FAIL_SAFETY, V_SD_MISSED, 5, 5 }; sc[n].rc.bugs = BUG_NO_SD; n++;
    sc[n] = (scen_t){ "S8_offset_-16", cfg_hot(), VERDICT_FAIL_SAFETY, V_TJ_EXCEEDED, 0, 0 }; sc[n].rc.sens.offset_mc = -16000; n++;
    sc[n] = (scen_t){ "S9_thr_trim", cfg_sustain(), VERDICT_FAIL_POLICY, V_TRIP_LATE, 0, 0 }; sc[n].rc.gov.thr_mc = 93000; n++;

    static char lines[10][200];
    for (int i = 0; i < n; i++) {
        report_t r; metrics_t m;
        sim_run(&sc[i].rc, &g_tr);
        rep_init(&r); check_policy(&g_tr, &SPEC_NOM, &r); check_safety(&g_tr, &SPEC_NOM, &r);
        metrics_from_trace(&g_tr, SPEC_NOM.tj_max_mc, &m);
        verdict_t v = verdict_of(&r);
        render_row(lines[i], sizeof lines[i], sc[i].name, v, &m, &r);
        CHECK(v == sc[i].want, "%s verdict = %s", sc[i].name, VERDICT_NAME[sc[i].want]);
        if (i == 1) CHECK(r.count[V_CHATTER] > 0, "S1 also shows CHATTER (%u windows)", r.count[V_CHATTER]);
        if (sc[i].want_code >= 0) {
            bool loc = r.count[sc[i].want_code] > 0;
            if (loc && sc[i].want_from >= 0 && sc[i].want_code != V_VF_ORDER && sc[i].want_code != V_TJ_EXCEEDED && sc[i].want_code != V_STALE_NOT_DETECTED)
                loc = r.first[sc[i].want_code].from == sc[i].want_from && r.first[sc[i].want_code].to == sc[i].want_to;
            CHECK(loc, "%s signature %s", sc[i].name, VNAME[sc[i].want_code]);
        } else {
            CHECK(rep_total(&r) == 0, "%s has 0 violations", sc[i].name);
        }
    }
    printf("=== Thermal governor verdict: SoC Orion-S1 (VDD_NPU) board EVB-07 fw 2.3.1 ===\n");
    printf("spec thr/rel/emg/sd/Tjmax = 90.0/84.0/98.0/105.0/105.0 C\n");
    for (int i = 0; i < n; i++) printf("%s\n", lines[i]);
    CHECK(strstr(lines[1], "first=HYST_UP_ABOVE_REL") != NULL, "report: S1 row names the first violation");
    CHECK(strstr(lines[0], " first=-") != NULL && strstr(lines[0], "PASS") != NULL, "report: S0 row shows PASS, first=-");
    CHECK(strstr(lines[2], "L2->L4") != NULL, "report: S2 row carries the skipped transition L2->L4");

    /* 코버리지 한계: 없는 shutdown 경로는 온도가 거기까지 가지 않으면 보이지 않는다 */
    report_t r; metrics_t m;
    run_cfg_t rc = cfg_sustain(); rc.bugs = BUG_NO_SD;
    sim_run(&rc, &g_tr);
    rep_init(&r); check_policy(&g_tr, &SPEC_NOM, &r); check_safety(&g_tr, &SPEC_NOM, &r);
    metrics_from_trace(&g_tr, SPEC_NOM.tj_max_mc, &m);
    CHECK(rep_total(&r) == 0 && m.peak_true_mc < SPEC_NOM.sd_mc, "LIMIT: BUG_NO_SD is invisible while the workload never reaches shutdown temp (peak %.1f C)", m.peak_true_mc / 1000.0);
}

int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
