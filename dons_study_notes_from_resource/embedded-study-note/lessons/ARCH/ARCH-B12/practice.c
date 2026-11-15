/* ARCH-B12 추가 연습 — 한 번에 검증
 *
 *   연습 1  중단 가능한(abortable) 프리페치: 분기 미스가 진행 중인 프리페치를 끊는다
 *   연습 2  ART 같은 작은 명령 캐시 (완전 연관, LRU): 루프는 살리고, 캐시보다 큰 루프는 못 살린다
 *   연습 3  라인 폭 일반화: 순차 코드 정상 상태 = wpl / max(wpl, L) 페치/사이클, WS 를 가리는 최소 라인 폭
 *   연습 4  부트 .data 복사 (Flash → SRAM) 사이클과 시간
 *   연습 5  클럭 올리기/내리기 순서: LATENCY 먼저냐 클럭 먼저냐
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t u64max(uint64_t a, uint64_t b) { return a > b ? a : b; }
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

#define FLASH_BASE 0x08000000u
#define SRAM_BASE  0x20000000u

/* ---------- 본문 모델의 일반화: 라인 폭과 abort 여부를 매개변수로 ---------- */
typedef enum { FETCH_NOBUF, FETCH_LINEBUF, FETCH_PREFETCH } fetch_mode_t;
typedef struct { fetch_mode_t mode; unsigned ws; unsigned line_bytes; bool abortable; } fcfg_t;
typedef struct { uint64_t cycles, hits, pf_hits, misses; } fstat_t;

static fstat_t fetch_run2(fcfg_t c, const uint32_t *addr, size_t n) {
    fstat_t s = {0};
    const uint64_t L = (uint64_t)c.ws + 1u;
    uint64_t t = 0, flash_free = 0, pf_ready = 0;
    int64_t cur = -1, pf = -1;
    for (size_t i = 0; i < n; i++) {
        int64_t line = (int64_t)(addr[i] / c.line_bytes);
        if (c.mode != FETCH_NOBUF && line == cur) { t += 1; s.hits++; }
        else if (c.mode == FETCH_PREFETCH && line == pf) {
            uint64_t t_req = t;
            t = u64max(t + 1, pf_ready); s.pf_hits++;
            cur = line; pf = line + 1;
            pf_ready = flash_free = u64max(flash_free, t_req) + L;
        } else {
            /* 연습 1: abortable 이면 진행 중인 프리페치를 버리고 바로 시작 */
            uint64_t start = c.abortable ? t : u64max(t, flash_free);
            t = start + L; flash_free = t; s.misses++;
            cur = (c.mode == FETCH_NOBUF) ? -1 : line;
            if (c.mode == FETCH_PREFETCH) { pf = line + 1; pf_ready = flash_free = t + L; }
            else pf = -1;
        }
    }
    s.cycles = t;
    return s;
}

/* ---------- 연습 2: 완전 연관 LRU 명령 캐시 (라인 16 B), 미스 = L, 히트 = 1 ---------- */
#define IC_MAX 64
static uint64_t icache_run(unsigned ws, unsigned nlines, const uint32_t *addr, size_t n, uint64_t *hits) {
    int64_t tag[IC_MAX]; uint64_t last[IC_MAX];
    for (unsigned k = 0; k < nlines; k++) { tag[k] = -1; last[k] = 0; }
    uint64_t t = 0, h = 0;
    for (size_t i = 0; i < n; i++) {
        int64_t line = (int64_t)(addr[i] / 16u);
        unsigned hit = nlines, victim = 0;
        for (unsigned k = 0; k < nlines; k++) {
            if (tag[k] == line) hit = k;
            if (last[k] < last[victim]) victim = k;          /* 가장 오래 안 쓴 줄 (빈 줄은 last=0) */
        }
        if (hit < nlines) { t += 1; h++; last[hit] = t; }
        else { t += ws + 1u; tag[victim] = line; last[victim] = t; }
    }
    if (hits) *hits = h;
    return t;
}

/* ---------- 연습 3: 순차 코드 정상 상태와 최소 라인 폭 ---------- */
static uint64_t seq_cycles_formula(uint64_t n, unsigned ws, unsigned line_bytes) {
    uint64_t L = ws + 1u, wpl = line_bytes / 4u, period = L > wpl ? L : wpl;
    return L + (n / wpl - 1u) * period + (wpl - 1u);
}
/* WS 를 프리페치로 완전히 가리는 가장 작은 2의 거듭제곱 라인 폭(바이트): wpl ≥ L */
static unsigned min_line_bytes_to_hide(unsigned ws) {
    unsigned wpl = 1;
    while (wpl < ws + 1u) wpl <<= 1;
    return wpl * 4u;
}

/* ---------- 연습 4: Flash → SRAM 워드 복사 (본문 bus_run 의 파이프라인 모드를 공식으로) ---------- */
/* 읽기(Flash, ws_src) 와 쓰기(SRAM, ws_dst) 가 번갈아 버스에 나간다: 1 + N × ((1+ws_src) + (1+ws_dst)) */
static uint64_t copy_cycles(uint64_t words, unsigned ws_src, unsigned ws_dst) {
    return 1u + words * ((1u + ws_src) + (1u + ws_dst));
}
/* 사이클 루프로 같은 값을 확인하는 작은 시뮬레이터 (본문 bus_run 의 PIPE 경로만) */
static uint64_t pipe_sim(const unsigned *ws, size_t n) {
    uint64_t c = 0; size_t next = 0; int d = -1; unsigned rem = 0;
    while (next < n || d >= 0) {
        int hready = 1;
        if (d >= 0) { if (rem > 0) { hready = 0; rem--; } else d = -1; }
        if (next < n && hready) { d = (int)next; rem = ws[next]; next++; }
        c++;
    }
    return c;
}

/* ---------- 연습 5: 클럭 변경 순서 ---------- */
#define PS_PER_S 1000000000000ull
static unsigned ws_for_clock(uint64_t t_acc_ps, uint64_t f_hz) {
    uint64_t cyc = (t_acc_ps * f_hz + PS_PER_S - 1) / PS_PER_S;
    return cyc > 1 ? (unsigned)(cyc - 1) : 0u;
}
typedef enum { SET_LATENCY, SET_CLOCK } op_t;
typedef struct { op_t op; uint64_t val; } step_t;
/* 각 단계 직후 "현재 클럭에 필요한 WS ≤ 현재 LATENCY" 인지 검사. 한 번이라도 어기면 false
 * (실제 칩이라면 잘못된 명령을 읽어 HardFault / 이상 동작) */
static bool run_steps(uint64_t t_acc, uint64_t f0, unsigned lat0, const step_t *s, size_t n,
                      uint64_t *f_out, unsigned *lat_out) {
    uint64_t f = f0; unsigned lat = lat0; bool ok = ws_for_clock(t_acc, f) <= lat;
    for (size_t i = 0; i < n; i++) {
        if (s[i].op == SET_LATENCY) lat = (unsigned)s[i].val; else f = s[i].val;
        if (ws_for_clock(t_acc, f) > lat) ok = false;
    }
    *f_out = f; *lat_out = lat;
    return ok;
}
/* 올릴 때: LATENCY 먼저 (그리고 되읽어 확인) → 클럭.  내릴 때: 클럭 먼저 → LATENCY */
static size_t plan_clock_change(uint64_t t_acc, uint64_t f_old, uint64_t f_new, step_t out[2]) {
    unsigned ws_new = ws_for_clock(t_acc, f_new);
    if (f_new >= f_old) { out[0] = (step_t){ SET_LATENCY, ws_new }; out[1] = (step_t){ SET_CLOCK, f_new }; }
    else                { out[0] = (step_t){ SET_CLOCK, f_new };    out[1] = (step_t){ SET_LATENCY, ws_new }; }
    return 2;
}

static uint32_t g_tr[8192];
static void gen_seq(uint32_t *a, size_t n) { for (size_t i = 0; i < n; i++) a[i] = FLASH_BASE + 4u * (uint32_t)i; }
static void gen_loop(uint32_t *a, size_t n, unsigned body) {
    for (size_t i = 0; i < n; i++) a[i] = FLASH_BASE + 4u * (uint32_t)(i % body);
}
static void gen_branchy(uint32_t *a, size_t n, uint32_t seed) {
    uint32_t s = seed, pc = FLASH_BASE; size_t i = 0;
    while (i < n) {
        unsigned run = 1u + xs32(&s) % 8u;
        for (unsigned k = 0; k < run && i < n; k++, i++) { a[i] = pc; pc += 4u; }
        pc = FLASH_BASE + 4u * (xs32(&s) % (65536u / 4u));
    }
}

int main(void) {
    /* 연습 1: abortable 프리페치 */
    for (size_t i = 0; i < 200; i++) g_tr[i] = FLASH_BASE + (uint32_t)i * 160u;    /* 먼 점프 */
    {
        fstat_t na = fetch_run2((fcfg_t){ FETCH_PREFETCH, 3, 16, false }, g_tr, 200);
        fstat_t ab = fetch_run2((fcfg_t){ FETCH_PREFETCH, 3, 16, true }, g_tr, 200);
        CHECK(na.cycles == 2 * 4 * 200 - 4 && ab.cycles == 4 * 200);
    }
    gen_branchy(g_tr, 4096, 0xB12u);
    {
        int ok = 1;
        for (unsigned ws = 0; ws <= 7; ws++) {
            fstat_t lb = fetch_run2((fcfg_t){ FETCH_LINEBUF, ws, 16, false }, g_tr, 4096);
            fstat_t na = fetch_run2((fcfg_t){ FETCH_PREFETCH, ws, 16, false }, g_tr, 4096);
            fstat_t ab = fetch_run2((fcfg_t){ FETCH_PREFETCH, ws, 16, true }, g_tr, 4096);
            if (!(ab.cycles <= lb.cycles && ab.cycles <= na.cycles)) ok = 0;
        }
        CHECK(ok);   /* 끊을 수 있으면 프리페치는 라인 버퍼보다 절대 나빠지지 않는다 */
    }
    gen_seq(g_tr, 1024);
    CHECK(fetch_run2((fcfg_t){ FETCH_PREFETCH, 3, 16, true }, g_tr, 1024).cycles ==
          fetch_run2((fcfg_t){ FETCH_PREFETCH, 3, 16, false }, g_tr, 1024).cycles);   /* 순차는 같다 */

    /* 연습 2: 작은 LRU 명령 캐시 */
    gen_loop(g_tr, 800, 8);                         /* 8 워드 = 2 라인 루프, 100 회 */
    {
        uint64_t h;
        CHECK(icache_run(3, 4, g_tr, 800, &h) == 2 * (4 + 3) + 8 * 99 && h == 800 - 2);
        CHECK(icache_run(3, 1, g_tr, 800, &h) == 14 * 100);                      /* 1줄 = 라인 버퍼 */
        CHECK(icache_run(5, 64, g_tr, 800, &h) == 2 * (6 + 3) + 8 * 99);          /* 5 WS 도 숨김 */
    }
    gen_loop(g_tr, 4000, 40);                       /* 40 워드 = 10 라인 루프 */
    {
        uint64_t h8, h16;
        uint64_t c8 = icache_run(3, 8, g_tr, 4000, &h8), c16 = icache_run(3, 16, g_tr, 4000, &h16);
        CHECK(c8 == 100 * 10 * (4 + 3) && c16 == 10 * (4 + 3) + 40 * 99);       /* 8줄 LRU 는 전멸 */
    }

    /* 연습 3: 라인 폭 일반화 */
    gen_seq(g_tr, 4096);
    {
        int ok = 1;
        static const unsigned lbs[] = { 4, 8, 16, 32, 64 };
        for (size_t k = 0; k < 5; k++)
            for (unsigned ws = 0; ws <= 9; ws++)
                if (fetch_run2((fcfg_t){ FETCH_PREFETCH, ws, lbs[k], false }, g_tr, 4096).cycles !=
                    seq_cycles_formula(4096, ws, lbs[k])) ok = 0;
        CHECK(ok);
    }
    CHECK(min_line_bytes_to_hide(0) == 4 && min_line_bytes_to_hide(3) == 16 &&
          min_line_bytes_to_hide(4) == 32 && min_line_bytes_to_hide(5) == 32 && min_line_bytes_to_hide(7) == 32);
    CHECK(fetch_run2((fcfg_t){ FETCH_PREFETCH, 5, 32, false }, g_tr, 4096).cycles == 6 + 4095);

    /* 연습 4: .data 복사 64 KB = 16384 워드, Flash 3 WS → SRAM 0 WS */
    {
        static unsigned ws[2 * 16384];
        for (size_t i = 0; i < 16384; i++) { ws[2 * i] = 3; ws[2 * i + 1] = 0; }
        uint64_t c = copy_cycles(16384, 3, 0);
        CHECK(c == 81921 && pipe_sim(ws, 2 * 16384) == c);
        CHECK(c * 1000000u / 120u / 1000000u == 682);           /* 120 MHz → 682.675 µs → 682 µs */
        CHECK(copy_cycles(16384, 0, 0) == 1 + 2 * 16384);      /* 원본이 SRAM 이면 2.5배 빠름 */
    }

    /* 연습 5: 클럭 변경 순서 (t_acc = 33.333 ns, 16 MHz 0 WS ↔ 168 MHz 5 WS) */
    {
        const uint64_t T = 33333, LO = 16000000u, HI = 168000000u;
        step_t p[2]; uint64_t f; unsigned lat;
        size_t n = plan_clock_change(T, LO, HI, p);
        CHECK(run_steps(T, LO, 0, p, n, &f, &lat) && f == HI && lat == 5);
        n = plan_clock_change(T, HI, LO, p);
        CHECK(run_steps(T, HI, 5, p, n, &f, &lat) && f == LO && lat == 0);
        step_t bad_up[2] = { { SET_CLOCK, HI }, { SET_LATENCY, 5 } };          /* 클럭 먼저 올림 */
        step_t bad_dn[2] = { { SET_LATENCY, 0 }, { SET_CLOCK, LO } };          /* LATENCY 먼저 내림 */
        CHECK(!run_steps(T, LO, 0, bad_up, 2, &f, &lat));
        CHECK(!run_steps(T, HI, 5, bad_dn, 2, &f, &lat));
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
