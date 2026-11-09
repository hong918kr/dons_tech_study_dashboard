/* ARCH-M12 DMA와 캐시 일관성: Clean/Invalidate 시나리오
 *
 * 단일 스레드 모델 (Cortex-M7 같은 MCU 를 흉내):
 *   RAM g_ram[]  +  write-back / write-allocate D-cache (32 B 라인, 8 세트 x 2 웨이, LRU)
 *   + 쓰기 버퍼 (clean 의 write-back 과 non-cacheable 쓰기가 잠깐 머무는 곳, dsb() 가 비운다)
 *   + DMA 엔진 (RAM 을 직접 읽고 쓴다 — 캐시도 쓰기 버퍼도 보지 않는다)
 *   + 훅(hook): hw_evict() = 캐시가 "아무 때나" 더티 라인을 내보냄, hw_prefetch() = 추측성 라인 채움.
 *     테스트가 이 훅을 정해진 순간에 불러서 버그를 매번 똑같이 재현한다 (결정적).
 *   + 골든 모델 g_gold[]: "캐시가 없는(일관된) 메모리" 였다면 그 주소에 있어야 할 값.
 *     CPU 쓰기와 DMA 쓰기가 둘 다 g_gold 를 갱신한다. 검사 = CPU 가 보는 값 / 장치가 읽은 값 vs g_gold.
 *
 *   Part A  범위 캐시 유지보수 (CMSIS SCB_*DCache_by_Addr 모양) + TX/RX 드라이버 순서
 *   Part B  라인 정렬 규칙: dma_round_up, dma_buf_isolated, 옆 변수가 라인을 같이 쓸 때
 *   Part C  STM32H7 이더넷 같은 RX 링 + TX (디스크립터는 MPU non-cacheable 영역) + 비용 표
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st      (-v : 이벤트 추적 출력)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LINE      32u              /* Cortex-M7 D-cache 라인 = 32 B (CMSIS __SCB_DCACHE_LINE_SIZE)   */
#define NSETS     8u
#define NWAYS     2u               /* 8 x 2 x 32 = 512 B 캐시 (작게 — 충돌을 손으로 따라갈 수 있게)  */
#define RAM_SIZE  4096u
#define NC_BASE   0x0C00u          /* [NC_BASE, NC_LIMIT) = MPU 로 non-cacheable 지정한 영역          */
#define NC_LIMIT  0x1000u
#define WB_DEPTH  8u

/* ======================= 하드웨어 모델 (주어진 코드 — 고치지 않는다) ======================= */
typedef struct { bool valid, dirty; uint32_t base, lru; uint8_t data[LINE]; } line_t;
typedef struct { uint32_t addr, len; uint8_t data[LINE]; } wb_ent_t;
typedef struct { unsigned fills, writebacks, nc_rd, nc_wr, cmo, dsb, dma_rd, dma_wr; } stats_t;

static uint8_t  g_ram[RAM_SIZE];   /* 진짜 RAM (DMA 가 보는 곳)                       */
static uint8_t  g_gold[RAM_SIZE];  /* 일관된 메모리였다면의 값                         */
static line_t   g_cache[NSETS][NWAYS];
static wb_ent_t g_wb[WB_DEPTH];
static unsigned g_wb_n;
static uint32_t g_tick;
static stats_t  g_st;
static bool     g_verbose;
static unsigned g_tx_stale;        /* 장치가 TX 때 읽은 바이트 중 골든과 다른 수       */
static uint8_t  g_dev_tx[256];     /* 장치(MAC/UART)가 실제로 내보낸 바이트            */
static uint32_t g_dev_tx_len;
static struct { bool armed; uint32_t addr, len; } g_rx;
static unsigned g_rx_violations;   /* arm 안 된 버퍼에 DMA 가 쓰려 한 횟수 (모델 오류)  */

#define TRACE(...) do { if (g_verbose) { printf("    · "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t lbase(uint32_t a) { return a & ~(LINE - 1u); }
static bool is_nc(uint32_t a) { return a >= NC_BASE && a < NC_LIMIT; }
static line_t *find(uint32_t a) {
    uint32_t b = lbase(a), s = (b / LINE) % NSETS;
    for (unsigned w = 0; w < NWAYS; w++)
        if (g_cache[s][w].valid && g_cache[s][w].base == b) return &g_cache[s][w];
    return NULL;
}
static void wb_drain(void) {                       /* 쓰기 버퍼 → RAM (오래된 것부터)          */
    for (unsigned i = 0; i < g_wb_n; i++) memcpy(&g_ram[g_wb[i].addr], g_wb[i].data, g_wb[i].len);
    g_wb_n = 0;
}
static void wb_post(uint32_t addr, const uint8_t *src, uint32_t len) {
    if (g_wb_n == WB_DEPTH) wb_drain();            /* 가득 차면 하드웨어가 알아서 비운다       */
    g_wb[g_wb_n].addr = addr; g_wb[g_wb_n].len = len; memcpy(g_wb[g_wb_n].data, src, len); g_wb_n++;
}
static void writeback(line_t *l) {                 /* 교체(eviction) 때의 write-back: RAM 직행 */
    memcpy(&g_ram[l->base], l->data, LINE); l->dirty = false; g_st.writebacks++;
}
static line_t *fill(uint32_t a) {                  /* write-allocate 미스 처리                  */
    uint32_t b = lbase(a), s = (b / LINE) % NSETS;
    line_t *v = &g_cache[s][0];
    for (unsigned w = 0; w < NWAYS; w++) {
        if (!g_cache[s][w].valid) { v = &g_cache[s][w]; break; }
        if (g_cache[s][w].lru < v->lru) v = &g_cache[s][w];
    }
    if (v->valid && v->dirty) { TRACE("교체: 더티 라인 0x%03x write-back → RAM", (unsigned)v->base); writeback(v); }
    wb_drain();                                    /* CPU 는 자기 쓰기를 항상 본다 (단순화)     */
    memcpy(v->data, &g_ram[b], LINE); v->base = b; v->valid = true; v->dirty = false; g_st.fills++;
    return v;
}
static line_t *touch(uint32_t a) { line_t *l = find(a); if (!l) l = fill(a); l->lru = ++g_tick; return l; }

/* ---- CPU 접근 (= 실제 load/store 명령. volatile 접근도 정확히 이것이다) ---- */
static uint8_t cpu_rd8(uint32_t a) {
    if (is_nc(a)) { wb_drain(); g_st.nc_rd++; return g_ram[a]; }
    return touch(a)->data[a % LINE];
}
static void cpu_wr8(uint32_t a, uint8_t v) {
    g_gold[a] = v;
    if (is_nc(a)) { wb_post(a, &v, 1); g_st.nc_wr++; return; }
    line_t *l = touch(a); l->data[a % LINE] = v; l->dirty = true;
}
static uint32_t cpu_rd32(uint32_t a) {             /* a 는 4 의 배수 (한 라인 안)              */
    uint8_t b[4];
    if (is_nc(a)) { wb_drain(); g_st.nc_rd++; memcpy(b, &g_ram[a], 4); }
    else memcpy(b, &touch(a)->data[a % LINE], 4);
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static void cpu_wr32(uint32_t a, uint32_t v) {
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    memcpy(&g_gold[a], b, 4);
    if (is_nc(a)) { wb_post(a, b, 4); g_st.nc_wr++; return; }
    line_t *l = touch(a); memcpy(&l->data[a % LINE], b, 4); l->dirty = true;
}
static void cpu_write_buf(uint32_t a, const uint8_t *p, uint32_t n) { for (uint32_t i = 0; i < n; i++) cpu_wr8(a + i, p[i]); }
static void cpu_read_buf(uint32_t a, uint8_t *p, uint32_t n) { for (uint32_t i = 0; i < n; i++) p[i] = cpu_rd8(a + i); }
/* 부작용 없이 "CPU 가 지금 읽으면 받을 값" (검사용) */
static uint8_t cpu_view(uint32_t a) {
    line_t *l = is_nc(a) ? NULL : find(a);
    if (l) return l->data[a % LINE];
    for (unsigned i = g_wb_n; i-- > 0;)
        if (a >= g_wb[i].addr && a < g_wb[i].addr + g_wb[i].len) return g_wb[i].data[a - g_wb[i].addr];
    return g_ram[a];
}
static unsigned view_mismatch(uint32_t a, uint32_t n) {
    unsigned bad = 0;
    for (uint32_t i = 0; i < n; i++) bad += cpu_view(a + i) != g_gold[a + i];
    return bad;
}

/* ---- 하드웨어가 제멋대로 하는 일 (테스트가 순간을 고른다) ---- */
static void hw_evict(uint32_t a) {                 /* 교체 정책이 이 라인을 지금 골랐다         */
    line_t *l = find(a);
    if (!l) return;
    if (l->dirty) { TRACE("교체(훅): 더티 라인 0x%03x write-back → RAM  ← DMA 데이터 위에 덮어씀?", (unsigned)l->base); writeback(l); }
    l->valid = false;
}
static void hw_prefetch(uint32_t a) {              /* 추측성 읽기/프리페처가 라인을 채움        */
    if (is_nc(a) || find(a)) return;
    TRACE("프리페치(훅): 라인 0x%03x 를 RAM 에서 채움 (그 순간의 RAM 값)", (unsigned)lbase(a));
    touch(a);
}

/* ---- 캐시 유지보수 명령 한 줄짜리 (DCCMVAC / DCIMVAC / DCCIMVAC — 하위 5 비트 무시) ---- */
static void dc_clean_line(uint32_t a) {
    g_st.cmo++;
    line_t *l = find(a);
    if (l && l->dirty) { wb_post(l->base, l->data, LINE); l->dirty = false; TRACE("clean 0x%03x (쓰기 버퍼로)", (unsigned)l->base); }
}
static void dc_inval_line(uint32_t a) {
    g_st.cmo++;
    line_t *l = find(a);
    if (l) { if (l->dirty) TRACE("invalidate 0x%03x — 더티 데이터 버려짐!", (unsigned)l->base); l->valid = false; }
}
static void dc_cinval_line(uint32_t a) {
    g_st.cmo++;
    line_t *l = find(a);
    if (!l) return;
    if (l->dirty) { wb_post(l->base, l->data, LINE); TRACE("clean+invalidate 0x%03x (더티 → 쓰기 버퍼)", (unsigned)l->base); }
    l->valid = false;
}
static void dsb(void) { wb_drain(); g_st.dsb++; }   /* 앞선 쓰기·유지보수가 끝날 때까지 기다림 */

/* ---- DMA 엔진 / 장치 (RAM 만 본다) ---- */
static void dma_tx_kick(uint32_t addr, uint32_t len) {   /* 장치가 RAM 에서 len 바이트를 읽어 내보냄 */
    for (uint32_t i = 0; i < len; i++) {
        g_dev_tx[i] = g_ram[addr + i];
        g_tx_stale += g_ram[addr + i] != g_gold[addr + i];
    }
    g_dev_tx_len = len; g_st.dma_rd += len;
    TRACE("DMA TX: RAM 0x%03x..+%u 읽음", (unsigned)addr, (unsigned)len);
}
static void dma_rx_arm(uint32_t addr, uint32_t len) { g_rx.armed = true; g_rx.addr = addr; g_rx.len = len; }
static void dma_rx_write(uint32_t off, const uint8_t *src, uint32_t n) {   /* 장치가 RAM 에 직접 씀 */
    if (!g_rx.armed || off + n > g_rx.len) { g_rx_violations++; return; }
    memcpy(&g_ram[g_rx.addr + off], src, n); memcpy(&g_gold[g_rx.addr + off], src, n); g_st.dma_wr += n;
    TRACE("DMA RX: RAM 0x%03x..+%u 씀", (unsigned)(g_rx.addr + off), (unsigned)n);
}
static void dma_rx_stop(void) { g_rx.armed = false; }

static void model_reset(void) {
    memset(g_ram, 0, sizeof g_ram); memset(g_gold, 0, sizeof g_gold); memset(g_cache, 0, sizeof g_cache);
    memset(&g_st, 0, sizeof g_st); memset(&g_rx, 0, sizeof g_rx);
    g_wb_n = 0; g_tick = 0; g_tx_stale = 0; g_dev_tx_len = 0; g_rx_violations = 0;
}

/* ======================= Part A — 범위 유지보수와 드라이버 순서 ======================= */
/* A1–A3: [addr, addr+len) 에 걸친 모든 라인에 한 줄짜리 명령을 낸다 (부분 라인 포함 — CMSIS V1.0.3 처럼).
 *        len == 0 이면 아무것도 하지 않는다. 마지막에 dsb() — 유지보수(write-back)가 끝나야 DMA 가 본다. */
static void cache_clean_range(uint32_t addr, uint32_t len) {
    /* TODO A1 */
    (void)addr; (void)len; (void)dc_clean_line;
}
static void cache_invalidate_range(uint32_t addr, uint32_t len) {
    /* TODO A2 */
    (void)addr; (void)len;
}
static void cache_clean_invalidate_range(uint32_t addr, uint32_t len) {
    /* TODO A3 */
    (void)addr; (void)len;
}
/* A4: CPU 가 buf 에 다 쓴 TX 데이터를 장치에 넘긴다. 마지막에 dma_tx_kick(buf, len). */
static void drv_tx_submit(uint32_t buf, uint32_t len) {
    /* TODO A4 */
    dma_tx_kick(buf, len);
}
/* A5: RX 버퍼를 DMA 에 넘기기 전. 조건: (1) 범위 안에 더티 라인이 남지 않는다 (나중에 교체되면 DMA 데이터를 덮음)
 *     (2) 가장자리 라인을 같이 쓰는 옆 변수의 더티 데이터를 잃지 않는다. 마지막에 dma_rx_arm(buf, len). */
static void drv_rx_arm(uint32_t buf, uint32_t len) {
    /* TODO A5 */
    dma_rx_arm(buf, len);
}
/* A6: DMA 완료 후, CPU 가 버퍼를 읽기 전. 그 사이 캐시에 들어온(프리페치된) 낡은 라인을 버린다. */
static void drv_rx_complete(uint32_t buf, uint32_t len) {
    dma_rx_stop();
    /* TODO A6 */
    (void)buf; (void)len;
}

/* ======================= Part B — 라인 정렬 규칙 ======================= */
/* B1: len 을 LINE 의 배수로 올림 (0 → 0). B2: [addr, addr+len) 이 다른 데이터와 라인을 같이 쓰지 않나? */
static uint32_t dma_round_up(uint32_t len) {
    /* TODO B1 */
    return len;
}
static bool dma_buf_isolated(uint32_t addr, uint32_t len) {
    /* TODO B2 */
    (void)addr; (void)len;
    return true;
}

/* ======================= Part C — 이더넷 RX 링 + TX (STM32H7 ETH 모양, 단순화) ======================= */
/* 디스크립터 = non-cacheable 영역의 3 워드 {buf, len, own}. own 1 = DMA 소유, 0 = CPU 소유. */
#define RING     4u
#define RXBUF    64u                               /* 라인 배수                                   */
#define RXDESC   (NC_BASE + 0x000u)                /* RX 디스크립터 i: RXDESC + 16 i               */
#define TXDESC   (NC_BASE + 0x080u)
#define RXPOOL   0x0400u                           /* RX 버퍼 i: RXPOOL + RXBUF i (캐시 가능 RAM)  */
#define TXBUF    0x0600u
#define D_BUF 0u
#define D_LEN 4u
#define D_OWN 8u
static unsigned g_drv_rx_idx, g_mac_rx_idx, g_mac_drops, g_mac_tx_bad_desc;

static void eth_init(void) {                       /* 주어진 코드: 모든 RX 버퍼를 DMA 에 넘김      */
    g_drv_rx_idx = g_mac_rx_idx = g_mac_drops = g_mac_tx_bad_desc = 0;
    for (uint32_t i = 0; i < RING; i++) {
        uint32_t d = RXDESC + 16u * i, b = RXPOOL + RXBUF * i;
        for (uint32_t k = 0; k < RXBUF; k++) cpu_wr8(b + k, 0);   /* .bss 0 초기화 = 캐시에 더티 라인 */
        drv_rx_arm(b, RXBUF);
        cpu_wr32(d + D_BUF, b); cpu_wr32(d + D_LEN, 0); cpu_wr32(d + D_OWN, 1);
    }
    dsb();
}
/* MAC(장치) 쪽 모델: 다음 디스크립터가 DMA 소유면 프레임을 RAM 에 쓰고 len, own=0 을 RAM 에 직접 쓴다. */
static void mac_deliver(const uint8_t *frame, uint32_t len) {
    uint32_t d = RXDESC + 16u * g_mac_rx_idx;
    uint32_t own, buf;
    memcpy(&own, &g_ram[d + D_OWN], 4); memcpy(&buf, &g_ram[d + D_BUF], 4);   /* 호스트 = little-endian */
    if (own != 1u || len > RXBUF) { g_mac_drops++; return; }
    dma_rx_arm(buf, RXBUF);                        /* (모델 편의: 링 버퍼마다 armed 창)            */
    dma_rx_write(0, frame, len);
    uint32_t zero = 0;
    memcpy(&g_ram[d + D_LEN], &len, 4); memcpy(&g_ram[d + D_OWN], &zero, 4);
    memcpy(&g_gold[d + D_LEN], &len, 4); memcpy(&g_gold[d + D_OWN], &zero, 4);
    g_mac_rx_idx = (g_mac_rx_idx + 1u) % RING;
}
static void mac_tx_doorbell(void) {                /* TX 도어벨 레지스터 쓰기 = 즉시 디스크립터를 읽음 */
    uint32_t own, len, buf;
    memcpy(&own, &g_ram[TXDESC + D_OWN], 4); memcpy(&len, &g_ram[TXDESC + D_LEN], 4);
    memcpy(&buf, &g_ram[TXDESC + D_BUF], 4);
    if (own != 1u || len == 0 || len > sizeof g_dev_tx || buf != TXBUF) { g_mac_tx_bad_desc++; return; }
    dma_tx_kick(buf, len);
    uint32_t zero = 0;
    memcpy(&g_ram[TXDESC + D_OWN], &zero, 4); memcpy(&g_gold[TXDESC + D_OWN], &zero, 4);
}
/* C1: 다음 RX 디스크립터가 CPU 소유면 프레임을 out 에 복사하고 버퍼를 DMA 에 다시 넘긴 뒤 길이를 돌려준다.
 *     아직 DMA 소유면 -1. (len > cap 이면 len 만큼 복사하지 말고 -2, 그래도 버퍼는 다시 넘긴다.) */
static int eth_rx_poll(uint8_t *out, uint32_t cap) {
    /* TODO C1 */
    (void)out; (void)cap;
    return -1;
}
/* C2: frame 을 TXBUF 에 복사 → 디스크립터 {TXBUF, len, own=1} → 도어벨 (mac_tx_doorbell). */
static void eth_tx_send(const uint8_t *frame, uint32_t len) {
    /* TODO C2 */
    (void)frame; (void)len;
    mac_tx_doorbell();
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; if (g_verbose) printf("PASS %s\n", #expr); } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define SCENARIO(name) do { if (g_verbose) printf("-- %s\n", name); } while (0)

static void fill_pattern(uint8_t *p, uint32_t n, uint8_t seed) {
    for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)(seed + 7u * i);
}

/* 공유 라인 실험 (모델의 성질): 버퍼 끝과 옆 변수가 한 라인, DMA 중 CPU 가 옆 변수를 씀.
 * 완료 후 그 라인에 after_op(0 = invalidate, 1 = clean+invalidate)을 하면 어느 쪽이 손상되나. */
static void shared_line_experiment(int after_op, unsigned *bad_buf, unsigned *bad_nb) {
    model_reset();
    const uint32_t buf = 0x200, len = 40, nb = 0x228;      /* 0x220 라인 = buf[32..39] + nb        */
    uint8_t data[40]; fill_pattern(data, 40, 0x51);
    for (uint32_t a = lbase(buf); a < buf + len + 4; a += LINE) dc_cinval_line(a);
    dsb();
    dma_rx_arm(buf, len);
    cpu_wr32(nb, 0xDEADBEEFu);         /* DMA 진행 중 옆 변수 갱신 → 0x220 라인을 (DMA 전 RAM 값으로) 채우고 더티 */
    dma_rx_write(0, data, len);
    dma_rx_stop();
    for (uint32_t a = lbase(buf); a < buf + len; a += LINE) { if (after_op) dc_cinval_line(a); else dc_inval_line(a); }
    dsb();
    *bad_buf = view_mismatch(buf, len);
    *bad_nb = view_mismatch(nb, 4);
}

static void test_part_a(void) {
    uint8_t pkt[64], got[64];
    /* A1–A3 라인 수: [addr, addr+len) 에 걸친 라인마다 정확히 1 번 */
    model_reset(); cache_clean_range(0x100, 64);            CHECK(g_st.cmo == 2u);
    model_reset(); cache_invalidate_range(0x11F, 2);        CHECK(g_st.cmo == 2u);   /* 0x11F, 0x120 */
    model_reset(); cache_clean_invalidate_range(0x105, 1);  CHECK(g_st.cmo == 1u);
    model_reset(); cache_clean_range(0x100, 0);             CHECK(g_st.cmo == 0u && g_st.dsb == 0u);
    model_reset(); cache_invalidate_range(0x10C, 52);       CHECK(g_st.cmo == 2u);   /* 0x100, 0x120 */
    model_reset(); cache_clean_range(0x10C, 53);            CHECK(g_st.cmo == 3u && g_st.dsb == 1u);

    SCENARIO("A-TX: CPU 가 쓴 패킷을 DMA 로 보냄");
    model_reset(); fill_pattern(pkt, 64, 0x10);
    cpu_write_buf(0x300, pkt, 64);                          /* 캐시에만 있음 (더티)                  */
    drv_tx_submit(0x300, 64);
    CHECK(g_tx_stale == 0u && memcmp(g_dev_tx, pkt, 64) == 0);

    SCENARIO("A-RX1: 예전에 읽어 둔 버퍼에 새 데이터가 DMA 로 들어옴");
    model_reset(); fill_pattern(pkt, 64, 0x20);
    cpu_read_buf(0x300, got, 64);                           /* 이전 패킷 처리 → 라인이 캐시에 (clean) */
    drv_rx_arm(0x300, 64);
    dma_rx_write(0, pkt, 64);
    drv_rx_complete(0x300, 64);
    cpu_read_buf(0x300, got, 64);
    CHECK(memcmp(got, pkt, 64) == 0 && view_mismatch(0x300, 64) == 0u);

    SCENARIO("A-RX2: 버퍼를 memset(0) 해 두고 DMA 시작, DMA 도중 더티 라인이 교체됨");
    model_reset(); fill_pattern(pkt, 64, 0x30);
    for (uint32_t i = 0; i < 64; i++) cpu_wr8(0x300 + i, 0);   /* 흔한 습관: 받기 전에 0 으로 지움 */
    drv_rx_arm(0x300, 64);
    dma_rx_write(0, pkt, 32);
    hw_evict(0x300);                                        /* DMA 가 쓴 직후 라인 0 교체            */
    dma_rx_write(32, pkt + 32, 32);
    hw_evict(0x320);
    drv_rx_complete(0x300, 64);
    cpu_read_buf(0x300, got, 64);
    CHECK(memcmp(got, pkt, 64) == 0);
    CHECK(memcmp(&g_ram[0x300], pkt, 64) == 0);             /* RAM 자체가 망가지지 않았나            */

    SCENARIO("A-RX2': 같은 상황에서 '완료 후 invalidate' 만 하면? (시작 전 처리를 건너뜀)");
    model_reset(); fill_pattern(pkt, 64, 0x30);
    for (uint32_t i = 0; i < 64; i++) cpu_wr8(0x300 + i, 0);
    dma_rx_arm(0x300, 64);                                  /* drv_rx_arm 없이 바로 시작             */
    dma_rx_write(0, pkt, 64);
    hw_evict(0x300);
    drv_rx_complete(0x300, 64);
    cpu_read_buf(0x300, got, 64);
    CHECK(memcmp(got, pkt, 32) != 0);                       /* 모델의 성질: 완료 후만으론 못 살린다   */

    SCENARIO("A-RX3: DMA 도중 추측성 프리페치가 낡은 라인을 다시 가져옴");
    model_reset(); fill_pattern(pkt, 64, 0x40);
    drv_rx_arm(0x300, 64);
    hw_prefetch(0x300); hw_prefetch(0x320);                 /* 시작 직후, DMA 가 쓰기 전             */
    dma_rx_write(0, pkt, 64);
    drv_rx_complete(0x300, 64);
    cpu_read_buf(0x300, got, 64);
    CHECK(memcmp(got, pkt, 64) == 0);
    CHECK(g_rx_violations == 0u);
}

static void test_part_b(void) {
    uint8_t pkt[64], got[64];
    CHECK(dma_round_up(0) == 0u && dma_round_up(1) == 32u && dma_round_up(32) == 32u);
    CHECK(dma_round_up(40) == 64u && dma_round_up(1500) == 1504u && dma_round_up(1536) == 1536u);
    CHECK(dma_buf_isolated(0x200, 64) && dma_buf_isolated(0x220, 32));
    CHECK(!dma_buf_isolated(0x200, 40) && !dma_buf_isolated(0x210, 32) && !dma_buf_isolated(0x200, 0));

    SCENARIO("B1: 40 B RX 버퍼 끝 라인을 옆 카운터가 같이 씀 — arm 할 때 카운터가 살아남나");
    model_reset(); fill_pattern(pkt, 40, 0x60);
    cpu_wr32(0x228, 0xDEADBEEFu);                             /* 옆 변수: 더티, RAM 엔 아직 0         */
    drv_rx_arm(0x200, 40);
    CHECK(view_mismatch(0x228, 4) == 0u);                   /* invalidate 로 버리면 0 으로 돌아감   */
    dma_rx_write(0, pkt, 40);
    drv_rx_complete(0x200, 40);
    cpu_read_buf(0x200, got, 40);
    CHECK(memcmp(got, pkt, 40) == 0 && cpu_rd32(0x228) == 0xDEADBEEFu);

    unsigned bb, bn;                                        /* 모델의 성질 (TODO 와 무관하게 성립) */
    shared_line_experiment(0, &bb, &bn); CHECK(bb == 0u && bn == 4u);   /* inval: 옆 변수 4 B 손실 */
    shared_line_experiment(1, &bb, &bn); CHECK(bb == 8u && bn == 0u);   /* c+i: 버퍼 8 B 가 낡은 값 */

    SCENARIO("B2: 규칙대로 배치 — 버퍼 크기 올림, 옆 변수는 다음 라인");
    model_reset(); fill_pattern(pkt, 40, 0x70);
    uint32_t buf = 0x200, cap = dma_round_up(40), nb = buf + cap;   /* nb = 0x240 이어야 함        */
    CHECK(dma_buf_isolated(buf, cap) && lbase(nb) != lbase(buf + 39u));
    drv_rx_arm(buf, cap);
    dma_rx_write(0, pkt, 40);
    cpu_wr32(nb, 0xDEADBEEFu);                                /* DMA 중 옆 변수 갱신 — 이제 안전      */
    drv_rx_complete(buf, cap);
    cpu_read_buf(buf, got, 40);
    CHECK(memcmp(got, pkt, 40) == 0 && cpu_rd32(nb) == 0xDEADBEEFu && view_mismatch(buf, cap + 4) == 0u);
}

static void test_part_c(void) {
    uint8_t f[64], got[64], echo[64];
    unsigned ok_rx = 0, ok_tx = 0, frames = 0;
    SCENARIO("C: 이더넷 RX 링 4 개 + 에코 TX, 나쁜 순간에 교체·프리페치");
    model_reset(); eth_init();
    for (unsigned r = 0; r < 12; r++) {
        uint32_t len = 20u + 4u * r;                        /* 20..64 B                            */
        fill_pattern(f, len, (uint8_t)(0x80 + r));
        uint32_t cur = RXPOOL + RXBUF * (r % RING);
        hw_prefetch(cur + LINE);                            /* MAC 이 쓰기 직전: 둘째 라인 프리페치  */
        mac_deliver(f, len);
        hw_evict(cur);                                      /* MAC 이 쓴 직후: 첫 라인 교체         */
        int n = eth_rx_poll(got, sizeof got);
        frames++;
        if (n == (int)len && memcmp(got, f, len) == 0) ok_rx++;
        if (n > 0) {
            for (int i = 0; i < n; i++) echo[i] = (uint8_t)(got[n - 1 - i] ^ 0xFFu);   /* 응답 프레임 */
            g_tx_stale = 0; g_dev_tx_len = 0;
            eth_tx_send(echo, (uint32_t)n);
            if (g_dev_tx_len == (uint32_t)n && g_tx_stale == 0u && memcmp(g_dev_tx, echo, (size_t)n) == 0) ok_tx++;
        }
    }
    CHECK(frames == 12u && ok_rx == 12u);
    CHECK(ok_tx == 12u && g_mac_tx_bad_desc == 0u);
    CHECK(g_mac_drops == 0u && g_rx_violations == 0u);
    CHECK(eth_rx_poll(got, sizeof got) == -1);              /* 새 프레임 없음                       */
    fill_pattern(f, 48, 0x99); mac_deliver(f, 48);
    CHECK(eth_rx_poll(got, 16) == -2);                      /* 너무 작은 out → -2, 버퍼는 반납      */
    fill_pattern(f, 30, 0x9A); mac_deliver(f, 30);
    CHECK(g_mac_drops == 0u && eth_rx_poll(got, sizeof got) == 30 && memcmp(got, f, 30) == 0);
}

/* 비용 표 (결정적 카운트): 프레임 N 개를 받아 워드 단위 체크섬. 캐시 가능 + 유지보수 vs non-cacheable 버퍼. */
static void cost_table(void) {
    const unsigned N = 8;
    uint8_t f[64];
    uint32_t sum_c = 0, sum_n = 0;
    model_reset();
    for (unsigned k = 0; k < N; k++) {                      /* (1) 캐시 가능 RAM 버퍼 0x300         */
        fill_pattern(f, 64, (uint8_t)k);
        drv_rx_arm(0x300, 64); dma_rx_write(0, f, 64); drv_rx_complete(0x300, 64);
        for (uint32_t i = 0; i < 64; i += 4) sum_c += cpu_rd32(0x300 + i);
        for (uint32_t i = 0; i < 64; i += 4) sum_c ^= cpu_rd32(0x300 + i);   /* 두 번째 훑기       */
    }
    stats_t c = g_st;
    model_reset();
    for (unsigned k = 0; k < N; k++) {                      /* (2) MPU non-cacheable 버퍼 0xD00     */
        fill_pattern(f, 64, (uint8_t)k);
        dma_rx_arm(0xD00, 64); dma_rx_write(0, f, 64); dma_rx_stop();
        for (uint32_t i = 0; i < 64; i += 4) sum_n += cpu_rd32(0xD00 + i);
        for (uint32_t i = 0; i < 64; i += 4) sum_n ^= cpu_rd32(0xD00 + i);
    }
    stats_t n = g_st;
    printf("cost (N=%u frames x 64 B, read twice):  line fills  NC reads  CMO lines  DSB\n", N);
    printf("  cacheable + maintenance             %10u %9u %10u %4u\n", c.fills, c.nc_rd, c.cmo, c.dsb);
    printf("  MPU non-cacheable                   %10u %9u %10u %4u\n", n.fills, n.nc_rd, n.cmo, n.dsb);
    CHECK(sum_c == sum_n);                                  /* 같은 데이터를 봤다                  */
    CHECK(c.fills == 2u * N && n.fills == 0u && n.nc_rd == 32u * N && n.cmo == 0u);
}

int main(int argc, char **argv) {
    g_verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    test_part_a();
    test_part_b();
    test_part_c();
    cost_table();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
