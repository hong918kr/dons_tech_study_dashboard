/* MEM-B03 추가 연습 — 한 번에 검증
 *   연습 1: adaptive timeout 페이지 정책 (idle T cycle 뒤 precharge) — burst 워크로드에서 open/close 둘 다 이긴다
 *   연습 2: XOR 뱅크 해싱 — 128 KB stride 의 bank camping 이 사라지면 G=0 지연이 어떻게 바뀌나
 *   연습 3: shadow row 레지스터 — close-page 로 돌면서 "open 이었다면" 의 hit/conflict 를 센다
 *   연습 4: 손익분기 h* 일반화 — 다른 tRCD/tRP, empty 포함 부등식, gap G 에 따른 h*(G)
 *   연습 5: power-of-two stride 의 hit/empty/conflict 닫힌 식
 * 모델은 solution.c 축약판 (DDR4-3200AA 22-22-22, 16 bank RoBaCo, row 8 KB, 읽기만, 순서대로 하나씩).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint32_t CL, tRCD, tRP, tRAS, tRC, tRTP, BURST; } dram_timing;
static const dram_timing T = { 22, 22, 22, 52, 74, 12, 4 };

#define NBANK    16u
#define PA_MASK  ((1ull << 33) - 1ull)
#define NO_ROW   (-1)
#define LONG_AGO (-1000000)
#define T_INF    UINT32_MAX

static bool g_xor;                                  /* 연습 2: bank ^= row[3:0] */
typedef struct { uint32_t bank, row; } loc;
static loc dec(uint64_t pa) {
    loc l;
    l.row  = (uint32_t)((pa >> 17) & 0xFFFFu);
    l.bank = (uint32_t)((pa >> 13) & 0xFu);
    if (g_xor) l.bank ^= l.row & 0xFu;
    return l;
}

typedef enum { RB_HIT, RB_EMPTY, RB_CONFLICT } rb_class;
typedef enum { POL_OPEN, POL_CLOSE, POL_TIMEOUT } policy_t;
typedef struct { int32_t open_row; int64_t act_at, rd_at, ready_at, done_at; } bank_state;
static void bank_reset(bank_state *b) {
    b->open_row = NO_ROW; b->act_at = LONG_AGO; b->rd_at = LONG_AGO; b->ready_at = 0; b->done_at = LONG_AGO;
}
static int64_t max64(int64_t a, int64_t b) { return a > b ? a : b; }
static uint32_t lat_of(rb_class c, const dram_timing *t) {
    return c == RB_HIT ? t->CL + t->BURST : c == RB_EMPTY ? t->tRCD + t->CL + t->BURST
                                                          : t->tRP + t->tRCD + t->CL + t->BURST;
}

/* 연습 1 포함 TIMED 접근. POL_TIMEOUT: 마지막 데이터 뒤 tmo cycle 동안 요청이 없으면
 * p = max(done + tmo, ACT + tRAS, RD + tRTP) 에 precharge. 요청이 now >= p 에 오면 이미 닫혀 있다. */
static int64_t access_timed(bank_state *b, uint32_t row, int64_t now, policy_t pol, uint32_t tmo,
                            const dram_timing *t, rb_class *cls) {
    if (pol == POL_TIMEOUT && b->open_row != NO_ROW && tmo != T_INF) {
        int64_t p = max64(b->done_at + tmo, max64(b->act_at + t->tRAS, b->rd_at + t->tRTP));
        if (now >= p) { b->open_row = NO_ROW; b->ready_at = p + t->tRP; }
    }
    int64_t act, rd;
    *cls = b->open_row == NO_ROW ? RB_EMPTY : ((uint32_t)b->open_row == row ? RB_HIT : RB_CONFLICT);
    if (*cls == RB_HIT) {
        rd = max64(now, b->act_at + t->tRCD);
    } else {
        if (*cls == RB_CONFLICT) act = max64(now, max64(b->act_at + t->tRAS, b->rd_at + t->tRTP)) + t->tRP;
        else                     act = max64(now, b->ready_at);
        act = max64(act, b->act_at + t->tRC);
        b->act_at = act;
        rd = act + t->tRCD;
    }
    b->rd_at = rd;
    b->done_at = rd + t->CL + t->BURST;
    if (pol == POL_CLOSE) {
        b->ready_at = max64(rd + t->tRTP, b->act_at + t->tRAS) + t->tRP;
        b->open_row = NO_ROW;
    } else {
        b->open_row = (int32_t)row;
    }
    return b->done_at;
}

typedef struct { uint64_t n, hit, empty, conflict; int64_t total; } sim_stats;

/* gaps == NULL 이면 모든 gap = gap0 */
static void sim_timed(const uint64_t *pa, const uint32_t *gaps, size_t n, uint32_t gap0, policy_t pol,
                      uint32_t tmo, const dram_timing *t, sim_stats *st) {
    bank_state bk[NBANK];
    for (unsigned i = 0; i < NBANK; i++) bank_reset(&bk[i]);
    memset(st, 0, sizeof *st);
    int64_t now = 0;
    for (size_t i = 0; i < n; i++) {
        loc l = dec(pa[i]);
        rb_class c;
        int64_t done = access_timed(&bk[l.bank], l.row, now, pol, tmo, t, &c);
        st->n++; st->total += done - now;
        if (c == RB_HIT) st->hit++; else if (c == RB_EMPTY) st->empty++; else st->conflict++;
        now = done + (gaps ? gaps[i] : gap0);
    }
}

/* SIMPLE 모드 (open/close) + 연습 3 의 shadow row: close 로 돌면서 open 이었다면의 클래스를 센다 */
static void sim_simple(const uint64_t *pa, size_t n, policy_t pol, const dram_timing *t, sim_stats *st, sim_stats *shadow) {
    int32_t open_row[NBANK], sh_row[NBANK];
    for (unsigned i = 0; i < NBANK; i++) open_row[i] = sh_row[i] = NO_ROW;
    memset(st, 0, sizeof *st);
    if (shadow) memset(shadow, 0, sizeof *shadow);
    for (size_t i = 0; i < n; i++) {
        loc l = dec(pa[i]);
        rb_class c = open_row[l.bank] == NO_ROW ? RB_EMPTY
                   : ((uint32_t)open_row[l.bank] == l.row ? RB_HIT : RB_CONFLICT);
        open_row[l.bank] = pol == POL_OPEN ? (int32_t)l.row : NO_ROW;
        st->n++; st->total += lat_of(c, t);
        if (c == RB_HIT) st->hit++; else if (c == RB_EMPTY) st->empty++; else st->conflict++;
        if (shadow) {                                   /* shadow: 정책과 무관하게 마지막 row 만 기억 */
            rb_class s = sh_row[l.bank] == NO_ROW ? RB_EMPTY
                       : ((uint32_t)sh_row[l.bank] == l.row ? RB_HIT : RB_CONFLICT);
            sh_row[l.bank] = (int32_t)l.row;
            shadow->n++; shadow->total += lat_of(s, t);
            if (s == RB_HIT) shadow->hit++; else if (s == RB_EMPTY) shadow->empty++; else shadow->conflict++;
        }
    }
}

/* 워크로드 (solution.c 와 같은 정의) */
typedef enum { WL_SEQ, WL_STRIDE, WL_RANDOM, WL_PINGPONG, WL_HITMIX } workload_t;
static uint64_t xs64(uint64_t *s) { uint64_t x = *s; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return *s = x; }
static uint64_t enc(uint32_t bank, uint32_t row, uint32_t col) {
    return ((uint64_t)row << 17) | ((uint64_t)bank << 13) | ((uint64_t)col << 3);
}
static size_t gen(workload_t w, uint32_t p, size_t n, uint64_t *out) {
    uint64_t s = 12345u;
    uint32_t row = 0, col = 0;
    for (size_t i = 0; i < n; i++) {
        switch (w) {
        case WL_SEQ:      out[i] = ((uint64_t)i * 64u) & PA_MASK; break;
        case WL_STRIDE:   out[i] = ((uint64_t)i * p) & PA_MASK; break;
        case WL_RANDOM:   out[i] = xs64(&s) & PA_MASK & ~63ull; break;
        case WL_PINGPONG: out[i] = enc(3, (i & 1u) ? 0x20u : 0x10u, (uint32_t)(i / 2u % 128u) * 8u); break;
        case WL_HITMIX:
            if (i > 0) {
                if (((uint64_t)i * p) / 100u > ((uint64_t)(i - 1u) * p) / 100u) col = (col + 8u) & 0x3FFu;
                else { row++; col = 0; }
            }
            out[i] = enc(0, row, col);
            break;
        }
    }
    return n;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define N 4096u
static uint64_t tr[N + 1];
static uint32_t gp[N + 1];

static uint32_t gcd_u32(uint32_t a, uint32_t b) { while (b) { uint32_t r = a % b; a = b; b = r; } return a; }

int main(void) {
    sim_stats o, c, x;

    /* ---- 연습 1: adaptive timeout ---- */
    {
        const size_t n = 1024;                  /* 256 burst x 4: 같은 row 4번(gap 5), burst 사이 gap 300 */
        for (size_t i = 0; i < n; i++) { tr[i] = enc(0, (uint32_t)(i / 4), (uint32_t)(i % 4) * 8u); gp[i] = (i % 4 == 3) ? 300u : 5u; }
        sim_timed(tr, gp, n, 0, POL_OPEN, 0, &T, &o);
        sim_timed(tr, gp, n, 0, POL_CLOSE, 0, &T, &c);
        printf("[1] burst workload: open %.2f  close %.2f  (cycles/req)\n", (double)o.total / n, (double)c.total / n);
        static const uint32_t tmos[] = { 0, 4, 5, 6, 50, 300, 301, T_INF };
        int64_t tot[8];
        for (int k = 0; k < 8; k++) {
            sim_timed(tr, gp, n, 0, POL_TIMEOUT, tmos[k], &T, &x);
            tot[k] = x.total;
            printf("    timeout %10u: %.2f  (hit %llu empty %llu conflict %llu)\n", tmos[k], (double)x.total / n,
                   (unsigned long long)x.hit, (unsigned long long)x.empty, (unsigned long long)x.conflict);
        }
        CHECK(o.total == 48 + 255 * 70 + 768 * 26 && c.total == 256 * 48 + 768 * 69);
        CHECK(tot[7] == o.total && tot[6] == o.total);         /* T > 300: 절대 안 닫힘 = open */
        CHECK(tot[0] == c.total && tot[1] == c.total);         /* T <= 4: tRAS 끝(ACT+52)에 닫힘 = RDA 와 같은 시각 */
        CHECK(tot[2] == c.total + 768);                        /* T = gap 5: 요청 도착 순간 PRE -> RDA 보다 1 cycle 늦다 */
        CHECK(tot[3] == 256 * 48 + 768 * 26 && tot[4] == tot[3]);   /* 5 < T < 300: burst 안 hit + burst 사이 숨김 */
        CHECK(tot[5] == o.total);                              /* T = 300 = gap: empty 로 세지만 tRP 를 통째로 기다림 */
        CHECK(tot[4] < o.total && tot[4] < c.total);
    }

    /* ---- 연습 2: XOR 뱅크 해싱과 128 KB stride ---- */
    {
        gen(WL_STRIDE, 131072, N, tr);
        g_xor = false;
        sim_timed(tr, NULL, N, 0, POL_OPEN, 0, &T, &o);
        sim_timed(tr, NULL, N, 0, POL_CLOSE, 0, &T, &c);
        CHECK(o.empty == 1 && o.total == 48 + 74 * (int64_t)(N - 1) && c.total == o.total);
        g_xor = true;
        bool used[NBANK] = { false };
        int nb = 0;
        for (size_t i = 0; i < N; i++) { loc l = dec(tr[i]); if (!used[l.bank]) { used[l.bank] = true; nb++; } }
        sim_timed(tr, NULL, N, 0, POL_OPEN, 0, &T, &o);
        sim_timed(tr, NULL, N, 0, POL_CLOSE, 0, &T, &c);
        printf("[2] stride 128KB G=0 with XOR: banks used %d, open %.2f close %.2f\n", nb,
               (double)o.total / N, (double)c.total / N);
        CHECK(nb == 16 && o.hit == 0 && o.empty == 16 && o.total == 16 * 48 + 70 * (int64_t)(N - 16));
        CHECK(c.total == 48 * (int64_t)N);               /* 같은 뱅크 재방문이 16 요청 뒤 -> precharge 숨김 */
        gen(WL_SEQ, 0, N, tr);
        sim_simple(tr, N, POL_OPEN, &T, &o, NULL);
        CHECK(o.hit == 4064);                            /* 순차 접근의 row 지역성은 그대로 */
        g_xor = false;
    }

    /* ---- 연습 3: shadow row 레지스터 ---- */
    {
        static const workload_t ws[] = { WL_SEQ, WL_STRIDE, WL_STRIDE, WL_STRIDE, WL_RANDOM, WL_PINGPONG, WL_HITMIX };
        static const uint32_t   ps[] = { 0, 256, 4096, 131072, 0, 0, 70 };
        bool same = true;
        for (int i = 0; i < 7; i++) {
            gen(ws[i], ps[i], N, tr);
            sim_simple(tr, N, POL_CLOSE, &T, &c, &x);    /* close 로 돌며 shadow 집계 */
            sim_simple(tr, N, POL_OPEN, &T, &o, NULL);   /* 실제 open */
            if (x.hit != o.hit || x.conflict != o.conflict || x.total != o.total || c.hit != 0) same = false;
        }
        CHECK(same);
    }

    /* ---- 연습 4: 손익분기 일반화 ---- */
    {
        static const uint32_t rcd[] = { 22, 17, 39 }, rp[] = { 22, 17, 45 };
        static const uint32_t want_n[] = { 1, 1, 15 }, want_d[] = { 2, 2, 28 };
        bool ok = true;
        for (int i = 0; i < 3; i++) {
            uint32_t g = gcd_u32(rp[i], rp[i] + rcd[i]);
            if (rp[i] / g != want_n[i] || (rp[i] + rcd[i]) / g != want_d[i]) ok = false;
        }
        CHECK(ok);
        /* empty 포함: close - open = H*tRCD - C*tRP  (랜덤 1000 조합으로 직접 합과 대조) */
        uint64_t s = 99;
        bool rule = true;
        for (int i = 0; i < 1000; i++) {
            uint64_t H = xs64(&s) % 1000, E = xs64(&s) % 1000, C = xs64(&s) % 1000;
            int64_t open = (int64_t)(H * 26 + E * 48 + C * 70), close = (int64_t)((H + E + C) * 48);
            if (close - open != (int64_t)(H * 22) - (int64_t)(C * 22)) rule = false;
        }
        CHECK(rule);
        /* gap G (4 <= G <= 26) 에서 h*(G) = (G - 4) / 44 : G = 10 -> 6/44 = 0.136 */
        gen(WL_HITMIX, 10, N + 1, tr);
        sim_timed(tr, NULL, N + 1, 10, POL_OPEN, 0, &T, &o);
        sim_timed(tr, NULL, N + 1, 10, POL_CLOSE, 0, &T, &c);
        CHECK(o.total > c.total);
        gen(WL_HITMIX, 15, N + 1, tr);
        sim_timed(tr, NULL, N + 1, 10, POL_OPEN, 0, &T, &o);
        sim_timed(tr, NULL, N + 1, 10, POL_CLOSE, 0, &T, &c);
        CHECK(o.total < c.total);
        printf("[4] G=10: close mean %.2f (theory 48+16=64)\n", (double)c.total / (N + 1));
    }

    /* ---- 연습 5: stride 닫힌 식 ---- */
    {
        bool ok = true;
        printf("[5] %8s %6s %6s %6s\n", "stride", "hit", "empty", "confl");
        for (uint32_t st = 64; st <= 262144; st <<= 1) {
            gen(WL_STRIDE, st, N, tr);
            sim_simple(tr, N, POL_OPEN, &T, &o, NULL);
            uint64_t per_row = st <= 8192 ? 8192 / st : 1;           /* 한 row 방문에서 쓰는 라인 수 */
            uint64_t V = N / per_row;                                /* row 방문 횟수 */
            uint64_t B = st <= 8192 ? 16 : (131072 / st ? 131072 / st : 1);   /* 쓰이는 뱅크 수 */
            uint64_t E = V < B ? V : B;
            printf("    %8u %6llu %6llu %6llu\n", st, (unsigned long long)o.hit, (unsigned long long)o.empty,
                   (unsigned long long)o.conflict);
            if (o.hit != N - V || o.empty != E || o.conflict != V - E) ok = false;
        }
        CHECK(ok);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
