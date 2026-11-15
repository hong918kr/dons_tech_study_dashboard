/* COMM-M06 practice.c — 추가 연습 1~5 를 한 번에 검증한다.
 *
 * 연습 1: Linux can_calc_bittiming 식 선택(기억 기반 모사 — 커널 소스 확인 권장)과 이 레슨의 bt_best 비교
 * 연습 2: 내부 RC 발진기로 CAN 이 되나 — 16 MHz 500k 정확한 조합 중 허용 오차 최대
 * 연습 3: BTR 덤프 감식 — 다른 보드에서 복사한 BTR 이 원래 어떤 f_can 용이었나
 * 연습 4: 표준 bitrate 4개를 모두 오차 0 · SP 87.5% 로 내는 f_can (정수 MHz, 8–80)
 * 연습 5: 버스 길이 예산 — PROP 가 덮는 왕복 지연 → 최대 버스 길이 (지연 모델은 가정값)
 * Part A/B 함수와 bxcan_btr / decode 는 solution.c 에서 그대로 복사했다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
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

/* 레지스터 덤프 → 설정. 예약 비트가 1 이거나 SJW > TS2 면 BT_EARG. bitrate = 비교 기준(목표) bitrate. */
static int bxcan_btr_decode(uint32_t btr, uint32_t f_can, uint32_t bitrate, can_bt *out) {
    if (btr & BTR_RESERVED) return BT_EARG;
    return bt_make(f_can, bitrate, (btr & 0x3FFu) + 1, ((btr >> 16) & 15u) + 1, ((btr >> 20) & 7u) + 1,
                   ((btr >> 24) & 3u) + 1, &BXCAN_HW, out);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ===== 연습 1: Linux 식 선택 (drivers/net/can/dev/calc_bittiming.c 를 기억으로 단순화 — 확인 권장) =====
 *  - 목표 SP 가 0 이면 CiA 식 기본값: > 800k → 750, > 500k → 800, 그 외 875
 *  - tseg = (TSEG1max + TSEG2max)·2 + 1 부터 (TSEG1min + TSEG2min)·2 까지 내려가며 NBT = 1 + tseg/2,
 *    BRP = f / (NBT·R) + (tseg 홀수 ? 1 : 0)  (같은 NBT 로 내림/올림 두 번 시도)
 *  - 실제 bitrate 는 정수 나눗셈, 오차 = |R − 실제| (bit/s). 오차가 더 나쁘면 건너뜀, 같으면 SP 오차로 비교
 *  - SP 는 "목표 이하" 중 가장 가까운 것만 (TSEG2 = NBT − ⌊SP·NBT/1000⌋ 와 그 −1 두 후보)
 *  - NBT 하한 8 이 없다 (3 tq 까지), 동점이면 먼저 찾은 것 (= 큰 NBT) 유지 */
static int linux_like(uint32_t f, uint32_t R, uint32_t sp_nom, const bt_limits *lim, uint32_t *brp_o, uint32_t *t1_o,
                      uint32_t *t2_o, uint32_t *sp_o) {
    if (!sp_nom) sp_nom = R > 800000 ? 750 : R > 500000 ? 800 : 875;
    uint32_t best_be = UINT32_MAX, best_spe = UINT32_MAX, best_brp = 0, best_t1 = 0, best_t2 = 0, best_sp = 0;
    for (int tseg = (int)(lim->tseg1_max + lim->tseg2_max) * 2 + 1; tseg >= (int)(lim->tseg1_min + lim->tseg2_min) * 2; tseg--) {
        uint32_t ts = (uint32_t)tseg / 2, all = 1 + ts;
        uint32_t brp = f / (all * R) + (uint32_t)(tseg % 2);
        if (brp < lim->brp_min || brp > lim->brp_max) continue;
        uint32_t rate = f / (brp * all);
        uint32_t be = rate > R ? rate - R : R - rate;
        if (be > best_be) continue;
        if (be < best_be) best_spe = UINT32_MAX;
        uint32_t spe = UINT32_MAX, t1s = 0, t2s = 0, sps = 0;
        for (uint32_t i = 0; i <= 1; i++) {
            int64_t t2 = (int64_t)all - (int64_t)sp_nom * all / 1000 - i;
            if (t2 < (int64_t)lim->tseg2_min) t2 = lim->tseg2_min;
            if (t2 > (int64_t)lim->tseg2_max) t2 = lim->tseg2_max;
            int64_t t1 = (int64_t)ts - t2;
            if (t1 > (int64_t)lim->tseg1_max) { t1 = lim->tseg1_max; t2 = (int64_t)ts - t1; }
            if (t1 < 1) continue;
            uint32_t sp = (uint32_t)(1000 * (all - (uint32_t)t2) / all);
            uint32_t e = sp_nom > sp ? sp_nom - sp : sp - sp_nom;
            if (sp <= sp_nom && e < spe) { spe = e; t1s = (uint32_t)t1; t2s = (uint32_t)t2; sps = sp; }
        }
        if (spe >= best_spe) continue;
        best_spe = spe; best_be = be; best_brp = brp; best_t1 = t1s; best_t2 = t2s; best_sp = sps;
        if (be == 0 && spe == 0) break;
    }
    if (!best_brp) return BT_ENOSOL;
    *brp_o = best_brp; *t1_o = best_t1; *t2_o = best_t2; *sp_o = best_sp;
    return BT_OK;
}

int main(void) {
    can_bt b;
    bt_tol t;

    /* ---- 연습 1 ---- */
    static const uint32_t F[] = { 8000000, 16000000, 36000000, 42000000, 45000000, 48000000, 80000000 };
    static const uint32_t R[] = { 125000, 250000, 500000, 1000000 };
    int same = 0, diff = 0;
    for (int fi = 0; fi < 7; fi++)
        for (int ri = 0; ri < 4; ri++) {
            uint32_t sp_nom = R[ri] > 800000 ? 750 : R[ri] > 500000 ? 800 : 875;
            uint32_t lb, l1, l2, lsp;
            int r1 = linux_like(F[fi], R[ri], 0, &BXCAN_HW, &lb, &l1, &l2, &lsp);
            int r2 = bt_best(F[fi], R[ri], sp_nom, &BXCAN, 5000, &b);
            if (r1 == BT_OK && r2 == BT_OK && b.brp == lb && b.tseg1 == l1 && b.tseg2 == l2) same++;
            else {
                diff++;
                printf("ex1 %2u MHz %4uk SP %u: linux BRP %u NBT %u (1+%u+%u) SP %u.%u%% | ours BRP %u NBT %u (1+%u+%u) SP %u.%u%%\n",
                       F[fi] / 1000000, R[ri] / 1000, sp_nom, lb, 1 + l1 + l2, l1, l2, lsp / 10, lsp % 10,
                       b.brp, b.ntq, b.tseg1, b.tseg2, b.sp_permille / 10, b.sp_permille % 10);
            }
        }
    printf("ex1: linux-like == ours in %d/28, differ in %d\n", same, diff);
    /* 우리 명령 줄 1 ("bitrate R sample-point <우리 SP>") 을 커널이 받으면 같은 조합이 나오나 */
    int same2 = 0;
    for (int fi = 0; fi < 7; fi++)
        for (int ri = 0; ri < 4; ri++) {
            uint32_t lb2, l12, l22, lsp2;
            if (bt_best(F[fi], R[ri], 875, &BXCAN, 5000, &b) != BT_OK) continue;
            if (linux_like(F[fi], R[ri], b.sp_permille, &BXCAN_HW, &lb2, &l12, &l22, &lsp2) == BT_OK &&
                lb2 == b.brp && l12 == b.tseg1 && l22 == b.tseg2) same2++;
            else printf("ex1b %2u MHz %4uk sample-point 0.%03u: linux BRP %u NBT %u (1+%u+%u) SP %u.%u%% | ours BRP %u NBT %u (1+%u+%u)\n",
                        F[fi] / 1000000, R[ri] / 1000, b.sp_permille, lb2, 1 + l12 + l22, l12, l22, lsp2 / 10, lsp2 % 10,
                        b.brp, b.ntq, b.tseg1, b.tseg2);
        }
    printf("ex1b: kernel reproduces our 87.5%%-target pick from line 1 in %d/28\n", same2);
    CHECK(same2 == 28);
    uint32_t lb, l1, l2, lsp;
    CHECK(linux_like(16000000, 500000, 0, &BXCAN_HW, &lb, &l1, &l2, &lsp) == BT_OK && lb == 2 && l1 == 13 && l2 == 2 && lsp == 875);
    CHECK(linux_like(36000000, 1000000, 875, &BXCAN_HW, &lb, &l1, &l2, &lsp) == BT_OK && lb == 2 && l1 == 14 && l2 == 3 && lsp == 833);
    CHECK(bt_best(36000000, 1000000, 875, &BXCAN, 5000, &b) == BT_OK && b.brp == 2 && b.tseg1 == 15 && b.sp_permille == 889);
    CHECK(linux_like(4000000, 1000000, 0, &BXCAN_HW, &lb, &l1, &l2, &lsp) == BT_OK && lb == 1 && 1 + l1 + l2 == 4 && lsp == 750);
    CHECK(same == 27 && diff == 1);

    /* ---- 연습 2: 16 MHz 500k, 정확한 조합 중 df 최대 (왕복 지연 0 ns / 400 ns) ---- */
    can_bt list[64];
    size_t tot = 0;
    CHECK(bt_enumerate(16000000, 500000, &BXCAN, 0, list, 64, &tot) == BT_OK && tot == 14);
    uint32_t rt[2] = { 0, 400 }, best_df[2] = { 0, 0 };
    can_bt best_b[2];
    for (int k = 0; k < 2; k++) {
        for (size_t i = 0; i < tot; i++) {
            uint32_t prop = bt_prop_tq(16000000, list[i].brp, rt[k]);
            if (bt_tolerance(&list[i], prop, &t) != BT_OK) continue;
            if (t.df_ppm > best_df[k]) { best_df[k] = t.df_ppm; best_b[k] = list[i]; }
        }
        printf("ex2 t_rt %3u ns: max df %u ppm at BRP %u NBT %u (1+%u+%u) SJW %u SP %u.%u%%\n", rt[k], best_df[k],
               best_b[k].brp, best_b[k].ntq, best_b[k].tseg1, best_b[k].tseg2, best_b[k].sjw,
               best_b[k].sp_permille / 10, best_b[k].sp_permille % 10);
    }
    CHECK(best_df[0] == 15000 && best_b[0].ntq == 8 && best_b[0].tseg1 == 3);
    CHECK(best_df[1] == 12376 && best_b[1].ntq == 16 && best_b[1].tseg1 == 9);   /* 지연 400 ns: SP 62.5% 까지 내려가야 */

    /* ---- 연습 3: BTR 감식 ---- */
    can_bt x, y;
    CHECK(bxcan_btr_decode(0x001C0003u, 36000000, 500000, &x) == BT_OK && x.brp == 4 && x.ntq == 16 && x.sjw == 1);
    CHECK(x.err_ppm == 125000);                               /* 36 MHz / 64 = 562.5k: +12.5% */
    uint32_t f_design = 500000u * x.brp * x.ntq;              /* 이 BTR 로 500k 가 되는 클럭 */
    CHECK(f_design == 32000000);
    CHECK(bt_best(36000000, 500000, 875, &BXCAN, 5000, &y) == BT_OK && bxcan_btr(&y, 0, 0) == 0x00050008u);
    printf("ex3: BTR 0x001C0003 @36 MHz -> %lld ppm, designed for %u Hz; fix BTR 0x%08X\n",
           (long long)x.err_ppm, f_design, bxcan_btr(&y, 0, 0));

    /* ---- 연습 4: 네 bitrate 모두 오차 0, SP 정확히 87.5% 인 f_can ---- */
    int nf = 0, all8 = 1;
    char lst[200] = "";
    for (uint32_t mhz = 8; mhz <= 80; mhz++) {
        int ok = 1;
        for (int ri = 0; ri < 4 && ok; ri++)
            ok = bt_best(mhz * 1000000u, R[ri], 875, &BXCAN, 0, &b) == BT_OK && b.sp_permille == 875 && 8 * (1 + b.tseg1) == 7 * b.ntq;
        if (ok) { nf++; all8 &= mhz % 8 == 0; snprintf(lst + strlen(lst), sizeof lst - strlen(lst), " %u", mhz); }
    }
    printf("ex4: f_can (MHz) with exact 125k/250k/500k/1M at 87.5%%:%s (%d)\n", lst, nf);
    CHECK(nf == 10 && all8);

    /* ---- 연습 5: 버스 길이 예산. t_rt = 2·(L·5 ns/m) + 300 ns (두 노드의 트랜시버·컨트롤러 지연 합, 가정값) ---- */
    static const uint32_t R5[] = { 1000000, 500000, 250000, 125000 };
    uint32_t lmax_keep[4], lmax_min[4];
    for (int ri = 0; ri < 4; ri++) {
        bt_best(16000000, R5[ri], 875, &BXCAN, 5000, &b);
        uint32_t keep = 0, minps1 = 0;
        for (uint32_t L = 0; L <= 2000; L++) {
            uint32_t prop = bt_prop_tq(16000000, b.brp, 10 * L + 300);
            if (bt_tolerance(&b, prop, &t) != BT_OK) break;
            minps1 = L;
            if (t.ps1 >= t.ps2) keep = L;
        }
        lmax_keep[ri] = keep; lmax_min[ri] = minps1;
        printf("ex5 %4uk (16 MHz, %u tq, tq %.1f ns): PS1>=PS2 up to %u m, PS1>=1 up to %u m\n", R5[ri] / 1000, b.ntq,
               1e9 * b.brp / 16000000, keep, minps1);
    }
    CHECK(lmax_keep[0] == 38 && lmax_keep[1] == 107 && lmax_keep[2] == 245 && lmax_keep[3] == 520);
    CHECK(lmax_min[0] < lmax_min[1] && lmax_min[1] < lmax_min[2] && lmax_min[2] < lmax_min[3]);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
