/* SIV-M07 추가 연습 — 한 번에 검증
 * 본문(solution.c)의 DUT 모델 · 격자 · sweep · Vmin bisect 를 그대로 복사해 넣고 (lesson 디렉터리 밖 include 금지),
 * 연습 1~5 의 답을 구현한다.
 *   P1 Fmax 이진 탐색 (고정 V)            P2 홀이 probe 위에 있을 때: bisect 의 낙관 vs coarse(top-down)+fine
 *   P3 0-실패 신뢰도: 필요한 반복 수 · 검출 확률  P4 두 shmoo 비교 (25°C vs 125°C, 열별 Vmin 이동)
 *   P5 온도 역전 교차 전압을 이진 탐색으로
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice -lm && ./practice
 */
#include <math.h>
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

/* 열 j 의 strict Vmin: "그 V 부터 V_HI 까지 모든 셀이 PASS" 인 최저 격자 V. 맨 위가 PASS 가 아니면 -1 */
static int shmoo_vmin_col(const shmoo_t *s, int j) {
    int v = -1;
    for (int i = NV - 1; i >= 0; i--) {
        if (cell_class(s->npass[i][j], s->trials) != CELL_PASS) break;
        v = V_AT(i);
    }
    return v;
}

/* 주어진 헬퍼: V 에서 trial base..base+trials-1 이 전부 PASS 인가. 첫 FAIL 에서 중단(시간 절약) */
static bool probe_all(chip_t *c, int t_c, int f, int v, uint32_t trials, uint32_t base) {
    for (uint32_t k = 0; k < trials; k++)
        if (!measure_once(c, v, f, t_c, base + k)) return false;
    return true;
}

typedef enum { VMIN_OK, VMIN_BELOW_RANGE, VMIN_NO_PASS, VMIN_UNSTABLE, VMIN_BAD_ARG } vmin_status_t;
typedef struct {
    vmin_status_t status;
    int vmin;                 /* OK/BELOW_RANGE: 찾은 V (BELOW_RANGE 면 lo). 그 외 -1 */
    uint32_t probes;          /* 탐색에서 V 를 바꿔 본 횟수 */
    uint32_t runs;            /* 이 호출이 쓴 measure_once 횟수 = 테스트 시간 */
    uint32_t confirm_steps;   /* robust: 확인 실패로 res 만큼 올린 횟수 */
} vmin_result_t;


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

/* ======================= 연습 1: Fmax 이진 탐색 (고정 V) ======================= */
/* PASS 는 F 에 대해 단조 감소. 불변식: lo PASS, hi FAIL → hi-lo ≤ res 이면 fmax = lo.
 * 반환 -1: f_lo 에서도 FAIL. f_hi 에서도 PASS 면 f_hi (범위 위) */
static int fmax_bisect(chip_t *c, int t_c, int v, int f_lo, int f_hi, int res, uint32_t trials) {
    if (!probe_all(c, t_c, f_lo, v, trials, 0)) return -1;
    if (probe_all(c, t_c, f_hi, v, trials, 0)) return f_hi;
    while (f_hi - f_lo > res) {
        int mid = f_lo + (f_hi - f_lo) / 2;
        if (probe_all(c, t_c, mid, v, trials, 0)) f_lo = mid; else f_hi = mid;
    }
    return f_lo;
}
static int fmax_truth(const chip_t *c, int t_c, int v, int f_lo, int f_hi) {
    int best = -1;
    for (int f = f_lo; f <= f_hi; f++) if (model_pass(c, v, f, t_c, 0)) best = f;   /* 단조라 마지막 PASS = Fmax */
    return best;
}

/* ======================= 연습 2: coarse(top-down) + fine ======================= */
/* 위(안전)에서 step 씩 내려오다 처음 FAIL 을 만나면, 그 FAIL 과 바로 위 PASS 사이만 1 mV bisect.
 * = "여기부터 위로 전부 PASS" (strict) 의 1 mV 버전. 홀이 있으면 홀 위를 답한다 (보수적, 정직) */
static int vmin_coarse_fine(chip_t *c, int t_c, int f, int lo, int hi, int step, uint32_t trials) {
    if (!probe_all(c, t_c, f, hi, trials, 0)) return -1;
    int pass_v = hi;
    for (int v = hi - step; v >= lo; v -= step) {
        if (!probe_all(c, t_c, f, v, trials, 0)) {               /* [v FAIL, pass_v PASS] 사이를 bisect */
            int a = v, b = pass_v;
            while (b - a > 1) { int m = a + (b - a) / 2; if (probe_all(c, t_c, f, m, trials, 0)) b = m; else a = m; }
            return b;
        }
        pass_v = v;
    }
    return pass_v;                                                /* lo 까지 전부 PASS */
}

/* ======================= 연습 3: 0-실패 신뢰도 ======================= */
/* 실패 확률이 p 이상이면 n 회 중 한 번은 볼 확률이 CL 이상이 되는 최소 n: (1-p)^n ≤ 1-CL */
static uint32_t trials_needed(double p, double cl) {
    return (uint32_t)ceil(log(1.0 - cl) / log(1.0 - p));
}
/* n 회 전부 PASS 를 봤을 때, CL 신뢰도로 말할 수 있는 실패 확률 상한: 1 - (1-CL)^(1/n)  (≈ 3/n @95%) */
static double p_upper(uint32_t n, double cl) { return 1.0 - pow(1.0 - cl, 1.0 / (double)n); }
/* 실패율 p 인 산발 결함을 n 회 안에 적어도 한 번 볼 확률 */
static double detect_prob(double p, uint32_t n) { return 1.0 - pow(1.0 - p, (double)n); }

/* ======================= 연습 4: 두 shmoo 비교 ======================= */
typedef struct { int changed; int worst_j; int worst_delta; int dvmin[NF]; } shmoo_diff_t;
/* 셀 분류가 바뀐 수, 열별 strict Vmin 변화(b - a, 한쪽이 -1 이면 9999), 가장 크게 오른 열 */
static void shmoo_diff(const shmoo_t *a, const shmoo_t *b, shmoo_diff_t *d) {
    memset(d, 0, sizeof *d);
    d->worst_j = -1; d->worst_delta = -9999;
    for (int j = 0; j < NF; j++) {
        for (int i = 0; i < NV; i++)
            d->changed += cell_class(a->npass[i][j], a->trials) != cell_class(b->npass[i][j], b->trials);
        int va = shmoo_vmin_col(a, j), vb = shmoo_vmin_col(b, j);
        d->dvmin[j] = (va < 0 || vb < 0) ? (va == vb ? 0 : 9999) : vb - va;
        if (d->dvmin[j] > d->worst_delta) { d->worst_delta = d->dvmin[j]; d->worst_j = j; }
    }
}

/* ======================= 연습 5: 온도 역전 교차 전압 ======================= */
/* g(V) = delay(hot) - delay(cold). 저전압에선 g<0 (뜨거운 쪽이 빠름), 고전압에선 g≥0.
 * g(V) ≥ 0 이 되는 최저 V 를 이진 탐색. 범위 안에 부호 변화가 없으면 -1 */
static int inversion_crossover(const chip_t *c, int t_cold, int t_hot, int lo, int hi) {
    #define G_NONNEG(v) (delay_ps(c, (v), t_hot) >= delay_ps(c, (v), t_cold))
    if (G_NONNEG(lo) || !G_NONNEG(hi)) return -1;
    while (hi - lo > 1) { int m = lo + (hi - lo) / 2; if (G_NONNEG(m)) hi = m; else lo = m; }
    return hi;
    #undef G_NONNEG
}

/* ======================= 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static shmoo_t g_a, g_b;

int main(void) {
    /* P1 */
    chip_t tt = { .name = "TT", .corner = CORNER_TT, .seed = 1 };
    int mism = 0;
    for (int v = 700; v <= 1000; v += 50) {
        int fb = fmax_bisect(&tt, 25, v, 100, 2000, 1, 1), ft = fmax_truth(&tt, 25, v, 100, 2000);
        if (fb != ft) mism++;
        if (v == 800 || v == 1000) printf("P1 V=%dmV Fmax=%dMHz\n", v, fb);
    }
    CHECK(mism == 0);
    CHECK(fmax_bisect(&tt, 25, 650, 2000, 3000, 1, 1) == -1);

    /* P2 홀(700 MHz, 880..920 mV)이 bisect probe 들 위에 있다 */
    chip_t h = { .name = "TT-hole", .corner = CORNER_TT, .seed = 3,
                 .fault = { .hole_f = 700, .hole_v_lo = 880, .hole_v_hi = 920 } };
    vmin_result_t r;
    vmin_bisect(&h, 25, 700, V_LO, V_HI, 1, 1, &r);
    uint32_t r0 = h.runs;
    int cf = vmin_coarse_fine(&h, 25, 700, V_LO, V_HI, 10, 1);
    uint32_t cf_runs = h.runs - r0;
    printf("P2 bisect=%dmV (runs %u)  coarse+fine=%dmV (runs %u)\n", r.vmin, (unsigned)r.runs, cf, (unsigned)cf_runs);
    CHECK(r.status == VMIN_OK && r.vmin < 880);                  /* bisect 는 홀을 못 보고 낙관적 답 */
    CHECK(cf == 921);                                            /* top-down 은 홀 바로 위 = strict Vmin */
    chip_t clean = { .name = "TT", .corner = CORNER_TT, .seed = 3 };
    CHECK(vmin_coarse_fine(&clean, 25, 700, V_LO, V_HI, 10, 1) == vmin_truth(&clean, 25, 700, 0, V_LO, V_HI));

    /* P3 */
    printf("P3 n(1%%,95%%)=%u n(0.1%%,95%%)=%u n(5%%,95%%)=%u p_up(200)=%.4f%% detect(5%%,20)=%.2f%%\n",
           (unsigned)trials_needed(0.01, 0.95), (unsigned)trials_needed(0.001, 0.95), (unsigned)trials_needed(0.05, 0.95),
           100.0 * p_upper(200, 0.95), 100.0 * detect_prob(0.05, 20));
    CHECK(trials_needed(0.01, 0.95) == 299 && trials_needed(0.001, 0.95) == 2995 && trials_needed(0.05, 0.95) == 59);
    CHECK(fabs(p_upper(200, 0.95) - 0.014868) < 1e-5 && fabs(p_upper(200, 0.95) - 3.0 / 200) < 0.0003);
    CHECK(fabs(detect_prob(0.05, 20) - 0.6415) < 1e-4);

    /* P4 같은 TT 칩 25°C vs 125°C (노이즈 0) */
    shmoo_sweep(&tt, 25, 1, &g_a);
    shmoo_sweep(&tt, 125, 1, &g_b);
    shmoo_diff_t d;
    shmoo_diff(&g_a, &g_b, &d);
    int j500 = (500 - F_LO) / F_STEP, j700 = (700 - F_LO) / F_STEP, j900 = (900 - F_LO) / F_STEP;
    printf("P4 changed=%d dVmin@500=%d @700=%d @900=%d worst=%dMHz (%+d)\n", d.changed, d.dvmin[j500],
           d.dvmin[j700], d.dvmin[j900], F_AT(d.worst_j), d.worst_delta);
    CHECK(d.changed > 0 && d.dvmin[j700] < 0 && d.dvmin[j900] > 0);   /* 저전압 열은 뜨거울 때 좋아지고, 고전압 열은 나빠진다 */
    CHECK(d.worst_j == NF - 1 || d.worst_delta == 9999);

    /* P5 */
    int x = inversion_crossover(&tt, -40, 125, V_LO, V_HI);
    printf("P5 TT crossover=%dmV  d(-40)=%llu d(125)=%llu at %dmV\n", x, (unsigned long long)delay_ps(&tt, x, -40),
           (unsigned long long)delay_ps(&tt, x, 125), x);
    int scan = -1;
    for (int v = V_LO; v <= V_HI; v++) if (delay_ps(&tt, v, 125) >= delay_ps(&tt, v, -40)) { scan = v; break; }
    CHECK(x == scan && x > 650 && x < 800);
    CHECK(delay_ps(&tt, x - 1, 125) < delay_ps(&tt, x - 1, -40));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
