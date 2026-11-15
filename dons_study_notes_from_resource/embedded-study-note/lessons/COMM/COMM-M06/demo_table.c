/* COMM-M06 demo_table.c — f_can × bitrate × 목표 SP 표 (bt_best + 허용 오차 + BTR).
 *
 * solution.c 의 Part A/B 함수와 bxcan_btr 를 그대로 복사했다. 각 행:
 *   - bt_best(f, R, SP 목표, BXCAN, 5000 ppm) 의 선택 (규칙: 오차 → SP 거리 → tq 많이 → BRP 작게 → 낮은 SP)
 *   - df: PROP = TSEG1 − TSEG2 (PS1 = PS2 가 되도록 PROP 를 최대로 줬을 때) 의 노드당 허용 오차와 어느 조건이 묶는지
 *   - t_rt max: 그 PROP 가 덮는 최대 왕복 지연 = (TSEG1 − TSEG2)·tq [ns]
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_table.c -o demo_table && ./demo_table
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { BT_OK = 0, BT_EARG = -1, BT_ENOSOL = -2, BT_ERATE = -3, BT_EPROP = -4, BT_ESPACE = -5 };

typedef struct {
    uint32_t brp_min, brp_max, tseg1_min, tseg1_max, tseg2_min, tseg2_max, sjw_max, ntq_min, ntq_max;
} bt_limits;

static const bt_limits BXCAN    = { 1, 1024, 1, 16, 1, 8, 4, 8, 25 };  /* 계산기 기본: NBT 8–25 tq */
static const bt_limits BXCAN_HW = { 1, 1024, 1, 16, 1, 8, 4, 3, 25 };  /* 레지스터가 표현하는 전부 (BTR 디코드용) */

typedef struct {
    uint32_t f_can, bitrate;            /* 입력: CAN 커널 클럭 [Hz], 목표 bitrate [bit/s] */
    uint32_t brp, tseg1, tseg2, sjw;    /* 실제 길이 (레지스터 값 + 1) */
    uint32_t ntq;                       /* NBT = 1 + tseg1 + tseg2 */
    int64_t  err_ppm;                   /* (실제 − 목표) / 목표 × 1e6, 반올림 (표시용) */
    uint32_t sp_permille;               /* 1000·(1 + tseg1) / ntq, 반올림 (표시용) */
} can_bt;

/* ---------------- 주어진 도우미 ---------------- */
static uint64_t absdiff_u64(uint64_t a, uint64_t b) { return a > b ? a - b : b - a; }
static uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }
static int args_ok(uint32_t f_can, uint32_t bitrate) {
    return f_can >= 1000u && f_can <= 1000000000u && bitrate >= 1000u && bitrate <= 10000000u;
}
/* |실제 − 목표| ≤ max_err_ppm ?  — 정확한 정수 비교: |f − R·d|·1e6 ≤ max·R·d  (d = brp·ntq) */
static int rate_within(uint32_t f_can, uint32_t bitrate, uint64_t d, uint32_t max_err_ppm) {
    uint64_t rd = (uint64_t)bitrate * d;
    return absdiff_u64(f_can, rd) * 1000000u <= (uint64_t)max_err_ppm * rd;
}

/* ================= Part A: 계산기 ================= */

/* 한 조합을 검사하고 파생값을 채운다. 한계 위반, SJW > TSEG2, NBT 범위 밖, 입력 범위 밖 → BT_EARG.
 * 실패하면 *out 은 건드리지 않는다. */
static int bt_make(uint32_t f_can, uint32_t bitrate, uint32_t brp, uint32_t tseg1, uint32_t tseg2, uint32_t sjw,
                   const bt_limits *lim, can_bt *out) {
    if (!lim || !out || !args_ok(f_can, bitrate)) return BT_EARG;
    if (brp < lim->brp_min || brp > lim->brp_max || tseg1 < lim->tseg1_min || tseg1 > lim->tseg1_max ||
        tseg2 < lim->tseg2_min || tseg2 > lim->tseg2_max || sjw < 1 || sjw > lim->sjw_max || sjw > tseg2)
        return BT_EARG;
    uint32_t ntq = 1 + tseg1 + tseg2;
    if (ntq < lim->ntq_min || ntq > lim->ntq_max) return BT_EARG;
    int64_t rd = (int64_t)bitrate * brp * ntq;                  /* 실제 bitrate = f / (brp·ntq) */
    int64_t num = ((int64_t)f_can - rd) * 1000000;              /* 오차 = (f − R·d) / (R·d) */
    out->f_can = f_can; out->bitrate = bitrate;
    out->brp = brp; out->tseg1 = tseg1; out->tseg2 = tseg2; out->sjw = sjw; out->ntq = ntq;
    out->err_ppm = num >= 0 ? (num + rd / 2) / rd : -((-num + rd / 2) / rd);
    out->sp_permille = (2000u * (1 + tseg1) + ntq) / (2u * ntq);
    return BT_OK;
}

/* a 가 b 보다 엄격히 나으면 1. 규칙 (위에서부터):
 *   1) |bitrate 오차| 작은 쪽   2) |SP − 목표| 작은 쪽   3) NBT(tq 수) 큰 쪽   4) BRP 작은 쪽
 *   5) TSEG2 큰 쪽 (= 같은 거리면 낮은 SP)
 * 1), 2) 는 분수를 교차 곱으로 비교한다 (부동소수 없음). a, b 는 같은 f_can, bitrate 에 대한 후보. */
static int bt_better(const can_bt *a, const can_bt *b, uint32_t sp_target) {
    uint64_t da = (uint64_t)a->brp * a->ntq, db = (uint64_t)b->brp * b->ntq;
    uint64_t ea = absdiff_u64(a->f_can, (uint64_t)a->bitrate * da) * db;
    uint64_t eb = absdiff_u64(b->f_can, (uint64_t)b->bitrate * db) * da;
    if (ea != eb) return ea < eb;
    uint64_t sa = absdiff_u64(1000u * (uint64_t)(1 + a->tseg1), (uint64_t)sp_target * a->ntq) * b->ntq;
    uint64_t sb = absdiff_u64(1000u * (uint64_t)(1 + b->tseg1), (uint64_t)sp_target * b->ntq) * a->ntq;
    if (sa != sb) return sa < sb;
    if (a->ntq != b->ntq) return a->ntq > b->ntq;
    if (a->brp != b->brp) return a->brp < b->brp;
    return a->tseg2 > b->tseg2;
}

/* 목표 bitrate·SP(‰)에 대한 최선. SJW = min(sjw_max, TSEG2) (이 레슨의 정책).
 * NBT 마다 BRP 후보는 floor/ceil(f / (R·NBT)) 와 한계 양 끝뿐이다 (실제 bitrate 가 BRP 에 단조라서).
 * 반환: BT_OK / BT_ERATE (최선의 |오차| > max_err_ppm — *out 에는 그 최선이 들어 있다) / BT_ENOSOL / BT_EARG. */
static int bt_best(uint32_t f_can, uint32_t bitrate, uint32_t sp_permille, const bt_limits *lim,
                   uint32_t max_err_ppm, can_bt *out) {
    if (!lim || !out || !args_ok(f_can, bitrate) || sp_permille == 0 || sp_permille >= 1000) return BT_EARG;
    can_bt best, c;
    int found = 0;
    for (uint32_t n = lim->ntq_min; n <= lim->ntq_max; n++) {
        uint64_t q = f_can / ((uint64_t)bitrate * n);
        uint64_t cand[4] = { q, q + 1, lim->brp_min, lim->brp_max };
        for (int i = 0; i < 4; i++) {
            if (cand[i] < lim->brp_min || cand[i] > lim->brp_max) continue;
            for (uint32_t t2 = lim->tseg2_min; t2 <= lim->tseg2_max && t2 + 1 < n; t2++) {
                uint32_t sjw = min_u32(lim->sjw_max, t2);
                if (bt_make(f_can, bitrate, (uint32_t)cand[i], n - 1 - t2, t2, sjw, lim, &c) != BT_OK) continue;
                if (!found || bt_better(&c, &best, sp_permille)) { best = c; found = 1; }
            }
        }
    }
    if (!found) return BT_ENOSOL;
    *out = best;
    return rate_within(f_can, bitrate, (uint64_t)best.brp * best.ntq, max_err_ppm) ? BT_OK : BT_ERATE;
}

/* |오차| ≤ max_err_ppm 인 모든 유효 조합을 (BRP↑, NBT↑, TSEG1↑) 순서로 out[] 에 쓴다 (SJW = min(sjw_max, TSEG2)).
 * *total = 찾은 전체 개수. cap 보다 많으면 앞의 cap 개만 쓰고 BT_ESPACE. */
static int bt_enumerate(uint32_t f_can, uint32_t bitrate, const bt_limits *lim, uint32_t max_err_ppm,
                        can_bt *out, size_t cap, size_t *total) {
    if (!lim || !total || (!out && cap) || !args_ok(f_can, bitrate)) return BT_EARG;
    size_t k = 0;
    for (uint32_t brp = lim->brp_min; brp <= lim->brp_max; brp++)
        for (uint32_t n = lim->ntq_min; n <= lim->ntq_max; n++) {
            if (!rate_within(f_can, bitrate, (uint64_t)brp * n, max_err_ppm)) continue;
            for (uint32_t t1 = lim->tseg1_min; t1 <= lim->tseg1_max && t1 + 1 < n; t1++) {
                uint32_t t2 = n - 1 - t1;
                can_bt c;
                if (bt_make(f_can, bitrate, brp, t1, t2, min_u32(lim->sjw_max, t2), lim, &c) != BT_OK) continue;
                if (k < cap) out[k] = c;
                k++;
            }
        }
    *total = k;
    return k > cap ? BT_ESPACE : BT_OK;
}

/* ================= Part B: 전파 지연과 발진기 허용 오차 ================= */

/* PROP_SEG [tq] = ceil(왕복 지연 / tq),  tq = brp / f_can. */
static uint32_t bt_prop_tq(uint32_t f_can, uint32_t brp, uint32_t t_rt_ns) {
    uint64_t num = (uint64_t)t_rt_ns * f_can, den = (uint64_t)brp * 1000000000u;
    return den ? (uint32_t)((num + den - 1) / den) : 0;
}

typedef struct {
    uint32_t prop, ps1, ps2;     /* TSEG1 = PROP + PS1, PS2 = TSEG2 */
    uint32_t df1_ppm, df2_ppm;   /* 조건 1, 2 각각의 노드당 허용 |Δf/f| (ppm, 내림) */
    uint32_t df_ppm;             /* min(df1, df2) */
    int limiter;                 /* 1 또는 2: 어느 조건이 더 빡빡한가 (같으면 1) */
} bt_tol;

/* 발진기 허용 오차 (Bosch / ISO 11898-1 식 — 확인 권장):
 *   조건 1: df ≤ min(PS1, PS2) / (2·(13·NBT − PS2))     (에러 플래그 구간, 13 비트)
 *   조건 2: df ≤ SJW / (20·NBT)                         (재동기화 에지 사이 최대 10 비트)
 * PS1 = TSEG1 − PROP ≥ 1 이어야 한다. 아니면 BT_EPROP. */
static int bt_tolerance(const can_bt *b, uint32_t prop_tq, bt_tol *t) {
    if (!b || !t || b->ntq != 1 + b->tseg1 + b->tseg2 || b->sjw == 0) return BT_EARG;
    if (prop_tq >= b->tseg1) return BT_EPROP;
    t->prop = prop_tq;
    t->ps1 = b->tseg1 - prop_tq;
    t->ps2 = b->tseg2;
    uint64_t m = min_u32(t->ps1, t->ps2);
    t->df1_ppm = (uint32_t)(m * 1000000u / (2u * (13u * (uint64_t)b->ntq - t->ps2)));
    t->df2_ppm = (uint32_t)((uint64_t)b->sjw * 1000000u / (20u * (uint64_t)b->ntq));
    t->limiter = t->df1_ppm <= t->df2_ppm ? 1 : 2;
    t->df_ppm = min_u32(t->df1_ppm, t->df2_ppm);
    return BT_OK;
}

/* STM32 bxCAN CAN_BTR (RM0008/RM0090 — RM 확인 권장):
 *   [31] SILM  [30] LBKM  [25:24] SJW−1  [22:20] TS2−1  [19:16] TS1−1  [9:0] BRP−1, 나머지 예약(0). */
#define BTR_SILM (1u << 31)
#define BTR_LBKM (1u << 30)
#define BTR_RESERVED (~(BTR_SILM | BTR_LBKM | (3u << 24) | (7u << 20) | (15u << 16) | 0x3FFu))

static uint32_t bxcan_btr(const can_bt *b, int loopback, int silent) {
    return (silent ? BTR_SILM : 0) | (loopback ? BTR_LBKM : 0) | ((b->sjw - 1) & 3u) << 24 |
           ((b->tseg2 - 1) & 7u) << 20 | ((b->tseg1 - 1) & 15u) << 16 | ((b->brp - 1) & 0x3FFu);
}

int main(void) {
    (void)BXCAN_HW; (void)bt_prop_tq;               /* 복사해 온 것 중 이 데모가 안 쓰는 것 */
    static const uint32_t F[] = { 8000000, 16000000, 36000000, 42000000, 45000000, 48000000, 80000000 };
    static const uint32_t R[] = { 125000, 250000, 500000, 1000000 };
    static const uint32_t SP[] = { 875, 800 };
    for (int s = 0; s < 2; s++) {
        printf("\n### target SP %u.%u%%\n\n", SP[s] / 10, SP[s] % 10);
        printf("| f_can | bitrate | BRP | NBT | TS1 | TS2 | SJW | SP | err | BTR | df (cond) | t_rt max |\n");
        printf("|---|---|---|---|---|---|---|---|---|---|---|---|\n");
        for (int fi = 0; fi < 7; fi++)
            for (int ri = 0; ri < 4; ri++) {
                can_bt b;
                int rc = bt_best(F[fi], R[ri], SP[s], &BXCAN, 5000, &b);
                if (rc != BT_OK && rc != BT_ERATE) { printf("| %u | %u | rc=%d |\n", F[fi] / 1000000, R[ri] / 1000, rc); continue; }
                uint32_t prop = b.tseg1 > b.tseg2 ? b.tseg1 - b.tseg2 : 0;
                bt_tol t;
                int tr = bt_tolerance(&b, prop, &t);
                double tq_ns = 1e9 * b.brp / F[fi];
                printf("| %u MHz | %uk | %u | %u | %u | %u | %u | %u.%u%% | %+lld ppm%s | 0x%08X | ", F[fi] / 1000000,
                       R[ri] / 1000, b.brp, b.ntq, b.tseg1, b.tseg2, b.sjw, b.sp_permille / 10, b.sp_permille % 10,
                       (long long)b.err_ppm, rc == BT_ERATE ? " ERATE" : "", bxcan_btr(&b, 0, 0));
                if (tr == BT_OK) printf("%.3f%% (%d) | %.0f ns |\n", t.df_ppm / 1e4, t.limiter, prop * tq_ns);
                else printf("PROP %u: rc=%d | - |\n", prop, tr);
            }
    }
    /* 같은 bitrate 에 대해 "tq 많이 vs 적게" 의 허용 오차 비교: 16 MHz 500k, 정확한 모든 조합 중 SP 80~90% */
    printf("\n### 16 MHz 500k: exact settings with SP 80-90%% (PROP = 0)\n\n| BRP | NBT | TS1 | TS2 | SJW | SP | df1 | df2 | df |\n|---|---|---|---|---|---|---|---|---|\n");
    can_bt list[64];
    size_t tot = 0;
    bt_enumerate(16000000, 500000, &BXCAN, 0, list, 64, &tot);
    for (size_t i = 0; i < tot && i < 64; i++) {
        bt_tol t;
        if (list[i].sp_permille < 800 || list[i].sp_permille > 900 || bt_tolerance(&list[i], 0, &t) != BT_OK) continue;
        printf("| %u | %u | %u | %u | %u | %u.%u%% | %.3f%% | %.3f%% | %.3f%% |\n", list[i].brp, list[i].ntq, list[i].tseg1,
               list[i].tseg2, list[i].sjw, list[i].sp_permille / 10, list[i].sp_permille % 10, t.df1_ppm / 1e4,
               t.df2_ppm / 1e4, t.df_ppm / 1e4);
    }
    return 0;
}
