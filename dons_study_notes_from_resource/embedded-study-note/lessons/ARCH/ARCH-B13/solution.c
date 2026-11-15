/* ARCH-B13 타이밍 기초: setup/hold와 최대 클럭 주파수
 *
 * 단위: 모든 시간은 정수 피코초(ps), 주파수는 정수 kHz. (1 / 1 ps = 1e9 kHz)
 * 스큐 부호: skew = capture 플롭의 클럭 도착 시각 − launch 플롭의 클럭 도착 시각.
 *   skew > 0  → capture 클럭이 늦게 온다 → setup 에 유리, hold 에 불리.
 *
 * setup slack = Tclk + skew − (tpcq + tpd + tsu)
 * hold  slack = (tccq + tcd) − th − skew          ← Tclk 가 없다!
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    int32_t tpcq;  /* clk→Q 최대 (propagation)      */
    int32_t tccq;  /* clk→Q 최소 (contamination)    */
    int32_t tpd;   /* 조합 논리 최대 지연 (긴 경로) */
    int32_t tcd;   /* 조합 논리 최소 지연 (짧은 경로) */
    int32_t tsu;   /* capture 플롭 setup time       */
    int32_t th;    /* capture 플롭 hold time        */
    int32_t skew;  /* capture 도착 − launch 도착    */
} ff_path;

#define PS_PER_KHZ_INV 1000000000u /* f[kHz] = 1e9 / T[ps] */

/* ---------------- Part A: slack 과 Fmax ---------------- */
static int32_t setup_slack_ps(const ff_path *p, int32_t tclk_ps) {
    return tclk_ps + p->skew - (p->tpcq + p->tpd + p->tsu);
}
static int32_t hold_slack_ps(const ff_path *p) {
    return p->tccq + p->tcd - p->th - p->skew;
}
static int32_t min_period_ps(const ff_path *p) {
    return p->tpcq + p->tpd + p->tsu - p->skew;
}
/* 이 경로가 허용하는 최대 주파수(kHz, 내림). min period <= 0 이면 0 = "이 경로는 주파수를 제한하지 않음". */
static uint32_t fmax_khz(const ff_path *p) {
    int32_t t = min_period_ps(p);
    if (t <= 0) return 0;
    return PS_PER_KHZ_INV / (uint32_t)t;
}

/* ---------------- Part B: 경계·함정 ---------------- */
/* kHz → 주기(ps). setup 검사용이므로 비관적으로 "내림" (실제 주기보다 짧게). 0 kHz 는 0. */
static int32_t period_ps_from_khz(uint32_t khz) {
    if (khz == 0) return 0;
    return (int32_t)(PS_PER_KHZ_INV / khz);
}
static bool path_ok(const ff_path *p, int32_t tclk_ps) {
    return setup_slack_ps(p, tclk_ps) >= 0 && hold_slack_ps(p) >= 0;
}
/* 주어진 Tclk 에서 setup·hold 를 모두 만족하는 skew 범위 [lo, hi]. 비면 false. */
static bool skew_window(const ff_path *p, int32_t tclk_ps, int32_t *lo, int32_t *hi) {
    *lo = p->tpcq + p->tpd + p->tsu - tclk_ps;  /* setup: skew >= lo */
    *hi = p->tccq + p->tcd - p->th;             /* hold : skew <= hi */
    return *lo <= *hi;
}
/* hold 를 고치려면 지연 버퍼(buf_ps 짜리)를 몇 개 넣어야 하나. 이미 OK면 0, buf_ps <= 0 이면 -1. */
static int hold_fix_buffers(const ff_path *p, int32_t buf_ps) {
    if (buf_ps <= 0) return -1;
    int32_t hs = hold_slack_ps(p);
    if (hs >= 0) return 0;
    return (int)((-hs + buf_ps - 1) / buf_ps);
}
/* 같은 배선에 버퍼 k개 → 짧은 경로와 긴 경로가 둘 다 k*buf 만큼 늘어난다 (주어진 헬퍼) */
static ff_path add_buffers(ff_path p, int k, int32_t buf_ps) {
    p.tpd += k * buf_ps;
    p.tcd += k * buf_ps;
    return p;
}

/* ---------------- Part C: 여러 경로 타이밍 리포트 ---------------- */
typedef struct { int32_t tpcq, tccq, tsu, th; } ff_lib;            /* 플롭 셀 하나로 통일 */
typedef struct { const char *name; int32_t clk_arrival_ps; } reg_t; /* 클럭 트리 삽입 지연 */
typedef struct { const char *name; int launch, capture; int32_t tpd, tcd; } net_path;

typedef struct {
    int     worst_setup_idx;  int32_t wns_setup; int64_t tns_setup; int n_setup_viol;
    int     worst_hold_idx;   int32_t wns_hold;  int64_t tns_hold;  int n_hold_viol;
    int32_t min_period_ps;    uint32_t fmax_khz; /* 설계 전체: 가장 느린(critical) 경로 기준 */
} timing_report;

static ff_path make_path(const ff_lib *lib, const reg_t *regs, const net_path *np) {
    ff_path p;
    p.tpcq = lib->tpcq; p.tccq = lib->tccq; p.tsu = lib->tsu; p.th = lib->th;
    p.tpd = np->tpd;    p.tcd = np->tcd;
    p.skew = regs[np->capture].clk_arrival_ps - regs[np->launch].clk_arrival_ps;
    return p;
}

/* WNS = 가장 나쁜(작은) slack (양수일 수도 있다), TNS = 음수 slack 의 합 (<= 0). n >= 1. */
static void analyze(const ff_lib *lib, const reg_t *regs, const net_path *paths, int n,
                    int32_t tclk_ps, timing_report *r) {
    r->worst_setup_idx = r->worst_hold_idx = -1;
    r->wns_setup = r->wns_hold = INT32_MAX;
    r->tns_setup = r->tns_hold = 0;
    r->n_setup_viol = r->n_hold_viol = 0;
    r->min_period_ps = INT32_MIN;
    for (int i = 0; i < n; i++) {
        ff_path p = make_path(lib, regs, &paths[i]);
        int32_t s = setup_slack_ps(&p, tclk_ps), h = hold_slack_ps(&p), t = min_period_ps(&p);
        if (s < r->wns_setup) { r->wns_setup = s; r->worst_setup_idx = i; }
        if (h < r->wns_hold)  { r->wns_hold = h;  r->worst_hold_idx = i; }
        if (s < 0) { r->tns_setup += s; r->n_setup_viol++; }
        if (h < 0) { r->tns_hold += h;  r->n_hold_viol++; }
        if (t > r->min_period_ps) r->min_period_ps = t;
    }
    r->fmax_khz = r->min_period_ps > 0 ? PS_PER_KHZ_INV / (uint32_t)r->min_period_ps : 0;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng_state = 0x13B13u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
static int32_t rnd(int32_t lo, int32_t hi) { return lo + (int32_t)(xorshift32() % (uint32_t)(hi - lo + 1)); }

int main(void) {
    /* 기준 경로 P: tpcq 100, tccq 60, tpd 1500, tcd 200, tsu 80, th 50, skew 0 */
    const ff_path P = { 100, 60, 1500, 200, 80, 50, 0 };

    /* Part A */
    CHECK(setup_slack_ps(&P, 2000) == 320);
    CHECK(hold_slack_ps(&P) == 210);
    CHECK(min_period_ps(&P) == 1680);
    CHECK(fmax_khz(&P) == 595238u);                  /* 595.238 MHz */
    CHECK(setup_slack_ps(&P, 1680) == 0);
    CHECK(setup_slack_ps(&P, 1600) == -80);
    {   ff_path q = P; q.skew = 150;                 /* capture 클럭 150 ps 늦게 */
        CHECK(setup_slack_ps(&q, 2000) == 470);      /* setup 은 +150 */
        CHECK(hold_slack_ps(&q) == 60);              /* hold 는 −150 */
        CHECK(fmax_khz(&q) == 653594u);              /* 1e9 / 1530 */
        q.skew = 250;
        CHECK(hold_slack_ps(&q) == -40);             /* 너무 많으면 hold 위반 */
        q.skew = -100;
        CHECK(setup_slack_ps(&q, 2000) == 220 && hold_slack_ps(&q) == 310);
    }

    /* Part B */
    CHECK(period_ps_from_khz(595238u) == 1680);      /* 1680.0003 → 내림 */
    CHECK(period_ps_from_khz(595239u) == 1679);      /* 1679.997  → 내림 */
    CHECK(setup_slack_ps(&P, period_ps_from_khz(fmax_khz(&P))) >= 0);
    CHECK(setup_slack_ps(&P, period_ps_from_khz(fmax_khz(&P) + 1u)) < 0); /* Fmax 를 올림하면 위반 */
    CHECK((uint64_t)fmax_khz(&P) * 1680u <= 1000000000u && (uint64_t)(fmax_khz(&P) + 1u) * 1680u > 1000000000u);
    CHECK(period_ps_from_khz(500000u) == 2000 && period_ps_from_khz(0) == 0);
    {   int32_t lo = 0, hi = 0;
        CHECK(skew_window(&P, 2000, &lo, &hi) && lo == -320 && hi == 210);
        CHECK(!skew_window(&P, 1400, &lo, &hi));     /* 1400 ps 에선 어떤 skew 로도 안 된다 */
    }
    {   /* 시프트 레지스터: 플롭 → 플롭 직결 (tpd=tcd=0). hold 는 tccq − th − skew 뿐 */
        ff_path sr = { 100, 60, 0, 0, 80, 50, 30 };
        CHECK(hold_slack_ps(&sr) == -20);
        bool any_ok = false;                         /* 주파수를 아무리 낮춰도 못 고친다 */
        for (int32_t t = 200; t <= 1000000; t *= 2) any_ok |= path_ok(&sr, t);
        CHECK(!any_ok);
        CHECK(hold_fix_buffers(&sr, 15) == 2);       /* ceil(20/15) */
        ff_path fixed = add_buffers(sr, 2, 15);
        CHECK(hold_slack_ps(&fixed) == 10 && path_ok(&fixed, 1000));
        CHECK(hold_fix_buffers(&P, 15) == 0 && hold_fix_buffers(&sr, 0) == -1);
    }
    {   /* 짧은 경로 = 긴 경로인 배선에 버퍼를 넣으면 setup 이 깨질 수 있다 */
        ff_path tight = { 100, 60, 300, 300, 80, 50, 400 };     /* hold −90, Tmin 80 */
        int k = hold_fix_buffers(&tight, 40);
        ff_path f = add_buffers(tight, k, 40);
        CHECK(k == 3 && hold_slack_ps(&f) == 30);
        CHECK(setup_slack_ps(&tight, 100) == 20 && setup_slack_ps(&f, 100) == -100);
    }
    {   /* skew 스윕: setup 은 정확히 +Δ, hold 는 정확히 −Δ */
        ff_path q = P; int mono = 1;
        for (int32_t s = -400; s < 400; s += 10) {
            ff_path a = q, b = q; a.skew = s; b.skew = s + 10;
            mono &= setup_slack_ps(&b, 2000) - setup_slack_ps(&a, 2000) == 10;
            mono &= hold_slack_ps(&b) - hold_slack_ps(&a) == -10;
        }
        CHECK(mono);
    }
    {   /* 결정적 랜덤 경로 2000개: 항등식 + window 와 path_ok 의 일치 */
        int ok = 1;
        for (int i = 0; i < 2000; i++) {
            ff_path r;
            r.tccq = rnd(20, 80);  r.tpcq = r.tccq + rnd(0, 60);
            r.tcd  = rnd(0, 300);  r.tpd  = r.tcd + rnd(0, 2500);
            r.tsu  = rnd(20, 120); r.th   = rnd(-20, 80);
            r.skew = rnd(-300, 300);
            int32_t tclk = rnd(500, 4000), lo, hi;
            ok &= setup_slack_ps(&r, min_period_ps(&r)) == 0;
            ok &= hold_slack_ps(&r) == r.tccq + r.tcd - r.th - r.skew;
            bool w = skew_window(&r, tclk, &lo, &hi);
            ok &= path_ok(&r, tclk) == (w && lo <= r.skew && r.skew <= hi);
            int32_t t = min_period_ps(&r);
            if (t > 0) {
                uint32_t f = fmax_khz(&r);
                ok &= (uint64_t)f * (uint32_t)t <= PS_PER_KHZ_INV && (uint64_t)(f + 1u) * (uint32_t)t > PS_PER_KHZ_INV;
                ok &= setup_slack_ps(&r, period_ps_from_khz(f)) >= 0;
                ok &= setup_slack_ps(&r, period_ps_from_khz(f + 1u)) < 0;
            }
        }
        CHECK(ok);
    }
    {   ff_path big = P; big.skew = 2000;            /* 비현실적 skew → Tmin <= 0 */
        CHECK(min_period_ps(&big) == -320 && fmax_khz(&big) == 0u);
    }

    /* Part C: 작은 데이터패스 + 클럭 트리 도착 시각 */
    const ff_lib lib = { 90, 40, 60, 30 };
    const reg_t regs[] = {
        { "pc", 400 }, { "ir", 420 }, { "opa", 450 }, { "acc", 600 },
        { "out", 430 }, { "sh0", 500 }, { "sh1", 580 },
    };
    const net_path paths[] = {
        { "pc->ir   imem decode",  0, 1, 1100, 200 },
        { "ir->opa  regfile read", 1, 2, 1300, 150 },
        { "opa->acc 32b adder",    2, 3, 1900, 160 },
        { "acc->opa forward mux",  3, 2, 1500, 120 },
        { "acc->out output reg",   3, 4,  300,  60 },
        { "sh0->sh1 shift wire",   5, 6,   20,  10 },
    };
    const int NP = (int)(sizeof paths / sizeof paths[0]);
    {   ff_path p2 = make_path(&lib, regs, &paths[2]), p3 = make_path(&lib, regs, &paths[3]);
        CHECK(p2.skew == 150 && p3.skew == -150);    /* 같은 두 레지스터, 방향 따라 부호 반대 */
        CHECK(min_period_ps(&p2) == 1900 && min_period_ps(&p3) == 1800);
    }
    timing_report r;
    analyze(&lib, regs, paths, NP, 2000, &r);        /* 500 MHz */
    CHECK(r.worst_setup_idx == 2 && r.wns_setup == 100 && r.tns_setup == 0 && r.n_setup_viol == 0);
    CHECK(r.worst_hold_idx == 5 && r.wns_hold == -60 && r.tns_hold == -60 && r.n_hold_viol == 1);
    CHECK(r.min_period_ps == 1900 && r.fmax_khz == 526315u);
    analyze(&lib, regs, paths, NP, 1850, &r);
    CHECK(r.wns_setup == -50 && r.tns_setup == -50 && r.n_setup_viol == 1);
    analyze(&lib, regs, paths, NP, 1700, &r);
    CHECK(r.wns_setup == -200 && r.tns_setup == -300 && r.n_setup_viol == 2);
    CHECK(r.n_hold_viol == 1 && r.wns_hold == -60);  /* hold 는 Tclk 와 무관 */
    {   ff_path p5 = make_path(&lib, regs, &paths[5]);
        int k = hold_fix_buffers(&p5, 25);
        ff_path f = add_buffers(p5, k, 25);
        CHECK(k == 3 && hold_slack_ps(&f) == 15 && setup_slack_ps(&f, 2000) >= 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
