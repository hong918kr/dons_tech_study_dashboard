/* SIV-M07 Shmoo Plot 생성과 마진 분석
 *
 * 실리콘 특성화(characterization)의 기본 도구: 전압(V) × 주파수(F) 격자의 각 점에서 같은 테스트를 N번 돌려
 * PASS / MIXED / FAIL 지도(shmoo plot)를 그리고, 주파수별 최소 동작 전압(Vmin)과 OPP(DVFS 동작점) 마진을 수치화한다.
 *
 * DUT 는 진짜 칩 대신 "임계 경로 지연 = f(V, T, 공정)" 모델이다 (alpha-power law, alpha = 1.5, 전부 정수 연산).
 *   delay_ps = K * V / (V - Vth)^1.5 * mob(T)
 *   Vth(T)   = Vth0 - 0.8 mV/°C * (T - 25)        (뜨거우면 Vth ↓ → 빨라짐)
 *   mob(T)   = 1 + 3500 ppm/°C * (T - 25)          (뜨거우면 이동도 ↓ → 느려짐)  → 저전압에선 "온도 역전"
 *   PASS  ⇔  delay * (1 + noise) + setup ≤ 1/F
 * noise 는 측정마다 ±noise_ppm 균등분포. counter-based 난수 = hash(seed, V, F, T, trial) 이라서
 * 스윕 순서와 무관하게 같은 (V,F,T,trial) 은 언제나 같은 결과 → 체크가 결정적이다.
 * 결함 주입(fault_t): 홀(hole: 특정 F 의 V 창에서 항상 FAIL), 산발 실패(sporadic: 마진과 무관한 확률적 FAIL),
 * 약한 다이(extra Vth).
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 주어진 부분: DUT 모델 ======================= */
static uint64_t mix64(uint64_t x) {                  /* splitmix64 finalizer: counter → 난수 */
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
static uint64_t isqrt64(uint64_t x) {                /* floor(sqrt(x)), 비트 단위 */
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

typedef enum { CORNER_SS, CORNER_TT, CORNER_FF } corner_t;
typedef struct {
    int      hole_f, hole_v_lo, hole_v_hi;            /* hole_f == 0 → 없음. 그 F 에서 V∈[lo,hi] 는 항상 FAIL      */
    uint32_t spor_ppm; int spor_t_min;                /* T ≥ spor_t_min 이고 아래 창 안이면 spor_ppm 확률로 FAIL     */
    int      spor_v_lo, spor_v_hi, spor_f_lo, spor_f_hi;
    int      extra_vth_mv;                            /* 약한 다이: Vth 가 공정 코너보다 더 높음                   */
} fault_t;
typedef struct {
    const char *name; corner_t corner; uint64_t seed; uint32_t noise_ppm; fault_t fault;
    uint32_t runs;                                    /* measure_once 호출 수 = 테스트 실행 횟수 (시간 비용 지표)  */
} chip_t;

#define K_TT_PS       12000
#define VTH_TT_MV     350
#define VTH_TC_UV     800       /* µV/°C */
#define MOB_TC_PPM    3500      /* ppm/°C */
#define SETUP_PS      30

static int vth_mv(const chip_t *c, int t_c) {
    int v0 = VTH_TT_MV + (c->corner == CORNER_SS ? 40 : c->corner == CORNER_FF ? -40 : 0) + c->fault.extra_vth_mv;
    return v0 - (t_c - 25) * VTH_TC_UV / 1000;
}
static uint64_t delay_ps(const chip_t *c, int v_mv, int t_c) {
    int vov = v_mv - vth_mv(c, t_c);
    if (vov <= 50) return 1000000000ull;                                /* 사실상 동작 안 함 */
    uint64_t k = c->corner == CORNER_SS ? K_TT_PS * 110 / 100 : c->corner == CORNER_FF ? K_TT_PS * 90 / 100 : K_TT_PS;
    uint64_t mob = (uint64_t)(1000000 + MOB_TC_PPM * (t_c - 25));       /* ppm 스케일 */
    uint64_t vov15 = (uint64_t)vov * isqrt64((uint64_t)vov * 1000000u); /* 1000 * vov^1.5 */
    return k * (uint64_t)v_mv * mob / vov15 / 1000u;
}
/* 노이즈 e_ppm 을 고정했을 때의 판정 (진실 계산용) */
static bool model_pass(const chip_t *c, int v_mv, int f_mhz, int t_c, int32_t e_ppm) {
    uint64_t d = delay_ps(c, v_mv, t_c);
    uint64_t lhs = (d * (uint64_t)(1000000 + e_ppm) + (uint64_t)SETUP_PS * 1000000u) * (uint64_t)f_mhz;
    return lhs <= 1000000000000ull;                                     /* (d + setup) ≤ 1e6/F ps */
}
/* 실제 "측정" 1회: 테스트 패턴 1번 실행에 해당. trial = 반복 번호 */
static bool measure_once(chip_t *c, int v_mv, int f_mhz, int t_c, uint32_t trial) {
    c->runs++;
    const fault_t *ft = &c->fault;
    if (ft->hole_f == f_mhz && v_mv >= ft->hole_v_lo && v_mv <= ft->hole_v_hi) return false;
    uint64_t key = ((uint64_t)(uint32_t)v_mv << 53) ^ ((uint64_t)(uint32_t)f_mhz << 41)
                 ^ ((uint64_t)(uint32_t)(t_c + 273) << 32) ^ trial;
    uint64_t r = mix64(c->seed ^ mix64(key));
    if (ft->spor_ppm && t_c >= ft->spor_t_min && v_mv >= ft->spor_v_lo && v_mv <= ft->spor_v_hi &&
        f_mhz >= ft->spor_f_lo && f_mhz <= ft->spor_f_hi && (uint32_t)((r >> 32) % 1000000u) < ft->spor_ppm)
        return false;
    int32_t e = 0;
    if (c->noise_ppm) e = (int32_t)((uint32_t)r % (2u * c->noise_ppm + 1u)) - (int32_t)c->noise_ppm;
    return model_pass(c, v_mv, f_mhz, t_c, e);
}
/* 진실(reference): 노이즈를 e_ppm 으로 고정하고 1 mV 씩 위에서 내려오며 "여기부터 위로 전부 PASS" 인 최저 V.
 * hi 에서도 FAIL 이면 -1. (주어진 함수 — 테스트의 정답지) */
static int vmin_truth(const chip_t *c, int t_c, int f_mhz, int32_t e_ppm, int lo, int hi) {
    if (!model_pass(c, hi, f_mhz, t_c, e_ppm)) return -1;
    int v = hi;
    while (v > lo && model_pass(c, v - 1, f_mhz, t_c, e_ppm)) v--;
    return v;
}

/* ======================= 주어진 부분: 격자 · 문자열 버퍼 ======================= */
#define V_LO   650
#define V_HI   1000
#define V_STEP 10
#define NV     ((V_HI - V_LO) / V_STEP + 1)     /* 36 행 */
#define F_LO   400
#define F_HI   1200
#define F_STEP 50
#define NF     ((F_HI - F_LO) / F_STEP + 1)     /* 17 열 */
#define V_AT(i) (V_LO + (i) * V_STEP)
#define F_AT(j) (F_LO + (j) * F_STEP)

typedef enum { CELL_FAIL, CELL_MIXED, CELL_PASS } cell_t;
typedef struct { const char *chip; int t_c; uint32_t trials; uint16_t npass[NV][NF]; } shmoo_t;

typedef struct { char *p; size_t cap; size_t len; } sbuf_t;   /* len 은 잘려도 "필요했던 길이" 를 센다 */
static void sb_init(sbuf_t *b, char *p, size_t cap) { b->p = p; b->cap = cap; b->len = 0; if (cap) p[0] = '\0'; }
static void sb_printf(sbuf_t *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sb_printf(sbuf_t *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    size_t room = b->len < b->cap ? b->cap - b->len : 0;
    int k = vsnprintf(room ? b->p + b->len : NULL, room, fmt, ap);
    va_end(ap);
    if (k > 0) b->len += (size_t)k;
}

/* ======================= Part A: 2차원 sweep · 렌더링 · 열별 Vmin ======================= */
/* npass/trials → 셀 분류. trials == 0 이면 증거가 없으므로 FAIL */
static cell_t cell_class(uint32_t npass, uint32_t trials) {
    if (trials == 0 || npass == 0) return CELL_FAIL;
    return npass == trials ? CELL_PASS : CELL_MIXED;
}
/* 모든 (V,F) 셀에서 trial 0..trials-1 을 전부 실행해 PASS 횟수를 센다 (MIXED 를 보려면 조기 종료 금지) */
static void shmoo_sweep(chip_t *c, int t_c, uint32_t trials, shmoo_t *s) {
    memset(s, 0, sizeof *s);
    s->chip = c->name; s->t_c = t_c; s->trials = trials;
    for (int j = 0; j < NF; j++)                         /* 실보드 습관: F 고정, V 는 높은 쪽(안전)부터 내려온다 */
        for (int i = NV - 1; i >= 0; i--) {
            uint32_t k = 0;
            for (uint32_t t = 0; t < trials; t++) k += measure_once(c, V_AT(i), F_AT(j), t_c, t);
            s->npass[i][j] = (uint16_t)k;
        }
}
/* ASCII shmoo: 위가 높은 V. 셀 4칸 폭: '*' PASS, '#' MIXED, '.' FAIL. 반환 = 필요한 길이(snprintf 처럼) */
static int shmoo_render_ascii(char *buf, size_t n, const shmoo_t *s) {
    sbuf_t b; sb_init(&b, buf, n);
    sb_printf(&b, "SHMOO chip=%s T=%dC trials=%u  (* pass, # mixed, . fail)\n", s->chip, s->t_c, (unsigned)s->trials);
    sb_printf(&b, "   mV |");
    for (int j = 0; j < NF; j++) sb_printf(&b, "%4d", F_AT(j) / 10);
    sb_printf(&b, "   F[x10MHz]\n");
    for (int i = NV - 1; i >= 0; i--) {
        sb_printf(&b, "%5d |", V_AT(i));
        for (int j = 0; j < NF; j++) {
            cell_t cl = cell_class(s->npass[i][j], s->trials);
            sb_printf(&b, "   %c", cl == CELL_PASS ? '*' : cl == CELL_MIXED ? '#' : '.');
        }
        sb_printf(&b, "\n");
    }
    return (int)b.len;
}
/* CSV: 헤더 "mV,400,...,1200", 행은 높은 V 부터, 값 = PASS 횟수 (도구에서 pass 비율 heatmap 으로 쓰기 좋게) */
static int shmoo_render_csv(char *buf, size_t n, const shmoo_t *s) {
    sbuf_t b; sb_init(&b, buf, n);
    sb_printf(&b, "mV");
    for (int j = 0; j < NF; j++) sb_printf(&b, ",%d", F_AT(j));
    sb_printf(&b, "\n");
    for (int i = NV - 1; i >= 0; i--) {
        sb_printf(&b, "%d", V_AT(i));
        for (int j = 0; j < NF; j++) sb_printf(&b, ",%u", (unsigned)s->npass[i][j]);
        sb_printf(&b, "\n");
    }
    return (int)b.len;
}
/* 열 j 의 strict Vmin: "그 V 부터 V_HI 까지 모든 셀이 PASS" 인 최저 격자 V. 맨 위가 PASS 가 아니면 -1 */
static int shmoo_vmin_col(const shmoo_t *s, int j) {
    int v = -1;
    for (int i = NV - 1; i >= 0; i--) {
        if (cell_class(s->npass[i][j], s->trials) != CELL_PASS) break;
        v = V_AT(i);
    }
    return v;
}

/* ======================= Part B: Vmin 이진 탐색 · 반복 측정 · 확인 ======================= */
typedef enum { VMIN_OK, VMIN_BELOW_RANGE, VMIN_NO_PASS, VMIN_UNSTABLE, VMIN_BAD_ARG } vmin_status_t;
typedef struct {
    vmin_status_t status;
    int vmin;                 /* OK/BELOW_RANGE: 찾은 V (BELOW_RANGE 면 lo). 그 외 -1 */
    uint32_t probes;          /* 탐색에서 V 를 바꿔 본 횟수 */
    uint32_t runs;            /* 이 호출이 쓴 measure_once 횟수 = 테스트 시간 */
    uint32_t confirm_steps;   /* robust: 확인 실패로 res 만큼 올린 횟수 */
} vmin_result_t;

/* 주어진 헬퍼: V 에서 trial base..base+trials-1 이 전부 PASS 인가. 첫 FAIL 에서 중단(시간 절약) */
static bool probe_all(chip_t *c, int t_c, int f, int v, uint32_t trials, uint32_t base) {
    for (uint32_t k = 0; k < trials; k++)
        if (!measure_once(c, v, f, t_c, base + k)) return false;
    return true;
}
#define CONFIRM_BASE 1000000u   /* 확인 측정은 탐색과 겹치지 않는 trial 번호를 쓴다 */

/* [lo,hi] 에서 Vmin 이진 탐색. 각 probe = trials 회 전부 PASS (trial 0..trials-1).
 * 불변식: lo 는 FAIL 로 확인됨, hi 는 PASS 로 확인됨 → hi-lo ≤ res 가 되면 vmin = hi. */
static vmin_status_t vmin_bisect(chip_t *c, int t_c, int f, int lo, int hi, int res, uint32_t trials,
                                 vmin_result_t *r) {
    *r = (vmin_result_t){ .status = VMIN_BAD_ARG, .vmin = -1 };
    if (lo >= hi || res <= 0 || trials == 0) return VMIN_BAD_ARG;           /* 측정 0회 */
    uint32_t runs0 = c->runs;
    r->probes++;
    if (!probe_all(c, t_c, f, hi, trials, 0)) { r->status = VMIN_NO_PASS; goto out; }
    r->probes++;
    if (probe_all(c, t_c, f, lo, trials, 0)) { r->status = VMIN_BELOW_RANGE; r->vmin = lo; goto out; }
    while (hi - lo > res) {
        int mid = lo + (hi - lo) / 2;                                       /* 오버플로 없는 중간값 */
        r->probes++;
        if (probe_all(c, t_c, f, mid, trials, 0)) hi = mid; else lo = mid;
    }
    r->status = VMIN_OK; r->vmin = hi;
out:
    r->runs = c->runs - runs0;
    return r->status;
}
/* V 에서 n 회 "새" 측정이 전부 PASS 인가 (trial = CONFIRM_BASE + salt*n + k) */
static bool confirm_pass(chip_t *c, int t_c, int f, int v, uint32_t n, uint32_t salt) {
    return probe_all(c, t_c, f, v, n, CONFIRM_BASE + salt * n);
}
/* 반복 측정 탐색 + 확인: bisect(trials) → vmin 에서 confirm_n 회 새 측정. 실패하면 res 만큼 올려 다시 확인,
 * max_steps 를 넘거나 hi 를 넘으면 UNSTABLE. */
static vmin_status_t vmin_robust(chip_t *c, int t_c, int f, int lo, int hi, int res, uint32_t trials,
                                 uint32_t confirm_n, uint32_t max_steps, vmin_result_t *r) {
    uint32_t runs0 = c->runs;
    vmin_status_t st = vmin_bisect(c, t_c, f, lo, hi, res, trials, r);
    if (st == VMIN_OK || st == VMIN_BELOW_RANGE) {
        int v = r->vmin;
        while (!confirm_pass(c, t_c, f, v, confirm_n, r->confirm_steps)) {
            r->confirm_steps++;
            v += res;
            if (r->confirm_steps > max_steps || v > hi) { r->status = VMIN_UNSTABLE; r->vmin = -1; break; }
        }
        if (r->status != VMIN_UNSTABLE) r->vmin = v;
    }
    r->runs = c->runs - runs0;
    return r->status;
}

/* ======================= Part C: 이상(anomaly) 분석 · OPP 마진 · 리포트 ======================= */
typedef enum { AN_HOLE, AN_SPORADIC } an_kind_t;
typedef struct {
    an_kind_t kind; int f;
    int v_lo, v_hi;           /* 해당 셀들의 V 범위 */
    int cells;                /* 셀 개수 */
    uint32_t min_pass;        /* 가장 나쁜 셀의 PASS 횟수 */
    int dist_mv;              /* 첫 clean PASS 행보다 몇 mV 위인가 */
} anomaly_t;

/* 열마다 p = 아래에서 처음 만나는 PASS 행. p 위에서
 *   - FAIL 연속 구간 → HOLE 1개씩 (아래가 PASS 인데 위가 FAIL = 비단조)
 *   - MIXED 이고 V - V(p) > edge_band_mv → SPORADIC (열마다 1개로 합침: 범위·개수·최소 pass)
 * 반환 = 찾은 이상 개수 (max 를 넘어도 끝까지 센다; out 에는 max 개까지만) */
static int shmoo_analyze(const shmoo_t *s, int edge_band_mv, anomaly_t *out, int max) {
    int n = 0;
    for (int j = 0; j < NF; j++) {
        int p = -1;
        for (int i = 0; i < NV; i++)
            if (cell_class(s->npass[i][j], s->trials) == CELL_PASS) { p = i; break; }
        if (p < 0) continue;                              /* clean PASS 없음: OPP 검사가 잡는다 */
        anomaly_t sp = { .kind = AN_SPORADIC, .f = F_AT(j), .min_pass = s->trials };
        int i = p + 1;
        while (i < NV) {
            cell_t cl = cell_class(s->npass[i][j], s->trials);
            if (cl == CELL_FAIL) {
                int start = i;
                while (i < NV && cell_class(s->npass[i][j], s->trials) == CELL_FAIL) i++;
                if (n < max) out[n] = (anomaly_t){ .kind = AN_HOLE, .f = F_AT(j), .v_lo = V_AT(start),
                                                    .v_hi = V_AT(i - 1), .cells = i - start, .min_pass = 0,
                                                    .dist_mv = V_AT(start) - V_AT(p) };
                n++;
                continue;
            }
            if (cl == CELL_MIXED && V_AT(i) - V_AT(p) > edge_band_mv) {
                if (sp.cells == 0) { sp.v_lo = V_AT(i); sp.dist_mv = V_AT(i) - V_AT(p); }
                sp.v_hi = V_AT(i);
                sp.cells++;
                if (s->npass[i][j] < sp.min_pass) sp.min_pass = s->npass[i][j];
            }
            i++;
        }
        if (sp.cells) { if (n < max) out[n] = sp; n++; }
    }
    return n;
}

typedef struct { int f; int v; } opp_t;
typedef struct {
    int f, v_opp;
    int vmin_fine;            /* vmin_robust 결과 (1 mV) — 단조성을 가정 */
    int vmin_shmoo;           /* shmoo 열의 strict Vmin (10 mV 격자) — 홀/산발을 반영 */
    int vmin_eff;             /* 판정에 쓰는 값: shmoo 가 fine 보다 한 격자 이상 높으면 shmoo(이상 반영), 아니면 fine.
                                 하나라도 없으면 -1 */
    int margin;               /* v_opp - vmin_eff (vmin_eff < 0 이면 의미 없음) */
    bool pass;
} opp_res_t;

#define OPP_TRIALS     8u
#define OPP_CONFIRM    200u
#define OPP_MAX_STEPS  40u

/* OPP 하나 판정: fine(1 mV 해상도) 와 shmoo strict(이상 반영) 를 합친 vmin_eff 로 margin = v_opp - vmin_eff ≥ guard_mv */
static void opp_check(chip_t *c, const shmoo_t *s, const opp_t *o, int guard_mv, opp_res_t *r) {
    *r = (opp_res_t){ .f = o->f, .v_opp = o->v, .vmin_fine = -1, .vmin_shmoo = -1, .vmin_eff = -1 };
    int j = (o->f - F_LO) / F_STEP;
    r->vmin_shmoo = shmoo_vmin_col(s, j);
    vmin_result_t vr;
    vmin_status_t st = vmin_robust(c, s->t_c, o->f, V_LO, V_HI, 1, OPP_TRIALS, OPP_CONFIRM, OPP_MAX_STEPS, &vr);
    if (st == VMIN_OK || st == VMIN_BELOW_RANGE) r->vmin_fine = vr.vmin;
    if (r->vmin_fine >= 0 && r->vmin_shmoo >= 0)
        r->vmin_eff = r->vmin_shmoo - r->vmin_fine >= V_STEP ? r->vmin_shmoo : r->vmin_fine;
    r->margin = r->vmin_eff >= 0 ? o->v - r->vmin_eff : 0;
    r->pass = r->vmin_eff >= 0 && r->margin >= guard_mv;
}

/* 디버그 엔지니어용 리포트 (chip·온도 하나). 줄 형식은 md 의 Part C 참고. 반환 = 실패 항목 수 */
static int report_chip(char *buf, size_t n, const shmoo_t *s, const anomaly_t *an, int nan,
                       const opp_res_t *opp, int nopp, int guard_mv) {
    sbuf_t b; sb_init(&b, buf, n);
    int fails = 0;
    for (int k = 0; k < nan; k++, fails++) {
        const anomaly_t *a = &an[k];
        if (a->kind == AN_HOLE)
            sb_printf(&b, "HOLE     chip=%s T=%dC F=%dMHz V=%d..%dmV cells=%d pass=0/%u (+%dmV above first clean pass)"
                          " -> non-monotonic, bisect answer depends on probe points\n",
                      s->chip, s->t_c, a->f, a->v_lo, a->v_hi, a->cells, (unsigned)s->trials, a->dist_mv);
        else
            sb_printf(&b, "SPORADIC chip=%s T=%dC F=%dMHz V=%d..%dmV cells=%d worst=%u/%u (+%dmV above first clean pass)"
                          " -> not margin-related\n",
                      s->chip, s->t_c, a->f, a->v_lo, a->v_hi, a->cells, (unsigned)a->min_pass,
                      (unsigned)s->trials, a->dist_mv);
    }
    for (int k = 0; k < nopp; k++) {
        const opp_res_t *o = &opp[k];
        if (o->vmin_eff >= 0)
            sb_printf(&b, "OPP      chip=%s T=%dC F=%dMHz opp=%dmV vmin=%dmV (fine=%d shmoo=%d) margin=%dmV need>=%dmV %s\n",
                      s->chip, s->t_c, o->f, o->v_opp, o->vmin_eff, o->vmin_fine, o->vmin_shmoo, o->margin,
                      guard_mv, o->pass ? "PASS" : "FAIL");
        else
            sb_printf(&b, "OPP      chip=%s T=%dC F=%dMHz opp=%dmV vmin=none (fine=%d shmoo=%d) no stable pass <=%dmV FAIL\n",
                      s->chip, s->t_c, o->f, o->v_opp, o->vmin_fine, o->vmin_shmoo, V_HI);
        if (!o->pass) fails++;
    }
    sb_printf(&b, "SUMMARY  chip=%s T=%dC anomalies=%d opp_fail=%d/%d -> %s\n", s->chip, s->t_c, nan,
              fails - nan, nopp, fails ? "FAIL" : "PASS");
    return fails;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static shmoo_t g_s;                       /* 큰 구조체는 static (스택 절약) */
static char g_buf[16384];

static int round_up_grid(int v) {          /* 1 mV 진실값 → shmoo 격자(10 mV)로 올림 */
    if (v < 0) return -1;
    int i = (v - V_LO + V_STEP - 1) / V_STEP;
    return i < NV ? V_AT(i) : -1;
}

static void test_part_a(void) {
    /* A1 셀 분류 */
    CHECK(cell_class(0, 8) == CELL_FAIL && cell_class(8, 8) == CELL_PASS && cell_class(3, 8) == CELL_MIXED);
    CHECK(cell_class(0, 0) == CELL_FAIL);

    /* A2 노이즈 0 인 TT 칩: 모든 셀이 0 또는 trials, 실행 횟수 = 36*17*4 */
    chip_t tt = { .name = "TT-ideal", .corner = CORNER_TT, .seed = 1 };
    shmoo_sweep(&tt, 25, 4, &g_s);
    int bad = 0, nonmono = 0, vmin_mismatch = 0;
    for (int j = 0; j < NF; j++) {
        bool seen_pass = false;
        for (int i = 0; i < NV; i++) {
            uint16_t k = g_s.npass[i][j];
            if (k != 0 && k != 4) bad++;
            if (k == 4) seen_pass = true; else if (seen_pass) nonmono++;
        }
        if (shmoo_vmin_col(&g_s, j) != round_up_grid(vmin_truth(&tt, 25, F_AT(j), 0, V_LO, V_HI))) vmin_mismatch++;
    }
    CHECK(bad == 0 && tt.runs == (uint32_t)NV * NF * 4);
    CHECK(nonmono == 0);                                    /* 결함 없는 모델은 열마다 단조 */
    CHECK(vmin_mismatch == 0);                              /* 열 Vmin = 1 mV 진실을 격자로 올린 값 */
    CHECK(shmoo_vmin_col(&g_s, 0) == 650 && shmoo_vmin_col(&g_s, (900 - F_LO) / F_STEP) == 770);

    /* A3 ASCII */
    int len = shmoo_render_ascii(g_buf, sizeof g_buf, &g_s);
    CHECK(len == (int)strlen(g_buf) && len > 0);
    CHECK(strstr(g_buf, "SHMOO chip=TT-ideal T=25C trials=4") == g_buf);
    CHECK(strstr(g_buf, "  mV |  40  45  50") != NULL && strstr(g_buf, " 115 120   F[x10MHz]\n") != NULL);
    CHECK(strstr(g_buf, "  650 |   *   *   *   *   *   *   .   .") != NULL);   /* 650 mV: 400..650 MHz 만 PASS */
    {   int nl = 0; for (const char *q = g_buf; *q; q++) nl += *q == '\n';
        CHECK(nl == NV + 2); }
    char tiny[32];
    int need = shmoo_render_ascii(tiny, sizeof tiny, &g_s);
    CHECK(need == len && strlen(tiny) == sizeof tiny - 1);  /* 잘려도 필요한 길이를 돌려주고 NUL 종료 */

    /* A4 CSV */
    len = shmoo_render_csv(g_buf, sizeof g_buf, &g_s);
    {   const char *hdr = "mV,400,450,500,550,600,650,700,750,800,850,900,950,1000,1050,1100,1150,1200\n";
        CHECK(strncmp(g_buf, hdr, strlen(hdr)) == 0); }
    CHECK(strstr(g_buf, "\n1000,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4\n") != NULL);
    {   int nl = 0; for (const char *q = g_buf; *q; q++) nl += *q == '\n';
        CHECK(nl == NV + 1 && len == (int)strlen(g_buf)); }

    /* A5 열 전체가 FAIL 이면 -1: SS 칩, 125°C, 1200 MHz 는 1.0 V 로도 안 됨 */
    chip_t ss = { .name = "SS-ideal", .corner = CORNER_SS, .seed = 2 };
    shmoo_sweep(&ss, 125, 2, &g_s);
    CHECK(shmoo_vmin_col(&g_s, NF - 1) == -1 && vmin_truth(&ss, 125, 1200, 0, V_LO, V_HI) == -1);
}

static void test_part_b(void) {
    vmin_result_t r;
    /* B1 노이즈 0: 1 mV 이진 탐색 = 1 mV 선형 진실, probe 수 ≤ 2 + ceil(log2(350)) = 11 */
    chip_t tt = { .name = "TT-ideal", .corner = CORNER_TT, .seed = 1 };
    int mism = 0; uint32_t max_probes = 0, total_runs = 0;
    for (int j = 0; j < NF; j++) {
        int f = F_AT(j), truth = vmin_truth(&tt, 25, f, 0, V_LO, V_HI);
        vmin_status_t st = vmin_bisect(&tt, 25, f, V_LO, V_HI, 1, 1, &r);
        vmin_status_t want = truth < 0 ? VMIN_NO_PASS : truth == V_LO ? VMIN_BELOW_RANGE : VMIN_OK;
        if (st != want || r.vmin != truth) mism++;
        if (r.probes > max_probes) max_probes = r.probes;
        total_runs += r.runs;
    }
    CHECK(mism == 0);
    CHECK(max_probes <= 11);
    printf("B1 bisect: 17 freqs, max probes=%u, total runs=%u (linear 1mV scan would be %d per freq)\n",
           (unsigned)max_probes, (unsigned)total_runs, V_HI - V_LO + 1);

    /* B2 경계와 인자 */
    CHECK(vmin_bisect(&tt, 25, 400, V_LO, V_HI, 1, 1, &r) == VMIN_BELOW_RANGE && r.vmin == V_LO);
    CHECK(vmin_bisect(&tt, 25, 1200, V_LO, 900, 1, 1, &r) == VMIN_NO_PASS && r.vmin == -1 && r.probes == 1);
    uint32_t runs0 = tt.runs;
    CHECK(vmin_bisect(&tt, 25, 800, 900, 900, 1, 1, &r) == VMIN_BAD_ARG);
    CHECK(vmin_bisect(&tt, 25, 800, V_LO, V_HI, 0, 1, &r) == VMIN_BAD_ARG);
    CHECK(vmin_bisect(&tt, 25, 800, V_LO, V_HI, 1, 0, &r) == VMIN_BAD_ARG && tt.runs == runs0);
    CHECK(vmin_bisect(&tt, 25, 800, V_LO, V_HI, 10, 1, &r) == VMIN_OK &&
          r.vmin >= vmin_truth(&tt, 25, 800, 0, V_LO, V_HI) && r.vmin - vmin_truth(&tt, 25, 800, 0, V_LO, V_HI) <= 10);

    /* B3 노이즈 ±2%: 1회 측정 탐색이라도 결과는 [진실(-2%), 진실(+2%)] 안 */
    chip_t nz = { .name = "TT-noisy", .corner = CORNER_TT, .seed = 0x5EED0007u, .noise_ppm = 20000 };
    int out_of_band = 0, naive_esc = 0, replay_pass = 0, robust_esc = 0, gb_esc = 0, stepped = 0, n_ok = 0;
    uint32_t naive_runs = 0, robust_runs = 0;
    for (int j = 0; j < NF; j++) {
        int f = F_AT(j);
        int lo_t = vmin_truth(&nz, 25, f, -20000, V_LO, V_HI), hi_t = vmin_truth(&nz, 25, f, +20000, V_LO, V_HI);
        if (vmin_bisect(&nz, 25, f, V_LO, V_HI, 1, 1, &r) != VMIN_OK || lo_t <= V_LO || hi_t < 0) continue;
        n_ok++;
        naive_runs += r.runs;
        if (r.vmin < lo_t || r.vmin > hi_t) out_of_band++;
        int naive = r.vmin;
        if (probe_all(&nz, 25, f, naive, 1, 0)) replay_pass++;        /* 같은 trial 0 재생 = 항상 PASS */
        if (!confirm_pass(&nz, 25, f, naive, 1000, 7)) naive_esc++;   /* 새 측정 1000회 */
        vmin_robust(&nz, 25, f, V_LO, V_HI, 1, 8, 200, 40, &r);
        robust_runs += r.runs;
        if (r.status != VMIN_OK || r.vmin < lo_t || r.vmin > hi_t) out_of_band++;
        if (r.confirm_steps) stepped++;
        if (!confirm_pass(&nz, 25, f, r.vmin, 1000, 7)) robust_esc++;
        if (!confirm_pass(&nz, 25, f, r.vmin + 10, 1000, 7)) gb_esc++;   /* + 10 mV guard band */
    }
    printf("B3 noisy: freqs=%d escape(1000 fresh) naive=%d robust=%d robust+10mV=%d stepped=%d runs naive=%u robust=%u\n",
           n_ok, naive_esc, robust_esc, gb_esc, stepped, (unsigned)naive_runs, (unsigned)robust_runs);
    CHECK(n_ok >= 10 && out_of_band == 0);
    CHECK(replay_pass == n_ok);                 /* 같은 난수를 다시 보면 "확인" 이 아니다 */
    CHECK(naive_esc >= n_ok / 2);               /* 1회 측정 Vmin 은 대부분 낙관적 */
    CHECK(robust_esc < naive_esc && stepped >= 1);   /* 반복 + 새 측정 확인: 크게 줄지만 0 은 아니다 (3/n 규칙) */
    CHECK(gb_esc == 0);                              /* 확인된 Vmin + guard band 에서야 탈출 0 */
}

/* ---------- Part C: 야간 특성화 런 ---------- */
static const opp_t OPPS[] = { { 500, 720 }, { 700, 820 }, { 900, 975 } };
#define NOPP ((int)(sizeof OPPS / sizeof OPPS[0]))
#define GUARD_MV  30
#define EDGE_BAND 50
static const int TEMPS[] = { -40, 25, 125 };

typedef struct { int chip_fails; int n_an[3]; anomaly_t an[3][24]; opp_res_t opp[3][NOPP]; } chip_run_t;

static int run_chip(chip_t *c, chip_run_t *cr, bool print) {
    memset(cr, 0, sizeof *cr);
    for (int ti = 0; ti < 3; ti++) {
        shmoo_sweep(c, TEMPS[ti], 20, &g_s);
        cr->n_an[ti] = shmoo_analyze(&g_s, EDGE_BAND, cr->an[ti], 24);
        int nan = cr->n_an[ti] < 24 ? cr->n_an[ti] : 24;
        for (int k = 0; k < NOPP; k++) opp_check(c, &g_s, &OPPS[k], GUARD_MV, &cr->opp[ti][k]);
        int fails = report_chip(g_buf, sizeof g_buf, &g_s, cr->an[ti], nan, cr->opp[ti], NOPP, GUARD_MV);
        const char *sum = strstr(g_buf, "SUMMARY");
        if (print) fputs(fails ? g_buf : sum ? sum : "", stdout);   /* PASS 는 조용히 SUMMARY 한 줄 */
        cr->chip_fails += fails;
    }
    return cr->chip_fails;
}

static void test_part_c(void) {
    chip_t chips[] = {
        { .name = "TT-01", .corner = CORNER_TT, .seed = 0xA001, .noise_ppm = 20000 },
        { .name = "SS-05", .corner = CORNER_SS, .seed = 0xA005, .noise_ppm = 20000 },
        { .name = "FF-06", .corner = CORNER_FF, .seed = 0xA006, .noise_ppm = 20000 },
        { .name = "FF-02", .corner = CORNER_FF, .seed = 0xA002, .noise_ppm = 20000,
          .fault = { .spor_ppm = 50000, .spor_t_min = 125, .spor_v_lo = 850, .spor_v_hi = 1000,
                     .spor_f_lo = 400, .spor_f_hi = 800 } },
        { .name = "TT-03", .corner = CORNER_TT, .seed = 0xA003, .noise_ppm = 20000,
          .fault = { .hole_f = 700, .hole_v_lo = 800, .hole_v_hi = 840 } },
        { .name = "SS-04", .corner = CORNER_SS, .seed = 0xA004, .noise_ppm = 20000,
          .fault = { .extra_vth_mv = 45 } },
    };
    static chip_run_t cr[6];
    int bad_chips = 0;
    printf("---- night run: VDD_NPU characterization, 6 parts x {-40,25,125}C ----\n");
    for (int k = 0; k < 6; k++) bad_chips += run_chip(&chips[k], &cr[k], true) > 0;

    /* C1 양품 3개: 이상 0, OPP 전부 PASS */
    for (int k = 0; k < 3; k++) {
        int an = 0, of = 0;
        for (int ti = 0; ti < 3; ti++) { an += cr[k].n_an[ti]; for (int o = 0; o < NOPP; o++) of += !cr[k].opp[ti][o].pass; }
        CHECK(an == 0 && of == 0 && cr[k].chip_fails == 0);
    }
    /* C2 FF-02: 125°C 에서만 SPORADIC, 전부 주입 창(400..800 MHz, ≥850 mV) 안 */
    {   int in_win = 1;
        for (int a = 0; a < cr[3].n_an[2]; a++) {
            const anomaly_t *x = &cr[3].an[2][a];
            if (x->kind != AN_SPORADIC || x->f < 400 || x->f > 800 || x->v_lo < 850) in_win = 0;
        }
        CHECK(cr[3].n_an[0] == 0 && cr[3].n_an[1] == 0 && cr[3].n_an[2] >= 7 && in_win); }
    /* C3 TT-03: 모든 온도에서 HOLE 정확히 1개 = 700 MHz, 800..840 mV, 5셀.
     *    bisect 의 첫 mid(825 mV)가 홀에 떨어져 위쪽으로만 탐색 → fine=841 ("약한 칩" 처럼 보임). 진짜 경계는 ~680 */
    for (int ti = 0; ti < 3; ti++) {
        const anomaly_t *x = &cr[4].an[ti][0];
        const opp_res_t *o = &cr[4].opp[ti][1];
        CHECK(cr[4].n_an[ti] == 1 && x->kind == AN_HOLE && x->f == 700 && x->v_lo == 800 && x->v_hi == 840 && x->cells == 5);
        CHECK(o->vmin_fine == 841 && o->vmin_shmoo == 850 && o->vmin_eff == 841 && !o->pass);
    }
    /* C4 SS-04 (약한 다이): 이상(anomaly) 없음 = 지도는 깨끗하게 단조, 그냥 전체가 위로 밀림 → 700 MHz OPP 는 모든 온도에서 FAIL */
    CHECK(cr[5].n_an[0] + cr[5].n_an[1] + cr[5].n_an[2] == 0);
    CHECK(!cr[5].opp[0][1].pass && !cr[5].opp[1][1].pass && !cr[5].opp[2][1].pass);
    CHECK(bad_chips == 3);

    /* C5 리포트 문자열 */
    {   shmoo_sweep(&chips[4], 25, 20, &g_s);
        anomaly_t an[4]; int nan = shmoo_analyze(&g_s, EDGE_BAND, an, 4);
        opp_res_t o[NOPP]; for (int k = 0; k < NOPP; k++) opp_check(&chips[4], &g_s, &OPPS[k], GUARD_MV, &o[k]);
        int f = report_chip(g_buf, sizeof g_buf, &g_s, an, nan, o, NOPP, GUARD_MV);
        CHECK(f == 2);
        CHECK(strstr(g_buf, "HOLE     chip=TT-03 T=25C F=700MHz V=800..840mV cells=5 pass=0/20") != NULL);
        CHECK(strstr(g_buf, "F=700MHz opp=820mV vmin=841mV (fine=841 shmoo=850) margin=-21mV need>=30mV FAIL") != NULL);
        CHECK(strstr(g_buf, "SUMMARY  chip=TT-03 T=25C anomalies=1 opp_fail=1/3 -> FAIL\n") != NULL); }

    /* C6 최악 코너: 양품 3개 × 3온도 중 OPP 별 vmin_eff 최대값을 낸 (칩, 온도) — 온도 역전 때문에 바뀐다 */
    int lim_t[NOPP], lim_v[NOPP]; const char *lim_c[NOPP];
    for (int o = 0; o < NOPP; o++) {
        lim_v[o] = -1; lim_t[o] = 0; lim_c[o] = "?";
        for (int k = 0; k < 3; k++) for (int ti = 0; ti < 3; ti++)
            if (cr[k].opp[ti][o].vmin_eff > lim_v[o]) { lim_v[o] = cr[k].opp[ti][o].vmin_eff; lim_t[o] = TEMPS[ti]; lim_c[o] = chips[k].name; }
        printf("WORST    OPP %4dMHz@%dmV: vmin=%dmV by %s at %dC -> margin %dmV\n", OPPS[o].f, OPPS[o].v, lim_v[o],
               lim_c[o], lim_t[o], OPPS[o].v - lim_v[o]);
    }
    CHECK(lim_t[0] == -40 && strcmp(lim_c[0], "SS-05") == 0);   /* 저전압 OPP: 차가울 때가 최악 */
    CHECK(lim_t[2] == 125 && strcmp(lim_c[2], "SS-05") == 0);   /* 고전압 OPP: 뜨거울 때가 최악 */
}

int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
