/* ARCH-M14 AXI 핸드셰이크 모델: 5채널과 VALID/READY
 *
 *   Part A  AXI4 5채널(AW/W/B/AR/R) 사이클 모델: VALID 를 handshake 까지 유지하는 소스, INCR 버스트
 *           beat 주소, 다중 ID 응답 스케줄러(같은 ID 는 순서대로 / 다른 ID 는 out-of-order),
 *           고정 시드 무작위 VALID 지연 · READY 패턴
 *   Part B  수동(passive) 프로토콜 체커: VALID 유지 · payload 안정 · WLAST · B 의존성(마지막 W, AW) ·
 *           RLAST · 같은 ID 순서 · 4 KB 경계 — 규칙마다 위반을 주입한 테스트가 그 규칙을 잡는지 확인
 *   Part C  outstanding 과 대역폭(Little 의 법칙) 닫힌 식, "VALID 가 READY 를 기다리면" 데드락,
 *           무작위 회귀(시드 200개) + kill matrix
 *
 * 모델 단순화: 버스트는 INCR 만, 데이터 버스 32비트(AxSIZE ≤ 2), 데이터는 beat 주소의 2^size 바이트를
 *             하위 정렬로 싣는다(실제 byte lane · WSTRB · WRAP 은 SOCX-B02), 배타 접근 없음(SOCX-B03).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================= 공통 ================= */
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static uint32_t seed_mix(uint32_t s) {   /* 작은 시드 → 잘 섞인 0 아닌 상태 (xorshift 초반 출력 편향 방지) */
    s ^= s >> 16; s *= 0x7FEB352Du; s ^= s >> 15; s *= 0x846CA68Bu; s ^= s >> 16;
    return s ? s : 1u;
}

enum { CH_AW, CH_W, CH_B, CH_AR, CH_R, NCH };
static const char *const CH_NAME[NCH] = { "AW", "W", "B", "AR", "R" };

typedef struct {                 /* 다섯 채널의 payload 를 한 구조체로 (채널마다 쓰는 필드만 의미, 나머지 0) */
    uint8_t  id, len, size, resp;    /* AW/AR: id len size · B: id resp · R: id resp */
    uint32_t addr, data;             /* AW/AR: addr · W/R: data */
    bool     last;                   /* W: WLAST · R: RLAST */
} pay_t;
typedef struct { bool valid, ready; pay_t p; } chan_t;
typedef struct { chan_t ch[NCH]; } bus_t;              /* 한 사이클 동안의 선(wire) 값 */

static bool pay_eq(const pay_t *a, const pay_t *b) {
    return a->id == b->id && a->len == b->len && a->size == b->size && a->resp == b->resp &&
           a->addr == b->addr && a->data == b->data && a->last == b->last;
}

enum { RESP_OKAY = 0, RESP_SLVERR = 2, RESP_DECERR = 3 };
#define MEM_SIZE 0x10000u          /* 서브오디네이트: 0x0000–0xFFFF 매핑, 그 밖은 DECERR */

/* 메모리 초기 패턴 (스코어보드도 같은 식으로 기대값을 만든다) */
static uint8_t pat(uint32_t a) { return (uint8_t)(a * 0x9Du ^ (a >> 8) ^ 0x5Au); }
static uint32_t pat_word(uint32_t addr, unsigned size) {
    if (addr >= MEM_SIZE) return 0;
    uint32_t v = 0;
    for (unsigned i = 0; i < (1u << size); i++) v |= (uint32_t)pat(addr + i) << (8 * i);
    return v;
}
static uint8_t exp_resp(uint32_t addr) { return addr < MEM_SIZE ? RESP_OKAY : RESP_DECERR; }

/* ================= Part A-1: INCR 버스트 주소와 4 KB 경계 ================= */
/* INCR 버스트의 n 번째 beat 주소 (n = 0..len). 첫 beat 는 시작 주소 그대로, 그 다음부터는
 * 2^size 로 정렬한 시작 주소 + n·2^size (AXI 스펙의 INCR 주소 식). */
static uint32_t axi_beat_addr(uint32_t addr, unsigned size, unsigned n) {
    (void)size; (void)n;
    return addr; /* TODO: 첫 beat 는 addr, 그 뒤는 정렬된 시작 주소 + n·2^size */
}
/* 버스트가 건드리는 마지막 바이트가 첫 바이트와 다른 4 KB 페이지에 있나? (AXI: 금지) */
static bool axi_crosses_4k(uint32_t addr, unsigned len, unsigned size) {
    (void)addr; (void)len; (void)size;
    return false; /* TODO: 마지막 바이트 주소 계산 → 4 KB 페이지 번호 비교 */
}

/* ================= Part A-2: VALID/READY 소스 ================= */
#define QCAP 1024
typedef struct {
    pay_t    q[QCAP];
    unsigned head, n;          /* 보낼 항목 큐 */
    bool     valid;            /* 지금 VALID 를 내고 있나 — handshake 전까지 내리면 안 된다 */
    unsigned wait;             /* VALID 를 올리기 전에 더 쉴 사이클 (무작위 지연) */
    unsigned max_delay;        /* 항목마다 0..max_delay 사이클 쉼 */
    uint32_t rng;
    bool     polite, saw_ready;/* Part C 데드락 데모 전용 "틀린 소스": READY 를 본 뒤에만 VALID */
} src_t;

static unsigned src_draw(src_t *s) { return s->max_delay ? xs32(&s->rng) % (s->max_delay + 1u) : 0u; }
static void src_init(src_t *s, unsigned max_delay, uint32_t seed) {
    memset(s, 0, sizeof *s);
    s->max_delay = max_delay; s->rng = seed_mix(seed); s->wait = src_draw(s);
}
static bool src_push(src_t *s, pay_t p) {
    if (s->n == QCAP) return false;
    s->q[(s->head + s->n) % QCAP] = p; s->n++;
    return true;
}
/* 이 사이클의 VALID 와 payload 를 정한다. READY 는 보지 않는다(볼 수도 없다 — 인자에 없음).
 * 보낼 것이 있고 VALID 가 아직 낮으면: wait 가 남았으면 하나 줄이고 쉬고, 0 이면 VALID 를 올린다.
 * VALID 가 낮을 때 payload 는 0 으로 둔다. */
static void src_drive(src_t *s, chan_t *c) {
    (void)s;
    c->valid = false; c->p = (pay_t){ 0 }; /* TODO: wait 세기 → VALID 올리기 → handshake 까지 유지 */
}
/* 클럭 엣지. hs = 이번 사이클 VALID && READY. handshake 가 없으면 아무것도 바꾸지 않는다(VALID·payload 유지).
 * handshake 면 항목을 빼고 VALID 를 내리고 다음 항목의 지연을 새로 뽑는다(src_draw). */
static void src_edge(src_t *s, bool hs) {
    (void)s; (void)hs; /* TODO: handshake 면 pop · VALID 내림 · 다음 지연 뽑기, 아니면 아무것도 안 함 */
}
/* 하네스 쪽 래퍼: polite(틀린) 소스는 직전 사이클 READY 를 못 봤으면 VALID 를 안 올린다 */
static void src_drive_any(src_t *s, chan_t *c) {
    if (s->polite && !s->valid && !s->saw_ready) { c->valid = false; c->p = (pay_t){ 0 }; return; }
    src_drive(s, c);
}

/* READY 를 내는 쪽 */
enum { RDY_RAND, RDY_ALWAYS, RDY_LAZY };
typedef struct { int mode; unsigned pct; uint32_t rng; bool saw_valid; } sink_t;
static bool sink_ready(sink_t *k) {
    if (k->mode == RDY_ALWAYS) return true;
    if (k->mode == RDY_LAZY) return k->saw_valid;           /* 직전 사이클 VALID 를 본 뒤에만 (합법) */
    return xs32(&k->rng) % 100u < k->pct;
}

/* ================= Part A-3: 응답 스케줄러 (다중 ID) ================= */
#define NPEND 64
typedef struct { bool used; uint8_t id; uint32_t seq; long ready_at; } pend_t;
/* 다음에 응답할 항목을 고른다. 후보 = used && ready_at <= now && 같은 ID 의 더 오래된(seq 작은) used 항목이
 * 없는 것. 후보가 있으면 xs32(rng) % 후보수 번째를 고른다(다른 ID 끼리는 순서 자유 = out-of-order).
 * 반환: 인덱스, 후보가 없으면 -1. */
static int sub_pick(const pend_t *e, int n, long now, uint32_t *rng) {
    (void)e; (void)n; (void)now; (void)rng;
    return -1; /* TODO: 같은 ID 의 가장 오래된 것만 후보, 후보 중 무작위 */
}

/* ================= 서브오디네이트(slave) 모델 — 주어짐 ================= */
enum {                        /* 주입할 버그 (테스트벤치가 켬) */
    F_DROP_VALID = 1u << 0,   /* 선: inj_ch 의 VALID 를 handshake 전에 한 사이클 내림 */
    F_CHG_PAYLOAD = 1u << 1,  /* 선: inj_ch 의 payload 를 기다리는 중에 바꿈 */
    F_WLAST = 1u << 2,        /* 매니저: WLAST 를 마지막 하나 전 beat 에 */
    F_B_EARLY = 1u << 3,      /* 서브: AW 만 받고 W 가 끝나기 전에 B */
    F_B_NO_AW = 1u << 4,      /* 서브: W 버스트만 받고 AW 전에 B (ID 0) */
    F_RLAST = 1u << 5,        /* 서브: RLAST 를 마지막 하나 전 beat 에 */
    F_REORDER = 1u << 6,      /* 서브: 같은 ID 안에서 젊은 응답을 먼저 (다른 ID 끼리는 원래 자유) */
};
typedef struct { uint8_t id, len, size; uint32_t addr; unsigned done; bool b_sent; } wr_t;
typedef struct { uint8_t len, size, resp; uint32_t addr; unsigned beat; } rd_t;
typedef struct {
    uint8_t  mem[MEM_SIZE];
    wr_t     aw_q[NPEND]; int aw_n;            /* AW 받은 순서 = W 순서 (AXI4 는 WID 없음) */
    pay_t    wbuf[QCAP]; unsigned wb_head, wb_n;
    pend_t   bp[NPEND]; uint8_t b_resp[NPEND]; int b_fly;      /* b_fly = B 소스에 올라간 항목 */
    pend_t   rp[NPEND]; rd_t rd[NPEND]; int r_cur, r_fly;
    src_t    b, r;
    sink_t   awk, wk, ark;
    unsigned b_lat, r_lat, slow_lat, fault, wlast_seen, aw_seen, pre_b;
    bool     interleave;
    uint32_t seq, rng;
} sub_t;

static int free_slot(const pend_t *e) { for (int i = 0; i < NPEND; i++) if (!e[i].used) return i; return -1; }
/* F_REORDER 용: 고른 항목 k 보다 젊은, 같은 ID 의 준비된 항목이 있으면 그것으로 바꿔치기 (같은 ID 순서만 깸) */
static int reorder_same_id(const pend_t *e, int k, long now) {
    int y = k;
    if (k < 0) return k;
    for (int i = 0; i < NPEND; i++)
        if (e[i].used && e[i].id == e[k].id && e[i].ready_at <= now && e[i].seq > e[y].seq) y = i;
    return y;
}
static void sub_push_b(sub_t *s, uint8_t id, uint8_t resp, long ready_at) {
    int k = free_slot(s->bp);
    if (k < 0) return;
    s->bp[k] = (pend_t){ true, id, s->seq++, ready_at }; s->b_resp[k] = resp;
}
/* 사이클 시작: B/R 소스가 비었으면 다음 응답 beat 를 올려 둔다 */
static void sub_fill(sub_t *s, long t) {
    if (s->b.n == 0 && !s->b.valid) {
        int k = sub_pick(s->bp, NPEND, t, &s->rng);
        if (s->fault & F_REORDER) k = reorder_same_id(s->bp, k, t);
        if (k >= 0) { src_push(&s->b, (pay_t){ .id = s->bp[k].id, .resp = s->b_resp[k] }); s->b_fly = k; }
    }
    if (s->r.n == 0 && !s->r.valid) {
        int k = s->r_cur;
        if (k < 0 || s->interleave) {
            k = sub_pick(s->rp, NPEND, t, &s->rng);
            if ((s->fault & F_REORDER) && k >= 0 && s->rd[k].beat == 0) k = reorder_same_id(s->rp, k, t);
        }
        if (k >= 0) {
            rd_t *d = &s->rd[k];
            uint32_t a = axi_beat_addr(d->addr, d->size, d->beat), v = 0;
            for (unsigned i = 0; i < (1u << d->size) && d->resp == RESP_OKAY && a + i < MEM_SIZE; i++)
                v |= (uint32_t)s->mem[a + i] << (8 * i);
            bool last = (s->fault & F_RLAST) ? d->beat + 1u == d->len : d->beat == d->len;
            src_push(&s->r, (pay_t){ .id = s->rp[k].id, .data = v, .resp = d->resp, .last = last });
            s->r_cur = k; s->r_fly = k;
        }
    }
}
/* 쓰기 진행: AW 순서대로 W beat 를 메모리에 쓰고, 버스트가 끝나면 B 를 예약 */
static void sub_writes(sub_t *s, long t) {
    while (s->aw_n > 0 && s->wb_n > 0) {
        wr_t *w = &s->aw_q[0];
        pay_t d = s->wbuf[s->wb_head]; s->wb_head = (s->wb_head + 1u) % QCAP; s->wb_n--;
        uint32_t a = axi_beat_addr(w->addr, w->size, w->done);
        for (unsigned i = 0; i < (1u << w->size) && exp_resp(w->addr) == RESP_OKAY && a + i < MEM_SIZE; i++)
            s->mem[a + i] = (uint8_t)(d.data >> (8 * i));
        if (++w->done == w->len + 1u) {
            if (!w->b_sent) {
                if (s->pre_b > 0) s->pre_b--;                     /* F_B_NO_AW 로 이미 답함 */
                else sub_push_b(s, w->id, exp_resp(w->addr), t + 1 + (long)s->b_lat);
            }
            memmove(&s->aw_q[0], &s->aw_q[1], (size_t)(--s->aw_n) * sizeof s->aw_q[0]);
        }
    }
}
/* 클럭 엣지: handshake 된 것 받기 · 응답 진행 */
static void sub_edge(sub_t *s, const bus_t *w, const bool *hs, long t) {
    if (hs[CH_AW]) {
        const pay_t *p = &w->ch[CH_AW].p;
        s->aw_q[s->aw_n++] = (wr_t){ p->id, p->len, p->size, p->addr, 0, false };
        s->aw_seen++;
        if (s->fault & F_B_EARLY) { sub_push_b(s, p->id, exp_resp(p->addr), t + 1 + (long)s->b_lat); s->aw_q[s->aw_n - 1].b_sent = true; }
    }
    if (hs[CH_W]) {
        s->wbuf[(s->wb_head + s->wb_n) % QCAP] = w->ch[CH_W].p; s->wb_n++;
        if (w->ch[CH_W].p.last && ++s->wlast_seen > s->aw_seen && (s->fault & F_B_NO_AW)) {
            sub_push_b(s, 0, RESP_OKAY, t + 1); s->pre_b++;
        }
    }
    if (hs[CH_AR]) {
        const pay_t *p = &w->ch[CH_AR].p;
        int k = free_slot(s->rp);
        long lat = (long)s->r_lat + (p->addr >= 0xC000u && p->addr < MEM_SIZE ? (long)s->slow_lat : 0);   /* 0xC000~ = 느린 영역 */
        s->rp[k] = (pend_t){ true, p->id, s->seq++, t + 1 + lat };
        s->rd[k] = (rd_t){ p->len, p->size, exp_resp(p->addr), p->addr, 0 };
    }
    if (hs[CH_B]) s->bp[s->b_fly].used = false;
    if (hs[CH_R]) {
        rd_t *d = &s->rd[s->r_fly];
        if (++d->beat > d->len) { s->rp[s->r_fly].used = false; s->r_cur = -1; }
    }
    sub_writes(s, t);
}

/* ================= 매니저(master) 모델 — 주어짐 ================= */
#define MAXT 256
#define MAXBEATS 8192
typedef struct { bool write; uint8_t id, len, size; uint32_t addr; } txn_t;
typedef struct {
    const txn_t *t; int nt, next_w, next_r, ot_w, ot_r, max_ot, ndone;
    int wiss[MAXT], nwiss, riss[MAXT], nriss;     /* 낸 순서 */
    bool done[MAXT]; unsigned rbeat[MAXT], off[MAXT]; uint8_t bresp[MAXT], rresp[MAXT];
    uint32_t rdat[MAXBEATS];
    src_t aw, w, ar; sink_t bk, rk;
    unsigned fault;
} mgr_t;
static uint32_t wdata(int i, unsigned b) { return (uint32_t)i * 0x01000193u ^ b * 0x9E3779B9u ^ 0xC0DE0000u; }
static void mgr_issue(mgr_t *m) {
    while (m->next_w < m->nt && !m->t[m->next_w].write) m->next_w++;
    if (m->next_w < m->nt && m->ot_w < m->max_ot) {
        int i = m->next_w++; const txn_t *x = &m->t[i];
        src_push(&m->aw, (pay_t){ .id = x->id, .len = x->len, .size = x->size, .addr = x->addr });
        for (unsigned b = 0; b <= x->len; b++) {
            bool last = (m->fault & F_WLAST) ? b + 1u == x->len : b == x->len;
            src_push(&m->w, (pay_t){ .data = wdata(i, b), .last = last });
        }
        m->wiss[m->nwiss++] = i; m->ot_w++;
    }
    while (m->next_r < m->nt && m->t[m->next_r].write) m->next_r++;
    if (m->next_r < m->nt && m->ot_r < m->max_ot) {
        int i = m->next_r++; const txn_t *x = &m->t[i];
        src_push(&m->ar, (pay_t){ .id = x->id, .len = x->len, .size = x->size, .addr = x->addr });
        m->riss[m->nriss++] = i; m->ot_r++;
    }
}
static int oldest_open(const mgr_t *m, const int *iss, int n, uint8_t id) {
    for (int k = 0; k < n; k++) if (!m->done[iss[k]] && m->t[iss[k]].id == id) return iss[k];
    return -1;
}
static void mgr_edge(mgr_t *m, const bus_t *w, const bool *hs) {
    if (hs[CH_B]) {
        int i = oldest_open(m, m->wiss, m->nwiss, w->ch[CH_B].p.id);
        if (i >= 0) { m->done[i] = true; m->bresp[i] = w->ch[CH_B].p.resp; m->ot_w--; m->ndone++; }
    }
    if (hs[CH_R]) {
        int i = oldest_open(m, m->riss, m->nriss, w->ch[CH_R].p.id);
        if (i >= 0) {
            if (m->off[i] + m->rbeat[i] < MAXBEATS) m->rdat[m->off[i] + m->rbeat[i]] = w->ch[CH_R].p.data;
            m->rresp[i] |= w->ch[CH_R].p.resp;
            if (++m->rbeat[i] == m->t[i].len + 1u) { m->done[i] = true; m->ot_r--; m->ndone++; }
        }
    }
}

/* ================= Part B: 수동 프로토콜 체커 ================= */
enum { V_VALID_DROP, V_PAYLOAD, V_WLAST, V_B_EARLY, V_B_NO_AW, V_RLAST, V_ID_ORDER, V_4K, V_UNEXP_R, V_DATA, NV };
static const char *const V_NAME[NV] = { "VALID_DROP", "PAYLOAD_CHANGE", "WLAST", "B_BEFORE_WLAST", "B_BEFORE_AW",
                                        "RLAST", "SAME_ID_ORDER", "CROSS_4KB", "R_WITHOUT_AR", "DATA_RESP" };
typedef struct { uint8_t id, len, size; uint32_t addr; long hs; bool done; unsigned beats; } cx_t;   /* 본 AW/AR */
typedef struct { unsigned nbeats; int last_idx; long last_hs; } wb_t;                              /* W 버스트 */
typedef struct {
    chan_t prev[NCH]; bool have_prev;
    cx_t aw[MAXT]; int naw;
    wb_t wb[MAXT]; int wcur;          /* wb[k] = k 번째 W 버스트 (W 는 AW 순서). wcur = 지금 받는 버스트 */
    cx_t ar[MAXT]; int nar;
    unsigned cnt[NV]; int first, first_ch; long first_t;
} chk_t;

static void chk_init(chk_t *c) { memset(c, 0, sizeof *c); c->first = -1; for (int k = 0; k < MAXT; k++) c->wb[k].last_idx = -1; }
static void chk_flag(chk_t *c, int rule, int ch, long t) {
    c->cnt[rule]++;
    if (c->first < 0) { c->first = rule; c->first_ch = ch; c->first_t = t; }
}
static unsigned chk_total(const chk_t *c) { unsigned s = 0; for (int r = 0; r < NV; r++) s += c->cnt[r]; return s; }
/* 이번 사이클에 새로 VALID 가 올라왔나 (직전에 기다리던 같은 항목이 아니라) */
static bool chk_is_new(const chk_t *c, int ch, const chan_t *cur) {
    return cur->valid && !(c->have_prev && c->prev[ch].valid && !c->prev[ch].ready);
}

/* 규칙 1·2 (모든 채널): 직전 사이클이 "VALID=1, READY=0" (기다리는 중) 이었다면 이번 사이클에
 *   VALID 가 0 → V_VALID_DROP,  VALID 는 1 인데 payload 가 다름 → V_PAYLOAD.  c->prev[ch] 가 직전 사이클 값. */
static void chk_stable(chk_t *c, int ch, const chan_t *cur, long t) {
    (void)c; (void)ch; (void)cur; (void)t; (void)pay_eq; /* TODO: 규칙 1·2 */
}

/* 규칙 3 (W beat handshake 마다): 지금 버스트 wb[wcur] 의 beat 번호 idx 를 센다.
 * 그 버스트의 AW 를 이미 봤으면 (wcur < naw) WLAST 는 정확히 idx == len 일 때만 1 이어야 한다 → 아니면 V_WLAST.
 * (AW 를 아직 못 봤으면 검사는 AW 가 올 때 chk_on_ax 가 대신 한다.)  WLAST 면 last_idx/last_hs 기록, 다음 버스트로. */
static void chk_w_beat(chk_t *c, const pay_t *p, long t) {
    (void)c; (void)p; (void)t; /* TODO: 규칙 3 — beat 세기, WLAST 검사, 버스트 경계 기록 */
}

/* 규칙 4 (B 채널, BVALID 가 1 인 사이클마다): k = 아직 B 를 못 받은 같은 BID 의 가장 오래된 AW.
 *   새로 올라온 BVALID 라면(newp):  k 없음 → V_B_NO_AW (AW handshake 보다 먼저 B)
 *                                  k 의 W 버스트가 아직 안 끝남(k >= wcur) → V_B_EARLY
 *                                  (AW·마지막 W handshake 는 반드시 "이전" 사이클이어야 한다 — 같은 사이클의
 *                                   handshake 는 아직 기록 전이라 자동으로 위반이 된다)
 *                                  BRESP 가 exp_resp(aw[k].addr) 와 다름 → 같은 ID 의 더 젊은 미완료 AW 중
 *                                   기대 응답이 맞는 것이 있으면 V_ID_ORDER, 없으면 V_DATA
 *   hs 면 aw[k].done = true. */
static void chk_b(chk_t *c, const pay_t *p, bool newp, bool hs, long t) {
    (void)c; (void)p; (void)newp; (void)hs; (void)t; /* TODO: 규칙 4 — B 의존성 + 응답 대조 */
}

/* 규칙 5·6 (R beat handshake 마다): k = 같은 RID 의 가장 오래된 미완료 AR. 없으면 V_UNEXP_R.
 *   idx = ar[k].beats++ ;  ok = data/resp 가 기대값(pat_word(beat 주소, size), exp_resp(시작 주소))과 같나
 *   ok 가 아니고 같은 RID 의 더 젊은 미완료 AR 의 같은 idx beat 기대값과 맞으면 → V_ID_ORDER (이 경우 RLAST 는 안 봄)
 *   그 밖에는: RLAST != (idx == len) → V_RLAST,  !ok → V_DATA.
 *   idx == len 이면 그 AR 완료 (체커 자신의 셈 — RLAST 를 믿지 않는다). */
static void chk_r(chk_t *c, const pay_t *p, bool hs, long t) {
    (void)c; (void)p; (void)hs; (void)t; /* TODO: 규칙 5·6 — RLAST, 같은 ID 순서, 데이터 */
}

/* AW/AR handshake 기록 (주어짐): 4 KB 규칙 + W 가 AW 보다 먼저 왔던 버스트의 WLAST 뒤늦은 검사 */
static void chk_on_ax(chk_t *c, int ch, const pay_t *p, long t) {
    if (axi_crosses_4k(p->addr, p->len, p->size)) chk_flag(c, V_4K, ch, t);
    cx_t *list = ch == CH_AW ? c->aw : c->ar;
    int *n = ch == CH_AW ? &c->naw : &c->nar;
    if (*n >= MAXT) return;
    list[*n] = (cx_t){ p->id, p->len, p->size, p->addr, t, false, 0 };
    int k = (*n)++;
    if (ch == CH_AW) {
        const wb_t *w = &c->wb[k];
        if (k < c->wcur && w->last_idx != (int)p->len) chk_flag(c, V_WLAST, CH_W, t);       /* 이미 끝난 버스트 */
        else if (k == c->wcur && w->nbeats > p->len) chk_flag(c, V_WLAST, CH_W, t);         /* len+1 beat 넘게 WLAST 없음 */
    }
}

/* 한 사이클 관찰 (주어짐). 순서: 안정성 → B/R(이번 사이클 handshake 기록 전 상태로) → AW/W/AR 기록 */
static void chk_cycle(chk_t *c, const bus_t *b, long t) {
    for (int ch = 0; ch < NCH; ch++) chk_stable(c, ch, &b->ch[ch], t);
    const chan_t *B = &b->ch[CH_B], *R = &b->ch[CH_R];
    if (B->valid) chk_b(c, &B->p, chk_is_new(c, CH_B, B), B->ready, t);
    if (R->valid) chk_r(c, &R->p, R->ready, t);
    for (int ch = 0; ch < NCH; ch++) {
        const chan_t *x = &b->ch[ch];
        if (!(x->valid && x->ready)) continue;
        if (ch == CH_AW || ch == CH_AR) chk_on_ax(c, ch, &x->p, t);
        else if (ch == CH_W) chk_w_beat(c, &x->p, t);
    }
    c->prev[0] = b->ch[0]; c->prev[1] = b->ch[1]; c->prev[2] = b->ch[2]; c->prev[3] = b->ch[3]; c->prev[4] = b->ch[4];
    c->have_prev = true;
}

/* ================= Part C: outstanding 과 대역폭 ================= */
/* 모델 조건: 지연 0 · READY 항상 1 · 읽기만 · 버스트 beats 개 · 서브 지연 lat(AR handshake 다음 사이클부터 lat 사이클 뒤
 * 첫 R beat) · 매니저는 RLAST handshake 다음 사이클에 다음 AR 을 낸다.
 * → 한 버스트의 왕복(round trip) = lat + beats + 1 사이클. R 채널을 쉬지 않게 하려면 그 동안 R 을 채울 만큼
 *   outstanding 이 있어야 한다 (Little 의 법칙: 동시에 진행 중인 양 = 처리량 × 지연). 최소 K 를 정수로. */
static unsigned ot_needed(unsigned lat, unsigned beats) {
    (void)lat; (void)beats;
    return 0; /* TODO: 왕복 사이클 / beats 올림 */
}
/* n 개 버스트(n 은 k 의 배수) 를 outstanding k 로 읽을 때 총 사이클.
 *   부족할 때: k 개 묶음이 왕복마다 한 번 → (n/k)·rt + (k−1)·beats
 *   충분할 때: 첫 데이터까지 lat+1, 그 뒤 R 채널이 매 사이클 1 beat → lat + 1 + n·beats
 *   실제는 둘 중 큰 값. */
static long pred_read_cycles(unsigned n, unsigned beats, unsigned lat, unsigned k) {
    (void)n; (void)beats; (void)lat; (void)k;
    return 0; /* TODO: max(부족할 때 식, 충분할 때 식) */
}

/* ================= 시뮬레이터 — 주어짐 ================= */
typedef struct {
    const txn_t *t; int nt; uint32_t seed;
    unsigned m_delay, s_delay;          /* VALID 지연 최대: 매니저(AW/W/AR) · 서브(B/R) */
    int s_rdy_mode; unsigned s_rdy_pct; /* 서브 AW/W/AR READY */
    unsigned m_rdy_pct;                 /* 매니저 B/R READY (%) */
    unsigned b_lat, r_lat, slow_lat, aw_lag; int max_ot; bool interleave, polite_ar;
    unsigned fault; int inj_ch;
} cfg_t;
typedef struct {
    mgr_t m; sub_t s; chk_t c;
    long cycles; bool hung; unsigned hs_count[NCH];
    bool injected; chan_t inj_prev;
    void (*tap)(long t, const bus_t *b);
} sim_t;
static sim_t g_sim;
#define HANG_CYCLES 300
#define MAX_CYCLES 200000

static void inject(sim_t *S, const cfg_t *g, bus_t *w) {
    chan_t *x = &w->ch[g->inj_ch];
    bool waiting = S->inj_prev.valid && !S->inj_prev.ready;   /* 직전 사이클 선 값 기준 */
    if (!S->injected && waiting && x->valid) {
        if (g->fault & F_DROP_VALID) { x->valid = false; x->p = (pay_t){ 0 }; S->injected = true; }
        else if (g->fault & F_CHG_PAYLOAD) {
            if (g->inj_ch == CH_W || g->inj_ch == CH_R) x->p.data ^= 1u; else x->p.addr ^= 4u;
            S->injected = true;
        }
    }
    S->inj_prev = *x;
}

static long sim_run(const cfg_t *g) {
    sim_t *S = &g_sim;
    void (*tap)(long, const bus_t *) = S->tap;
    memset(S, 0, sizeof *S); S->tap = tap;
    mgr_t *m = &S->m; sub_t *s = &S->s;
    m->t = g->t; m->nt = g->nt; m->max_ot = g->max_ot > 0 ? g->max_ot : 8; m->fault = g->fault;
    for (int i = 0, o = 0; i < g->nt; i++) { m->off[i] = (unsigned)o; o += g->t[i].len + 1; }
    src_init(&m->aw, g->m_delay, g->seed * 3u + 1u); src_init(&m->w, g->m_delay, g->seed * 5u + 2u);
    src_init(&m->ar, g->m_delay, g->seed * 7u + 3u);
    m->aw.wait += g->aw_lag; m->ar.polite = g->polite_ar;
    m->bk = (sink_t){ RDY_RAND, g->m_rdy_pct, g->seed * 11u + 4u, false };
    m->rk = (sink_t){ RDY_RAND, g->m_rdy_pct, g->seed * 13u + 5u, false };
    if (g->m_rdy_pct >= 100) m->bk.mode = m->rk.mode = RDY_ALWAYS;
    m->bk.rng = seed_mix(m->bk.rng); m->rk.rng = seed_mix(m->rk.rng);
    for (uint32_t a = 0; a < MEM_SIZE; a++) s->mem[a] = pat(a);
    src_init(&s->b, g->s_delay, g->seed * 17u + 6u); src_init(&s->r, g->s_delay, g->seed * 19u + 7u);
    s->awk = (sink_t){ g->s_rdy_mode, g->s_rdy_pct, g->seed * 23u + 8u, false };
    s->wk = (sink_t){ g->s_rdy_mode, g->s_rdy_pct, g->seed * 29u + 9u, false };
    s->ark = (sink_t){ g->s_rdy_mode, g->s_rdy_pct, g->seed * 31u + 10u, false };
    s->awk.rng = seed_mix(s->awk.rng); s->wk.rng = seed_mix(s->wk.rng); s->ark.rng = seed_mix(s->ark.rng);
    s->b_lat = g->b_lat; s->r_lat = g->r_lat; s->slow_lat = g->slow_lat; s->interleave = g->interleave; s->fault = g->fault;
    s->rng = seed_mix(g->seed * 37u + 11u); s->r_cur = -1;
    chk_init(&S->c);

    long idle = 0;
    for (long t = 0; t < MAX_CYCLES; t++) {
        mgr_issue(m);
        sub_fill(s, t);
        bus_t w; memset(&w, 0, sizeof w);
        src_drive_any(&m->aw, &w.ch[CH_AW]); src_drive_any(&m->w, &w.ch[CH_W]); src_drive_any(&m->ar, &w.ch[CH_AR]);
        src_drive_any(&s->b, &w.ch[CH_B]); src_drive_any(&s->r, &w.ch[CH_R]);
        w.ch[CH_AW].ready = sink_ready(&s->awk) && s->aw_n < NPEND;
        w.ch[CH_W].ready = sink_ready(&s->wk) && s->wb_n < QCAP;
        w.ch[CH_AR].ready = sink_ready(&s->ark) && free_slot(s->rp) >= 0;
        w.ch[CH_B].ready = sink_ready(&m->bk);
        w.ch[CH_R].ready = sink_ready(&m->rk);
        if (g->fault & (F_DROP_VALID | F_CHG_PAYLOAD)) inject(S, g, &w);
        chk_cycle(&S->c, &w, t);
        if (S->tap) S->tap(t, &w);
        bool hs[NCH], any = false;
        for (int ch = 0; ch < NCH; ch++) { hs[ch] = w.ch[ch].valid && w.ch[ch].ready; any |= hs[ch]; S->hs_count[ch] += hs[ch]; }
        src_t *src[NCH] = { &m->aw, &m->w, &s->b, &m->ar, &s->r };
        sink_t *snk[NCH] = { &s->awk, &s->wk, &m->bk, &s->ark, &m->rk };
        for (int ch = 0; ch < NCH; ch++) {
            src_edge(src[ch], hs[ch]);
            src[ch]->saw_ready = w.ch[ch].ready;
            snk[ch]->saw_valid = w.ch[ch].valid && !hs[ch];
        }
        sub_edge(s, &w, hs, t);
        mgr_edge(m, &w, hs);
        if (m->ndone == m->nt) { S->cycles = t + 1; return S->cycles; }
        idle = any ? 0 : idle + 1;
        if (idle >= HANG_CYCLES) { S->hung = true; S->cycles = t + 1; return -1; }
    }
    S->hung = true; return -1;
}

/* ---------- 트래픽 생성 · 기대값 ---------- */
static txn_t g_tx[MAXT];
static uint8_t g_expmem[MEM_SIZE];
/* 쓰기는 0x0000–0x7FFF 의 겹치지 않는 256 B 칸, 읽기는 0x8000–0xFFFF (안 쓰는 영역 → 패턴 그대로),
 * 약 1/10 은 매핑 안 된 0x20000 영역(DECERR). 버스트는 4 KB 를 넘지 않게 고른다. */
static int gen_traffic(uint32_t seed, int n) {
    uint32_t r = seed_mix(seed);
    for (int i = 0; i < n; i++) {
        txn_t *x = &g_tx[i];
        x->write = xs32(&r) & 1u; x->id = (uint8_t)(xs32(&r) % 4u); x->len = (uint8_t)(xs32(&r) % 16u);
        x->size = (uint8_t)(xs32(&r) % 4u == 0 ? xs32(&r) % 2u : 2u);
        uint32_t nb = 1u << x->size, span = (x->len + 1u) * nb;
        if (xs32(&r) % 10u == 0) x->addr = 0x20000u + (uint32_t)i * 0x100u;
        else if (x->write) x->addr = (uint32_t)i * 0x100u + (xs32(&r) % (256u - span + 1u)) / nb * nb;
        else {
            uint32_t page = 0x8000u + (xs32(&r) % 8u) * 0x1000u;
            x->addr = page + (xs32(&r) % (4096u - span + 1u)) / nb * nb;
        }
    }
    return n;
}
static void build_expmem(const txn_t *t, int n) {
    for (uint32_t a = 0; a < MEM_SIZE; a++) g_expmem[a] = pat(a);
    for (int i = 0; i < n; i++) {
        if (!t[i].write || t[i].addr >= MEM_SIZE) continue;
        for (unsigned b = 0; b <= t[i].len; b++) {
            uint32_t a = axi_beat_addr(t[i].addr, t[i].size, b), d = wdata(i, b);
            for (unsigned k = 0; k < (1u << t[i].size); k++) g_expmem[a + k] = (uint8_t)(d >> (8 * k));
        }
    }
}
/* 끝난 시뮬레이션이 맞게 동작했나: 전부 완료 · 메모리 · 읽은 데이터 · 응답 코드 */
static bool sim_correct(const txn_t *t, int n) {
    const mgr_t *m = &g_sim.m;
    if (g_sim.hung || m->ndone != n) return false;
    build_expmem(t, n);
    if (memcmp(g_sim.s.mem, g_expmem, MEM_SIZE) != 0) return false;
    for (int i = 0; i < n; i++) {
        if (t[i].write) { if (m->bresp[i] != exp_resp(t[i].addr)) return false; continue; }
        if (m->rresp[i] != exp_resp(t[i].addr)) return false;
        for (unsigned b = 0; b <= t[i].len; b++)
            if (m->rdat[m->off[i] + b] != pat_word(axi_beat_addr(t[i].addr, t[i].size, b), t[i].size)) return false;
    }
    return true;
}
static cfg_t cfg_rand(uint32_t seed, int n) {
    return (cfg_t){ .t = g_tx, .nt = n, .seed = seed, .m_delay = 3, .s_delay = 2, .s_rdy_mode = RDY_RAND,
                    .s_rdy_pct = 50, .m_rdy_pct = 60, .b_lat = 3, .r_lat = 6, .slow_lat = 10, .max_ot = 8, .interleave = (seed & 1u) != 0,
                    .inj_ch = CH_W };
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 체커 직접 테스트용: 손으로 만든 사이클 */
static chk_t g_c;
static void cyc(long t, int ch, bool v, bool r, pay_t p) {
    bus_t b; memset(&b, 0, sizeof b);
    b.ch[ch] = (chan_t){ v, r, p };
    chk_cycle(&g_c, &b, t);
}
static pay_t AX(uint8_t id, uint32_t addr, uint8_t len) { return (pay_t){ .id = id, .addr = addr, .len = len, .size = 2 }; }
static pay_t WD(uint32_t d, bool last) { return (pay_t){ .data = d, .last = last }; }
static pay_t BR(uint8_t id) { return (pay_t){ .id = id }; }

/* 주입 실험 하나: 무작위 트래픽 + 버그 → 체커가 처음 잡은 규칙 */
static int run_fault(uint32_t seed, unsigned fault, int inj_ch) {
    int n = gen_traffic(seed, 40);
    cfg_t g = cfg_rand(seed, n);
    g.fault = fault; g.inj_ch = inj_ch;
    if (fault & F_B_NO_AW) g.aw_lag = 150;
    if (fault & F_B_EARLY) g.b_lat = 0;
    if (fault & F_REORDER) { g.r_lat = 12; g.b_lat = 12; }
    if (fault == 0x100u) {                       /* 4 KB: 드라이버 버그 — 읽기 하나를 페이지 끝에 걸치게 */
        for (int i = 0; i < n; i++) if (!g_tx[i].write) { g_tx[i].addr = 0x8FF8u; g_tx[i].len = 3; g_tx[i].size = 2; break; }
        g.fault = 0;
    }
    sim_run(&g);
    return g_sim.c.first;
}

int main(void) {
    /* ---------- Part A-1: beat 주소 · 4 KB ---------- */
    CHECK(axi_beat_addr(0x1000, 2, 0) == 0x1000 && axi_beat_addr(0x1000, 2, 3) == 0x100C);
    CHECK(axi_beat_addr(0x1002, 2, 0) == 0x1002 && axi_beat_addr(0x1002, 2, 1) == 0x1004);   /* 비정렬 시작 */
    CHECK(axi_beat_addr(0x2001, 0, 7) == 0x2008 && axi_beat_addr(0x2006, 1, 2) == 0x200A);
    CHECK(!axi_crosses_4k(0x0FF0, 3, 2) && axi_crosses_4k(0x0FF0, 4, 2));                    /* 16 B 는 OK, 20 B 는 넘음 */
    CHECK(!axi_crosses_4k(0x0000, 255, 4) && axi_crosses_4k(0x0010, 255, 4));                /* 256 × 16 B = 정확히 4 KB */
    CHECK(!axi_crosses_4k(0x0FFE, 0, 2) && axi_crosses_4k(0x0FFC, 1, 2));
    {   /* 무작위 대조: 바이트를 하나씩 세어 본 것과 같다 */
        uint32_t r = 0xA14u; int ok = 1;
        for (int i = 0; i < 20000; i++) {
            unsigned size = xs32(&r) % 4u, len = xs32(&r) % 256u; uint32_t addr = xs32(&r) & 0xFFFFu;
            uint32_t nb = 1u << size, a0 = addr, aend = axi_beat_addr(addr, size, len) | (nb - 1u);
            if (axi_crosses_4k(addr, len, size) != ((a0 >> 12) != (aend >> 12))) ok = 0;
            for (unsigned b = 1; b <= len && ok; b++)
                if (axi_beat_addr(addr, size, b) - axi_beat_addr(addr, size, b - 1) != (b == 1 ? nb - (addr & (nb - 1)) : nb)) ok = 0;
        }
        CHECK(ok);
    }

    /* ---------- Part A-2: 소스는 handshake 까지 VALID·payload 를 유지 ---------- */
    {
        static src_t s; chan_t c; src_init(&s, 0, 1);
        src_push(&s, (pay_t){ .data = 11 }); src_push(&s, (pay_t){ .data = 22 });
        src_drive(&s, &c); CHECK(c.valid && c.p.data == 11);
        src_edge(&s, false); src_drive(&s, &c); CHECK(c.valid && c.p.data == 11);          /* READY=0: 그대로 */
        src_edge(&s, false); src_drive(&s, &c); CHECK(c.valid && c.p.data == 11);
        src_edge(&s, true);  src_drive(&s, &c); CHECK(c.valid && c.p.data == 22);          /* 지연 0 → 바로 다음 */
        src_edge(&s, true);  src_drive(&s, &c); CHECK(!c.valid && c.p.data == 0 && s.n == 0);
    }
    {   /* 무작위 지연: VALID 는 내려가지 않고, 순서·개수가 보존된다 */
        static src_t s; chan_t c, pv = { 0 }; uint32_t r = 7; int ok = 1; unsigned got = 0, waits = 0;
        src_init(&s, 5, 99);
        for (unsigned i = 0; i < 200; i++) src_push(&s, (pay_t){ .data = i });
        for (int t = 0; t < 5000 && got < 200; t++) {
            src_drive(&s, &c);
            if (pv.valid && !pv.ready && (!c.valid || c.p.data != pv.p.data)) ok = 0;
            c.ready = xs32(&r) % 3u == 0;
            if (c.valid && c.ready) { if (c.p.data != got) ok = 0; got++; }
            if (!c.valid && s.n > 0) waits++;
            src_edge(&s, c.valid && c.ready); pv = c;
        }
        CHECK(ok && got == 200 && waits > 100);
    }

    /* ---------- Part A-3: 응답 순서 ---------- */
    {
        pend_t e[NPEND]; memset(e, 0, sizeof e); uint32_t r = 5;
        e[0] = (pend_t){ true, 1, 10, 50 };   /* ID1 가장 오래됨, 아직 준비 안 됨 */
        e[1] = (pend_t){ true, 1, 11, 0 };    /* ID1 두 번째: 준비됐지만 첫 번째를 추월 불가 */
        e[2] = (pend_t){ true, 2, 12, 0 };    /* ID2: 준비됨 → 먼저 나가도 된다 (OoO) */
        CHECK(sub_pick(e, NPEND, 10, &r) == 2);
        e[2].used = false;
        CHECK(sub_pick(e, NPEND, 10, &r) == -1);           /* ID1 의 오래된 것이 막고 있음 */
        CHECK(sub_pick(e, NPEND, 50, &r) == 0);
        e[0].used = false;
        CHECK(sub_pick(e, NPEND, 50, &r) == 1);
        e[3] = (pend_t){ true, 3, 13, 0 }; e[4] = (pend_t){ true, 0, 14, 0 };
        int seen = 0;
        for (int i = 0; i < 64; i++) { int k = sub_pick(e, NPEND, 60, &r); if (k == 1) seen |= 1; if (k == 3) seen |= 2; if (k == 4) seen |= 4; if (k != 1 && k != 3 && k != 4) seen |= 8; }
        CHECK(seen == 7);                                  /* 서로 다른 ID 셋이 모두 뽑힌다 */
    }
    /* 전체 시뮬레이션: 깨끗한 트래픽 → 모두 완료, 메모리·데이터 정확, 체커 위반 0 */
    {
        int n = gen_traffic(1, 40); cfg_t g = cfg_rand(1, n);
        CHECK(sim_run(&g) > 0 && sim_correct(g_tx, n));
        CHECK(chk_total(&g_sim.c) == 0);
        g = cfg_rand(2, gen_traffic(2, 40));
        CHECK(sim_run(&g) > 0 && sim_correct(g_tx, 40) && chk_total(&g_sim.c) == 0);
    }

    /* ---------- Part B-1: 체커 직접 (손으로 만든 사이클) ---------- */
    chk_init(&g_c); cyc(0, CH_AR, 1, 0, AX(0, 0x8000, 0)); cyc(1, CH_AR, 0, 0, AX(0, 0, 0));
    CHECK(g_c.first == V_VALID_DROP && g_c.first_ch == CH_AR && g_c.first_t == 1);
    chk_init(&g_c); cyc(0, CH_AW, 1, 0, AX(0, 0x100, 0)); cyc(1, CH_AW, 1, 1, AX(0, 0x104, 0));
    CHECK(g_c.first == V_PAYLOAD && g_c.first_ch == CH_AW);
    chk_init(&g_c);                                         /* READY 는 VALID 없이 올렸다 내려도 된다 */
    cyc(0, CH_W, 0, 1, WD(0, 0)); cyc(1, CH_W, 0, 0, WD(0, 0)); cyc(2, CH_W, 1, 0, WD(5, 1)); cyc(3, CH_W, 1, 1, WD(5, 1));
    CHECK(g_c.cnt[V_VALID_DROP] == 0 && g_c.cnt[V_PAYLOAD] == 0);
    chk_init(&g_c);                                         /* WLAST 가 beat 1 에 (len 2 인데) */
    cyc(0, CH_AW, 1, 1, AX(0, 0x100, 2)); cyc(1, CH_W, 1, 1, WD(1, 0)); cyc(2, CH_W, 1, 1, WD(2, 1));
    CHECK(g_c.first == V_WLAST && g_c.first_t == 2);
    chk_init(&g_c);                                         /* W 가 AW 보다 먼저, WLAST 없이 3 beat — AW(len 1) 가 와서야 판정 */
    cyc(0, CH_W, 1, 1, WD(1, 0)); cyc(1, CH_W, 1, 1, WD(2, 0)); cyc(2, CH_W, 1, 1, WD(3, 0));
    CHECK(g_c.first == -1);
    cyc(3, CH_AW, 1, 1, AX(0, 0x100, 1));
    CHECK(g_c.first == V_WLAST && g_c.first_t == 3);
    chk_init(&g_c);                                         /* 합법: AW → W(last) → 다음 사이클 B */
    cyc(0, CH_AW, 1, 1, AX(3, 0x100, 0)); cyc(1, CH_W, 1, 1, WD(9, 1)); cyc(2, CH_B, 1, 1, BR(3));
    CHECK(chk_total(&g_c) == 0 && g_c.aw[0].done);
    chk_init(&g_c);                                         /* B 가 마지막 W 와 같은 사이클 → 너무 이름 */
    cyc(0, CH_AW, 1, 1, AX(3, 0x100, 0));
    { bus_t b; memset(&b, 0, sizeof b); b.ch[CH_W] = (chan_t){ 1, 1, WD(9, 1) }; b.ch[CH_B] = (chan_t){ 1, 0, BR(3) }; chk_cycle(&g_c, &b, 1); }
    CHECK(g_c.first == V_B_EARLY && g_c.first_t == 1);
    chk_init(&g_c);                                         /* W 는 다 왔는데 AW 전에 B (AXI4 위반, AXI3 에선 허용이었던 모양) */
    cyc(0, CH_W, 1, 1, WD(9, 1)); cyc(1, CH_B, 1, 1, BR(0));
    CHECK(g_c.first == V_B_NO_AW);
    chk_init(&g_c);                                         /* RLAST 빠짐: len 1 인데 beat 1 에 RLAST=0 */
    cyc(0, CH_AR, 1, 1, AX(2, 0x8000, 1));
    cyc(1, CH_R, 1, 1, (pay_t){ .id = 2, .data = pat_word(0x8000, 2) });
    cyc(2, CH_R, 1, 1, (pay_t){ .id = 2, .data = pat_word(0x8004, 2) });
    CHECK(g_c.first == V_RLAST && g_c.first_t == 2);
    chk_init(&g_c);                                         /* 같은 ID 두 읽기, 두 번째 데이터가 먼저 → 순서 위반 */
    cyc(0, CH_AR, 1, 1, AX(1, 0x8000, 0)); cyc(1, CH_AR, 1, 1, AX(1, 0x9000, 0));
    cyc(5, CH_R, 1, 1, (pay_t){ .id = 1, .data = pat_word(0x9000, 2), .last = 1 });
    CHECK(g_c.first == V_ID_ORDER);
    chk_init(&g_c);                                         /* 다른 ID 면 순서가 바뀌어도 합법 */
    cyc(0, CH_AR, 1, 1, AX(1, 0x8000, 0)); cyc(1, CH_AR, 1, 1, AX(2, 0x9000, 0));
    cyc(5, CH_R, 1, 1, (pay_t){ .id = 2, .data = pat_word(0x9000, 2), .last = 1 });
    cyc(6, CH_R, 1, 1, (pay_t){ .id = 1, .data = pat_word(0x8000, 2), .last = 1 });
    CHECK(chk_total(&g_c) == 0);
    chk_init(&g_c); cyc(0, CH_AR, 1, 1, AX(0, 0x8FF8, 3));
    CHECK(g_c.first == V_4K);

    /* ---------- Part B-2: 전체 시뮬레이션에 버그 주입 → 그 규칙이 처음 잡힌다 ---------- */
    CHECK(run_fault(3, 0, CH_W) == -1);                     /* 대조군: 같은 트래픽, 버그 없음 */
    {
        const struct { unsigned fault; int ch, expect; } inj[] = {
            { F_DROP_VALID, CH_AR, V_VALID_DROP }, { F_DROP_VALID, CH_W, V_VALID_DROP },
            { F_CHG_PAYLOAD, CH_W, V_PAYLOAD },    { F_CHG_PAYLOAD, CH_AW, V_PAYLOAD },
            { F_WLAST, CH_W, V_WLAST },            { F_B_EARLY, CH_W, V_B_EARLY },
            { F_B_NO_AW, CH_W, V_B_NO_AW },        { F_RLAST, CH_W, V_RLAST },
            { F_REORDER, CH_W, V_ID_ORDER },       { 0x100u, CH_W, V_4K },
        };
        for (size_t i = 0; i < sizeof inj / sizeof inj[0]; i++) {
            int got = run_fault(3, inj[i].fault, inj[i].ch);
            CHECK(got == inj[i].expect);
            if (got >= 0) printf("info: inject %-14s -> %-14s on %-2s at cycle %ld (total %u)\n", V_NAME[inj[i].expect],
                                 V_NAME[got], CH_NAME[g_sim.c.first_ch], g_sim.c.first_t, chk_total(&g_sim.c));
        }
    }

    /* ---------- Part C-1: outstanding 과 대역폭 ---------- */
    CHECK(ot_needed(20, 8) == 4 && ot_needed(0, 1) == 2 && ot_needed(100, 4) == 27 && ot_needed(7, 16) == 2);
    {
        static txn_t rd[48];
        for (int i = 0; i < 48; i++) rd[i] = (txn_t){ false, (uint8_t)(i % 4), 7, 2, 0x8000u + (uint32_t)i * 32u };
        int ok = 1;
        const unsigned ks[] = { 1, 2, 3, 4, 6, 8 };
        for (int j = 0; j < 6; j++) {
            cfg_t g = { .t = rd, .nt = 48, .seed = 1, .s_rdy_mode = RDY_ALWAYS, .m_rdy_pct = 100, .r_lat = 20, .max_ot = (int)ks[j] };
            long cy = sim_run(&g);
            if (cy != pred_read_cycles(48, 8, 20, ks[j]) || !sim_correct(rd, 48)) ok = 0;
        }
        CHECK(ok);
        CHECK(pred_read_cycles(48, 8, 20, 1) == 48 * 29 && pred_read_cycles(48, 8, 20, 4) == 20 + 1 + 48 * 8);
        CHECK(pred_read_cycles(48, 8, 20, 3) > pred_read_cycles(48, 8, 20, 4) &&
              pred_read_cycles(48, 8, 20, 8) == pred_read_cycles(48, 8, 20, 4));
    }

    /* ---------- Part C-2: VALID 가 READY 를 기다리면 — 데드락 ---------- */
    {
        static txn_t rd[4];
        for (int i = 0; i < 4; i++) rd[i] = (txn_t){ false, 0, 3, 2, 0x8000u + (uint32_t)i * 16u };
        cfg_t g = { .t = rd, .nt = 4, .seed = 9, .s_rdy_mode = RDY_LAZY, .m_rdy_pct = 100, .r_lat = 2, .polite_ar = true };
        CHECK(sim_run(&g) == -1 && g_sim.hung && g_sim.hs_count[CH_AR] == 0);
        CHECK(chk_total(&g_sim.c) == 0);                    /* 수동 체커에게 이건 "조용한 버스" 일 뿐 */
        g.s_rdy_mode = RDY_ALWAYS;                          /* 같은 (틀린) 매니저 + READY 를 미리 올리는 서브 → 잘 됨 */
        CHECK(sim_run(&g) > 0 && sim_correct(rd, 4));
        g.s_rdy_mode = RDY_LAZY; g.polite_ar = false;       /* 올바른 매니저 + 게으른(합법) 서브 → 잘 됨 */
        CHECK(sim_run(&g) > 0 && sim_correct(rd, 4) && chk_total(&g_sim.c) == 0);
    }

    /* ---------- Part C-3: 무작위 회귀 200 시드 + kill matrix ---------- */
    {
        int clean = 0;
        for (uint32_t seed = 100; seed < 300; seed++) {
            int n = gen_traffic(seed, 40); cfg_t g = cfg_rand(seed, n);
            if (sim_run(&g) > 0 && sim_correct(g_tx, n) && chk_total(&g_sim.c) == 0) clean++;
        }
        CHECK(clean == 200);
        const unsigned faults[] = { F_DROP_VALID, F_CHG_PAYLOAD, F_WLAST, F_B_EARLY, F_B_NO_AW, F_RLAST, F_REORDER, 0x100u };
        const int expect[] = { V_VALID_DROP, V_PAYLOAD, V_WLAST, V_B_EARLY, V_B_NO_AW, V_RLAST, V_ID_ORDER, V_4K };
        int all = 1;
        for (int f = 0; f < 8; f++) {
            int hit = 0;
            for (uint32_t seed = 100; seed < 150; seed++) hit += run_fault(seed, faults[f], CH_W) == expect[f];
            if (hit != 50) { all = 0; printf("info: %-15s %d/50\n", V_NAME[expect[f]], hit); }
        }
        CHECK(all);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
