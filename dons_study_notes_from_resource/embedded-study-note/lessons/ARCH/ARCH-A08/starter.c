/* ARCH-A08 전력 모델: DVFS와 Race-to-Idle 에너지 계산
 *
 * 모델 (SI 단위: W, J, s, V, Hz, F, A. 숫자는 전부 "대표값/가정" — 특정 칩 데이터시트 값이 아니다)
 *   P_dyn  = α · C · V² · f                                  동적(스위칭) 전력
 *   P_leak = V · I_leak0 · scale · e^(k_v·(V−V0)) · e^((T−T0)/T_s)   누설 전력 (전압·온도에 지수)
 *   P_on   = P_dyn + P_leak + P_static     P_static = 전원이 켜져 있으면 V 와 무관하게 드는 몫 (PLL, 레귤레이터, always-on)
 *   idle  (clock gated)  클럭만 멈춤 → P_leak(V_idle) + P_static 은 계속           (WFI 같은 얕은 상태)
 *   sleep (power gated)  P_sleep 만. 대신 진입+복귀 에너지 e_trans, 시간 t_trans    (코어/클러스터 power-down)
 *
 *   작업 = N 사이클, 마감 D.  mem_frac m = f_ref 에서 잰 실행 시간 중 메모리 스톨 비율 (f 를 올려도 안 줄어든다)
 *     t_run(f) = N·(1−m)/f + N·m/f_ref        스톨 동안의 활동도 = α · alpha_stall
 *
 *   Part A  계산기: p_dyn · p_leak · opp_table_ok · run_time · sleep_breakeven · eval_opp           (TODO 6개)
 *   Part B  전략: race-to-idle / race-to-sleep / pace / optimal, critical speed, crossover 스윕     (TODO 3개)
 *   Part C  듀티 사이클 센서 노드: 배터리 에너지 · 항목별 평균 전력 · 수명(일) · 민감도 표        (TODO 3개)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 *        macOS 는 libm 이 libSystem 안에 있어 -lm 불필요. Linux 에서는 끝에 -lm.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ======================= 모델 자료형 ======================= */
typedef struct { double f_hz, v; } opp_t;            /* Operating Performance Point: (주파수, 전압) 한 쌍 */

typedef struct {
    double alpha;        /* 활동도 α: 사이클당 평균 스위칭 비율 (0..1) */
    double c_sw;         /* 스위칭 커패시턴스 C [F] */
    double alpha_stall;  /* 메모리 스톨 동안의 활동도 배율 (실효 α = alpha · alpha_stall) */
    double i_leak0;      /* 누설 전류 @ V0, T0, scale 1 [A] */
    double v0, k_v;      /* 기준 전압 [V], 전압 지수 계수 [1/V] (DIBL 등을 뭉뚱그린 경험식) */
    double t0, t_s;      /* 기준 온도 [°C], 온도 e-배 스케일 [°C] (t_s 25 → 약 17 °C 마다 2 배) */
    double temp_c;       /* 접합 온도 Tj [°C] */
    double leak_scale;   /* 누설 배율 (공정 코너·칩 편차: SS 칩 < 1 < FF 칩) */
    double p_static;     /* 켜져 있으면 V 와 무관하게 드는 전력 [W] */
    double p_sleep;      /* power-gated 슬립 전력 [W] (retention, 웨이크업 로직) */
    double e_trans;      /* 슬립 진입+복귀 에너지 [J] (상태 저장·복원, 레일 재충전, 캐시 flush) */
    double t_trans;      /* 슬립 진입+복귀 시간 [s] */
    bool idle_drops_v;   /* idle 동안 전압을 OPP0 으로 내리나 (false = 실행하던 OPP 의 전압 유지) */
} pmodel_t;

typedef struct {
    double n_cycles;     /* 작업량 N [cycle] (f_ref 기준) */
    double mem_frac;     /* f_ref 에서 잰 실행 시간 중 메모리 스톨 비율 m (0..1) */
    double f_ref;        /* m 을 잰 주파수 [Hz] */
    double deadline;     /* 마감 D [s] — 이 안에서 실행 + 휴식, 에너지는 [0, D] 전체로 센다 */
} job_t;

typedef enum { REST_NONE, REST_IDLE, REST_SLEEP } rest_t;
static const char *const REST_NAME[] = {"none", "idle", "sleep"};
typedef enum { POL_IDLE_ONLY, POL_BEST } rest_pol_t;    /* 휴식 정책: idle 만 / idle·sleep 중 싼 쪽 */

typedef struct {
    int opp;             /* 고른 OPP 번호 (-1 = 가능한 OPP 없음) */
    bool feasible;       /* t_run ≤ D */
    double t_run, e_run; /* 실행 시간 [s], 실행 에너지 [J] */
    double slack, e_rest;/* 남은 시간 [s], 휴식 에너지 [J] */
    rest_t rest;
    double e_total;      /* e_run + e_rest [J] */
} plan_t;

typedef enum { S_RACE_IDLE, S_RACE_SLEEP, S_PACE, S_OPT, NSTRAT } strat_t;
static const char *const STRAT_NAME[NSTRAT] = {"race-to-idle", "race-to-sleep", "pace", "optimal"};

/* 대표값 OPP 표 (앱 프로세서 코어 하나를 흉내 낸 가정값. f 오름차순, V 비감소) */
#define NOPP 5
static const opp_t OPP5[NOPP] = {
    {300e6, 0.60}, {600e6, 0.70}, {1000e6, 0.80}, {1400e6, 0.90}, {1800e6, 1.00}};

static pmodel_t model_default(void) {
    pmodel_t m = {
        .alpha = 0.25, .c_sw = 1.0e-9, .alpha_stall = 0.3,
        .i_leak0 = 0.05, .v0 = 0.80, .k_v = 3.0, .t0 = 25.0, .t_s = 25.0, .temp_c = 25.0, .leak_scale = 1.0,
        .p_static = 0.02, .p_sleep = 0.002, .e_trans = 0.5e-3, .t_trans = 2e-3, .idle_drops_v = false};
    return m;
}
static job_t job_default(void) {
    job_t j = {.n_cycles = 1e9, .mem_frac = 0.0, .f_ref = 1800e6, .deadline = 4.0};
    return j;
}

/* ======================= Part A: 계산기 ======================= */

/* A-1  동적 전력 [W] */
static double p_dyn(const pmodel_t *m, double v, double f) {
    /* TODO(A-1): P_dyn = α · C · V² · f */
    (void)m; (void)v; (void)f;
    return 0.0;
}

/* A-2  누설 전력 [W] — 온도는 m->temp_c, 배율은 m->leak_scale */
static double p_leak(const pmodel_t *m, double v) {
    /* TODO(A-2): P_leak = V · I_leak0 · scale · e^(k_v·(V−V0)) · e^((T−T0)/T_s) */
    (void)m; (void)v;
    return 0.0;
}

/* A-3  OPP 표 검사: n ≥ 1, 모든 f·V 가 유한한 양수, f 는 순증가, V 는 비감소 */
static bool opp_table_ok(const opp_t *o, int n) {
    /* TODO(A-3): 조건을 하나라도 어기면 false. (같은 V 에 두 f 는 허용, 같은 f 두 번은 금지) */
    (void)o; (void)n;
    return false;
}

/* A-4  실행 시간 [s]: 계산 몫은 1/f 로 줄고, 메모리 스톨 몫(f_ref 에서 잰 시간)은 그대로 */
static double run_time(const job_t *j, double f) {
    /* TODO(A-4): t_run = N·(1−m)/f + N·m/f_ref */
    (void)j; (void)f;
    return 0.0;
}

/* A-5  sleep 손익분기 휴식 시간 [s]: 휴식 시간이 이 값보다 길어야 sleep 이 idle 보다 싸다.
 *      p_idle = idle 상태 전력. sleep 이 절대 이길 수 없으면 INFINITY. 진입+복귀 시간 t_trans 보다 짧을 수는 없다. */
static double sleep_breakeven(const pmodel_t *m, double p_idle) {
    /* TODO(A-5): idle  : p_idle · s
     *            sleep : e_trans + p_sleep · (s − t_trans)        (s ≥ t_trans 일 때만 가능)
     *            두 식이 같아지는 s 를 구하고, t_trans 와 비교해 큰 쪽 */
    (void)m; (void)p_idle;
    return 0.0;
}

/* A-6  OPP i 로 작업을 실행하고 남은 시간을 쉴 때의 에너지.
 *      실행:  계산 시간 t_c 동안 P_dyn(V,f), 스톨 시간 t_m 동안 alpha_stall·P_dyn(V,f), 내내 P_leak(V) + P_static
 *      휴식:  slack = D − t_run (음수면 0, feasible = false).  idle 전압 = idle_drops_v ? OPP0 의 V : 실행 V
 *             pol == POL_BEST 이고 slack ≥ t_trans 이고 sleep 이 idle 보다 "엄격히" 싸면 sleep, 아니면 idle.
 *             slack == 0 이면 REST_NONE, e_rest = 0.
 *      feasible 판정은 부동소수 오차를 위해 t_run ≤ D·(1 + 1e-12). */
static plan_t eval_opp(const pmodel_t *m, const opp_t *o, int n, int i, const job_t *j, rest_pol_t pol) {
    plan_t p;
    memset(&p, 0, sizeof p);
    p.opp = i;
    if (i < 0 || i >= n) { p.opp = -1; return p; }
    /* TODO(A-6): 위 규칙대로 t_run, e_run, slack, rest, e_rest, e_total, feasible 을 채운다 */
    (void)m; (void)o; (void)j; (void)pol;
    return p;
}

/* ======================= Part B: 전략과 critical speed ======================= */

/* B-1  전략별 계획.
 *   S_RACE_IDLE  : 최고 OPP, 휴식은 idle 만
 *   S_RACE_SLEEP : 최고 OPP, 휴식은 POL_BEST
 *   S_PACE       : 마감을 지키는 가장 낮은 OPP, 휴식은 POL_BEST
 *   S_OPT        : 마감을 지키는 OPP 중 e_total 최소 (POL_BEST, 동점이면 낮은 OPP)
 *   가능한 OPP 가 없으면 opp = -1, feasible = false 인 plan (race 는 최고 OPP 의 결과를 그대로, feasible = false). */
static plan_t plan_strategy(const pmodel_t *m, const opp_t *o, int n, const job_t *j, strat_t s) {
    plan_t none;
    memset(&none, 0, sizeof none);
    none.opp = -1;
    /* TODO(B-1) */
    (void)m; (void)o; (void)n; (void)j; (void)s;
    return none;
}

/* B-2  사이클당 에너지 [J/cycle] — 계산만 하는 작업(m = 0)을 그 (V, f) 로 돌릴 때 (휴식 제외) */
static double e_per_cycle(const pmodel_t *m, double v, double f) {
    /* TODO(B-2): (P_dyn + P_leak + P_static) / f */
    (void)m; (void)v; (void)f;
    return 0.0;
}

/* B-3  critical OPP: e_per_cycle 가 최소인 OPP 번호 (동점이면 낮은 쪽) */
static int critical_opp(const pmodel_t *m, const opp_t *o, int n) {
    /* TODO(B-3) */
    (void)m; (void)o; (void)n;
    return -1;
}

/* ======================= Part C: 듀티 사이클 센서 노드 ======================= */
typedef struct {
    double period_s;                         /* 깨어나는 주기 [s] */
    double v_load;                           /* 부하 전압 (레귤레이터 출력) [V] */
    double mcu_cycles, mcu_f_hz;             /* 깨어날 때마다 MCU 가 도는 사이클, 클럭 */
    double mcu_ua_per_mhz, mcu_i_base_a;     /* MCU 활성 전류 = k[µA/MHz]·f[MHz] + I_base */
    double sensor_i_a, sensor_t_s;           /* 센서 측정 전류·시간 */
    double tx_i_a, tx_t_s, rx_i_a, rx_t_s;   /* 무선 송신·수신(ACK 창) 전류·시간 */
    double sleep_i_a;                        /* 부하 쪽 슬립 전류 (RTC + RAM retention + 센서 대기) */
    double eta_active, eta_sleep;            /* 레귤레이터 효율: mA 부하 / µA 부하 (경부하에서 나빠진다) */
    double iq_a;                             /* 레귤레이터 자체 소비 (배터리 쪽) [A] */
    double batt_mah, batt_v;                 /* 공칭 용량 [mAh], 공칭 전압 [V] */
    double derate;                           /* 실제로 꺼낼 수 있는 비율 (컷오프 전압·펄스 부하·저온) */
    double self_dis_per_year;                /* 자가 방전 [공칭 용량 대비 비율 / 년] */
} node_t;

typedef enum { T_MCU, T_SENSOR, T_TX, T_RX, T_SLEEP, T_IQ, T_SELFDIS, NTERM } term_t;
static const char *const TERM_NAME[NTERM] = {"MCU active", "sensor", "radio TX", "radio RX", "sleep", "reg Iq",
                                             "self-discharge"};

typedef struct {
    bool ok;               /* false: 주기 안에 깨어 있는 시간이 다 안 들어감 */
    double t_awake;        /* 한 주기에 깨어 있는 시간 [s] (MCU → 센서 → TX → RX 순차, 겹침 없음 가정) */
    double p[NTERM];       /* 항목별 배터리 쪽 평균 전력 [W] */
    double p_total;        /* 합 [W] */
    double e_usable;       /* 꺼낼 수 있는 에너지 [J] */
} node_split_t;

/* C-1  배터리 에너지 [J] = mAh × V × 3.6   (1 mAh = 3.6 C, C × V = J) */
static double batt_energy_j(double mah, double v) {
    /* TODO(C-1) */
    (void)mah; (void)v;
    return 0.0;
}

/* C-2  항목별 배터리 쪽 평균 전력.
 *   활성 항목 X (MCU/센서/TX/RX): E_X = v_load · I_X · t_X  →  p = E_X / eta_active / period
 *     MCU: t = cycles / f,  I = ua_per_mhz·1e-6 · (f / 1e6) + I_base
 *   sleep : v_load · sleep_i · (period − t_awake) / period / eta_sleep
 *   Iq    : iq_a · batt_v                         (레귤레이터는 배터리에 바로 붙어 있다)
 *   자가방전: self_dis_per_year · E_nom / (365·86400)   (E_nom = batt_energy_j(batt_mah, batt_v))
 *   e_usable = E_nom · derate.   t_awake > period 이면 ok = false (나머지는 0). */
static node_split_t node_split(const node_t *nd) {
    node_split_t s;
    memset(&s, 0, sizeof s);
    /* TODO(C-2) */
    (void)nd;
    return s;
}

/* C-3  수명 [일] = e_usable / p_total / 86400.  ok 가 아니거나 p_total ≤ 0 이면 NAN */
static double node_life_days(const node_t *nd) {
    /* TODO(C-3) */
    (void)nd;
    return NAN;
}

/* 대표값 센서 노드: CR2032 코인셀 + BLE 급 SoC 를 흉내 낸 가정값 (특정 부품 데이터시트 아님) */
static node_t node_default(void) {
    node_t n = {
        .period_s = 10.0, .v_load = 1.8,
        .mcu_cycles = 200e3, .mcu_f_hz = 64e6, .mcu_ua_per_mhz = 50.0, .mcu_i_base_a = 0.1e-3,
        .sensor_i_a = 1.0e-3, .sensor_t_s = 10e-3,
        .tx_i_a = 6.0e-3, .tx_t_s = 1.5e-3, .rx_i_a = 5.0e-3, .rx_t_s = 1.0e-3,
        .sleep_i_a = 2.0e-6, .eta_active = 0.85, .eta_sleep = 0.70, .iq_a = 0.5e-6,
        .batt_mah = 220.0, .batt_v = 3.0, .derate = 0.8, .self_dis_per_year = 0.01};
    return n;
}

/* ======================= 하네스 (starter / solution 공통) ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 상대 오차 tol 안이면 참. 손계산 기대값은 유효숫자 10 자리로 적었으므로 기본 tol = 1e-9 */
static bool near(double got, double want, double tol) {
    if (isnan(got) || isnan(want)) return false;
    if (isinf(want)) return got == want;
    double d = fabs(got - want), s = fabs(want) > 1e-300 ? fabs(want) : 1.0;
    return d <= tol * s;
}

static void print_plan(const char *name, plan_t p) {
    if (p.opp < 0) { printf("  %-14s  (가능한 OPP 없음)\n", name); return; }
    printf("  %-14s  OPP%d %4.0f MHz %.2f V  t_run=%.4f s  E_run=%.5f J  slack=%.4f s %-5s E_rest=%.5f J  "
           "E=%.5f J%s\n", name, p.opp, OPP5[p.opp].f_hz / 1e6, OPP5[p.opp].v, p.t_run, p.e_run, p.slack,
           REST_NAME[p.rest], p.e_rest, p.e_total, p.feasible ? "" : "  (마감 초과!)");
}

static double e_strat(const pmodel_t *m, const job_t *j, strat_t s) {
    plan_t p = plan_strategy(m, OPP5, NOPP, j, s);
    return p.feasible ? p.e_total : INFINITY;
}

/* 하나의 파라미터 x 를 바꾸는 setter 와 이분법 crossover 탐색:
 * f(x) = E_a(x) − E_b(x) 의 부호가 lo 와 hi 에서 다르면 근을 찾고, 아니면 NAN */
typedef void (*setter_t)(pmodel_t *m, job_t *j, double x);
static double crossover(pmodel_t m, job_t j, setter_t set, double lo, double hi, strat_t a, strat_t b, bool logx) {
    pmodel_t ml = m, mh = m; job_t jl = j, jh = j;
    set(&ml, &jl, lo); set(&mh, &jh, hi);
    double fl = e_strat(&ml, &jl, a) - e_strat(&ml, &jl, b);
    double fh = e_strat(&mh, &jh, a) - e_strat(&mh, &jh, b);
    if (!isfinite(fl) || !isfinite(fh) || fl == 0 || fh == 0 || (fl < 0) == (fh < 0)) return NAN;
    for (int it = 0; it < 200; it++) {
        double mid = logx ? sqrt(lo * hi) : 0.5 * (lo + hi);
        pmodel_t mm = m; job_t jm = j;
        set(&mm, &jm, mid);
        double fm = e_strat(&mm, &jm, a) - e_strat(&mm, &jm, b);
        if (fm == 0) return mid;
        if ((fm < 0) == (fl < 0)) { lo = mid; fl = fm; } else { hi = mid; }
    }
    return logx ? sqrt(lo * hi) : 0.5 * (lo + hi);
}
static void set_leak(pmodel_t *m, job_t *j, double x) { (void)j; m->leak_scale = x; }
static void set_temp(pmodel_t *m, job_t *j, double x) { (void)j; m->temp_c = x; }
static void set_pstatic(pmodel_t *m, job_t *j, double x) { (void)j; m->p_static = x; }
static void set_psleep(pmodel_t *m, job_t *j, double x) { (void)j; m->p_sleep = x; }
static void set_etrans(pmodel_t *m, job_t *j, double x) { (void)j; m->e_trans = x; }
static void set_memfrac(pmodel_t *m, job_t *j, double x) { (void)m; j->mem_frac = x; }

static pmodel_t model_platform_heavy(void) {         /* Part B 의 "race 가 이기는" 기준점 */
    pmodel_t m = model_default();
    m.p_static = 0.10;
    return m;
}

static void strategy_table(const char *title, const pmodel_t *m, const job_t *j) {
    printf("  -- %s --\n", title);
    for (int s = 0; s < NSTRAT; s++) print_plan(STRAT_NAME[s], plan_strategy(m, OPP5, NOPP, j, (strat_t)s));
}

static void part_a(void) {
    printf("== Part A: 계산기 (대표값 모델, Tj 25 °C) ==\n");
    pmodel_t m = model_default();
    job_t j = job_default();
    /* A-1 / A-2 손계산 */
    CHECK(near(p_dyn(&m, 1.00, 1800e6), 0.45, 1e-12));
    CHECK(near(p_dyn(&m, 0.60, 300e6), 0.027, 1e-12));
    CHECK(near(p_dyn(&m, 1.20, 1000e6) / p_dyn(&m, 0.60, 1000e6), 4.0, 1e-12));   /* V 두 배 → 4 배 */
    CHECK(near(p_leak(&m, 0.80), 0.04, 1e-12));                                     /* V0, T0 에서 V0·I0 */
    CHECK(near(p_leak(&m, 1.00), 0.09110594002, 1e-9));                            /* 0.05·e^0.6 */
    CHECK(near(p_leak(&m, 0.60), 0.01646434908, 1e-9));                            /* 0.03·e^-0.6 */
    pmodel_t hot = m; hot.temp_c = 50.0;
    CHECK(near(p_leak(&hot, 0.80), 0.1087312731, 1e-9));                           /* 0.04·e^1 */
    pmodel_t ff = m; ff.leak_scale = 2.5;
    CHECK(near(p_leak(&ff, 0.80), 0.10, 1e-12));
    /* A-3 OPP 표 */
    CHECK(opp_table_ok(OPP5, NOPP));
    opp_t bad_v[3] = {{100e6, 0.7}, {200e6, 0.65}, {300e6, 0.8}};
    opp_t dup_f[3] = {{100e6, 0.6}, {100e6, 0.7}, {300e6, 0.8}};
    opp_t same_v[2] = {{100e6, 0.6}, {200e6, 0.6}};
    opp_t neg[1] = {{-1.0, 0.6}};
    opp_t nanv[2] = {{100e6, 0.6}, {200e6, NAN}};
    CHECK(!opp_table_ok(bad_v, 3) && !opp_table_ok(dup_f, 3) && opp_table_ok(same_v, 2));
    CHECK(!opp_table_ok(neg, 1) && !opp_table_ok(nanv, 2) && !opp_table_ok(OPP5, 0));
    /* A-4 실행 시간 */
    CHECK(near(run_time(&j, 1800e6), 0.5555555556, 1e-9));
    job_t jm = j; jm.mem_frac = 0.5;
    CHECK(near(run_time(&jm, 300e6), 1.944444444, 1e-9));
    CHECK(near(run_time(&jm, 1800e6), run_time(&j, 1800e6), 1e-12));               /* f_ref 에서는 m 무관 */
    /* A-5 손익분기 */
    double p_idle4 = p_leak(&m, 1.0) + m.p_static;                                  /* 0.1111059400 W */
    CHECK(near(sleep_breakeven(&m, p_idle4), 0.004546040297, 1e-9));
    CHECK(isinf(sleep_breakeven(&m, m.p_sleep)) && isinf(sleep_breakeven(&m, 0.001)));
    pmodel_t cheap = m; cheap.e_trans = 1e-6;
    CHECK(near(sleep_breakeven(&cheap, p_idle4), cheap.t_trans, 1e-12));             /* 바닥 = t_trans */
    /* A-6 OPP 하나의 계획: 손계산 (풀이 표 참고) */
    plan_t r_idle = eval_opp(&m, OPP5, NOPP, 4, &j, POL_IDLE_ONLY);
    plan_t r_best = eval_opp(&m, OPP5, NOPP, 4, &j, POL_BEST);
    plan_t p0 = eval_opp(&m, OPP5, NOPP, 0, &j, POL_BEST);
    plan_t p1 = eval_opp(&m, OPP5, NOPP, 1, &j, POL_BEST);
    CHECK(r_idle.feasible && r_idle.rest == REST_IDLE && near(r_idle.e_run, 0.3117255222, 1e-9));
    CHECK(near(r_idle.slack, 3.444444444, 1e-9) && near(r_idle.e_total, 0.6944237601, 1e-9));
    CHECK(r_best.rest == REST_SLEEP && near(r_best.e_rest, 0.007384888889, 1e-9) &&
          near(r_best.e_total, 0.3191104111, 1e-9));
    CHECK(p0.rest == REST_SLEEP && near(p0.e_total, 0.2133771636, 1e-9));
    CHECK(p1.rest == REST_SLEEP && near(p1.e_total, 0.2042103962, 1e-9));
    /* 손익분기 바로 위/아래: slack = t_be·(1 ± 1%) 가 되도록 마감을 맞춘다 */
    job_t jb = j;
    jb.deadline = run_time(&j, 1800e6) + 1.01 * sleep_breakeven(&m, p_idle4);
    CHECK(eval_opp(&m, OPP5, NOPP, 4, &jb, POL_BEST).rest == REST_SLEEP);
    jb.deadline = run_time(&j, 1800e6) + 0.99 * sleep_breakeven(&m, p_idle4);
    CHECK(eval_opp(&m, OPP5, NOPP, 4, &jb, POL_BEST).rest == REST_IDLE);
    /* slack < t_trans → sleep 불가, 마감 초과 → infeasible, slack 0 → none */
    jb.deadline = run_time(&j, 1800e6) + 0.001;
    CHECK(eval_opp(&cheap, OPP5, NOPP, 4, &jb, POL_BEST).rest == REST_IDLE);
    jb.deadline = 0.5;
    CHECK(!eval_opp(&m, OPP5, NOPP, 0, &jb, POL_BEST).feasible && !eval_opp(&m, OPP5, NOPP, 4, &jb, POL_BEST).feasible);
    jb.deadline = run_time(&j, 1800e6);
    plan_t z = eval_opp(&m, OPP5, NOPP, 4, &jb, POL_BEST);
    CHECK(z.feasible && z.rest == REST_NONE && z.e_rest == 0.0);
    /* idle 전압을 내리면 race-to-idle 휴식 에너지가 OPP0 누설 수준으로 */
    pmodel_t dv = m; dv.idle_drops_v = true;
    plan_t r_dv = eval_opp(&dv, OPP5, NOPP, 4, &j, POL_IDLE_ONLY);
    CHECK(near(r_dv.e_rest, (p_leak(&m, 0.6) + m.p_static) * r_dv.slack, 1e-12) && r_dv.e_rest < r_idle.e_rest);

    printf("  OPP별 (N=1e9 cycle, m=0, D=4 s, 휴식 = 싼 쪽):\n");
    printf("   OPP   f[MHz]  V     P_dyn[W]  P_leak[W] P_on[W]   t_run[s]  E_run[J]  E/cyc[nJ] slack[s] rest  E_rest[J] E_total[J]\n");
    for (int i = 0; i < NOPP; i++) {
        plan_t p = eval_opp(&m, OPP5, NOPP, i, &j, POL_BEST);
        double pd = p_dyn(&m, OPP5[i].v, OPP5[i].f_hz), pl = p_leak(&m, OPP5[i].v);
        printf("   %d   %6.0f   %.2f  %.5f   %.5f   %.5f   %.4f    %.5f   %.4f    %.4f   %-5s %.5f   %.5f\n", i,
               OPP5[i].f_hz / 1e6, OPP5[i].v, pd, pl, pd + pl + m.p_static, p.t_run, p.e_run,
               e_per_cycle(&m, OPP5[i].v, OPP5[i].f_hz) * 1e9, p.slack, REST_NAME[p.rest], p.e_rest, p.e_total);
    }
    printf("   idle 전력 @1.00 V = %.5f W, sleep 손익분기 = %.3f ms (t_trans %.1f ms)\n", p_idle4,
           sleep_breakeven(&m, p_idle4) * 1e3, m.t_trans * 1e3);
}

static void part_b(void) {
    printf("== Part B: 언제 무엇이 이기나 ==\n");
    pmodel_t m = model_default();
    job_t j = job_default();
    strategy_table("기준 (Tj 25 °C, P_static 20 mW)", &m, &j);
    plan_t ri = plan_strategy(&m, OPP5, NOPP, &j, S_RACE_IDLE), rs = plan_strategy(&m, OPP5, NOPP, &j, S_RACE_SLEEP);
    plan_t pa = plan_strategy(&m, OPP5, NOPP, &j, S_PACE), op = plan_strategy(&m, OPP5, NOPP, &j, S_OPT);
    CHECK(ri.opp == 4 && rs.opp == 4 && pa.opp == 0 && op.opp == 1);
    CHECK(op.e_total <= pa.e_total && op.e_total <= rs.e_total && rs.e_total < ri.e_total);
    CHECK(critical_opp(&m, OPP5, NOPP) == 1);
    CHECK(near(e_per_cycle(&m, 0.7, 600e6), 1.990477295e-10, 1e-9));

    /* critical speed 는 누설·정적 전력 때문에 생긴다 */
    pmodel_t ideal = m; ideal.leak_scale = 0; ideal.p_static = 0; ideal.p_sleep = 0; ideal.e_trans = 0;
    pmodel_t st_only = m; st_only.leak_scale = 0;
    pmodel_t lk_only = m; lk_only.p_static = 0;
    pmodel_t hot = m; hot.temp_c = 85;
    CHECK(critical_opp(&ideal, OPP5, NOPP) == 0 && plan_strategy(&ideal, OPP5, NOPP, &j, S_OPT).opp == 0);
    CHECK(critical_opp(&st_only, OPP5, NOPP) == 1 && critical_opp(&lk_only, OPP5, NOPP) == 0);   /* 25 °C 누설만으론 부족 */
    CHECK(critical_opp(&hot, OPP5, NOPP) == 2);
    pmodel_t flat = m; flat.k_v = 0; flat.leak_scale = 16;   /* 누설이 V 에 비례만 (지수 없음) + 큼 → race 가 최적 */
    CHECK(critical_opp(&flat, OPP5, NOPP) == NOPP - 1 && plan_strategy(&flat, OPP5, NOPP, &j, S_OPT).opp == NOPP - 1);
    bool mono = true;   /* 누설·정적·슬립 0 이면 낮은 OPP 일수록 E_total 이 작거나 같다 */
    for (int i = 1; i < NOPP; i++)
        mono = mono && eval_opp(&ideal, OPP5, NOPP, i - 1, &j, POL_BEST).e_total <=
                       eval_opp(&ideal, OPP5, NOPP, i, &j, POL_BEST).e_total;
    CHECK(mono);
    printf("  E/cycle [nJ]         300     600    1000    1400    1800 MHz   critical\n");
    const char *nm[6] = {"leak=0 static=0", "static only", "leak only", "base 25C", "base 85C", "k_v=0 leak x16"};
    const pmodel_t *ms[6] = {&ideal, &st_only, &lk_only, &m, &hot, &flat};
    for (int k = 0; k < 6; k++) {
        printf("  %-16s", nm[k]);
        for (int i = 0; i < NOPP; i++) printf(" %7.4f", e_per_cycle(ms[k], OPP5[i].v, OPP5[i].f_hz) * 1e9);
        int c = critical_opp(ms[k], OPP5, NOPP);
        printf("   OPP%d %4.0f MHz\n", c, c >= 0 ? OPP5[c].f_hz / 1e6 : 0.0);
    }

    /* 마감이 빡빡하면 선택지가 없다 / 불가능하면 없다 */
    job_t tight = j; tight.deadline = 0.6;
    CHECK(plan_strategy(&m, OPP5, NOPP, &tight, S_PACE).opp == 4 && plan_strategy(&m, OPP5, NOPP, &tight, S_OPT).opp == 4);
    tight.deadline = 0.5;
    CHECK(plan_strategy(&m, OPP5, NOPP, &tight, S_PACE).opp == -1 && !plan_strategy(&m, OPP5, NOPP, &tight, S_OPT).feasible &&
          !plan_strategy(&m, OPP5, NOPP, &tight, S_RACE_SLEEP).feasible);

    /* 완전 메모리 바운드: 시간이 f 와 무관 → 어떤 누설 배율에서도 최저 OPP */
    bool memlow = true;
    for (double ls = 0.1; ls < 1001; ls *= 10) {
        pmodel_t mm = m; mm.leak_scale = ls;
        job_t jm = j; jm.mem_frac = 1.0; jm.deadline = 1.0;
        memlow = memlow && plan_strategy(&mm, OPP5, NOPP, &jm, S_OPT).opp == 0;
    }
    CHECK(memlow);

    /* 스윕 1: 누설 배율 (= 공정 코너·온도) */
    printf("  -- 스윕: 누설 배율 (기준 모델) --\n");
    printf("   scale  leak%%@1.8GHz  race-idle  race-sleep   pace     optimal(OPP)  승자(race-sleep vs pace)\n");
    double scales[] = {0, 0.5, 1, 2, 4, 8, 16, 32, 64};
    for (int k = 0; k < 9; k++) {
        pmodel_t mm = m; mm.leak_scale = scales[k];
        double pl = p_leak(&mm, 1.0), pt = pl + p_dyn(&mm, 1.0, 1800e6) + mm.p_static;
        plan_t o = plan_strategy(&mm, OPP5, NOPP, &j, S_OPT);
        double a = e_strat(&mm, &j, S_RACE_SLEEP), b = e_strat(&mm, &j, S_PACE);
        printf("   %5.1f   %5.1f%%     %8.4f   %8.4f   %8.4f   %8.4f(%d)   %s\n", scales[k], 100 * pl / pt,
               e_strat(&mm, &j, S_RACE_IDLE), a, b, o.e_total, o.opp, a < b ? "race" : "pace");
    }
    double x_leak = crossover(m, j, set_leak, 1, 1000, S_RACE_SLEEP, S_PACE, true);
    double x_temp = crossover(m, j, set_temp, 25, 150, S_RACE_SLEEP, S_PACE, false);
    double x_stat = crossover(m, j, set_pstatic, 0.001, 1.0, S_RACE_SLEEP, S_PACE, true);
    double x_opt_stat = NAN;   /* optimal 이 최고 OPP 가 되는 P_static */
    {
        double lo = 0.001, hi = 2.0;
        for (int it = 0; it < 100; it++) {
            double mid = sqrt(lo * hi);
            pmodel_t mm = m; mm.p_static = mid;
            if (plan_strategy(&mm, OPP5, NOPP, &j, S_OPT).opp == NOPP - 1) hi = mid; else lo = mid;
        }
        x_opt_stat = hi;
    }
    pmodel_t ph = model_platform_heavy();
    double x_psl = crossover(ph, j, set_psleep, 1e-4, 0.1, S_RACE_SLEEP, S_PACE, true);
    job_t jsmall = j; jsmall.n_cycles = 1e7; jsmall.deadline = 0.04;     /* 10 M cycle, 40 ms 마다 (프레임 같은 주기 작업) */
    double x_etr = crossover(ph, jsmall, set_etrans, 1e-6, 0.1, S_RACE_SLEEP, S_PACE, true);
    double x_mem = crossover(ph, j, set_memfrac, 0.0, 1.0, S_RACE_SLEEP, S_PACE, false);
    printf("  -- crossover (race-to-sleep == pace) --\n");
    printf("   누설 배율 (기준)                 : %.3f ×\n", x_leak);
    printf("   Tj (기준)                        : %.2f °C\n", x_temp);
    printf("   P_static (기준)                  : %.2f mW   (optimal 이 최고 OPP 가 되는 P_static: %.1f mW)\n",
           x_stat * 1e3, x_opt_stat * 1e3);
    printf("   P_sleep (P_static 100 mW)        : %.2f mW\n", x_psl * 1e3);
    printf("   e_trans (P_static 100 mW, N=1e7, D=40 ms) : %.3f mJ\n", x_etr * 1e3);
    printf("   mem_frac (P_static 100 mW)       : %.4f\n", x_mem);
    CHECK(x_leak > 10 && x_leak < 40 && near(x_temp, 25 + 25 * log(x_leak), 1e-6));   /* 온도 = 누설 배율의 로그 */
    CHECK(x_stat > 0.04 && x_stat < 0.08 && x_opt_stat > x_stat);
    CHECK(isfinite(x_psl) && isfinite(x_etr) && isfinite(x_mem) && x_mem > 0 && x_mem < 1);
    {   /* crossover 양쪽에서 승자가 실제로 바뀌는지 */
        pmodel_t a = ph, b = ph; a.p_sleep = x_psl * 0.9; b.p_sleep = x_psl * 1.1;
        CHECK(e_strat(&a, &j, S_RACE_SLEEP) < e_strat(&a, &j, S_PACE) && e_strat(&b, &j, S_RACE_SLEEP) > e_strat(&b, &j, S_PACE));
        job_t c = j, d = j; c.mem_frac = x_mem - 0.02; d.mem_frac = x_mem + 0.02;
        CHECK(e_strat(&ph, &c, S_RACE_SLEEP) < e_strat(&ph, &c, S_PACE) && e_strat(&ph, &d, S_RACE_SLEEP) > e_strat(&ph, &d, S_PACE));
    }
    strategy_table("P_static 100 mW (플랫폼 전력 큼)", &ph, &j);
    printf("  -- 스윕: mem_frac (P_static 100 mW) --\n");
    printf("   m     race-sleep  pace(OPP)     optimal(OPP)\n");
    for (int k = 0; k <= 4; k++) {
        job_t jm = j; jm.mem_frac = 0.25 * k;
        plan_t pp = plan_strategy(&ph, OPP5, NOPP, &jm, S_PACE), o = plan_strategy(&ph, OPP5, NOPP, &jm, S_OPT);
        printf("   %.2f  %8.4f    %8.4f(%d)   %8.4f(%d)\n", jm.mem_frac, e_strat(&ph, &jm, S_RACE_SLEEP), pp.e_total,
               pp.opp, o.e_total, o.opp);
    }
}

static void part_c(void) {
    printf("== Part C: 듀티 사이클 센서 노드 배터리 수명 ==\n");
    CHECK(near(batt_energy_j(220, 3.0), 2376.0, 1e-12));
    CHECK(near(batt_energy_j(1000, 3.7), 13320.0, 1e-12));
    /* 손계산 1: 슬립 10 µA 만, 효율 1, 손실 0 → 220 mAh / 10 µA = 22000 h */
    node_t s;
    memset(&s, 0, sizeof s);
    s.period_s = 1.0; s.v_load = 3.0; s.mcu_f_hz = 1e6; s.sleep_i_a = 10e-6;
    s.eta_active = 1.0; s.eta_sleep = 1.0; s.batt_mah = 220; s.batt_v = 3.0; s.derate = 1.0;
    CHECK(near(node_life_days(&s), 22000.0 / 24.0, 1e-9));
    /* 손계산 2: 기준 노드 (풀이 표) */
    node_t nd = node_default();
    node_split_t sp = node_split(&nd);
    CHECK(sp.ok && near(sp.t_awake, 0.015625, 1e-12));
    CHECK(near(sp.p[T_MCU] * nd.period_s * nd.eta_active, 1.8 * 3.3e-3 * 3.125e-3, 1e-12));   /* 18.5625 µJ */
    CHECK(near(sp.p[T_TX], 1.8 * 6e-3 * 1.5e-3 / 0.85 / 10.0, 1e-12));
    CHECK(near(sp.p[T_SLEEP], 1.8 * 2e-6 * (10.0 - 0.015625) / 10.0 / 0.70, 1e-12));
    CHECK(near(sp.p[T_IQ], 1.5e-6, 1e-12) && near(sp.p[T_SELFDIS], 23.76 / (365.0 * 86400.0), 1e-12));
    CHECK(near(sp.p_total, 1.465442256e-05, 1e-9));
    CHECK(near(node_life_days(&nd), 1501.253285, 1e-9));
    node_t bad = nd; bad.period_s = 0.01;                                   /* 깨어 있는 시간 15.6 ms > 주기 */
    CHECK(isnan(node_life_days(&bad)) && !node_split(&bad).ok);

    printf("  기준 노드 (주기 %.0f s, 깨어 있는 시간 %.3f ms / 주기)\n", nd.period_s, sp.t_awake * 1e3);
    printf("   항목              배터리쪽 평균[µW]  비중    절반이면 수명[일]  Δ\n");
    double life = node_life_days(&nd), share_sum = 0;
    int dom = 0;
    for (int t = 0; t < NTERM; t++) {
        double sh = sp.p[t] / sp.p_total;
        share_sum += sh;
        if (sp.p[t] > sp.p[dom]) dom = t;
        double l2 = sp.e_usable / (sp.p_total - 0.5 * sp.p[t]) / 86400.0;
        printf("   %-15s   %9.3f        %5.1f%%    %8.1f        %+5.1f%%\n", TERM_NAME[t], sp.p[t] * 1e6, 100 * sh, l2,
               100 * (l2 / life - 1));
    }
    printf("   합계              %9.3f        → 수명 %.1f 일 (%.2f 년), 사용 가능 에너지 %.1f J\n", sp.p_total * 1e6, life,
           life / 365.0, sp.e_usable);
    CHECK(near(share_sum, 1.0, 1e-12) && dom == T_SLEEP);

    printf("  -- 주기 스윕: 무엇이 지배하나 --\n");
    printf("   주기[s]  활성합[µW]  슬립+Iq+자가[µW]  수명[일]   최대 항목\n");
    double periods[] = {0.1, 1, 10, 60, 600, 3600};
    int dom_fast = -1, dom_slow = -1;
    for (int k = 0; k < 6; k++) {
        node_t n2 = nd; n2.period_s = periods[k];
        node_split_t s2 = node_split(&n2);
        double act = s2.p[T_MCU] + s2.p[T_SENSOR] + s2.p[T_TX] + s2.p[T_RX];
        int d = 0;
        for (int t = 1; t < NTERM; t++) if (s2.p[t] > s2.p[d]) d = t;
        if (k == 0) dom_fast = d;
        if (k == 5) dom_slow = d;
        printf("   %7.1f  %9.3f   %9.3f          %8.1f   %s\n", periods[k], act * 1e6, (s2.p_total - act) * 1e6,
               node_life_days(&n2), TERM_NAME[d]);
    }
    CHECK(dom_fast == T_MCU && dom_slow == T_SLEEP);
    /* 주기가 무한대로 가면 수명은 '고정 손실'만으로 정해지는 상한에 수렴 */
    node_t inf = nd; inf.period_s = 1e12;
    double cap = node_split(&inf).e_usable /
                 ((nd.v_load * nd.sleep_i_a / nd.eta_sleep + nd.iq_a * nd.batt_v + sp.p[T_SELFDIS]) * 86400.0);
    CHECK(near(node_life_days(&inf), cap, 1e-6));
    printf("   주기 → ∞ 상한: %.1f 일 (%.2f 년) — 슬립·Iq·자가방전만으로 정해진다\n", cap, cap / 365.0);
}

int main(void) {
    part_a();
    part_b();
    part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
