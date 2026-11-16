/* MEM-B03 Row hit/miss/conflict와 페이지 정책 — row buffer 분류기 + open/close page 지연 시뮬레이터
 *
 * 이 레슨의 정의 (JESD79-4 / 데이터시트 대표값 — 실제 부품은 데이터시트 확인):
 *   DDR4-3200AA (22-22-22): tCK 0.625 ns, CL 22, tRCD 22, tRP 22, tRAS 52, tRC 74, tRTP 12, BL8 = 4 tCK
 *   지연 = 명령 시작부터 마지막 데이터 beat 까지 (cycle):
 *     hit = CL+4 = 26,  empty = tRCD+CL+4 = 48,  conflict = tRP+tRCD+CL+4 = 70 (+ tRAS 대기, TIMED 모드만)
 * 주소 매핑 (MEM-B01 모델 축약판): 1 channel x 1 rank, 16 banks (4 BG x 4 bank 를 flat index 로), RoBaCo
 *   pa[2:0] = 8 B beat 안의 바이트, [12:3] column (1K x 8 B = row 8 KB), [16:13] bank, [32:17] row (64 K)
 * 지연 모드 두 가지:
 *   LAT_SIMPLE: 뱅크 상태(열린 row)만 보고 고정 지연. tRAS 대기와 "precharge 가 숨겨졌는가" 는 무시 (명시적 단순화).
 *   LAT_TIMED : 사이클 시각 추적. tRCD, tRAS(ACT->PRE), tRTP(RD->PRE), tRP(PRE->ACT), tRC(ACT->ACT) 를 모두 지킨다.
 * 요청 = 읽기만, 순서대로 하나씩 (다음 요청 발행 = 앞 요청 데이터 완료 + gap). 스케줄링(FR-FCFS) = MEM-M01.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 주어짐: 타이밍 · 매핑 · 뱅크 상태 ---------------- */
typedef struct { uint32_t tCK_ps, CL, tRCD, tRP, tRAS, tRC, tRTP, BURST; } dram_timing;

static uint32_t ps_to_cyc(uint32_t ps, uint32_t tck_ps) { return (ps + tck_ps - 1u) / tck_ps; } /* 정수 ps ceil */

static dram_timing ddr4_3200aa(void) {
    dram_timing t;
    t.tCK_ps = 625;
    t.CL     = 22;
    t.tRCD   = ps_to_cyc(13750, 625);
    t.tRP    = ps_to_cyc(13750, 625);
    t.tRAS   = ps_to_cyc(32000, 625);
    t.tRC    = ps_to_cyc(45750, 625);
    t.tRTP   = ps_to_cyc(7500, 625);
    t.BURST  = 4;                                 /* BL8 = 8 beat = 4 tCK (DDR) */
    return t;
}

#define NBANK     16u
#define ROW_BYTES 8192u
#define PA_MASK   ((1ull << 33) - 1ull)          /* 8 GB = 16 bank x 64 K row x 8 KB */

typedef struct { uint32_t bank, row, col; } dram_loc;   /* col = 8 B 단위 column 0..1023 */

static dram_loc map_decode(uint64_t pa) {
    dram_loc l;
    l.col  = (uint32_t)((pa >> 3) & 0x3FFu);
    l.bank = (uint32_t)((pa >> 13) & 0xFu);
    l.row  = (uint32_t)((pa >> 17) & 0xFFFFu);
    return l;
}
static uint64_t map_encode(uint32_t bank, uint32_t row, uint32_t col) {
    return ((uint64_t)(row & 0xFFFFu) << 17) | ((uint64_t)(bank & 0xFu) << 13) | ((uint64_t)(col & 0x3FFu) << 3);
}

typedef enum { RB_HIT, RB_EMPTY, RB_CONFLICT } rb_class;
typedef enum { POL_OPEN, POL_CLOSE } policy_t;
typedef enum { LAT_SIMPLE, LAT_TIMED } lat_mode;

#define NO_ROW   (-1)
#define LONG_AGO (-1000000)

typedef struct {
    int32_t open_row;   /* NO_ROW = 닫힘 (precharged) */
    int64_t act_at;     /* 마지막 ACT 시각 (cycle) */
    int64_t rd_at;      /* 마지막 RD 시각 */
    int64_t ready_at;   /* 닫힌 뱅크가 다음 ACT 를 받을 수 있는 시각 (= PRE + tRP) */
} bank_state;

static void bank_reset(bank_state *b) { b->open_row = NO_ROW; b->act_at = LONG_AGO; b->rd_at = LONG_AGO; b->ready_at = 0; }
static int64_t max64(int64_t a, int64_t b) { return a > b ? a : b; }

/* ---------------- Part A: 분류기와 두 가지 지연 모델 ---------------- */
/* open-page 관점의 분류: 닫힘 -> EMPTY, 같은 row 열림 -> HIT, 다른 row 열림 -> CONFLICT */
static rb_class rb_classify(const bank_state *b, uint32_t row) {
    (void)b; (void)row;
    return RB_EMPTY; /* TODO: 닫힘 -> EMPTY, 같은 row -> HIT, 다른 row -> CONFLICT */
}

/* 클래스별 고정 지연 (cycle): 명령 시작 -> 마지막 데이터 beat */
static uint32_t class_latency(rb_class c, const dram_timing *t) {
    (void)c; (void)t;
    return 0; /* TODO: hit = CL+BURST, empty = tRCD+CL+BURST, conflict = tRP+tRCD+CL+BURST */
}

/* LAT_SIMPLE: 반환 = 지연. open 은 row 를 열어 둔다, close 는 접근 뒤 바로 닫는다 (precharge 는 숨겨졌다고 가정). */
static uint32_t access_simple(bank_state *b, uint32_t row, policy_t pol, const dram_timing *t, rb_class *cls) {
    (void)b; (void)row; (void)pol; (void)t;
    *cls = RB_EMPTY;
    return 0; /* TODO: 분류 -> 정책대로 open_row 갱신 -> 클래스 지연 */
}

/* LAT_TIMED: now = 요청이 들어온 시각. 반환 = 마지막 데이터 beat 시각. 뱅크 상태(시각들)를 갱신한다. */
static int64_t access_timed(bank_state *b, uint32_t row, int64_t now, policy_t pol, const dram_timing *t, rb_class *cls) {
    (void)b; (void)row; (void)pol; (void)t; (void)max64;
    *cls = RB_EMPTY;
    return now; /* TODO: hit / empty(ready_at) / conflict(PRE = max(now, ACT+tRAS, RD+tRTP)) + tRC,
                   close 면 RDA 로 ready_at = max(RD+tRTP, ACT+tRAS) + tRP. 반환 = RD + CL + BURST */
}

/* 주어짐: 시뮬레이터 루프 (순서대로 하나씩, closed loop) */
typedef struct { uint64_t n, hit, empty, conflict; int64_t total, max; } sim_stats;

static void sim_run(const uint64_t *pa, size_t n, policy_t pol, lat_mode mode, uint32_t gap,
                    const dram_timing *t, sim_stats *st) {
    bank_state banks[NBANK];
    for (unsigned i = 0; i < NBANK; i++) bank_reset(&banks[i]);
    memset(st, 0, sizeof *st);
    int64_t now = 0;
    for (size_t i = 0; i < n; i++) {
        dram_loc l = map_decode(pa[i]);
        rb_class c;
        int64_t lat;
        if (mode == LAT_SIMPLE) {
            lat = access_simple(&banks[l.bank], l.row, pol, t, &c);
        } else {
            int64_t done = access_timed(&banks[l.bank], l.row, now, pol, t, &c);
            lat = done - now;
            now = done + gap;
        }
        st->n++;
        st->total += lat;
        if (lat > st->max) st->max = lat;
        if (c == RB_HIT) st->hit++; else if (c == RB_EMPTY) st->empty++; else st->conflict++;
    }
}
static double mean_cyc(const sim_stats *s) { return s->n ? (double)s->total / (double)s->n : 0.0; }

/* ---------------- Part B: 워크로드 생성과 손익분기 ---------------- */
typedef enum { WL_SEQ, WL_STRIDE, WL_RANDOM, WL_PINGPONG, WL_HITMIX } workload_t;

static uint64_t xs64(uint64_t *s) { uint64_t x = *s; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return *s = x; }
static uint32_t gcd_u32(uint32_t a, uint32_t b) { while (b) { uint32_t r = a % b; a = b; b = r; } return a; }

/* param: WL_STRIDE = stride 바이트, WL_HITMIX = hit 비율 %(0..100), 나머지는 무시.
 * WL_SEQ: 64 B 라인 순차.  WL_RANDOM: xorshift64, 8 GB 안 64 B 정렬.
 * WL_PINGPONG: bank 3 의 row 0x10 과 0x20 을 번갈아 (column 은 라인 단위로 전진).
 * WL_HITMIX: bank 0 하나. i >= 1 에서 floor(i*p/100) 이 늘면 같은 row 다음 라인(hit), 아니면 row+1 (conflict).
 * 반환 = 쓴 개수 = min(n, cap). */
static size_t gen_trace(workload_t w, uint32_t param, size_t n, uint64_t seed, uint64_t *out, size_t cap) {
    (void)w; (void)param; (void)seed; (void)xs64;
    if (n > cap) n = cap;
    memset(out, 0, n * sizeof *out); /* TODO: 다섯 가지 워크로드 */
    return n;
}

/* LAT_SIMPLE 에서 open == close 가 되는 hit 비율 h* (empty 가 없을 때) = tRP / (tRP + tRCD), 기약분수 */
static void break_even(const dram_timing *t, uint32_t *num, uint32_t *den) {
    (void)t; (void)gcd_u32;
    *num = 0; /* TODO: tRP / (tRP + tRCD) 기약분수 */
    *den = 1;
}

/* ---------------- Part C: PMU 카운터 -> 정책 추천 ---------------- */
typedef enum { REC_BAD = -1, REC_OPEN = 0, REC_CLOSE = 1, REC_TIE = 2 } rec_t;

/* open-page 로 돌던 컨트롤러의 카운터: cas = 읽기 CAS 수, act = ACT 수, pre_miss = 충돌로 인한 PRE 수.
 * hit = cas - act, conflict = pre_miss, empty = act - pre_miss. 모순(act > cas, pre_miss > act) 이면 -1. */
static int pmu_to_mix(uint64_t cas, uint64_t act, uint64_t pre_miss, uint64_t *hit, uint64_t *empty, uint64_t *conf) {
    (void)cas; (void)act; (void)pre_miss;
    *hit = *empty = *conf = 0;
    return 0; /* TODO: 모순 검사 후 hit = cas-act, conflict = pre_miss, empty = act-pre_miss */
}

/* LAT_SIMPLE 기준 open 총 지연 vs close 총 지연(모두 empty). *saved = close - open (>0 이면 open 이 이득). */
static rec_t pmu_recommend(uint64_t cas, uint64_t act, uint64_t pre_miss, const dram_timing *t, int64_t *saved) {
    (void)cas; (void)act; (void)pre_miss; (void)t;
    *saved = 0;
    return REC_TIE; /* TODO: open 총 지연 vs close 총 지연(cas x empty) */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define NREQ 4096u
static uint64_t g_tr[NREQ + 1];

static void run(workload_t w, uint32_t p, size_t n, policy_t pol, lat_mode m, uint32_t gap, sim_stats *st) {
    const dram_timing t = ddr4_3200aa();
    size_t k = gen_trace(w, p, n, 12345u, g_tr, NREQ + 1);
    sim_run(g_tr, k, pol, m, gap, &t, st);
}

static void print_report(void) {
    static const struct { const char *name; workload_t w; uint32_t p; } wl[] = {
        { "seq 64B",        WL_SEQ, 0 },          { "stride 256B",   WL_STRIDE, 256 },
        { "stride 4KB",     WL_STRIDE, 4096 },    { "stride 8KB=row", WL_STRIDE, 8192 },
        { "stride 128KB",   WL_STRIDE, 131072 },  { "random 8GB",    WL_RANDOM, 0 },
        { "pingpong 1bank", WL_PINGPONG, 0 },     { "hitmix 50%",    WL_HITMIX, 50 },
    };
    printf("\n[Part B] n=%u reads, mean latency in cycles (x0.625 = ns)\n", NREQ);
    printf("%-15s %5s %5s %5s | %7s %7s | %7s %7s | %7s %7s | %s\n", "workload", "hit", "empty", "confl",
           "SIMopen", "SIMclos", "G0open", "G0clos", "G100opn", "G100cls", "best(G0)");
    for (size_t i = 0; i < sizeof wl / sizeof wl[0]; i++) {
        sim_stats so, sc, t0o, t0c, t1o, t1c;
        run(wl[i].w, wl[i].p, NREQ, POL_OPEN, LAT_SIMPLE, 0, &so);
        run(wl[i].w, wl[i].p, NREQ, POL_CLOSE, LAT_SIMPLE, 0, &sc);
        run(wl[i].w, wl[i].p, NREQ, POL_OPEN, LAT_TIMED, 0, &t0o);
        run(wl[i].w, wl[i].p, NREQ, POL_CLOSE, LAT_TIMED, 0, &t0c);
        run(wl[i].w, wl[i].p, NREQ, POL_OPEN, LAT_TIMED, 100, &t1o);
        run(wl[i].w, wl[i].p, NREQ, POL_CLOSE, LAT_TIMED, 100, &t1c);
        printf("%-15s %5llu %5llu %5llu | %7.2f %7.2f | %7.2f %7.2f | %7.2f %7.2f | %s\n", wl[i].name,
               (unsigned long long)so.hit, (unsigned long long)so.empty, (unsigned long long)so.conflict,
               mean_cyc(&so), mean_cyc(&sc), mean_cyc(&t0o), mean_cyc(&t0c), mean_cyc(&t1o), mean_cyc(&t1c),
               t0o.total < t0c.total ? "open" : (t0o.total > t0c.total ? "close" : "tie"));
    }
    printf("\n[Part B] hitmix sweep (bank 0, n=%u): mean cycles open/close\n", NREQ + 1);
    printf("%5s | %7s %7s | %7s %7s | %7s %7s\n", "hit%", "SIMopen", "SIMclos", "G0open", "G0clos", "G20open", "G20clos");
    for (uint32_t p = 0; p <= 100; p += 5) {
        sim_stats a, b, c, d, e, f;
        run(WL_HITMIX, p, NREQ + 1, POL_OPEN, LAT_SIMPLE, 0, &a);
        run(WL_HITMIX, p, NREQ + 1, POL_CLOSE, LAT_SIMPLE, 0, &b);
        run(WL_HITMIX, p, NREQ + 1, POL_OPEN, LAT_TIMED, 0, &c);
        run(WL_HITMIX, p, NREQ + 1, POL_CLOSE, LAT_TIMED, 0, &d);
        run(WL_HITMIX, p, NREQ + 1, POL_OPEN, LAT_TIMED, 20, &e);
        run(WL_HITMIX, p, NREQ + 1, POL_CLOSE, LAT_TIMED, 20, &f);
        printf("%5u | %7.2f %7.2f | %7.2f %7.2f | %7.2f %7.2f\n", p, mean_cyc(&a), mean_cyc(&b),
               mean_cyc(&c), mean_cyc(&d), mean_cyc(&e), mean_cyc(&f));
    }
    printf("\n");
}

int main(void) {
    const dram_timing T = ddr4_3200aa();

    /* 주어진 부분: 타이밍 변환과 매핑 */
    CHECK(T.tRCD == 22 && T.tRP == 22 && T.tRAS == 52 && T.tRC == 74 && T.tRTP == 12);
    {
        dram_loc l = map_decode(map_encode(5, 0x1234, 0x3F8));
        CHECK(l.bank == 5 && l.row == 0x1234 && l.col == 0x3F8);
        l = map_decode(0x123456740ull);            /* row = pa>>17, bank = pa[16:13], col = pa[12:3] */
        CHECK(l.row == 0x91A2 && l.bank == 0xB && l.col == 0xE8);
    }

    /* Part A: 분류와 고정 지연 */
    {
        bank_state b; bank_reset(&b);
        CHECK(rb_classify(&b, 5) == RB_EMPTY);
        b.open_row = 5;
        CHECK(rb_classify(&b, 5) == RB_HIT);
        CHECK(rb_classify(&b, 6) == RB_CONFLICT);
        CHECK(class_latency(RB_HIT, &T) == 26 && class_latency(RB_EMPTY, &T) == 48 && class_latency(RB_CONFLICT, &T) == 70);
        CHECK(class_latency(RB_CONFLICT, &T) * T.tCK_ps == 43750);   /* 43.75 ns */

        rb_class c1, c2, c3;
        bank_reset(&b);
        uint32_t l1 = access_simple(&b, 7, POL_OPEN, &T, &c1);
        uint32_t l2 = access_simple(&b, 7, POL_OPEN, &T, &c2);
        uint32_t l3 = access_simple(&b, 9, POL_OPEN, &T, &c3);
        CHECK(c1 == RB_EMPTY && c2 == RB_HIT && c3 == RB_CONFLICT && l1 == 48 && l2 == 26 && l3 == 70);
        CHECK(b.open_row == 9);
        bank_reset(&b);
        l1 = access_simple(&b, 7, POL_CLOSE, &T, &c1);
        l2 = access_simple(&b, 7, POL_CLOSE, &T, &c2);
        CHECK(c1 == RB_EMPTY && c2 == RB_EMPTY && l1 == 48 && l2 == 48 && b.open_row == NO_ROW);
    }
    /* Part A: TIMED 모드 — tRAS / tRP / tRTP 대기 */
    {
        bank_state b; rb_class c; bank_reset(&b);
        CHECK(access_timed(&b, 1, 0, POL_OPEN, &T, &c) == 48 && c == RB_EMPTY);       /* ACT0 RD22 data..48 */
        CHECK(access_timed(&b, 2, 48, POL_OPEN, &T, &c) == 122 && c == RB_CONFLICT);  /* PRE 52(tRAS) ACT74 RD96 */
        CHECK(access_timed(&b, 2, 122, POL_OPEN, &T, &c) == 148 && c == RB_HIT);
        CHECK(access_timed(&b, 3, 150, POL_OPEN, &T, &c) == 220 && c == RB_CONFLICT); /* tRAS 이미 지남: 70 */
        bank_reset(&b);
        access_timed(&b, 1, 0, POL_OPEN, &T, &c);
        CHECK(access_timed(&b, 2, 50, POL_OPEN, &T, &c) - 50 == 72);                  /* gap 2: tRAS 대기 2 */
        bank_reset(&b);
        access_timed(&b, 1, 0, POL_OPEN, &T, &c);
        CHECK(access_timed(&b, 2, 52, POL_OPEN, &T, &c) - 52 == 70);                  /* gap 4: 경계, 대기 0 */

        bank_reset(&b);
        CHECK(access_timed(&b, 1, 0, POL_CLOSE, &T, &c) == 48 && b.ready_at == 74);    /* RDA: PRE 52, ready 74 */
        CHECK(access_timed(&b, 1, 48, POL_CLOSE, &T, &c) - 48 == 74 && c == RB_EMPTY); /* 같은 row 라도 다시 ACT */
        bank_reset(&b);
        access_timed(&b, 1, 0, POL_CLOSE, &T, &c);
        CHECK(access_timed(&b, 1, 74, POL_CLOSE, &T, &c) - 74 == 48);                 /* gap 26: precharge 숨김 */
        bank_reset(&b);
        access_timed(&b, 1, 0, POL_CLOSE, &T, &c);
        CHECK(access_timed(&b, 1, 73, POL_CLOSE, &T, &c) - 73 == 49);                 /* gap 25: 1 cycle 대기 */

        dram_timing u = T; u.tRAS = 20; u.tRC = 42;   /* tRTP 가 묶이는 인위적 타이밍 */
        bank_reset(&b);
        access_timed(&b, 1, 0, POL_OPEN, &u, &c);                                     /* ACT0 RD22 */
        CHECK(access_timed(&b, 2, 23, POL_OPEN, &u, &c) == 104);                      /* PRE max(23,20,34)=34 ACT56 RD78 */
    }

    /* Part B: 워크로드 생성 */
    {
        size_t k = gen_trace(WL_SEQ, 0, 10, 1, g_tr, 4);
        CHECK(k == 4 && g_tr[1] == 64 && g_tr[3] == 192);
        gen_trace(WL_STRIDE, 4096, 8, 1, g_tr, NREQ);
        CHECK(g_tr[3] == 12288 && g_tr[7] == 28672);
        gen_trace(WL_PINGPONG, 0, 4, 1, g_tr, NREQ);
        dram_loc a = map_decode(g_tr[0]), b = map_decode(g_tr[1]), c = map_decode(g_tr[2]);
        CHECK(a.bank == 3 && b.bank == 3 && a.row == 0x10 && b.row == 0x20 && c.row == 0x10 && c.col == 8);
        static uint64_t r2[64];
        gen_trace(WL_RANDOM, 0, 64, 77, g_tr, NREQ);
        gen_trace(WL_RANDOM, 0, 64, 77, r2, 64);
        bool ok = true, differ = false;
        for (int i = 0; i < 64; i++) {
            if (g_tr[i] != r2[i] || (g_tr[i] & 63u) || g_tr[i] > PA_MASK) ok = false;
            if (i && g_tr[i] != g_tr[0]) differ = true;
        }
        CHECK(ok && differ);
    }
    /* Part B: LAT_SIMPLE 결과의 정확한 값 */
    {
        sim_stats o, c;
        run(WL_SEQ, 0, NREQ, POL_OPEN, LAT_SIMPLE, 0, &o);
        run(WL_SEQ, 0, NREQ, POL_CLOSE, LAT_SIMPLE, 0, &c);
        CHECK(o.hit == 4064 && o.empty == 16 && o.conflict == 16 && o.total == 107552);
        CHECK(c.empty == NREQ && c.total == 48 * (int64_t)NREQ);
        run(WL_STRIDE, 4096, NREQ, POL_OPEN, LAT_SIMPLE, 0, &o);
        CHECK(o.hit == 2048 && o.empty == 16 && o.conflict == 2032);            /* hit 50 % = 손익분기 근처 */
        run(WL_STRIDE, 8192, NREQ, POL_OPEN, LAT_SIMPLE, 0, &o);
        CHECK(o.hit == 0 && o.empty == 16 && o.conflict == NREQ - 16);
        run(WL_STRIDE, 131072, NREQ, POL_OPEN, LAT_SIMPLE, 0, &o);
        CHECK(o.hit == 0 && o.empty == 1 && o.conflict == NREQ - 1);            /* bank camping */
        run(WL_HITMIX, 50, 101, POL_OPEN, LAT_SIMPLE, 0, &o);
        CHECK(o.hit == 50 && o.empty == 1 && o.conflict == 50);

        /* 규칙: close_total - open_total == H*tRCD - C*tRP (empty 는 두 정책에서 같다) */
        static const workload_t ws[] = { WL_SEQ, WL_STRIDE, WL_STRIDE, WL_RANDOM, WL_PINGPONG, WL_HITMIX };
        static const uint32_t   ps[] = { 0, 4096, 8192, 0, 0, 30 };
        bool rule = true;
        for (int i = 0; i < 6; i++) {
            run(ws[i], ps[i], NREQ, POL_OPEN, LAT_SIMPLE, 0, &o);
            run(ws[i], ps[i], NREQ, POL_CLOSE, LAT_SIMPLE, 0, &c);
            if (c.total - o.total != (int64_t)(o.hit * T.tRCD) - (int64_t)(o.conflict * T.tRP)) rule = false;
        }
        CHECK(rule);
    }
    /* Part B: LAT_TIMED — gap 에 따라 결론이 바뀐다 */
    {
        sim_stats o, c;
        run(WL_PINGPONG, 0, 5, POL_OPEN, LAT_TIMED, 0, &o);
        run(WL_PINGPONG, 0, 5, POL_CLOSE, LAT_TIMED, 0, &c);
        CHECK(o.total == 48 + 4 * 74 && c.total == 48 + 4 * 74);                /* 같은 뱅크 연속 = tRC 바닥 */
        run(WL_PINGPONG, 0, 5, POL_OPEN, LAT_TIMED, 100, &o);
        run(WL_PINGPONG, 0, 5, POL_CLOSE, LAT_TIMED, 100, &c);
        CHECK(o.total == 48 + 4 * 70 && c.total == 5 * 48);
        run(WL_SEQ, 0, 128, POL_OPEN, LAT_TIMED, 0, &o);
        run(WL_SEQ, 0, 128, POL_CLOSE, LAT_TIMED, 0, &c);
        CHECK(o.total == 48 + 127 * 26 && c.total == 48 + 127 * 74 && c.max == 74);
        run(WL_HITMIX, 50, 101, POL_OPEN, LAT_TIMED, 20, &o);
        run(WL_HITMIX, 50, 101, POL_CLOSE, LAT_TIMED, 20, &c);
        CHECK(o.total == 48 + 50 * 26 + 50 * 70 && c.total == 48 + 100 * 54);
    }
    /* Part B: 손익분기 h* — 식과 시뮬레이션 */
    {
        uint32_t num = 0, den = 0;
        break_even(&T, &num, &den);
        CHECK(num == 1 && den == 2);
        dram_timing u = T; u.tRP = 27;             /* tRP > tRCD 이면 h* 가 1/2 보다 커진다 */
        break_even(&u, &num, &den);
        CHECK(num == 27 && den == 49);
        sim_stats o, c;
        run(WL_HITMIX, 50, NREQ + 1, POL_OPEN, LAT_SIMPLE, 0, &o);
        run(WL_HITMIX, 50, NREQ + 1, POL_CLOSE, LAT_SIMPLE, 0, &c);
        CHECK(o.total == c.total);                                              /* h = 1/2: 정확히 같다 */
        run(WL_HITMIX, 45, NREQ + 1, POL_OPEN, LAT_SIMPLE, 0, &o);
        run(WL_HITMIX, 45, NREQ + 1, POL_CLOSE, LAT_SIMPLE, 0, &c);
        CHECK(o.total > c.total);
        run(WL_HITMIX, 55, NREQ + 1, POL_OPEN, LAT_SIMPLE, 0, &o);
        run(WL_HITMIX, 55, NREQ + 1, POL_CLOSE, LAT_SIMPLE, 0, &c);
        CHECK(o.total < c.total);
        run(WL_HITMIX, 35, NREQ + 1, POL_OPEN, LAT_TIMED, 20, &o);              /* gap 20: h* = 16/44 = 0.364 */
        run(WL_HITMIX, 35, NREQ + 1, POL_CLOSE, LAT_TIMED, 20, &c);
        CHECK(o.total > c.total);
        run(WL_HITMIX, 40, NREQ + 1, POL_OPEN, LAT_TIMED, 20, &o);
        run(WL_HITMIX, 40, NREQ + 1, POL_CLOSE, LAT_TIMED, 20, &c);
        CHECK(o.total < c.total);
    }

    /* Part C: PMU 카운터 -> 정책 */
    {
        uint64_t h = 0, e = 0, c = 0;
        CHECK(pmu_to_mix(1000000, 300000, 250000, &h, &e, &c) == 0 && h == 700000 && e == 50000 && c == 250000);
        CHECK(pmu_to_mix(100, 120, 10, &h, &e, &c) == -1);                     /* ACT > CAS: 모순 */
        CHECK(pmu_to_mix(100, 50, 60, &h, &e, &c) == -1);                      /* PRE_MISS > ACT: 모순 */
        int64_t saved = 0;
        CHECK(pmu_recommend(1000000, 300000, 250000, &T, &saved) == REC_OPEN && saved == 9900000);
        CHECK(pmu_recommend(1000000, 900000, 880000, &T, &saved) == REC_CLOSE && saved == -17160000);
        CHECK(pmu_recommend(1000, 600, 400, &T, &saved) == REC_TIE && saved == 0);      /* H = C = 400 */
        CHECK(pmu_recommend(10, 20, 0, &T, &saved) == REC_BAD);
        /* 시뮬레이터가 센 클래스를 카운터로 바꿔 넣으면 같은 결론 */
        sim_stats o, cl;
        run(WL_STRIDE, 4096, NREQ, POL_OPEN, LAT_SIMPLE, 0, &o);
        run(WL_STRIDE, 4096, NREQ, POL_CLOSE, LAT_SIMPLE, 0, &cl);
        rec_t r = pmu_recommend(o.n, o.empty + o.conflict, o.conflict, &T, &saved);
        CHECK(r == REC_OPEN && saved == cl.total - o.total && saved == 16 * 22);
    }

    print_report();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
