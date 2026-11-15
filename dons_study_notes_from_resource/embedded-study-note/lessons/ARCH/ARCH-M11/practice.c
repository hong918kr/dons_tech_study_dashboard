/* ARCH-M11 추가 연습 — 한 번에 검증
 *
 * 본문 solution.c 의 하드웨어 모델을 그대로 넣고 (연습 2 를 위해 TX FIFO 와 F_DREQ_DST 만 추가) 연습 답을 붙였다.
 *   연습 1  시작 전 체인 검사: 플로이드(Floyd) 순환 검출 — O(1) 메모리로 루프·나쁜 포인터를 미리 찾기
 *   연습 2  mem → 주변장치 TX: 목적지 쪽 DREQ (TX FIFO 빈자리) — 처음엔 몰아서, 그다음엔 선로 속도로
 *   연습 3  FIFO 깊이 공식: 우선순위가 높으면 ceil(N/P), 같은 레벨 RR 은 대역폭 조건 N ≤ (P-1)·Bu 가 먼저
 *   연습 4  디스크립터 페치 비용을 넣은 scatter-gather 효율과 최소 조각 크기
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 메모리 맵과 디스크립터 형식 (주어짐) ======================= */
#define SRAM_BASE  0x20000000u
#define SRAM_SIZE  0x10000u                   /* 64 KB                                         */
#define PER_BASE   0x40001000u                /* 모델 주변장치 (UART RX 같은 것)                */
#define PER_SR     (PER_BASE + 0u)            /* 읽기 전용: FIFO 레벨                           */
#define PER_DR     (PER_BASE + 4u)            /* 읽기 = RX FIFO 에서 꺼냄, 쓰기 = TX 싱크       */

enum { D_SRC = 0, D_DST = 4, D_LEN = 8, D_NEXT = 12, D_FLAGS = 16, DESC_BYTES = 20 };
#define F_IRQ       (1u << 0)                 /* 이 디스크립터가 끝나면 완료(TC) 인터럽트        */
#define F_SRC_INC   (1u << 1)                 /* 0 = 소스 주소 고정 (주변장치 DR)               */
#define F_DST_INC   (1u << 2)
#define F_ESIZE(x)  ((uint32_t)(x) << 4)      /* 비트 5:4 — 0:1 B, 1:2 B, 2:4 B, 3: 예약(에러)  */
#define F_DREQ_SRC  (1u << 8)                 /* 소스가 주변장치: DREQ 가 설 때만 비트를 낸다    */
#define F_DREQ_DST  (1u << 9)                 /* 연습 2: 목적지가 주변장치 TX FIFO — 빈자리가 있을 때만 */

typedef enum { E_NONE, E_LOOP, E_DESC, E_CFG, E_LEN0, E_ALIGN, E_BUS } dma_err_t;
static const char *const err_name[] = { "NONE", "LOOP", "DESC", "CFG", "LEN0", "ALIGN", "BUS" };

static uint8_t g_sram[SRAM_SIZE];

static bool sram_ok(uint32_t a, uint32_t n) { return a >= SRAM_BASE && a - SRAM_BASE <= SRAM_SIZE - n; }
static uint32_t rd32(uint32_t a) {
    const uint8_t *p = &g_sram[a - SRAM_BASE];
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void wr32(uint32_t a, uint32_t v) {
    uint8_t *p = &g_sram[a - SRAM_BASE];
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint8_t *P(uint32_t a) { return &g_sram[a - SRAM_BASE]; }
static void wdesc(uint32_t a, uint32_t src, uint32_t dst, uint32_t len, uint32_t next, uint32_t flags) {
    wr32(a + D_SRC, src); wr32(a + D_DST, dst); wr32(a + D_LEN, len); wr32(a + D_NEXT, next); wr32(a + D_FLAGS, flags);
}

/* ======================= 주변장치 모델 ======================= */
#define FIFO_CAP 64u
typedef struct {
    uint32_t period, phase, depth;    /* phase, phase+period, ... 사이클마다 1 B 생산. depth ≤ FIFO_CAP */
    const uint8_t *src; uint32_t n, produced;
    uint8_t  fifo[FIFO_CAP]; uint32_t head, count;
    uint32_t overrun, underrun, max_level;
    uint8_t  tx[256]; uint32_t ntx;   /* TX 선로로 실제 나간 바이트                              */
    /* 연습 2: TX FIFO — DR 쓰기가 들어가고, tx_period 사이클마다 1 B 가 선로로 나간다 */
    uint32_t tx_period, tx_phase, tx_depth;
    uint8_t  txq[FIFO_CAP]; uint32_t txhead, txcount, tx_overflow;
} periph_t;
static periph_t g_per;

/* (본문 TODO C1)  이번 사이클이 생산 시점이면 다음 바이트를 FIFO 에 넣는다. FIFO 가 꽉 찼으면 그 새 바이트를 잃고
 * overrun++ (UART ORE 처럼 새 데이터가 버려진다). 넣은 뒤 max_level 갱신. 생산 시점 = cycle ≥ phase 이고
 * (cycle - phase) % period == 0, 그리고 아직 n 바이트를 다 만들지 않았을 때 (period 0 = 생산 안 함). */
static void periph_tick(periph_t *p, uint32_t cycle) {
    if (p->tx_period && cycle >= p->tx_phase && (cycle - p->tx_phase) % p->tx_period == 0 && p->txcount) {
        if (p->ntx < sizeof p->tx) p->tx[p->ntx++] = p->txq[p->txhead];       /* 시프트 레지스터로 1 B */
        p->txhead = (p->txhead + 1) % FIFO_CAP; p->txcount--;
    }
    if (p->period == 0 || p->produced >= p->n || cycle < p->phase || (cycle - p->phase) % p->period) return;
    uint8_t b = p->src[p->produced++];
    if (p->count == p->depth) { p->overrun++; return; }
    p->fifo[(p->head + p->count) % FIFO_CAP] = b;
    p->count++;
    if (p->count > p->max_level) p->max_level = p->count;
}
/* (본문 TODO C1)  FIFO 에서 n 바이트(1/2/4)를 꺼내 리틀 엔디언으로 *v 에 모은다. 모자라면 아무것도 꺼내지 않고
 * *v = 0, underrun++ (버스 에러는 아님 — DR 은 매핑돼 있다). */
static void periph_pop(periph_t *p, uint32_t n, uint32_t *v) {
    *v = 0;
    if (p->count < n) { p->underrun++; return; }
    for (uint32_t i = 0; i < n; i++) {
        *v |= (uint32_t)p->fifo[p->head] << (8 * i);
        p->head = (p->head + 1) % FIFO_CAP;
        p->count--;
    }
}

/* 버스: SRAM 범위 안이거나 DR/SR 이면 OK, 나머지는 버스 에러 (false) — 주어짐 */
static bool bus_rd(uint32_t a, uint32_t n, uint32_t *v) {
    if (sram_ok(a, n)) {
        *v = 0;
        for (uint32_t i = 0; i < n; i++) *v |= (uint32_t)g_sram[a - SRAM_BASE + i] << (8 * i);
        return true;
    }
    if (a == PER_DR) { periph_pop(&g_per, n, v); return true; }
    if (a == PER_SR && n == 4) { *v = g_per.count; return true; }
    return false;
}
static bool bus_wr(uint32_t a, uint32_t n, uint32_t v) {
    if (sram_ok(a, n)) {
        for (uint32_t i = 0; i < n; i++) g_sram[a - SRAM_BASE + i] = (uint8_t)(v >> (8 * i));
        return true;
    }
    if (a == PER_DR) {
        for (uint32_t i = 0; i < n; i++) {
            if (g_per.txcount == g_per.tx_depth) { g_per.tx_overflow++; continue; }   /* 꽉 찬 FIFO 에 쓰기 → 잃음 */
            g_per.txq[(g_per.txhead + g_per.txcount) % FIFO_CAP] = (uint8_t)(v >> (8 * i));
            g_per.txcount++;
        }
        return true;
    }
    return false;                                     /* SR 쓰기, 미매핑 → 버스 에러 */
}

/* ======================= DMA 엔진 상태 (주어짐) ======================= */
#define NCH       4
#define TRACE_MAX 2048u
typedef enum { CH_IDLE, CH_BUSY, CH_DONE, CH_ERROR } ch_state_t;
typedef struct {
    uint8_t  prio, burst; uint16_t max_desc;          /* 설정: 우선순위 0..3, 버스트 ≥ 1, 루프 가드(0=무제한) */
    ch_state_t st;
    uint32_t cur; bool loaded;                         /* 현재 디스크립터 주소, 레지스터로 읽어 들였나 */
    uint32_t src, dst, len, left, next, flags, esize;  /* 읽어 들인 디스크립터 (src/dst 는 진행하며 바뀜) */
    uint32_t ndesc, total;                             /* 이번 시작 후 가져온 디스크립터 수, 옮긴 바이트 */
    dma_err_t err; uint32_t err_desc, err_addr, err_done;
    uint32_t first_beat;                               /* 첫 비트 사이클 (없으면 UINT32_MAX)        */
} chan_t;
typedef struct { uint32_t cycle; int ch; bool err; dma_err_t code; uint32_t desc, n, addr; } irq_t;
typedef struct {
    chan_t   ch[NCH];
    uint32_t cycle;
    int      gch; uint32_t gleft;                      /* 현재 그랜트: 채널(-1 = 없음), 남은 비트 수 */
    int      rr_last[4];                               /* 우선순위 레벨별 마지막 승자 (라운드로빈)   */
    irq_t    irq[64]; uint32_t nirq;
    char     trace[TRACE_MAX + 1];                     /* 사이클별 버스 사용 채널 '0'..'3', 빈 사이클 '.' */
    uint32_t beats, idle;
} dma_t;

static void irq_push(dma_t *e, int ch, bool err, dma_err_t code, uint32_t desc, uint32_t n, uint32_t addr) {
    if (e->nirq < 64) e->irq[e->nirq++] = (irq_t){ e->cycle, ch, err, code, desc, n, addr };
}
/* 채널을 멈추고 상태를 남긴 뒤 에러 인터럽트 (주어짐). 전송 도중이면 done = 그 디스크립터에서 끝낸 바이트 */
static void chan_error(dma_t *e, chan_t *c, dma_err_t code, uint32_t addr) {
    int i = (int)(c - e->ch);
    c->st = CH_ERROR; c->err = code; c->err_desc = c->cur; c->err_addr = addr;
    c->err_done = c->loaded ? c->len - c->left : 0;
    irq_push(e, i, true, code, c->cur, c->err_done, addr);
    if (e->gch == i) e->gch = -1;
}
/* DREQ: 주변장치 소스면 FIFO 에 원소 하나(esize 바이트)가 있어야 준비됨 (주어짐) */
static bool chan_ready(const chan_t *c) {
    return c->st == CH_BUSY && c->loaded && (!(c->flags & F_DREQ_SRC) || g_per.count >= c->esize)
        && (!(c->flags & F_DREQ_DST) || g_per.tx_depth - g_per.txcount >= c->esize);     /* 연습 2 */
}

/* ======================= Part A — 디스크립터 페치, 비트, 완료 ======================= */

/* (본문 TODO A1)  c->cur 의 디스크립터를 읽어 채널 레지스터에 싣는다. 검사 순서 고정 — 첫 실패에서 chan_error 후 false:
 *   ① 루프 가드: max_desc != 0 이고 이미 max_desc 개를 가져왔으면 E_LOOP (addr = cur)
 *   ② 디스크립터 주소: 4 B 정렬이 아니거나 20 B 전체가 SRAM 안이 아니면 E_DESC (addr = cur)
 *   ── 여기서 5 워드를 읽고 ndesc++ ──
 *   ③ 원소 크기 코드 3 → E_CFG   ④ len == 0 → E_LEN0   ⑤ src/dst/len 이 esize 배수가 아니면 E_ALIGN
 *   (③~⑤ 의 addr = cur). 성공하면 src,dst,len,next,flags,esize 설정, left = len, loaded = true. */
static bool desc_fetch(dma_t *e, chan_t *c) {
    if (c->max_desc && c->ndesc >= c->max_desc) { chan_error(e, c, E_LOOP, c->cur); return false; }
    if ((c->cur & 3u) || !sram_ok(c->cur, DESC_BYTES)) { chan_error(e, c, E_DESC, c->cur); return false; }
    uint32_t src = rd32(c->cur + D_SRC), dst = rd32(c->cur + D_DST), len = rd32(c->cur + D_LEN);
    uint32_t next = rd32(c->cur + D_NEXT), flags = rd32(c->cur + D_FLAGS);
    c->ndesc++;
    uint32_t code = (flags >> 4) & 3u;
    if (code == 3u) { chan_error(e, c, E_CFG, c->cur); return false; }
    if (len == 0) { chan_error(e, c, E_LEN0, c->cur); return false; }
    uint32_t es = 1u << code;
    if ((src | dst | len) & (es - 1u)) { chan_error(e, c, E_ALIGN, c->cur); return false; }
    c->src = src; c->dst = dst; c->len = len; c->left = len; c->next = next; c->flags = flags; c->esize = es;
    c->loaded = true;
    return true;
}

/* (본문 TODO A2)  원소 하나를 옮긴다: bus_rd(src) → bus_wr(dst). 읽기나 쓰기가 버스 에러면 그 주소로
 * chan_error(E_BUS) 하고 끝 (그 원소는 끝난 것으로 치지 않는다). 성공하면 INC 플래그대로 주소 증가,
 * left -= esize, total += esize. */
static void do_beat(dma_t *e, chan_t *c) {
    uint32_t v;
    if (!bus_rd(c->src, c->esize, &v)) { chan_error(e, c, E_BUS, c->src); return; }
    if (!bus_wr(c->dst, c->esize, v)) { chan_error(e, c, E_BUS, c->dst); return; }
    if (c->flags & F_SRC_INC) c->src += c->esize;
    if (c->flags & F_DST_INC) c->dst += c->esize;
    c->left -= c->esize;
    c->total += c->esize;
}

/* (본문 TODO A3)  left == 0 이 된 직후 호출. F_IRQ 면 완료 인터럽트(TC: desc = cur, n = len). 그다음 next == 0 이면
 * 채널 DONE, 아니면 cur = next 로 옮기고 loaded = false (다음 사이클 페치). */
static void desc_complete(dma_t *e, chan_t *c) {
    if (c->flags & F_IRQ) irq_push(e, (int)(c - e->ch), false, E_NONE, c->cur, c->len, 0);
    c->loaded = false;
    if (c->next == 0) c->st = CH_DONE;
    else c->cur = c->next;
}

/* ======================= Part B — 중재와 버스트 ======================= */

/* (본문 TODO B1)  준비된(chan_ready) 채널 중 우선순위 레벨이 가장 높은 것. 같은 레벨 안에서는 라운드로빈:
 * rr_last[lvl] 다음 번호부터 차례로 보고 처음 준비된 채널. 승자를 rr_last 에 기록. 없으면 -1. */
static int arbitrate(dma_t *e) {
    for (int lvl = 3; lvl >= 0; lvl--) {
        for (int k = 1; k <= NCH; k++) {
            int i = (e->rr_last[lvl] + k) % NCH;
            if (e->ch[i].prio == lvl && chan_ready(&e->ch[i])) { e->rr_last[lvl] = i; return i; }
        }
    }
    return -1;
}

/* (본문 TODO B2)  이번 사이클에 버스를 쓸 채널을 정하고 비트 하나를 실행, 그 채널 번호(없으면 -1)를 돌려준다.
 *   - 현재 그랜트(gch)가 있고 gleft > 0 이고 그 채널이 아직 준비돼 있으면 계속 (비선점 — 더 높은 우선순위가
 *     와도 버스트가 끝날 때까지 기다린다). 아니면 arbitrate() 로 새 그랜트, gleft = 그 채널의 burst.
 *   - 비트 실행 → first_beat 기록(처음이면), gleft--. 채널이 아직 BUSY 이고 left == 0 이면 desc_complete,
 *     그리고 그랜트 종료 (버스트는 디스크립터 경계를 넘지 않는다). 에러면 chan_error 가 그랜트를 끝낸다.
 *   - 빈 사이클이면 idle++, 비트면 beats++. */
static int grant_step(dma_t *e) {
    if (e->gch >= 0 && (e->gleft == 0 || !chan_ready(&e->ch[e->gch]))) e->gch = -1;
    if (e->gch < 0) {
        e->gch = arbitrate(e);
        if (e->gch < 0) { e->idle++; return -1; }
        e->gleft = e->ch[e->gch].burst;
    }
    int g = e->gch;
    chan_t *c = &e->ch[g];
    if (c->first_beat == UINT32_MAX) c->first_beat = e->cycle;
    do_beat(e, c);
    e->beats++;
    e->gleft--;
    if (c->st == CH_BUSY && c->left == 0) { desc_complete(e, c); e->gch = -1; }
    return g;
}

/* ======================= 엔진 바깥 (주어짐) ======================= */
static void engine_step(dma_t *e) {
    periph_tick(&g_per, e->cycle);                      /* ① 주변장치 생산이 먼저 — 같은 사이클에 옮길 수 있다 */
    for (int i = 0; i < NCH; i++)                        /* ② 페치 유닛: 시간 0 으로 디스크립터를 읽음      */
        if (e->ch[i].st == CH_BUSY && !e->ch[i].loaded) desc_fetch(e, &e->ch[i]);
    int g = grant_step(e);                               /* ③ 중재 + 비트 1개                               */
    if (e->cycle < TRACE_MAX) e->trace[e->cycle] = g < 0 ? '.' : (char)('0' + g);
    e->cycle++;
}
static void dma_reset(dma_t *e) {
    memset(e, 0, sizeof *e);
    e->gch = -1;
    for (int l = 0; l < 4; l++) e->rr_last[l] = NCH - 1;
    for (int i = 0; i < NCH; i++) { e->ch[i].burst = 1; e->ch[i].first_beat = UINT32_MAX; }
}
static void chan_cfg(dma_t *e, int ch, uint8_t prio, uint8_t burst, uint16_t max_desc) {
    e->ch[ch].prio = prio; e->ch[ch].burst = burst ? burst : 1; e->ch[ch].max_desc = max_desc;
}
static int dma_start(dma_t *e, int ch, uint32_t desc) {
    chan_t *c = &e->ch[ch];
    if (c->st == CH_BUSY) return -1;
    c->st = CH_BUSY; c->cur = desc; c->loaded = false; c->ndesc = 0; c->total = 0;
    c->err = E_NONE; c->err_desc = c->err_addr = c->err_done = 0; c->first_beat = UINT32_MAX;
    return 0;
}
static bool any_busy(const dma_t *e) {
    for (int i = 0; i < NCH; i++) if (e->ch[i].st == CH_BUSY) return true;
    return false;
}
static uint32_t dma_run(dma_t *e, uint32_t max_cycles) {
    while (any_busy(e) && e->cycle < max_cycles) engine_step(e);
    return e->cycle;
}
static const char *irq_log(const dma_t *e) {          /* "@15 ch0 TC d=20000100 n=64; @20 ch1 ERR BUS ..." */
    static char buf[4096];
    size_t o = 0;
    buf[0] = 0;
    for (uint32_t i = 0; i < e->nirq && o < sizeof buf - 96; i++) {
        const irq_t *q = &e->irq[i];
        if (q->err)
            o += (size_t)snprintf(buf + o, sizeof buf - o, "%s@%u ch%d ERR %s d=%08x a=%08x done=%u", i ? "; " : "",
                                  (unsigned)q->cycle, q->ch, err_name[q->code], (unsigned)q->desc, (unsigned)q->addr,
                                  (unsigned)q->n);
        else
            o += (size_t)snprintf(buf + o, sizeof buf - o, "%s@%u ch%d TC d=%08x n=%u", i ? "; " : "",
                                  (unsigned)q->cycle, q->ch, (unsigned)q->desc, (unsigned)q->n);
    }
    return buf;
}

/* ======================= Part C — 체인 만들기 ======================= */
typedef struct { uint32_t dst, len; } seg_t;

/* (본문 TODO C2)  주변장치 DR → 버퍼 n 개로 나눠 받는 체인을 desc_base 부터 32 B 간격으로 쓴다 (32 B = 라인 하나에
 * 디스크립터 하나, ARCH-M12). 각 디스크립터: src = PER_DR (고정), dst = seg[i].dst (증가), len = seg[i].len,
 * F_DREQ_SRC, 원소 크기 코드 esz, 마지막은 next = 0. F_IRQ 는 irq_each 면 모두, 아니면 마지막에만.
 * 첫 디스크립터 주소를 돌려준다. */
static uint32_t build_rx_chain(uint32_t desc_base, const seg_t *seg, int n, uint32_t esz, bool irq_each) {
    for (int i = 0; i < n; i++) {
        uint32_t a = desc_base + 32u * (uint32_t)i;
        bool last = i == n - 1;
        uint32_t fl = F_DST_INC | F_DREQ_SRC | F_ESIZE(esz) | ((irq_each || last) ? F_IRQ : 0u);
        wdesc(a, PER_DR, seg[i].dst, seg[i].len, last ? 0u : a + 32u, fl);
    }
    return desc_base;
}

/* ======================= 연습 1 — 체인 사전 검사 (Floyd) ======================= */
static bool desc_addr_ok(uint32_t a) { return (a & 3u) == 0 && sram_ok(a, DESC_BYTES); }
/* 0 = 정상 (*count = 디스크립터 수), -1 = 순환, -2 = 나쁜 포인터 (정렬 / SRAM 밖) */
static int chain_check(uint32_t first, uint32_t *count) {
    *count = 0;
    if (!desc_addr_ok(first)) return -2;
    uint32_t slow = first, fast = first;
    bool ends = false;
    while (!ends) {
        for (int k = 0; k < 2 && !ends; k++) {             /* fast 가 두 칸 — 지나가는 노드를 전부 검사 */
            fast = rd32(fast + D_NEXT);
            if (fast == 0) ends = true;
            else if (!desc_addr_ok(fast)) return -2;
        }
        if (ends) break;
        slow = rd32(slow + D_NEXT);                         /* slow 는 fast 가 이미 검사한 노드만 밟는다 */
        if (slow == fast) return -1;
    }
    for (uint32_t a = first; a; a = rd32(a + D_NEXT)) (*count)++;
    return 0;
}

/* ======================= 연습 3 — FIFO 깊이 ======================= */
static uint32_t need_depth_hi(uint32_t n_burst, uint32_t period) { return (n_burst + period - 1) / period; }
static bool rr_bandwidth_ok(uint32_t n_burst, uint32_t period, uint32_t uart_burst) {
    return n_burst <= (period - 1) * uart_burst;           /* 한 바퀴 (Bu + N) 사이클 동안 도착 ≤ Bu */
}

/* ======================= 연습 4 — 페치 비용을 넣은 효율 ======================= */
/* 천분율: 데이터 비트 / (데이터 비트 + 디스크립터당 fetch_words 비트) */
static uint32_t sg_eff_permille(const uint32_t *lens, int n, uint32_t esize, uint32_t fetch_words) {
    uint64_t data = 0;
    for (int i = 0; i < n; i++) data += lens[i] / esize;
    return (uint32_t)(1000u * data / (data + (uint64_t)fetch_words * (uint64_t)n));
}
/* 조각 하나의 효율이 permille 이상이 되는 가장 작은 조각 바이트 수: b/(b+F) ≥ x  ⇔  b ≥ x·F/(1-x) */
static uint32_t min_frag_bytes(uint32_t esize, uint32_t fetch_words, uint32_t permille) {
    uint32_t beats = (permille * fetch_words + (1000u - permille) - 1u) / (1000u - permille);
    return beats * esize;
}

/* ======================= 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static dma_t E;
static uint8_t g_stream[4096];
static void setup(void) { memset(g_sram, 0, sizeof g_sram); memset(&g_per, 0, sizeof g_per); dma_reset(&E); }
static void uart(uint32_t period, uint32_t phase, uint32_t depth, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) g_stream[i] = (uint8_t)(0x41u + i * 13u);
    g_per.period = period; g_per.phase = phase; g_per.depth = depth; g_per.src = g_stream; g_per.n = n;
}
#define D0  0x20000100u
#define SRC 0x20001000u
#define DST 0x20002000u

static uint32_t worst_level(uint8_t n_burst, uint32_t period, uint8_t uart_prio, uint8_t uart_burst) {
    uint32_t worst = 0;
    for (uint32_t ph = 0; ph < period; ph++) {
        setup();
        uart(period, ph, 64, 200);
        wdesc(D0, SRC, 0x20008000u, 0x4000u, 0, F_SRC_INC | F_DST_INC | F_ESIZE(2));
        seg_t one[1] = { { DST, 200 } };
        build_rx_chain(D0 + 0x40, one, 1, 0, true);
        chan_cfg(&E, 0, 1, n_burst, 0);
        chan_cfg(&E, 1, uart_prio, uart_burst, 0);
        dma_start(&E, 0, D0); dma_start(&E, 1, D0 + 0x40);
        dma_run(&E, 100000);
        if (g_per.max_level > worst) worst = g_per.max_level;
    }
    return worst;
}

int main(void) {
    uint32_t cnt;
    /* 연습 1 */
    setup();
    for (uint32_t i = 0; i < 5; i++) wdesc(D0 + 32 * i, SRC, DST, 4, i < 4 ? D0 + 32 * (i + 1) : 0, F_SRC_INC | F_DST_INC);
    CHECK(chain_check(D0, &cnt) == 0 && cnt == 5);
    wr32(D0 + 4 * 32 + D_NEXT, D0 + 32);                    /* d4 → d1 : 꼬리가 몸통으로 */
    CHECK(chain_check(D0, &cnt) == -1);
    wr32(D0 + D_NEXT, D0);                                  /* 자기 자신 */
    CHECK(chain_check(D0, &cnt) == -1);
    wr32(D0 + D_NEXT, D0 + 32); wr32(D0 + 2 * 32 + D_NEXT, 0x20000F02u);   /* 정렬 안 된 next */
    CHECK(chain_check(D0, &cnt) == -2);
    wr32(D0 + 2 * 32 + D_NEXT, 0x50000000u);
    CHECK(chain_check(D0, &cnt) == -2 && chain_check(0x2000FFF0u, &cnt) == -2);
    wr32(D0 + 2 * 32 + D_NEXT, 0);
    CHECK(chain_check(D0, &cnt) == 0 && cnt == 3);
    /* 엔진의 하드웨어 가드와 같은 결론: 순환 체인은 가드 없으면 영원히, 가드 있으면 E_LOOP */
    wr32(D0 + 2 * 32 + D_NEXT, D0);
    chan_cfg(&E, 0, 1, 4, 0);
    dma_start(&E, 0, D0);
    dma_run(&E, 5000);
    CHECK(chain_check(D0, &cnt) == -1 && E.ch[0].st == CH_BUSY && E.cycle == 5000);

    /* 연습 2 — mem → TX: 32 B, TX FIFO 깊이 4, 선로 4 사이클/B */
    setup();
    for (uint32_t i = 0; i < 32; i++) P(SRC)[i] = (uint8_t)(0x30u + i);
    g_per.tx_period = 4; g_per.tx_phase = 0; g_per.tx_depth = 4;
    wdesc(D0, SRC, PER_DR, 32, 0, F_SRC_INC | F_DREQ_DST | F_IRQ);
    dma_start(&E, 0, D0);
    dma_run(&E, 10000);
    CHECK(strncmp(E.trace, "00000...0...0...0", 17) == 0 && E.beats == 32 && g_per.tx_overflow == 0);
    CHECK(strcmp(irq_log(&E), "@112 ch0 TC d=20000100 n=32") == 0 && g_per.ntx == 28 && g_per.txcount == 4);
    for (uint32_t t = E.cycle; t < E.cycle + 16; t++) periph_tick(&g_per, t);   /* 남은 4 B 가 선로로 */
    CHECK(g_per.ntx == 32 && memcmp(g_per.tx, P(SRC), 32) == 0);
    setup();                                                /* DREQ 없이 쓰면: FIFO 가 넘쳐 바이트를 잃는다 */
    for (uint32_t i = 0; i < 32; i++) P(SRC)[i] = (uint8_t)(0x30u + i);
    g_per.tx_period = 4; g_per.tx_depth = 4;
    wdesc(D0, SRC, PER_DR, 32, 0, F_SRC_INC | F_IRQ);
    dma_start(&E, 0, D0);
    dma_run(&E, 10000);
    CHECK(E.cycle == 32 && g_per.tx_overflow == 21);                  /* 받아들인 것 = 처음 4 + 선로가 뺀 7 */

    /* 연습 3 — FIFO 깊이: 우선순위 높음 = ceil(N/P) (최악 위상), 같은 레벨 RR = 대역폭 조건 */
    static const uint8_t Ns[] = { 1, 2, 4, 8, 16, 32 };
    static const uint32_t Ps[] = { 2, 3, 4, 8 };
    int hi_ok = 1, rr_ok = 1;
    for (unsigned i = 0; i < sizeof Ns; i++)
        for (unsigned j = 0; j < 4; j++) {
            hi_ok &= worst_level(Ns[i], Ps[j], 2, 8) == need_depth_hi(Ns[i], Ps[j]);
            for (uint8_t bu = 4; bu <= 8; bu += 4) {
                uint32_t w = worst_level(Ns[i], Ps[j], 1, bu), lim = need_depth_hi(Ns[i], Ps[j]) + 1;
                bool ok = rr_bandwidth_ok(Ns[i], Ps[j], bu);
                if (ok ? w > lim : w <= lim) { rr_ok = 0; printf("  RR N=%u P=%u Bu=%u: worst %u limit %u\n", Ns[i], Ps[j], bu, w, lim); }
            }
        }
    CHECK(hi_ok);
    CHECK(rr_ok);
    CHECK(need_depth_hi(16, 3) == 6 && rr_bandwidth_ok(16, 3, 8) && !rr_bandwidth_ok(16, 2, 8));

    /* 연습 4 — 페치 5 워드 / 디스크립터 */
    uint32_t a[1] = { 64 }, eth[1] = { 1500 }, tiny[1] = { 8 }, mix[3] = { 13, 7, 20 };
    CHECK(sg_eff_permille(a, 1, 4, 5) == 761 && sg_eff_permille(eth, 1, 4, 5) == 986 && sg_eff_permille(tiny, 1, 1, 5) == 615);
    CHECK(sg_eff_permille(mix, 3, 1, 5) == 727);            /* 본문 A2 gather: 40 비트 + 15 */
    CHECK(min_frag_bytes(4, 5, 800) == 80 && min_frag_bytes(4, 5, 900) == 180 && min_frag_bytes(1, 5, 500) == 5);
    int brute = 1;
    for (uint32_t pm = 100; pm <= 950; pm += 50) {
        uint32_t b = 1;
        while (1000u * b / (b + 5u) < pm) b++;
        uint32_t l[1] = { b * 4 }, l2[1] = { (b - 1) * 4 };
        brute &= min_frag_bytes(4, 5, pm) == b * 4 && sg_eff_permille(l, 1, 4, 5) >= pm
                 && (b == 1 || sg_eff_permille(l2, 1, 4, 5) < pm);
    }
    CHECK(brute);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
