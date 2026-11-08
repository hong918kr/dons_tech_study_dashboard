/* SIV-M05 DDR 검증: Training 결과와 메모리 스트레스
 *   Part A — DDR PHY training 결과 레지스터(rank x byte lane x RD/WR DQS window) 해석 + 마진 판정
 *   Part B — 스트레스 패턴(solid / checkerboard / own-address / LFSR 주소·데이터 / row hammer)
 *            + 비트 단위 오류 맵(lane / DQ bit / bank / row / address)
 *   Part C — 야간 회귀: 보드 5장 training + stress → 진단(PIN / LANE / ROW / ADDR) 리포트
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 공통 도구 ======================= */
typedef struct {                       /* SIV-B02 의 bus ops 를 축약 */
    uint32_t (*rd32)(uint32_t off);
    void     (*wr32)(uint32_t off, uint32_t v);
} bus_ops_t;

static unsigned popcount32(uint32_t x) { unsigned n = 0; while (x) { x &= x - 1u; n++; } return n; }

static uint32_t xorshift32(uint32_t *s) {          /* Marsaglia xorshift32, 주기 2^32-1 (seed != 0) */
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

static void fmt_bits(uint32_t m, char *out, size_t sz) {        /* 0x00220000 -> "[17,21]" */
    size_t len = 0;
    if (popcount32(m) > 6) { snprintf(out, sz, "[%u bits]", popcount32(m)); return; }
    len += (size_t)snprintf(out + len, sz - len, "[");
    for (unsigned b = 0, first = 1; b < 32; b++)
        if ((m >> b) & 1u) { len += (size_t)snprintf(out + len, sz - len, first ? "%u" : ",%u", b); first = 0; }
    snprintf(out + len, sz - len, "]");
}

/* ======================= Part A: DDR PHY training 결과 레지스터 (mock) ======================= */
#define NLANE 4u                       /* x32 = byte lane 4개 (DQ0-7, DQ8-15, DQ16-23, DQ24-31) */
#define NRANK 2u
enum { DIR_RD = 0, DIR_WR = 1 };
static const char *const DIR_NAME[2] = { "RD", "WR" };

#define PHY_TSTAT_OFF 0x008u   /* RO [0] done  [7:4] rank0 lane_err  [11:8] rank1 lane_err  [16] rank1 present */
#define PHY_CFG_OFF   0x00Cu   /* RO [7:0] taps_per_UI  [15:8] max_tap  [31:16] data rate (MT/s)               */
#define PHY_WIN_OFF(rk, ln, d) (0x100u + (rk) * 0x40u + (ln) * 0x10u + (d) * 4u) /* RO [7:0] L  [15:8] R  */
#define PHY_DLY_OFF(ln, d)     (0x200u + (ln) * 0x8u + (d) * 4u)               /* RO [7:0] 적용된 delay */
#define PHY_NWORDS (0x240u / 4u)

static uint32_t g_phy[PHY_NWORDS];
static uint32_t phy_rd32(uint32_t off) { return off < sizeof g_phy ? g_phy[off >> 2] : 0xDEADBEEFu; }
static void phy_wr32(uint32_t off, uint32_t v) { (void)off; (void)v; }      /* 결과 레지스터는 RO */
static const bus_ops_t PHY_BUS = { phy_rd32, phy_wr32 };

static void phy_set_win(unsigned rk, unsigned ln, unsigned d, unsigned l, unsigned r) {
    g_phy[PHY_WIN_OFF(rk, ln, d) >> 2] = (l & 0xFFu) | ((r & 0xFFu) << 8);
}
static void phy_set_dly(unsigned ln, unsigned d, unsigned t) { g_phy[PHY_DLY_OFF(ln, d) >> 2] = t & 0xFFu; }

/* 정상 보드: DDR4-3200, 64 tap/UI, delay line 0..127, 2 rank. lane 마다 보드 skew 가 조금씩 다르다 */
static void phy_reset_good(void) {
    static const uint8_t L0[NLANE][2] = { { 20, 24 }, { 23, 26 }, { 21, 22 }, { 25, 27 } };
    memset(g_phy, 0, sizeof g_phy);
    g_phy[PHY_TSTAT_OFF >> 2] = 1u | (1u << 16);
    g_phy[PHY_CFG_OFF >> 2] = 64u | (127u << 8) | (3200u << 16);
    for (unsigned ln = 0; ln < NLANE; ln++)
        for (unsigned d = 0; d < 2; d++) {
            unsigned l = L0[ln][d];
            phy_set_win(0, ln, d, l, l + 32u);            /* rank0 [L, L+32]    */
            phy_set_win(1, ln, d, l + 2u, l + 33u);       /* rank1 [L+2, L+33]  */
            phy_set_dly(ln, d, (l + 2u + l + 32u) / 2u);  /* 교집합 [L+2, L+32] 의 중앙 */
        }
}

typedef struct { bool valid; unsigned l, r; } win_t;
typedef enum { LS_OK, LS_WARN, LS_INCONC, LS_FAIL } lane_st_t;
static const char *const LS_NAME[4] = { "OK", "WARN", "INCONC", "FAIL" };
typedef struct { unsigned min_width_mui, min_margin_mui; } train_limits_t;

typedef struct {
    unsigned  lane, dir, nrank;
    win_t     eff;                          /* rank 교집합 = 공유 delay 가 실제로 쓸 수 있는 창 */
    unsigned  dly, width_t, width_mui, margin_t, margin_mui;
    uint32_t  width_fs;
    lane_st_t st;
    const char *why;
} lane_rep_t;

/* TODO (Part A-1): [7:0]=L, [15:8]=R 을 꺼내고, R > L 일 때만 유효한 창 */
static win_t win_decode(uint32_t reg) {
    win_t w = { false, reg & 0xFFu, (reg >> 8) & 0xFFu };
    w.valid = w.r > w.l;
    return w;
}

/* TODO (Part A-1): 두 창의 교집합. 하나라도 무효이거나 겹치지 않으면 무효 */
static win_t win_intersect(win_t a, win_t b) {
    win_t w = { false, a.l > b.l ? a.l : b.l, a.r < b.r ? a.r : b.r };
    w.valid = a.valid && b.valid && w.r > w.l;
    return w;
}

/* TODO (Part A-2): 한 lane·방향의 판정. 우선순위 FAIL > INCONC > WARN > OK.
 *   FAIL  : training 미완료 / lane_err 비트 / rank 창 비어 있음 / rank 창 교집합 없음 / delay 가 창 밖
 *   INCONC: 어느 rank 창이든 경계가 delay line 끝(0 또는 max_tap)에 닿음 → 진짜 경계를 못 찾음
 *   WARN  : 폭 < min_width_mui  또는  margin = min(dly-L, R-dly) < min_margin_mui
 *   mUI = taps * 1000 / taps_per_UI (내림 = 보수적), fs = taps * UI_fs / taps_per_UI, UI_fs = 1e9 / MT/s */
static lane_st_t check_lane(const bus_ops_t *phy, unsigned ln, unsigned d,
                            const train_limits_t *lim, lane_rep_t *o) {
    uint32_t cfg = phy->rd32(PHY_CFG_OFF), tstat = phy->rd32(PHY_TSTAT_OFF);
    unsigned tpu = cfg & 0xFFu, maxtap = (cfg >> 8) & 0xFFu, mts = cfg >> 16;
    bool clipped = false;
    memset(o, 0, sizeof *o);
    o->lane = ln; o->dir = d; o->nrank = ((tstat >> 16) & 1u) ? 2u : 1u;
    o->st = LS_FAIL;
    if (!(tstat & 1u)) { o->why = "training not done"; return o->st; }
    for (unsigned rk = 0; rk < o->nrank; rk++)
        if ((tstat >> (4u + 4u * rk + ln)) & 1u) { o->why = rk ? "lane_err (rank1)" : "lane_err (rank0)"; return o->st; }
    o->eff = (win_t){ true, 0, maxtap };
    for (unsigned rk = 0; rk < o->nrank; rk++) {
        win_t w = win_decode(phy->rd32(PHY_WIN_OFF(rk, ln, d)));
        if (!w.valid) { o->why = rk ? "rank1 window empty" : "rank0 window empty"; return o->st; }
        if (w.l == 0 || w.r >= maxtap) clipped = true;
        o->eff = win_intersect(o->eff, w);
    }
    if (!o->eff.valid) { o->why = "rank windows disjoint"; return o->st; }
    o->dly = phy->rd32(PHY_DLY_OFF(ln, d)) & 0xFFu;
    o->width_t = o->eff.r - o->eff.l;
    o->width_mui = o->width_t * 1000u / tpu;
    o->width_fs = (uint32_t)((uint64_t)o->width_t * (1000000000u / mts) / tpu);
    if (o->dly < o->eff.l || o->dly > o->eff.r) { o->why = "delay outside window"; return o->st; }
    o->margin_t = (o->dly - o->eff.l < o->eff.r - o->dly) ? o->dly - o->eff.l : o->eff.r - o->dly;
    o->margin_mui = o->margin_t * 1000u / tpu;
    if (clipped)                                   { o->st = LS_INCONC; o->why = "edge clipped at delay-line limit"; }
    else if (o->width_mui < lim->min_width_mui)    { o->st = LS_WARN;   o->why = "eye width below limit"; }
    else if (o->margin_mui < lim->min_margin_mui)  { o->st = LS_WARN;   o->why = "delay off-center (margin below limit)"; }
    else                                           { o->st = LS_OK;     o->why = "-"; }
    return o->st;
}

/* 8개(lane x RD/WR) 전부 판정. 가장 나쁜 상태를 돌려준다 */
static lane_st_t train_check_all(const bus_ops_t *phy, const train_limits_t *lim,
                                 lane_rep_t rep[NLANE * 2u], bool print) {
    lane_st_t worst = LS_OK;
    for (unsigned ln = 0; ln < NLANE; ln++)
        for (unsigned d = 0; d < 2; d++) {
            lane_rep_t *o = &rep[ln * 2u + d];
            lane_st_t s = check_lane(phy, ln, d, lim, o);
            if (s > worst) worst = s;
            if (print && s != LS_OK && !o->eff.valid)
                printf("    %-6s lane=%u dir=%s why=%s\n", LS_NAME[s], ln, DIR_NAME[d], o->why);
            else if (print && s != LS_OK)
                printf("    %-6s lane=%u dir=%s win=[%u,%u] width=%ut/%umUI(%" PRIu32 ".%" PRIu32 "ps) dly=%u "
                       "margin=%ut/%umUI why=%s\n", LS_NAME[s], ln, DIR_NAME[d], o->eff.l, o->eff.r,
                       o->width_t, o->width_mui, o->width_fs / 1000u, (o->width_fs % 1000u) / 100u,
                       o->dly, o->margin_t, o->margin_mui, o->why);
        }
    return worst;
}

/* ======================= Part B: DRAM mock (x32, 4 MiB) + 결함 주입 ======================= */
#define DRAM_BASE 0x80000000u
#define COL_BITS  10u                  /* 1024 word/row (4 KiB page)  */
#define BANK_BITS 3u                   /* 8 bank                       */
#define ROW_BITS  7u                   /* 128 row/bank (축소 모델)      */
#define NWORDS    (1u << (COL_BITS + BANK_BITS + ROW_BITS))   /* 1Mi word = 4 MiB */
#define NBANK     (1u << BANK_BITS)
#define NROW      (1u << ROW_BITS)
#define W_COL(w)  ((w) & ((1u << COL_BITS) - 1u))
#define W_BANK(w) (((w) >> COL_BITS) & (NBANK - 1u))
#define W_ROW(w)  ((w) >> (COL_BITS + BANK_BITS))
#define W_IDX(b, r, c) (((uint32_t)(r) << (COL_BITS + BANK_BITS)) | ((uint32_t)(b) << COL_BITS) | (uint32_t)(c))
#define HAMMER_TH      600u            /* 이웃 ACT 누적 임계 (축소 모델)              */
#define HAMMER_ITER    400u            /* aggressor 당 ACT 수: 한쪽 400 < 600 < 양쪽 800 */
#define REFRESH_EVERY  100000u         /* 이 접근 수마다 전체 refresh (tREFW 흉내)      */

typedef struct {
    uint32_t stuck_mask, stuck_val;    /* DQ 핀 stuck (읽기 경로: 볼/트레이스/PHY 비트)   */
    int      row_alias_bit;            /* -1: 없음. row 디코더가 row 비트 k 를 무시       */
    int      weak_bank, weak_row;      /* -1: 없음. hammer 에 약한 행                     */
    uint32_t weak_col, weak_mask;      /*   그 행에서 뒤집히는 셀들                        */
    int      xtalk_bit;                /* -1: 없음. 이웃 두 DQ 가 반대로 토글하면 이전 값에 머묾 */
    int      marg_lane;                /* -1: 없음. 읽기 마진 부족 lane                    */
    uint32_t marg_ppm, marg_seed;      /*   lane 바이트가 4비트 이상 토글한 읽기당 오류 확률 */
} dram_faults_t;

static const dram_faults_t NOFAULT = { 0, 0, -1, -1, -1, 0, 0, -1, -1, 0, 1 };

static uint32_t g_cell[NWORDS];
static struct {
    dram_faults_t f;
    int      open_row[NBANK];
    uint32_t disturb[NBANK][NROW];
    uint32_t nacc, weak_ref, last_bus, prng;
} g_dram;

static void dram_reset(const dram_faults_t *f) {
    memset(g_cell, 0, sizeof g_cell);
    memset(&g_dram, 0, sizeof g_dram);
    g_dram.f = *f;
    for (unsigned b = 0; b < NBANK; b++) g_dram.open_row[b] = -1;
    g_dram.prng = f->marg_seed ? f->marg_seed : 1u;
}

static uint32_t dram_map(uint32_t w) {                    /* 논리 word -> 물리 셀 (디코더) */
    if (g_dram.f.row_alias_bit >= 0) w &= ~(1u << (COL_BITS + BANK_BITS + (unsigned)g_dram.f.row_alias_bit));
    return w;
}

static void dram_touch(uint32_t p) {                      /* ACT / refresh / disturb 모델 */
    const dram_faults_t *f = &g_dram.f;
    unsigned b = W_BANK(p), r = W_ROW(p);
    if (++g_dram.nacc % REFRESH_EVERY == 0) memset(g_dram.disturb, 0, sizeof g_dram.disturb);
    if (g_dram.open_row[b] == (int)r) return;             /* row hit: ACT 없음 */
    g_dram.open_row[b] = (int)r;                          /* ACT: 이 행은 재충전, 이웃은 교란 */
    g_dram.disturb[b][r] = 0;
    if (r > 0) g_dram.disturb[b][r - 1]++;
    if (r + 1 < NROW) g_dram.disturb[b][r + 1]++;
    if (f->weak_row >= 0 && g_dram.disturb[f->weak_bank][f->weak_row] >= HAMMER_TH)
        g_cell[W_IDX(f->weak_bank, f->weak_row, f->weak_col)] = g_dram.weak_ref ^ f->weak_mask;
}

static void dram_wr32(uint32_t off, uint32_t v) {
    uint32_t p = dram_map(off >> 2);
    dram_touch(p);
    g_cell[p] = v;
    if (g_dram.f.weak_row >= 0 && p == W_IDX(g_dram.f.weak_bank, g_dram.f.weak_row, g_dram.f.weak_col))
        g_dram.weak_ref = v;
    g_dram.last_bus = v;
}

static uint32_t dram_rd32(uint32_t off) {
    const dram_faults_t *f = &g_dram.f;
    uint32_t p = dram_map(off >> 2), v, prev = g_dram.last_bus, t;
    dram_touch(p);
    v = (g_cell[p] & ~f->stuck_mask) | (f->stuck_val & f->stuck_mask);   /* 핀 stuck: 버스 자체가 고정 */
    g_dram.last_bus = v;
    t = prev ^ v;
    if (f->xtalk_bit > 0) {                               /* odd-mode crosstalk: victim 전이가 늦어 이전 값 샘플 */
        unsigned k = (unsigned)f->xtalk_bit;
        uint32_t vb = (v >> k) & 1u;
        if (((t >> k) & 7u) == 7u && ((v >> (k - 1u)) & 1u) != vb && ((v >> (k + 1u)) & 1u) != vb)
            v = (v & ~(1u << k)) | (prev & (1u << k));
    }
    if (f->marg_lane >= 0 && popcount32((t >> (8u * (unsigned)f->marg_lane)) & 0xFFu) >= 4u &&
        xorshift32(&g_dram.prng) % 1000000u < f->marg_ppm) {
        unsigned bit = (xorshift32(&g_dram.prng) % 10u < 7u) ? 1u : 5u;   /* 이 lane 에서 마진 최악인 두 비트 */
        v ^= 1u << (8u * (unsigned)f->marg_lane + bit);
    }
    return v;
}
static const bus_ops_t DRAM_BUS = { dram_rd32, dram_wr32 };

/* ======================= Part B: 오류 맵 ======================= */
enum { PAT_SOLID, PAT_CHECKER, PAT_OWNADDR, PAT_LFSR, PAT_HAMMER, NPAT };
static const char *const PAT_NAME[NPAT] = { "solid", "checker", "own-addr", "lfsr", "hammer" };
#define EM_LINES 4u

typedef struct {
    uint64_t reads, errors;
    uint32_t patcnt[NPAT];
    uint32_t failbits;                         /* 실패한 DQ 비트 OR                         */
    uint32_t rise[32], fall[32];               /* 비트별: exp0->act1 / exp1->act0 횟수       */
    uint32_t rowcnt[NBANK][NROW];              /* 논리 (bank,row) 별 오류 수                 */
    uint32_t oa_and, oa_or;                    /* own-address 패턴 오류의 diff AND / OR      */
    unsigned nline;
    char     line[EM_LINES][176];
} errmap_t;

static void em_init(errmap_t *em) { memset(em, 0, sizeof *em); em->oa_and = 0xFFFFFFFFu; }

/* TODO (Part B-4): 불일치 1건을 맵에 기록 — failbits, 비트별 rise/fall, (bank,row),
 * own-address 면 diff AND/OR, 앞의 EM_LINES 건은 리포트 줄:
 * "FAIL mem=DDR0 pat=<p> addr=0x%08X bank=%u row=%u col=%u exp=0x%08X act=0x%08X diff=0x%08X dq=[..]" */
static void errmap_record(errmap_t *em, unsigned pat, uint32_t off, uint32_t exp, uint32_t act) {
    uint32_t diff = exp ^ act, w = off >> 2;
    em->failbits |= diff;
    for (unsigned b = 0; b < 32; b++)
        if ((diff >> b) & 1u) { if ((exp >> b) & 1u) em->fall[b]++; else em->rise[b]++; }
    em->rowcnt[W_BANK(w)][W_ROW(w)]++;
    if (pat == PAT_OWNADDR) { em->oa_and &= diff; em->oa_or |= diff; }
    if (em->nline < EM_LINES) {
        char bits[40];
        fmt_bits(diff, bits, sizeof bits);
        snprintf(em->line[em->nline++], sizeof em->line[0],
                 "FAIL mem=DDR0 pat=%s addr=0x%08" PRIX32 " bank=%u row=%u col=%u exp=0x%08" PRIX32
                 " act=0x%08" PRIX32 " diff=0x%08" PRIX32 " dq=%s", PAT_NAME[pat], DRAM_BASE + off,
                 (unsigned)W_BANK(w), (unsigned)W_ROW(w), (unsigned)W_COL(w), exp, act, diff, bits);
    }
}

static void em_check(errmap_t *em, unsigned pat, uint32_t off, uint32_t exp, uint32_t act) {
    em->reads++;
    if (act != exp) { em->errors++; em->patcnt[pat]++; errmap_record(em, pat, off, exp, act); }
}

/* ======================= Part B: 스트레스 패턴 ======================= */
static void pat_solid(const bus_ops_t *m, errmap_t *em) {          /* 예시: all-0 다음 all-1 */
    static const uint32_t V[2] = { 0x00000000u, 0xFFFFFFFFu };
    for (unsigned k = 0; k < 2; k++) {
        for (uint32_t w = 0; w < NWORDS; w++) m->wr32(w << 2, V[k]);
        for (uint32_t w = 0; w < NWORDS; w++) em_check(em, PAT_SOLID, w << 2, V[k], m->rd32(w << 2));
    }
}

static void pat_own_addr(const bus_ops_t *m, errmap_t *em) {       /* 예시: data = 자기 주소, 다음 ~주소 */
    for (unsigned k = 0; k < 2; k++) {
        uint32_t inv = k ? 0xFFFFFFFFu : 0u;
        for (uint32_t w = 0; w < NWORDS; w++) m->wr32(w << 2, (DRAM_BASE + (w << 2)) ^ inv);
        for (uint32_t w = 0; w < NWORDS; w++)
            em_check(em, PAT_OWNADDR, w << 2, (DRAM_BASE + (w << 2)) ^ inv, m->rd32(w << 2));
    }
}

/* TODO (Part B-1): checkerboard — (row ^ col) 가 홀수면 0xAAAAAAAA, 짝수면 0x55555555 */
static uint32_t checker_word(uint32_t w) {
    return ((W_ROW(w) ^ W_COL(w)) & 1u) ? 0xAAAAAAAAu : 0x55555555u;
}

/* TODO (Part B-1): checker_word 로 전체 쓰기→읽기, 그 다음 반전(~) 으로 한 번 더 */
static void pat_checker(const bus_ops_t *m, errmap_t *em) {
    for (unsigned k = 0; k < 2; k++) {
        uint32_t inv = k ? 0xFFFFFFFFu : 0u;
        for (uint32_t w = 0; w < NWORDS; w++) m->wr32(w << 2, checker_word(w) ^ inv);
        for (uint32_t w = 0; w < NWORDS; w++) em_check(em, PAT_CHECKER, w << 2, checker_word(w) ^ inv, m->rd32(w << 2));
    }
}

/* TODO (Part B-2): 20비트 Galois LFSR, 다항식 x^20 + x^17 + 1 (오른쪽 시프트, 마스크 0x90000).
 * 상태 1..2^20-1 을 한 번씩 돈다 (0 은 고정점이라 절대 안 나온다) */
static uint32_t lfsr20_next(uint32_t s) {
    uint32_t lsb = s & 1u;
    s >>= 1;
    if (lsb) s ^= 0x90000u;
    return s;
}

/* TODO (Part B-2): 주소 순서 = word 0 먼저, 그다음 LFSR 상태 1 부터 다시 1 이 될 때까지.
 * 데이터 = xorshift32(seed) 스트림. 읽기 때는 seed·LFSR 을 처음부터 다시 돌려 기대값을 재생성 (저장 안 함) */
static void pat_lfsr(const bus_ops_t *m, errmap_t *em, uint32_t seed) {
    uint32_t ds = seed, s = 1u, e;
    m->wr32(0, xorshift32(&ds));
    do { m->wr32(s << 2, xorshift32(&ds)); s = lfsr20_next(s); } while (s != 1u);
    ds = seed;
    e = xorshift32(&ds);
    em_check(em, PAT_LFSR, 0, e, m->rd32(0));
    do { e = xorshift32(&ds); em_check(em, PAT_LFSR, s << 2, e, m->rd32(s << 2)); s = lfsr20_next(s); } while (s != 1u);
}

/* TODO (Part B-3): row hammer 유사 접근.
 *   1) row stripe 로 채움: 짝수 row = 0xFFFFFFFF, 홀수 row = 0x00000000
 *   2) bank 마다 victim v = 1..NROW-2 에 대해 aggressor (v-1, col0) 와 (v+1, col0) 를 번갈아 n 번씩 읽기(+확인)
 *      → 같은 bank 의 다른 row 를 번갈아 열므로 매 접근이 ACT
 *   3) 전체를 다시 읽어 확인 */
static uint32_t stripe_word(uint32_t w) { return (W_ROW(w) & 1u) ? 0x00000000u : 0xFFFFFFFFu; }
static void pat_hammer(const bus_ops_t *m, errmap_t *em, unsigned n) {
    for (uint32_t w = 0; w < NWORDS; w++) m->wr32(w << 2, stripe_word(w));
    for (unsigned b = 0; b < NBANK; b++)
        for (unsigned v = 1; v + 1u < NROW; v++) {
            uint32_t a0 = W_IDX(b, v - 1u, 0), a1 = W_IDX(b, v + 1u, 0);
            for (unsigned i = 0; i < n; i++) {
                em_check(em, PAT_HAMMER, a0 << 2, stripe_word(a0), m->rd32(a0 << 2));
                em_check(em, PAT_HAMMER, a1 << 2, stripe_word(a1), m->rd32(a1 << 2));
            }
        }
    for (uint32_t w = 0; w < NWORDS; w++) em_check(em, PAT_HAMMER, w << 2, stripe_word(w), m->rd32(w << 2));
}

static void run_pattern(unsigned p, const bus_ops_t *m, errmap_t *em, uint32_t seed) {
    switch (p) {
    case PAT_SOLID:   pat_solid(m, em); break;
    case PAT_CHECKER: pat_checker(m, em); break;
    case PAT_OWNADDR: pat_own_addr(m, em); break;
    case PAT_LFSR:    pat_lfsr(m, em, seed); break;
    default:          pat_hammer(m, em, HAMMER_ITER); break;
    }
}

static void print_errmap(const errmap_t *em) {
    printf("    reads=%" PRIu64 " errors=%" PRIu64 " failbits=0x%08" PRIX32 "  by pattern:", em->reads, em->errors, em->failbits);
    for (unsigned p = 0; p < NPAT; p++) printf(" %s=%" PRIu32, PAT_NAME[p], em->patcnt[p]);
    printf("\n");
    for (unsigned ln = 0; ln < NLANE; ln++) {
        uint32_t lb = (em->failbits >> (8u * ln)) & 0xFFu;
        if (!lb) continue;
        printf("    lane%u(DQ%u-%u):", ln, 8u * ln, 8u * ln + 7u);
        if (popcount32(lb) > 3u) {                       /* 비트가 많으면 lane 합계만 */
            uint64_t sum = 0;
            for (unsigned b = 8u * ln; b < 8u * ln + 8u; b++) sum += em->rise[b] + em->fall[b];
            printf(" %u DQ bits, %" PRIu64 " bit-errors\n", popcount32(lb), sum);
            continue;
        }
        for (unsigned b = 8u * ln; b < 8u * ln + 8u; b++)
            if (em->rise[b] + em->fall[b]) printf(" DQ%u=%" PRIu32 "(0->1:%" PRIu32 ",1->0:%" PRIu32 ")",
                                                  b, em->rise[b] + em->fall[b], em->rise[b], em->fall[b]);
        printf("\n");
    }
    for (unsigned i = 0; i < em->nline; i++) printf("    %s\n", em->line[i]);
}

/* ======================= Part C: 진단 ======================= */
typedef enum { DX_PASS, DX_PIN, DX_ROW_CELL, DX_LANE_MARGIN, DX_ADDR, DX_UNKNOWN } dx_kind_t;
static const char *const DX_NAME[] = { "PASS", "PIN", "ROW_CELL", "LANE_MARGIN", "ADDR", "UNKNOWN" };

typedef struct {
    dx_kind_t kind;
    int  lane, bit, bank, row, abit, stuck_val;
    bool hammer_only, corroborated;
} diag_t;

/* TODO (Part C): 규칙 기반 진단. 순서가 중요하다.
 *   1) errors == 0                                              → PASS
 *   2) own-address 오류의 diff 가 전부 같은 1비트 && failbits 가 3개 이상 lane 에 걸침
 *      (= word 전체가 다른 주소의 데이터로 바뀜)                → ADDR (abit = 그 바이트 주소 비트)
 *   3) failbits 가 1비트 && 오류 난 (bank,row) 가 3개 이상     → PIN  (rise 만 있으면 stuck-at-1, fall 만 → 0)
 *   4) 오류 난 (bank,row) 가 2개 이하                           → ROW_CELL (최다 bank,row; hammer 에서만이면 hammer_only)
 *   5) failbits 가 한 lane 안                                   → LANE_MARGIN
 *   6) 그 외                                                    → UNKNOWN
 *   PIN / LANE_MARGIN 은 그 lane 의 training 판정(RD 또는 WR)이 OK 가 아니면 corroborated */
static diag_t diagnose(const errmap_t *em, const lane_rep_t tr[NLANE * 2u]) {
    diag_t d = { DX_UNKNOWN, -1, -1, -1, -1, -1, -1, false, false };
    unsigned nrows = 0, best = 0;
    if (em->errors == 0) { d.kind = DX_PASS; return d; }
    unsigned nlanes = 0;
    for (unsigned ln = 0; ln < NLANE; ln++) nlanes += ((em->failbits >> (8u * ln)) & 0xFFu) != 0;
    if (em->patcnt[PAT_OWNADDR] && em->oa_and == em->oa_or && popcount32(em->oa_or) == 1u && nlanes >= 3u) {
        d.kind = DX_ADDR;
        for (unsigned b = 0; b < 32; b++) if ((em->oa_or >> b) & 1u) d.abit = (int)b;
        return d;
    }
    for (unsigned b = 0; b < NBANK; b++)
        for (unsigned r = 0; r < NROW; r++)
            if (em->rowcnt[b][r]) {
                nrows++;
                if (em->rowcnt[b][r] > best) { best = em->rowcnt[b][r]; d.bank = (int)b; d.row = (int)r; }
            }
    if (popcount32(em->failbits) == 1u && nrows >= 3u) {
        d.kind = DX_PIN;
        for (unsigned b = 0; b < 32; b++) if ((em->failbits >> b) & 1u) d.bit = (int)b;
        d.lane = d.bit / 8;
        d.stuck_val = em->fall[d.bit] == 0 ? 1 : (em->rise[d.bit] == 0 ? 0 : -1);
    } else if (nrows <= 2u) {
        d.kind = DX_ROW_CELL;
        d.hammer_only = em->patcnt[PAT_HAMMER] == em->errors;
        return d;
    } else {
        for (unsigned ln = 0; ln < NLANE; ln++)
            if ((em->failbits & ~(0xFFu << (8u * ln))) == 0) { d.kind = DX_LANE_MARGIN; d.lane = (int)ln; }
        if (d.kind != DX_LANE_MARGIN) return d;
    }
    d.corroborated = tr[d.lane * 2 + DIR_RD].st != LS_OK || tr[d.lane * 2 + DIR_WR].st != LS_OK;
    return d;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

typedef struct {
    const char *name;
    void (*phy_setup)(void);
    dram_faults_t f;
    dx_kind_t expect;
} board_t;

static void phy_board_lane2_rd(void) { phy_reset_good(); phy_set_win(0, 2, DIR_RD, 30, 46); phy_set_win(1, 2, DIR_RD, 31, 47); phy_set_dly(2, DIR_RD, 38); }
static void phy_board_lane1_err(void) { phy_reset_good(); g_phy[PHY_TSTAT_OFF >> 2] |= 1u << (4u + 1u); }

static diag_t run_board(const board_t *bd, errmap_t *em, uint32_t seed, bool print) {
    static const train_limits_t LIM = { 350u, 120u };
    lane_rep_t tr[NLANE * 2u];
    bd->phy_setup();
    if (print) printf("  [%s] training:\n", bd->name);
    lane_st_t ts = train_check_all(&PHY_BUS, &LIM, tr, print);
    if (print && ts == LS_OK) printf("    all 8 lane/dir OK\n");
    dram_reset(&bd->f);
    em_init(em);
    for (unsigned p = 0; p < NPAT; p++) run_pattern(p, &DRAM_BUS, em, seed);
    diag_t d = diagnose(em, tr);
    if (print) {
        printf("  [%s] stress:\n", bd->name);
        print_errmap(em);
        printf("  [%s] => %s diag=%s", bd->name, d.kind == DX_PASS ? (ts == LS_OK ? "PASS" : "PASS(train warn)") : "FAIL",
               DX_NAME[d.kind]);
        if (d.kind == DX_PIN) printf(" lane=%d DQ%d stuck-at-%d", d.lane, d.bit, d.stuck_val);
        if (d.kind == DX_LANE_MARGIN) {
            char bits[40];
            fmt_bits(em->failbits, bits, sizeof bits);
            printf(" lane=%d (DQ%d-%d) dq=%s", d.lane, 8 * d.lane, 8 * d.lane + 7, bits);
        }
        if (d.kind == DX_ROW_CELL) printf(" bank=%d row=%d%s", d.bank, d.row, d.hammer_only ? " hammer-only" : "");
        if (d.kind == DX_ADDR) printf(" alias on byte-address bit A%d", d.abit);
        if (d.kind == DX_PIN || d.kind == DX_LANE_MARGIN)
            printf(d.corroborated ? " (corroborated by training)" : " (training clean)");
        printf("\n\n");
    }
    return d;
}

int main(void) {
    static const train_limits_t LIM = { 350u, 120u };
    static errmap_t em;
    lane_rep_t rep[NLANE * 2u], o;

    /* ---- Part A: training 결과 판정 ---- */
    printf("[Part A] DDR4-3200 x32, 64 tap/UI, UI=312.5ps, limits: width>=350mUI margin>=120mUI\n");
    CHECK(win_decode(0x00002A14u).valid && win_decode(0x00002A14u).l == 20 && win_decode(0x00002A14u).r == 42);
    CHECK(!win_decode(0x00001414u).valid && !win_decode(0x00001020u).valid);           /* R==L, R<L */
    CHECK(win_intersect((win_t){ true, 10, 40 }, (win_t){ true, 20, 50 }).l == 20 &&
          win_intersect((win_t){ true, 10, 40 }, (win_t){ true, 20, 50 }).r == 40);
    CHECK(!win_intersect((win_t){ true, 10, 30 }, (win_t){ true, 34, 60 }).valid);

    phy_reset_good();
    CHECK(train_check_all(&PHY_BUS, &LIM, rep, true) == LS_OK);
    printf("  good board: all 8 lane/dir %s, lane0 RD eff=[%u,%u] width=%umUI margin=%umUI\n",
           LS_NAME[rep[0].st], rep[0].eff.l, rep[0].eff.r, rep[0].width_mui, rep[0].margin_mui);
    CHECK(rep[0].eff.l == 22 && rep[0].eff.r == 52 && rep[0].width_mui == 468 && rep[0].margin_mui == 234);
    CHECK(rep[0].width_fs == 146484u);                                                  /* 30 tap = 146.48 ps */

    printf("  planted defects:\n");
    phy_reset_good(); phy_set_win(0, 2, DIR_RD, 30, 48); phy_set_win(1, 2, DIR_RD, 30, 48); phy_set_dly(2, DIR_RD, 39);
    CHECK(train_check_all(&PHY_BUS, &LIM, rep, true) == LS_WARN && rep[2 * 2 + DIR_RD].st == LS_WARN &&
          rep[2 * 2 + DIR_RD].width_mui == 281);
    phy_reset_good(); phy_set_dly(1, DIR_WR, 28 + 3);                                    /* 창 [28,58] 의 왼쪽 끝 근처 */
    CHECK(train_check_all(&PHY_BUS, &LIM, rep, true) == LS_WARN && rep[1 * 2 + DIR_WR].st == LS_WARN &&
          strstr(rep[1 * 2 + DIR_WR].why, "off-center") != NULL);
    phy_reset_good(); g_phy[PHY_TSTAT_OFF >> 2] |= 1u << (8u + 3u);                     /* rank1 lane3 error */
    CHECK(train_check_all(&PHY_BUS, &LIM, rep, true) == LS_FAIL && rep[3 * 2].st == LS_FAIL && rep[3 * 2 + 1].st == LS_FAIL &&
          strcmp(rep[3 * 2].why, "lane_err (rank1)") == 0);
    phy_reset_good(); phy_set_win(0, 0, DIR_RD, 10, 30); phy_set_win(1, 0, DIR_RD, 34, 60);
    CHECK(check_lane(&PHY_BUS, 0, DIR_RD, &LIM, &o) == LS_FAIL && strcmp(o.why, "rank windows disjoint") == 0);
    phy_reset_good(); phy_set_win(0, 1, DIR_RD, 0, 40);
    CHECK(check_lane(&PHY_BUS, 1, DIR_RD, &LIM, &o) == LS_INCONC);                      /* 왼쪽 경계 못 찾음 */
    phy_reset_good(); phy_set_dly(3, DIR_WR, 70);
    CHECK(check_lane(&PHY_BUS, 3, DIR_WR, &LIM, &o) == LS_FAIL && strcmp(o.why, "delay outside window") == 0);
    phy_reset_good(); phy_set_win(0, 2, DIR_WR, 30, 53); phy_set_win(1, 2, DIR_WR, 30, 53); phy_set_dly(2, DIR_WR, 41);
    CHECK(check_lane(&PHY_BUS, 2, DIR_WR, &LIM, &o) == LS_OK && o.width_mui == 359);   /* 23 tap: 경계 바로 위 */
    phy_set_win(0, 2, DIR_WR, 30, 52); phy_set_win(1, 2, DIR_WR, 30, 52);
    CHECK(check_lane(&PHY_BUS, 2, DIR_WR, &LIM, &o) == LS_WARN && o.width_mui == 343); /* 22 tap: 경계 바로 아래 */
    g_phy[PHY_TSTAT_OFF >> 2] &= ~(1u << 16);                                           /* rank 1개 보드 */
    phy_set_win(1, 2, DIR_WR, 0, 0);                                                    /* 없는 rank 의 쓰레기 값은 무시 */
    CHECK(check_lane(&PHY_BUS, 2, DIR_WR, &LIM, &o) == LS_WARN && o.nrank == 1);

    /* ---- Part B: 패턴 x 결함 검출 매트릭스 ---- */
    printf("\n[Part B] stress patterns on DDR0 mock: %u words (%u MiB), %u banks x %u rows x %u cols\n",
           NWORDS, NWORDS * 4u >> 20, NBANK, NROW, 1u << COL_BITS);
    {
        uint32_t s = 1u, steps = 0;
        static uint8_t seen[NWORDS];
        bool once = true;
        memset(seen, 0, sizeof seen);
        seen[0] = 1;
        do { if (seen[s]++) once = false; s = lfsr20_next(s); steps++; } while (s != 1u && steps <= NWORDS);
        for (uint32_t w = 0; w < NWORDS; w++) once = once && seen[w] == 1;
        CHECK(steps == NWORDS - 1u && once);                     /* 주기 2^20-1, 0 포함 전 주소 정확히 1번 */
        CHECK(lfsr20_next(0) == 0);                              /* 0 은 고정점 → 따로 처리해야 한다 */
    }
    CHECK(stripe_word(W_IDX(0, 2, 5)) == 0xFFFFFFFFu && stripe_word(W_IDX(7, 3, 0)) == 0u);
    CHECK(checker_word(W_IDX(0, 0, 0)) == 0x55555555u && checker_word(W_IDX(0, 0, 1)) == 0xAAAAAAAAu &&
          checker_word(W_IDX(3, 1, 0)) == 0xAAAAAAAAu && checker_word(W_IDX(3, 1, 1)) == 0x55555555u);

    dram_faults_t F[5];
    const char *fname[5] = { "good", "DQ13 stuck-at-0", "row-bit4 alias", "weak row b2/r33", "xtalk DQ18" };
    for (unsigned i = 0; i < 5; i++) F[i] = NOFAULT;
    F[1].stuck_mask = 1u << 13; F[1].stuck_val = 0;
    F[2].row_alias_bit = 4;
    F[3].weak_bank = 2; F[3].weak_row = 33; F[3].weak_col = 341; F[3].weak_mask = (1u << 9) | (1u << 26);
    F[4].xtalk_bit = 18;
    uint64_t cnt[5][NPAT], reads[NPAT] = { 0 };
    static errmap_t keep[5][NPAT];
    for (unsigned i = 0; i < 5; i++)
        for (unsigned p = 0; p < NPAT; p++) {
            dram_reset(&F[i]);
            em_init(&keep[i][p]);
            run_pattern(p, &DRAM_BUS, &keep[i][p], 0x2545F491u);
            cnt[i][p] = keep[i][p].errors;
            if (i == 0) reads[p] = keep[i][p].reads;
        }
    printf("  errors per (fault, pattern):\n  %-18s", "fault \\ pattern");
    for (unsigned p = 0; p < NPAT; p++) printf("%11s", PAT_NAME[p]);
    printf("\n");
    for (unsigned i = 0; i < 5; i++) {
        printf("  %-18s", fname[i]);
        for (unsigned p = 0; p < NPAT; p++) printf("%11" PRIu64, cnt[i][p]);
        printf("\n");
    }
    printf("  %-18s", "reads/pattern");
    for (unsigned p = 0; p < NPAT; p++) printf("%11" PRIu64, reads[p]);
    printf("\n");
    bool good0 = true;
    for (unsigned p = 0; p < NPAT; p++) good0 = good0 && cnt[0][p] == 0;
    CHECK(good0);                                                                   /* 오탐 없음 */
    CHECK(reads[PAT_SOLID] == 2u * NWORDS && reads[PAT_CHECKER] == 2u * NWORDS && reads[PAT_LFSR] == NWORDS &&
          reads[PAT_HAMMER] == NWORDS + (uint64_t)NBANK * (NROW - 2u) * 2u * HAMMER_ITER);  /* 실제로 일했나 */
    CHECK(cnt[1][PAT_SOLID] && cnt[1][PAT_CHECKER] && cnt[1][PAT_OWNADDR] && cnt[1][PAT_LFSR] && cnt[1][PAT_HAMMER]);
    CHECK(cnt[2][PAT_OWNADDR] && cnt[2][PAT_LFSR] && !cnt[2][PAT_SOLID] && !cnt[2][PAT_CHECKER] && !cnt[2][PAT_HAMMER]);
    CHECK(cnt[3][PAT_HAMMER] && !cnt[3][PAT_SOLID] && !cnt[3][PAT_CHECKER] && !cnt[3][PAT_OWNADDR] && !cnt[3][PAT_LFSR]);
    CHECK(cnt[4][PAT_CHECKER] && !cnt[4][PAT_SOLID] && !cnt[4][PAT_HAMMER]);

    printf("  error maps:\n");
    const errmap_t *e1 = &keep[1][PAT_CHECKER], *e2 = &keep[2][PAT_OWNADDR], *e3 = &keep[3][PAT_HAMMER], *e4 = &keep[4][PAT_CHECKER];
    printf("   - %s / %s\n", fname[1], PAT_NAME[PAT_CHECKER]); print_errmap(e1);
    printf("   - %s / %s\n", fname[2], PAT_NAME[PAT_OWNADDR]); print_errmap(e2);
    printf("   - %s / %s\n", fname[3], PAT_NAME[PAT_HAMMER]);  print_errmap(e3);
    printf("   - %s / %s\n", fname[4], PAT_NAME[PAT_CHECKER]); print_errmap(e4);
    CHECK(e1->failbits == (1u << 13) && e1->rise[13] == 0 && e1->fall[13] == e1->errors);   /* 1->0 만 = stuck-at-0 */
    CHECK(e2->oa_and == (1u << 19) && e2->oa_or == (1u << 19));                            /* 바이트 주소 A19 = row bit4 */
    CHECK(e3->rowcnt[2][33] == e3->errors && e3->failbits == ((1u << 9) | (1u << 26)) &&
          strstr(e3->line[0], "addr=0x8010A554 bank=2 row=33 col=341 ") != NULL);
    CHECK(e4->failbits == (1u << 18) && strstr(e4->line[0], "dq=[18]") != NULL);

    /* ---- Part C: 야간 회귀 — 보드 5장 ---- */
    printf("\n[Part C] nightly DDR regression, 5 boards, all patterns, seed=0x2545F491\n");
    board_t B[5] = {
        { "EVT1-01", phy_reset_good,      NOFAULT, DX_PASS },
        { "EVT1-03", phy_reset_good,      NOFAULT, DX_ADDR },
        { "EVT1-07", phy_board_lane2_rd,  NOFAULT, DX_LANE_MARGIN },
        { "EVT1-11", phy_reset_good,      NOFAULT, DX_ROW_CELL },
        { "EVT1-14", phy_board_lane1_err, NOFAULT, DX_PIN },
    };
    B[1].f.row_alias_bit = 4;
    B[2].f.marg_lane = 2; B[2].f.marg_ppm = 30; B[2].f.marg_seed = 0xC0FFEEu;
    B[3].f = F[3];
    B[4].f = F[1];
    diag_t d[5];
    for (unsigned i = 0; i < 5; i++) { d[i] = run_board(&B[i], &em, 0x2545F491u, true); CHECK(d[i].kind == B[i].expect); }
    CHECK(d[1].abit == 19);
    CHECK(d[2].lane == 2 && d[2].corroborated);
    CHECK(d[3].bank == 2 && d[3].row == 33 && d[3].hammer_only);
    CHECK(d[4].bit == 13 && d[4].lane == 1 && d[4].stuck_val == 0 && d[4].corroborated);

    /* 간헐 오류: 같은 보드 EVT1-07 을 seed 3개로 — 오류 수는 달라도 진단은 같아야 한다 */
    printf("  EVT1-07 re-run with 3 marginal-lane seeds:\n");
    bool same = true;
    for (unsigned k = 0; k < 3; k++) {
        board_t b = B[2];
        b.f.marg_seed = 0x1000u + 77u * k;
        diag_t dk = run_board(&b, &em, 0x2545F491u, false);
        printf("    seed=0x%04" PRIX32 " errors=%" PRIu64 " failbits=0x%08" PRIX32 " diag=%s lane=%d\n",
               b.f.marg_seed, em.errors, em.failbits, DX_NAME[dk.kind], dk.lane);
        same = same && dk.kind == DX_LANE_MARGIN && dk.lane == 2 && em.errors > 0;
    }
    CHECK(same);

    printf("\n%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
