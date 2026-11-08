/* SIV-M06 SerDes PRBS 생성·검사와 BER 신뢰구간
 *
 * 고속 링크(SerDes) 검증의 기본 측정: TX 가 PRBS 를 보내고 RX 쪽 checker 가 같은 PRBS 를 예측해 비트 오류를 센다.
 *   Part A  PRBS7/15/31 generator (Fibonacci LFSR) + self-synchronizing checker (HUNT → LOCKED)
 *   Part B  오류 증식(error multiplication), bit slip → 동기 상실 검출·재동기, 극성 반전, BER 신뢰구간(Poisson)
 *   Part C  6-lane 회귀 밤샘 시나리오: 결함을 심은 lane 들을 판정하고 디버그 엔지니어용 한 줄 리포트 + 측정 시간 계산
 *
 * 결정성: 오류 주입은 고정 시드 xorshift64. 실제 시간 측정은 demo_prbs31_period.c / demo_ber_mc.c 에만.
 * 비트 순서: 상태 s 의 bit0 = 가장 최근 비트, bit(n-1) = n 비트 전. 다항식 x^n + x^k + 1 → s[t] = s[t-n] ^ s[t-k].
 */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 결정적 난수 (오류 주입용) ---------------- */
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static void rng_seed(uint64_t s) { g_rng = s ? s : 0x9E3779B97F4A7C15ull; }
static uint64_t xs64(void) { uint64_t x = g_rng; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return g_rng = x; }
static double urand(void) { return (double)(xs64() >> 11) * 0x1.0p-53; }      /* [0,1) */

/* ---------------- PRBS 다항식 ---------------- */
typedef struct { const char *name; int n, k; } prbs_poly_t;        /* x^n + x^k + 1 */
static const prbs_poly_t PRBS7  = { "PRBS7",  7,  6 };
static const prbs_poly_t PRBS15 = { "PRBS15", 15, 14 };
static const prbs_poly_t PRBS31 = { "PRBS31", 31, 28 };
static uint32_t poly_mask(const prbs_poly_t *p) { return (uint32_t)((1ull << p->n) - 1u); }

/* ================= Part A: generator ================= */
/* 탭 n, k 의 XOR = 다음 비트 */
static int prbs_feedback(const prbs_poly_t *p, uint32_t s) {
    (void)p; (void)s;
    return 0; /* TODO: 탭 n 과 k 의 비트 (s 의 bit n-1, bit k-1) XOR */
}

typedef struct { const prbs_poly_t *p; uint32_t s; } prbs_gen_t;
static void prbs_seed(prbs_gen_t *g, const prbs_poly_t *p, uint32_t seed) {
    g->p = p;
    g->s = seed & poly_mask(p);
    if (g->s == 0) g->s = poly_mask(p);       /* 전부 0 = lock-up 상태 (영원히 0 만 나온다) */
}
/* 한 비트 내보내고 상태를 민다: s = (s << 1 | b) & mask */
static int prbs_next(prbs_gen_t *g) {
    (void)g; (void)prbs_feedback;
    return 0; /* TODO: b = feedback → s = (s << 1 | b) & mask → b 반환 */
}

/* ================= Part A: self-synchronizing checker ================= */
#define LOCK_RUN 32    /* HUNT: 연속 정답 예측 32번 → LOCKED (오탐 확률 ~2^-32)     */
#define LOSS_WIN 64    /* LOCKED: 64비트 창                                         */
#define LOSS_THR 16    /* 창 안 오류 16개(25%) 이상 → 동기 상실 (bit slip, 신호 끊김) */
#define NONE UINT64_MAX

typedef enum { CHK_HUNT, CHK_LOCKED } chk_mode_t;
typedef struct {
    const prbs_poly_t *p;
    bool invert;                 /* RX 극성 반전 설정 (P/N swap 보정)                   */
    chk_mode_t mode;
    uint32_t s; int fill, run;   /* HUNT: 받은 비트로 채운 상태, 채운 수, 연속 정답 수    */
    uint32_t win_bits, win_err;  /* LOCKED: 동기 상실 검출 창                            */
    uint64_t rx_bits;            /* 들어온 전체 비트 수                                  */
    uint64_t hunt_pred, hunt_miss;   /* HUNT 중 예측 / 틀림 → 극성·패턴 진단           */
    uint64_t bits, errors;       /* LOCKED 동안 비교한 비트 / 오류 (BER 분모·분자)       */
    uint64_t first_err_at;       /* 첫 오류의 rx 비트 위치 (0-based), 없으면 NONE         */
    uint64_t lock_at;            /* 마지막으로 LOCKED 된 시점의 rx_bits                   */
    uint32_t locks, sync_loss;
    uint64_t first_loss_at;
} prbs_chk_t;

static void chk_init(prbs_chk_t *c, const prbs_poly_t *p, bool invert) {
    memset(c, 0, sizeof *c);
    c->p = p; c->invert = invert; c->mode = CHK_HUNT;
    c->first_err_at = NONE; c->first_loss_at = NONE; c->lock_at = NONE;
}

/* HUNT: 처음 n 비트는 상태에 그대로 싣고, 그 뒤로는 예측(feedback) == 받은 비트 인지 본다.
 * 틀려도 받은 비트를 상태에 싣는다(self-sync). LOCK_RUN 연속 정답이면 LOCKED. */
static void chk_hunt(prbs_chk_t *c, int rx) {
    (void)c; (void)rx;
    /* TODO: fill < n 이면 rx 를 상태에 싣기만. 아니면 pred = feedback(s), hunt_pred++,
     *       맞으면 run++ (LOCK_RUN 도달 → mode/locks/lock_at/창 리셋), 틀리면 hunt_miss++ run=0,
     *       마지막으로 rx 를 상태에 싣는다 (self-sync) */
}

/* LOCKED: 로컬 LFSR 이 기대 비트를 만들고, 상태는 "기대 비트"로 민다 (받은 비트 아님 → 오류 증식 없음).
 * 오류면 1 반환, errors/first_err_at 갱신. */
static int chk_locked(prbs_chk_t *c, int rx) {
    (void)c; (void)rx;
    return 0; /* TODO: exp = feedback(s), 상태는 exp 로 민다, bits++, 다르면 errors++/first_err_at */
}

/* ================= Part B: 동기 상실 검출 ================= */
/* 창 안의 오류가 LOSS_THR 에 닿으면 즉시 true (창 리셋). 창이 차면 리셋. */
static bool chk_loss(prbs_chk_t *c, int err) {
    (void)c; (void)err;
    return false; /* TODO: win_bits++, win_err += err; win_err >= LOSS_THR → 창 리셋 + true;
                   *       win_bits >= LOSS_WIN → 창 리셋 */
}

/* 비트 하나를 checker 에 넣는다 (주어진 코드). 반환: LOCKED 상태에서 센 오류면 1 */
static int chk_push(prbs_chk_t *c, int rx) {
    c->rx_bits++;
    if (c->invert) rx ^= 1;
    if (c->mode == CHK_HUNT) { chk_hunt(c, rx); return 0; }
    int e = chk_locked(c, rx);
    if (chk_loss(c, e)) {
        if (c->sync_loss++ == 0) c->first_loss_at = c->rx_bits - 1;
        c->mode = CHK_HUNT; c->fill = 0; c->run = 0;     /* 다시 받은 비트로 상태를 채운다 */
    }
    return e;
}

/* ================= Part B: 순수 feed-forward checker (오류 증식 관찰용) ================= */
/* 예측 = rx[t-n] ^ rx[t-k] 를 "받은 비트"로만 계산. 오류 1개가 몇 번 세어지는지 본다. */
static uint64_t ff_count_errors(const prbs_poly_t *p, const uint8_t *rx, size_t nbits) {
    (void)p; (void)rx; (void)nbits;
    return 0; /* TODO: t = n .. nbits-1 에서 (rx[t-n] ^ rx[t-k]) != rx[t] 를 센다 */
}

/* ================= Part B: BER 통계 (Poisson) ================= */
/* P(X <= k), X ~ Poisson(lambda).  N 비트, 실제 BER p 이면 오류 수 ≈ Poisson(N p) */
static double poisson_cdf(int k, double lambda) {
    (void)k; (void)lambda;
    return 0.0; /* TODO: Σ_{i=0..k} e^-λ λ^i / i!  (큰 λ 대비 log 영역: exp(i·lnλ - λ - lgamma(i+1))) */
}
/* poisson_cdf(k, λ) = 1 - cl 이 되는 λ (오류 k 개를 봤을 때 기대 오류 수의 단측 상한) */
static double ber_lambda(int k, double cl) {
    (void)k; (void)cl;
    return 0.0; /* TODO: poisson_cdf(k, λ) 는 λ 에 대해 감소 → 이분법으로 1-cl 이 되는 λ */
}
/* "BER < ber 를 신뢰도 cl 로" 주장하는 데 필요한 비트 수 (오류 k 개까지 허용) */
static double ber_bits_needed(double ber, double cl, int k) {
    (void)ber; (void)cl; (void)k;
    return 0.0; /* TODO: λ_k(cl) / ber */
}
/* 오류 k 개 / bits 비트 관측 → BER 의 단측 상한 (신뢰도 cl) */
static double ber_upper(uint64_t k, uint64_t bits, double cl) {
    (void)k; (void)bits; (void)cl;
    return 0.0; /* TODO: λ_k(cl) / bits */
}
/* bits 비트 동안 오류 ≤ k 였다면 "BER < ber" 의 신뢰도 */
static double ber_confidence(double bits, double ber, int k) {
    (void)bits; (void)ber; (void)k;
    return 0.0; /* TODO: 1 - P(X <= k), X ~ Poisson(bits·ber) */
}
/* 링크 속도 gbps 에서 그 비트 수를 모으는 데 걸리는 시간 (초) — 주어진 코드 */
static double ber_test_seconds(double gbps, double ber, double cl, int k) {
    return ber_bits_needed(ber, cl, k) / (gbps * 1e9);
}

/* ================= Part C: lane 회귀 ================= */
typedef struct {
    int lane;
    const prbs_poly_t *tx;      /* TX 가 보내는 패턴                     */
    double ber;                 /* 채널이 뒤집는 확률 (랜덤 오류)          */
    uint64_t slip_at;           /* 이 TX 비트에서 1비트 빠짐 (NONE = 없음)  */
    bool pn_swap;               /* 보드에서 P/N 이 바뀜 → 모든 비트 반전   */
} lane_cfg_t;

typedef enum { V_PASS, V_FAIL_BER, V_INCONCLUSIVE, V_SYNC_LOSS, V_POLARITY, V_NO_LOCK } verdict_t;
static const char *const verdict_name[] = { "PASS", "BER", "INCONCLUSIVE", "SYNC_LOSS", "POLARITY", "NO_LOCK" };

typedef struct {
    lane_cfg_t cfg;
    prbs_chk_t chk;             /* RX 정상 극성으로 돌린 결과              */
    bool inv_lock;              /* POLARITY 의심 시 invert 로 재시험 → lock? */
    verdict_t v;
    double ber_meas, ber_ub;    /* 점추정, 단측 상한                       */
    double bits_need;           /* INCONCLUSIVE: 현재 오류 수로 필요한 총 비트 */
} lane_result_t;

#define HUNT_INV_RATIO 0.90     /* HUNT 예측이 90% 이상 틀림 → 반전 패턴 의심 (랜덤이면 ~50%) */

/* 채널 시뮬레이션 + checker (주어진 코드) */
static void lane_simulate(const lane_cfg_t *cfg, const prbs_poly_t *rx_poly, bool invert, uint64_t nbits,
                          prbs_chk_t *chk) {
    prbs_gen_t g;
    prbs_seed(&g, cfg->tx, 0x1234567u + 7919u * (uint32_t)cfg->lane);
    rng_seed(0x5EED0000ull + (uint64_t)cfg->lane);
    chk_init(chk, rx_poly, invert);
    for (uint64_t t = 0; t < nbits; t++) {
        if (t == cfg->slip_at) (void)prbs_next(&g);            /* CDR 가 한 비트 놓침 */
        int b = prbs_next(&g);
        if (cfg->ber > 0 && urand() < cfg->ber) b ^= 1;
        if (cfg->pn_swap) b ^= 1;
        (void)chk_push(chk, b);
    }
}

/* 판정: NO_LOCK/POLARITY → SYNC_LOSS → BER 상한 비교. INCONCLUSIVE 면 필요한 총 비트도 채운다 */
static verdict_t lane_judge(lane_result_t *r, double target, double cl) {
    (void)target; (void)cl;
    r->ber_meas = r->ber_ub = r->bits_need = 0;
    return r->v = V_PASS; /* TODO: lock 0 → hunt 틀림 비율로 POLARITY/NO_LOCK, sync_loss → SYNC_LOSS,
                           *       ub <= target → PASS, 점추정 >= target → BER, 그 외 INCONCLUSIVE(+bits_need) */
}

/* 한 줄 리포트. snprintf 처럼 "썼어야 할 길이" 반환, 버퍼는 넘치지 않음 */
static int lane_report(char *buf, size_t n, const lane_result_t *r, double target) {
    (void)r; (void)target; (void)verdict_name;
    if (n) buf[0] = '\0';
    return 0; /* TODO: "<PASS|FAIL|INCONCLUSIVE> lane= pat= verdict= rx= " + 판정별 증거와 다음 행동.
               *       snprintf 처럼 "썼어야 할 길이" 반환, 버퍼 넘치지 않게 */
}

/* lane 하나 시험 (주어진 코드): 정상 극성 → 판정 → POLARITY 의심이면 invert 로 재시험해 확인 */
static void lane_test(const lane_cfg_t *cfg, uint64_t nbits, double target, double cl, lane_result_t *r) {
    memset(r, 0, sizeof *r);
    r->cfg = *cfg;
    lane_simulate(cfg, &PRBS31, false, nbits, &r->chk);
    lane_judge(r, target, cl);
    if (r->v == V_POLARITY) {
        prbs_chk_t inv;
        lane_simulate(cfg, &PRBS31, true, nbits, &inv);
        r->inv_lock = inv.locks > 0 && inv.sync_loss == 0;
    }
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define NEAR(a, b, tol) (fabs((double)(a) - (double)(b)) <= (tol))

/* 주기: 시드 상태로 처음 돌아오는 걸음 수 (limit 안에서) */
static uint64_t prbs_period(const prbs_poly_t *p, uint32_t seed, uint64_t limit) {
    prbs_gen_t g; prbs_seed(&g, p, seed);
    uint32_t s0 = g.s;
    for (uint64_t i = 1; i <= limit; i++) { (void)prbs_next(&g); if (g.s == s0) return i; }
    return 0;
}
/* 한 주기의 1 개수, 최장 1-run, 최장 0-run (두 주기를 이어 보아 wrap 되는 run 도 포함) */
static void prbs_stats(const prbs_poly_t *p, uint64_t period, uint64_t *ones, int *run1, int *run0) {
    prbs_gen_t g; prbs_seed(&g, p, 1);
    *ones = 0; *run1 = *run0 = 0;
    int cur = -1, len = 0;
    for (uint64_t i = 0; i < 2 * period; i++) {
        int b = prbs_next(&g);
        if (i < period) *ones += (uint64_t)b;
        len = (b == cur) ? len + 1 : 1; cur = b;
        if (b && len > *run1) *run1 = len;
        if (!b && len > *run0) *run0 = len;
    }
}

#define NB_LANE 400000u
static uint8_t g_buf[300000];

int main(void) {
    /* ===== Part A: generator ===== */
    /* A1-A2: 주기 = 2^n - 1 (그 전에는 시드로 안 돌아옴) */
    CHECK(prbs_period(&PRBS7, 1, 1000) == 127);
    CHECK(prbs_period(&PRBS7, 0x55, 1000) == 127);
    CHECK(prbs_period(&PRBS15, 1, 100000) == 32767);
    /* A3: 균형(ones = 2^(n-1)) + run 성질 (최장 1-run = n, 최장 0-run = n-1) */
    uint64_t ones; int r1, r0;
    prbs_stats(&PRBS7, 127, &ones, &r1, &r0);
    CHECK(ones == 64 && r1 == 7 && r0 == 6);
    prbs_stats(&PRBS15, 32767, &ones, &r1, &r0);
    CHECK(ones == 16384 && r1 == 15 && r0 == 14);
    /* A4: PRBS31 — 주기(2^31-1)는 demo 에서. 여기선 점화식 s[t] = s[t-31]^s[t-28] 과 균형 */
    {
        prbs_gen_t g; prbs_seed(&g, &PRBS31, 0x2545F491u);
        size_t n = sizeof g_buf, bad = 0, ones31 = 0;
        for (size_t t = 0; t < n; t++) { g_buf[t] = (uint8_t)prbs_next(&g); ones31 += g_buf[t]; }
        for (size_t t = 31; t < n; t++) if (g_buf[t] != (g_buf[t - 31] ^ g_buf[t - 28])) bad++;
        CHECK(bad == 0);
        CHECK(NEAR((double)ones31 / (double)n, 0.5, 0.005));
    }
    /* A5: 시드 0 은 lock-up → 0 이 아닌 상태로 강제, 출력이 0 만은 아님 */
    {
        prbs_gen_t g; prbs_seed(&g, &PRBS15, 0);
        int any1 = 0; for (int i = 0; i < 100; i++) any1 |= prbs_next(&g);
        CHECK(g.s != 0 && any1);
    }

    /* ===== Part A: checker ===== */
    /* A6: 깨끗한 스트림, 임의 위상에서 시작 → 정확히 n + LOCK_RUN 비트 후 lock, 오류 0 (패턴 3종 × 위상 20개) */
    {
        const prbs_poly_t *ps[] = { &PRBS7, &PRBS15, &PRBS31 };
        int ok = 0, total = 0;
        rng_seed(0xA6);
        for (int pi = 0; pi < 3; pi++)
            for (int trial = 0; trial < 20; trial++) {
                prbs_gen_t g; prbs_seed(&g, ps[pi], (uint32_t)xs64());
                unsigned skip = (unsigned)(xs64() % 5000u);
                for (unsigned i = 0; i < skip; i++) (void)prbs_next(&g);
                prbs_chk_t c; chk_init(&c, ps[pi], false);
                for (int i = 0; i < 3000; i++) (void)chk_push(&c, prbs_next(&g));
                total++;
                ok += c.mode == CHK_LOCKED && c.locks == 1 && c.lock_at == (uint64_t)(ps[pi]->n + LOCK_RUN)
                      && c.errors == 0 && c.bits == 3000u - c.lock_at && c.hunt_miss == 0;
            }
        CHECK(ok == total);
    }
    /* A7: lock 후 정해진 위치 5곳에 오류 → 정확히 5개, 첫 오류 위치 */
    {
        static const unsigned pos[] = { 500, 777, 1200, 1201, 2999 };   /* 1200·1201: 연속 오류도 각각 1개 */
        prbs_gen_t g; prbs_seed(&g, &PRBS15, 0xACE1);
        prbs_chk_t c; chk_init(&c, &PRBS15, false);
        size_t pi = 0;
        for (unsigned t = 0; t < 4000; t++) {
            int b = prbs_next(&g);
            if (pi < 5 && t == pos[pi]) { b ^= 1; pi++; }
            (void)chk_push(&c, b);
        }
        CHECK(c.errors == 5 && c.first_err_at == 500 && c.sync_loss == 0 && c.locks == 1);
    }
    /* A8: 패턴 불일치 (TX PRBS15, RX PRBS31 checker) → lock 안 됨, HUNT 예측 ~50% 틀림 */
    {
        prbs_gen_t g; prbs_seed(&g, &PRBS15, 1);
        prbs_chk_t c; chk_init(&c, &PRBS31, false);
        for (int i = 0; i < 20000; i++) (void)chk_push(&c, prbs_next(&g));
        double miss = (double)c.hunt_miss / (double)(c.hunt_pred ? c.hunt_pred : 1);
        CHECK(c.locks == 0 && c.hunt_pred > 19000 && miss > 0.45 && miss < 0.55);
    }

    /* ===== Part B: 오류 증식 · 랜덤 BER · slip · 극성 ===== */
    /* B1: 고립 오류 4개 → feed-forward 는 3배(=12), LOCKED checker 는 4 */
    {
        prbs_gen_t g; prbs_seed(&g, &PRBS7, 0x11);
        size_t n = 2000;
        for (size_t t = 0; t < n; t++) g_buf[t] = (uint8_t)prbs_next(&g);
        static const size_t epos[] = { 300, 600, 900, 1500 };            /* 서로 n(=7) 이상 떨어짐 */
        for (size_t i = 0; i < 4; i++) g_buf[epos[i]] ^= 1u;
        CHECK(ff_count_errors(&PRBS7, g_buf, n) == 12);
        prbs_chk_t c; chk_init(&c, &PRBS7, false);
        for (size_t t = 0; t < n; t++) (void)chk_push(&c, g_buf[t]);
        CHECK(c.errors == 4 && c.first_err_at == 300);
        /* 붙어 있는 두 오류(거리 = n-k = 1)는 feed-forward 에서 서로 상쇄되기도 한다: 2개 → 4개로 셈 */
        g_buf[300] ^= 1u; g_buf[600] ^= 1u; g_buf[900] ^= 1u; g_buf[1500] ^= 1u;
        g_buf[1000] ^= 1u; g_buf[1001] ^= 1u;
        CHECK(ff_count_errors(&PRBS7, g_buf, n) == 4);
    }
    /* B2: lock 후 BER 1e-3 랜덤 주입 → checker 가 센 수 == 주입한 수 (정확히), 그리고 상한 ≥ 실제 BER */
    {
        prbs_gen_t g; prbs_seed(&g, &PRBS31, 0xBEEF);
        prbs_chk_t c; chk_init(&c, &PRBS31, false);
        rng_seed(0xB2);
        uint64_t injected = 0;
        for (unsigned t = 0; t < 200000; t++) {
            int b = prbs_next(&g);
            if (t >= 200 && urand() < 1e-3) { b ^= 1; injected++; }
            (void)chk_push(&c, b);
        }
        printf("B2 injected=%llu counted=%llu bits=%llu ber=%.3e ub95=%.3e\n", (unsigned long long)injected,
               (unsigned long long)c.errors, (unsigned long long)c.bits, (double)c.errors / (double)c.bits,
               ber_upper(c.errors, c.bits, 0.95));
        CHECK(c.locks == 1 && c.sync_loss == 0 && c.errors == injected && injected > 150);
        CHECK(ber_upper(c.errors, c.bits, 0.95) >= 1e-3);
    }
    /* B3: bit slip (1비트 빠짐) → 동기 상실 1회, 재동기 후 오류 멈춤 / B4: 1비트 끼어듦도 같음 */
    for (int mode = 0; mode < 2; mode++) {
        prbs_gen_t g; prbs_seed(&g, &PRBS31, 0xC0FFEE);
        prbs_chk_t c; chk_init(&c, &PRBS31, false);
        int prev = 0;
        for (unsigned t = 0; t < 20000; t++) {
            int b = prbs_next(&g);
            if (t == 10000) { if (mode == 0) b = prbs_next(&g); else { (void)chk_push(&c, prev); } }
            (void)chk_push(&c, b);
            prev = b;
        }
        uint64_t err_after_relock = c.errors;   /* 재동기 후엔 더 늘지 않아야 */
        for (unsigned t = 0; t < 5000; t++) (void)chk_push(&c, prbs_next(&g));
        CHECK(c.sync_loss == 1 && c.locks == 2 && c.first_loss_at >= 10000 && c.first_loss_at < 10000 + LOSS_WIN
              && c.errors >= LOSS_THR && c.errors < 2 * LOSS_WIN && c.errors == err_after_relock
              && c.first_err_at >= 10000);
    }
    /* B5: P/N 반전 → HUNT 예측이 100% 틀림 (XOR 두 탭이 반전을 상쇄), invert 설정하면 정상 lock */
    {
        prbs_gen_t g; prbs_seed(&g, &PRBS31, 0x1D);
        prbs_chk_t c, ci; chk_init(&c, &PRBS31, false); chk_init(&ci, &PRBS31, true);
        for (int i = 0; i < 5000; i++) { int b = prbs_next(&g) ^ 1; (void)chk_push(&c, b); (void)chk_push(&ci, b); }
        CHECK(c.locks == 0 && c.hunt_miss == c.hunt_pred && c.hunt_pred == 5000u - 31u);
        CHECK(ci.locks == 1 && ci.errors == 0);
    }

    /* ===== Part B: BER 통계 ===== */
    CHECK(NEAR(poisson_cdf(0, 1.0), exp(-1.0), 1e-12));
    CHECK(NEAR(poisson_cdf(2, 3.0), exp(-3.0) * (1 + 3 + 4.5), 1e-12));
    CHECK(NEAR(poisson_cdf(5, 0.0), 1.0, 0));
    /* B6: λ 표 (95%) — 카이제곱 χ²(0.95, 2(k+1))/2 와 같은 값 */
    CHECK(NEAR(ber_lambda(0, 0.95), 2.995732, 1e-5) && NEAR(ber_lambda(1, 0.95), 4.743865, 1e-5));
    CHECK(NEAR(ber_lambda(2, 0.95), 6.295794, 1e-5) && NEAR(ber_lambda(3, 0.95), 7.753657, 1e-5));
    CHECK(NEAR(ber_lambda(0, 0.99), 4.605170, 1e-5) && NEAR(ber_lambda(0, 0.95), -log(0.05), 1e-9));
    /* B7: 1e-12 @95%, 오류 0 → ≈ 3.0e12 비트. 정확한 이항식 ln(1-CL)/ln(1-p) 와 상대오차 < 1e-6 */
    double n12 = ber_bits_needed(1e-12, 0.95, 0);
    double n12_binom = log(0.05) / log1p(-1e-12);
    CHECK(n12 > 2.995e12 && n12 < 2.997e12 && fabs(n12 - n12_binom) / n12_binom < 1e-6);
    /* B8: 1e12 비트 무오류 → "BER<1e-12" 신뢰도는 63.2% 뿐 */
    CHECK(NEAR(ber_confidence(1e12, 1e-12, 0), 1.0 - exp(-1.0), 1e-9));
    /* B9: 상한 — 0 err / 1e6 비트 → 3.0e-6, 2 err → 6.3e-6 */
    CHECK(NEAR(ber_upper(0, 1000000, 0.95), 2.995732e-6, 1e-11) && NEAR(ber_upper(2, 1000000, 0.95), 6.295794e-6, 1e-11));
    /* B10: Monte-Carlo — 실제 BER = 목표 인 링크가 무오류 시험을 통과하는 비율 ≈ 5% (= 95% 의 의미) */
    {
        double p = 1e-3; unsigned nb = (unsigned)ceil(ber_bits_needed(p, 0.95, 0));   /* 2996 */
        unsigned pass_at = 0, pass_good = 0, T = 1000;
        rng_seed(0xB10);
        for (unsigned tr = 0; tr < T; tr++) {
            unsigned e = 0; for (unsigned i = 0; i < nb && !e; i++) e += urand() < p;        pass_at += !e;
            e = 0;          for (unsigned i = 0; i < nb && !e; i++) e += urand() < p / 10;   pass_good += !e;
        }
        printf("B10 nb=%u pass@target=%u/%u pass@target/10=%u/%u (theory %.1f%% / %.1f%%)\n", nb, pass_at, T,
               pass_good, T, 100 * exp(-p * nb), 100 * exp(-p / 10 * nb));
        CHECK(nb == 2996 && pass_at >= 30 && pass_at <= 70 && pass_good >= 700 && pass_good <= 785);
    }

    /* ===== Part C: 6-lane 회귀 밤샘 ===== */
    {
        const double target = 1e-5, cl = 0.95;
        const lane_cfg_t lanes[] = {
            { 0, &PRBS31, 0,    NONE,   false },   /* 정상                                  */
            { 1, &PRBS31, 1e-4, NONE,   false },   /* eye 가 닫힌 lane (랜덤 오류 많음)      */
            { 2, &PRBS31, 0,    200000, false },   /* CDR 순간 unlock → bit slip            */
            { 3, &PRBS31, 0,    NONE,   true  },   /* 보드에서 P/N 바뀜                      */
            { 4, &PRBS15, 0,    NONE,   false },   /* TX 패턴 선택 오류 (PRBS15 ↔ PRBS31)    */
            { 5, &PRBS31, 2e-6, NONE,   false },   /* 실제론 양호(2e-6)지만 비트가 모자람 → 보류 */
        };
        static const verdict_t expect[] = { V_PASS, V_FAIL_BER, V_SYNC_LOSS, V_POLARITY, V_NO_LOCK, V_INCONCLUSIVE };
        lane_result_t r[6];
        char line[320];
        int ok = 0;
        printf("-- Part C: lane report (target BER %.0e @ %.0f%%, %u bits/lane) --\n", target, cl * 100, NB_LANE);
        for (int i = 0; i < 6; i++) {
            lane_test(&lanes[i], NB_LANE, target, cl, &r[i]);
            lane_report(line, sizeof line, &r[i], target);
            printf("%s\n", line);
            ok += r[i].v == expect[i];
        }
        CHECK(ok == 6);
        CHECK(r[0].chk.errors == 0 && r[0].ber_ub <= target && r[0].chk.bits == NB_LANE - 63u);
        CHECK(r[1].chk.errors > 20 && r[1].chk.first_err_at != NONE && r[1].ber_meas > target);
        CHECK(r[2].chk.first_loss_at >= 200000 && r[2].chk.first_loss_at < 200000 + LOSS_WIN && r[2].chk.locks == 2);
        CHECK(r[3].inv_lock && r[3].chk.hunt_miss == r[3].chk.hunt_pred);
        CHECK(!r[4].inv_lock && r[4].chk.locks == 0);
        CHECK(r[5].chk.errors >= 1 && r[5].ber_meas < target && r[5].ber_ub > target && r[5].bits_need > NB_LANE);
        lane_report(line, sizeof line, &r[2], target);
        CHECK(strstr(line, "verdict=SYNC_LOSS") && strstr(line, "first_loss@") && strstr(line, "relocks=1"));
        lane_report(line, sizeof line, &r[3], target);
        CHECK(strstr(line, "FAIL") == line && strstr(line, "inverted_lock=yes") && strstr(line, "P/N"));
        lane_report(line, sizeof line, &r[5], target);
        CHECK(strstr(line, "INCONCLUSIVE") == line && strstr(line, "need "));
        char tiny[20];
        CHECK(lane_report(tiny, sizeof tiny, &r[1], target) > (int)sizeof tiny && strlen(tiny) == sizeof tiny - 1);
    }
    /* C: 링크 속도별 측정 시간 (1e-12 / 1e-15 @95%, 오류 0) */
    {
        static const double rates[] = { 10, 25, 56, 112 };
        printf("-- BER test time @95%% CL, 0 errors --\n");
        for (int i = 0; i < 4; i++)
            printf("%6.0f Gb/s  1e-12: %8.1f s   1e-15: %9.0f s (%.2f h)\n", rates[i],
                   ber_test_seconds(rates[i], 1e-12, 0.95, 0), ber_test_seconds(rates[i], 1e-15, 0.95, 0),
                   ber_test_seconds(rates[i], 1e-15, 0.95, 0) / 3600);
        CHECK(NEAR(ber_test_seconds(10, 1e-12, 0.95, 0), 299.57, 0.01));
        CHECK(NEAR(ber_test_seconds(112, 1e-12, 0.95, 0), 26.75, 0.01));
        CHECK(NEAR(ber_test_seconds(25, 1e-15, 0.95, 0) / 3600, 33.29, 0.01));
        CHECK(NEAR(ber_test_seconds(25, 1e-12, 0.95, 2), 251.83, 0.01));      /* 오류 2개 허용 → 2.1배 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
