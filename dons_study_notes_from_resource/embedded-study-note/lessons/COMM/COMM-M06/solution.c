/* COMM-M06 CAN 비트 타이밍 계산기: TQ, 세그먼트, 샘플 포인트
 *
 * Part A: 계산기 — BRP/TSEG1/TSEG2/SJW 로 비트 시간 하나를 만들고(bt_make), 목표 bitrate 에 맞는 조합을 모두
 *         열거하고(bt_enumerate), 정해진 규칙(bt_better)으로 최선을 고른다(bt_best). 비교는 정수 연산만 쓴다.
 * Part B: 경계·실패 — TSEG1/TSEG2/NBT 한계, 정확히 안 나누어지는 클럭(최선 오차 > 허용 → BT_ERATE),
 *         왕복 전파 지연 → PROP_SEG → PS1 분할(bt_prop_tq), 발진기 허용 오차 두 조건(bt_tolerance),
 *         클럭 편차를 주입한 재동기화 시뮬레이션(resync_on_edge + rx_sim)으로 조건 2 를 확인.
 * Part C: 펌웨어 출력 — STM32 bxCAN CAN_BTR 인코드/디코드, Linux `ip link set ... type can` 명령 문자열.
 *
 * 모델 정의 (bxCAN 류 — RM 확인 권장): BRP 1–1024, TSEG1 1–16, TSEG2 1–8, SJW 1–4, SJW ≤ TSEG2,
 *   NBT 8–25 tq (계산기 기본 한계. 레지스터 자체는 3 tq 까지 허용).
 *   tq = BRP / f_can,  NBT = 1 + TSEG1 + TSEG2 [tq],  bitrate = f_can / (BRP·NBT),  SP = (1 + TSEG1) / NBT.
 * 입력 범위 (모델 정의): 1 kHz ≤ f_can ≤ 1 GHz, 1 kbit/s ≤ bitrate ≤ 10 Mbit/s — 이 안에서 64비트가 넘치지 않는다.
 * 테스트: 손으로 검산한 설정들, 브루트포스와 비교하는 xorshift32 랜덤 3000 케이스, BTR 인코드/디코드 전수(524288).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
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

/* ---- 재동기화 시뮬레이션 (조건 2 확인용) ----
 * RX tq = 1000 tick. TX 비트 = round(NBT·1000·1e6 / (1e6 + δppm)) tick, δ = TX bitrate / RX bitrate − 1.
 * RX 는 tq 마다 (slot 끝에서) 버스를 한 번 본다. slot 0 = SYNC_SEG, 1..TSEG1, 그 뒤 TSEG2.
 * 샘플 포인트 = slot sp 의 끝 (처음엔 sp = TSEG1). 비트의 마지막 slot = end (처음엔 NBT − 1).
 * 첫 열성→우성 에지에서 hard sync, 그 뒤 열성→우성 에지로 재동기화 (샘플 포인트 사이 1회). */
typedef struct { int pos, sp, end, resynced; long bitno; } rx_bt;

/* 열성→우성 에지가 slot rx->pos (≥ 1) 에서 보였다 (rx->resynced == 0 일 때만 불린다). 규칙:
 *   pos ≤ sp (샘플 전): 위상 오차 e = +pos  → 샘플 포인트와 비트 끝을 min(e, SJW) 만큼 뒤로 (PS1 연장).
 *   pos > sp (샘플 후): e = pos − (end + 1) < 0 → |e| ≤ SJW 면 이 slot 이 다음 비트의 SYNC_SEG
 *                       (pos = 0, sp = TSEG1, end = NBT − 1, bitno++), 아니면 end 를 SJW 만큼 앞으로 (PS2 단축).
 *   어느 경우든 resynced = 1. */
static void resync_on_edge(rx_bt *rx, int tseg1, int ntq, int sjw) {
    if (rx->pos <= rx->sp) {
        int a = rx->pos < sjw ? rx->pos : sjw;
        rx->sp += a; rx->end += a;
    } else if (rx->end + 1 - rx->pos <= sjw) {
        rx->pos = 0; rx->sp = tseg1; rx->end = ntq - 1; rx->bitno++;
    } else {
        rx->end -= sjw;
    }
    rx->resynced = 1;
}

/* bits[0..nbits) 를 TX 가 보내고 (앞에 idle 3 비트), RX 가 b 의 설정으로 받는다. 샘플 오류 수를 돌려준다.
 * 오류 = n 번째 샘플이 TX 비트 n 밖에 떨어짐 (비트 슬립) 또는 값이 다름. 루프는 시간으로 끝난다.
 * 잘못된 설정 (NBT ≠ 1 + TSEG1 + TSEG2, SJW 범위 밖, |δ| ≥ 50%) → −1. */
static long rx_sim(const can_bt *b, int32_t delta_ppm, int64_t phase, const uint8_t *bits, int nbits) {
    const int64_t Qt = 1000;
    if (!b || !bits || nbits < 2 || b->ntq < 3 || b->ntq != 1 + b->tseg1 + b->tseg2 || b->sjw < 1 || b->sjw > b->tseg2 ||
        delta_ppm <= -500000 || delta_ppm >= 500000) return -1;
    int ntq = (int)b->ntq, tseg1 = (int)b->tseg1, sjw = (int)b->sjw;
    int64_t P = ((int64_t)ntq * Qt * 1000000 * 2 / (1000000 + delta_ppm) + 1) / 2;
    int64_t t0 = 3 * ntq * Qt + phase, t_end = t0 + (int64_t)(nbits - 1) * P;
    rx_bt rx = { 0, 0, 0, 0, 0 };
    int prev = 1, running = 0;
    long errors = 0;
    for (int64_t t = Qt; t < t_end; t += Qt) {
        int64_t ti = t < t0 ? -1 : (t - t0) / P;
        int lvl = ti < 0 || ti >= nbits ? 1 : bits[ti];
        int edge = prev == 1 && lvl == 0;
        prev = lvl;
        if (!running) {
            if (edge) { running = 1; rx.pos = 0; rx.sp = tseg1; rx.end = ntq - 1; rx.bitno = 0; rx.resynced = 0; }
            continue;
        }
        if (++rx.pos > rx.end) { rx.pos = 0; rx.sp = tseg1; rx.end = ntq - 1; rx.bitno++; }
        if (edge && rx.pos != 0 && !rx.resynced) resync_on_edge(&rx, tseg1, ntq, sjw);
        if (rx.pos == rx.sp) {
            if (ti != rx.bitno || ti < 0 || lvl != bits[ti]) errors++;
            rx.resynced = 0;
        }
    }
    return errors;
}

/* ================= Part C: 레지스터 값과 Linux 명령 ================= */

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

/* Linux iproute2 명령. 줄 1: bitrate + sample-point (커널이 자기 알고리즘으로 다시 고른다).
 * 줄 2 (tq 가 정수 ns 일 때만): tq / prop-seg / phase-seg1 / phase-seg2 / sjw 로 정확히 지정.
 * 반환: 쓴 길이 (NUL 제외) / BT_ESPACE (cap 부족, 잘린 문자열) / BT_EPROP / BT_EARG. */
static int linux_ip_cmd(char *buf, size_t cap, const char *ifname, const can_bt *b, uint32_t prop_tq) {
    if (!buf || !ifname || !b || b->f_can == 0) return BT_EARG;
    if (prop_tq >= b->tseg1) return BT_EPROP;
    int n1 = snprintf(buf, cap, "ip link set %s type can bitrate %u sample-point 0.%03u",
                      ifname, (unsigned)b->bitrate, (unsigned)b->sp_permille);
    if (n1 < 0) return BT_EARG;
    int n2 = 0;
    uint64_t tq_num = (uint64_t)b->brp * 1000000000u;
    if (tq_num % b->f_can == 0) {
        size_t off = (size_t)n1 < cap ? (size_t)n1 : cap;
        n2 = snprintf(buf + off, cap - off, "\nip link set %s type can tq %u prop-seg %u phase-seg1 %u phase-seg2 %u sjw %u",
                      ifname, (unsigned)(tq_num / b->f_can), (unsigned)prop_tq, (unsigned)(b->tseg1 - prop_tq),
                      (unsigned)b->tseg2, (unsigned)b->sjw);
        if (n2 < 0) return BT_EARG;
    }
    if ((size_t)n1 + (size_t)n2 >= cap) return BT_ESPACE;
    return n1 + n2;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static int is_cfg(const can_bt *b, uint32_t brp, uint32_t t1, uint32_t t2, uint32_t sjw) {
    return b->brp == brp && b->tseg1 == t1 && b->tseg2 == t2 && b->sjw == sjw;
}

static void bt_print(const char *tag, int rc, const can_bt *b) {
    if (rc != BT_OK && rc != BT_ERATE) { printf("%-24s rc=%d\n", tag, rc); return; }
    printf("%-24s BRP %4u  NBT %2u (1+%2u+%u)  SJW %u  SP %2u.%u%%  err %+lld ppm%s\n", tag, (unsigned)b->brp,
           (unsigned)b->ntq, (unsigned)b->tseg1, (unsigned)b->tseg2, (unsigned)b->sjw, (unsigned)(b->sp_permille / 10),
           (unsigned)(b->sp_permille % 10), (long long)b->err_ppm, rc == BT_ERATE ? "  (BT_ERATE)" : "");
}

/* 브루트포스 기준: 모든 (BRP, NBT, TSEG1) 을 bt_make + bt_better 로 비교. bitrate 오차가 현재 최선보다
 * 나쁜 (BRP, NBT) 는 정수 비교로 먼저 건너뛴다 (bt_best 의 floor/ceil 논리를 쓰지 않는 독립 경로). */
static int brute_best(uint32_t f, uint32_t R, uint32_t sp, const bt_limits *lim, can_bt *out) {
    can_bt best, c;
    int found = 0;
    for (uint32_t brp = lim->brp_min; brp <= lim->brp_max; brp++)
        for (uint32_t n = lim->ntq_min; n <= lim->ntq_max; n++) {
            uint64_t d = (uint64_t)brp * n;
            if (found) {
                uint64_t db = (uint64_t)best.brp * best.ntq;
                if (absdiff_u64(f, (uint64_t)R * d) * db > absdiff_u64(f, (uint64_t)R * db) * d) continue;
            }
            for (uint32_t t1 = lim->tseg1_min; t1 <= lim->tseg1_max && t1 + 1 < n; t1++) {
                uint32_t t2 = n - 1 - t1;
                if (bt_make(f, R, brp, t1, t2, min_u32(lim->sjw_max, t2), lim, &c) != BT_OK) continue;
                if (!found || bt_better(&c, &best, sp)) { best = c; found = 1; }
            }
        }
    if (found) *out = best;
    return found;
}

int main(void) {
    can_bt b = {0}, c = {0};
    int rc;

    /* ---- 주어진 도우미 ---- */
    CHECK(args_ok(16000000, 500000) && !args_ok(0, 500000) && !args_ok(16000000, 20000000));
    CHECK(rate_within(16000000, 500000, 32, 0) && !rate_within(14745600, 500000, 30, 5000) && rate_within(14745600, 500000, 30, 17000));

    /* ---- Part A: bt_make ---- */
    CHECK(bt_make(16000000, 500000, 2, 13, 2, 2, &BXCAN, &b) == BT_OK && b.ntq == 16 && b.err_ppm == 0 && b.sp_permille == 875);
    CHECK(bt_make(14745600, 500000, 2, 12, 2, 2, &BXCAN, &b) == BT_OK && b.ntq == 15 && b.err_ppm == -16960 && b.sp_permille == 867);
    CHECK(bt_make(16000000, 500000, 2, 17, 2, 2, &BXCAN, &b) == BT_EARG);    /* TSEG1 > 16 */
    CHECK(bt_make(16000000, 500000, 2, 13, 2, 3, &BXCAN, &b) == BT_EARG);    /* SJW > TSEG2 */
    CHECK(bt_make(16000000, 500000, 0, 13, 2, 1, &BXCAN, &b) == BT_EARG);    /* BRP 0 */
    CHECK(bt_make(16000000, 1000000, 4, 2, 1, 1, &BXCAN, &b) == BT_EARG);    /* NBT 4 < 8 (계산기 한계) */
    CHECK(bt_make(16000000, 1000000, 4, 2, 1, 1, &BXCAN_HW, &b) == BT_OK && b.ntq == 4 && b.sp_permille == 750);
    CHECK(bt_make(16000000, 0, 2, 13, 2, 2, &BXCAN, &b) == BT_EARG);
    CHECK(bt_make(8000000, 1000000, 1, 6, 1, 1, &BXCAN, &b) == BT_OK && b.ntq == 8 && b.sp_permille == 875);
    CHECK(bt_make(80000000, 1000000, 6, 10, 2, 2, &BXCAN, &b) == BT_OK && b.err_ppm == 25641);     /* 80M/78 = 1025641 → +2.56% */

    /* ---- Part A: bt_better 규칙 ---- */
    bt_make(14745600, 500000, 2, 12, 2, 2, &BXCAN, &b);       /* −1.70% */
    bt_make(14745600, 500000, 3, 6, 2, 2, &BXCAN, &c);        /* NBT 9, BRP 3 → 27 → +9.2% */
    CHECK(bt_better(&b, &c, 875) == 1 && bt_better(&c, &b, 875) == 0);                 /* 1) 오차 작은 쪽 */
    bt_make(16000000, 500000, 2, 13, 2, 2, &BXCAN, &b);       /* 87.5% */
    bt_make(16000000, 500000, 2, 12, 3, 3, &BXCAN, &c);       /* 81.25% */
    CHECK(bt_better(&b, &c, 875) == 1 && bt_better(&c, &b, 800) == 1);                 /* 2) SP 가까운 쪽 */
    bt_make(16000000, 500000, 4, 6, 1, 1, &BXCAN, &c);        /* 8 tq, 87.5%, 오차 0 */
    CHECK(bt_better(&b, &c, 875) == 1 && bt_better(&c, &b, 875) == 0);                 /* 3) tq 많은 쪽 */
    bt_make(48000000, 500000, 8, 9, 2, 2, &BXCAN, &b);        /* 12 tq: 10/12 = 83.3% */
    bt_make(48000000, 500000, 8, 10, 1, 1, &BXCAN, &c);       /* 12 tq: 11/12 = 91.7% — 87.5 에서 같은 거리 */
    CHECK(bt_better(&b, &c, 875) == 1 && bt_better(&c, &b, 875) == 0);                 /* 5) 낮은 SP */
    CHECK(bt_better(&b, &b, 875) == 0);                                                /* 엄격: 자기 자신보다 낫지 않다 */

    /* ---- Part A: bt_best — 손으로 검산한 표 ---- */
    struct { uint32_t f, R, sp; int rc; uint32_t brp, t1, t2, sjw; const char *why; } T[] = {
        { 16000000,  500000, 875, BT_OK, 2, 13, 2, 2, "16 MHz 500k: 16 tq 정확히 87.5%" },
        { 16000000, 1000000, 875, BT_OK, 1, 13, 2, 2, "16 MHz 1M" },
        {  8000000, 1000000, 875, BT_OK, 1,  6, 1, 1, "8 MHz 1M: 8 tq 뿐" },
        {  8000000, 1000000, 800, BT_OK, 1,  5, 2, 2, "8 MHz 1M @80%: 75% 가 87.5% 보다 가깝다" },
        { 42000000, 1000000, 875, BT_OK, 3, 11, 2, 2, "42 MHz 1M: 14 tq 85.7%" },
        { 42000000,  500000, 875, BT_OK, 6, 11, 2, 2, "42 MHz 500k: 21 tq 는 TSEG1 한계로 탈락" },
        { 45000000,  500000, 875, BT_OK, 6, 12, 2, 2, "45 MHz 500k: 15 tq 86.7%" },
        { 45000000, 1000000, 875, BT_OK, 3, 12, 2, 2, "45 MHz 1M" },
        { 48000000, 1000000, 875, BT_OK, 3, 13, 2, 2, "48 MHz 1M: 24 tq 는 TSEG1 > 16" },
        { 25000000, 1000000, 875, BT_OK, 1, 16, 8, 4, "25 MHz 1M: 25 tq, 16/8 강제 → 68%" },
        { 14745600,  500000, 875, BT_ERATE, 2, 12, 2, 2, "UART 크리스털 14.7456 MHz: −1.70%" },
        {  4000000, 1000000, 875, BT_ERATE, 1,  6, 1, 1, "4 MHz 1M: 8 tq 도 못 만든다 → 500k" },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        memset(&b, 0, sizeof b);
        rc = bt_best(T[i].f, T[i].R, T[i].sp, &BXCAN, 5000, &b);
        CHECK(rc == T[i].rc && is_cfg(&b, T[i].brp, T[i].t1, T[i].t2, T[i].sjw));
        char tag[40];
        snprintf(tag, sizeof tag, "%5.2f MHz %4uk @%u", T[i].f / 1e6, (unsigned)(T[i].R / 1000), (unsigned)T[i].sp);
        bt_print(tag, rc, &b);
    }
    CHECK(bt_best(16000000, 0, 875, &BXCAN, 5000, &b) == BT_EARG);
    CHECK(bt_best(16000000, 500000, 0, &BXCAN, 5000, &b) == BT_EARG);
    CHECK(bt_best(16000000, 500000, 1000, &BXCAN, 5000, &b) == BT_EARG);
    CHECK(bt_best(0, 500000, 875, &BXCAN, 5000, &b) == BT_EARG);
    bt_limits nosol = BXCAN; nosol.ntq_min = 26; nosol.ntq_max = 30;      /* 1+16+8 = 25 를 넘는 NBT 만 허용 */
    CHECK(bt_best(16000000, 500000, 875, &nosol, 5000, &b) == BT_ENOSOL);

    /* ---- Part A: bt_enumerate ---- */
    can_bt list[20];
    size_t total = 99;
    CHECK(bt_enumerate(16000000, 500000, &BXCAN, 0, list, 20, &total) == BT_OK && total == 14);
    int all_exact = 1;
    for (size_t i = 0; i < total && i < 20; i++)
        all_exact &= list[i].err_ppm == 0 && list[i].brp * list[i].ntq == 32;
    CHECK(all_exact);
    CHECK(total == 14 && list[0].brp == 2 && list[0].ntq == 16 && list[0].tseg1 == 7 && list[13].brp == 4 && list[13].tseg1 == 6);
    total = 0;
    CHECK(bt_enumerate(16000000, 500000, &BXCAN, 0, list, 5, &total) == BT_ESPACE && total == 14);
    CHECK(bt_enumerate(16000000, 500000, &BXCAN, 0, NULL, 0, &total) == BT_ESPACE && total == 14);
    CHECK(bt_enumerate(16000000, 500000, &BXCAN, 0, NULL, 5, &total) == BT_EARG);
    total = 99;
    CHECK(bt_enumerate(14745600, 500000, &BXCAN, 5000, list, 20, &total) == BT_OK && total == 0);  /* ±0.5% 안 조합 없음 */

    /* ---- Part A: 랜덤 — bt_best == 브루트포스, 그리고 enumerate 안에 있다 ---- */
    static const uint32_t RATES[] = { 10000, 20000, 33333, 50000, 83333, 100000, 125000, 250000, 500000, 800000, 1000000 };
    int agree = 0, inlist = 0, n_enum = 0, n_rand = 3000;
    for (int k = 0; k < n_rand; k++) {
        uint32_t f = (xorshift32() & 1) ? 1000000u * (4 + xorshift32() % 97) : 4000000u + xorshift32() % 96000001u;
        uint32_t R = (xorshift32() % 4) ? RATES[xorshift32() % 11] : 1000u + xorshift32() % 999001u;
        uint32_t sp = 600 + xorshift32() % 321;
        can_bt x = {0}, y = {0};
        int r1 = bt_best(f, R, sp, &BXCAN, 5000, &x);
        int ok = brute_best(f, R, sp, &BXCAN, &y);
        if ((r1 == BT_OK || r1 == BT_ERATE) && ok && is_cfg(&x, y.brp, y.tseg1, y.tseg2, y.sjw)) agree++;
        if (k % 10 == 0 && r1 == BT_OK) {   /* 최선의 오차를 한도로 열거하면 그 최선이 목록에 있다 */
            n_enum++;
            uint64_t d = (uint64_t)x.brp * x.ntq;
            uint32_t lim = (uint32_t)((absdiff_u64(f, (uint64_t)R * d) * 1000000u + (uint64_t)R * d - 1) / ((uint64_t)R * d));
            static can_bt big[4096];
            size_t tot = 0;
            int r2 = bt_enumerate(f, R, &BXCAN, lim, big, 4096, &tot);
            int seen = 0;
            for (size_t i = 0; r2 == BT_OK && i < tot; i++) seen |= is_cfg(&big[i], x.brp, x.tseg1, x.tseg2, x.sjw);
            inlist += seen;
        }
    }
    CHECK(agree == n_rand);
    CHECK(n_enum > 0 && inlist == n_enum);
    printf("random: bt_best == brute force in %d/%d, best found by enumerate in %d/%d\n", agree, n_rand, inlist, n_enum);

    /* ---- Part B: PROP 와 허용 오차 ---- */
    bt_tol t = {0};
    CHECK(bt_prop_tq(16000000, 2, 700) == 6);       /* tq 125 ns, 700/125 = 5.6 → 6 */
    CHECK(bt_prop_tq(16000000, 2, 750) == 6);       /* 딱 나누어지면 올림 없음 */
    CHECK(bt_prop_tq(16000000, 2, 751) == 7);
    CHECK(bt_prop_tq(16000000, 2, 0) == 0);
    CHECK(bt_prop_tq(80000000, 5, 500) == 8);       /* tq 62.5 ns, 500/62.5 = 8 */
    bt_make(16000000, 500000, 2, 13, 2, 2, &BXCAN, &b);
    CHECK(bt_tolerance(&b, 6, &t) == BT_OK && t.ps1 == 7 && t.ps2 == 2 && t.df1_ppm == 4854 && t.df2_ppm == 6250 &&
          t.df_ppm == 4854 && t.limiter == 1);
    bt_make(16000000, 500000, 2, 13, 2, 1, &BXCAN, &c);        /* SJW 1 이면 조건 2 가 이긴다 */
    CHECK(bt_tolerance(&c, 6, &t) == BT_OK && t.df2_ppm == 3125 && t.df_ppm == 3125 && t.limiter == 2);
    CHECK(bt_tolerance(&b, 12, &t) == BT_OK && t.ps1 == 1 && t.df1_ppm == 2427 && t.limiter == 1);  /* PS1 1 로 반토막 */
    CHECK(bt_tolerance(&b, 13, &t) == BT_EPROP);                /* PS1 = 0 */
    bt_make(8000000, 1000000, 1, 6, 1, 1, &BXCAN, &c);          /* 8 tq 도 같은 비율이면 같은 허용치 */
    CHECK(bt_tolerance(&c, 4, &t) == BT_OK && t.df1_ppm == 4854 && t.df2_ppm == 6250);
    bt_make(25000000, 1000000, 1, 16, 8, 4, &BXCAN, &c);        /* 68%: PS2 가 커서 오히려 관대하다 */
    CHECK(bt_tolerance(&c, 4, &t) == BT_OK && t.df1_ppm == 12618 && t.df2_ppm == 8000 && t.limiter == 2);

    /* ---- Part B: 클럭 편차 주입 — 재동기화가 조건 2 를 지키는가 ---- */
    static uint8_t pat[2000];
    for (int i = 0; i < 2000; i++) pat[i] = (uint8_t)((i / 5) & 1);   /* 00000 11111 ...: 에지 10 비트마다 */
    struct { uint32_t f, R, brp, t1, t2, sjw; } RS[] = {
        { 16000000, 500000, 2, 13, 2, 2 }, { 16000000, 500000, 2, 13, 2, 1 }, { 8000000, 1000000, 1, 6, 1, 1 },
        { 25000000, 1000000, 1, 16, 8, 4 }, { 16000000, 500000, 2, 11, 4, 4 },
    };
    int rs_ok = 0;
    for (int i = 0; i < 5; i++) {
        bt_make(RS[i].f, RS[i].R, RS[i].brp, RS[i].t1, RS[i].t2, RS[i].sjw, &BXCAN_HW, &b);
        bt_tolerance(&b, 0, &t);
        int32_t lim = (int32_t)(2 * t.df2_ppm);            /* 두 노드 상대 편차 = 2·df */
        long in_p = 0, in_m = 0, out_p = 0, out_m = 0;
        for (int ph = 0; ph < 20; ph++) {
            in_p += rx_sim(&b, lim * 95 / 100, ph * 50, pat, 2000);
            in_m += rx_sim(&b, -lim * 95 / 100, ph * 50, pat, 2000);
            out_p += rx_sim(&b, lim * 110 / 100, ph * 50, pat, 2000);
            out_m += rx_sim(&b, -lim * 110 / 100, ph * 50, pat, 2000);
        }
        int ok = in_p == 0 && in_m == 0 && out_p > 0 && out_m > 0;
        rs_ok += ok;
        printf("resync NBT %2u SJW %u: 2*df2 = %5.2f%%  errors @0.95x: %ld/%ld  @1.10x: %ld/%ld\n", (unsigned)b.ntq,
               (unsigned)b.sjw, lim / 1e4, in_p, in_m, out_p, out_m);
    }
    CHECK(rs_ok == 5);
    bt_make(16000000, 500000, 2, 13, 2, 2, &BXCAN, &b);
    CHECK(rx_sim(&b, 0, 300, pat, 2000) == 0);
    long sjw2 = rx_sim(&b, 10000, 300, pat, 2000);                       /* +1.0%: SJW 2 는 견딘다 */
    bt_make(16000000, 500000, 2, 13, 2, 1, &BXCAN, &c);
    long sjw1 = rx_sim(&c, 10000, 300, pat, 2000);                       /* SJW 1 은 0.625% 까지 */
    CHECK(sjw2 == 0 && sjw1 > 0);

    /* ---- Part C: bxCAN BTR ---- */
    bt_make(16000000, 500000, 2, 13, 2, 2, &BXCAN, &b);
    CHECK(bxcan_btr(&b, 0, 0) == 0x011C0001u);
    CHECK(bxcan_btr(&b, 1, 1) == 0xC11C0001u);
    bt_make(42000000, 500000, 6, 11, 2, 2, &BXCAN, &c);
    CHECK(bxcan_btr(&c, 0, 0) == 0x011A0005u);
    CHECK(bxcan_btr_decode(0x011C0001u, 16000000, 500000, &c) == BT_OK && is_cfg(&c, 2, 13, 2, 2) && c.err_ppm == 0);
    CHECK(bxcan_btr_decode(0x011C0001u | (1u << 12), 16000000, 500000, &c) == BT_EARG);  /* 예약 비트 */
    CHECK(bxcan_btr_decode(0x03000000u, 16000000, 500000, &c) == BT_EARG);   /* SJW 4 > TS2 1 */
    long bad = 0, n_ok = 0;
    for (uint32_t brp = 1; brp <= 1024; brp++)
        for (uint32_t t1 = 1; t1 <= 16; t1++)
            for (uint32_t t2 = 1; t2 <= 8; t2++)
                for (uint32_t sjw = 1; sjw <= 4; sjw++) {
                    can_bt in = { 0 }, o = { 0 };
                    in.brp = brp; in.tseg1 = t1; in.tseg2 = t2; in.sjw = sjw;
                    uint32_t r = bxcan_btr(&in, (int)(brp & 1), (int)(t1 & 1));
                    int d = bxcan_btr_decode(r, 80000000, 500000, &o);
                    int want_ok = sjw <= t2;
                    if (want_ok ? (d != BT_OK || !is_cfg(&o, brp, t1, t2, sjw) || o.ntq != 1 + t1 + t2) : d != BT_EARG) bad++;
                    else n_ok += want_ok;
                }
    CHECK(bad == 0 && n_ok == 1024L * 16 * (8 + 7 + 6 + 5));
    printf("BTR encode/decode: %ld valid round trips, %ld mismatches\n", n_ok, bad);

    /* ---- Part C: Linux 명령 ---- */
    char cmd[200];
    bt_make(16000000, 500000, 2, 13, 2, 2, &BXCAN, &b);
    rc = linux_ip_cmd(cmd, sizeof cmd, "can0", &b, 6);
    CHECK(rc > 0 && strcmp(cmd, "ip link set can0 type can bitrate 500000 sample-point 0.875\n"
                                "ip link set can0 type can tq 125 prop-seg 6 phase-seg1 7 phase-seg2 2 sjw 2") == 0);
    int need = rc;
    CHECK(linux_ip_cmd(cmd, (size_t)need, "can0", &b, 6) == BT_ESPACE);      /* NUL 자리 없음 */
    CHECK(linux_ip_cmd(cmd, (size_t)need + 1, "can0", &b, 6) == need);
    CHECK(linux_ip_cmd(cmd, sizeof cmd, "can0", &b, 13) == BT_EPROP);
    bt_make(36000000, 500000, 4, 15, 2, 2, &BXCAN, &c);                     /* tq = 111.1 ns → 줄 1 만 */
    rc = linux_ip_cmd(cmd, sizeof cmd, "can1", &c, 5);
    CHECK(rc > 0 && strcmp(cmd, "ip link set can1 type can bitrate 500000 sample-point 0.889") == 0);
    bt_make(25000000, 1000000, 1, 16, 8, 4, &BXCAN, &c);                     /* 68% → "0.680" */
    rc = linux_ip_cmd(cmd, sizeof cmd, "can0", &c, 2);
    CHECK(rc > 0 && strncmp(cmd, "ip link set can0 type can bitrate 1000000 sample-point 0.680\n"
                                 "ip link set can0 type can tq 40 prop-seg 2 phase-seg1 14 phase-seg2 8 sjw 4", 200) == 0);

    /* ---- Part C: bring-up 요약 — 두 노드(STM32F4 APB1 42 MHz, 16 MHz MCU)를 500k 로 맞춘다 ---- */
    struct { const char *name; uint32_t f; } N[] = { { "node A (42 MHz APB1)", 42000000 }, { "node B (16 MHz)", 16000000 } };
    for (int i = 0; i < 2; i++) {
        can_bt x = {0};
        bt_tol tt = {0};
        int r = bt_best(N[i].f, 500000, 875, &BXCAN, 5000, &x);
        uint32_t prop = bt_prop_tq(N[i].f, x.brp, 600);           /* 가정: 왕복 지연 600 ns */
        int r2 = bt_tolerance(&x, prop, &tt);
        CHECK(r == BT_OK && r2 == BT_OK);
        bt_print(N[i].name, r, &x);
        printf("  BTR=0x%08X  PROP %u + PS1 %u, PS2 %u  df %u ppm (cond %d)\n", (unsigned)bxcan_btr(&x, 0, 0),
               (unsigned)tt.prop, (unsigned)tt.ps1, (unsigned)tt.ps2, (unsigned)tt.df_ppm, tt.limiter);
        if (linux_ip_cmd(cmd, sizeof cmd, "can0", &x, prop) > 0) printf("  %s\n", strchr(cmd, '\n') ? strchr(cmd, '\n') + 1 : cmd);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
