/* ARCH-M11 DMA 컨트롤러 모델: 디스크립터 체인 — 체크 테스트
 *
 * 하드웨어 쪽 모델 (드라이버 API 는 BM-M08, 캐시 일관성은 ARCH-M12):
 *   SRAM 64 KB @ 0x2000_0000  +  모델 주변장치 @ 0x4000_1000 (SR = FIFO 레벨, DR = RX FIFO 팝 / TX 싱크)
 *   + 4 채널 DMA 엔진: 채널마다 우선순위(0..3, 클수록 높음)·버스트(중재 1회당 최대 비트 수)·루프 가드
 *   + 디스크립터 {src, dst, len, next, flags} 20 B 가 SRAM 안에 있고 next 로 이어진다 (scatter-gather)
 *   + 1 사이클 = 버스 비트(beat) 1개 = 원소 1개 (1/2/4 B) 를 읽어서 쓴다. 디스크립터 페치는 시간 0.
 *
 *   Part A  디스크립터 페치·검증, 비트(주소 증가/고정, 원소 크기), 체인 따라가기 + 완료 인터럽트
 *   Part B  중재: 고정 우선순위 + 같은 레벨은 라운드로빈, 버스트 단위(비선점) / 에러: CFG·LEN0·ALIGN·
 *           DESC·BUS(전송 도중)·LOOP — 채널은 멈추고 상태(어느 디스크립터, 몇 바이트)를 남긴다
 *   Part C  주변장치 → 메모리: DREQ(FIFO 에 원소가 있을 때만) + 생산 속도 + FIFO 깊이 + overrun,
 *           헤더/페이로드 분리 체인, 대량 mem→mem 복사와의 경쟁에서 overrun 이 나는 조건
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so      (-v : 트레이스·IRQ 로그 출력)
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
    uint8_t  tx[256]; uint32_t ntx;   /* DR 쓰기가 쌓이는 곳 (mem→periph 연습용)                */
} periph_t;
static periph_t g_per;

/* TODO C1: 이번 사이클이 생산 시점이면 다음 바이트를 FIFO 에 넣는다. FIFO 가 꽉 찼으면 그 새 바이트를 잃고
 * overrun++ (UART ORE 처럼 새 데이터가 버려진다). 넣은 뒤 max_level 갱신. 생산 시점 = cycle ≥ phase 이고
 * (cycle - phase) % period == 0, 그리고 아직 n 바이트를 다 만들지 않았을 때 (period 0 = 생산 안 함). */
static void periph_tick(periph_t *p, uint32_t cycle) {
    (void)p; (void)cycle;          /* TODO C1 */
}
/* TODO C1: FIFO 에서 n 바이트(1/2/4)를 꺼내 리틀 엔디언으로 *v 에 모은다. 모자라면 아무것도 꺼내지 않고
 * *v = 0, underrun++ (버스 에러는 아님 — DR 은 매핑돼 있다). */
static void periph_pop(periph_t *p, uint32_t n, uint32_t *v) {
    (void)p; (void)n;
    *v = 0;                        /* TODO C1 */
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
        for (uint32_t i = 0; i < n && g_per.ntx < sizeof g_per.tx; i++) g_per.tx[g_per.ntx++] = (uint8_t)(v >> (8 * i));
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
    return c->st == CH_BUSY && c->loaded && (!(c->flags & F_DREQ_SRC) || g_per.count >= c->esize);
}

/* ======================= Part A — 디스크립터 페치, 비트, 완료 ======================= */

/* TODO A1: c->cur 의 디스크립터를 읽어 채널 레지스터에 싣는다. 검사 순서 고정 — 첫 실패에서 chan_error 후 false:
 *   ① 루프 가드: max_desc != 0 이고 이미 max_desc 개를 가져왔으면 E_LOOP (addr = cur)
 *   ② 디스크립터 주소: 4 B 정렬이 아니거나 20 B 전체가 SRAM 안이 아니면 E_DESC (addr = cur)
 *   ── 여기서 5 워드를 읽고 ndesc++ ──
 *   ③ 원소 크기 코드 3 → E_CFG   ④ len == 0 → E_LEN0   ⑤ src/dst/len 이 esize 배수가 아니면 E_ALIGN
 *   (③~⑤ 의 addr = cur). 성공하면 src,dst,len,next,flags,esize 설정, left = len, loaded = true. */
static bool desc_fetch(dma_t *e, chan_t *c) {
    (void)e; (void)c; (void)chan_error;
    return false;                  /* TODO A1 */
}

/* TODO A2: 원소 하나를 옮긴다: bus_rd(src) → bus_wr(dst). 읽기나 쓰기가 버스 에러면 그 주소로
 * chan_error(E_BUS) 하고 끝 (그 원소는 끝난 것으로 치지 않는다). 성공하면 INC 플래그대로 주소 증가,
 * left -= esize, total += esize. */
static void do_beat(dma_t *e, chan_t *c) {
    (void)e; (void)c; (void)bus_rd; (void)bus_wr;   /* TODO A2 */
}

/* TODO A3: left == 0 이 된 직후 호출. F_IRQ 면 완료 인터럽트(TC: desc = cur, n = len). 그다음 next == 0 이면
 * 채널 DONE, 아니면 cur = next 로 옮기고 loaded = false (다음 사이클 페치). */
static void desc_complete(dma_t *e, chan_t *c) {
    (void)e; (void)c;              /* TODO A3 */
}

/* ======================= Part B — 중재와 버스트 ======================= */

/* TODO B1: 준비된(chan_ready) 채널 중 우선순위 레벨이 가장 높은 것. 같은 레벨 안에서는 라운드로빈:
 * rr_last[lvl] 다음 번호부터 차례로 보고 처음 준비된 채널. 승자를 rr_last 에 기록. 없으면 -1. */
static int arbitrate(dma_t *e) {
    (void)e; (void)chan_ready;
    return -1;                     /* TODO B1 */
}

/* TODO B2: 이번 사이클에 버스를 쓸 채널을 정하고 비트 하나를 실행, 그 채널 번호(없으면 -1)를 돌려준다.
 *   - 현재 그랜트(gch)가 있고 gleft > 0 이고 그 채널이 아직 준비돼 있으면 계속 (비선점 — 더 높은 우선순위가
 *     와도 버스트가 끝날 때까지 기다린다). 아니면 arbitrate() 로 새 그랜트, gleft = 그 채널의 burst.
 *   - 비트 실행 → first_beat 기록(처음이면), gleft--. 채널이 아직 BUSY 이고 left == 0 이면 desc_complete,
 *     그리고 그랜트 종료 (버스트는 디스크립터 경계를 넘지 않는다). 에러면 chan_error 가 그랜트를 끝낸다.
 *   - 빈 사이클이면 idle++, 비트면 beats++. */
static int grant_step(dma_t *e) {
    (void)e; (void)arbitrate; (void)do_beat; (void)desc_complete;
    return -1;                     /* TODO B2 */
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

/* TODO C2: 주변장치 DR → 버퍼 n 개로 나눠 받는 체인을 desc_base 부터 32 B 간격으로 쓴다 (32 B = 라인 하나에
 * 디스크립터 하나, ARCH-M12). 각 디스크립터: src = PER_DR (고정), dst = seg[i].dst (증가), len = seg[i].len,
 * F_DREQ_SRC, 원소 크기 코드 esz, 마지막은 next = 0. F_IRQ 는 irq_each 면 모두, 아니면 마지막에만.
 * 첫 디스크립터 주소를 돌려준다. */
static uint32_t build_rx_chain(uint32_t desc_base, const seg_t *seg, int n, uint32_t esz, bool irq_each) {
    (void)desc_base; (void)seg; (void)n; (void)esz; (void)irq_each;
    return 0;                      /* TODO C2 */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
static bool g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; if (g_verbose) printf("PASS %s\n", #expr); } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static dma_t E;
static uint8_t g_stream[4096];
static void setup(void) {
    memset(g_sram, 0, sizeof g_sram);
    memset(&g_per, 0, sizeof g_per);
    dma_reset(&E);
}
static void fill(uint32_t a, uint32_t n, uint8_t seed) { for (uint32_t i = 0; i < n; i++) P(a)[i] = (uint8_t)(seed + 7u * i); }
static void show(const char *name) {
    if (!g_verbose) return;
    printf("-- %s  (cycles=%u beats=%u idle=%u)\n   trace %.*s%s\n   irq   %s\n", name, (unsigned)E.cycle,
           (unsigned)E.beats, (unsigned)E.idle, 96, E.trace, strlen(E.trace) > 96 ? "…" : "", irq_log(&E));
}
static void uart(uint32_t period, uint32_t phase, uint32_t depth, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) g_stream[i] = (uint8_t)(0x41u + i * 13u);
    g_per.period = period; g_per.phase = phase; g_per.depth = depth; g_per.src = g_stream; g_per.n = n;
}

#define D0 0x20000100u                       /* 디스크립터 영역 */
#define SRC 0x20001000u
#define DST 0x20002000u

int main(int argc, char **argv) {
    g_verbose = argc > 1 && strcmp(argv[1], "-v") == 0;

    /* ---------------- Part A: 한 개, 체인(gather), 원소 크기, 고정 주소 ---------------- */
    setup();
    fill(SRC, 64, 1);
    wdesc(D0, SRC, DST, 64, 0, F_IRQ | F_SRC_INC | F_DST_INC | F_ESIZE(2));
    chan_cfg(&E, 0, 1, 4, 0);
    dma_start(&E, 0, D0);
    dma_run(&E, 10000);
    show("A1 mem->mem 64 B, esize 4");
    CHECK(memcmp(P(DST), P(SRC), 64) == 0 && E.ch[0].st == CH_DONE && E.ch[0].total == 64);
    CHECK(strcmp(irq_log(&E), "@15 ch0 TC d=20000100 n=64") == 0);
    CHECK(E.cycle == 16 && strcmp(E.trace, "0000000000000000") == 0);

    setup();                                              /* gather: 13 + 7 + 20 B → 연속 40 B */
    fill(SRC, 13, 10); fill(SRC + 0x100, 7, 50); fill(SRC + 0x200, 20, 90);
    wdesc(D0,        SRC,         DST,      13, D0 + 0x20, F_SRC_INC | F_DST_INC);
    wdesc(D0 + 0x20, SRC + 0x100, DST + 13,  7, D0 + 0x40, F_SRC_INC | F_DST_INC);
    wdesc(D0 + 0x40, SRC + 0x200, DST + 20, 20, 0,         F_SRC_INC | F_DST_INC | F_IRQ);
    dma_start(&E, 0, D0);
    dma_run(&E, 10000);
    show("A2 gather 13+7+20, IRQ on last");
    CHECK(memcmp(P(DST), P(SRC), 13) == 0 && memcmp(P(DST + 13), P(SRC + 0x100), 7) == 0
          && memcmp(P(DST + 20), P(SRC + 0x200), 20) == 0 && P(DST)[40] == 0);
    CHECK(strcmp(irq_log(&E), "@39 ch0 TC d=20000140 n=20") == 0 && E.ch[0].ndesc == 3 && E.ch[0].total == 40);

    for (int d = 0; d < 3; d++) wr32(D0 + 0x20u * (uint32_t)d + D_FLAGS, F_SRC_INC | F_DST_INC | F_IRQ);
    dma_reset(&E);                                        /* 같은 체인, 디스크립터마다 IRQ */
    dma_start(&E, 0, D0);
    dma_run(&E, 10000);
    CHECK(strcmp(irq_log(&E), "@12 ch0 TC d=20000100 n=13; @19 ch0 TC d=20000120 n=7; @39 ch0 TC d=20000140 n=20") == 0);

    setup();                                              /* 고정 소스 = 패턴 채우기 (esize 2) */
    wr32(SRC, 0x0000A55Au);
    wdesc(D0, SRC, DST, 64, 0, F_DST_INC | F_ESIZE(1));
    dma_start(&E, 0, D0);
    dma_run(&E, 10000);
    int okfill = 1;
    for (int i = 0; i < 64; i += 2) okfill &= P(DST)[i] == 0x5A && P(DST)[i + 1] == 0xA5;
    CHECK(okfill && E.cycle == 32 && P(DST)[64] == 0);
    setup();                                              /* 고정 목적지 = 마지막 원소만 남음 (FIFO 레지스터처럼) */
    fill(SRC, 16, 3);
    wdesc(D0, SRC, DST, 16, 0, F_SRC_INC | F_ESIZE(2));
    dma_start(&E, 0, D0);
    dma_run(&E, 10000);
    CHECK(rd32(DST) == rd32(SRC + 12) && rd32(DST + 4) == 0 && E.cycle == 4);
    setup();                                              /* 같은 64 B 를 1 B 원소로 → 64 비트 */
    fill(SRC, 64, 5);
    wdesc(D0, SRC, DST, 64, 0, F_SRC_INC | F_DST_INC | F_ESIZE(0));
    dma_start(&E, 0, D0);
    dma_run(&E, 10000);
    CHECK(memcmp(P(DST), P(SRC), 64) == 0 && E.cycle == 64 && E.beats == 64);

    /* ---------------- Part B: 중재 ---------------- */
    setup();                                              /* 같은 레벨 둘, 버스트 4 → 4 비트씩 번갈아 */
    fill(SRC, 16, 1); fill(SRC + 0x100, 16, 2);
    wdesc(D0,        SRC,         DST,         16, 0, F_SRC_INC | F_DST_INC);
    wdesc(D0 + 0x20, SRC + 0x100, DST + 0x100, 16, 0, F_SRC_INC | F_DST_INC);
    chan_cfg(&E, 0, 1, 4, 0); chan_cfg(&E, 1, 1, 4, 0);
    dma_start(&E, 0, D0); dma_start(&E, 1, D0 + 0x20);
    dma_run(&E, 10000);
    show("B1 RR, burst 4");
    CHECK(strcmp(E.trace, "00001111000011110000111100001111") == 0);
    CHECK(memcmp(P(DST), P(SRC), 16) == 0 && memcmp(P(DST + 0x100), P(SRC + 0x100), 16) == 0);

    setup();                                              /* 높은 레벨이 먼저, 같은 레벨끼리 RR */
    wdesc(D0,        SRC,         DST,         8, 0, F_SRC_INC | F_DST_INC);
    wdesc(D0 + 0x20, SRC + 0x100, DST + 0x100, 8, 0, F_SRC_INC | F_DST_INC);
    wdesc(D0 + 0x40, SRC + 0x200, DST + 0x200, 8, 0, F_SRC_INC | F_DST_INC);
    chan_cfg(&E, 0, 1, 4, 0); chan_cfg(&E, 1, 1, 4, 0); chan_cfg(&E, 2, 2, 4, 0);
    dma_start(&E, 0, D0); dma_start(&E, 1, D0 + 0x20); dma_start(&E, 2, D0 + 0x40);
    dma_run(&E, 10000);
    show("B2 prio 2 > prio 1 (RR)");
    CHECK(strcmp(E.trace, "222222220000111100001111") == 0);

    for (int burst = 8; burst >= 1; burst -= 7) {         /* 버스트 = 중재 단위: 높은 우선순위도 기다린다 */
        setup();
        wdesc(D0,        SRC,         DST,         16, 0, F_SRC_INC | F_DST_INC);
        wdesc(D0 + 0x20, SRC + 0x100, DST + 0x100,  4, 0, F_SRC_INC | F_DST_INC | F_IRQ);
        chan_cfg(&E, 0, 0, (uint8_t)burst, 0); chan_cfg(&E, 1, 3, 8, 0);
        dma_start(&E, 0, D0);
        dma_run(&E, 3);                                   /* 사이클 0..2 에 ch0 만 */
        dma_start(&E, 1, D0 + 0x20);                      /* 사이클 3 에 높은 우선순위 요청 */
        dma_run(&E, 10000);
        show(burst == 8 ? "B3 ch1 (prio 3) arrives at cycle 3, ch0 burst 8" : "B3 same, ch0 burst 1");
        if (burst == 8) CHECK(E.ch[1].first_beat == 8 && strcmp(E.trace, "00000000111100000000") == 0);
        else            CHECK(E.ch[1].first_beat == 3 && strcmp(E.trace, "00011110000000000000") == 0);
    }

    setup();                                              /* 고정 우선순위의 기아(starvation) */
    wdesc(D0,        SRC,         DST,         64, 0, F_SRC_INC | F_DST_INC);
    wdesc(D0 + 0x20, SRC + 0x100, DST + 0x100,  4, 0, F_SRC_INC | F_DST_INC);
    chan_cfg(&E, 3, 3, 4, 0); chan_cfg(&E, 0, 0, 4, 0);
    dma_start(&E, 3, D0); dma_start(&E, 0, D0 + 0x20);
    dma_run(&E, 10000);
    CHECK(E.ch[0].first_beat == 64 && E.ch[3].first_beat == 0);

    /* ---------------- Part B: 에러 ---------------- */
    setup();                                              /* len 0 */
    wdesc(D0, SRC, DST, 0, 0, F_SRC_INC | F_DST_INC | F_IRQ);
    dma_start(&E, 0, D0);
    dma_run(&E, 10000);
    CHECK(E.ch[0].st == CH_ERROR && E.ch[0].err == E_LEN0 && E.ch[0].err_done == 0 && E.beats == 0);
    CHECK(strcmp(irq_log(&E), "@0 ch0 ERR LEN0 d=20000100 a=20000100 done=0") == 0);

    setup();                                              /* 4 B 원소인데 src 가 2 B 정렬 / 예약 크기 */
    wdesc(D0, SRC + 2, DST, 16, 0, F_SRC_INC | F_DST_INC | F_ESIZE(2));
    wdesc(D0 + 0x20, SRC, DST, 16, 0, F_SRC_INC | F_DST_INC | F_ESIZE(3));
    wdesc(D0 + 0x40, SRC, DST, 6, 0, F_SRC_INC | F_DST_INC | F_ESIZE(2));
    dma_start(&E, 0, D0); dma_start(&E, 1, D0 + 0x20); dma_start(&E, 2, D0 + 0x40);
    dma_run(&E, 10000);
    CHECK(E.ch[0].err == E_ALIGN && E.ch[1].err == E_CFG && E.ch[2].err == E_ALIGN && E.beats == 0);

    setup();                                              /* 전송 도중 버스 에러: dst 가 SRAM 끝을 넘음 */
    fill(SRC, 64, 9);
    wdesc(D0, SRC, 0x2000FFF0u, 64, 0, F_SRC_INC | F_DST_INC | F_ESIZE(2) | F_IRQ);
    dma_start(&E, 0, D0);
    dma_run(&E, 10000);
    show("B4 bus error mid-transfer");
    CHECK(E.ch[0].st == CH_ERROR && E.ch[0].err == E_BUS && E.ch[0].err_addr == 0x20010000u
          && E.ch[0].err_done == 16 && E.ch[0].total == 16);
    CHECK(memcmp(P(0x2000FFF0u), P(SRC), 16) == 0
          && strcmp(irq_log(&E), "@4 ch0 ERR BUS d=20000100 a=20010000 done=16") == 0);

    setup();                                              /* 체인 두 번째가 나쁜 주소 / 정렬 안 된 디스크립터 */
    wdesc(D0, SRC, DST, 8, 0x30000000u, F_SRC_INC | F_DST_INC | F_IRQ);
    wdesc(D0 + 0x20, SRC, DST + 0x100, 8, D0 + 0x42, F_SRC_INC | F_DST_INC | F_IRQ);
    dma_start(&E, 0, D0); dma_start(&E, 1, D0 + 0x20);
    chan_cfg(&E, 0, 2, 8, 0); chan_cfg(&E, 1, 1, 8, 0);
    dma_run(&E, 10000);
    show("B5 bad next pointer (ch0) / misaligned next (ch1)");
    CHECK(strcmp(irq_log(&E), "@7 ch0 TC d=20000100 n=8; @8 ch0 ERR DESC d=30000000 a=30000000 done=0; "
                              "@15 ch1 TC d=20000120 n=8; @16 ch1 ERR DESC d=20000142 a=20000142 done=0") == 0);

    setup();                                              /* 루프: d0 → d1 → d0 …, 가드 8 */
    wdesc(D0,        SRC, DST, 4, D0 + 0x20, F_SRC_INC | F_DST_INC | F_ESIZE(2));
    wdesc(D0 + 0x20, SRC, DST, 4, D0,        F_SRC_INC | F_DST_INC | F_ESIZE(2));
    chan_cfg(&E, 0, 1, 4, 8);
    dma_start(&E, 0, D0);
    dma_run(&E, 10000);
    CHECK(E.ch[0].err == E_LOOP && E.ch[0].ndesc == 8 && E.ch[0].total == 32 && E.cycle == 9
          && strcmp(irq_log(&E), "@8 ch0 ERR LOOP d=20000100 a=20000100 done=0") == 0);

    setup();                                              /* 한 채널 에러가 다른 채널을 막지 않는다 */
    fill(SRC + 0x100, 32, 4);
    wdesc(D0,        PER_BASE + 8u, DST, 16, 0, F_DST_INC | F_ESIZE(2));          /* 미매핑 주변장치 레지스터 */
    wdesc(D0 + 0x20, SRC + 0x100, DST + 0x100, 32, 0, F_SRC_INC | F_DST_INC | F_ESIZE(2) | F_IRQ);
    chan_cfg(&E, 0, 1, 4, 0); chan_cfg(&E, 1, 1, 4, 0);
    dma_start(&E, 0, D0); dma_start(&E, 1, D0 + 0x20);
    dma_run(&E, 10000);
    show("B6 ch0 bus error, ch1 continues");
    CHECK(E.ch[0].err == E_BUS && E.ch[0].err_addr == PER_BASE + 8u && E.ch[1].st == CH_DONE
          && memcmp(P(DST + 0x100), P(SRC + 0x100), 32) == 0 && strcmp(E.trace, "011111111") == 0);
    CHECK(dma_start(&E, 0, D0 + 0x20) == 0 && dma_start(&E, 0, D0) == -1);   /* 에러 후 재시작 가능, BUSY 면 거부 */

    /* ---------------- Part C: 주변장치 모델 ---------------- */
    setup();
    uart(1, 0, 4, 6);
    for (uint32_t t = 0; t < 6; t++) periph_tick(&g_per, t);
    uint32_t v = 0;
    CHECK(g_per.count == 4 && g_per.overrun == 2 && g_per.max_level == 4);
    periph_pop(&g_per, 2, &v);
    CHECK(v == (uint32_t)(g_stream[0] | g_stream[1] << 8) && g_per.count == 2);
    periph_pop(&g_per, 4, &v);
    CHECK(v == 0 && g_per.underrun == 1 && g_per.count == 2);
    setup();
    uart(3, 2, 8, 100);
    for (uint32_t t = 0; t < 12; t++) periph_tick(&g_per, t);      /* 2, 5, 8, 11 */
    CHECK(g_per.produced == 4 && g_per.count == 4);

    /* ---------------- Part C: UART RX → 버퍼 3 개 (DREQ) ---------------- */
    setup();
    uart(4, 0, 4, 48);
    seg_t s3[3] = { { DST, 16 }, { DST + 0x100, 16 }, { DST + 0x200, 16 } };
    CHECK(build_rx_chain(D0, s3, 3, 0, true) == D0 && rd32(D0 + D_NEXT) == D0 + 32 && rd32(D0 + 64 + D_NEXT) == 0
          && rd32(D0 + D_SRC) == PER_DR && rd32(D0 + 32 + D_FLAGS) == (F_IRQ | F_DST_INC | F_DREQ_SRC));
    chan_cfg(&E, 0, 1, 4, 0);
    dma_start(&E, 0, D0);
    dma_run(&E, 10000);
    show("C1 UART RX 48 B, period 4, 3 buffers");
    CHECK(memcmp(P(DST), g_stream, 16) == 0 && memcmp(P(DST + 0x100), g_stream + 16, 16) == 0
          && memcmp(P(DST + 0x200), g_stream + 32, 16) == 0);
    CHECK(strcmp(irq_log(&E), "@60 ch0 TC d=20000100 n=16; @124 ch0 TC d=20000120 n=16; @188 ch0 TC d=20000140 n=16") == 0);
    CHECK(g_per.overrun == 0 && g_per.underrun == 0 && g_per.max_level == 1 && E.beats == 48 && E.idle == 141);

    setup();                                              /* 헤더 8 B / 페이로드 24 B 분리, 16 비트 원소 */
    uart(2, 1, 8, 32);
    seg_t hp[2] = { { DST, 8 }, { DST + 0x400, 24 } };
    build_rx_chain(D0, hp, 2, 1, false);
    dma_start(&E, 0, D0);
    dma_run(&E, 10000);
    show("C2 header/payload split, esize 2");
    CHECK(memcmp(P(DST), g_stream, 8) == 0 && memcmp(P(DST + 0x400), g_stream + 8, 24) == 0 && P(DST)[8] == 0);
    CHECK(strcmp(irq_log(&E), "@63 ch0 TC d=20000120 n=24") == 0 && E.beats == 16 && g_per.max_level == 2);

    /* ---------------- Part C: 대량 복사와 경쟁 → overrun ---------------- */
    struct { uint8_t bulk_prio, bulk_burst, uart_prio; uint32_t want_over; } cfg[] = {
        { 2, 16, 1, 44 },  /* 대량 복사가 더 높음 → UART 굶음 (대량이 끝날 때까지)             */
        { 1, 16, 2, 12 },  /* UART 가 더 높아도: 대량의 버스트 16 한 번을 기다리는 동안 넘침      */
        { 1, 16, 1, 12 },  /* 같은 레벨 RR — 기다리는 시간은 위와 같다                          */
        { 1,  4, 1,  0 },  /* 같은 레벨, 대량 버스트 4                                         */
        { 1,  4, 2,  0 },  /* UART 높음 + 대량 버스트 4                                        */
    };
    for (int k = 0; k < 5; k++) {
        setup();
        uart(3, 0, 4, 48);
        fill(SRC, 4096, 7);
        wdesc(D0, SRC, 0x20008000u, 4096, 0, F_SRC_INC | F_DST_INC | F_ESIZE(2) | F_IRQ);   /* 1024 비트 */
        seg_t one[1] = { { DST, 48 } };
        build_rx_chain(D0 + 0x40, one, 1, 0, true);
        chan_cfg(&E, 0, cfg[k].bulk_prio, cfg[k].bulk_burst, 0);
        chan_cfg(&E, 1, cfg[k].uart_prio, 8, 0);
        dma_start(&E, 0, D0); dma_start(&E, 1, D0 + 0x40);
        dma_run(&E, 20000);
        if (g_verbose)
            printf("-- C3 bulk prio %u burst %2u vs uart prio %u: overrun=%u max_level=%u uart_state=%d cycles=%u\n",
                   cfg[k].bulk_prio, cfg[k].bulk_burst, cfg[k].uart_prio, (unsigned)g_per.overrun,
                   (unsigned)g_per.max_level, (int)E.ch[1].st, (unsigned)E.cycle);
        CHECK(memcmp(P(0x20008000u), P(SRC), 4096) == 0);                   /* 대량 복사는 항상 정확 */
        CHECK(g_per.overrun == cfg[k].want_over);
        if (cfg[k].want_over) CHECK(E.ch[1].st == CH_BUSY);   /* 바이트를 잃어 len 48 을 못 채우고 영원히 기다림 */
        else CHECK(E.ch[1].st == CH_DONE && memcmp(P(DST), g_stream, 48) == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
