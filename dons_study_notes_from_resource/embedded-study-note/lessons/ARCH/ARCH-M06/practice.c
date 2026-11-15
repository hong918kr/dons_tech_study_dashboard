/* ARCH-M06 추가 연습 — 한 번에 검증
 *
 * solution.c 의 모델(WB/WT x WA/NWA + 쓰기 버퍼 + 데이터 골든)을 그대로 넣고 확장 세 가지를 더했다:
 *   연습 1  DC ZVA 같은 "라인 0 채우기" 레코드 'Z' — 할당은 하되 DRAM 에서 읽지 않는다 (RFO 없음)
 *   연습 2  쓰기 스트리밍 감지기 — 연속 라인 write miss 가 T 번 넘으면 그 뒤로는 할당 안 함
 *   연습 3  섹터 dirty 비트 (라인당 4 개) — write-back 은 더티 섹터 바이트만
 *   연습 4  memset 트래픽 닫힌 식 vs 시뮬레이터 (K = 0)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXS      256u
#define MAXW      16u
#define MAXB      64u
#define MAXK      16u
#define DRAM_SIZE 0x40000u

typedef struct { uint32_t addr; char rw; uint8_t size; } trace_t;   /* rw: 'R' 'W' 'Z'(연습 1) */
typedef struct {
    uint32_t C, B, W; bool wb, wa; unsigned K; bool coalesce; unsigned drain_period;
    uint32_t wt_base, wt_limit;
    unsigned stream_T;             /* 연습 2: 0 = 감지기 끔                                    */
    bool     sector;               /* 연습 3: 섹터 dirty                                        */
} cfg_t;
typedef struct {
    uint64_t accesses, reads, writes, hits, misses, rd_bytes, rd_txn, wr_bytes, wr_txn;
    uint64_t evict_bytes, clean_bytes, flush_bytes, stalls, coalesced, raw_drains, cmo_lines;
    uint64_t read_mismatch, final_mismatch, stream_noalloc;
} stats_t;
typedef struct { bool valid; uint8_t dmask; uint32_t line; uint64_t lru; uint8_t data[MAXB]; } cline_t;
typedef struct { uint32_t line; uint64_t mask; uint8_t data[MAXB]; } wbe_t;

static struct {
    cfg_t cfg; uint32_t S, obits;
    cline_t set[MAXS][MAXW];
    wbe_t wbuf[MAXK]; unsigned wn;
    uint64_t clock, cycle;
    uint32_t last_wmiss; unsigned stream_cnt;      /* 연습 2 상태 */
    stats_t st;
} g;
static uint8_t g_dram[DRAM_SIZE], g_gold[DRAM_SIZE];

static bool     is_pow2(uint32_t x) { return x != 0 && (x & (x - 1u)) == 0; }
static unsigned log2u(uint32_t x)   { unsigned n = 0; while (x > 1u) { x >>= 1; n++; } return n; }
static bool sim_init(const cfg_t *c) {
    if (!is_pow2(c->C) || !is_pow2(c->B) || !is_pow2(c->W)) return false;
    if (c->B > MAXB || c->B < 4u || c->W > MAXW || c->K > MAXK || c->C < c->B * c->W) return false;
    if (c->C / (c->B * c->W) > MAXS) return false;
    if (((c->wt_base | c->wt_limit) & (c->B - 1u)) != 0 || c->wt_limit < c->wt_base) return false;
    memset(&g, 0, sizeof g);
    g.cfg = *c; g.S = c->C / (c->B * c->W); g.obits = log2u(c->B); g.last_wmiss = 0xFFFFFFFFu;
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
    for (unsigned w = 0; w < g.cfg.W; w++) if (g.set[s][w].valid && g.set[s][w].line == l) return (int)w;
    return -1;
}
static unsigned victim(uint32_t s) {
    unsigned v = 0;
    for (unsigned w = 0; w < g.cfg.W; w++) {
        if (!g.set[s][w].valid) return w;
        if (g.set[s][w].lru < g.set[s][v].lru) v = w;
    }
    return v;
}
static void wbuf_drain_oldest(void) {
    wbe_t *e = &g.wbuf[0]; unsigned n = 0;
    for (uint32_t i = 0; i < g.cfg.B; i++) if (e->mask >> i & 1u) { g_dram[e->line + i] = e->data[i]; n++; }
    g.st.wr_bytes += n; g.st.wr_txn++;
    memmove(&g.wbuf[0], &g.wbuf[1], (g.wn - 1u) * sizeof g.wbuf[0]); g.wn--;
}
static void wbuf_drain_all(void) { while (g.wn) wbuf_drain_oldest(); }
static void wbuf_push(uint32_t a, uint32_t size, const uint8_t *data) {
    uint32_t l = line_of(a), off = a - l; wbe_t *e = NULL;
    if (g.cfg.coalesce)
        for (unsigned i = 0; i < g.wn; i++) if (g.wbuf[i].line == l) { e = &g.wbuf[i]; g.st.coalesced++; break; }
    if (e == NULL) {
        if (g.wn == g.cfg.K) { g.st.stalls++; wbuf_drain_oldest(); }
        e = &g.wbuf[g.wn++]; e->line = l; e->mask = 0;
    }
    for (uint32_t i = 0; i < size; i++) { e->data[off + i] = data[i]; e->mask |= 1ull << (off + i); }
}
static void wbuf_drain_line(uint32_t l) {
    for (;;) {
        bool pending = false;
        for (unsigned i = 0; i < g.wn; i++) if (g.wbuf[i].line == l) { pending = true; break; }
        if (!pending) return;
        wbuf_drain_oldest(); g.st.raw_drains++;
    }
}
static void mem_store(uint32_t a, uint32_t size, const uint8_t *data) {
    if (g.cfg.K == 0) { g.st.stalls++; memcpy(&g_dram[a], data, size); g.st.wr_bytes += size; g.st.wr_txn++; }
    else wbuf_push(a, size, data);
}

/* ---- 연습 3: write-back 은 더티 섹터만 (sector=false 면 라인 전체). 반환 = 쓴 바이트 ---- */
static uint32_t write_back(cline_t *L, bool count_run) {
    uint32_t sec = g.cfg.B / 4u, n = 0;
    if (L->dmask == 0) return 0;
    for (unsigned k = 0; k < 4; k++)
        if (!g.cfg.sector || (L->dmask >> k & 1u)) { memcpy(&g_dram[L->line + k * sec], &L->data[k * sec], sec); n += sec; }
    if (count_run) { g.st.wr_bytes += n; g.st.wr_txn++; }
    L->dmask = 0;
    return n;
}
static void mark_dirty(cline_t *L, uint32_t off, uint32_t size) {
    uint32_t sec = g.cfg.B / 4u;
    for (uint32_t k = off / sec; k <= (off + size - 1u) / sec; k++) L->dmask |= (uint8_t)(1u << k);
}
static void evict(uint32_t s, unsigned w) {
    cline_t *L = &g.set[s][w];
    if (L->valid) g.st.evict_bytes += write_back(L, true);
    L->valid = false;
}
static unsigned allocate_nofill(uint32_t a) {
    uint32_t s = set_of(a); unsigned w = victim(s);
    evict(s, w);
    wbuf_drain_line(line_of(a));
    g.set[s][w].valid = true; g.set[s][w].dmask = 0; g.set[s][w].line = line_of(a);
    return w;
}
static unsigned allocate(uint32_t a) {
    unsigned w = allocate_nofill(a);
    memcpy(g.set[set_of(a)][w].data, &g_dram[line_of(a)], g.cfg.B);
    g.st.rd_bytes += g.cfg.B; g.st.rd_txn++;
    return w;
}
static void cache_read(uint32_t a, uint32_t size, uint8_t *out) {
    int w = lookup(a);
    if (w >= 0) g.st.hits++;
    else { g.st.misses++; g.stream_cnt = 0; w = (int)allocate(a); }      /* 연습 2: 읽기 미스 → 리셋 */
    cline_t *L = &g.set[set_of(a)][w];
    L->lru = ++g.clock;
    memcpy(out, &L->data[a - L->line], size);
}
/* 연습 2: 스트리밍 감지 — 같은 라인이면 유지, 바로 다음 라인이면 +1, 아니면 1 부터 */
static bool stream_says_no_alloc(uint32_t l) {
    if (g.cfg.stream_T == 0) return false;
    if (l == g.last_wmiss) { /* 같은 라인 */ }
    else if (g.last_wmiss != 0xFFFFFFFFu && l == g.last_wmiss + g.cfg.B) g.stream_cnt++;
    else g.stream_cnt = 1;
    g.last_wmiss = l;
    return g.stream_cnt > g.cfg.stream_T;
}
static void cache_write(uint32_t a, uint32_t size, const uint8_t *data) {
    int w = lookup(a);
    if (w >= 0) g.st.hits++;
    else {
        g.st.misses++;
        bool no_alloc = stream_says_no_alloc(line_of(a));
        if (!pol_wa(a) || no_alloc) { g.st.stream_noalloc += no_alloc; mem_store(a, size, data); return; }
        w = (int)allocate(a);
    }
    cline_t *L = &g.set[set_of(a)][w];
    L->lru = ++g.clock;
    memcpy(&L->data[a - L->line], data, size);
    if (pol_wb(a)) mark_dirty(L, a - L->line, size);
    else mem_store(a, size, data);
}
/* 연습 1: DC ZVA — 라인 전체를 0 으로. WB 면 읽지 않고 할당, WT 면 0 라인을 메모리로 */
static void cache_zero_line(uint32_t a) {
    uint8_t z[MAXB] = { 0 };
    uint32_t l = line_of(a);
    int w = lookup(l);
    if (pol_wb(l)) {
        if (w >= 0) g.st.hits++;
        else { g.st.misses++; w = (int)allocate_nofill(l); }
        cline_t *L = &g.set[set_of(l)][w];
        L->lru = ++g.clock;
        memset(L->data, 0, g.cfg.B);
        mark_dirty(L, 0, g.cfg.B);
    } else {
        if (w >= 0) { g.st.hits++; memset(g.set[set_of(l)][w].data, 0, g.cfg.B); }
        else g.st.misses++;
        mem_store(l, g.cfg.B, z);
    }
}
static void cache_flush(void) {
    for (uint32_t s = 0; s < g.S; s++)
        for (unsigned w = 0; w < g.cfg.W; w++)
            if (g.set[s][w].valid) g.st.flush_bytes += write_back(&g.set[s][w], false);
}
static void clean_range(uint32_t base, uint32_t len) {
    if (len == 0) return;
    for (uint32_t a = line_of(base); a < base + len; a += g.cfg.B) {
        g.st.cmo_lines++;
        int w = lookup(a);
        if (w >= 0) g.st.clean_bytes += write_back(&g.set[set_of(a)][w], true);
    }
    wbuf_drain_all();
}
static void log_publish(uint32_t rb, uint32_t rsize, uint32_t from, uint32_t to) {
    uint32_t len = to - from, start = from % rsize;
    if (len == 0) return;
    if (len > rsize) { start = 0; len = rsize; }
    uint32_t first = len < rsize - start ? len : rsize - start;
    uint32_t pb[2] = { rb + start, rb }, pl[2] = { first, len - first };
    for (unsigned k = 0; k < 2; k++) {
        if (pl[k] == 0) continue;
        if (in_wt(pb[k])) wbuf_drain_all(); else clean_range(pb[k], pl[k]);
    }
}

/* ======================= 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint8_t store_byte(uint64_t seq, uint32_t a) { return (uint8_t)(seq * 29u + a * 7u + 3u); }
static void sim_access(const trace_t *t) {
    uint8_t buf[MAXB];
    uint64_t seq = g.st.accesses++;
    if (t->rw == 'R') {
        g.st.reads++;
        cache_read(t->addr, t->size, buf);
        if (memcmp(buf, &g_gold[t->addr], t->size) != 0) g.st.read_mismatch++;
    } else if (t->rw == 'Z') {
        g.st.writes++;
        memset(&g_gold[line_of(t->addr)], 0, g.cfg.B);
        cache_zero_line(t->addr);
    } else {
        g.st.writes++;
        for (uint32_t i = 0; i < t->size; i++) buf[i] = store_byte(seq, t->addr + i);
        memcpy(&g_gold[t->addr], buf, t->size);
        cache_write(t->addr, t->size, buf);
    }
    g.cycle++;
    if (g.cfg.drain_period && g.cycle % g.cfg.drain_period == 0 && g.wn) wbuf_drain_oldest();
}
static void sim_finish(void) {
    wbuf_drain_all(); cache_flush();
    g.st.final_mismatch = 0;
    for (uint32_t i = 0; i < DRAM_SIZE; i++) g.st.final_mismatch += g_dram[i] != g_gold[i];
}
static stats_t run(const cfg_t *c, const trace_t *t, size_t n) {
    if (!sim_init(c)) { stats_t bad; memset(&bad, 0xff, sizeof bad); return bad; }
    for (size_t i = 0; i < n; i++) sim_access(&t[i]);
    sim_finish();
    return g.st;
}

static trace_t T[16384];
static size_t  Tn;
static uint32_t g_rng;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static void put(uint32_t a, char rw, uint8_t sz) { T[Tn].addr = a; T[Tn].rw = rw; T[Tn].size = sz; Tn++; }
static void gen_memset(uint32_t base, uint32_t n, uint8_t sz) { Tn = 0; for (uint32_t a = base; a < base + n; a += sz) put(a, 'W', sz); }
static cfg_t mk(uint32_t C, uint32_t B, uint32_t W, bool wb, bool wa, unsigned K, bool co, unsigned dp) {
    cfg_t c = { C, B, W, wb, wa, K, co, dp, 0, 0, 0, false };
    return c;
}

/* ---- 연습 1: DC ZVA memset ---- */
static void ex1(void) {
    cfg_t c = mk(4096, 32, 4, true, true, 4, true, 4);
    gen_memset(0x10000, 65536, 8);
    stats_t plain = run(&c, T, Tn);
    Tn = 0;
    for (uint32_t a = 0x10000; a < 0x20000; a += 32) put(a, 'Z', 32);
    stats_t zva = run(&c, T, Tn);
    printf("ex1 memset 64K WB+WA:  str x8  rd=%llu wr+flush=%llu   |  DC ZVA  rd=%llu wr+flush=%llu\n",
           (unsigned long long)plain.rd_bytes, (unsigned long long)(plain.wr_bytes + plain.flush_bytes),
           (unsigned long long)zva.rd_bytes, (unsigned long long)(zva.wr_bytes + zva.flush_bytes));
    CHECK(plain.rd_bytes == 65536 && zva.rd_bytes == 0);
    CHECK(zva.wr_bytes + zva.flush_bytes == 65536 && zva.final_mismatch == 0);
    /* 부분적으로 캐시에 있던 라인 + 읽기 섞기 + WT 영역: 데이터는 골든과 같아야 */
    Tn = 0;
    put(0x10010, 'W', 4); put(0x10000, 'Z', 32); put(0x10010, 'R', 4); put(0x30000, 'Z', 32); put(0x30004, 'R', 4);
    c.wt_base = 0x30000; c.wt_limit = 0x30400;
    stats_t mix = run(&c, T, Tn);
    CHECK(mix.read_mismatch == 0 && mix.final_mismatch == 0 && mix.rd_bytes == 32u * 2u);
}

/* ---- 연습 2: 스트리밍 감지기 ---- */
static void ex2(void) {
    cfg_t c = mk(4096, 32, 4, true, true, 4, true, 4);
    c.stream_T = 3;
    gen_memset(0x10000, 65536, 8);
    stats_t s = run(&c, T, Tn);
    printf("ex2 memset 64K WB+WA + stream(T=3): rd=%llu wr=%llu flush=%llu wr_txn=%llu noalloc_stores=%llu\n",
           (unsigned long long)s.rd_bytes, (unsigned long long)s.wr_bytes, (unsigned long long)s.flush_bytes,
           (unsigned long long)s.wr_txn, (unsigned long long)s.stream_noalloc);
    CHECK(s.rd_bytes == 3u * 32u && s.wr_bytes + s.flush_bytes == 65536 && s.final_mismatch == 0);
    /* 읽기가 끼면 감지기는 꺼진 채: RMW 히스토그램은 그대로 write-back 의 이득 */
    Tn = 0; g_rng = 0x9E3779B9u;
    for (unsigned i = 0; i < 2048; i++) { uint32_t h = 0x8000u + (xs32() % 256u) * 4u; put(0x20000u + i * 4u, 'R', 4); put(h, 'R', 4); put(h, 'W', 4); }
    stats_t on = run(&c, T, Tn);
    c.stream_T = 0;
    stats_t off = run(&c, T, Tn);
    CHECK(on.stream_noalloc == 0 && on.wr_bytes == off.wr_bytes && on.flush_bytes == off.flush_bytes && on.final_mismatch == 0);
    /* 퍼즈: 감지기 켜고 무작위 트레이스 → 데이터는 늘 맞다 */
    c.stream_T = 2;
    uint64_t bad = 0;
    for (unsigned k = 0; k < 6; k++) {
        Tn = 0; g_rng = 0xC0FFEEu + k;
        for (int i = 0; i < 3000; i++) {
            uint32_t r = xs32();
            uint32_t a = 0x1000u + (r >> 8) % 0x2000u;
            if (r & 1u) { uint32_t b = 0x1000u + ((uint32_t)i * 8u) % 0x2000u; put(b, 'W', 8); }   /* 순차 쓰기 */
            else put(a & ~3u, (r & 2u) ? 'R' : 'W', 4);
        }
        stats_t f = run(&c, T, Tn);
        bad += f.read_mismatch + f.final_mismatch;
    }
    CHECK(bad == 0);
}

/* ---- 연습 3: 섹터 dirty — 레코드마다 publish 하는 로그 ---- */
static uint64_t log_clean_bytes(bool sector, uint64_t *mismatch) {
    cfg_t c = mk(4096, 32, 4, true, true, 4, true, 4);
    c.sector = sector;
    sim_init(&c);
    g_rng = 0x2545F491u;
    uint32_t pos = 0;
    for (unsigned rec = 0; rec < 512; rec++) {
        Tn = 0;
        for (uint32_t k = 0; k < 16; k += 4) put(0x30000u + (pos + k) % 1024u, 'W', 4);
        for (int k = 0; k < 8; k++) put(0x20000u + (xs32() % 4096u) * 4u, 'R', 4);
        for (size_t i = 0; i < Tn; i++) sim_access(&T[i]);
        log_publish(0x30000, 1024, pos, pos + 16);
        pos += 16;
    }
    sim_finish();
    *mismatch = g.st.final_mismatch + g.st.read_mismatch;
    return g.st.clean_bytes;
}
static void ex3(void) {
    uint64_t m1, m2;
    uint64_t line = log_clean_bytes(false, &m1), sec = log_clean_bytes(true, &m2);
    printf("ex3 log publish every record: clean bytes  line-dirty=%llu  sector-dirty=%llu  (log data 8192)\n",
           (unsigned long long)line, (unsigned long long)sec);
    CHECK(line == 16384 && sec == 8192 && m1 == 0 && m2 == 0);
    /* 무작위 크기 쓰기 퍼즈에서도 섹터 write-back 이 데이터를 잃지 않는다 */
    cfg_t c = mk(512, 32, 2, true, true, 0, false, 0);
    c.sector = true;
    Tn = 0; g_rng = 0xABCDEFu;
    for (int i = 0; i < 4000; i++) {
        uint32_t r = xs32(); uint8_t sz = (uint8_t)(1u << (r >> 30));
        put((0x1000u + (r >> 4) % 0x2000u) & ~(uint32_t)(sz - 1u), (r & 1u) ? 'W' : 'R', sz);
    }
    stats_t f = run(&c, T, Tn);
    CHECK(f.read_mismatch == 0 && f.final_mismatch == 0);
}

/* ---- 연습 4: memset 닫힌 식 (K = 0, 순차, N 은 B 의 배수) ---- */
typedef struct { uint64_t rd, wr_run, flush, wr_txn; } pred_t;
static pred_t predict_memset(uint32_t N, uint32_t s, bool wb, bool wa, uint32_t C, uint32_t B) {
    pred_t p = { 0, 0, 0, 0 };
    uint32_t lines = N / B, resident = (N < C ? N : C) / B;
    if (wa) p.rd = N;
    if (wb && wa) { p.wr_run = (uint64_t)(lines - resident) * B; p.flush = (uint64_t)resident * B; p.wr_txn = lines - resident; }
    else          { p.wr_run = N; p.wr_txn = N / s; }
    return p;
}
static void ex4(void) {
    uint32_t Ns[3] = { 512, 4096, 65536 }, ss[3] = { 4, 8, 16 };
    int ok = 1, n = 0;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) for (int p = 0; p < 4; p++) {
        bool wb = p < 2, wa = (p & 1) == 0;
        cfg_t c = mk(4096, 32, 4, wb, wa, 0, false, 0);
        gen_memset(0x10000, Ns[i], (uint8_t)ss[j]);
        stats_t s = run(&c, T, Tn);
        pred_t e = predict_memset(Ns[i], ss[j], wb, wa, 4096, 32);
        ok &= s.rd_bytes == e.rd && s.wr_bytes == e.wr_run && s.flush_bytes == e.flush && s.wr_txn == e.wr_txn;
        n++;
    }
    CHECK(ok && n == 36);
}

int main(void) {
    ex1();
    ex2();
    ex3();
    ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
