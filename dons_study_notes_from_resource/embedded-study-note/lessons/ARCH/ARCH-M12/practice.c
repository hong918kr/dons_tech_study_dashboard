/* ARCH-M12 추가 연습 — 한 번에 검증 (본문과 같은 하드웨어 모델 + 정답의 A1–A6·B1–B2 를 그대로 가져와 씀)
 *
 *   연습 1  레이아웃 검사기 shares_line + _Alignas/_Static_assert 로 고친 드라이버 구조체
 *   연습 2  양방향(BIDIRECTIONAL) 버퍼: SPI 전이중 in-place 전송 — 시작 전 clean, 완료 후 invalidate
 *   연습 3  CMSIS 옛 루프(core_cm7.h V5.00) vs 새 루프(cachel1_armv7.h V1.0.3) + 라인 크기 레지스터 해석
 *   연습 4  MPU write-through 영역: TX 는 clean 대신 dsb 만, RX 는 여전히 invalidate
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stddef.h>
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
static uint32_t g_wt_base = 1, g_wt_limit = 0;    /* 연습 4: [base, limit) = write-through 영역 (기본: 없음) */
static bool is_wt(uint32_t a) { return a >= g_wt_base && a < g_wt_limit; }
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
    if (is_wt(a)) {                                /* write-through, no-write-allocate          */
        line_t *l = find(a); if (l) l->data[a % LINE] = v;
        wb_post(a, &v, 1); return;
    }
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
    if (is_wt(a)) { line_t *l = find(a); if (l) memcpy(&l->data[a % LINE], b, 4); wb_post(a, b, 4); return; }
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
    g_wt_base = 1; g_wt_limit = 0;
}

/* ======================= Part A — 범위 유지보수와 드라이버 순서 ======================= */
/* A1–A3: [addr, addr+len) 에 걸친 모든 라인에 한 줄짜리 명령을 낸다 (부분 라인 포함 — CMSIS V1.0.3 처럼).
 *        len == 0 이면 아무것도 하지 않는다. 마지막에 dsb() — 유지보수(write-back)가 끝나야 DMA 가 본다. */
static void cache_clean_range(uint32_t addr, uint32_t len) {
    if (len == 0) return;
    for (uint32_t a = lbase(addr); a < addr + len; a += LINE) dc_clean_line(a);
    dsb();
}
static void cache_invalidate_range(uint32_t addr, uint32_t len) {
    if (len == 0) return;
    for (uint32_t a = lbase(addr); a < addr + len; a += LINE) dc_inval_line(a);
    dsb();
}
static void cache_clean_invalidate_range(uint32_t addr, uint32_t len) {
    if (len == 0) return;
    for (uint32_t a = lbase(addr); a < addr + len; a += LINE) dc_cinval_line(a);
    dsb();
}
/* A4: CPU 가 buf 에 다 쓴 TX 데이터를 장치에 넘긴다. 마지막에 dma_tx_kick(buf, len). */
static void drv_tx_submit(uint32_t buf, uint32_t len) {
    cache_clean_range(buf, len);                   /* 더티 라인 → RAM, 내부 dsb: 끝나야 장치가 본다 */
    dma_tx_kick(buf, len);
}
/* A5: RX 버퍼를 DMA 에 넘기기 전. 조건: (1) 범위 안에 더티 라인이 남지 않는다 (나중에 교체되면 DMA 데이터를 덮음)
 *     (2) 가장자리 라인을 같이 쓰는 옆 변수의 더티 데이터를 잃지 않는다. 마지막에 dma_rx_arm(buf, len). */
static void drv_rx_arm(uint32_t buf, uint32_t len) {
    cache_clean_invalidate_range(buf, len);        /* 더티 0 개 + 가장자리 옆 데이터는 RAM 으로 */
    dma_rx_arm(buf, len);
}
/* A6: DMA 완료 후, CPU 가 버퍼를 읽기 전. 그 사이 캐시에 들어온(프리페치된) 낡은 라인을 버린다. */
static void drv_rx_complete(uint32_t buf, uint32_t len) {
    dma_rx_stop();
    cache_invalidate_range(buf, len);              /* DMA 중 들어온 낡은 라인 버리기            */
}

/* ======================= Part B — 라인 정렬 규칙 ======================= */
/* B1: len 을 LINE 의 배수로 올림 (0 → 0). B2: [addr, addr+len) 이 다른 데이터와 라인을 같이 쓰지 않나? */
static uint32_t dma_round_up(uint32_t len) {
    return (len + LINE - 1u) & ~(LINE - 1u);
}
static bool dma_buf_isolated(uint32_t addr, uint32_t len) {
    return len != 0 && addr % LINE == 0 && len % LINE == 0;
}


/* ======================= 연습 1 — 레이아웃 검사 ======================= */
static bool shares_line(uint32_t a_off, uint32_t a_len, uint32_t b_off, uint32_t b_len, uint32_t line) {
    if (a_len == 0 || b_len == 0) return false;
    uint32_t a0 = a_off / line, a1 = (a_off + a_len - 1u) / line;
    uint32_t b0 = b_off / line, b1 = (b_off + b_len - 1u) / line;
    return a0 <= b1 && b0 <= a1;                   /* 라인 번호 구간이 겹치나                    */
}
struct bad_dev  { uint32_t state; uint8_t rx[40]; uint32_t rx_count; };
struct good_dev { uint32_t state; _Alignas(32) uint8_t rx[64]; _Alignas(32) uint32_t rx_count; };
_Static_assert(_Alignof(struct good_dev) == 32, "구조체 자체가 라인 정렬로 놓여야 오프셋 규칙이 주소 규칙이 된다");
_Static_assert(offsetof(struct good_dev, rx) % 32 == 0, "rx 시작 = 라인 경계");
_Static_assert(sizeof(((struct good_dev *)0)->rx) % 32 == 0, "rx 크기 = 라인 배수");
_Static_assert(offsetof(struct good_dev, rx_count) % 32 == 0, "rx 다음 필드는 새 라인에서");

/* ======================= 연습 2 — 양방향 버퍼 ======================= */
static void spi_xfer_inplace(uint32_t buf, uint32_t len) {   /* 장치: RAM 에서 명령을 읽고 같은 곳에 응답을 씀 */
    dma_tx_kick(buf, len);
    uint8_t resp[64];
    for (uint32_t i = 0; i < len; i++) resp[i] = (uint8_t)(g_dev_tx[i] ^ 0x5Au);
    dma_rx_write(0, resp, len);
}
static void drv_bidir_start(uint32_t buf, uint32_t len) {
    cache_clean_range(buf, len);                   /* 명령 → RAM, 더티 라인 0 개 (교체돼도 무해)  */
    dma_rx_arm(buf, len);
}
static void drv_bidir_done(uint32_t buf, uint32_t len) {
    dma_rx_stop();
    cache_invalidate_range(buf, len);              /* 응답을 보려면 낡은 라인을 버린다           */
}

/* ======================= 연습 3 — CMSIS 옛 루프 vs 새 루프 ======================= */
static unsigned cmsis500_invalidate(uint32_t addr, int32_t dsize) {   /* core_cm7.h V5.00 의 루프 */
    unsigned n = 0;
    int32_t op_size = dsize; uint32_t op_addr = addr;
    dsb();
    while (op_size > 0) { dc_inval_line(op_addr); n++; op_addr += LINE; op_size -= (int32_t)LINE; }
    dsb();
    return n;
}
static unsigned cmsis103_invalidate(uint32_t addr, int32_t dsize) {   /* cachel1_armv7.h V1.0.3   */
    unsigned n = 0;
    if (dsize > 0) {
        int32_t op_size = dsize + (int32_t)(addr & (LINE - 1u)); uint32_t op_addr = addr;
        dsb();
        do { dc_inval_line(op_addr); n++; op_addr += LINE; op_size -= (int32_t)LINE; } while (op_size > 0);
        dsb();
    }
    return n;
}
static uint32_t ccsidr_line_bytes(uint32_t ccsidr) { return 1u << ((ccsidr & 7u) + 4u); }   /* LineSize = log2(words) - 2 */
static uint32_t ctr_dminline_bytes(uint64_t ctr) { return 4u << ((ctr >> 16) & 0xFu); }    /* DminLine = log2(words)     */
static uint32_t round_up_line(uint32_t len, uint32_t line) { return (len + line - 1u) & ~(line - 1u); }

/* ======================= 연습 4 — write-through 영역 ======================= */
static void wt_tx_submit(uint32_t buf, uint32_t len) {
    dsb();                                         /* RAM 엔 이미 쓰는 중 — 쓰기 버퍼만 비우면 됨 */
    dma_tx_kick(buf, len);
}

/* ======================= 테스트 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static void fill_pattern(uint8_t *p, uint32_t n, uint8_t seed) {
    for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)(seed + 7u * i);
}

int main(void) {
    uint8_t pkt[64], got[64];
    /* 연습 1 */
    CHECK(shares_line(offsetof(struct bad_dev, rx), 40, offsetof(struct bad_dev, rx_count), 4, LINE));
    CHECK(shares_line(offsetof(struct bad_dev, state), 4, offsetof(struct bad_dev, rx), 40, LINE));
    CHECK(!shares_line(offsetof(struct good_dev, state), 4, offsetof(struct good_dev, rx), 64, LINE));
    CHECK(!shares_line(offsetof(struct good_dev, rx), 64, offsetof(struct good_dev, rx_count), 4, LINE));
    CHECK(!shares_line(0, 32, 32, 32, LINE) && shares_line(0, 33, 32, 1, LINE) && !shares_line(0, 0, 0, 4, LINE));
    CHECK(sizeof(struct good_dev) == 128u && offsetof(struct good_dev, rx) == 32u);
    CHECK(dma_round_up(40) == sizeof(((struct good_dev *)0)->rx) &&
          dma_buf_isolated(offsetof(struct good_dev, rx), 64) && !dma_buf_isolated(offsetof(struct bad_dev, rx), 40));
    for (int v = 0; v < 2; v++) {                  /* 두 레이아웃을 모델 RAM 0x200 에 놓고 RX + DMA 중 카운터 갱신 */
        uint32_t base = 0x200;
        uint32_t rx = base + (uint32_t)(v ? offsetof(struct good_dev, rx) : offsetof(struct bad_dev, rx));
        uint32_t cnt = base + (uint32_t)(v ? offsetof(struct good_dev, rx_count) : offsetof(struct bad_dev, rx_count));
        uint32_t len = v ? 64u : 40u;
        model_reset(); fill_pattern(pkt, len, 0x61);
        drv_rx_arm(rx, len);
        cpu_wr32(cnt, 7u);                         /* ISR 이 DMA 도중 rx_count 갱신              */
        dma_rx_write(0, pkt, len);
        drv_rx_complete(rx, len);
        unsigned bad = view_mismatch(rx, len) + view_mismatch(cnt, 4);
        CHECK(v ? bad == 0u : bad > 0u);           /* 나쁜 구조체만 손상                         */
    }

    /* 연습 2: CPU 가 명령을 쓰고, 장치가 같은 버퍼에 응답. 나쁜 순간에 프리페치·교체 */
    model_reset(); fill_pattern(pkt, 64, 0x11);
    cpu_write_buf(0x300, pkt, 64);
    drv_bidir_start(0x300, 64);
    hw_evict(0x300);                               /* clean 됐으므로 write-back 없음            */
    hw_prefetch(0x320);
    spi_xfer_inplace(0x300, 64);
    drv_bidir_done(0x300, 64);
    cpu_read_buf(0x300, got, 64);
    unsigned ok = 0;
    for (unsigned i = 0; i < 64; i++) ok += got[i] == (uint8_t)(pkt[i] ^ 0x5Au);
    CHECK(g_tx_stale == 0u && memcmp(g_dev_tx, pkt, 64) == 0);
    CHECK(ok == 64u && view_mismatch(0x300, 64) == 0u && g_rx_violations == 0u);

    /* 연습 3: 시작이 정렬 안 된 32 B 버퍼 (0x210..0x22F = 라인 두 개에 걸침) */
    model_reset(); CHECK(cmsis500_invalidate(0x210, 32) == 1u);   /* 0x220 라인을 빠뜨림        */
    model_reset(); CHECK(cmsis103_invalidate(0x210, 32) == 2u);
    model_reset(); CHECK(cmsis500_invalidate(0x200, 64) == 2u && (model_reset(), cmsis103_invalidate(0x200, 64)) == 2u);
    for (int v = 0; v < 2; v++) {                  /* 같은 RX 시나리오를 두 루프로               */
        model_reset(); fill_pattern(pkt, 32, 0x21);
        cpu_read_buf(0x210, got, 32);              /* 예전 내용이 캐시에                         */
        dma_rx_arm(0x210, 32); dma_rx_write(0, pkt, 32); dma_rx_stop();
        if (v == 0) (void)cmsis500_invalidate(0x210, 32); else (void)cmsis103_invalidate(0x210, 32);
        CHECK(view_mismatch(0x210, 32) == (v == 0 ? 16u : 0u));    /* 옛 루프: 뒤 16 B 가 낡음  */
    }
    CHECK(ccsidr_line_bytes(0x1u) == 32u && ccsidr_line_bytes(0x2u) == 64u);
    CHECK(ctr_dminline_bytes((uint64_t)4u << 16) == 64u && ctr_dminline_bytes((uint64_t)5u << 16) == 128u);
    CHECK(round_up_line(1500, 64) == 1536u && round_up_line(1500, 128) == 1536u && round_up_line(100, 128) == 128u);

    /* 연습 4: write-through 영역 [0x700, 0x800) */
    model_reset(); g_wt_base = 0x700; g_wt_limit = 0x800; fill_pattern(pkt, 64, 0x31);
    cpu_write_buf(0x700, pkt, 64);
    CHECK(find(0x700) == NULL);                    /* no-write-allocate: 쓰기로는 라인이 안 생김  */
    wt_tx_submit(0x700, 64);
    CHECK(g_tx_stale == 0u && memcmp(g_dev_tx, pkt, 64) == 0 && g_st.cmo == 0u);
    cpu_wr32(0x740, 0xA5C3E1F0u); dsb();           /* 워드 쓰기도 RAM 으로 바로 간다             */
    CHECK(g_ram[0x740] == 0xF0u && g_ram[0x743] == 0xA5u && cpu_rd32(0x740) == 0xA5C3E1F0u);
    model_reset(); fill_pattern(pkt, 64, 0x31);    /* 비교: 같은 TX 를 write-back 영역에서        */
    cpu_write_buf(0x300, pkt, 64); drv_tx_submit(0x300, 64);
    CHECK(g_tx_stale == 0u && g_st.cmo == 2u);     /* write-back 은 라인당 clean 1 번이 든다      */
    model_reset(); g_wt_base = 0x700; g_wt_limit = 0x800; fill_pattern(pkt, 64, 0x41);
    cpu_write_buf(0x700, pkt, 64);                 /* 쓰기 버퍼에만 있음                         */
    dma_tx_kick(0x700, 64);                        /* dsb 없이 바로 시작                         */
    CHECK(g_tx_stale > 0u);                        /* 모델의 성질: WT 여도 쓰기 버퍼는 비워야 함  */
    model_reset(); g_wt_base = 0x700; g_wt_limit = 0x800; fill_pattern(pkt, 64, 0x52);   /* 0 바이트 없는 패턴 */
    cpu_read_buf(0x700, got, 64);                  /* 읽기는 할당한다 → 라인 2 개 캐시에         */
    dma_rx_arm(0x700, 64);                         /* WT 라인은 더티가 될 수 없다 → 시작 전 처리 생략 가능 */
    hw_evict(0x700);                               /* 교체돼도 write-back 없음 (더티 아님)        */
    (void)cpu_rd8(0x700);                          /* DMA 도중 CPU 가 다시 읽음 → 라인 재할당    */
    dma_rx_write(0, pkt, 64); dma_rx_stop();
    CHECK(g_st.writebacks == 0u && view_mismatch(0x700, 64) == 64u);   /* invalidate 안 하면 낡음 */
    drv_rx_complete(0x700, 64);
    cpu_read_buf(0x700, got, 64);
    CHECK(memcmp(got, pkt, 64) == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
