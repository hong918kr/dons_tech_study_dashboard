/* ARCH-M05 집합 연관 캐시와 교체 정책
 *
 *   Part A  N-way 집합 연관 캐시 + 교체 정책 LRU / FIFO / Random(고정 시드 xorshift32)
 *   Part B  tree-PLRU 비트 갱신/희생자 규칙, 3C 분류 (compulsory / capacity / conflict, Hill 정의)
 *   Part C  펌웨어 시나리오: 핫 셋 + 스트리밍 DMA 버퍼 → 정책별 핫 미스, MPU non-cacheable 창, 필요 연관도
 *
 * 주소 분해(tag/index/offset)와 직접 사상 조회는 ARCH-M04 모델을 축소해 넣었다 (#include 없음).
 * 쓰기는 write-allocate 로 보고 읽기와 똑같이 hit/miss 만 센다 (쓰기 정책·DRAM 바이트는 ARCH-M06).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================= 공통: 트레이스 · 기하 (ARCH-M04 축소) ======================= */
typedef struct { uint32_t addr; char rw; uint8_t size; } acc_t;   /* rw = 'R' / 'W' */

typedef enum { POL_LRU, POL_FIFO, POL_RANDOM, POL_PLRU, POL_N } policy_t;
static const char *const pol_name[POL_N] = {"LRU", "FIFO", "Random", "PLRU"};

typedef struct { uint32_t tag; uint8_t valid; uint64_t stamp; } way_t;

typedef struct {
    uint32_t C, B, W, S;         /* 용량, 라인, 연관도, 집합 수 = C / (B*W) */
    unsigned off_bits, idx_bits; /* log2 B, log2 S */
    policy_t pol;
    way_t *ways;                 /* S*W, 집합 s = ways[s*W .. s*W+W-1] */
    uint8_t *plru;               /* S*(W-1) 트리 노드 비트 (힙 순서: 루트 0, 자식 2i+1 / 2i+2) */
    uint64_t clock;              /* 접근마다 +1 → LRU/FIFO 의 시각 도장 */
    uint32_t rng;                /* Random 정책 xorshift32 상태 (init 때 RNG_SEED) */
    uint64_t accesses, hits, misses;
    int evicted;                 /* 마지막 접근이 유효 라인을 쫓아냈나 */
    uint32_t evicted_line;       /* 쫓겨난 라인 주소 (addr >> off_bits) */
} cache_t;

#define RNG_SEED 0x2545F491u

static int is_pow2(uint32_t x) { return x && !(x & (x - 1)); }
static unsigned log2u(uint32_t x) { unsigned n = 0; while (x >>= 1) n++; return n; }
static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* 2의 거듭제곱 · 4 <= B · W >= 1 · C >= B*W · C <= 1 GiB. 실패 -1. */
static int cache_init(cache_t *c, uint32_t C, uint32_t B, uint32_t W, policy_t pol) {
    memset(c, 0, sizeof *c);
    if (!is_pow2(C) || !is_pow2(B) || !is_pow2(W) || B < 4 || C > (1u << 30) ||
        (uint64_t)B * W > C || (unsigned)pol >= POL_N)
        return -1;
    c->C = C; c->B = B; c->W = W; c->S = C / (B * W);
    c->off_bits = log2u(B); c->idx_bits = log2u(c->S);
    c->pol = pol;
    c->rng = RNG_SEED;
    c->ways = calloc((size_t)c->S * W, sizeof *c->ways);
    c->plru = calloc((size_t)c->S * (W > 1 ? W - 1 : 1), 1);
    if (!c->ways || !c->plru) { free(c->ways); free(c->plru); return -1; }
    return 0;
}
static void cache_free(cache_t *c) { free(c->ways); free(c->plru); c->ways = NULL; c->plru = NULL; }
static uint32_t addr_set(const cache_t *c, uint32_t a) { return (a >> c->off_bits) & (c->S - 1); }
static uint32_t addr_tag(const cache_t *c, uint32_t a) { return a >> (c->off_bits + c->idx_bits); }

/* ======================= Part B (먼저 선언) — tree-PLRU ======================= */
/* 규칙: 노드 비트 0 = "다음 희생자는 왼쪽 서브트리", 1 = "오른쪽".
 * 접근(히트 또는 채움)한 way 의 경로 위 노드를 모두 그 way 의 "반대쪽" 을 가리키게 한다. W = 1 이면 비트 없음. */
static void plru_touch(uint8_t *bits, uint32_t W, uint32_t way) {
    uint32_t node = 0, lo = 0, size = W;
    while (size > 1) {
        uint32_t half = size / 2;
        if (way < lo + half) { bits[node] = 1; node = 2 * node + 1; }
        else                 { bits[node] = 0; node = 2 * node + 2; lo += half; }
        size = half;
    }
}
/* 루트부터 비트가 가리키는 쪽으로 내려간다. */
static uint32_t plru_victim(const uint8_t *bits, uint32_t W) {
    uint32_t node = 0, lo = 0, size = W;
    while (size > 1) {
        uint32_t half = size / 2;
        if (bits[node] == 0) node = 2 * node + 1;
        else               { node = 2 * node + 2; lo += half; }
        size = half;
    }
    return lo;
}

/* ======================= Part A — 교체 정책 ======================= */
/* 집합 set 의 PLRU 노드 비트 시작 */
#define SET_PLRU(c, set) (&(c)->plru[(size_t)(set) * ((c)->W > 1 ? (c)->W - 1 : 1)])

/* 히트 때 정책 상태 갱신. c->clock 은 이번 접근의 시각 (cache_access 가 이미 올렸다). */
static void policy_on_hit(cache_t *c, uint32_t set, uint32_t way) {
    way_t *L = &c->ways[(size_t)set * c->W];
    switch (c->pol) {
    case POL_LRU:  L[way].stamp = c->clock; break;            /* 최근 사용 시각 */
    case POL_FIFO: break;                                     /* 히트는 순서를 안 바꾼다 */
    case POL_RANDOM: break;
    case POL_PLRU: plru_touch(SET_PLRU(c, set), c->W, way); break;
    default: break;
    }
}

/* 미스 후 way 에 새 라인을 채운 뒤 정책 상태 갱신. */
static void policy_on_fill(cache_t *c, uint32_t set, uint32_t way) {
    way_t *L = &c->ways[(size_t)set * c->W];
    switch (c->pol) {
    case POL_LRU:
    case POL_FIFO: L[way].stamp = c->clock; break;            /* LRU: 사용 시각, FIFO: 들어온 시각 */
    case POL_RANDOM: break;
    case POL_PLRU: plru_touch(SET_PLRU(c, set), c->W, way); break;
    default: break;
    }
}

/* 집합이 꽉 찼을 때(유효 way 뿐) 쫓아낼 way. */
static uint32_t policy_victim(cache_t *c, uint32_t set) {
    way_t *L = &c->ways[(size_t)set * c->W];
    switch (c->pol) {
    case POL_LRU:
    case POL_FIFO: {
        uint32_t v = 0;
        for (uint32_t w = 1; w < c->W; w++)
            if (L[w].stamp < L[v].stamp) v = w;               /* 도장이 가장 오래된 way */
        return v;
    }
    case POL_RANDOM: return xorshift32(&c->rng) & (c->W - 1);
    case POL_PLRU:   return plru_victim(SET_PLRU(c, set), c->W);
    default:         return 0;
    }
}

/* 조회 1회 (주어짐). 1 = hit, 0 = miss. 채울 곳: 빈 way 중 번호가 가장 작은 것 → 없으면 policy_victim. */
static int cache_access(cache_t *c, uint32_t addr) {
    uint32_t set = addr_set(c, addr), tag = addr_tag(c, addr);
    way_t *L = &c->ways[(size_t)set * c->W];
    c->accesses++; c->clock++; c->evicted = 0;
    for (uint32_t w = 0; w < c->W; w++)
        if (L[w].valid && L[w].tag == tag) { c->hits++; policy_on_hit(c, set, w); return 1; }
    c->misses++;
    uint32_t way = c->W;
    for (uint32_t w = 0; w < c->W; w++) if (!L[w].valid) { way = w; break; }
    if (way == c->W) {
        way = policy_victim(c, set);
        c->evicted = 1;
        c->evicted_line = (L[way].tag << c->idx_bits) | set;
    }
    L[way].valid = 1; L[way].tag = tag;
    policy_on_fill(c, set, way);
    return 0;
}

/* ======================= Part B — 3C 분류 ======================= */
typedef struct {
    uint64_t acc, hits, misses;
    uint64_t comp, cap, conf;   /* 미스 = comp + cap + conf (접근 단위 분류) */
    uint64_t anti;              /* 이 캐시는 히트, FA-LRU 는 미스 (집합 연관이 FA-LRU 보다 잘한 접근) */
    uint64_t fa_misses;         /* 같은 용량 FA-LRU 의 미스 = comp + cap + anti */
} stats_t;

/* 한 번이라도 본 라인 주소 집합 (주어짐): open addressing, 키 0xFFFFFFFF = 빈 칸. */
typedef struct { uint32_t *k; uint32_t mask; } lineset_t;
static int lineset_init(lineset_t *s, size_t n) {
    uint32_t cap = 16;
    while (cap < 2 * n + 16) cap <<= 1;
    s->k = malloc((size_t)cap * sizeof *s->k);
    if (!s->k) return -1;
    memset(s->k, 0xFF, (size_t)cap * sizeof *s->k);
    s->mask = cap - 1;
    return 0;
}
static int lineset_insert(lineset_t *s, uint32_t line) {       /* 1 = 처음 본 라인 */
    uint32_t h = (line * 2654435761u) & s->mask;
    while (s->k[h] != 0xFFFFFFFFu) { if (s->k[h] == line) return 0; h = (h + 1) & s->mask; }
    s->k[h] = line;
    return 1;
}

/* compulsory = 그 라인 주소를 처음 만짐 · capacity = 같은 용량 FA-LRU 도 미스 · conflict = 나머지 (FA-LRU 는 히트).
 * (Hill & Smith 1989). 집계 정의의 conflict = misses - fa_misses = conf - anti (음수가 될 수 있다). */
static int sim_run(uint32_t C, uint32_t B, uint32_t W, policy_t pol, const acc_t *t, size_t n, stats_t *st) {
    cache_t c, fa;
    lineset_t seen;
    memset(st, 0, sizeof *st);
    if (cache_init(&c, C, B, W, pol)) return -1;
    if (cache_init(&fa, C, B, C / B, POL_LRU)) { cache_free(&c); return -1; }
    if (lineset_init(&seen, n)) { cache_free(&c); cache_free(&fa); return -1; }
    for (size_t i = 0; i < n; i++) {
        int first = lineset_insert(&seen, t[i].addr >> c.off_bits);
        int h = cache_access(&c, t[i].addr);
        int fh = cache_access(&fa, t[i].addr);
        st->acc++;
        if (!fh) st->fa_misses++;
        if (h) { st->hits++; if (!fh) st->anti++; continue; }
        st->misses++;
        if (first)    st->comp++;
        else if (!fh) st->cap++;
        else          st->conf++;
    }
    free(seen.k); cache_free(&c); cache_free(&fa);
    return 0;
}

/* ======================= Part C — 펌웨어 시나리오 ======================= */
typedef struct {
    uint32_t C, B, W;
    policy_t pol;
    uint32_t nc_lo, nc_hi;      /* MPU non-cacheable 창 [nc_lo, nc_hi): 조회도 채움도 안 한다 (nc_lo == nc_hi → 없음) */
} scen_t;

/* 주소가 [lo, hi) 인 접근 중 캐시 미스 수. non-cacheable 접근은 캐시를 건드리지 않고 미스로도 세지 않는다.
 * 설정이 잘못되면 UINT64_MAX. */
static uint64_t region_misses(const scen_t *sc, const acc_t *t, size_t n, uint32_t lo, uint32_t hi) {
    cache_t c;
    if (cache_init(&c, sc->C, sc->B, sc->W, sc->pol)) return UINT64_MAX;
    uint64_t m = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t a = t[i].addr;
        if (a >= sc->nc_lo && a < sc->nc_hi) continue;       /* 버스로 바로 — 캐시 상태 불변 */
        if (!cache_access(&c, a) && a >= lo && a < hi) m++;
    }
    cache_free(&c);
    return m;
}

/* W = 1, 2, 4, ..., C/B 중 misses(W, pol) <= (1 + tol) * misses(FA-LRU) 인 가장 작은 W. 실패 0. */
static uint32_t min_ways_for(uint32_t C, uint32_t B, policy_t pol, const acc_t *t, size_t n, double tol) {
    stats_t st;
    for (uint32_t W = 1; W <= C / B; W *= 2) {
        if (sim_run(C, B, W, pol, t, n, &st)) return 0;
        if ((double)st.misses <= (1.0 + tol) * (double)st.fa_misses) return W;
    }
    return 0;
}

/* ======================= 트레이스 생성기 (주어짐) ======================= */
#define TMAX (1u << 16)
static acc_t g_t[TMAX];

static size_t push(acc_t *t, size_t n, uint32_t a, char rw) { t[n].addr = a; t[n].rw = rw; t[n].size = 4; return n + 1; }
static size_t trace_lines(acc_t *t, const uint32_t *lines, size_t k, uint32_t B) {   /* 라인 번호 목록 → 주소 */
    for (size_t i = 0; i < k; i++) push(t, i, lines[i] * B, 'R');
    return k;
}
/* 20 x 64 int32 행렬 (행 256 B, base 0x10000000) 을 열 우선으로 1회 */
static size_t gen_colwalk(acc_t *t) {
    size_t n = 0;
    for (uint32_t c = 0; c < 64; c++)
        for (uint32_t r = 0; r < 20; r++) n = push(t, n, 0x10000000u + r * 256u + c * 4u, 'R');
    return n;
}
/* nlines 개 라인을 reps 번 순환 (라인당 1 접근) */
static size_t gen_loop(acc_t *t, uint32_t nlines, uint32_t reps, uint32_t B) {
    size_t n = 0;
    for (uint32_t r = 0; r < reps; r++)
        for (uint32_t i = 0; i < nlines; i++) n = push(t, n, 0x20000000u + i * B, 'R');
    return n;
}
/* 제어 루프 tick 마다: 핫 영역(계수표+상태) 64 라인 읽기 → DMA RX 버퍼 96 라인 새로 읽기(스트리밍).
 * 핫 = [HOT_BASE, HOT_BASE+2 KB), 스트림 = [STRM_BASE, ...) tick 마다 새 주소. */
#define HOT_BASE  0x20000000u
#define HOT_END   (HOT_BASE + 64u * 32u)
#define STRM_BASE 0x30000000u
#define STRM_END  0x40000000u
static size_t gen_hot_stream(acc_t *t, uint32_t ticks) {
    size_t n = 0;
    for (uint32_t k = 0; k < ticks; k++) {
        for (uint32_t i = 0; i < 64; i++) n = push(t, n, HOT_BASE + i * 32u, 'R');
        for (uint32_t i = 0; i < 96; i++) n = push(t, n, STRM_BASE + (k * 96u + i) * 32u, 'R');
    }
    return n;
}
/* 90% 는 핫 96 라인, 10% 는 콜드 4096 라인에서 균등 랜덤 (xorshift32 고정 시드) */
static size_t gen_rand_hot(acc_t *t, size_t count, uint32_t seed) {
    uint32_t s = seed;
    size_t n = 0;
    for (size_t i = 0; i < count; i++) {
        uint32_t r = xorshift32(&s);
        uint32_t line = (r % 10u) ? (r >> 8) % 96u : 96u + (r >> 8) % 4096u;
        n = push(t, n, 0x40000000u + line * 32u, (r & 0x10000u) ? 'W' : 'R');
    }
    return n;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

/* 라인 번호 목록을 한 캐시에 흘려 미스 수 */
static uint64_t misses_of(uint32_t C, uint32_t B, uint32_t W, policy_t pol, const uint32_t *lines, size_t k) {
    acc_t t[64];
    stats_t st;
    trace_lines(t, lines, k, B);
    return sim_run(C, B, W, pol, t, k, &st) ? UINT64_MAX : st.misses;
}

#ifndef ARCH_M05_NO_MAIN
int main(void) {
    cache_t c;
    stats_t st;

    /* ---------- Part A ---------- */
    printf("-- Part A: N-way + LRU / FIFO / Random --\n");
    CHECK(cache_init(&c, 4096, 32, 4, POL_LRU) == 0 && c.S == 32 && c.off_bits == 5 && c.idx_bits == 5);
    CHECK(addr_set(&c, 0x12345678u) == 0x13 && addr_tag(&c, 0x12345678u) == 0x48D15u);   /* set = (a >> 5) & 31, tag = a >> 10 */
    cache_free(&c);
    CHECK(cache_init(&c, 4096, 32, 3, POL_LRU) == -1);          /* 3-way: 2의 거듭제곱 아님 */
    CHECK(cache_init(&c, 64, 32, 4, POL_LRU) == -1);            /* B*W > C */
    {   /* 한 집합(FA 4-way, 128 B) 에 라인 A B C D A E A: LRU 는 B 를, FIFO 는 A 를 쫓는다 */
        const uint32_t s[] = {0, 1, 2, 3, 0, 4, 0};
        CHECK(misses_of(128, 32, 4, POL_LRU, s, 7) == 5);       /* A B C D E */
        CHECK(misses_of(128, 32, 4, POL_FIFO, s, 7) == 6);      /* A B C D E A */
        cache_t l, f;
        cache_init(&l, 128, 32, 4, POL_LRU); cache_init(&f, 128, 32, 4, POL_FIFO);
        for (int i = 0; i < 6; i++) { cache_access(&l, s[i] * 32); cache_access(&f, s[i] * 32); }
        CHECK(l.evicted && l.evicted_line == 1);                /* LRU 희생자 = B */
        CHECK(f.evicted && f.evicted_line == 0);                /* FIFO 희생자 = A (히트해도 순서 그대로) */
        cache_free(&l); cache_free(&f);
    }
    {   /* 순환 루프 5 라인 x 4 회, FA 4-way: LRU / FIFO 는 전부 미스 (LRU 절벽), Random 은 일부 히트 */
        uint32_t s[20];
        for (int i = 0; i < 20; i++) s[i] = (uint32_t)(i % 5);
        CHECK(misses_of(128, 32, 4, POL_LRU, s, 20) == 20);
        CHECK(misses_of(128, 32, 4, POL_FIFO, s, 20) == 20);
        uint64_t r = misses_of(128, 32, 4, POL_RANDOM, s, 20);
        printf("      loop5 on FA-4: Random misses = %llu / 20\n", (unsigned long long)r);
        CHECK(r < 20 && r == misses_of(128, 32, 4, POL_RANDOM, s, 20));   /* 고정 시드 → 재현 */
    }
    {   /* 직접 사상(W = 1) 이면 정책이 의미 없다: 4 정책 모두 같은 미스 */
        size_t n = gen_rand_hot(g_t, 20000, 7);
        uint64_t m[POL_N];
        for (int p = 0; p < POL_N; p++) { sim_run(4096, 32, 1, (policy_t)p, g_t, n, &st); m[p] = st.misses; }
        CHECK(m[0] == m[1] && m[1] == m[2] && m[2] == m[3]);
    }
    {   /* LRU 포함 성질(stack property): S 고정, W → 2W 면 매 접근 hit(W) ⊆ hit(2W) */
        size_t n = gen_rand_hot(g_t, 20000, 11);
        int ok = 1;
        for (uint32_t W = 1; W <= 8; W *= 2) {
            cache_t a, b;
            cache_init(&a, 32u * 32u * W, 32, W, POL_LRU);           /* S = 32 고정 */
            cache_init(&b, 32u * 32u * W * 2, 32, W * 2, POL_LRU);
            for (size_t i = 0; i < n; i++) {
                int ha = cache_access(&a, g_t[i].addr), hb = cache_access(&b, g_t[i].addr);
                if (ha && !hb) ok = 0;
            }
            cache_free(&a); cache_free(&b);
        }
        CHECK(ok);
    }

    /* ---------- Part B ---------- */
    printf("-- Part B: tree-PLRU, 3C --\n");
    {
        uint8_t b4[3] = {0, 0, 0};
        plru_touch(b4, 4, 0); plru_touch(b4, 4, 1); plru_touch(b4, 4, 2); plru_touch(b4, 4, 3);
        CHECK(b4[0] == 0 && b4[1] == 0 && b4[2] == 0 && plru_victim(b4, 4) == 0);
        plru_touch(b4, 4, 0);
        CHECK(b4[0] == 1 && b4[1] == 1 && b4[2] == 0 && plru_victim(b4, 4) == 2);   /* LRU 라면 way 1 */
        uint8_t b8[7] = {0};
        for (uint32_t w = 0; w < 8; w++) plru_touch(b8, 8, w);
        plru_touch(b8, 8, 3); plru_touch(b8, 8, 5);
        printf("      8-way after fill 0..7, touch 3, 5: bits = %d%d%d%d%d%d%d, victim = %u\n",
               b8[0], b8[1], b8[2], b8[3], b8[4], b8[5], b8[6], plru_victim(b8, 8));
        CHECK(b8[0] == 0 && b8[1] == 0 && b8[2] == 1 && b8[3] == 0 && b8[4] == 0 && b8[5] == 0 && b8[6] == 0);
        CHECK(plru_victim(b8, 8) == 0);
        plru_touch(b8, 8, 0);
        CHECK(plru_victim(b8, 8) == 6);                          /* LRU 순서로는 way 1 */
        uint8_t b1[1] = {0};
        plru_touch(b1, 1, 0);
        CHECK(plru_victim(b1, 1) == 0 && b1[0] == 0);            /* W = 1: 비트 없음 */
    }
    {   /* PLRU != LRU 트레이스: A B C D A E C */
        const uint32_t s[] = {0, 1, 2, 3, 0, 4, 2};
        CHECK(misses_of(128, 32, 4, POL_LRU, s, 7) == 5);        /* E 가 B 를 쫓음 → C 히트 */
        CHECK(misses_of(128, 32, 4, POL_PLRU, s, 7) == 6);       /* E 가 C 를 쫓음 → C 미스 */
    }
    {   /* 2-way 에서 PLRU 비트 1개 = 정확한 LRU */
        size_t n = gen_rand_hot(g_t, 20000, 3);
        stats_t a, b;
        sim_run(2048, 32, 2, POL_LRU, g_t, n, &a); sim_run(2048, 32, 2, POL_PLRU, g_t, n, &b);
        CHECK(a.misses == b.misses && a.misses > 0);
    }
    {   /* 3C 손계산: 8 라인 캐시(256 B, 32 B 라인) */
        const uint32_t pp[] = {0, 8, 0, 8};                      /* 직접 사상 같은 집합 핑퐁 */
        acc_t t[32];
        size_t n = trace_lines(t, pp, 4, 32);
        sim_run(256, 32, 1, POL_LRU, t, n, &st);
        CHECK(st.misses == 4 && st.comp == 2 && st.cap == 0 && st.conf == 2 && st.fa_misses == 2);
        sim_run(256, 32, 2, POL_LRU, t, n, &st);
        CHECK(st.misses == 2 && st.conf == 0);                   /* 2-way 가 conflict 를 없앤다 */
        uint32_t l9[27];
        for (int i = 0; i < 27; i++) l9[i] = (uint32_t)(i % 9);  /* 9 라인 순환 x 3, 용량 8 라인 */
        n = trace_lines(t, l9, 27, 32);
        sim_run(256, 32, 8, POL_LRU, t, n, &st);                 /* FA-LRU 자신 */
        CHECK(st.misses == 27 && st.comp == 9 && st.cap == 18 && st.conf == 0 && st.anti == 0);
        sim_run(256, 32, 1, POL_LRU, t, n, &st);                 /* 직접 사상: 0 과 8 만 충돌 */
        CHECK(st.misses == 13 && st.comp == 9 && st.cap == 4 && st.conf == 0 && st.anti == 14);
        CHECK((int64_t)st.misses - (int64_t)st.fa_misses == -14);   /* 집계 conflict 음수 */
    }
    {   /* 항등식: 모든 정책 · 연관도 · 트레이스에서 misses = comp+cap+conf, fa = comp+cap+anti */
        int ok = 1;
        size_t n = gen_rand_hot(g_t, 30000, 5);
        for (int p = 0; p < POL_N; p++)
            for (uint32_t W = 1; W <= 128; W *= 2) {
                sim_run(4096, 32, W, (policy_t)p, g_t, n, &st);
                if (st.misses != st.comp + st.cap + st.conf || st.fa_misses != st.comp + st.cap + st.anti ||
                    st.hits + st.misses != st.acc) ok = 0;
                if (W == 128 && p == POL_LRU && (st.conf != 0 || st.anti != 0)) ok = 0;
            }
        CHECK(ok);
    }
    {   /* 같은 용량에서 연관도를 올렸는데 미스가 는다: 라인 0 2 4 반복, 4 라인 캐시 */
        const uint32_t s[] = {0, 2, 4, 0, 2, 4};
        CHECK(misses_of(128, 32, 1, POL_LRU, s, 6) == 5);        /* 집합 0: 0/4 충돌, 집합 2: 2 혼자 */
        CHECK(misses_of(128, 32, 2, POL_LRU, s, 6) == 6);        /* 집합 0 하나에 3 라인 → 2-way 순환 */
    }

    /* ---------- Part C ---------- */
    printf("-- Part C: firmware hot set + DMA stream --\n");
    {
        size_t n = gen_hot_stream(g_t, 16);
        CHECK(n == 16u * 160u);
        uint64_t hm[POL_N];
        for (int p = 0; p < POL_N; p++) {
            scen_t sc = {4096, 32, 4, (policy_t)p, 0, 0};
            hm[p] = region_misses(&sc, g_t, n, HOT_BASE, HOT_END);
            printf("      4 KB 4-way %-6s: hot misses %4llu / %u\n", pol_name[p], (unsigned long long)hm[p], 16u * 64u);
        }
        CHECK(hm[POL_LRU] == 16u * 64u);                          /* 핫 5 라인/집합 > 4 way → 매 tick 전부 미스 */
        CHECK(hm[POL_FIFO] == 16u * 64u);
        CHECK(hm[POL_RANDOM] < hm[POL_LRU] && hm[POL_PLRU] < hm[POL_LRU]);
        scen_t fa = {4096, 32, 128, POL_LRU, 0, 0};
        CHECK(region_misses(&fa, g_t, n, HOT_BASE, HOT_END) == 16u * 64u);   /* FA 여도 160 > 128 라인 */
        scen_t nc = {4096, 32, 4, POL_LRU, STRM_BASE, STRM_END};
        CHECK(region_misses(&nc, g_t, n, HOT_BASE, HOT_END) == 64);          /* 스트림 non-cacheable → 첫 tick 만 */
        CHECK(region_misses(&nc, g_t, n, STRM_BASE, STRM_END) == 0);         /* 우회한 접근은 미스로 안 센다 */
        scen_t bad = {4096, 32, 3, POL_LRU, 0, 0};
        CHECK(region_misses(&bad, g_t, n, 0, 0xFFFFFFFFu) == UINT64_MAX);
    }
    {
        size_t n = gen_colwalk(g_t);
        CHECK(min_ways_for(4096, 32, POL_LRU, g_t, n, 0.05) == 32);    /* 행 20개가 한 집합에 → 32-way 부터 들어간다 */
        n = gen_loop(g_t, 160, 8, 32);
        CHECK(min_ways_for(4096, 32, POL_LRU, g_t, n, 0.0) == 1);      /* FA-LRU 가 전부 미스 → DM 도 충분 */
        n = gen_rand_hot(g_t, 30000, 9);
        uint32_t w = min_ways_for(4096, 32, POL_LRU, g_t, n, 0.05);
        printf("      rand_hot: min ways within 5%% of FA-LRU = %u\n", w);
        CHECK(w == 8);
        CHECK(min_ways_for(4096, 32, POL_LRU, g_t, n, -2.0) == 0);     /* 만족 못 함 → 0 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
