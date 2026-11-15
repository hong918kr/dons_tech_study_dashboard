/* DS-A02 추가 연습 — 한 번에 검증
 *   연습 1  우선순위가 다른 ISR 두 개가 같은 링에 넣으면 생산자가 둘이다 (강제 재현 + PRIMASK 구간으로 수정)
 *   연습 2  Cortex-M7 D-cache 모델: 쓰기 뒤 clean, 읽기 전 invalidate, 캐시 라인 정렬 안 하면 이웃 바이트가 사라진다
 *   연습 3  bip 위의 프레임 레코드: 레코드가 절대 쪼개지지 않고 페이로드가 4 바이트 정렬이라 포인터 그대로 넘긴다
 *   연습 4  공간 보장 정리: 전수 탐색으로 최소 용량을 찾으면 정확히 D + 2M
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

static uint32_t g_rng = 0x1BADB002u;
static uint32_t rnd(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; g_rng = x; return x; }

/* ======================= 연습 1 — ISR 우선순위 두 단계 ======================= */
/* 낮은 우선순위 ISR 이 put 하는 도중, k 번째 접근 앞에서 높은 우선순위 ISR 이 끼어들어 같은 링에 put 한다. */
typedef struct { uint32_t head, tail; uint8_t buf[8]; } ring_t;
static struct { uint32_t step, at; bool in_high, masked, pending; void (*high)(void); } g_p;
static void p_step(void) {
    if (g_p.in_high) return;
    uint32_t s = g_p.step++;
    if (s != g_p.at || !g_p.high) return;
    if (g_p.masked) { g_p.pending = true; return; }
    g_p.in_high = true; g_p.high(); g_p.in_high = false;
}
static ring_t g_r;
static void put_naive(ring_t *q, uint8_t v) {
    p_step(); uint32_t h = q->head;
    p_step(); q->buf[h & 7u] = v;
    p_step(); q->head = h + 1u;
}
static void put_locked(ring_t *q, uint8_t v) {            /* 낮은 우선순위 쪽만 BASEPRI/PRIMASK 로 잠깐 막는다 */
    g_p.masked = true;
    put_naive(q, v);
    g_p.masked = false;
    if (g_p.pending) { g_p.pending = false; g_p.in_high = true; g_p.high(); g_p.in_high = false; }
}
static void high_isr(void) { put_naive(&g_r, 0xBB); }
static void run_ex1(void (*put)(ring_t *, uint8_t), uint32_t k, uint32_t *count, bool *has_a, bool *has_b) {
    memset(&g_r, 0, sizeof g_r);
    memset(&g_p, 0, sizeof g_p);
    g_p.at = k; g_p.high = high_isr;
    put(&g_r, 0xAA);
    *count = g_r.head - g_r.tail;
    *has_a = *has_b = false;
    for (uint32_t i = 0; i < *count; i++) { if (g_r.buf[i] == 0xAA) *has_a = true; if (g_r.buf[i] == 0xBB) *has_b = true; }
}
static void ex1(void) {
    printf("== 연습 1: 두 ISR 생산자\n");
    uint32_t naive_lost = 0, locked_lost = 0, n; bool a, b;
    for (uint32_t k = 0; k < 3u; k++) {
        run_ex1(put_naive, k, &n, &a, &b);
        printf("  naive k=%u: count %u, AA %d, BB %d\n", k, n, a, b);
        if (n != 2u || !a || !b) naive_lost++;
        run_ex1(put_locked, k, &n, &a, &b);
        if (n != 2u || !a || !b) locked_lost++;
    }
    CHECK(naive_lost == 2u /* head 를 읽은 뒤로 끼어드는 k = 1, 2 에서 한 바이트가 사라진다 */);
    CHECK(locked_lost == 0u);
}

/* ======================= 연습 2 — Cortex-M7 D-cache 모델 ======================= */
/* 쓰기 되돌림(write-back) 캐시. 캐시 줄 32 바이트. CPU 는 캐시를 통해서만, DMA 는 RAM 만 본다. */
#define LINE 32u
#define MEM_N (LINE * 8u)
static uint8_t g_ram[MEM_N], g_cache[MEM_N];
static bool g_valid[MEM_N / LINE], g_dirty[MEM_N / LINE];

static void cache_reset(void) { memset(g_ram, 0, MEM_N); memset(g_cache, 0, MEM_N); memset(g_valid, 0, sizeof g_valid); memset(g_dirty, 0, sizeof g_dirty); }
static void line_fill(uint32_t l) { if (!g_valid[l]) { memcpy(&g_cache[l * LINE], &g_ram[l * LINE], LINE); g_valid[l] = true; g_dirty[l] = false; } }
static void cpu_write(uint32_t a, uint8_t v) { uint32_t l = a / LINE; line_fill(l); g_cache[a] = v; g_dirty[l] = true; }
static uint8_t cpu_read(uint32_t a) { uint32_t l = a / LINE; line_fill(l); return g_cache[a]; }
static void dma_write(uint32_t a, uint8_t v) { g_ram[a] = v; }
static uint8_t dma_read(uint32_t a) { return g_ram[a]; }
static void cache_clean(uint32_t a, uint32_t n) {
    for (uint32_t l = a / LINE; l <= (a + n - 1u) / LINE; l++)
        if (g_valid[l] && g_dirty[l]) { memcpy(&g_ram[l * LINE], &g_cache[l * LINE], LINE); g_dirty[l] = false; }
}
static void cache_invalidate(uint32_t a, uint32_t n) {    /* 더러운 바이트가 있어도 그대로 버린다 (SCB_InvalidateDCache_by_Addr 처럼) */
    for (uint32_t l = a / LINE; l <= (a + n - 1u) / LINE; l++) { g_valid[l] = false; g_dirty[l] = false; }
}
/* 예약을 캐시 줄 단위로 넓힌다: 시작은 내림, 끝은 올림 */
static uint32_t line_floor(uint32_t a) { return a & ~(LINE - 1u); }
static uint32_t line_ceil(uint32_t a) { return (a + LINE - 1u) & ~(LINE - 1u); }

static void ex2(void) {
    printf("== 연습 2: D-cache 모델\n");
    /* (a) TX: CPU 가 쓰고 DMA 가 읽는다 */
    cache_reset();
    for (uint32_t i = 0; i < 16u; i++) cpu_write(32u + i, (uint8_t)(i + 1u));
    bool stale = true;
    for (uint32_t i = 0; i < 16u; i++) stale = stale && dma_read(32u + i) == 0u;
    CHECK(stale /* clean 전: DMA 는 0 을 읽는다 */);
    cache_clean(32u, 16u);
    bool ok = true;
    for (uint32_t i = 0; i < 16u; i++) ok = ok && dma_read(32u + i) == (uint8_t)(i + 1u);
    CHECK(ok /* clean 후: 정상 */);

    /* (b) RX: DMA 가 쓰고 CPU 가 읽는다 */
    cache_reset();
    (void)cpu_read(64u);                                 /* 예전에 읽어 둔 줄이 캐시에 있다 */
    for (uint32_t i = 0; i < 16u; i++) dma_write(64u + i, (uint8_t)(0x40u + i));
    CHECK(cpu_read(64u) == 0u /* invalidate 전: 낡은 캐시 값 */);
    cache_invalidate(64u, 16u);
    ok = true;
    for (uint32_t i = 0; i < 16u; i++) ok = ok && cpu_read(64u + i) == (uint8_t)(0x40u + i);
    CHECK(ok /* invalidate 후: 정상 */);

    /* (c) 정렬 안 된 RX 예약: 같은 줄의 이웃 바이트(방금 CPU 가 쓴 것)가 invalidate 로 사라진다 */
    cache_reset();
    cpu_write(70u, 0x77);                                /* 이웃 데이터: 줄 2 (64..95) */
    for (uint32_t i = 0; i < 10u; i++) dma_write(72u + i, 0xD0);
    cache_invalidate(72u, 10u);                          /* 72..81 을 무효화 → 줄 2 전체가 날아간다 */
    CHECK(cpu_read(70u) != 0x77u /* 이웃 바이트 소실 */);

    /* (d) 고침: 예약을 캐시 줄 단위로 맞추면 무효화 범위가 다른 예약과 겹치지 않는다 */
    cache_reset();
    cpu_write(70u, 0x77);
    uint32_t start = line_ceil(72u), end = start + line_ceil(10u);        /* 10 바이트 요청도 한 줄을 통째로 차지한다 */
    CHECK(start == 96u && end == 128u && line_floor(start) == start);
    for (uint32_t i = start; i < end; i++) dma_write(i, 0xD0);
    cache_invalidate(start, end - start);
    CHECK(cpu_read(70u) == 0x77u && cpu_read(start) == 0xD0u /* 이웃 안전, DMA 데이터도 정상 */);
}

/* ======================= 공용: 최소 bip (solution.c 와 같은 알고리즘, 계측 없음) ======================= */
typedef struct {
    uint32_t r, w, wm;
    uint8_t *buf; uint32_t cap;
    bool res_active, res_wrap; uint32_t res_start, res_len;
    bool rd_active; uint32_t rd_len;
} bip_t;
static void bip_init(bip_t *b, uint8_t *m, uint32_t cap) { memset(b, 0, sizeof *b); b->buf = m; b->cap = cap; }
static uint8_t *bip_reserve(bip_t *b, uint32_t n) {
    if (b->res_active || n == 0 || n > b->cap) return NULL;
    uint32_t w = b->w, r = b->r, start; bool wrap = false;
    if (w < r) { if (n >= r - w) return NULL; start = w; }
    else if (b->cap - w >= n) start = w;
    else if (n < r) { start = 0; wrap = true; }
    else return NULL;
    b->res_active = true; b->res_wrap = wrap; b->res_start = start; b->res_len = n;
    return b->buf + start;
}
static bool bip_commit(bip_t *b, uint32_t k) {
    if (!b->res_active || k > b->res_len) return false;
    if (k) { if (b->res_wrap) b->wm = b->w; b->w = b->res_start + k; }
    b->res_active = false; b->res_wrap = false; b->res_len = 0;
    return true;
}
static const uint8_t *bip_read(bip_t *b, uint32_t *n) {
    *n = 0;
    if (b->rd_active) return NULL;
    uint32_t r = b->r, w = b->w, limit = w;
    if (w < r) { if (r == b->wm) { r = 0; b->r = 0; } else limit = b->wm; }
    if (limit == r) return NULL;
    b->rd_active = true; b->rd_len = limit - r; *n = b->rd_len;
    return b->buf + r;
}
static bool bip_release(bip_t *b, uint32_t m) {
    if (!b->rd_active || m > b->rd_len) return false;
    b->rd_active = false; b->rd_len = 0;
    if (!m) return true;
    uint32_t nr = b->r + m;
    if (b->w < nr && nr == b->wm) nr = 0;
    b->r = nr;
    return true;
}

/* ======================= 연습 3 — 프레임 레코드 ======================= */
/* [hdr 4B: u16 total(헤더 포함, 4의 배수), u16 type][payload...]. bip 는 레코드를 쪼개지 않으므로 캐스팅이 안전하다. */
typedef struct { uint16_t total, type; } rec_hdr_t;
_Static_assert(sizeof(rec_hdr_t) == 4, "header is 4 bytes");
#define REC_ALIGN 4u
#define ALIGN_UP(x) (((x) + (REC_ALIGN - 1u)) & ~(REC_ALIGN - 1u))

static _Alignas(4) uint8_t g_rec_mem[256];
static bip_t g_rb;
static void *rec_reserve(bip_t *b, uint16_t type, uint32_t payload) {
    uint32_t total = ALIGN_UP((uint32_t)sizeof(rec_hdr_t) + payload);
    uint8_t *p = bip_reserve(b, total);
    if (!p) return NULL;
    rec_hdr_t *h = (rec_hdr_t *)(void *)p;
    h->total = (uint16_t)total; h->type = type;
    return p + sizeof(rec_hdr_t);
}
static bool rec_commit(bip_t *b, void *payload) {
    rec_hdr_t *h = (rec_hdr_t *)(void *)((uint8_t *)payload - sizeof(rec_hdr_t));
    return bip_commit(b, h->total);
}
/* 다음 레코드의 페이로드 포인터. *len 은 페이로드 공간, 호출자가 rec_free 로 반납 */
static const void *rec_peek(bip_t *b, uint16_t *type, uint32_t *len) {
    uint32_t n; const uint8_t *p = bip_read(b, &n);
    if (!p) return NULL;
    const rec_hdr_t *h = (const rec_hdr_t *)(const void *)p;
    *type = h->type; *len = h->total - (uint32_t)sizeof(rec_hdr_t);
    return p + sizeof(rec_hdr_t);
}
static bool rec_free(bip_t *b, const void *payload) {
    const rec_hdr_t *h = (const rec_hdr_t *)(const void *)((const uint8_t *)payload - sizeof(rec_hdr_t));
    return bip_release(b, h->total);
}
static void ex3(void) {
    printf("== 연습 3: 프레임 레코드\n");
    bip_init(&g_rb, g_rec_mem, sizeof g_rec_mem);
    /* 참조 FIFO: 넣은 레코드의 (type, payload_len) */
    static struct { uint16_t type; uint32_t len; } q[1024];
    uint32_t qh = 0, qt = 0, wraps = 0, bad = 0, unaligned = 0;
    for (uint32_t i = 0; i < 300000u; i++) {
        if (rnd() % 2u == 0u) {
            uint32_t pl = rnd() % 53u;
            uint16_t ty = (uint16_t)(i & 0xFFFFu);
            uint8_t *p = rec_reserve(&g_rb, ty, pl);
            if (!p) continue;
            if (g_rb.res_wrap) wraps++;
            if (((uintptr_t)p & 3u) != 0u) unaligned++;
            for (uint32_t j = 0; j < ALIGN_UP(4u + pl) - 4u; j++) p[j] = (uint8_t)(ty ^ j);
            rec_commit(&g_rb, p);
            q[qh++ % 1024u].type = ty; q[(qh - 1u) % 1024u].len = ALIGN_UP(4u + pl) - 4u;
        } else {
            uint16_t ty; uint32_t len;
            const uint8_t *p = rec_peek(&g_rb, &ty, &len);
            if (!p) { if (qh != qt) bad++; continue; }
            if (qh == qt || q[qt % 1024u].type != ty || q[qt % 1024u].len != len) bad++;
            for (uint32_t j = 0; j < len; j++) if (p[j] != (uint8_t)(ty ^ j)) { bad++; break; }
            rec_free(&g_rb, p);
            qt++;
        }
    }
    printf("  30만 연산: 랩 %u, 위반 %u, 정렬 어긋남 %u\n", wraps, bad, unaligned);
    CHECK(bad == 0u && unaligned == 0u);
    CHECK(wraps > 100u /* 경계를 많이 지났는데도 레코드가 쪼개지지 않았다 */);
    /* 맨 끝에 쓸모없는 틈: 예약 크기가 4 의 배수이고 cap 도 4 의 배수면 w, wm 모두 항상 4 의 배수 */
    bip_init(&g_rb, g_rec_mem, sizeof g_rec_mem);
    (void)bip_reserve(&g_rb, 16u); bip_commit(&g_rb, 16u);
    CHECK(g_rb.w == 16u && (g_rb.w & 3u) == 0u);
}

/* ======================= 연습 4 — 최소 용량 탐색 ======================= */
#define MC_MAX 16u
typedef struct { uint8_t r, w, wm; } st_t;
static st_t g_q[(MC_MAX + 1u) * (MC_MAX + 1u) * (MC_MAX + 1u)];
static bool g_seen[MC_MAX + 1u][MC_MAX + 1u][MC_MAX + 1u];
static uint32_t g_nq;
static uint32_t live_of(const bip_t *b) { return b->w < b->r ? (b->wm - b->r) + b->w : b->w - b->r; }
static void mc_add(const bip_t *b) {
    if (!g_seen[b->r][b->w][b->wm]) { g_seen[b->r][b->w][b->wm] = true; g_q[g_nq++] = (st_t){ (uint8_t)b->r, (uint8_t)b->w, (uint8_t)b->wm }; }
}
static void mc_load(bip_t *b, uint8_t *m, uint32_t cap, const st_t *s) { bip_init(b, m, cap); b->r = s->r; b->w = s->w; b->wm = s->wm; }
/* live <= D 일 때 크기 M 이하 reserve 만 하는 모든 실행에서 reserve(M) 이 언제나 성공하나 */
static bool cap_ok(uint32_t cap, uint32_t D, uint32_t M) {
    uint8_t m[MC_MAX]; bip_t b; uint32_t n;
    memset(g_seen, 0, sizeof g_seen);
    g_nq = 0;
    bip_init(&b, m, cap);
    mc_add(&b);
    for (uint32_t i = 0; i < g_nq; i++) {
        st_t s = g_q[i];
        mc_load(&b, m, cap, &s);
        uint32_t live = live_of(&b);
        if (live <= D && !bip_reserve(&b, M)) return false;
        for (uint32_t k = 1; k <= live; k++) {
            mc_load(&b, m, cap, &s);
            if (!bip_read(&b, &n) || k > n) continue;
            bip_release(&b, k);
            mc_add(&b);
        }
        if (live > D) continue;
        for (uint32_t res = 1; res <= M; res++) for (uint32_t c = 1; c <= res; c++) {
            mc_load(&b, m, cap, &s);
            if (!bip_reserve(&b, res)) continue;
            bip_commit(&b, c);
            mc_add(&b);
        }
    }
    return true;
}
static void ex4(void) {
    printf("== 연습 4: 최소 용량\n");
    uint32_t mismatches = 0, combos = 0;
    for (uint32_t D = 0; D <= 5u; D++) for (uint32_t M = 1; M <= 5u; M++) {
        uint32_t found = 0;
        for (uint32_t cap = M; cap <= MC_MAX; cap++) if (cap_ok(cap, D, M)) { found = cap; break; }
        if (found == 0u) continue;
        combos++;
        if (found != D + 2u * M) { mismatches++; printf("  D=%u M=%u: 최소 cap %u (예상 %u)\n", D, M, found, D + 2u * M); }
    }
    printf("  %u 조합 탐색, 공식과 다른 것 %u\n", combos, mismatches);
    CHECK(combos >= 25u && mismatches == 0u);
}

int main(void) {
    ex1();
    ex2();
    ex3();
    ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
