/* BM-A10 인터럽트 지연 측정: DWT CYCCNT, tail-chaining, late arrival — 호스트(맥) 부분
 *
 * 이 파일은 "측정" 이 아니다. 세 부분 중 호스트에서 돌릴 수 있는 둘을 담는다.
 *   Part A  Cortex-M3/M4 예외 타이밍 사이클 "모델" (파라미터 = ARM 문서의 대표값 + 이 레슨의 가정, 문서 확인 권장)
 *           + 사이클 단위 이벤트 시뮬레이터: 진입, 중첩, tail-chaining, late arrival, pop pre-emption, lazy FP.
 *   Part B  측정이 틀리는 길: CYCCNT 감김, 보정값, 멈춘 CYCCNT(QEMU), 잘린/깨진 UART 덤프, 경계 조건.
 *   Part C  보드 펌웨어(board/latency_fw.c)가 UART 로 내보내는 덤프 형식의 분석기.
 *           입력은 모델 + 고정 시드 지터로 만든 **합성(SYNTHETIC) 덤프** — 실측이 아니다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-A10_so && /tmp/BM-A10_so
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(cond, msg) do { ++g_total; if (cond) { ++g_pass; } \
    else printf("FAIL  %s  (line %d)\n", msg, __LINE__); } while (0)

static uint32_t g_rng = 0xA10A10u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

/* ===================== Part A — 사이클 모델 ===================== */
typedef struct {
    uint32_t entry;     /* IRQ 가 NVIC 에 도착 → ISR 첫 명령 실행, 0 wait state (ARM: 12) */
    uint32_t tail;      /* 앞 ISR 끝 → 다음 ISR 첫 명령, 꼬리 물기 (ARM: 6) */
    uint32_t exit;      /* ISR 끝(BX LR) → 스레드 첫 명령 (모델 가정 12, 문헌에 따라 10~12) */
    uint32_t late_min;  /* late arrival 로 벡터를 다시 읽을 때 도착 → 첫 명령 최소 (모델 가정 6) */
    uint32_t fp_words;  /* FP 확장 프레임의 추가 저장 워드 S0–S15 + FPSCR = 17, 워드당 1 사이클 가정 */
} cm_params_t;
static const cm_params_t CM4 = { 12, 6, 12, 6, 17 };

typedef struct {
    uint8_t ws;          /* 플래시 wait state (STM32F4 @168 MHz 이면 5) */
    uint8_t art_hit;     /* 1 = 벡터·ISR 첫 줄이 ART(명령 캐시)에 있음 → 플래시 대기 없음 */
    uint8_t vec_in_ram;  /* 1 = VTOR 이 SRAM → 벡터 읽기에 플래시 대기 없음 */
    uint8_t fpca;        /* 스레드가 FP 를 쓰는 중 (CONTROL.FPCA = 1) */
    uint8_t aspen;       /* FPCCR.ASPEN: FP 문맥 자동 저장 */
    uint8_t lspen;       /* FPCCR.LSPEN: 게으른(lazy) 저장 — 공간만 잡고 실제 저장은 ISR 의 첫 FP 명령 때 */
} cm_cfg_t;

/* TODO A1: 진입·꼬리 물기마다 붙는 플래시 대기 사이클.
 * 모델: 플래시 접근 1 번이 miss 면 ws 사이클 추가. 접근은 (1) 벡터 읽기 (2) ISR 첫 명령 줄 읽기 두 번.
 * ART hit 이면 둘 다 0. 벡터가 RAM 에 있으면 (1) 은 0. */
static uint32_t fetch_penalty(const cm_cfg_t *c)
{
    if (c->art_hit) return 0;
    return (c->vec_in_ram ? 0u : c->ws) + c->ws;
}

/* TODO A2: 스레드/ISR 를 선점하는 정상 진입 비용. ctx_fp = 선점당하는 문맥이 FP 를 쓰는 중.
 * FP 문맥이 있고 ASPEN=1, LSPEN=0 이면 17 워드를 진입 때 바로 쌓는다(+fp_words). LSPEN=1 이면 공간만 → 0. */
static uint32_t entry_cycles(const cm_params_t *p, const cm_cfg_t *c, int ctx_fp)
{
    uint32_t fp = (ctx_fp && c->aspen && !c->lspen) ? p->fp_words : 0u;
    return p->entry + fetch_penalty(c) + fp;
}

/* TODO A3: 꼬리 물기 = 스택 그대로, 벡터 + 첫 명령만 다시. 복귀 = 언스태킹 (+FP 가 실제로 쌓였으면 그만큼). */
static uint32_t tail_cycles(const cm_params_t *p, const cm_cfg_t *c) { return p->tail + fetch_penalty(c); }
static uint32_t exit_cycles(const cm_params_t *p, int fp_stacked) { return p->exit + (fp_stacked ? p->fp_words : 0u); }

/* TODO A4: late arrival — 낮은 IRQ 의 스태킹 중(첫 명령 전) 높은 IRQ 가 t 에 도착.
 * 스태킹은 그대로 쓰고 벡터만 높은 쪽으로 바꾼다: 시작 = max(원래 준비 시각, t + late_min + 플래시 대기). */
static uint32_t late_ready(const cm_params_t *p, const cm_cfg_t *c, uint32_t ready_old, uint32_t t)
{
    uint32_t r = t + p->late_min + fetch_penalty(c);
    return r > ready_old ? r : ready_old;
}

/* TODO A5: lazy FP — 프레임에 FP 저장이 "예약만" 돼 있고 ISR 이 FP 를 쓰면, 첫 FP 명령에서 fp_words 만큼 멈춘다. */
static uint32_t lazy_stall_cycles(const cm_params_t *p, int frame_lazy_pending, int isr_uses_fp)
{
    return (frame_lazy_pending && isr_uses_fp) ? p->fp_words : 0u;
}

/* ---------- 이벤트 시뮬레이터 (주어짐): 1 사이클씩, 결정적 ---------- */
#define MAXEV 8
#define MAXDEPTH (MAXEV + 1)
#define SIM_CAP 200000u
enum { HOW_NONE, HOW_ENTRY, HOW_NESTED, HOW_LATE, HOW_TAIL, HOW_POP };
static const char *HOW_NAME[] = { "-", "entry", "nested", "late", "tail", "pop" };
typedef struct { uint32_t t_arrive; uint8_t prio; uint32_t body; uint8_t uses_fp; } irq_ev_t;
typedef struct {
    uint32_t t_start[MAXEV], t_end[MAXEV], t_fp_ready[MAXEV];
    uint8_t how[MAXEV];
    int order[MAXEV], n_done;
    uint32_t t_thread;                 /* 마지막 복귀 뒤 스레드가 다시 도는 시각 */
    uint32_t n_tail, n_late, n_pop, n_exit, max_depth;
} sim_out_t;
enum { ST_RUN, ST_ENTRY, ST_TAIL, ST_EXIT };

static int sim_run(const cm_params_t *p, const cm_cfg_t *c, const irq_ev_t *ev, int n, sim_out_t *o)
{
    int pend[MAXEV] = {0}, arrived = 0;
    int act[MAXDEPTH] = {0};
    uint32_t rem[MAXDEPTH] = {0};
    int fr_lazy[MAXDEPTH] = {0}, fr_stacked[MAXDEPTH] = {0};
    int depth = 0, st = ST_RUN, tgt = -1, how = HOW_NONE;
    int new_lazy = 0, new_stacked = 0;
    uint32_t ready = 0;
    memset(o, 0, sizeof *o);
    for (uint32_t t = 0; t < SIM_CAP; ++t) {
        for (int i = 0; i < n; ++i) if (ev[i].t_arrive == t) { pend[i] = 1; ++arrived; }
        int step;
        for (step = 0; step < 16; ++step) {
            /* 지금 문맥보다 급한(숫자가 작은) pending 중 최선. 동률이면 번호 작은 것 */
            int ctx = depth ? ev[act[depth]].prio : 256, below = ctx, b = -1;
            if (st == ST_EXIT) below = (depth > 1) ? ev[act[depth - 1]].prio : 256;
            if (st == ST_ENTRY || st == ST_TAIL) below = ev[tgt].prio;
            for (int i = 0; i < n; ++i)
                if (pend[i] && ev[i].prio < below && (b < 0 || ev[i].prio < ev[b].prio)) b = i;

            if (st == ST_RUN) {
                if (depth > 0 && rem[depth] == 0) {                   /* ISR 끝 (BX LR) */
                    o->t_end[act[depth]] = t;
                    int b2 = -1, back = (depth > 1) ? ev[act[depth - 1]].prio : 256;
                    for (int i = 0; i < n; ++i)
                        if (pend[i] && ev[i].prio < back && (b2 < 0 || ev[i].prio < ev[b2].prio)) b2 = i;
                    if (b2 >= 0) { st = ST_TAIL; tgt = b2; how = HOW_TAIL; ready = t + tail_cycles(p, c); ++o->n_tail; }
                    else { st = ST_EXIT; ready = t + exit_cycles(p, fr_stacked[depth]); }
                    continue;
                }
                if (b >= 0) {                                         /* 선점: 스태킹 시작 */
                    int ctx_fp = depth ? ev[act[depth]].uses_fp : c->fpca;
                    new_lazy = ctx_fp && c->aspen && c->lspen;
                    new_stacked = ctx_fp && c->aspen && !c->lspen;
                    st = ST_ENTRY; tgt = b; how = depth ? HOW_NESTED : HOW_ENTRY;
                    ready = t + entry_cycles(p, c, ctx_fp);
                    continue;
                }
                if (depth > 0) --rem[depth];                          /* ISR 본문 1 사이클 */
                break;
            }
            if (st == ST_ENTRY || st == ST_TAIL) {
                if (t < ready && b >= 0) {                            /* late arrival: 벡터만 교체 */
                    tgt = b; how = HOW_LATE; ready = late_ready(p, c, ready, t); ++o->n_late;
                }
                if (t < ready) break;
                if (st == ST_ENTRY) {
                    ++depth;
                    fr_lazy[depth] = new_lazy; fr_stacked[depth] = new_stacked;
                    if ((uint32_t)depth > o->max_depth) o->max_depth = (uint32_t)depth;
                }
                act[depth] = tgt; pend[tgt] = 0;
                uint32_t stall = lazy_stall_cycles(p, fr_lazy[depth], ev[tgt].uses_fp);
                if (stall) { fr_lazy[depth] = 0; fr_stacked[depth] = 1; }
                rem[depth] = ev[tgt].body + stall;
                o->t_start[tgt] = t; o->t_fp_ready[tgt] = t + stall; o->how[tgt] = (uint8_t)how;
                if (o->n_done < MAXEV) o->order[o->n_done++] = tgt;
                st = ST_RUN;
                continue;
            }
            /* ST_EXIT: 언스태킹 중 */
            if (t < ready && b >= 0) {                                /* pop pre-emption → 꼬리 물기로 */
                st = ST_TAIL; tgt = b; how = HOW_POP; ready = t + tail_cycles(p, c); ++o->n_pop;
                continue;
            }
            if (t < ready) break;
            --depth; ++o->n_exit; st = ST_RUN;
            if (depth == 0) o->t_thread = t;
            continue;
        }
        if (arrived == n && depth == 0 && st == ST_RUN && o->n_done == n) return 0;
    }
    return -1;
}

/* ---------- 이름 붙은 시나리오 → 사이클 ---------- */
enum { SC_ENTRY, SC_TAIL_GAP, SC_EXIT, SC_B2B_GAP, SC_LATE_HIGH, SC_POP, SC_LAZY_STALL };
static uint32_t latency_model(const cm_params_t *p, const cm_cfg_t *c, int kind, uint32_t d)
{
    irq_ev_t ev[2];
    sim_out_t o;
    memset(ev, 0, sizeof ev);
    switch (kind) {
    case SC_ENTRY:                         /* IRQ 하나: 도착 → 첫 명령 */
        ev[0] = (irq_ev_t){ 10, 1, 20, 0 };
        if (sim_run(p, c, ev, 1, &o)) return 0xFFFFFFFFu;
        return o.t_start[0] - 10;
    case SC_LAZY_STALL:                    /* ISR 가 FP 를 쓸 때 첫 FP 명령의 멈춤 */
        ev[0] = (irq_ev_t){ 10, 1, 20, 1 };
        if (sim_run(p, c, ev, 1, &o)) return 0xFFFFFFFFu;
        return o.t_fp_ready[0] - o.t_start[0];
    case SC_EXIT:                          /* ISR 끝 → 스레드 */
        ev[0] = (irq_ev_t){ 10, 1, 20, (uint8_t)d };
        if (sim_run(p, c, ev, 1, &o)) return 0xFFFFFFFFu;
        return o.t_thread - o.t_end[0];
    case SC_TAIL_GAP:                      /* 같은 우선순위 둘이 동시에: ISR1 끝 → ISR2 첫 명령 */
        ev[0] = (irq_ev_t){ 10, 1, 20, 0 }; ev[1] = (irq_ev_t){ 10, 1, 20, 0 };
        if (sim_run(p, c, ev, 2, &o)) return 0xFFFFFFFFu;
        return o.t_start[1] - o.t_end[0];
    case SC_B2B_GAP: {                     /* 두 번째가 스레드 복귀 직후에 도착: 복귀 + 진입 */
        ev[0] = (irq_ev_t){ 10, 1, 20, 0 };
        if (sim_run(p, c, ev, 1, &o)) return 0xFFFFFFFFu;
        ev[1] = (irq_ev_t){ o.t_thread, 1, 20, 0 };
        if (sim_run(p, c, ev, 2, &o)) return 0xFFFFFFFFu;
        return o.t_start[1] - o.t_end[0];
    }
    case SC_LATE_HIGH:                     /* 낮은(prio 2) 도착 10, 높은(prio 1) 도착 10+d: 높은 쪽 지연 */
        ev[0] = (irq_ev_t){ 10, 2, 20, 0 }; ev[1] = (irq_ev_t){ 10 + d, 1, 20, 0 };
        if (sim_run(p, c, ev, 2, &o)) return 0xFFFFFFFFu;
        return o.t_start[1] - (10 + d);
    case SC_POP: {                         /* A 끝난 뒤 d 사이클(언스태킹 중)에 B 도착: B 지연 */
        ev[0] = (irq_ev_t){ 10, 1, 20, 0 };
        if (sim_run(p, c, ev, 1, &o)) return 0xFFFFFFFFu;
        uint32_t tb = o.t_end[0] + d;
        ev[1] = (irq_ev_t){ tb, 1, 20, 0 };
        if (sim_run(p, c, ev, 2, &o)) return 0xFFFFFFFFu;
        return o.t_start[1] - tb;
    }
    }
    return 0xFFFFFFFFu;
}

/* ===================== Part B — 측정 보정 ===================== */
/* TODO B1: CYCCNT 차이 (감김 안전). t1 이 2^32 을 넘어 0 부근으로 돌아와도 정확해야 한다. */
static uint32_t cyc_delta(uint32_t t0, uint32_t t1) { return t1 - t0; }

enum { DUMP_OK, ERR_PARSE, ERR_ORDER, ERR_TRUNCATED, ERR_CHECKSUM, ERR_STUCK, ERR_CAPACITY, ERR_NEGATIVE };
static const char *ERR_NAME[] = { "OK", "PARSE", "ORDER", "TRUNCATED", "CHECKSUM", "STUCK", "CAPACITY", "NEGATIVE" };

/* TODO B2: 보정 — 원시 차이에서 back-to-back CYCCNT 읽기 비용(calib)을 뺀다. raw < calib 이면 측정 순서가
 * 틀렸거나 보정이 잘못된 것 → ERR_NEGATIVE (조용히 감겨서 40 억이 되면 안 된다). */
static int lat_correct(uint32_t raw, uint32_t calib, uint32_t *out)
{
    if (raw < calib) { *out = 0; return ERR_NEGATIVE; }
    *out = raw - calib;
    return DUMP_OK;
}

/* ===================== Part C — 덤프 분석기 ===================== */
/* 덤프 형식 v1 (board/latency_fw.c 가 USART2 로 찍는 것과 같음):
 *   # 주석            ("# SYNTHETIC" 으로 시작하면 합성 표시)
 *   H,<core_hz>,<n_scn>
 *   C,<calib>
 *   S,<id>,<name>,<n>                      시나리오 머리
 *   D,<id>,<idx>,<t0 8hex>,<t1 8hex>       표본 (idx 는 0 부터 연속)
 *   E,<total>,<sum 8hex>                   sum = core_hz + calib + Σ(t1−t0)  (mod 2^32)
 */
#define MAX_SCN 16
#define MAX_SAMP 4096
typedef struct { char name[24]; uint32_t n_expected, first, count; } scn_t;
typedef struct {
    uint32_t core_hz, calib, n_scn_hdr; int n_scn; scn_t scn[MAX_SCN];
    uint32_t raw[MAX_SAMP], t0[MAX_SAMP]; uint32_t n;
    uint32_t end_count, end_sum; int have_end, have_h, have_c, synthetic;
    int err, err_line;
} dump_t;

static int parse_u32(const char **s, const char *e, uint32_t *v)          /* 10 진수, 최소 1 자리 */
{
    const char *q = *s; uint64_t x = 0;
    if (q >= e || *q < '0' || *q > '9') return -1;
    while (q < e && *q >= '0' && *q <= '9') { x = x * 10 + (uint64_t)(*q - '0'); if (x > 0xFFFFFFFFu) return -1; ++q; }
    *v = (uint32_t)x; *s = q; return 0;
}
static int parse_hex8(const char **s, const char *e, uint32_t *v)         /* 정확히 8 자리 16 진수 */
{
    const char *q = *s; uint32_t x = 0;
    if (e - q < 8) return -1;
    for (int i = 0; i < 8; ++i, ++q) {
        char ch = *q; uint32_t d;
        if (ch >= '0' && ch <= '9') d = (uint32_t)(ch - '0');
        else if (ch >= 'a' && ch <= 'f') d = (uint32_t)(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') d = (uint32_t)(ch - 'A' + 10);
        else return -1;
        x = (x << 4) | d;
    }
    *v = x; *s = q; return 0;
}
static int expect(const char **s, const char *e, char ch) { if (*s < e && **s == ch) { ++*s; return 0; } return -1; }

/* TODO C1: 끝 검사. E 줄이 없으면 TRUNCATED. 시나리오마다 받은 개수 == 머리의 n, 총 개수 == E 의 total,
 * 시나리오 수 == H 의 n_scn 이 아니면 TRUNCATED. 합 불일치면 CHECKSUM. 그리고 모든 표본의 t0 와 raw 가 0 이면
 * STUCK (CYCCNT 가 안 센다: TRCENA 빠뜨림, 또는 QEMU). */
static int dump_finish(dump_t *d)
{
    if (!d->have_end || !d->have_h || !d->have_c) return ERR_TRUNCATED;
    if ((uint32_t)d->n_scn != d->n_scn_hdr || d->end_count != d->n) return ERR_TRUNCATED;
    for (int i = 0; i < d->n_scn; ++i) if (d->scn[i].count != d->scn[i].n_expected) return ERR_TRUNCATED;
    uint32_t sum = d->core_hz + d->calib;
    int all_zero = 1;
    for (uint32_t i = 0; i < d->n; ++i) { sum += d->raw[i]; if (d->raw[i] || d->t0[i]) all_zero = 0; }
    if (sum != d->end_sum) return ERR_CHECKSUM;
    if (d->n > 0 && all_zero) return ERR_STUCK;
    return DUMP_OK;
}

static int dump_parse(const char *text, size_t len, dump_t *d)
{
    memset(d, 0, sizeof *d);
    const char *p = text, *end = text + len;
    int line = 0;
    while (p < end) {
        const char *e = memchr(p, '\n', (size_t)(end - p));
        const char *nx = e ? e + 1 : end;
        if (!e) e = end;
        ++line;
        const char *s = p;
        int rc = DUMP_OK;
        if (s == e) { p = nx; continue; }                                    /* 빈 줄 */
        if (d->have_end) rc = ERR_ORDER;                                     /* E 뒤엔 아무것도 */
        else if (*s == '#') {
            if ((size_t)(e - s) >= 11 && memcmp(s, "# SYNTHETIC", 11) == 0) d->synthetic = 1;
        } else if (*s == 'H') {
            ++s;
            if (expect(&s, e, ',') || parse_u32(&s, e, &d->core_hz) || expect(&s, e, ',') ||
                parse_u32(&s, e, &d->n_scn_hdr) || s != e) rc = ERR_PARSE;
            else if (d->have_h) rc = ERR_ORDER;
            else d->have_h = 1;
        } else if (*s == 'C') {
            ++s;
            if (expect(&s, e, ',') || parse_u32(&s, e, &d->calib) || s != e) rc = ERR_PARSE;
            else if (!d->have_h || d->have_c) rc = ERR_ORDER;
            else d->have_c = 1;
        } else if (*s == 'S') {
            uint32_t id, n;
            ++s;
            if (expect(&s, e, ',') || parse_u32(&s, e, &id) || expect(&s, e, ',')) rc = ERR_PARSE;
            else {
                const char *nm = s;
                while (s < e && *s != ',') ++s;
                size_t nlen = (size_t)(s - nm);
                if (nlen == 0 || nlen >= 24 || expect(&s, e, ',') || parse_u32(&s, e, &n) || s != e) rc = ERR_PARSE;
                else if (!d->have_c || id != (uint32_t)d->n_scn) rc = ERR_ORDER;
                else if (d->n_scn >= MAX_SCN) rc = ERR_CAPACITY;
                else {
                    scn_t *sc = &d->scn[d->n_scn++];
                    memcpy(sc->name, nm, nlen); sc->name[nlen] = 0;
                    sc->n_expected = n; sc->first = d->n; sc->count = 0;
                }
            }
        } else if (*s == 'D') {
            uint32_t id, idx, t0, t1;
            ++s;
            if (expect(&s, e, ',') || parse_u32(&s, e, &id) || expect(&s, e, ',') || parse_u32(&s, e, &idx) ||
                expect(&s, e, ',') || parse_hex8(&s, e, &t0) || expect(&s, e, ',') || parse_hex8(&s, e, &t1) || s != e)
                rc = ERR_PARSE;
            else if (d->n_scn == 0 || id != (uint32_t)(d->n_scn - 1) || idx != d->scn[d->n_scn - 1].count) rc = ERR_ORDER;
            else if (d->n >= MAX_SAMP) rc = ERR_CAPACITY;
            else {
                d->t0[d->n] = t0; d->raw[d->n] = cyc_delta(t0, t1); ++d->n; ++d->scn[d->n_scn - 1].count;
            }
        } else if (*s == 'E') {
            ++s;
            if (expect(&s, e, ',') || parse_u32(&s, e, &d->end_count) || expect(&s, e, ',') ||
                parse_hex8(&s, e, &d->end_sum) || s != e) rc = ERR_PARSE;
            else d->have_end = 1;
        } else rc = ERR_PARSE;
        if (rc != DUMP_OK) { d->err = rc; d->err_line = line; return rc; }
        p = nx;
    }
    d->err = dump_finish(d);
    if (d->err) d->err_line = line;
    return d->err;
}

/* ---------- 통계 ---------- */
typedef struct { uint32_t n, min, max, p50, p99, mean_x100; } stats_t;
static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}
/* TODO C2: v[0..n) 을 정렬된 사본 tmp 에서 min/max/p50/p99(nearest-rank: rank = ceil(q·n), 값 = 정렬[rank−1])
 * 와 평균×100 (반올림 내림, 64 비트 합) 을 구한다. n == 0 이면 전부 0. tmp 는 호출자가 n 칸 준다. */
static void stats_compute(const uint32_t *v, uint32_t n, uint32_t *tmp, stats_t *s)
{
    memset(s, 0, sizeof *s);
    if (n == 0) return;
    memcpy(tmp, v, n * sizeof *tmp);
    qsort(tmp, n, sizeof *tmp, cmp_u32);
    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; ++i) sum += tmp[i];
    s->n = n; s->min = tmp[0]; s->max = tmp[n - 1];
    s->p50 = tmp[(50u * n + 99u) / 100u - 1u];
    s->p99 = tmp[(99u * n + 99u) / 100u - 1u];
    s->mean_x100 = (uint32_t)(sum * 100u / n);
}

/* TODO C3: 쌍봉(bimodal) 검출. 정렬된 값에서 이웃 간 간격이 가장 큰 곳 i (v[i+1]−v[i] 최대, 동률이면 앞쪽)
 * 을 찾고, 간격 ≥ min_gap 이고 양쪽 모두 표본이 ≥ max(1, n·min_frac‰/1000) 개면 쌍봉. 그때 lo/hi = 각 쪽의
 * 중앙값(nearest-rank 50%), frac_hi = 오른쪽 개수 ×1000 / n. 아니면 0 반환하고 출력은 0. */
typedef struct { int bimodal; uint32_t lo, hi, frac_hi_permille; } bimodal_t;
static void bimodal_detect(const uint32_t *sorted, uint32_t n, uint32_t min_gap, uint32_t min_frac_permille, bimodal_t *b)
{
    memset(b, 0, sizeof *b);
    if (n < 2) return;
    uint32_t best = 0, bi = 0;
    for (uint32_t i = 0; i + 1 < n; ++i)
        if (sorted[i + 1] - sorted[i] > best) { best = sorted[i + 1] - sorted[i]; bi = i; }
    uint32_t nl = bi + 1, nr = n - nl;
    uint32_t need = (uint32_t)((uint64_t)n * min_frac_permille / 1000u);
    if (need < 1) need = 1;
    if (best < min_gap || nl < need || nr < need) return;
    b->bimodal = 1;
    b->lo = sorted[(50u * nl + 99u) / 100u - 1u];
    b->hi = sorted[nl + (50u * nr + 99u) / 100u - 1u];
    b->frac_hi_permille = (uint32_t)((uint64_t)nr * 1000u / n);
}

/* ---------- 합성 덤프 생성기 (주어짐) — 실측 아님 ---------- */
typedef struct { const char *name; uint32_t model, model_miss, miss_permille, trig; } synth_scn_t;
static size_t synth_dump(char *buf, size_t cap, uint32_t seed, uint32_t core_hz, uint32_t calib,
                         const synth_scn_t *sc, int ns, uint32_t n_per)
{
    size_t k = 0;
    uint32_t sum = core_hz + calib, total = 0;
    g_rng = seed;
#define EMIT(...) do { int w_ = snprintf(buf + k, k < cap ? cap - k : 0, __VA_ARGS__); \
                       if (w_ < 0 || (size_t)w_ >= cap - k) return 0; k += (size_t)w_; } while (0)
    EMIT("# SYNTHETIC - BM-A10 model + xorshift32 jitter (seed 0x%08x), NOT a measurement\n", (unsigned)seed);
    EMIT("H,%u,%d\nC,%u\n", (unsigned)core_hz, ns, (unsigned)calib);
    for (int s = 0; s < ns; ++s) {
        EMIT("S,%d,%s,%u\n", s, sc[s].name, (unsigned)n_per);
        for (uint32_t i = 0; i < n_per; ++i) {
            uint32_t miss = (xs32() % 1000u) < sc[s].miss_permille;
            uint32_t jit = (xs32() % 10u) == 0;                        /* 10 % 확률 +1 사이클 */
            uint32_t raw = calib + sc[s].trig + (miss ? sc[s].model_miss : sc[s].model) + jit;
            uint32_t t0 = (i % 50u == 7u) ? 0xFFFFFFF0u + (xs32() & 7u) : xs32();   /* 가끔 감김 직전 */
            EMIT("D,%d,%u,%08x,%08x\n", s, (unsigned)i, (unsigned)t0, (unsigned)(t0 + raw));
            sum += raw; ++total;
        }
    }
    EMIT("E,%u,%08x\n", (unsigned)total, (unsigned)sum);
#undef EMIT
    return k;
}

/* ===================== 테스트 ===================== */
static cm_cfg_t cfg(uint8_t ws, uint8_t hit, uint8_t vram, uint8_t fpca, uint8_t lspen)
{
    cm_cfg_t c = { ws, hit, vram, fpca, 1, lspen };
    return c;
}

static char g_buf[200000], g_mut[4096];
static uint32_t g_tmp[MAX_SAMP], g_vals[MAX_SAMP];
static dump_t g_d, g_d2;

static void part_a(void)
{
    const cm_params_t *p = &CM4;
    printf("-- Part A: 모델 시나리오 (사이클, 모델 값 — 측정 아님)\n");
    struct { const char *nm; cm_cfg_t c; int kind; uint32_t d; uint32_t want; } T[] = {
        { "entry, 0 WS",                         cfg(0,0,0,0,1), SC_ENTRY,      0, 12 },
        { "tail-chain gap, 0 WS",                cfg(0,0,0,0,1), SC_TAIL_GAP,   0,  6 },
        { "exit (unstack) -> thread",            cfg(0,0,0,0,1), SC_EXIT,       0, 12 },
        { "back-to-back gap (exit+entry)",       cfg(0,0,0,0,1), SC_B2B_GAP,    0, 24 },
        { "entry, 5 WS, ART miss",               cfg(5,0,0,0,1), SC_ENTRY,      0, 22 },
        { "entry, 5 WS, ART hit",                cfg(5,1,0,0,1), SC_ENTRY,      0, 12 },
        { "entry, 5 WS, miss, VTOR in SRAM",     cfg(5,0,1,0,1), SC_ENTRY,      0, 17 },
        { "tail-chain gap, 5 WS miss",           cfg(5,0,0,0,1), SC_TAIL_GAP,   0, 16 },
        { "entry, FPCA=1, LSPEN=0 (full FP)",    cfg(0,0,0,1,0), SC_ENTRY,      0, 29 },
        { "entry, FPCA=1, LSPEN=1 (lazy)",       cfg(0,0,0,1,1), SC_ENTRY,      0, 12 },
        { "lazy stall at ISR's 1st FP instr",    cfg(0,0,0,1,1), SC_LAZY_STALL, 0, 17 },
        { "lazy stall, FPCA=0 (nothing to save)",cfg(0,0,0,0,1), SC_LAZY_STALL, 0,  0 },
        { "exit after lazy save happened",       cfg(0,0,0,1,1), SC_EXIT,       1, 29 },
        { "late arrival high, d=3",              cfg(0,0,0,0,1), SC_LATE_HIGH,  3,  9 },
        { "late arrival high, d=8",              cfg(0,0,0,0,1), SC_LATE_HIGH,  8,  6 },
        { "high at d=12 (low already running)",  cfg(0,0,0,0,1), SC_LATE_HIGH, 12, 12 },
        { "pop pre-emption, d=4 into exit",      cfg(0,0,0,0,1), SC_POP,        4,  6 },
        { "arrive after exit done, d=12",        cfg(0,0,0,0,1), SC_POP,       12, 12 },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; ++i) {
        uint32_t got = latency_model(p, &T[i].c, T[i].kind, T[i].d);
        printf("   %-40s %3u (expect %u)\n", T[i].nm, (unsigned)got, (unsigned)T[i].want);
        CHECK(got == T[i].want, T[i].nm);
    }
    /* 순서 관계: 모든 WS × ART hit/miss × VTOR 위치 */
    int bad_tail = 0, bad_mono = 0, bad_hit = 0, bad_late = 0, bad_pop = 0;
    for (uint8_t ws = 0; ws <= 7; ++ws)
        for (uint8_t hit = 0; hit <= 1; ++hit)
            for (uint8_t vr = 0; vr <= 1; ++vr) {
                cm_cfg_t c = cfg(ws, hit, vr, 0, 1);
                uint32_t en = latency_model(p, &c, SC_ENTRY, 0), tl = latency_model(p, &c, SC_TAIL_GAP, 0);
                uint32_t b2 = latency_model(p, &c, SC_B2B_GAP, 0);
                if (!(tl < b2) || b2 != en + latency_model(p, &c, SC_EXIT, 0)) ++bad_tail;
                if (ws && en < latency_model(p, &(cm_cfg_t){ (uint8_t)(ws - 1), hit, vr, 0, 1, 1 }, SC_ENTRY, 0)) ++bad_mono;
                if (hit && en != 12) ++bad_hit;
                for (uint32_t d = 0; d <= 30; ++d) {
                    if (latency_model(p, &c, SC_LATE_HIGH, d) > en) ++bad_late;
                    if (latency_model(p, &c, SC_POP, d) > en) ++bad_pop;
                }
            }
    CHECK(bad_tail == 0, "tail < exit+entry (=back-to-back) for every WS/ART/VTOR");
    CHECK(bad_mono == 0, "entry latency non-decreasing in WS");
    CHECK(bad_hit == 0, "ART hit: entry = 12 regardless of WS");
    CHECK(bad_late == 0, "late-arriving high IRQ never waits longer than a normal entry (d=0..30)");
    CHECK(bad_pop == 0, "pop pre-emption never worse than a normal entry (d=0..30)");

    /* 무작위 성질 검사 (고정 시드) */
    int v_once = 0, v_prio = 0, v_min = 0, v_term = 0, runs = 3000;
    g_rng = 0xBEEF1234u;
    for (int r = 0; r < runs; ++r) {
        irq_ev_t ev[MAXEV];
        sim_out_t o;
        int n = 2 + (int)(xs32() % 5u);
        cm_cfg_t c = cfg((uint8_t)(xs32() % 6u), (uint8_t)(xs32() & 1u), 0, (uint8_t)(xs32() & 1u), (uint8_t)(xs32() & 1u));
        for (int i = 0; i < n; ++i)
            ev[i] = (irq_ev_t){ xs32() % 60u, (uint8_t)(xs32() % 4u), 1u + xs32() % 30u, (uint8_t)(xs32() & 1u) };
        if (sim_run(p, &c, ev, n, &o)) { ++v_term; continue; }
        if (o.n_done != n) ++v_once;
        uint32_t lo = p->tail < p->late_min ? p->tail : p->late_min;
        for (int h = 0; h < n; ++h) {
            if (o.t_start[h] < ev[h].t_arrive + lo) ++v_min;
            for (int j = 0; j < n; ++j)            /* 시작 순간, 그 전에 와서 기다리는 더 급한 IRQ 가 있으면 위반 */
                if (j != h && ev[j].t_arrive < o.t_start[h] && o.t_start[j] > o.t_start[h] && ev[j].prio < ev[h].prio) ++v_prio;
        }
    }
    printf("   random: %d event sets, violations once=%d prio=%d min=%d unterminated=%d\n", runs, v_once, v_prio, v_min, v_term);
    CHECK(v_term == 0, "simulator terminates on every random set");
    CHECK(v_once == 0, "every IRQ handled exactly once");
    CHECK(v_prio == 0, "no handler starts while an earlier-arrived higher-priority IRQ waits");
    CHECK(v_min == 0, "no handler starts sooner than min(tail, late_min) after its arrival");
}

static size_t small_dump(char *buf, size_t cap)
{
    static const synth_scn_t S[3] = { { "entry", 12, 12, 0, 2 }, { "tail", 6, 6, 0, 0 }, { "ws5", 12, 22, 500, 2 } };
    return synth_dump(buf, cap, 0x5EEDu, 16000000u, 1u, S, 3, 3);
}

static void part_b(void)
{
    printf("-- Part B: 측정이 틀리는 길\n");
    /* B1 감김 */
    CHECK(cyc_delta(0xFFFFFFF8u, 0x0000000Cu) == 20u, "CYCCNT wrap: 0xFFFFFFF8 -> 0x0000000C = 20");
    int64_t naive = (int64_t)0x0000000Cu - (int64_t)0xFFFFFFF8u;
    printf("   wrap: unsigned delta=%u, naive int64 delta=%lld\n", (unsigned)cyc_delta(0xFFFFFFF8u, 0xCu), (long long)naive);
    CHECK(naive != 20, "naive 64-bit subtraction is wrong across the wrap (the bug)");
    int bad = 0;
    g_rng = 0x1234567u;
    for (int i = 0; i < 5000; ++i) { uint32_t t0 = xs32(), d = xs32() >> 1; if (cyc_delta(t0, t0 + d) != d) ++bad; }
    CHECK(bad == 0, "5000 random (t0, d < 2^31): delta exact");
    /* B2 보정 */
    uint32_t out = 77;
    CHECK(lat_correct(15, 1, &out) == DUMP_OK && out == 14, "raw 15 - calib 1 = 14");
    CHECK(lat_correct(0, 1, &out) == ERR_NEGATIVE, "raw < calib -> ERR_NEGATIVE, not 4294967295");
    /* B3 멈춘 CYCCNT: QEMU 실측 (board/qemu_cyccnt.txt) 처럼 t0 = t_isr = 0 */
    const char *qemu = "H,25000000,1\nC,0\nS,0,stir_entry,3\nD,0,0,00000000,00000000\nD,0,1,00000000,00000000\n"
                       "D,0,2,00000000,00000000\nE,3,017d7840\n";
    int rc = dump_parse(qemu, strlen(qemu), &g_d);
    printf("   QEMU-style all-zero dump -> %s\n", ERR_NAME[rc]);
    CHECK(rc == ERR_STUCK, "all-zero CYCCNT dump rejected as STUCK (QEMU / TRCENA forgotten)");
    /* B4 잘림: 모든 길이 */
    size_t L = small_dump(g_mut, sizeof g_mut);
    CHECK(L > 0 && dump_parse(g_mut, L, &g_d) == DUMP_OK, "small synthetic dump parses OK");
    int acc_short = 0, wrong_code = 0;
    for (size_t k = 0; k < L - 1; ++k) {
        int r2 = dump_parse(g_mut, k, &g_d2);
        if (r2 == DUMP_OK) ++acc_short;
        else if (r2 != ERR_TRUNCATED && r2 != ERR_PARSE) ++wrong_code;
    }
    printf("   truncation: %zu lengths tried, accepted=%d, other-code=%d\n", L - 1, acc_short, wrong_code);
    CHECK(acc_short == 0, "every truncated dump (lengths 0..L-2) rejected");
    CHECK(wrong_code == 0, "truncation reported as TRUNCATED or PARSE");
    CHECK(dump_parse(g_mut, L - 1, &g_d2) == DUMP_OK, "missing only the final newline is still OK");
    /* B5 1 바이트 손상 전수 */
    static char cor[4096];
    long tried = 0, accepted = 0, accepted_diff = 0;
    for (size_t pos = 0; pos < L; ++pos)
        for (int v = 0; v < 256; ++v) {
            if ((unsigned char)g_mut[pos] == v) continue;
            memcpy(cor, g_mut, L); cor[pos] = (char)v; ++tried;
            if (dump_parse(cor, L, &g_d2) != DUMP_OK) continue;
            ++accepted;
            int same = g_d2.n == g_d.n && g_d2.calib == g_d.calib && g_d2.core_hz == g_d.core_hz &&
                       memcmp(g_d2.raw, g_d.raw, g_d.n * sizeof g_d.raw[0]) == 0;
            if (!same) ++accepted_diff;
        }
    printf("   1-byte corruption: %ld variants, accepted=%ld (name/case/comment only), accepted with different data=%ld\n",
           tried, accepted, accepted_diff);
    CHECK(accepted_diff == 0, "no single-byte corruption changes the data and still passes");
    /* B6 late arrival 창 경계: d=11 이면 높은 쪽 먼저(late), d=12 이면 낮은 쪽이 먼저 시작하고 높은 쪽이 중첩 */
    {
        cm_cfg_t c = cfg(0, 0, 0, 0, 1);
        irq_ev_t ev[2] = { { 10, 2, 20, 0 }, { 21, 1, 20, 0 } };
        sim_out_t o;
        sim_run(&CM4, &c, ev, 2, &o);
        printf("   d=11: order %d,%d how(high)=%s how(low)=%s\n", o.order[0], o.order[1], HOW_NAME[o.how[1]], HOW_NAME[o.how[0]]);
        CHECK(o.order[0] == 1 && o.how[1] == HOW_LATE && o.how[0] == HOW_TAIL, "d=11: high first (late), low tail-chains after");
        ev[1].t_arrive = 22;
        sim_run(&CM4, &c, ev, 2, &o);
        printf("   d=12: order %d,%d how(high)=%s depth=%u\n", o.order[0], o.order[1], HOW_NAME[o.how[1]], (unsigned)o.max_depth);
        CHECK(o.order[0] == 0 && o.how[1] == HOW_NESTED && o.max_depth == 2, "d=12: low starts, high nests (depth 2)");
    }
    /* B7 FP: lazy 인데 ISR 이 FP 를 안 쓰면 FPCA=0 과 같다; LSPEN=0 이면 FP 안 써도 +17 */
    {
        cm_cfg_t lz = cfg(0, 0, 0, 1, 1), full = cfg(0, 0, 0, 1, 0), nf = cfg(0, 0, 0, 0, 1);
        CHECK(latency_model(&CM4, &lz, SC_ENTRY, 0) == latency_model(&CM4, &nf, SC_ENTRY, 0), "lazy + ISR without FP == no FP context");
        CHECK(latency_model(&CM4, &full, SC_ENTRY, 0) - latency_model(&CM4, &nf, SC_ENTRY, 0) == 17u, "LSPEN=0 costs +17 on every entry");
        CHECK(latency_model(&CM4, &lz, SC_EXIT, 0) == 12u, "lazy, ISR without FP: exit skips FP restore");
    }
    /* B8 보정 빼먹기 = 정확히 calib 만큼 치우침 */
    {
        uint32_t raw = 1 + 2 + 12, a, b;
        lat_correct(raw, 1, &a); lat_correct(raw, 0, &b);
        CHECK(b - a == 1u, "forgetting calibration biases every sample by exactly calib");
    }
}

static void part_c(void)
{
    printf("-- Part C: 합성 덤프 분석 (SYNTHETIC — 모델 + 지터, 실측 아님)\n");
    const cm_params_t *p = &CM4;
    cm_cfg_t c0 = cfg(0,0,0,0,1), c5m = cfg(5,0,0,0,1), c5h = cfg(5,1,0,0,1);
    cm_cfg_t fpn = cfg(0,0,0,1,0), fpl = cfg(0,0,0,1,1);
    synth_scn_t S[8] = {
        { "entry_ws0",       latency_model(p, &c0, SC_ENTRY, 0),  latency_model(p, &c0, SC_ENTRY, 0),  0,   2 },
        { "entry_ws5_art",   latency_model(p, &c5h, SC_ENTRY, 0), latency_model(p, &c5m, SC_ENTRY, 0), 200, 2 },
        { "entry_ws5_noart", latency_model(p, &c5m, SC_ENTRY, 0), latency_model(p, &c5m, SC_ENTRY, 0), 0,   2 },
        { "tail_gap_ws0",    latency_model(p, &c0, SC_TAIL_GAP, 0), latency_model(p, &c0, SC_TAIL_GAP, 0), 0, 0 },
        { "exit_ws0",        latency_model(p, &c0, SC_EXIT, 0),   latency_model(p, &c0, SC_EXIT, 0),   0,   0 },
        { "entry_fp_full",   latency_model(p, &fpn, SC_ENTRY, 0), latency_model(p, &fpn, SC_ENTRY, 0), 0,   2 },
        { "entry_fp_lazy",   latency_model(p, &fpl, SC_ENTRY, 0), latency_model(p, &fpl, SC_ENTRY, 0), 0,   2 },
        { "lazy_stall",      latency_model(p, &fpl, SC_LAZY_STALL, 0), latency_model(p, &fpl, SC_LAZY_STALL, 0), 0, 0 },
    };
    const uint32_t calib = 1, n_per = 400;
    size_t L = synth_dump(g_buf, sizeof g_buf, 0xC0FFEEu, 168000000u, calib, S, 8, n_per);
    int rc = dump_parse(g_buf, L, &g_d);
    printf("   dump %zu bytes, parse=%s, synthetic=%d, scenarios=%d, samples=%u\n", L, ERR_NAME[rc], g_d.synthetic, g_d.n_scn, (unsigned)g_d.n);
    CHECK(rc == DUMP_OK && g_d.synthetic == 1, "synthetic dump parses and is flagged SYNTHETIC");
    CHECK(g_d.n_scn == 8 && g_d.n == 8 * n_per, "8 scenarios x 400 samples");

    /* 통계 대 brute force */
    int bad = 0;
    g_rng = 0x51A7u;
    for (int r = 0; r < 2000; ++r) {
        uint32_t n = 1 + xs32() % 200u;
        stats_t s;
        for (uint32_t i = 0; i < n; ++i) g_vals[i] = xs32() % 61u;
        stats_compute(g_vals, n, g_tmp, &s);
        uint32_t mn = 0xFFFFFFFFu, mx = 0; uint64_t sum = 0;
        for (uint32_t i = 0; i < n; ++i) { if (g_vals[i] < mn) mn = g_vals[i]; if (g_vals[i] > mx) mx = g_vals[i]; sum += g_vals[i]; }
        uint32_t want[2], q[2] = { 50, 99 };
        for (int k = 0; k < 2; ++k) {              /* 정의대로: count(v ≤ x) ≥ q·n 인 가장 작은 x */
            for (uint32_t x = 0; x <= 60; ++x) {
                uint32_t cnt = 0;
                for (uint32_t i = 0; i < n; ++i) cnt += g_vals[i] <= x;
                if (100u * cnt >= q[k] * n) { want[k] = x; break; }
            }
        }
        if (s.n != n || s.min != mn || s.max != mx || s.p50 != want[0] || s.p99 != want[1] || s.mean_x100 != (uint32_t)(sum * 100u / n)) ++bad;
    }
    CHECK(bad == 0, "stats vs brute force: 2000 random arrays (min/max/p50/p99/mean)");
    stats_t s0;
    stats_compute(g_vals, 0, g_tmp, &s0);
    CHECK(s0.n == 0 && s0.max == 0, "stats of empty set = zeros");

    /* 시나리오별 분석 + 비교 표 */
    printf("   %-16s %5s %5s %5s | %4s %4s %4s %4s %7s | %s\n", "scenario", "model", "trig", "expect", "min", "p50", "p99", "max", "mean", "bimodal (lo/hi, hi share)");
    int ok_p50 = 0, n_bimodal = 0, neg = 0;
    bimodal_t bm_art = { 0, 0, 0, 0 };
    for (int s = 0; s < g_d.n_scn; ++s) {
        const scn_t *sc = &g_d.scn[s];
        for (uint32_t i = 0; i < sc->count; ++i)
            if (lat_correct(g_d.raw[sc->first + i], g_d.calib, &g_vals[i]) != DUMP_OK) ++neg;
        stats_t st; bimodal_t bm;
        stats_compute(g_vals, sc->count, g_tmp, &st);
        bimodal_detect(g_tmp, sc->count, 3, 50, &bm);
        uint32_t expect = S[s].model + S[s].trig;
        if (st.p50 == expect) ++ok_p50;
        if (bm.bimodal) ++n_bimodal;
        if (s == 1) bm_art = bm;
        printf("   %-16s %5u %5u %5u | %4u %4u %4u %4u %4u.%02u | ", sc->name, (unsigned)S[s].model, (unsigned)S[s].trig,
               (unsigned)expect, (unsigned)st.min, (unsigned)st.p50, (unsigned)st.p99, (unsigned)st.max,
               (unsigned)(st.mean_x100 / 100), (unsigned)(st.mean_x100 % 100));
        if (bm.bimodal) printf("YES %u/%u, %u.%u%%\n", (unsigned)bm.lo, (unsigned)bm.hi, (unsigned)(bm.frac_hi_permille / 10), (unsigned)(bm.frac_hi_permille % 10));
        else printf("no\n");
    }
    CHECK(neg == 0, "no sample below calibration");
    CHECK(ok_p50 == 8, "p50 - trig offset == model value for all 8 scenarios (pipeline end-to-end)");
    CHECK(n_bimodal == 1 && bm_art.bimodal, "only entry_ws5_art is bimodal (ART hit vs miss)");
    CHECK(bm_art.lo == S[1].model + 2 && bm_art.hi == S[1].model_miss + 2, "bimodal modes = hit and miss model values + trig");
    CHECK(bm_art.frac_hi_permille >= 150 && bm_art.frac_hi_permille <= 250, "miss share near the generator's 20 %");
    /* 히스토그램: ART 시나리오 */
    {
        const scn_t *sc = &g_d.scn[1];
        uint32_t h[64] = {0}, tot = 0, v;
        for (uint32_t i = 0; i < sc->count; ++i) { lat_correct(g_d.raw[sc->first + i], g_d.calib, &v); if (v < 64) ++h[v]; }
        printf("   histogram entry_ws5_art (SYNTHETIC):\n");
        for (int b = 0; b < 64; ++b) if (h[b]) {
            printf("   %3d | %4u ", b, (unsigned)h[b]);
            for (uint32_t k = 0; k < h[b] / 8; ++k) putchar('#');
            putchar('\n'); tot += h[b];
        }
        CHECK(tot == sc->count, "histogram counts sum to n");
    }
}

int main(void)
{
    part_a();
    part_b();
    part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
