/* COMM-A08 제로카피 DMA 드라이버 설계: 디스크립터 링과 캐시 일관성
 *
 * 결정적 단일 스레드 모델. 이더넷 MAC DMA 를 본뜬 "일반" 모델이다 (Synopsys/STM32 ETH 와 구조가 같지만
 * 비트 위치·레지스터는 이 레슨의 정의 — 실제 칩은 RM 확인).
 *   RAM g_ram[]  +  write-back / write-allocate D-cache (32 B 라인, 16 세트 x 2 웨이 = 1 KB, LRU)
 *        (ARCH-M12 모델 축약판: 쓰기 버퍼는 빼고, 대신 "도어벨 앞에 DSB 가 있었나" 를 검사기로 센다)
 *   DMA 엔진: TX/RX 디스크립터 링을 RAM 에서 직접 읽고 쓴다 (캐시를 보지 않는다)
 *   하드웨어 훅: hw_evict (아무 때나 교체), hw_prefetch (추측성 채움),
 *               g_adv (CPU 가 디스크립터 워드를 store 한 "직후" 에 DMA 가 끼어드는 순간)
 *   드라이버: TX submit / reclaim, RX give / poll (제로카피: 채워진 버퍼를 그대로 위로, 빈 자리엔 새 버퍼),
 *            버퍼 풀 (malloc 없음, LIFO)
 *
 *   Part A  OWN 핸드오프: TX/RX 경로, FS/LS 다중 디스크립터 프레임, 링 가득 참, TX 정지 → poll demand
 *   Part B  유지보수 한 줄씩 빼기 (a)~(e) + 버퍼 배치 규칙 + 오류 프레임 재활용
 *   Part C  제로카피 에코 soak (랜덤 교체·프리페치·끼어들기) + 소유권 감사 + 디스크립터 NC 영역 비교
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so      (-v : 시나리오 추적)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
static bool g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; if (g_verbose) printf("PASS %s\n", #expr); } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define MAYBE_UNUSED __attribute__((unused))   /* 시작 코드에서 아직 안 쓰는 도우미가 경고를 내지 않게 */
#define TRACE(...) do { if (g_verbose) { printf("    · "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ============================ 메모리 지도와 상수 (이 모델의 정의) ============================ */
#define LINE       32u
#define NSETS      16u
#define NWAYS      2u
#define RAM_SIZE   8192u
#define TXD_C      0x0000u          /* 캐시 가능 디스크립터: TX 8 개 x 32 B                          */
#define RXD_C      0x0100u          /*                      RX 8 개 x 32 B                          */
#define NC_BASE    0x0200u          /* [0x200, 0x400) = MPU non-cacheable 영역 (Part C 비교용)        */
#define NC_LIMIT   0x0400u
#define TXD_NC     0x0200u
#define RXD_NC     0x0300u
#define APP_BASE   0x0400u          /* 앱 변수 자리 (풀과 겹치면 안 됨)                               */
#define POOL_BASE  0x0800u
#define DESC_SZ    32u              /* 디스크립터 하나 = 캐시 라인 하나 (16 B 내용 + 16 B 패딩)        */
#define TX_RING    8u
#define RX_RING    8u
#define NBUF       40u
#define BUF_SZ     128u
#define MAX_SEG    3u
#define MAX_FRAME  (MAX_SEG * BUF_SZ)
#define RXQ_CAP    2u               /* MAC RX FIFO: 프레임 2 개까지 대기                              */

/* 디스크립터 word0 (오프셋 0). +4 = 버퍼 주소, +8 = 버퍼 길이 */
#define D_OWN      (1u << 31)       /* 1 = DMA 소유                                                  */
#define D_FS       (1u << 29)       /* 프레임의 첫 디스크립터                                        */
#define D_LS       (1u << 28)       /* 프레임의 마지막 디스크립터                                    */
#define D_ES       (1u << 15)       /* 오류 요약 (RX: CRC 오류 등, TX: 잘못된 디스크립터)             */
#define D_FL       0x3FFFu          /* RX 마지막 디스크립터: 프레임 전체 길이                         */

/* ============================ 하드웨어 모델 (주어진 코드) ============================ */
typedef struct { bool valid, dirty; uint32_t base, lru; uint8_t d[LINE]; } line_t;
typedef struct { unsigned fills, wbacks, cmo_desc, cmo_buf, nc_acc, dsb, evict_hook, prefetch_hook; } cst_t;
typedef struct { unsigned tx_susp, tx_underflow, tx_badseq, rx_rbu, rx_missed, barrier_viol, dma_err; } hwst_t;

static uint8_t  g_ram[RAM_SIZE];
static line_t   g_c[NSETS][NWAYS];
static uint32_t g_lru;
static cst_t    g_cs;
static hwst_t   g_hw;
static uint32_t g_txd = TXD_C, g_rxd = RXD_C;     /* 디스크립터 링 위치 (Part C 에서 NC 로 바꿔 봄) */
static unsigned g_since_dsb;                       /* 마지막 DSB 이후 디스크립터 store + 유지보수 수  */
static void (*g_adv)(uint32_t addr);               /* 디스크립터 store 직후 불리는 "끼어들기" 훅      */
static bool     g_in_adv;

static uint32_t lbase(uint32_t a) { return a & ~(LINE - 1u); }
static bool is_nc(uint32_t a) { return a >= NC_BASE && a < NC_LIMIT; }
static bool is_desc(uint32_t a) {
    return (a >= g_txd && a < g_txd + TX_RING * DESC_SZ) || (a >= g_rxd && a < g_rxd + RX_RING * DESC_SZ);
}
static uint32_t txd(uint32_t i) { return g_txd + (i % TX_RING) * DESC_SZ; }
static uint32_t rxd(uint32_t i) { return g_rxd + (i % RX_RING) * DESC_SZ; }

static line_t *find(uint32_t a) {
    uint32_t b = lbase(a), s = (b / LINE) % NSETS;
    for (unsigned w = 0; w < NWAYS; w++)
        if (g_c[s][w].valid && g_c[s][w].base == b) return &g_c[s][w];
    return NULL;
}
static void writeback(line_t *l) { memcpy(&g_ram[l->base], l->d, LINE); l->dirty = false; g_cs.wbacks++; }
static line_t *touch(uint32_t a) {                 /* 적중이면 그 라인, 미스면 LRU 교체 후 채움        */
    line_t *l = find(a);
    if (!l) {
        uint32_t b = lbase(a), s = (b / LINE) % NSETS;
        l = &g_c[s][0];
        for (unsigned w = 0; w < NWAYS; w++) {
            if (!g_c[s][w].valid) { l = &g_c[s][w]; break; }
            if (g_c[s][w].lru < l->lru) l = &g_c[s][w];
        }
        if (l->valid && l->dirty) writeback(l);    /* 용량 교체 = write-back (아무 때나 일어남)        */
        memcpy(l->d, &g_ram[b], LINE); l->base = b; l->valid = true; l->dirty = false; g_cs.fills++;
    }
    l->lru = ++g_lru;
    return l;
}
/* ---- CPU 접근 (= load/store 명령; volatile 도 똑같이 캐시를 지난다) ---- */
static uint8_t cpu_rd8(uint32_t a) {
    if (is_nc(a)) { g_cs.nc_acc++; return g_ram[a]; }
    return touch(a)->d[a % LINE];
}
static void cpu_wr8(uint32_t a, uint8_t v) {
    if (is_nc(a)) { g_cs.nc_acc++; g_ram[a] = v; return; }
    line_t *l = touch(a); l->d[a % LINE] = v; l->dirty = true;
}
static uint32_t ld32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void st32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t cpu_rd32(uint32_t a) {
    if (is_nc(a)) { g_cs.nc_acc++; return ld32(&g_ram[a]); }
    return ld32(&touch(a)->d[a % LINE]);
}
static void cpu_wr32(uint32_t a, uint32_t v) {
    if (is_nc(a)) { g_cs.nc_acc++; st32(&g_ram[a], v); }
    else { line_t *l = touch(a); st32(&l->d[a % LINE], v); l->dirty = true; }
    if (is_desc(a)) {
        g_since_dsb++;
        if (g_adv && !g_in_adv) { g_in_adv = true; g_adv(a); g_in_adv = false; }   /* DMA 가 지금 끼어듦 */
    }
}
static uint32_t ram_rd32(uint32_t a) { return ld32(&g_ram[a]); }        /* DMA 쪽 접근 (RAM 직접) */
static void ram_wr32(uint32_t a, uint32_t v) { st32(&g_ram[a], v); }

/* ---- 하드웨어가 "아무 때나" 하는 일 — 테스트가 순간을 고른다 ---- */
static void hw_evict(uint32_t a) {
    line_t *l = find(a);
    if (!l) return;
    g_cs.evict_hook++;
    if (l->dirty) { TRACE("교체(훅): 더티 라인 0x%04x → RAM write-back", (unsigned)l->base); writeback(l); }
    l->valid = false;
}
static void hw_prefetch(uint32_t a) {
    if (is_nc(a) || find(a)) return;
    g_cs.prefetch_hook++;
    TRACE("프리페치(훅): 라인 0x%04x 를 지금의 RAM 값으로 채움", (unsigned)lbase(a));
    touch(a);
}
/* ---- 캐시 유지보수 (CMSIS SCB_*DCache_by_Addr 모양: 걸친 라인 전부, 부분 라인 포함 — ARCH-M12) ---- */
static void cmo_count(uint32_t a) { if (is_desc(a)) g_cs.cmo_desc++; else g_cs.cmo_buf++; g_since_dsb++; }
static void cache_clean(uint32_t addr, uint32_t len) {
    for (uint32_t a = lbase(addr); len && a < addr + len; a += LINE) {
        cmo_count(a);
        line_t *l = find(a);
        if (l && l->dirty) writeback(l);
    }
}
static void cache_inval(uint32_t addr, uint32_t len) {
    for (uint32_t a = lbase(addr); len && a < addr + len; a += LINE) {
        cmo_count(a);
        line_t *l = find(a);
        if (l) { if (l->dirty) TRACE("invalidate 0x%04x — 더티 데이터 버려짐!", (unsigned)l->base); l->valid = false; }
    }
}
static void dsb(void) { g_cs.dsb++; g_since_dsb = 0; }

/* ---- DMA 엔진 ---- */
typedef struct { bool run, in_frame; uint32_t cur, flen; uint8_t fr[MAX_FRAME + BUF_SZ]; } dtx_t;
typedef struct { bool run; uint32_t cur; } drx_t;
typedef struct { uint16_t len, off; bool bad; uint8_t d[MAX_FRAME]; } rxf_t;
static dtx_t g_dtx;
static drx_t g_drx;
static rxf_t g_rxq[RXQ_CAP];
static unsigned g_rxq_head, g_rxq_n;
/* 선로로 나간 TX 프레임: 마지막 8 개 보관 + 콜백 (soak 검사용) */
static uint8_t  g_wire[8][MAX_FRAME + BUF_SZ];
static uint32_t g_wire_len[8];
static unsigned g_wire_n;
static void (*g_wire_cb)(const uint8_t *p, uint32_t len);

static void wire_emit(const uint8_t *p, uint32_t len) {
    unsigned k = g_wire_n % 8u;
    memcpy(g_wire[k], p, len); g_wire_len[k] = len; g_wire_n++;
    TRACE("선로 TX: 프레임 %u 바이트 [%02x %02x %02x %02x %02x ..]", (unsigned)len, p[0], p[1], p[2], p[3], p[4]);
    if (g_wire_cb) g_wire_cb(p, len);
}
static bool dma_tx_step(void) {
    if (!g_dtx.run) return false;
    uint32_t d = txd(g_dtx.cur), w0 = ram_rd32(d);
    if (!(w0 & D_OWN)) {                                   /* CPU 소유 → 정지, poll demand 까지 대기 */
        g_dtx.run = false; g_hw.tx_susp++;
        if (g_dtx.in_frame) { g_hw.tx_underflow++; g_dtx.in_frame = false;
                              TRACE("DMA TX: 프레임 중간 디스크립터 %u 가 OWN=0 → underflow, 프레임 버림", (unsigned)g_dtx.cur); }
        else TRACE("DMA TX: 디스크립터 %u OWN=0 → 정지(suspended)", (unsigned)g_dtx.cur);
        return false;
    }
    uint32_t buf = ram_rd32(d + 4), len = ram_rd32(d + 8);
    bool ok = len > 0 && buf < RAM_SIZE && len <= RAM_SIZE - buf;
    if (w0 & D_FS) { if (g_dtx.in_frame) g_hw.tx_badseq++; g_dtx.in_frame = true; g_dtx.flen = 0; }
    else if (!g_dtx.in_frame) { g_hw.tx_badseq++; ok = false; }
    if (ok && g_dtx.flen + len > sizeof g_dtx.fr) ok = false;
    TRACE("DMA TX: 디스크립터 %u 읽음 (RAM) OWN=1%s%s buf=0x%04x len=%u", (unsigned)g_dtx.cur,
          (w0 & D_FS) ? " FS" : "", (w0 & D_LS) ? " LS" : "", (unsigned)buf, (unsigned)len);
    if (ok) { memcpy(g_dtx.fr + g_dtx.flen, &g_ram[buf], len); g_dtx.flen += len; }
    else { g_hw.dma_err++; g_dtx.in_frame = false; }
    if ((w0 & D_LS) && g_dtx.in_frame) { wire_emit(g_dtx.fr, g_dtx.flen); g_dtx.in_frame = false; }
    ram_wr32(d, (w0 & ~D_OWN) | (ok ? 0u : D_ES));        /* 상태 write-back: OWN 을 내려 CPU 에 돌려줌 */
    g_dtx.cur = (g_dtx.cur + 1u) % TX_RING;
    return true;
}
static bool dma_rx_step(void) {
    if (!g_drx.run || g_rxq_n == 0) return false;
    rxf_t *f = &g_rxq[g_rxq_head];
    uint32_t d = rxd(g_drx.cur), w0 = ram_rd32(d);
    if (!(w0 & D_OWN)) {                                   /* 버퍼 없음 (RBU) → 정지, 프레임은 FIFO 에 남음 */
        g_drx.run = false; g_hw.rx_rbu++;
        TRACE("DMA RX: 디스크립터 %u OWN=0 → 버퍼 없음(RBU), 정지", (unsigned)g_drx.cur);
        return false;
    }
    uint32_t buf = ram_rd32(d + 4), blen = ram_rd32(d + 8);
    bool ok = blen > 0 && buf < RAM_SIZE && blen <= RAM_SIZE - buf;
    uint32_t n = (uint32_t)(f->len - f->off);
    if (ok && n > blen) n = blen;
    if (ok) memcpy(&g_ram[buf], f->d + f->off, n); else g_hw.dma_err++;
    TRACE("DMA RX: 디스크립터 %u → RAM 0x%04x..+%u 씀", (unsigned)g_drx.cur, (unsigned)buf, (unsigned)n);
    uint32_t nw = f->off == 0 ? D_FS : 0u;
    f->off = (uint16_t)(f->off + n);
    if (f->off == f->len) {
        nw |= D_LS | f->len | ((f->bad || !ok) ? D_ES : 0u);
        g_rxq_head = (g_rxq_head + 1u) % RXQ_CAP; g_rxq_n--;
    }
    ram_wr32(d, nw);                                       /* OWN=0 + FS/LS/길이 */
    g_drx.cur = (g_drx.cur + 1u) % RX_RING;
    return true;
}
/* 선로에서 프레임이 도착. FIFO 가 차 있으면 놓침(missed). 정지 상태면 디스크립터를 다시 읽는다 (모델 정의). */
static bool wire_rx(const uint8_t *p, uint32_t len, bool bad) {
    if (len == 0 || len > MAX_FRAME || g_rxq_n == RXQ_CAP) { g_hw.rx_missed++; return false; }
    rxf_t *f = &g_rxq[(g_rxq_head + g_rxq_n) % RXQ_CAP];
    memcpy(f->d, p, len); f->len = (uint16_t)len; f->off = 0; f->bad = bad; g_rxq_n++;
    g_drx.run = true;
    return true;
}
static void doorbell_check(void) { if (g_since_dsb) g_hw.barrier_viol++; }
static void dma_tx_poll_demand(void) { doorbell_check(); g_dtx.run = true; }   /* 도어벨 레지스터 쓰기 */
MAYBE_UNUSED static void dma_rx_poll_demand(void) { doorbell_check(); g_drx.run = true; }
static void dma_run(unsigned max) { for (unsigned i = 0; i < max; i++) if (!dma_tx_step() && !dma_rx_step()) break; }

/* ============================ 버퍼 풀 (주어진 코드, malloc 없음) ============================ */
static uint32_t g_pool_base = POOL_BASE, g_stride = BUF_SZ, g_bufsz = BUF_SZ;
static int16_t  g_free[NBUF];
static unsigned g_nfree, g_pool_err;
static uint32_t buf_addr(int b) { return g_pool_base + (uint32_t)b * g_stride; }
static int  pool_get(void) { return g_nfree ? g_free[--g_nfree] : -1; }
static void pool_put(int b) {
    if (b < 0 || b >= (int)NBUF || g_nfree == NBUF) { g_pool_err++; return; }
    g_free[g_nfree++] = (int16_t)b;
}

/* ============================ 드라이버 ============================ */
/* 결함 주입 스위치 (실제 제품 코드라면 #ifdef FAULT_INJECT). 각 유지보수 지점에 이름이 있다. */
#define M_TX_BUF_CLEAN  (1u << 0)   /* (a) TX 버퍼 clean                         */
#define M_TX_DESC_CLEAN (1u << 1)   /* (b) TX 디스크립터 clean (OWN=1 을 RAM 으로) */
#define M_TX_DESC_INVAL (1u << 2)   /* (b') reclaim 전 TX 디스크립터 invalidate   */
#define M_RX_BUF_PREP   (1u << 3)   /* (c') RX 버퍼를 넘기기 전 invalidate        */
#define M_RX_BUF_INVAL  (1u << 4)   /* (c) RX 완료 후 읽기 전 invalidate          */
#define M_RX_DESC_CLEAN (1u << 5)   /* (b-RX) RX 디스크립터 clean                */
#define M_RX_DESC_INVAL (1u << 6)   /* (c'') poll 전 RX 디스크립터 invalidate     */
#define F_OWN_FIRST     (1u << 7)   /* (e1) 디스크립터 안에서 OWN 을 먼저 store   */
#define F_FWD_ORDER     (1u << 8)   /* (e2) 다중 디스크립터 프레임을 앞에서부터 넘김 */
#define F_NO_DSB        (1u << 9)   /* 도어벨 앞 배리어 생략                      */
static unsigned g_omit;
#define MAINT(id, stmt) do { if (!(g_omit & (id))) { stmt; } } while (0)
/* 디스크립터 유지보수: NC 영역이면 캐시에 없으므로 할 일이 없다 (순서·배리어만 남는다) */
MAYBE_UNUSED static void desc_clean(uint32_t d, unsigned id) { if (!is_nc(d)) MAINT(id, cache_clean(d, DESC_SZ)); }
MAYBE_UNUSED static void desc_inval(uint32_t d, unsigned id) { if (!is_nc(d)) MAINT(id, cache_inval(d, DESC_SZ)); }
MAYBE_UNUSED static void barrier(void) { if (!(g_omit & F_NO_DSB)) dsb(); }

#define DRV_OK      0
#define DRV_EINVAL  (-1)
#define DRV_EBUSY   (-2)
#define DRV_ENOBUF  (-3)
typedef struct { int16_t buf; uint16_t len; } seg_t;                 /* 풀 버퍼 번호 + 쓴 길이 */
typedef struct { unsigned nseg; seg_t seg[MAX_SEG]; uint32_t len; } pkt_t;   /* 위로 넘기는 RX 패킷 */
typedef struct {
    uint32_t tx_head, tx_tail;          /* 자유 증가 카운터: 사용 중 = head - tail (0..TX_RING)  */
    int16_t  tx_buf[TX_RING];           /* 슬롯이 끝나면 풀에 돌려줄 버퍼                        */
    uint32_t rx_next;                   /* 다음에 검사할 RX 디스크립터 (자유 증가)                */
    int16_t  rx_buf[RX_RING];           /* 슬롯에 걸린 버퍼                                      */
    unsigned tx_frames, tx_busy, tx_err, rx_frames, rx_err, rx_nobuf;
} drv_t;
static drv_t g_drv;

/* ---------------- Part A ---------------- */
/* A1: 디스크립터 3 워드를 쓴다. OWN 이 든 word0 은 반드시 마지막 store. (F_OWN_FIRST = 일부러 어기는 결함) */
static void desc_write(uint32_t d, uint32_t w0, uint32_t buf, uint32_t len) {
    if (g_omit & F_OWN_FIRST) { cpu_wr32(d, w0); cpu_wr32(d + 4, buf); cpu_wr32(d + 8, len); return; }
    /* TODO A1: 올바른 순서로 세 워드를 쓴다 (cpu_wr32). 엔진이 "OWN=1 인데 내용은 옛것" 을 볼 수 없게. */
    (void)d; (void)w0; (void)buf; (void)len;
}
/* A2: nseg 개 세그먼트 프레임에서 k 번째로 넘길 세그먼트 번호. (F_FWD_ORDER = 앞에서부터 넘기는 결함) */
static unsigned tx_give_index(unsigned k, unsigned nseg) {
    if (g_omit & F_FWD_ORDER) return k;
    /* TODO A2: 어느 세그먼트부터 넘겨야 엔진이 반쯤 준비된 프레임을 시작하지 않나? */
    (void)nseg;
    return k;
}
/* A3: 프레임 하나(세그먼트 1..MAX_SEG 개, 버퍼 소유권은 성공 시 드라이버로)를 TX 링에 넣고 도어벨.
 *     반환 DRV_OK / DRV_EINVAL / DRV_EBUSY (링 공간 부족 — 아무 상태도 바꾸지 않는다). */
static int drv_tx_submit(const seg_t *seg, unsigned nseg) {
    /* TODO A3: 인자 검사 → 링 공간 검사(부족하면 tx_busy++ 후 DRV_EBUSY, 상태 불변)
     *          → 버퍼 유지보수 MAINT(M_TX_BUF_CLEAN, ...) → tx_give_index 순서로 디스크립터 쓰기(desc_write)
     *          + desc_clean(d, M_TX_DESC_CLEAN), tx_buf[slot] 기록 → tx_head += nseg → barrier() → dma_tx_poll_demand()
     *          → tx_frames++. 유지보수 한 줄마다 "왜" 를 주석으로. */
    (void)seg; (void)nseg;
    return DRV_EBUSY;
}
/* A4: 끝난 TX 디스크립터를 순서대로 회수하고 버퍼를 풀에 돌려준다. 회수한 디스크립터 수 반환. */
static unsigned drv_tx_reclaim(void) {
    /* TODO A4: tail 부터 순서대로: desc_inval(d, M_TX_DESC_INVAL) → word0 읽기 → OWN 이면 멈춤,
     *          아니면 (ES 면 tx_err++) 버퍼를 풀에 돌려주고 tail++. TX 버퍼에 유지보수가 필요한가? */
    return 0;
}
/* A5: RX 슬롯에 버퍼를 건다 (DMA 에 소유권을 넘김). 배리어와 도어벨은 부르는 쪽이 한 번에. */
MAYBE_UNUSED static void rx_give(uint32_t slot, int b) {
    /* TODO A5: 버퍼 유지보수 MAINT(M_RX_BUF_PREP, ...) → rx_buf[slot] = b → desc_write(OWN, 버퍼, g_bufsz)
     *          → desc_clean(d, M_RX_DESC_CLEAN). */
    (void)slot; (void)b;
}
/* A6: 모든 RX 슬롯에 버퍼를 걸고 도어벨. 풀이 모자라면 DRV_ENOBUF. */
static int drv_rx_init(void) {
    /* TODO A6: 슬롯마다 pool_get + rx_give, rx_next = 0, barrier(), dma_rx_poll_demand(). */
    return DRV_ENOBUF;
}

/* ---------------- Part B ---------------- */
#define LAYOUT_OK      0
#define LAYOUT_EALIGN  (-1)   /* base 가 라인 정렬이 아님                                  */
#define LAYOUT_ESTRIDE (-2)   /* stride 가 라인 배수가 아님                                */
#define LAYOUT_ESIZE   (-3)   /* size == 0 또는 size > stride                              */
#define LAYOUT_ERANGE  (-4)   /* [base, base + NBUF*stride) 가 [APP_BASE + 0x400, RAM_SIZE) 밖 */
/* B1: 버퍼 풀 배치 검사. invalidate 가 옆 데이터를 지우지 않으려면 모든 버퍼가 자기 라인만 가져야 한다. */
static int pool_layout_check(uint32_t base, uint32_t stride, uint32_t size) {
    /* TODO B1: EALIGN → ESTRIDE → ESIZE → ERANGE 순서로 검사 */
    (void)base; (void)stride; (void)size;
    return LAYOUT_OK;
}
/* B2: rx_next 부터 n 개 디스크립터의 버퍼를 같은 슬롯에 다시 건다 (프레임 버림: 오류 프레임 / 풀 고갈).
 *     버퍼를 잃지도, 위로 넘기지도 않는다. 배리어 + 도어벨까지. */
MAYBE_UNUSED static void rx_recycle(unsigned n) {
    /* TODO B2: rx_next 부터 n 슬롯에 같은 버퍼를 다시 rx_give, rx_next += n, barrier(), 도어벨 */
    (void)n;
}
/* A7: 완성된 프레임 하나를 꺼낸다 (제로카피). 1 = out 채움, 0 = 완성된 프레임 없음.
 *     FS..LS 디스크립터가 모두 CPU 소유일 때만 소비. 오류 프레임(ES, 길이 불일치)과 풀 고갈은 rx_recycle 로 버리고 다음. */
static int drv_rx_poll(pkt_t *out) {
    /* TODO A7: (1) rx_next 부터 FS..LS 를 찾는다 — 디스크립터마다 desc_inval(d, M_RX_DESC_INVAL) 후 word0,
     *              하나라도 OWN 이면 아무것도 소비하지 않고 0.
     *          (2) 검사: 첫 디스크립터만 FS, LS 있음, n <= MAX_SEG, 길이가 n 개 버퍼에 맞음, ES 아님
     *              → 아니면 rx_err++, rx_recycle(n), 다음 프레임. 풀에 새 버퍼 n 개가 없으면 rx_nobuf++, rx_recycle(n).
     *          (3) 버퍼마다 MAINT(M_RX_BUF_INVAL, ...), out 에 넣고, 슬롯엔 rx_give(slot, pool_get()).
     *          (4) out->nseg/len, rx_next += n, rx_frames++, barrier(), 도어벨, 1 반환.
     *          오류 프레임이 연달아 있어도 끝나도록 바깥 루프는 최대 RX_RING 번. */
    (void)out;
    return 0;
}

/* ---------------- Part C ---------------- */
#define ECHO_MARK 0xECu
/* C1: 버퍼 소유권 감사. 모든 버퍼는 정확히 한 곳에 있어야 한다:
 *     풀 / TX 링(tail..head) / RX 링(모든 슬롯) / 앱(app[0..napp)). 위반(0 곳 = 누수, 2 곳 이상 = 이중 소유) 수 반환. */
static unsigned drv_audit(const int16_t *app, unsigned napp) {
    /* TODO C1: cnt[NBUF] 를 세고 (풀, TX tail..head, RX 슬롯(-1 은 빈 슬롯), app) — 범위 밖 번호도 위반,
     *          cnt != 1 인 버퍼마다 위반 하나. */
    (void)app; (void)napp;
    return 0;
}
/* C2: 제로카피 에코. 받은 패킷의 4 번째 바이트를 제자리에서 ECHO_MARK 로 고치고, 같은 버퍼들을 TX 세그먼트로 보낸다.
 *     링이 가득 차면 패킷을 버리고 버퍼를 풀에 돌려준다 (누수 없음). drv_tx_submit 의 반환값을 그대로 반환. */
static int app_echo(const pkt_t *p) {
    /* TODO C2: seg[0] 버퍼의 오프셋 4 를 ECHO_MARK 로 (cpu_wr8), drv_tx_submit(p->seg, p->nseg),
     *          실패하면 세그먼트 버퍼를 모두 pool_put. 결과 반환. */
    (void)p;
    return DRV_EBUSY;
}

/* ============================ 테스트 하네스 ============================ */
static void sys_reset(bool desc_nc) {
    memset(g_ram, 0, sizeof g_ram); memset(g_c, 0, sizeof g_c);
    for (uint32_t a = POOL_BASE; a < RAM_SIZE; a++) g_ram[a] = (uint8_t)(0xA0u ^ (a >> 5));   /* 버퍼 자리의 옛 내용 */
    g_lru = 0; memset(&g_cs, 0, sizeof g_cs); memset(&g_hw, 0, sizeof g_hw);
    memset(&g_dtx, 0, sizeof g_dtx); memset(&g_drx, 0, sizeof g_drx);
    g_rxq_head = g_rxq_n = 0; g_wire_n = 0; g_wire_cb = NULL; g_adv = NULL; g_in_adv = false;
    g_since_dsb = 0; g_omit = 0;
    g_txd = desc_nc ? TXD_NC : TXD_C; g_rxd = desc_nc ? RXD_NC : RXD_C;
    g_pool_base = POOL_BASE; g_stride = BUF_SZ; g_bufsz = BUF_SZ;
    g_nfree = 0; g_pool_err = 0;
    for (unsigned b = 0; b < NBUF; b++) g_free[g_nfree++] = (int16_t)b;
    memset(&g_drv, 0, sizeof g_drv);
    for (unsigned s = 0; s < TX_RING; s++) g_drv.tx_buf[s] = -1;
    for (unsigned s = 0; s < RX_RING; s++) g_drv.rx_buf[s] = -1;
}
/* 프레임 내용: [0..3] = seq (LE), [4] = 0 (에코 표시 자리), 나머지 = seq 로 정해지는 의사난수 */
static void gen_frame(uint32_t seq, uint32_t len, uint8_t *out) {
    uint32_t s = seq * 2654435761u + 12345u;
    for (uint32_t i = 0; i < len; i++) out[i] = (uint8_t)xs32(&s);
    if (len >= 4) st32(out, seq);
    if (len >= 5) out[4] = 0;
}
static void app_write(int b, const uint8_t *p, uint32_t len) { for (uint32_t i = 0; i < len; i++) cpu_wr8(buf_addr(b) + i, p[i]); }
static uint32_t pkt_read(const pkt_t *p, uint8_t *out) {
    uint32_t k = 0;
    for (unsigned i = 0; i < p->nseg; i++)
        for (uint32_t j = 0; j < p->seg[i].len; j++) out[k++] = cpu_rd8(buf_addr(p->seg[i].buf) + j);
    return k;
}
static const uint8_t *wire_last(void) { return g_wire[(g_wire_n + 7u) % 8u]; }
static uint32_t wire_last_len(void) { return g_wire_len[(g_wire_n + 7u) % 8u]; }
static unsigned ram_own_count(uint32_t base, unsigned n) {   /* 엔진이 보는(RAM) OWN=1 디스크립터 수 */
    unsigned c = 0;
    for (unsigned i = 0; i < n; i++) c += (ram_rd32(base + i * DESC_SZ) & D_OWN) != 0;
    return c;
}
static void hdr(const char *s) { if (g_verbose) printf("-- %s\n", s); }

/* 단일 세그먼트 TX 한 프레임: 앱이 버퍼에 쓰고 submit. 반환 = submit 결과 */
static int tx_one(uint32_t seq, uint32_t len, int *bout) {
    uint8_t f[MAX_FRAME];
    int b = pool_get();
    if (b < 0) return DRV_ENOBUF;
    gen_frame(seq, len, f); app_write(b, f, len);
    seg_t s = { (int16_t)b, (uint16_t)len };
    int r = drv_tx_submit(&s, 1);
    if (r != DRV_OK) pool_put(b);
    if (bout) *bout = b;
    return r;
}

/* ---------------- 끼어들기 훅들 ---------------- */
static void adv_tx(uint32_t a) {          /* word0 store 직후: 그 라인이 교체되고, 달리던 TX 엔진이 다음 디스크립터를 가져감 */
    if (a % DESC_SZ != 0) return;
    hw_evict(a);
    for (unsigned i = 0; i < 16 && dma_tx_step(); i++) {}
}
static uint32_t g_adv_seq;
static void adv_rx_arrive(uint32_t a) {   /* 디스크립터 store 직후: 그 라인이 교체되고, 프레임이 도착 */
    if (!g_adv_seq) return;
    hw_evict(a);
    uint8_t f[MAX_FRAME]; gen_frame(g_adv_seq, 60, f);
    TRACE("끼어들기: 프레임 seq %u 도착", (unsigned)g_adv_seq);
    wire_rx(f, 60, false); g_adv_seq = 0;
    for (unsigned i = 0; i < 16 && dma_rx_step(); i++) {}
}

static uint32_t g_snap[4][3];
static unsigned g_nsnap;
static void adv_snap(uint32_t a) {        /* store 직후 라인이 교체됐다면 엔진이 볼 디스크립터 */
    hw_evict(a);
    uint32_t d = a & ~(DESC_SZ - 1u);
    if (g_nsnap < 4u) { g_snap[g_nsnap][0] = ram_rd32(d); g_snap[g_nsnap][1] = ram_rd32(d + 4); g_snap[g_nsnap][2] = ram_rd32(d + 8); g_nsnap++; }
}

/* ============================ Part A 테스트 ============================ */
static void test_part_a(void) {
    uint8_t f[MAX_FRAME], got[MAX_FRAME];
    /* A1: 엔진 시점에서 "OWN=1 이 보이면 내용도 완성" — store 마다 라인을 RAM 으로 내보내 스냅샷 */
    hdr("A1: desc_write 의 store 하나하나 직후 라인 교체");
    sys_reset(false);
    {
        uint32_t d = txd(3);
        desc_write(d, D_OWN | D_FS | D_LS, 0x1234, 77);
        CHECK(cpu_rd32(d) == (D_OWN | D_FS | D_LS) && cpu_rd32(d + 4) == 0x1234u && cpu_rd32(d + 8) == 77u);
        sys_reset(false);
        g_nsnap = 0; g_adv = adv_snap;
        desc_write(d, D_OWN | D_FS | D_LS, 0x1234, 77);
        g_adv = NULL;
        unsigned torn = 0;
        for (unsigned i = 0; i < g_nsnap; i++)
            torn += (g_snap[i][0] & D_OWN) && (g_snap[i][1] != 0x1234u || g_snap[i][2] != 77u);
        CHECK(g_nsnap == 3u && torn == 0u && (g_snap[2][0] & D_OWN));
    }
    CHECK(tx_give_index(0, 1) == 0u && tx_give_index(0, 3) == 2u && tx_give_index(1, 3) == 1u && tx_give_index(2, 3) == 0u);
    CHECK(tx_give_index(0, 2) == 1u && tx_give_index(1, 2) == 0u);

    /* A-TX1: 한 프레임 100 B */
    hdr("A-TX1: 단일 디스크립터 TX 100 B");
    sys_reset(false);
    int b0 = -1;
    CHECK(tx_one(1, 100, &b0) == DRV_OK);
    CHECK(g_nfree == NBUF - 1u && ram_own_count(g_txd, TX_RING) == 1u);
    dma_run(64);
    gen_frame(1, 100, f);
    CHECK(g_wire_n == 1u && wire_last_len() == 100u && memcmp(wire_last(), f, 100) == 0);
    CHECK(drv_tx_reclaim() == 1u && g_nfree == NBUF && g_hw.barrier_viol == 0u);
    CHECK(drv_audit(NULL, 0) == 0u);

    /* A-TX2: 세그먼트 3 개 (헤더 14 B + 128 B + 50 B), FS/LS */
    hdr("A-TX2: 세그먼트 3 개 프레임 (FS / - / LS)");
    sys_reset(false);
    {
        seg_t s[3]; uint32_t lens[3] = { 14, 128, 50 }, off = 0;
        gen_frame(2, 192, f);
        for (unsigned i = 0; i < 3; i++) { s[i].buf = (int16_t)pool_get(); s[i].len = (uint16_t)lens[i]; app_write(s[i].buf, f + off, lens[i]); off += lens[i]; }
        CHECK(drv_tx_submit(s, 3) == DRV_OK);
        uint32_t w[3]; for (unsigned i = 0; i < 3; i++) w[i] = ram_rd32(txd(i));
        CHECK(w[0] == (D_OWN | D_FS) && w[1] == D_OWN && w[2] == (D_OWN | D_LS));
        dma_run(64);
        CHECK(g_wire_n == 1u && wire_last_len() == 192u && memcmp(wire_last(), f, 192) == 0);
        CHECK(drv_tx_reclaim() == 3u && g_nfree == NBUF && g_hw.tx_underflow == 0u);
    }
    /* A-TX3: 잘못된 인자 */
    {
        seg_t s0 = { 0, 0 }, s1 = { 0, (uint16_t)(BUF_SZ + 1) }, s2 = { (int16_t)NBUF, 10 }, s3[4] = { { 0, 1 }, { 1, 1 }, { 2, 1 }, { 3, 1 } };
        CHECK(drv_tx_submit(&s0, 1) == DRV_EINVAL && drv_tx_submit(&s1, 1) == DRV_EINVAL && drv_tx_submit(&s2, 1) == DRV_EINVAL);
        CHECK(drv_tx_submit(s3, 4) == DRV_EINVAL && drv_tx_submit(s3, 0) == DRV_EINVAL && g_drv.tx_head == g_drv.tx_tail);
    }
    /* A-TX4: 링 가득 참 — 엔진을 돌리지 않고 8 개, 9 번째는 BUSY, 상태 불변 */
    hdr("A-TX4: 링 가득 참");
    sys_reset(false);
    {
        int ok = 0;
        for (uint32_t i = 0; i < TX_RING; i++) ok += tx_one(10 + i, 60, NULL) == DRV_OK;
        uint32_t h = g_drv.tx_head; unsigned nf = g_nfree;
        CHECK(ok == (int)TX_RING && g_drv.tx_head - g_drv.tx_tail == TX_RING);
        CHECK(tx_one(99, 60, NULL) == DRV_EBUSY && g_drv.tx_head == h && g_nfree == nf && g_drv.tx_busy == 1u);
        dma_run(2);                                  /* 엔진이 2 개만 처리 → 사용 중 6 */
        CHECK(drv_tx_reclaim() == 2u);
        seg_t s[3]; for (unsigned i = 0; i < 3; i++) { s[i].buf = (int16_t)pool_get(); s[i].len = 20; }
        h = g_drv.tx_head; nf = g_nfree;
        CHECK(drv_tx_submit(s, 3) == DRV_EBUSY && g_drv.tx_head == h && g_nfree == nf);   /* 6 + 3 > 8: 일부만 넣지 않는다 */
        dma_run(1);
        CHECK(drv_tx_reclaim() == 1u && drv_tx_submit(s, 3) == DRV_OK);                     /* 5 + 3 = 8: 딱 맞음 */
        CHECK(g_drv.tx_head - g_drv.tx_tail == TX_RING);
        dma_run(256);
        drv_tx_reclaim();
        CHECK(g_drv.tx_head == g_drv.tx_tail && g_nfree == NBUF && g_wire_n == TX_RING + 1u);
        CHECK(ram_own_count(g_txd, TX_RING) == 0u && drv_audit(NULL, 0) == 0u);
    }
    /* A-TX5: 정지 → poll demand 로 재개, 링 한 바퀴 이상 */
    sys_reset(false);
    {
        unsigned sent = 0;
        for (uint32_t i = 0; i < 20; i++) {
            if (tx_one(100 + i, 30 + i, NULL) == DRV_OK) sent++;
            dma_run(16); drv_tx_reclaim();
        }
        CHECK(sent == 20u && g_wire_n == 20u && g_hw.tx_susp >= 20u && g_nfree == NBUF);
        gen_frame(119, 49, f);
        CHECK(wire_last_len() == 49u && memcmp(wire_last(), f, 49) == 0);
    }

    /* A-RX1: init */
    hdr("A-RX1: RX 링 초기화 + 60 B 프레임");
    sys_reset(false);
    CHECK(drv_rx_init() == DRV_OK);
    CHECK(ram_own_count(g_rxd, RX_RING) == RX_RING && g_nfree == NBUF - RX_RING && g_hw.barrier_viol == 0u);
    {
        pkt_t p; memset(&p, 0, sizeof p);
        CHECK(drv_rx_poll(&p) == 0);
        int16_t was = g_drv.rx_buf[0];
        gen_frame(7, 60, f); wire_rx(f, 60, false); dma_run(16);
        CHECK(drv_rx_poll(&p) == 1 && p.nseg == 1u && p.len == 60u && p.seg[0].buf == was);
        CHECK(pkt_read(&p, got) == 60u && memcmp(got, f, 60) == 0);
        CHECK(g_drv.rx_buf[0] != was && (ram_rd32(rxd(0)) & D_OWN) && g_nfree == NBUF - RX_RING - 1u);
        CHECK(drv_audit(&p.seg[0].buf, 1) == 0u);
        pool_put(p.seg[0].buf);
    }
    /* A-RX2: 300 B → 3 디스크립터 FS / - / LS; 중간 상태에서는 아무것도 안 먹음 */
    hdr("A-RX2: 300 B 프레임이 디스크립터 3 개에 걸침");
    sys_reset(false);
    drv_rx_init();
    {
        pkt_t p; memset(&p, 0, sizeof p);
        gen_frame(8, 300, f); wire_rx(f, 300, false);
        dma_run(2);                                  /* 3 개 중 2 개만 */
        CHECK(drv_rx_poll(&p) == 0 && g_drv.rx_next == 0u);
        dma_run(16);
        CHECK(drv_rx_poll(&p) == 1 && p.nseg == 3u && p.len == 300u && p.seg[2].len == 44u);
        CHECK(pkt_read(&p, got) == 300u && memcmp(got, f, 300) == 0 && g_drv.rx_next == 3u);
        for (unsigned i = 0; i < 3; i++) pool_put(p.seg[i].buf);
        CHECK(drv_audit(NULL, 0) == 0u);
    }
    /* A-RX3: 링을 여러 바퀴, 길이 1..384 */
    sys_reset(false);
    drv_rx_init();
    {
        unsigned okc = 0, n = 0; uint32_t rs = 77;
        for (uint32_t seq = 1; seq <= 300; seq++) {
            uint32_t len = 1 + xs32(&rs) % MAX_FRAME;
            gen_frame(seq, len, f); wire_rx(f, len, false); dma_run(16);
            pkt_t p; memset(&p, 0, sizeof p);
            if (drv_rx_poll(&p) == 1) {
                n++;
                okc += pkt_read(&p, got) == len && memcmp(got, f, len) == 0;
                for (unsigned i = 0; i < p.nseg; i++) pool_put(p.seg[i].buf);
            }
        }
        CHECK(n == 300u && okc == 300u && g_hw.rx_rbu == 0u && drv_audit(NULL, 0) == 0u);
    }
    /* A-RX4: RBU → 앱이 늦게 poll → 놓친 프레임만 빠지고 순서대로 회복 */
    hdr("A-RX4: 앱이 늦음 → RBU, FIFO 넘침, 회복");
    sys_reset(false);
    drv_rx_init();
    {
        for (uint32_t seq = 1; seq <= 12; seq++) { gen_frame(seq, 100, f); wire_rx(f, 100, false); dma_run(16); }
        /* 링 8 칸 + FIFO 2 = 10 개 보관, 2 개 놓침 */
        CHECK(g_hw.rx_rbu >= 1u && g_hw.rx_missed == 2u);
        unsigned n = 0, inorder = 1; uint32_t last = 0;
        for (unsigned k = 0; k < 40; k++) {
            pkt_t p; memset(&p, 0, sizeof p);
            if (drv_rx_poll(&p) == 1) {
                pkt_read(&p, got); uint32_t sq = ld32(got);
                inorder &= sq == last + 1u; last = sq; n++;
                for (unsigned i = 0; i < p.nseg; i++) pool_put(p.seg[i].buf);
            }
            dma_run(16);
        }
        CHECK(n == 10u && inorder == 1u && last == 10u && drv_audit(NULL, 0) == 0u);
    }
}

/* ============================ Part B 테스트 ============================ */
/* (a) TX 버퍼 clean 빠짐 */
static bool scen_tx_buf(unsigned omit) {
    uint8_t f[MAX_FRAME];
    sys_reset(false); g_omit = omit;
    CHECK(tx_one(5, 96, NULL) == DRV_OK);
    dma_run(16);
    gen_frame(5, 96, f);
    return g_wire_n == 1u && memcmp(wire_last(), f, 96) == 0;
}
/* (b) TX 디스크립터 clean 빠짐: 엔진은 OWN=0 → 아무것도 안 나감. 나중에 우연한 교체가 "살려" 준다 */
static unsigned scen_tx_desc_clean(unsigned omit, unsigned *late) {
    sys_reset(false); g_omit = omit;
    tx_one(6, 64, NULL);
    dma_run(16);
    unsigned now = g_wire_n;
    hw_evict(txd(0)); dma_tx_poll_demand(); dma_run(16);        /* 한참 뒤 교체 + 다음 도어벨 */
    *late = g_wire_n - now;
    return now;
}
/* (b') reclaim 전 TX 디스크립터 invalidate 빠짐 */
static unsigned scen_tx_desc_inval(unsigned omit) {
    sys_reset(false); g_omit = omit;
    tx_one(7, 64, NULL);
    dma_run(16);
    return drv_tx_reclaim();
}
/* (c) RX 완료 후 invalidate 빠짐 + DMA 도중 프리페치 */
static bool scen_rx_buf_inval(unsigned omit) {
    uint8_t f[MAX_FRAME], got[MAX_FRAME];
    sys_reset(false); g_omit = omit;
    drv_rx_init();
    uint32_t a = buf_addr(g_drv.rx_buf[0]);
    hw_prefetch(a); hw_prefetch(a + 32);         /* 넘긴 뒤, DMA 쓰기 전에 옛 RAM 값으로 채워짐 */
    gen_frame(9, 64, f); wire_rx(f, 64, false); dma_run(16);
    pkt_t p; memset(&p, 0, sizeof p);
    if (drv_rx_poll(&p) != 1) return false;
    pkt_read(&p, got);
    return memcmp(got, f, 64) == 0;
}
/* (c') RX 버퍼를 넘기기 전 invalidate 빠짐: 앱이 잠깐 쓰고 반납한 버퍼(더티)가 RX 링에 걸리고, DMA 뒤 교체 */
static bool scen_rx_buf_prep(unsigned omit) {
    uint8_t f[MAX_FRAME], got[MAX_FRAME];
    sys_reset(false); g_omit = omit;
    int s = pool_get();
    for (uint32_t i = 0; i < BUF_SZ; i++) cpu_wr8(buf_addr(s) + i, 0x5A);   /* 앱의 임시 사용 → 더티 4 줄 */
    pool_put(s);
    drv_rx_init();                               /* LIFO: s 가 슬롯 0 에 걸림 */
    gen_frame(10, 100, f); wire_rx(f, 100, false); dma_run(16);
    for (uint32_t k = 0; k < BUF_SZ; k += LINE) hw_evict(buf_addr(s) + k);   /* DMA 뒤 교체 = write-back */
    pkt_t p; memset(&p, 0, sizeof p);
    if (drv_rx_poll(&p) != 1 || p.seg[0].buf != s) return false;
    pkt_read(&p, got);
    return memcmp(got, f, 100) == 0;
}
/* (c'') poll 전 RX 디스크립터 invalidate 빠짐 */
static int scen_rx_desc_inval(unsigned omit) {
    uint8_t f[MAX_FRAME];
    sys_reset(false); g_omit = omit;
    drv_rx_init();
    gen_frame(11, 50, f); wire_rx(f, 50, false); dma_run(16);
    pkt_t p; memset(&p, 0, sizeof p);
    int r = drv_rx_poll(&p);
    if (r == 1) pool_put(p.seg[0].buf);
    return r;
}
/* (b-RX) RX 디스크립터 clean 빠짐 */
static int scen_rx_desc_clean(unsigned omit) {
    uint8_t f[MAX_FRAME];
    sys_reset(false); g_omit = omit;
    drv_rx_init();
    gen_frame(12, 50, f); wire_rx(f, 50, false); dma_run(16);
    pkt_t p; memset(&p, 0, sizeof p);
    int r = drv_rx_poll(&p);
    if (r == 1) pool_put(p.seg[0].buf);
    return r;
}
/* (e1-TX) OWN 먼저: 링이 한 바퀴 돈 뒤, 엔진이 달리는 중에 디스크립터 0 을 다시 채움 */
static void scen_own_first_tx(unsigned omit, unsigned *wire_ok, unsigned *wire_n) {
    uint8_t f[MAX_FRAME];
    sys_reset(false);
    bool v = g_verbose; g_verbose = false;        /* 준비 단계(링 한 바퀴)는 추적하지 않음 */
    for (uint32_t i = 0; i < TX_RING - 1u; i++) { tx_one(200 + i, 40 + i, NULL); dma_run(16); drv_tx_reclaim(); }
    g_verbose = v;
    g_omit = omit;
    tx_one(207, 47, NULL);                        /* 디스크립터 7, 도어벨만 — 엔진 run, 아직 안 돎 */
    g_adv = adv_tx;
    hdr("   (프레임 B 를 디스크립터 0 에 쓰는 중 끼어들기)");
    tx_one(208, 88, NULL);                        /* 디스크립터 0: 이전 바퀴의 buf/len 이 RAM 에 남아 있음 */
    g_adv = NULL;
    dma_run(32); drv_tx_reclaim();
    *wire_n = g_wire_n - (TX_RING - 1u);
    unsigned ok = 0;
    for (unsigned k = 0; k < *wire_n && k < 8u; k++) {
        const uint8_t *w = g_wire[(TX_RING - 1u + k) % 8u];
        uint32_t L = g_wire_len[(TX_RING - 1u + k) % 8u], sq = ld32(w);
        gen_frame(sq, L, f);
        ok += (sq == 207u + k) && L == 47u + 41u * k && memcmp(w, f, L) == 0;
    }
    *wire_ok = ok;
}
/* (e1-RX) OWN 먼저: 앱이 들고 있는 버퍼에 DMA 가 새 프레임을 씀 */
static bool scen_own_first_rx(unsigned omit, bool *held_intact) {
    uint8_t f[MAX_FRAME], got[MAX_FRAME];
    sys_reset(false); g_omit = omit;
    drv_rx_init();
    bool v = g_verbose; g_verbose = false;
    for (uint32_t seq = 1; seq <= RX_RING; seq++) { gen_frame(seq, 60, f); wire_rx(f, 60, false); dma_run(16); }
    g_verbose = v;
    pkt_t p; memset(&p, 0, sizeof p);
    g_adv = adv_rx_arrive; g_adv_seq = 99;
    int r = drv_rx_poll(&p);                      /* 슬롯 0 의 프레임 1 을 위로, 슬롯 0 에 새 버퍼 — 그 사이 도착 */
    g_adv = NULL;
    if (r != 1) return false;
    pkt_read(&p, got); gen_frame(1, 60, f);
    *held_intact = memcmp(got, f, 60) == 0;
    /* 나머지 프레임 2..8 과 99 를 받아 본다 */
    unsigned good = 0, n = 0;
    for (unsigned k = 0; k < 20; k++) {
        pkt_t q; memset(&q, 0, sizeof q);
        dma_run(16);
        if (drv_rx_poll(&q) == 1) {
            uint32_t L = pkt_read(&q, got), sq = ld32(got);
            gen_frame(sq, L, f); n++;
            good += (L == 60u) && ((sq >= 2u && sq <= RX_RING) || sq == 99u) && memcmp(got, f, L) == 0;
            for (unsigned i = 0; i < q.nseg; i++) pool_put(q.seg[i].buf);
        }
    }
    pool_put(p.seg[0].buf);
    return n == RX_RING && good == RX_RING;
}
/* (e2) 다중 디스크립터 프레임을 앞에서부터 넘김: 엔진이 FS 를 잡고 다음이 아직 CPU 소유 → underflow */
static bool scen_fwd_order(unsigned omit) {
    uint8_t f[MAX_FRAME];
    sys_reset(false); g_omit = omit;
    tx_one(300, 40, NULL);                        /* 엔진 run 상태로 만들기 */
    g_adv = adv_tx;
    seg_t s[2]; gen_frame(301, 200, f);
    s[0].buf = (int16_t)pool_get(); s[0].len = 100; app_write(s[0].buf, f, 100);
    s[1].buf = (int16_t)pool_get(); s[1].len = 100; app_write(s[1].buf, f + 100, 100);
    hdr("   (2 세그먼트 프레임을 넘기는 중 끼어들기)");
    drv_tx_submit(s, 2);
    g_adv = NULL;
    dma_run(32); drv_tx_reclaim();
    return g_wire_n == 2u && wire_last_len() == 200u && memcmp(wire_last(), f, 200) == 0
        && g_hw.tx_underflow == 0u && g_hw.tx_badseq == 0u;
}
/* (d) 정렬 안 된 풀: stride = size = 120 B → 버퍼 경계가 라인 안에 있다 */
static bool scen_layout(uint32_t stride) {
    uint8_t f[MAX_FRAME];
    sys_reset(false);
    g_stride = stride; g_bufsz = stride;
    drv_rx_init();                                /* 슬롯 0..7 = 버퍼 39..32 */
    int nb = pool_get();                          /* 31: 앱이 TX 프레임을 만든다 */
    gen_frame(400, 100, f); app_write(nb, f, 100);
    uint8_t g[MAX_FRAME]; gen_frame(401, 60, g); wire_rx(g, 60, false); dma_run(16);
    pkt_t p; memset(&p, 0, sizeof p);
    drv_rx_poll(&p);                              /* 슬롯 0 재장전 = 버퍼 30 → rx_give 가 invalidate */
    TRACE("버퍼 30 = 0x%04x..0x%04x, 버퍼 31 = 0x%04x..", (unsigned)buf_addr(30), (unsigned)(buf_addr(30) + g_bufsz - 1u), (unsigned)buf_addr(31));
    seg_t s = { (int16_t)nb, 100 };
    drv_tx_submit(&s, 1);
    dma_run(16);
    pool_put(p.seg[0].buf);
    return g_wire_n == 1u && memcmp(wire_last(), f, 100) == 0;
}
static void test_part_b(void) {
    /* B1 배치 검사 */
    CHECK(pool_layout_check(POOL_BASE, 128, 128) == LAYOUT_OK && pool_layout_check(POOL_BASE + 0x20, 128, 100) == LAYOUT_OK);
    CHECK(pool_layout_check(POOL_BASE + 8, 128, 128) == LAYOUT_EALIGN);
    CHECK(pool_layout_check(POOL_BASE, 120, 120) == LAYOUT_ESTRIDE && pool_layout_check(POOL_BASE, 136, 128) == LAYOUT_ESTRIDE);
    CHECK(pool_layout_check(POOL_BASE, 128, 0) == LAYOUT_ESIZE && pool_layout_check(POOL_BASE, 128, 129) == LAYOUT_ESIZE);
    CHECK(pool_layout_check(0x0400, 128, 128) == LAYOUT_ERANGE && pool_layout_check(POOL_BASE, 192, 128) == LAYOUT_ERANGE);

    hdr("B-a: TX 버퍼 clean 빠짐");
    CHECK(scen_tx_buf(M_TX_BUF_CLEAN) == false);
    CHECK(scen_tx_buf(0) == true);

    hdr("B-b: TX 디스크립터 clean 빠짐");
    unsigned late = 0;
    CHECK(scen_tx_desc_clean(M_TX_DESC_CLEAN, &late) == 0u && late == 1u && g_hw.tx_susp >= 1u);
    CHECK(scen_tx_desc_clean(0, &late) == 1u && late == 0u);

    hdr("B-b': reclaim 전 TX 디스크립터 invalidate 빠짐");
    CHECK(scen_tx_desc_inval(M_TX_DESC_INVAL) == 0u && ram_own_count(g_txd, 1) == 0u && g_nfree == NBUF - 1u);
    CHECK(scen_tx_desc_inval(0) == 1u && g_nfree == NBUF);

    hdr("B-c: RX 완료 후 invalidate 빠짐 (+ 프리페치)");
    CHECK(scen_rx_buf_inval(M_RX_BUF_INVAL) == false);
    CHECK(scen_rx_buf_inval(0) == true);

    hdr("B-c': RX 버퍼 넘기기 전 invalidate 빠짐 (+ 더티 버퍼 재사용, DMA 뒤 교체)");
    CHECK(scen_rx_buf_prep(M_RX_BUF_PREP) == false);
    CHECK(scen_rx_buf_prep(M_RX_BUF_INVAL) == true);        /* 프리페치가 없는 이 시나리오에선 (c) 가 빠져도 무사 — 다른 위험 */
    CHECK(scen_rx_buf_prep(0) == true);

    hdr("B-c'': poll 전 RX 디스크립터 invalidate 빠짐");
    CHECK(scen_rx_desc_inval(M_RX_DESC_INVAL) == 0 && (ram_rd32(rxd(0)) & (D_OWN | D_LS)) == D_LS);
    CHECK(scen_rx_desc_inval(0) == 1);

    hdr("B-b-RX: RX 디스크립터 clean 빠짐");
    CHECK(scen_rx_desc_clean(M_RX_DESC_CLEAN) == 0 && g_hw.rx_rbu >= 1u && ram_own_count(g_rxd, RX_RING) == 0u);
    CHECK(scen_rx_desc_clean(0) == 1 && g_hw.rx_rbu == 0u);

    hdr("B-e1: TX 디스크립터에서 OWN 을 먼저 store");
    unsigned ok = 0, n = 0;
    scen_own_first_tx(F_OWN_FIRST, &ok, &n);
    CHECK(n == 2u && ok == 1u);                   /* A 는 정상, 두 번째는 A 의 앞 40 B (낡은 buf/len), B 는 사라짐 */
    hdr("B-e1: (정답 순서, 같은 끼어들기)");
    scen_own_first_tx(0, &ok, &n);
    CHECK(n == 2u && ok == 2u && g_hw.dma_err == 0u);

    hdr("B-e1-RX: RX 재장전에서 OWN 을 먼저 store");
    bool held = true;
    scen_own_first_rx(F_OWN_FIRST, &held);
    CHECK(held == false);
    hdr("B-e1-RX: (정답 순서)");
    held = false;
    CHECK(scen_own_first_rx(0, &held) == true && held == true);

    hdr("B-e2: 2 세그먼트 프레임을 앞에서부터 넘김");
    CHECK(scen_fwd_order(F_FWD_ORDER) == false && g_hw.tx_underflow == 1u);
    hdr("B-e2: (정답: 뒤에서부터)");
    CHECK(scen_fwd_order(0) == true);

    /* 배리어 검사기 */
    CHECK(scen_tx_buf(F_NO_DSB) == true && g_hw.barrier_viol == 1u);
    CHECK(scen_tx_buf(0) == true && g_hw.barrier_viol == 0u);

    hdr("B-d: 120 B 버퍼를 빈틈없이 붙인 풀 (정렬 규칙 위반)");
    CHECK(scen_layout(120) == false);
    hdr("B-d: 128 B 간격 (규칙 준수)");
    CHECK(scen_layout(128) == true);

    /* B2: 오류 프레임(ES)과 풀 고갈 → 재활용, 버퍼 수 보존 */
    hdr("B2: 오류 프레임과 풀 고갈 → 같은 버퍼 재장전");
    sys_reset(false);
    drv_rx_init();
    {
        uint8_t f[MAX_FRAME], got[MAX_FRAME];
        gen_frame(500, 200, f); wire_rx(f, 200, true); dma_run(16);
        int16_t b0 = g_drv.rx_buf[0], b1 = g_drv.rx_buf[1];
        pkt_t p; memset(&p, 0, sizeof p);
        CHECK(drv_rx_poll(&p) == 0 && g_drv.rx_err == 1u && g_drv.rx_next == 2u);
        CHECK(g_drv.rx_buf[0] == b0 && g_drv.rx_buf[1] == b1 && ram_own_count(g_rxd, RX_RING) == RX_RING);
        int16_t hoard[NBUF]; unsigned nh = 0;
        int b; while ((b = pool_get()) >= 0) hoard[nh++] = (int16_t)b;
        gen_frame(501, 70, f); wire_rx(f, 70, false); dma_run(16);
        CHECK(drv_rx_poll(&p) == 0 && g_drv.rx_nobuf == 1u && drv_audit(hoard, nh) == 0u);
        pool_put(hoard[--nh]);
        gen_frame(502, 70, f); wire_rx(f, 70, false); dma_run(16);
        CHECK(drv_rx_poll(&p) == 1 && pkt_read(&p, got) == 70u && memcmp(got, f, 70) == 0);
        hoard[nh++] = p.seg[0].buf;
        CHECK(drv_audit(hoard, nh) == 0u && g_pool_err == 0u);
    }
}

/* ============================ Part C: soak ============================ */
typedef struct {
    unsigned injected, inj_bad, delivered, rx_corrupt, tx_corrupt, echoed, echo_drop, leaks, unaccounted,
             multi, ring_bad, scratch, hoard_max;
} soak_t;
static soak_t   g_sk;
static uint16_t g_len_of[8192];
static uint8_t  g_bad_of[8192];
static uint32_t g_last_echo;
static void soak_wire(const uint8_t *p, uint32_t len) {
    uint8_t f[MAX_FRAME + BUF_SZ];
    uint32_t sq = len >= 4 ? ld32(p) : 0;
    g_sk.echoed++;
    if (sq == 0 || sq > g_sk.injected || sq <= g_last_echo || len != g_len_of[sq] || len > MAX_FRAME) { g_sk.tx_corrupt++; return; }
    g_last_echo = sq;
    gen_frame(sq, len, f);
    if (len >= 5) f[4] = ECHO_MARK;
    if (memcmp(f, p, len) != 0) g_sk.tx_corrupt++;
}
static uint32_t g_adv_rng;
static void adv_random(uint32_t a) {      /* soak: store 의 1/4 에서 라인 교체 + 엔진이 몇 걸음 */
    if (xs32(&g_adv_rng) % 4u) return;
    hw_evict(a);
    unsigned k = xs32(&g_adv_rng) % 4u;
    for (unsigned i = 0; i < k; i++) { dma_tx_step(); dma_rx_step(); }
}
static void soak_take(const pkt_t *p, uint8_t *got) {
    uint32_t L = pkt_read(p, got), sq = L >= 4 ? ld32(got) : 0;
    g_sk.delivered++;
    if (p->nseg > 1u) g_sk.multi++;
    uint8_t f[MAX_FRAME];
    if (sq == 0 || sq > g_sk.injected || L != g_len_of[sq] || g_bad_of[sq]) { g_sk.rx_corrupt++; return; }
    gen_frame(sq, L, f);
    if (memcmp(f, got, L) != 0) g_sk.rx_corrupt++;
}
static soak_t soak(uint32_t seed, unsigned omit, bool desc_nc, unsigned ticks) {
    uint8_t f[MAX_FRAME], got[MAX_FRAME];
    uint32_t rs = seed;
    bool v = g_verbose; g_verbose = false;        /* soak 은 추적하지 않음 */
    sys_reset(desc_nc);
    memset(&g_sk, 0, sizeof g_sk); g_last_echo = 0;
    g_adv_rng = seed ^ 0x9E3779B9u;
    g_wire_cb = soak_wire; g_adv = adv_random;
    g_omit = omit;
    int16_t hoard[NBUF]; unsigned nh = 0, busy = 0;
    drv_rx_init();
    for (unsigned t = 0; t < ticks + 400u; t++) {
        bool live = t < ticks;
        if (live && xs32(&rs) % 100u < 30u && g_sk.injected < 8000u) {        /* 프레임 도착 */
            uint32_t sq = g_sk.injected + 1u, r = xs32(&rs) % 100u;
            uint32_t len = r < 50 ? 16 + xs32(&rs) % 112 : r < 80 ? 129 + xs32(&rs) % 128 : 257 + xs32(&rs) % 128;
            bool bad = xs32(&rs) % 100u < 3u;
            g_sk.injected = sq; g_len_of[sq] = (uint16_t)len; g_bad_of[sq] = bad;
            gen_frame(sq, len, f);
            wire_rx(f, len, bad);
        }
        if (live) {                                                         /* 하드웨어의 "아무 때나" */
            unsigned r = xs32(&rs) % 100u;
            if (r < 20u) hw_evict(xs32(&rs) % RAM_SIZE);
            else if (r < 40u) hw_prefetch(xs32(&rs) % RAM_SIZE);
        }
        unsigned steps = live ? xs32(&rs) % 4u : 8u;
        for (unsigned i = 0; i < steps; i++) { dma_tx_step(); dma_rx_step(); }
        if (live && busy == 0 && xs32(&rs) % 100u < 4u) busy = 1 + xs32(&rs) % 12u;   /* 앱이 바쁨 */
        if (busy) busy--;
        else {
            for (unsigned k = 0; k < 2; k++) {
                pkt_t p; memset(&p, 0, sizeof p);
                if (drv_rx_poll(&p) != 1) break;
                soak_take(&p, got);
                if (app_echo(&p) != DRV_OK) g_sk.echo_drop++;
            }
        }
        drv_tx_reclaim();
        if (live) {
            unsigned r = xs32(&rs) % 100u;
            if (r < 5u) {                                                   /* 앱이 풀 버퍼를 잠깐 씀 → 더티 */
                int b = pool_get();
                if (b >= 0) { for (uint32_t i = 0; i < BUF_SZ; i += 4) cpu_wr32(buf_addr(b) + i, xs32(&rs)); pool_put(b); g_sk.scratch++; }
            } else if (r < 7u && nh < 30u) { int b = pool_get(); if (b >= 0) hoard[nh++] = (int16_t)b; }
            else if (r < 12u && nh) pool_put(hoard[--nh]);
            if (nh > g_sk.hoard_max) g_sk.hoard_max = nh;
        } else while (nh) pool_put(hoard[--nh]);
    }
    g_sk.leaks = drv_audit(NULL, 0);
    unsigned acc = g_sk.delivered + g_drv.rx_err + g_drv.rx_nobuf + g_hw.rx_missed;
    g_sk.unaccounted = acc > g_sk.injected ? acc - g_sk.injected : g_sk.injected - acc;
    g_sk.ring_bad = (RX_RING - ram_own_count(g_rxd, RX_RING)) + ram_own_count(g_txd, TX_RING)
                  + (g_drv.tx_head != g_drv.tx_tail);
    g_wire_cb = NULL; g_adv = NULL; g_verbose = v;
    return g_sk;
}
static unsigned soak_symptoms(const soak_t *s) {
    return s->rx_corrupt + s->tx_corrupt + s->leaks + s->unaccounted + s->ring_bad
         + (s->echoed + s->echo_drop != s->delivered) + g_hw.barrier_viol + g_hw.tx_underflow
         + g_hw.tx_badseq + g_hw.dma_err + g_pool_err;
}

static void test_part_c(void) {
    /* C1 감사 */
    sys_reset(false);
    drv_rx_init();
    CHECK(drv_audit(NULL, 0) == 0u);
    { int16_t dup = g_drv.rx_buf[3]; CHECK(drv_audit(&dup, 1) == 1u); }
    { int b = pool_get(); CHECK(drv_audit(NULL, 0) == 1u); int16_t a1 = (int16_t)b; CHECK(drv_audit(&a1, 1) == 0u); pool_put(b); }
    { int16_t junk = -1; CHECK(drv_audit(&junk, 1) == 1u); }

    /* C2 에코 하나 */
    hdr("C2: 제로카피 에코 (RX 버퍼가 그대로 TX 버퍼)");
    {
        uint8_t f[MAX_FRAME];
        gen_frame(600, 150, f); wire_rx(f, 150, false); dma_run(16);
        pkt_t p; memset(&p, 0, sizeof p);
        CHECK(drv_rx_poll(&p) == 1 && p.nseg == 2u);
        int16_t b0 = p.seg[0].buf;
        CHECK(app_echo(&p) == DRV_OK && g_drv.tx_buf[0] == b0);
        dma_run(16);
        f[4] = ECHO_MARK;
        CHECK(g_wire_n == 1u && wire_last_len() == 150u && memcmp(wire_last(), f, 150) == 0);
        CHECK(drv_tx_reclaim() == 2u && drv_audit(NULL, 0) == 0u);
        /* 링이 가득 차 있으면 버림 + 버퍼 반납 */
        for (uint32_t i = 0; i < TX_RING; i++) tx_one(610 + i, 20, NULL);
        gen_frame(620, 40, f); wire_rx(f, 40, false); dma_rx_step();
        memset(&p, 0, sizeof p);
        CHECK(drv_rx_poll(&p) == 1);
        unsigned nf = g_nfree;
        CHECK(app_echo(&p) == DRV_EBUSY && g_nfree == nf + 1u && drv_audit(NULL, 0) == 0u);
    }

    /* C3 soak: 올바른 드라이버는 랜덤 교체·프리페치·끼어들기·앱 지연 속에서도 증상 0 */
    static const uint32_t seeds[3] = { 1u, 0xC0FFEEu, 20261111u };
    for (unsigned i = 0; i < 3; i++) {
        soak_t s = soak(seeds[i], 0, false, 4000);
        CHECK(soak_symptoms(&s) == 0u);
        CHECK(s.delivered > 500u && s.multi > 100u && s.scratch > 50u && s.echoed + s.echo_drop == s.delivered);
        CHECK(g_hw.rx_rbu > 0u && g_hw.rx_missed > 0u && g_drv.rx_err > 0u && g_cs.evict_hook > 100u && g_cs.prefetch_hook > 100u);
        if (i == 0 || g_verbose)
            printf("soak seed %-9u : injected %u delivered %u (multi-desc %u) echoed %u, drops: missed %u err %u nobuf %u echo-busy %u, RBU %u -> symptoms %u\n",
                   (unsigned)seeds[i], s.injected, s.delivered, s.multi, s.echoed, g_hw.rx_missed, g_drv.rx_err, g_drv.rx_nobuf,
                   s.echo_drop, g_hw.rx_rbu, soak_symptoms(&s));
    }
    /* C4 디스크립터를 NC 영역에: 디스크립터 유지보수 0, 증상 0 */
    {
        soak_t s = soak(7u, 0, true, 3000);
        CHECK(soak_symptoms(&s) == 0u && g_cs.cmo_desc == 0u && g_cs.nc_acc > 0u);
    }
    /* C5 빠진 유지보수 하나하나가 soak 에서도 드러나는가 (seed 1) */
    static const unsigned om[] = { M_TX_BUF_CLEAN, M_TX_DESC_CLEAN, M_TX_DESC_INVAL, M_RX_BUF_PREP, M_RX_BUF_INVAL,
                                   M_RX_DESC_CLEAN, M_RX_DESC_INVAL, F_OWN_FIRST, F_FWD_ORDER, F_NO_DSB };
    unsigned caught = 0;
    for (unsigned i = 0; i < sizeof om / sizeof om[0]; i++) {
        soak_t s = soak(1u, om[i], false, 3000);
        caught += soak_symptoms(&s) > 0u;
    }
    CHECK(caught == sizeof om / sizeof om[0]);
}

#ifndef COMM_A08_NO_MAIN
int main(int argc, char **argv) {
    g_verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
