/* MEM-B02 DRAM 명령과 타이밍: ACT/RD/PRE, tRCD·tRP·tRAS
 *   Part A — ns(ps) <-> cycle 변환 (정수 ps, DDR4-3200AA 22-22-22 표 검증)
 *   Part B — 단일 뱅크 상태머신(IDLE/ACTIVE) + 사이클 트레이스 타이밍 체커
 *            (tRCD tRAS tRP tRC tWR tRTP + 상태 위반 + 순서 위반, RD 마다 CL 데이터 반환 시각)
 *   Part C — "트레이스 고치기": 순서를 지키며 각 명령을 가장 이른 합법 사이클로 미루기
 * 타이밍 값은 대표값 (JESD79-4 / 데이터시트 확인 권장). AL = 0, BL8 고정, 단일 뱅크.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= Part A: ns(ps) <-> cycle ======================= */
typedef struct {
    uint32_t tck_ps;                       /* 클럭 주기 (ps). DDR4-3200: 625 ps = 1600 MHz */
    uint32_t cl, cwl;                      /* 데이터시트가 사이클로 주는 값                  */
    uint32_t trcd_ps, trp_ps, tras_ps, trc_ps, twr_ps, trtp_ps;   /* ns 로 주는 값 (ps) */
} ddr_spec_ps;

typedef struct {
    uint32_t cl, cwl, bl2;                 /* bl2 = BL8 데이터가 버스를 쓰는 클럭 수 = 4 */
    uint32_t trcd, trp, tras, trc, twr, trtp;
} ddr_timing;

/* DDR4-3200AA (22-22-22) — 이 레슨의 정의 (JESD79-4 / 데이터시트 대표값) */
static const ddr_spec_ps DDR4_3200AA = { 625, 22, 16, 13750, 13750, 32000, 45750, 15000, 7500 };

#define CYC_INVALID UINT32_MAX

/* ps -> 사이클 = ceil(ps / tck). 정수만 쓴다. tck == 0 이면 CYC_INVALID */
static uint32_t ps_to_cyc(uint32_t ps, uint32_t tck_ps) {
    if (tck_ps == 0) return CYC_INVALID;
    return (uint32_t)(((uint64_t)ps + tck_ps - 1u) / tck_ps);
}
static uint64_t cyc_to_ps(uint32_t cyc, uint32_t tck_ps) { return (uint64_t)cyc * tck_ps; }

/* 데이터시트 표 -> 사이클 표. tRTP = max(4 nCK, tRTP(ns)). 잘못된 입력이면 -1 */
static int timing_from_spec(const ddr_spec_ps *s, ddr_timing *t) {
    if (!s || !t || s->tck_ps == 0) return -1;
    t->cl = s->cl; t->cwl = s->cwl; t->bl2 = 4;
    t->trcd = ps_to_cyc(s->trcd_ps, s->tck_ps);
    t->trp  = ps_to_cyc(s->trp_ps,  s->tck_ps);
    t->tras = ps_to_cyc(s->tras_ps, s->tck_ps);
    t->trc  = ps_to_cyc(s->trc_ps,  s->tck_ps);
    t->twr  = ps_to_cyc(s->twr_ps,  s->tck_ps);
    uint32_t rtp = ps_to_cyc(s->trtp_ps, s->tck_ps);
    t->trtp = rtp < 4u ? 4u : rtp;
    return 0;
}

/* ======================= Part B: 뱅크 상태머신 + 체커 ======================= */
typedef enum { CMD_ACT, CMD_RD, CMD_WR, CMD_PRE } cmd_kind;
typedef struct { uint32_t cyc; cmd_kind cmd; uint32_t row; } cmd_t;   /* row 는 ACT 에서만 의미 */

typedef enum { R_TRCD, R_TRAS, R_TRP, R_TRC, R_TWR, R_TRTP, R_STATE, R_ORDER } rule_t;
static const char *const RULE_NAME[] = { "tRCD", "tRAS", "tRP", "tRC", "tWR", "tRTP", "STATE", "ORDER" };
static const char *const CMD_NAME[]  = { "ACT", "RD", "WR", "PRE" };

/* 위반 1건: 명령 idx 가 규칙 rule 을 어김. ref = 기준 명령 인덱스 (-1 = 없음)
 * 타이밍 규칙: need/got = 기준 명령에서의 필요/실제 간격 (사이클)
 * ORDER: need = 낼 수 있는 가장 이른 사이클, got = 받은 사이클.  STATE: need = got = 0 */
typedef struct { uint32_t idx; rule_t rule; int32_t ref; uint32_t need, got; } viol_t;
typedef struct { uint32_t idx, row, data_start, data_end; } rd_ret_t;   /* 데이터 [start, end) */

#define NEVER (INT64_MIN / 4)
typedef struct {
    int      active;                       /* 0 = IDLE, 1 = ACTIVE(row)            */
    uint32_t row;
    int64_t  t_act, t_pre, t_rd, t_wr;     /* 마지막 ACT/PRE/RD/WR 사이클 (NEVER) */
    int32_t  i_act, i_pre, i_rd, i_wr;     /* 그 명령의 트레이스 인덱스 (-1)      */
} bank_t;

static void bank_init(bank_t *b) {
    b->active = 0; b->row = 0;
    b->t_act = b->t_pre = b->t_rd = b->t_wr = NEVER;
    b->i_act = b->i_pre = b->i_rd = b->i_wr = -1;
}

/* 상태 규칙: ACT 는 IDLE 에서만, RD/WR 은 ACTIVE 에서만. PRE 는 언제나 (IDLE 이면 NOP) */
static int bank_state_ok(const bank_t *b, cmd_kind c) {
    switch (c) {
    case CMD_ACT: return !b->active;
    case CMD_RD: case CMD_WR: return b->active;
    case CMD_PRE: return 1;
    }
    return 0;
}

/* 합법인 명령을 적용: 상태 전이 + 기준 시각 기록 */
static void bank_apply(bank_t *b, const cmd_t *c, uint32_t idx) {
    switch (c->cmd) {
    case CMD_ACT:
        b->active = 1; b->row = c->row;
        b->t_act = c->cyc; b->i_act = (int32_t)idx;
        b->t_rd = b->t_wr = NEVER; b->i_rd = b->i_wr = -1;    /* 새 row 사이클 */
        break;
    case CMD_RD: b->t_rd = c->cyc; b->i_rd = (int32_t)idx; break;
    case CMD_WR: b->t_wr = c->cyc; b->i_wr = (int32_t)idx; break;
    case CMD_PRE:
        if (!b->active) break;                                /* IDLE 의 PRE = NOP */
        b->active = 0; b->t_pre = c->cyc; b->i_pre = (int32_t)idx;
        break;
    }
}

typedef struct { viol_t *v; uint32_t cap, n, total; } viol_list;
static void viol_push(viol_list *L, uint32_t idx, rule_t r, int32_t ref, uint32_t need, uint32_t got) {
    L->total++;
    if (L->n < L->cap) L->v[L->n++] = (viol_t){ idx, r, ref, need, got };
}
/* 기준 명령(ref_t, ref_i)에서 need 사이클 이상 지났는지 */
static void need_gap(viol_list *L, uint32_t idx, rule_t r, int64_t ref_t, int32_t ref_i,
                     uint32_t need, uint32_t cyc) {
    if (ref_i < 0) return;
    int64_t got = (int64_t)cyc - ref_t;
    if (got < (int64_t)need) viol_push(L, idx, r, ref_i, need, (uint32_t)got);
}

/* 명령 c 가 지켜야 할 타이밍 규칙을 전부 검사 (위반은 모두 보고) */
static void timing_checks(const ddr_timing *t, const bank_t *b, const cmd_t *c, uint32_t idx,
                          viol_list *L) {
    switch (c->cmd) {
    case CMD_ACT:
        need_gap(L, idx, R_TRP, b->t_pre, b->i_pre, t->trp, c->cyc);     /* PRE -> ACT */
        need_gap(L, idx, R_TRC, b->t_act, b->i_act, t->trc, c->cyc);     /* ACT -> ACT */
        break;
    case CMD_RD: case CMD_WR:
        need_gap(L, idx, R_TRCD, b->t_act, b->i_act, t->trcd, c->cyc);   /* ACT -> RD/WR */
        break;
    case CMD_PRE:
        if (!b->active) break;
        need_gap(L, idx, R_TRAS, b->t_act, b->i_act, t->tras, c->cyc);   /* ACT -> PRE */
        need_gap(L, idx, R_TRTP, b->t_rd, b->i_rd, t->trtp, c->cyc);     /* RD  -> PRE */
        need_gap(L, idx, R_TWR, b->t_wr, b->i_wr, t->cwl + t->bl2 + t->twr, c->cyc); /* WR -> PRE */
        break;
    }
}

typedef struct {
    viol_t   *viol; uint32_t viol_cap, n_viol, n_viol_total;
    rd_ret_t *rd;   uint32_t rd_cap, n_rd;
} check_report;

/* 트레이스 전체 검사. 반환 = 위반 총수 (버퍼가 작아도 총수는 정확).
 * 타이밍 위반 명령은 "발행된 것"으로 보고 상태에 반영, 상태/순서 위반 명령은 무시 */
static uint32_t check_trace(const ddr_timing *t, const cmd_t *tr, uint32_t n, check_report *rep) {
    bank_t b; bank_init(&b);
    viol_list L = { rep->viol, rep->viol_cap, 0, 0 };
    rep->n_rd = 0;
    int64_t prev = -1; int32_t prev_i = -1;
    for (uint32_t i = 0; i < n; i++) {
        const cmd_t *c = &tr[i];
        if ((int64_t)c->cyc <= prev) {                       /* 명령 버스: 사이클당 1개, 순서대로 */
            viol_push(&L, i, R_ORDER, prev_i, (uint32_t)(prev + 1), c->cyc);
            continue;
        }
        prev = c->cyc; prev_i = (int32_t)i;
        if (!bank_state_ok(&b, c->cmd)) { viol_push(&L, i, R_STATE, -1, 0, 0); continue; }
        timing_checks(t, &b, c, i, &L);
        if (c->cmd == CMD_RD && rep->n_rd < rep->rd_cap)
            rep->rd[rep->n_rd++] = (rd_ret_t){ i, b.row, c->cyc + t->cl, c->cyc + t->cl + t->bl2 };
        bank_apply(&b, c, i);
    }
    rep->n_viol = L.n; rep->n_viol_total = L.total;
    return L.total;
}

/* ======================= Part C: earliest-legal 재배치 ======================= */
static int64_t max64(int64_t a, int64_t b) { return a > b ? a : b; }

/* 현재 뱅크 상태에서 명령 c 를 낼 수 있는 가장 이른 사이클 (상태 규칙은 호출자가 확인) */
static int64_t earliest_issue(const ddr_timing *t, const bank_t *b, cmd_kind c) {
    int64_t e = 0;
    switch (c) {
    case CMD_ACT:
        e = max64(e, b->t_pre + t->trp);
        e = max64(e, b->t_act + t->trc);
        break;
    case CMD_RD: case CMD_WR:
        e = max64(e, b->t_act + t->trcd);
        break;
    case CMD_PRE:
        if (!b->active) break;
        e = max64(e, b->t_act + t->tras);
        e = max64(e, b->t_rd + t->trtp);
        e = max64(e, b->t_wr + t->cwl + t->bl2 + t->twr);
        break;
    }
    return e;
}

/* 순서를 바꾸지 않고 각 명령을 max(원래 사이클, 이전 명령+1, earliest) 로 민다.
 * 0 = 성공, -1 = 인자/용량 오류, -2 = 상태 위반(지연으로 못 고침, *err_idx 에 위치) */
static int fix_trace(const ddr_timing *t, const cmd_t *in, uint32_t n, cmd_t *out, uint32_t cap,
                     uint32_t *err_idx, uint64_t *total_delay) {
    if (!t || (n && (!in || !out)) || cap < n) return -1;
    bank_t b; bank_init(&b);
    int64_t prev = -1; uint64_t delay = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!bank_state_ok(&b, in[i].cmd)) { if (err_idx) *err_idx = i; return -2; }
        int64_t at = max64((int64_t)in[i].cyc, prev + 1);
        at = max64(at, earliest_issue(t, &b, in[i].cmd));
        if (at > (int64_t)UINT32_MAX) return -1;
        out[i] = in[i]; out[i].cyc = (uint32_t)at;
        delay += (uint64_t)(at - (int64_t)in[i].cyc);
        bank_apply(&b, &out[i], i);
        prev = at;
    }
    if (total_delay) *total_delay = delay;
    return 0;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

#define VCAP 32u
static viol_t   g_v[VCAP];
static rd_ret_t g_r[VCAP];
static uint32_t run(const ddr_timing *t, const cmd_t *tr, uint32_t n, check_report *rep) {
    *rep = (check_report){ g_v, VCAP, 0, 0, g_r, VCAP, 0 };
    return check_trace(t, tr, n, rep);
}
/* 위반 목록에 (idx, rule, need, got) 가 있는가 */
static int has(const check_report *r, uint32_t idx, rule_t rule, uint32_t need, uint32_t got) {
    for (uint32_t k = 0; k < r->n_viol; k++)
        if (r->viol[k].idx == idx && r->viol[k].rule == rule &&
            r->viol[k].need == need && r->viol[k].got == got) return 1;
    return 0;
}
static int has_rule(const check_report *r, uint32_t idx, rule_t rule) {
    for (uint32_t k = 0; k < r->n_viol; k++)
        if (r->viol[k].idx == idx && r->viol[k].rule == rule) return 1;
    return 0;
}
static void print_report(const cmd_t *tr, const check_report *r) {
    for (uint32_t k = 0; k < r->n_viol; k++) {
        const viol_t *v = &r->viol[k]; const cmd_t *c = &tr[v->idx];
        printf("  VIOL #%u %s@%u: %-5s", v->idx, CMD_NAME[c->cmd], c->cyc, RULE_NAME[v->rule]);
        if (v->rule == R_STATE) printf(" (%s on %s)\n", CMD_NAME[c->cmd], c->cmd == CMD_ACT ? "ACTIVE" : "IDLE");
        else if (v->rule == R_ORDER) printf(" need cyc>=%u got %u\n", v->need, v->got);
        else printf(" need %u got %u (ref #%d %s@%u)\n", v->need, v->got, v->ref,
                    CMD_NAME[tr[v->ref].cmd], tr[v->ref].cyc);
    }
    for (uint32_t k = 0; k < r->n_rd; k++)
        printf("  DATA #%u RD row %u: beats [%u, %u)\n", r->rd[k].idx, r->rd[k].row,
               r->rd[k].data_start, r->rd[k].data_end);
}

int main(void) {
    ddr_timing t; check_report rep;

    /* ---------- Part A ---------- */
    CHECK(ps_to_cyc(13750, 625) == 22);         /* tRCD = tRP = 13.75 ns -> 22 (나누어떨어짐) */
    CHECK(ps_to_cyc(32000, 625) == 52);         /* tRAS 51.2 -> 52 */
    CHECK(ps_to_cyc(45750, 625) == 74);         /* tRC  73.2 -> 74 */
    CHECK(ps_to_cyc(15000, 625) == 24);         /* tWR */
    CHECK(ps_to_cyc(7500, 625) == 12);          /* tRTP */
    CHECK(ps_to_cyc(0, 625) == 0 && ps_to_cyc(1, 625) == 1 && ps_to_cyc(625, 625) == 1 && ps_to_cyc(626, 625) == 2);
    CHECK(ps_to_cyc(13750, 0) == CYC_INVALID);
    CHECK(ps_to_cyc(UINT32_MAX, 1) == UINT32_MAX);                  /* 덧셈 오버플로 없음 */
    {   /* 정의 확인: c*tck >= ps 이고 (c-1)*tck < ps, ps = 0..200000, tck 4종 */
        static const uint32_t tcks[] = { 625, 750, 833, 938 };
        uint32_t bad = 0;
        for (unsigned k = 0; k < 4; k++)
            for (uint32_t ps = 0; ps <= 200000; ps++) {
                uint64_t c = ps_to_cyc(ps, tcks[k]);
                if (!(c * tcks[k] >= ps && (c == 0 || (c - 1) * tcks[k] < ps))) bad++;
            }
        CHECK(bad == 0);
    }
    CHECK(cyc_to_ps(22, 625) == 13750 && cyc_to_ps(74, 625) == 46250);
    CHECK(timing_from_spec(&DDR4_3200AA, &t) == 0);
    CHECK(t.cl == 22 && t.cwl == 16 && t.bl2 == 4);
    CHECK(t.trcd == 22 && t.trp == 22 && t.tras == 52 && t.trc == 74 && t.twr == 24 && t.trtp == 12);
    CHECK(t.cwl + t.bl2 + t.twr == 44);                             /* WR -> PRE 최소 간격 */
    /* 지연 어휘 (data 마지막 beat 까지): hit / empty / conflict */
    CHECK(cyc_to_ps(t.cl + 4, 625) == 16250);
    CHECK(cyc_to_ps(t.trcd + t.cl + 4, 625) == 30000);
    CHECK(cyc_to_ps(t.trp + t.trcd + t.cl + 4, 625) == 43750);
    {   ddr_spec_ps slow = DDR4_3200AA; slow.tck_ps = 2500;          /* 가상의 느린 클럭 */
        ddr_timing ts; CHECK(timing_from_spec(&slow, &ts) == 0 && ts.trtp == 4);   /* 3 -> max(4,3) */
        slow.tck_ps = 0; CHECK(timing_from_spec(&slow, &ts) == -1);
        CHECK(timing_from_spec(NULL, &ts) == -1 && timing_from_spec(&DDR4_3200AA, NULL) == -1);
    }
    timing_from_spec(&DDR4_3200AA, &t);

    /* ---------- Part B: 깨끗한 트레이스 ---------- */
    {
        const cmd_t ok[] = { {0, CMD_ACT, 5}, {22, CMD_RD, 0}, {26, CMD_RD, 0}, {52, CMD_PRE, 0},
                             {74, CMD_ACT, 9}, {96, CMD_WR, 0}, {140, CMD_PRE, 0},
                             {162, CMD_ACT, 3}, {184, CMD_RD, 0} };
        CHECK(run(&t, ok, 9, &rep) == 0);
        CHECK(rep.n_rd == 3);
        CHECK(rep.rd[0].idx == 1 && rep.rd[0].row == 5 && rep.rd[0].data_start == 44 && rep.rd[0].data_end == 48);
        CHECK(rep.rd[1].data_start == 48 && rep.rd[1].data_end == 52);
        CHECK(rep.rd[2].row == 3 && rep.rd[2].data_start == 206 && rep.rd[2].data_end == 210);
        printf("clean trace: %u violations\n", rep.n_viol_total);
        print_report(ok, &rep);
    }
    /* ---------- Part B: 규칙별 위반 1개씩 (정확한 규칙, need, got) ---------- */
    {
        const cmd_t a[] = { {0, CMD_ACT, 1}, {21, CMD_RD, 0} };
        CHECK(run(&t, a, 2, &rep) == 1 && has(&rep, 1, R_TRCD, 22, 21) && rep.viol[0].ref == 0);
        const cmd_t a2[] = { {0, CMD_ACT, 1}, {21, CMD_WR, 0} };
        CHECK(run(&t, a2, 2, &rep) == 1 && has(&rep, 1, R_TRCD, 22, 21));
        const cmd_t b[] = { {0, CMD_ACT, 1}, {51, CMD_PRE, 0} };
        CHECK(run(&t, b, 2, &rep) == 1 && has(&rep, 1, R_TRAS, 52, 51));
        const cmd_t c[] = { {0, CMD_ACT, 1}, {60, CMD_PRE, 0}, {81, CMD_ACT, 2} };
        CHECK(run(&t, c, 3, &rep) == 1 && has(&rep, 2, R_TRP, 22, 21) && rep.viol[0].ref == 1);
        const cmd_t d[] = { {0, CMD_ACT, 1}, {22, CMD_WR, 0}, {65, CMD_PRE, 0} };
        CHECK(run(&t, d, 3, &rep) == 1 && has(&rep, 2, R_TWR, 44, 43) && rep.viol[0].ref == 1);
        const cmd_t e[] = { {0, CMD_ACT, 1}, {45, CMD_RD, 0}, {56, CMD_PRE, 0} };
        CHECK(run(&t, e, 3, &rep) == 1 && has(&rep, 2, R_TRTP, 12, 11));
        /* tRC 단독 위반: DDR4-3200AA 에서는 tRC = tRAS + tRP 라 단독으로 안 생긴다 (연습 3).
         * tRC 를 80 으로 늘린 가상의 부품으로 확인 */
        ddr_timing t80 = t; t80.trc = 80;
        const cmd_t f[] = { {0, CMD_ACT, 1}, {52, CMD_PRE, 0}, {74, CMD_ACT, 2} };
        CHECK(run(&t, f, 3, &rep) == 0);
        CHECK(run(&t80, f, 3, &rep) == 1 && has(&rep, 2, R_TRC, 80, 74) && rep.viol[0].ref == 0);
        /* 상태 위반 */
        const cmd_t g[] = { {5, CMD_RD, 0} };
        CHECK(run(&t, g, 1, &rep) == 1 && has(&rep, 0, R_STATE, 0, 0) && rep.n_rd == 0);
        const cmd_t g2[] = { {0, CMD_ACT, 1}, {100, CMD_ACT, 2}, {122, CMD_RD, 0} };
        CHECK(run(&t, g2, 3, &rep) == 1 && has(&rep, 1, R_STATE, 0, 0) && rep.rd[0].row == 1);
        const cmd_t g3[] = { {0, CMD_ACT, 1}, {60, CMD_PRE, 0}, {90, CMD_WR, 0} };
        CHECK(run(&t, g3, 3, &rep) == 1 && has(&rep, 2, R_STATE, 0, 0));
        /* IDLE 의 PRE = NOP: 위반 아님, tRP 기준도 바꾸지 않음 */
        const cmd_t h[] = { {0, CMD_ACT, 1}, {52, CMD_PRE, 0}, {70, CMD_PRE, 0}, {74, CMD_ACT, 2} };
        CHECK(run(&t, h, 4, &rep) == 0);
        /* 순서 위반: 같은 사이클 두 명령 */
        const cmd_t o[] = { {10, CMD_ACT, 1}, {10, CMD_RD, 0}, {32, CMD_RD, 0} };
        CHECK(run(&t, o, 3, &rep) == 1 && has(&rep, 1, R_ORDER, 11, 10) && rep.n_rd == 1);
    }
    /* ---------- Part B: 경계 전수 — 위반 <=> gap < need ---------- */
    {
        uint32_t bad = 0;
        for (uint32_t g = 1; g <= 80; g++) {
            cmd_t a[] = { {0, CMD_ACT, 1}, {g, CMD_RD, 0} };
            run(&t, a, 2, &rep);
            if (has_rule(&rep, 1, R_TRCD) != (g < 22) || (g < 22 && !has(&rep, 1, R_TRCD, 22, g))) bad++;
            cmd_t b[] = { {0, CMD_ACT, 1}, {g, CMD_PRE, 0} };
            run(&t, b, 2, &rep);
            if (has_rule(&rep, 1, R_TRAS) != (g < 52)) bad++;
            cmd_t c[] = { {0, CMD_ACT, 1}, {100, CMD_PRE, 0}, {100 + g, CMD_ACT, 2} };
            run(&t, c, 3, &rep);
            if (has_rule(&rep, 2, R_TRP) != (g < 22) || has_rule(&rep, 2, R_TRC)) bad++;
            cmd_t d[] = { {0, CMD_ACT, 1}, {22, CMD_WR, 0}, {22 + g, CMD_PRE, 0} };
            run(&t, d, 3, &rep);
            if (has_rule(&rep, 2, R_TWR) != (g < 44) || has_rule(&rep, 2, R_TRAS) != (22 + g < 52)) bad++;
            cmd_t e[] = { {0, CMD_ACT, 1}, {60, CMD_RD, 0}, {60 + g, CMD_PRE, 0} };
            run(&t, e, 3, &rep);
            if (has_rule(&rep, 2, R_TRTP) != (g < 12) || has_rule(&rep, 2, R_TRAS)) bad++;
        }
        CHECK(bad == 0);
    }
    /* ---------- Part B: 한 트레이스에 여러 위반 — 전부 보고 ---------- */
    const cmd_t multi[] = { {0, CMD_ACT, 1}, {10, CMD_RD, 0}, {20, CMD_PRE, 0},
                            {30, CMD_ACT, 2}, {31, CMD_RD, 0} };
    {
        CHECK(run(&t, multi, 5, &rep) == 6);
        CHECK(has(&rep, 1, R_TRCD, 22, 10));
        CHECK(has(&rep, 2, R_TRAS, 52, 20) && has(&rep, 2, R_TRTP, 12, 10));
        CHECK(has(&rep, 3, R_TRP, 22, 10) && has(&rep, 3, R_TRC, 74, 30));
        CHECK(has(&rep, 4, R_TRCD, 22, 1));
        CHECK(rep.n_rd == 2 && rep.rd[0].data_start == 32 && rep.rd[1].data_start == 53 && rep.rd[1].row == 2);
        printf("multi-violation trace: %u violations\n", rep.n_viol_total);
        print_report(multi, &rep);
        viol_t small[2]; rd_ret_t rs[1];
        check_report r2 = { small, 2, 0, 0, rs, 1, 0 };
        CHECK(check_trace(&t, multi, 5, &r2) == 6 && r2.n_viol == 2 && r2.n_viol_total == 6 && r2.n_rd == 1);
        const cmd_t w[] = { {0, CMD_ACT, 1}, {22, CMD_WR, 0}, {40, CMD_RD, 0}, {50, CMD_PRE, 0} };
        CHECK(run(&t, w, 4, &rep) == 3 && has(&rep, 3, R_TRAS, 52, 50) &&
              has(&rep, 3, R_TRTP, 12, 10) && has(&rep, 3, R_TWR, 44, 28));
    }

    /* ---------- Part C: 트레이스 고치기 ---------- */
    {
        cmd_t out[16] = {{0}}; uint32_t ei = 99; uint64_t dl = 0;
        CHECK(fix_trace(&t, multi, 5, out, 16, &ei, &dl) == 0);
        CHECK(out[0].cyc == 0 && out[1].cyc == 22 && out[2].cyc == 52 && out[3].cyc == 74 && out[4].cyc == 96);
        CHECK(dl == 153 && out[3].row == 2);
        CHECK(run(&t, out, 5, &rep) == 0);
        printf("fixed multi trace:");
        for (int i = 0; i < 5; i++) printf(" %s@%u", CMD_NAME[out[i].cmd], out[i].cyc);
        printf("  (total delay %llu cyc)\n", (unsigned long long)dl);

        const cmd_t wr[] = { {0, CMD_ACT, 1}, {5, CMD_WR, 0}, {6, CMD_PRE, 0}, {7, CMD_ACT, 2}, {8, CMD_RD, 0} };
        CHECK(fix_trace(&t, wr, 5, out, 16, &ei, &dl) == 0);
        CHECK(out[1].cyc == 22 && out[2].cyc == 66 && out[3].cyc == 88 && out[4].cyc == 110);
        printf("fixed write trace:");
        for (int i = 0; i < 5; i++) printf(" %s@%u", CMD_NAME[out[i].cmd], out[i].cyc);
        printf("\n");
        /* 행 충돌(conflict) 읽기: 오래 열린 row 1 에 row 2 요청이 200 에 도착 */
        const cmd_t cf[] = { {0, CMD_ACT, 1}, {200, CMD_PRE, 0}, {200, CMD_ACT, 2}, {200, CMD_RD, 0} };
        CHECK(fix_trace(&t, cf, 4, out, 16, &ei, &dl) == 0);
        CHECK(out[1].cyc == 200 && out[2].cyc == 222 && out[3].cyc == 244 && out[3].cyc + t.cl + t.bl2 - 200 == 70);
        const cmd_t ok[] = { {0, CMD_ACT, 5}, {22, CMD_RD, 0}, {52, CMD_PRE, 0}, {74, CMD_ACT, 6} };
        CHECK(fix_trace(&t, ok, 4, out, 16, &ei, &dl) == 0 && dl == 0 && memcmp(out, ok, sizeof ok) == 0);
        const cmd_t bad[] = { {0, CMD_ACT, 1}, {60, CMD_PRE, 0}, {90, CMD_RD, 0} };
        CHECK(fix_trace(&t, bad, 3, out, 16, &ei, &dl) == -2 && ei == 2);
        CHECK(fix_trace(&t, multi, 5, out, 4, &ei, &dl) == -1);
        CHECK(fix_trace(&t, NULL, 0, NULL, 0, NULL, NULL) == 0);
    }
    /* ---------- Part C: 랜덤 성질 검사 — 합법 + 최소 ---------- */
    {
        uint32_t seed = 0xB02u, n_tr = 2000, after_bad = 0, nonmin = 0, early = 0, delayed = 0, fail = 0;
        for (uint32_t k = 0; k < n_tr; k++) {
            cmd_t in[40], out[40], tmp[40];
            uint32_t n = 4 + xs32(&seed) % 37, cyc = 0; int act = 0;
            for (uint32_t i = 0; i < n; i++) {
                uint32_t r = xs32(&seed);
                cyc += r % 30;
                cmd_kind c;
                if (!act) c = (r >> 8) % 8 == 0 ? CMD_PRE : CMD_ACT;
                else { uint32_t q = (r >> 8) % 6; c = q < 3 ? CMD_RD : q < 5 ? CMD_WR : CMD_PRE; }
                if (c == CMD_ACT) act = 1; else if (c == CMD_PRE) act = 0;
                in[i] = (cmd_t){ cyc, c, (r >> 16) & 0xFFFF };
            }
            uint32_t ei; uint64_t dl;
            if (fix_trace(&t, in, n, out, 40, &ei, &dl) != 0) { fail++; continue; }
            if (run(&t, out, n, &rep) != 0) after_bad++;
            for (uint32_t i = 0; i < n; i++) {
                if (out[i].cyc < in[i].cyc) early++;
                uint32_t floor_c = in[i].cyc;
                if (i && out[i - 1].cyc + 1 > floor_c) floor_c = out[i - 1].cyc + 1;
                if (out[i].cyc > floor_c) {          /* 밀린 명령: 1 사이클 앞당기면 위반이어야 최소 */
                    delayed++;
                    memcpy(tmp, out, n * sizeof *out); tmp[i].cyc--;
                    run(&t, tmp, n, &rep);
                    int v = 0;
                    for (uint32_t m = 0; m < rep.n_viol; m++) if (rep.viol[m].idx == i) v = 1;
                    if (!v) nonmin++;
                }
            }
        }
        printf("random fix: %u traces, fail=%u, violations-after-fix=%u, earlier-than-asked=%u, "
               "delayed cmds=%u, non-minimal=%u\n", n_tr, fail, after_bad, early, delayed, nonmin);
        CHECK(fail == 0 && after_bad == 0 && early == 0 && nonmin == 0 && delayed > 1000);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
