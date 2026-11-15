/* ARCH-M06 쓰기 정책: Write-back vs Write-through
 *
 * 단일 스레드 캐시 + 메모리 트래픽 모델 (ARCH-M04/M05 의 N-way LRU 시뮬레이터를 축소해 넣었다):
 *   - 파라미터: 용량 C, 라인 B, 연관도 W (모두 2의 거듭제곱), 세트 S = C / (B*W), 교체 = LRU
 *   - 쓰기 정책 2축: write-back(WB) / write-through(WT)  x  write-allocate(WA) / no-write-allocate(NWA)
 *   - 쓰기 버퍼: K 칸 FIFO (K = 0 이면 없음), 라인 단위 합치기(coalescing) 켜고 끄기,
 *     drain_period 접근마다 1칸이 배경에서 DRAM 으로 나감 (DRAM 쓰기 포트의 속도 모델)
 *   - MPU 같은 영역 속성: [wt_base, wt_limit) 는 기본 정책과 무관하게 WT + NWA
 *   - 데이터까지 들고 다닌다: g_dram (실제 DRAM) vs g_gold (캐시가 없었다면의 메모리).
 *     모든 읽기 결과와 마지막 DRAM 을 골든과 비교한다 → 트래픽만 맞고 데이터가 틀린 구현을 잡는다.
 *
 * DRAM 바이트 세는 규칙 (레슨 전체에서 고정):
 *   읽기  : 라인 채움(fill) 1 번 = B 바이트, 트랜잭션 1
 *   쓰기  : 더티 라인 교체 = B 바이트 / 트랜잭션 1 (라인 전체 — 더티 비트가 라인당 1 개라서)
 *           버퍼 없는 store-through(WT 쓰기, NWA 쓰기 미스) = store 크기 바이트 / 트랜잭션 1
 *           버퍼 칸 1 개 배출(drain) = 그 칸의 유효 바이트 수(바이트 마스크 popcount) / 트랜잭션 1
 *           clean_range 의 write-back = B 바이트 / 트랜잭션 1
 *   flush : 실행 끝에 남은 더티 라인 write-back = B 바이트씩, wr_bytes 와 따로 flush_bytes 에.
 *           (실행 끝의 버퍼 배출은 이미 "보낸" 쓰기라 wr_bytes 에 넣는다)
 *
 *   Part A  4 조합의 읽기/쓰기 경로: evict, cache_write, cache_flush
 *   Part B  쓰기 버퍼: 합치기 + 꽉 참 스톨, 읽기 미스 전 같은 라인 배출 (RAW 위험)
 *   Part C  DMA 가 읽는 로그 링: clean_range, log_publish (링 wrap) — WB+clean vs WT 영역
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXS      256u
#define MAXW      16u
#define MAXB      64u              /* 바이트 마스크가 uint64_t 이므로 라인은 최대 64 B          */
#define MAXK      16u
#define DRAM_SIZE 0x40000u         /* 256 KB 모델 DRAM                                          */

typedef struct { uint32_t addr; char rw; uint8_t size; } trace_t;   /* ARCH-M04~M07 공통 레코드 */

typedef struct {
    uint32_t C, B, W;              /* 용량, 라인, 연관도                                        */
    bool     wb, wa;               /* 기본 정책: write-back?, write-allocate?                   */
    unsigned K;                    /* 쓰기 버퍼 칸 수 (0 = 없음)                                 */
    bool     coalesce;             /* 같은 라인 store 를 한 칸에 합치나                          */
    unsigned drain_period;         /* 이 접근 수마다 버퍼 1 칸이 배경에서 나감 (0 = 안 나감)     */
    uint32_t wt_base, wt_limit;    /* 이 범위는 WT+NWA (MPU 영역 속성). base == limit 이면 없음  */
} cfg_t;

typedef struct {
    uint64_t accesses, reads, writes, hits, misses;
    uint64_t rd_bytes, rd_txn;     /* DRAM 읽기: 라인 채움                                       */
    uint64_t wr_bytes, wr_txn;     /* DRAM 쓰기 (실행 중): store-through, 교체, 버퍼 배출, clean */
    uint64_t evict_bytes;          /*   그중 더티 교체                                           */
    uint64_t clean_bytes;          /*   그중 clean_range                                         */
    uint64_t flush_bytes;          /* 실행 끝 flush (wr_bytes 에 안 들어감)                       */
    uint64_t stalls;               /* 메모리로 가는 store 가 기다린 횟수 (K=0 이면 매번, K>0 이면 꽉 참) */
    uint64_t coalesced, raw_drains, cmo_lines;
    uint64_t read_mismatch, final_mismatch;
} stats_t;

typedef struct { bool valid, dirty; uint32_t line; uint64_t lru; uint8_t data[MAXB]; } cline_t;
typedef struct { uint32_t line; uint64_t mask; uint8_t data[MAXB]; } wbe_t;

static struct {
    cfg_t    cfg;
    uint32_t S, obits;
    cline_t  set[MAXS][MAXW];
    wbe_t    wbuf[MAXK];           /* wbuf[0] = 가장 오래된 칸                                   */
    unsigned wn;
    uint64_t clock, cycle;
    stats_t  st;
} g;
static uint8_t g_dram[DRAM_SIZE], g_gold[DRAM_SIZE];

/* ======================= 주어진 코드 (모델의 뼈대) ======================= */
static bool     is_pow2(uint32_t x) { return x != 0 && (x & (x - 1u)) == 0; }
static unsigned log2u(uint32_t x)   { unsigned n = 0; while (x > 1u) { x >>= 1; n++; } return n; }

static bool sim_init(const cfg_t *c) {
    if (!is_pow2(c->C) || !is_pow2(c->B) || !is_pow2(c->W)) return false;
    if (c->B > MAXB || c->W > MAXW || c->K > MAXK || c->C < c->B * c->W) return false;
    if (c->C / (c->B * c->W) > MAXS) return false;
    if (((c->wt_base | c->wt_limit) & (c->B - 1u)) != 0 || c->wt_limit < c->wt_base) return false;
    memset(&g, 0, sizeof g);
    g.cfg = *c;
    g.S = c->C / (c->B * c->W);
    g.obits = log2u(c->B);
    for (uint32_t i = 0; i < DRAM_SIZE; i++) g_dram[i] = g_gold[i] = (uint8_t)(i * 13u + 5u);
    return true;
}
static uint32_t line_of(uint32_t a) { return a & ~(g.cfg.B - 1u); }
static uint32_t set_of(uint32_t a)  { return (a >> g.obits) & (g.S - 1u); }
static bool in_wt(uint32_t a)  { return a >= g.cfg.wt_base && a < g.cfg.wt_limit; }
static bool pol_wb(uint32_t a) { return g.cfg.wb && !in_wt(a); }
static bool pol_wa(uint32_t a) { return g.cfg.wa && !in_wt(a); }

static int lookup(uint32_t a) {
    uint32_t s = set_of(a), l = line_of(a);
    for (unsigned w = 0; w < g.cfg.W; w++)
        if (g.set[s][w].valid && g.set[s][w].line == l) return (int)w;
    return -1;
}
static unsigned victim(uint32_t s) {               /* 빈 웨이 먼저, 없으면 LRU             */
    unsigned v = 0;
    for (unsigned w = 0; w < g.cfg.W; w++) {
        if (!g.set[s][w].valid) return w;
        if (g.set[s][w].lru < g.set[s][v].lru) v = w;
    }
    return v;
}
static void dram_raw_write(uint32_t a, const uint8_t *src, uint32_t n) { memcpy(&g_dram[a], src, n); }
static void dram_write(uint32_t a, const uint8_t *src, uint32_t n) {     /* 실행 중 쓰기 1 트랜잭션 */
    dram_raw_write(a, src, n);
    g.st.wr_bytes += n; g.st.wr_txn++;
}
static void wbuf_drain_oldest(void) {              /* 가장 오래된 칸 1 개 → DRAM (마스크 바이트만) */
    wbe_t *e = &g.wbuf[0];
    unsigned n = 0;
    for (uint32_t i = 0; i < g.cfg.B; i++)
        if (e->mask >> i & 1u) { g_dram[e->line + i] = e->data[i]; n++; }
    g.st.wr_bytes += n; g.st.wr_txn++;
    memmove(&g.wbuf[0], &g.wbuf[1], (g.wn - 1u) * sizeof g.wbuf[0]);
    g.wn--;
}
static void wbuf_drain_all(void) { while (g.wn) wbuf_drain_oldest(); }    /* = DSB: 앞선 쓰기 완료 대기 */

/* ======================= Part A / B TODO ======================= */
static void evict(uint32_t s, unsigned w);
static void wbuf_push(uint32_t a, uint32_t size, const uint8_t *data);
static void wbuf_drain_line(uint32_t line);

static void mem_store(uint32_t a, uint32_t size, const uint8_t *data) {  /* 메모리로 가야 하는 store */
    if (g.cfg.K == 0) { g.st.stalls++; dram_write(a, data, size); }       /* 버퍼 없음: CPU 가 기다림 */
    else wbuf_push(a, size, data);
}
static unsigned allocate(uint32_t a) {             /* 미스 → 교체 → 채움. 웨이 번호 반환     */
    uint32_t s = set_of(a), l = line_of(a);
    unsigned w = victim(s);
    evict(s, w);                                   /* A1                                    */
    wbuf_drain_line(l);                            /* B2: 채우기 전에 같은 라인의 버퍼 쓰기를 내보냄 */
    memcpy(g.set[s][w].data, &g_dram[l], g.cfg.B);
    g.st.rd_bytes += g.cfg.B; g.st.rd_txn++;
    g.set[s][w].valid = true; g.set[s][w].dirty = false; g.set[s][w].line = l;
    return w;
}
static void cache_read(uint32_t a, uint32_t size, uint8_t *out) {
    int w = lookup(a);
    if (w >= 0) g.st.hits++;
    else { g.st.misses++; w = (int)allocate(a); }
    cline_t *L = &g.set[set_of(a)][w];
    L->lru = ++g.clock;
    memcpy(out, &L->data[a - L->line], size);
}

/* TODO A1: 교체될 라인 (s, w) 를 비운다. 유효하고 더티면 라인 전체(B 바이트)를 dram_write 로
 *          write-back 하고 evict_bytes 에도 더한다. 끝나면 valid = dirty = false. */
static void evict(uint32_t s, unsigned w) {
    (void)s; (void)w;                              /* TODO A1 */
}

/* TODO A2: store 1 개. hits/misses 를 세고 pol_wb(a)/pol_wa(a) 로 4 조합을 처리한다.
 *   적중          : 라인 데이터 갱신, LRU 갱신. WB → dirty, WT → mem_store 도.
 *   미스 + NWA    : 캐시는 건드리지 않고 mem_store 만.
 *   미스 + WA     : allocate(a) 로 라인을 채운 뒤 적중과 똑같이. */
static void cache_write(uint32_t a, uint32_t size, const uint8_t *data) {
    (void)a; (void)size; (void)data;               /* TODO A2 */
    (void)pol_wb; (void)pol_wa; (void)mem_store;   /* 쓸 도우미들 (경고 제거용 — 지워도 됨) */
}

/* TODO A3: 실행 끝 flush. 유효 + 더티 라인을 전부 dram_raw_write 로 내리고 flush_bytes += B,
 *          dirty = false (라인은 남는다). wr_bytes/wr_txn 은 건드리지 않는다. */
static void cache_flush(void) {
                                                   /* TODO A3 */
}

/* TODO B1: 메모리로 가는 store 를 버퍼에 넣는다.
 *   coalesce 가 켜져 있고 같은 라인 칸이 있으면 그 칸에 합친다 (데이터 덮고 마스크 OR, coalesced++).
 *   아니면 새 칸: 버퍼가 꽉 찼으면(wn == K) 먼저 stalls++ 하고 wbuf_drain_oldest().
 *   칸 = {line, mask(라인 안 바이트 비트), data}. 새 칸은 맨 뒤(가장 젊은 자리). */
static void wbuf_push(uint32_t a, uint32_t size, const uint8_t *data) {
    (void)a; (void)size; (void)data;               /* TODO B1 */
}

/* TODO B2: 라인 l 을 DRAM 에서 채우기 직전에 호출된다. 버퍼에 l 칸이 하나라도 있으면
 *          가장 오래된 칸부터 순서대로(wbuf_drain_oldest) 배출해서 l 칸이 없어질 때까지.
 *          배출한 칸마다 raw_drains++. (순서를 건너뛰고 l 칸만 빼면 다른 라인 쓰기와의 순서가 바뀐다) */
static void wbuf_drain_line(uint32_t l) {
    (void)l;                                       /* TODO B2 */
}

/* ======================= Part C TODO ======================= */
/* TODO C1: [base, base+len) 에 걸친 모든 라인에 대해 (부분 라인 포함, len 0 이면 아무것도 안 함)
 *          cmo_lines++, 캐시에 있고 더티면 dram_write(B 바이트) + clean_bytes += B, dirty = false
 *          (라인은 남는다). 마지막에 wbuf_drain_all() (= DSB). */
static void clean_range(uint32_t base, uint32_t len) {
    (void)base; (void)len;                         /* TODO C1 */
}

/* TODO C2: 로그 링 [rb, rb+rsize) 에서 누적 바이트 위치 from..to 사이에 새로 쓴 부분을
 *          DMA 가 읽을 수 있게 만든다. 위치 → 링 오프셋은 % rsize. to - from > rsize 면 링 전체.
 *          링 끝을 넘으면 두 조각. 조각마다: WT 영역(in_wt)이면 wbuf_drain_all() 만,
 *          아니면 clean_range(조각). to == from 이면 아무것도 안 함. */
static void log_publish(uint32_t rb, uint32_t rsize, uint32_t from, uint32_t to) {
    (void)rb; (void)rsize; (void)from; (void)to;   /* TODO C2 */
}

/* ======================= 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint8_t store_byte(uint64_t seq, uint32_t a) { return (uint8_t)(seq * 29u + a * 7u + 3u); }

static void sim_access(const trace_t *t) {         /* 레코드 1 개 = 1 사이클                  */
    uint8_t buf[MAXB];
    uint64_t seq = g.st.accesses++;
    if (t->rw == 'R') {
        g.st.reads++;
        cache_read(t->addr, t->size, buf);
        if (memcmp(buf, &g_gold[t->addr], t->size) != 0) g.st.read_mismatch++;
    } else {
        g.st.writes++;
        for (uint32_t i = 0; i < t->size; i++) buf[i] = store_byte(seq, t->addr + i);
        memcpy(&g_gold[t->addr], buf, t->size);
        cache_write(t->addr, t->size, buf);
    }
    g.cycle++;
    if (g.cfg.drain_period && g.cycle % g.cfg.drain_period == 0 && g.wn) wbuf_drain_oldest();
}
static void sim_finish(void) {                     /* 버퍼 배출(실행 중 쓰기) → flush(따로)   */
    wbuf_drain_all();
    cache_flush();
    g.st.final_mismatch = 0;
    for (uint32_t i = 0; i < DRAM_SIZE; i++) g.st.final_mismatch += g_dram[i] != g_gold[i];
}
static stats_t run(const cfg_t *c, const trace_t *t, size_t n) {
    if (!sim_init(c)) { stats_t bad; memset(&bad, 0xff, sizeof bad); return bad; }
    for (size_t i = 0; i < n; i++) sim_access(&t[i]);
    sim_finish();
    return g.st;
}
static uint32_t stale_bytes(uint32_t base, uint32_t len) {   /* DMA 가 지금 RAM 에서 읽으면 틀리는 바이트 */
    uint32_t n = 0;
    for (uint32_t i = 0; i < len; i++) n += g_dram[base + i] != g_gold[base + i];
    return n;
}

/* ---- 트레이스 ---- */
static trace_t  T[16384];
static size_t   Tn;
static uint32_t g_rng = 0x2545F491u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static void put(uint32_t a, char rw, uint8_t sz) { T[Tn].addr = a; T[Tn].rw = rw; T[Tn].size = sz; Tn++; }

#define MEMSET_BASE 0x10000u
#define TABLE_BASE  0x20000u
#define LOG_BASE    0x30000u
#define LOG_RING    1024u
#define HIST_BASE   0x08000u

static void gen_memset(uint32_t base, uint32_t n, uint8_t sz) {     /* memset: sz 바이트 store 연속 */
    Tn = 0;
    for (uint32_t a = base; a < base + n; a += sz) put(a, 'W', sz);
}
static void gen_log_record(uint32_t *pos) {         /* 16 B 레코드 = 4 B store 4 개 + 앱 읽기 8 개 */
    for (uint32_t k = 0; k < 16; k += 4) put(LOG_BASE + (*pos + k) % LOG_RING, 'W', 4);
    *pos += 16;
    for (int k = 0; k < 8; k++) put(TABLE_BASE + (xs32() % 4096u) * 4u, 'R', 4);   /* 16 KB 표 */
}
static void gen_log(unsigned records) {
    Tn = 0; g_rng = 0x2545F491u;
    uint32_t pos = 0;
    for (unsigned r = 0; r < records; r++) gen_log_record(&pos);
}
static void gen_rmw(unsigned words) {               /* 히스토그램: 입력 읽기 → hist[idx]++ (R+W) */
    Tn = 0; g_rng = 0x9E3779B9u;
    for (unsigned i = 0; i < words; i++) {
        put(TABLE_BASE + i * 4u, 'R', 4);
        uint32_t h = HIST_BASE + (xs32() % 256u) * 4u;
        put(h, 'R', 4);
        put(h, 'W', 4);
    }
}

static const char *PNAME[4] = { "WB+WA ", "WB+NWA", "WT+WA ", "WT+NWA" };
static cfg_t mk(uint32_t C, uint32_t B, uint32_t W, int p, unsigned K, bool co, unsigned dp) {
    cfg_t c = { C, B, W, p < 2, (p & 1) == 0, K, co, dp, 0, 0 };
    return c;
}

/* ---------- Part A: 4 조합 손 계산 트레이스 (C=64, B=16, W=2 → 세트 2 개) ---------- */
static void test_part_a(void) {
    static const trace_t t1[] = { {0x00,'W',4}, {0x04,'W',4}, {0x08,'R',4},
                                  {0x40,'W',4}, {0x80,'W',4}, {0x00,'R',4} };
    /*                      rd   wr  wrtxn flush hits miss */
    static const unsigned exp1[4][6] = { { 64, 32, 2, 16, 2, 4 },     /* WB+WA  */
                                         { 16, 16, 4,  0, 1, 5 },     /* WB+NWA */
                                         { 64, 16, 4,  0, 2, 4 },     /* WT+WA  */
                                         { 16, 16, 4,  0, 1, 5 } };   /* WT+NWA */
    for (int p = 0; p < 4; p++) {
        cfg_t c = mk(64, 16, 2, p, 0, false, 0);
        stats_t s = run(&c, t1, 6);
        CHECK(s.rd_bytes == exp1[p][0] && s.wr_bytes == exp1[p][1] && s.wr_txn == exp1[p][2] &&
              s.flush_bytes == exp1[p][3] && s.hits == exp1[p][4] && s.misses == exp1[p][5] &&
              s.read_mismatch == 0 && s.final_mismatch == 0);
    }
    trace_t t2[20];                                 /* 카운터 RMW 10 번: R 0x00, W 0x00     */
    for (int i = 0; i < 20; i++) { t2[i].addr = 0; t2[i].rw = (i & 1) ? 'W' : 'R'; t2[i].size = 4; }
    for (int p = 0; p < 4; p++) {
        cfg_t c = mk(64, 16, 2, p, 0, false, 0);
        stats_t s = run(&c, t2, 20);
        if (p < 2) CHECK(s.rd_bytes == 16 && s.wr_bytes == 0 && s.flush_bytes == 16 && s.final_mismatch == 0);
        else       CHECK(s.rd_bytes == 16 && s.wr_bytes == 40 && s.wr_txn == 10 && s.flush_bytes == 0);
    }
    {   /* WT 에서는 더티 라인이 절대 생기지 않는다: 교체 write-back 0, flush 0 */
        gen_rmw(600);
        cfg_t c = mk(512, 16, 2, 3, 0, false, 0); c.wa = true;   /* WT+WA */
        stats_t s = run(&c, T, Tn);
        CHECK(s.evict_bytes == 0 && s.flush_bytes == 0 && s.wr_bytes == 600u * 4u && s.final_mismatch == 0);
    }
}

/* ---------- Part B: 쓰기 버퍼 ---------- */
static stats_t fuzz_one(int p, unsigned K, bool co, unsigned dp, uint32_t B, bool region, uint32_t seed) {
    trace_t f[3000];
    uint32_t r = seed;
    for (int i = 0; i < 3000; i++) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        uint8_t sz = (uint8_t)(1u << (r >> 30));                /* 1,2,4,8 */
        uint32_t a = 0x1000u + ((r >> 4) % 0x2000u);            /* 8 KB 공간, 캐시 512 B */
        a &= ~(uint32_t)(sz - 1u);                              /* 자연 정렬 → 라인 안 */
        f[i].addr = a; f[i].rw = (r & 3u) == 0 ? 'R' : ((r & 3u) == 1 ? 'R' : 'W'); f[i].size = sz;
    }
    cfg_t c = mk(512, B, 2, p, K, co, dp);
    if (region) { c.wt_base = 0x1800; c.wt_limit = 0x2000; }
    return run(&c, f, 3000);
}
static void test_part_b(void) {
    trace_t t[10];
    /* (1) 합치기: 한 라인(32 B)에 4 B store 8 개, WT+NWA, K=4, 배경 배출 없음 */
    for (int i = 0; i < 8; i++) { t[i].addr = 0x100u + 4u * (uint32_t)i; t[i].rw = 'W'; t[i].size = 4; }
    cfg_t c = mk(256, 32, 2, 3, 4, true, 0);
    stats_t s = run(&c, t, 8);
    CHECK(s.wr_txn == 1 && s.wr_bytes == 32 && s.coalesced == 7 && s.stalls == 0 && s.final_mismatch == 0);
    c.coalesce = false;
    s = run(&c, t, 8);
    CHECK(s.wr_txn == 8 && s.wr_bytes == 32 && s.stalls == 4 && s.final_mismatch == 0);
    /* (2) 같은 워드를 10 번: 합치기는 바이트까지 줄인다 (마지막 값만 나감) */
    for (int i = 0; i < 10; i++) { t[i].addr = 0x200; t[i].rw = 'W'; t[i].size = 4; }
    c.coalesce = true;
    s = run(&c, t, 10);
    CHECK(s.wr_txn == 1 && s.wr_bytes == 4 && s.coalesced == 9 && s.final_mismatch == 0);
    /* (3) RAW: 버퍼에 있는 store 를 같은 라인 읽기 미스가 지나치면 옛 값을 채운다 */
    trace_t raw[3] = { {0x300,'W',4}, {0x308,'W',2}, {0x300,'R',8} };
    c = mk(256, 32, 2, 3, 4, true, 0);
    s = run(&c, raw, 3);
    CHECK(s.read_mismatch == 0 && s.raw_drains == 1 && s.wr_txn == 1 && s.rd_txn == 1);
    /* (4) 배출 속도: 1 KB memset (8 B store), DRAM 은 4 사이클에 1 칸. 합치기 on/off */
    gen_memset(0x400, 1024, 8);
    c = mk(256, 32, 2, 3, 4, true, 4);
    s = run(&c, T, Tn);
    CHECK(s.stalls == 0 && s.wr_txn == 32 && s.wr_bytes == 1024 && s.final_mismatch == 0);
    c.coalesce = false;
    s = run(&c, T, Tn);
    CHECK(s.wr_txn == 128 && s.stalls == 93 && s.final_mismatch == 0);
    /* (5) 결정적 퍼즈: 4 정책 x K{0,1,4} x 합치기 x 배출 x 라인{16,32} x 영역 → 데이터가 늘 골든과 같다 */
    for (int p = 0; p < 4; p++) {
        uint64_t bad = 0;
        unsigned runs = 0;
        for (unsigned K = 0; K <= 4; K += (K == 0 ? 1u : 3u))
            for (int co = 0; co < 2; co++)
                for (unsigned dp = 0; dp <= 3; dp += 3)
                    for (uint32_t B = 16; B <= 32; B *= 2)
                        for (int rg = 0; rg < 2; rg++) {
                            stats_t f = fuzz_one(p, K, co, dp, B, rg, 0x1234567u + runs * 977u);
                            bad += f.read_mismatch + f.final_mismatch;
                            runs++;
                        }
        CHECK(bad == 0 && runs == 48);
    }
}

/* ---------- Part C: DMA 가 읽는 로그 링 ---------- */
typedef struct { stats_t s; uint32_t stale, publishes; } logres_t;
static logres_t log_scenario(bool wt_region, unsigned every, unsigned records) {
    cfg_t c = mk(4096, 32, 4, 0, 4, true, 4);       /* 기본 WB+WA, 버퍼 4 칸 합치기 */
    if (wt_region) { c.wt_base = LOG_BASE; c.wt_limit = LOG_BASE + LOG_RING; }
    logres_t r = { {0}, 0, 0 };
    if (!sim_init(&c)) return r;
    g_rng = 0x2545F491u;
    uint32_t pos = 0, published = 0;
    for (unsigned rec = 0; rec < records; rec++) {
        Tn = 0;
        gen_log_record(&pos);
        for (size_t i = 0; i < Tn; i++) sim_access(&T[i]);
        if ((rec + 1) % every == 0) {
            log_publish(LOG_BASE, LOG_RING, published, pos);
            uint32_t len = pos - published > LOG_RING ? LOG_RING : pos - published;
            uint32_t st = published % LOG_RING;          /* DMA 가 지금 읽을 범위만 검사 */
            if (pos - published > LOG_RING) st = 0;
            for (uint32_t k = 0; k < len; k++) {
                uint32_t a = LOG_BASE + (st + k) % LOG_RING;
                r.stale += g_dram[a] != g_gold[a];
            }
            published = pos;
            r.publishes++;
        }
    }
    sim_finish();
    r.s = g.st;
    return r;
}
static void test_part_c(void) {
    cfg_t c = mk(4096, 32, 4, 0, 4, true, 0);
    sim_init(&c);
    uint8_t v[4] = { 1, 2, 3, 4 };
    cache_write(0x500, 4, v); memcpy(&g_gold[0x500], v, 4);
    cache_write(0x51C, 4, v); memcpy(&g_gold[0x51C], v, 4);
    cache_write(0x520, 4, v); memcpy(&g_gold[0x520], v, 4);
    CHECK(stale_bytes(0x500, 0x40) == 12);
    clean_range(0x51F, 2);                          /* 0x51F..0x520: 두 라인 */
    CHECK(g.st.cmo_lines == 2 && g.st.clean_bytes == 64 && stale_bytes(0x500, 0x40) == 0);
    clean_range(0x500, 0);
    CHECK(g.st.cmo_lines == 2);
    {   /* WT 영역의 store 는 캐시가 아니라 쓰기 버퍼에 있다 → clean_range 끝의 DSB 가 내보내야 한다 */
        cfg_t w = c; w.wt_base = 0x600; w.wt_limit = 0x700;
        sim_init(&w);
        cache_write(0x640, 4, v); memcpy(&g_gold[0x640], v, 4);
        CHECK(g.wn == 1 && stale_bytes(0x640, 4) == 4);
        clean_range(0x640, 4);
        CHECK(stale_bytes(0x640, 4) == 0 && g.st.clean_bytes == 0 && g.st.cmo_lines == 1);
    }
    /* 링 wrap: 위치 1000 → 1040 은 [1000,1024) + [0,16) 두 조각 */
    sim_init(&c);
    for (uint32_t k = 1000; k < 1040; k += 4) {
        uint32_t a = LOG_BASE + k % LOG_RING;
        cache_write(a, 4, v); memcpy(&g_gold[a], v, 4);
    }
    log_publish(LOG_BASE, LOG_RING, 1000, 1040);
    CHECK(stale_bytes(LOG_BASE, LOG_RING) == 0 && g.st.cmo_lines == 2);
    log_publish(LOG_BASE, LOG_RING, 1040, 1040);
    CHECK(g.st.cmo_lines == 2);
    log_publish(LOG_BASE, LOG_RING, 0, 5000);       /* 링보다 많이 밀림 → 링 전체 (32 라인) */
    CHECK(g.st.cmo_lines == 2 + 32);

    printf("\nlog ring 1 KB, 512 records x 16 B, DMA reads after each publish   (C=4K B=32 W=4, K=4 coalesce, drain/4)\n");
    printf("  region      every  stale  rd_B   wr_B  wr_txn clean_B evict_B flush_B cmo_lines\n");
    unsigned every_list[3] = { 1, 4, 16 };
    logres_t res[2][3];
    for (int wt = 0; wt < 2; wt++)
        for (int k = 0; k < 3; k++) {
            logres_t r = log_scenario(wt, every_list[k], 512);
            res[wt][k] = r;
            printf("  %-10s %5u %6u %5llu %6llu %7llu %7llu %7llu %7llu %9llu\n", wt ? "WT region" : "WB+WA",
                   every_list[k], r.stale, (unsigned long long)r.s.rd_bytes, (unsigned long long)r.s.wr_bytes,
                   (unsigned long long)r.s.wr_txn, (unsigned long long)r.s.clean_bytes,
                   (unsigned long long)r.s.evict_bytes, (unsigned long long)r.s.flush_bytes,
                   (unsigned long long)r.s.cmo_lines);
        }
    for (int wt = 0; wt < 2; wt++)
        for (int k = 0; k < 3; k++)
            CHECK(res[wt][k].stale == 0 && res[wt][k].s.final_mismatch == 0 && res[wt][k].s.read_mismatch == 0);
    CHECK(res[1][0].s.cmo_lines == 0 && res[1][0].s.clean_bytes == 0);      /* WT 영역은 clean 0 번 */
    CHECK(res[0][0].s.clean_bytes == 2u * 512u * 16u);                       /* 매 레코드 publish: 라인당 2 번 */
}

/* ---------- 비교 표: 3 트레이스 x 4 조합 ---------- */
static void table(void) {
    printf("\nC=4096 B=32 W=4 LRU, write buffer K=4 coalescing, drain 1 entry / 4 accesses\n");
    printf("  trace         policy   rd_B    wr_B(run) flush_B  total_wr  wr_txn  stalls  hit%%\n");
    const char *tn[3] = { "memset 64K", "log 512x16B", "hist RMW" };
    stats_t r[3][4];
    for (int k = 0; k < 3; k++) {
        if (k == 0) gen_memset(MEMSET_BASE, 65536, 8);
        if (k == 1) gen_log(512);
        if (k == 2) gen_rmw(4096);
        for (int p = 0; p < 4; p++) {
            cfg_t c = mk(4096, 32, 4, p, 4, true, 4);
            stats_t s = run(&c, T, Tn);
            r[k][p] = s;
            printf("  %-12s  %s %7llu %9llu %8llu %9llu %7llu %7llu  %5.1f\n", tn[k], PNAME[p],
                   (unsigned long long)s.rd_bytes, (unsigned long long)s.wr_bytes,
                   (unsigned long long)s.flush_bytes, (unsigned long long)(s.wr_bytes + s.flush_bytes),
                   (unsigned long long)s.wr_txn, (unsigned long long)s.stalls,
                   s.accesses ? 100.0 * (double)s.hits / (double)s.accesses : 0.0);
        }
    }
    CHECK(r[0][0].rd_bytes == 65536 && r[0][0].wr_bytes + r[0][0].flush_bytes == 65536);   /* RFO */
    CHECK(r[0][1].rd_bytes == 0 && r[0][3].rd_bytes == 0 && r[0][3].wr_txn == 2048);
    CHECK(r[0][2].rd_bytes == 65536 && r[0][2].wr_bytes == 65536);
    CHECK(r[2][0].wr_bytes == 0 && r[2][0].flush_bytes == 1024 &&                         /* hist 1 KB */
          r[2][3].wr_bytes >= 15u * 1024u && r[2][3].wr_bytes <= 4096u * 4u);
    int ok = 1;
    for (int k = 0; k < 3; k++) for (int p = 0; p < 4; p++) ok &= r[k][p].final_mismatch == 0 && r[k][p].read_mismatch == 0;
    CHECK(ok);
}

int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    table();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
