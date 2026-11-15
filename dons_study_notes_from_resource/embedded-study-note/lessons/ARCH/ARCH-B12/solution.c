/* ARCH-B12 버스 트랜잭션 기초: 대기 상태와 처리량
 *
 *   Part A  사이클 단위 동기 버스: 주소 단계 + 데이터 단계 + wait state(HREADY=0)
 *           비파이프라인(APB 스타일) / 파이프라인(AHB 스타일) → 총 사이클 · 사이클별 파형 · 대역폭
 *   Part B  면접 함정: 클럭 → WS (정수 ps, 경계 포함), WS → 최대 클럭, 클럭을 올렸는데 느려지는 톱니,
 *           바이트 전송, 파이프라인 이득이 WS 에 따라 줄어드는 것
 *   Part C  플래시 명령 페치: 버퍼 없음 / 128비트 라인 버퍼 / 라인 버퍼 + 순차 프리페치,
 *           순차 · 루프 · 먼 점프 · 분기 많은 트레이스 비교
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ================= 공통: 메모리 맵 → wait state (디코더 자체는 ARCH-B11) ================= */
typedef struct { const char *name; uint32_t base, size; unsigned ws; } region_t;
static const region_t g_map[] = {
    { "FLASH",  0x08000000u, 0x00100000u, 3 },   /* 예: 120 MHz 에서 3 WS (Part B 표 참고) */
    { "SRAM",   0x20000000u, 0x00020000u, 0 },
    { "PERIPH", 0x40000000u, 0x00100000u, 1 },   /* 모델 값. 실제는 브리지·주변장치마다 다르다 */
};
#define FLASH_BASE 0x08000000u
#define SRAM_BASE  0x20000000u

static int ws_of(uint32_t addr) {
    for (size_t i = 0; i < sizeof g_map / sizeof g_map[0]; i++)
        if (addr - g_map[i].base < g_map[i].size) return (int)g_map[i].ws;
    return -1;                                     /* 미매핑 → bus fault 는 ARCH-B11 몫 */
}

/* ================= Part A: 사이클 단위 버스 모델 ================= */
typedef enum { BUS_NONPIPE, BUS_PIPE } bus_mode_t;
typedef struct { uint32_t addr; uint8_t write; uint8_t size; } xfer_t;   /* size = 1, 2, 4 바이트 */
typedef struct {
    int addr_owner;   /* 이 사이클 주소 버스에 주소를 내고 있는 전송 번호, -1 = idle   */
    int data_owner;   /* 이 사이클 데이터 단계에 있는 전송 번호,          -1 = 없음   */
    int hready;       /* 1 = 이 사이클 끝에 데이터 단계가 끝남(또는 데이터 단계 없음), 0 = wait */
} cycle_t;

/* 전송 n 개를 한 사이클씩 진행한다. wave 가 NULL 이 아니면 앞 cap 사이클을 기록한다.
 * 반환: 총 사이클 수, 미매핑 주소가 있으면 -1.
 *   BUS_NONPIPE: 주소 단계(1) → 데이터 단계(1 + WS) → 그 다음에야 다음 주소   = Σ(2 + WS)
 *   BUS_PIPE   : 다음 전송의 주소 단계가 지금 전송의 데이터 단계와 겹친다       = 1 + Σ(1 + WS)
 *                (wait 동안 다음 주소는 버스에 "붙잡혀(held)" 있다) */
static long bus_run(bus_mode_t mode, const xfer_t *x, size_t n, cycle_t *wave, size_t cap) {
    for (size_t i = 0; i < n; i++) if (ws_of(x[i].addr) < 0) return -1;
    long c = 0;
    size_t next = 0;          /* 아직 주소 단계를 끝내지 못한 첫 전송 */
    int d = -1;               /* 데이터 단계에 있는 전송, -1 = 없음 */
    unsigned rem = 0;         /* 그 전송에 남은 wait 사이클 */
    while (next < n || d >= 0) {
        cycle_t cy = { -1, d, 1 };
        bool addr_done = false;
        if (mode == BUS_PIPE || d < 0)
            if (next < n) cy.addr_owner = (int)next;
        if (d >= 0) {
            if (rem > 0) { cy.hready = 0; rem--; }
            else { d = -1; }                               /* 이 사이클 끝에 데이터 완료 */
        }
        /* 주소 단계는 HREADY=1 인 사이클 끝에 받아들여진다 */
        if (cy.addr_owner >= 0 && cy.hready) addr_done = true;
        if (addr_done) { d = (int)next; rem = (unsigned)ws_of(x[next].addr); next++; }
        if (wave && (size_t)c < cap) wave[c] = cy;
        c++;
    }
    return c;
}

static uint64_t total_bytes(const xfer_t *x, size_t n) {
    uint64_t b = 0;
    for (size_t i = 0; i < n; i++) b += x[i].size;
    return b;
}
static double bw_bytes_per_cycle(uint64_t bytes, long cycles) {
    return cycles > 0 ? (double)bytes / (double)cycles : 0.0;
}
static double bw_mb_per_s(uint64_t bytes, long cycles, double f_hz) {
    return bw_bytes_per_cycle(bytes, cycles) * f_hz / 1e6;    /* MB = 10^6 바이트 */
}

/* ================= Part B: 클럭과 wait state ================= */
#define PS_PER_S 1000000000000ull
/* 접근 시간 t_acc 를 덮는 데 필요한 사이클 = ceil(t_acc × f), WS = 사이클 − 1 (최소 0).
 * 부동소수점 금지 — 30 ns × 100 MHz 같은 "딱 맞는" 경계에서 ceil 이 4 로 튈 수 있다. */
static unsigned ws_for_clock(uint64_t t_acc_ps, uint64_t f_hz) {
    uint64_t cyc = (t_acc_ps * f_hz + PS_PER_S - 1) / PS_PER_S;
    return cyc > 1 ? (unsigned)(cyc - 1) : 0u;
}
/* 주어진 WS 로 쓸 수 있는 최대 클럭(Hz, 정수 내림): (WS+1) 사이클 ≥ t_acc × f */
static uint64_t max_clock_for_ws(uint64_t t_acc_ps, unsigned ws) {
    return ((uint64_t)ws + 1u) * PS_PER_S / t_acc_ps;
}
/* 캐시·프리페치 없이 32비트 워드를 플래시에서 연속으로 읽을 때 (파이프라인 버스, 정상 상태) MB/s */
static double flash_word_mbps(uint64_t t_acc_ps, uint64_t f_hz) {
    unsigned ws = ws_for_clock(t_acc_ps, f_hz);
    return 4.0 * (double)f_hz / (double)(ws + 1u) / 1e6;
}

/* ================= Part C: 플래시 명령 페치 (라인 버퍼 / 프리페치) ================= */
#define FLASH_LINE_BYTES 16u   /* 128비트 플래시 라인 = 32비트 페치 4개 */
typedef enum { FETCH_NOBUF, FETCH_LINEBUF, FETCH_PREFETCH } fetch_mode_t;
typedef struct { uint64_t cycles, fetches, hits, pf_hits, misses, stall; } fetch_stats_t;

static uint64_t u64max(uint64_t a, uint64_t b) { return a > b ? a : b; }

/* CPU 는 4바이트 페치를 하나씩, 앞 페치가 끝나야 다음을 낸다 (실행 스톨은 모델하지 않음).
 * L = WS + 1 = 플래시 배열 읽기 한 번의 사이클.
 *   NOBUF   : 모든 페치가 플래시 읽기 (L 사이클)
 *   LINEBUF : 마지막으로 읽은 16 B 라인을 보관. 같은 라인이면 1 사이클, 아니면 L 사이클
 *   PREFETCH: LINEBUF + 다음 순차 라인 1개를 미리 읽는다.
 *             - CPU 가 라인 X 를 쓰기 시작하면(미스 완료 시점 / 프리페치 라인을 가져가는 시점)
 *               플래시가 비는 대로 X+1 읽기 시작
 *             - 프리페치 라인 요청: 완료 = max(t+1, pf_ready)
 *             - 그 밖의 라인(분기): 프리페치 버퍼 버림(flush). 진행 중인 플래시 읽기는
 *               중단할 수 없다고 가정 → 플래시가 빌 때까지 기다린 뒤 L 사이클 */
static fetch_stats_t fetch_run(fetch_mode_t mode, unsigned ws, const uint32_t *addr, size_t n) {
    fetch_stats_t s = {0};
    const uint64_t L = (uint64_t)ws + 1u;
    uint64_t t = 0, flash_free = 0, pf_ready = 0;
    int64_t cur = -1, pf = -1;
    for (size_t i = 0; i < n; i++) {
        int64_t line = (int64_t)(addr[i] / FLASH_LINE_BYTES);
        s.fetches++;
        if (mode != FETCH_NOBUF && line == cur) {
            t += 1; s.hits++;
        } else if (mode == FETCH_PREFETCH && line == pf) {
            uint64_t t_req = t;
            t = u64max(t + 1, pf_ready); s.pf_hits++;
            cur = line; pf = line + 1;
            pf_ready = u64max(flash_free, t_req) + L;
            flash_free = pf_ready;
        } else {
            t = u64max(t, flash_free) + L; s.misses++;
            flash_free = t;
            cur = (mode == FETCH_NOBUF) ? -1 : line;
            if (mode == FETCH_PREFETCH) { pf = line + 1; pf_ready = flash_free = t + L; }
            else pf = -1;
        }
    }
    s.cycles = t;
    s.stall = t - s.fetches;
    return s;
}

/* ---------- 트레이스 생성기 (주어짐) ---------- */
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static void gen_seq(uint32_t *a, size_t n, uint32_t base) {
    for (size_t i = 0; i < n; i++) a[i] = base + 4u * (uint32_t)i;
}
static void gen_loop(uint32_t *a, size_t n, uint32_t base, unsigned body_words) {
    for (size_t i = 0; i < n; i++) a[i] = base + 4u * (uint32_t)(i % body_words);
}
static void gen_far(uint32_t *a, size_t n, uint32_t base, unsigned stride_lines) {
    for (size_t i = 0; i < n; i++) a[i] = base + (uint32_t)i * stride_lines * FLASH_LINE_BYTES;
}
/* 1..8 개 순차 실행 후 64 KB 안 임의 워드로 점프 (분기 많은 코드 흉내) */
static void gen_branchy(uint32_t *a, size_t n, uint32_t base, uint32_t seed) {
    uint32_t s = seed, pc = base;
    size_t i = 0;
    while (i < n) {
        unsigned run = 1u + xs32(&s) % 8u;
        for (unsigned k = 0; k < run && i < n; k++, i++) { a[i] = pc; pc += 4u; }
        pc = base + 4u * (xs32(&s) % (65536u / 4u));
    }
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-6)

#define NW 256
static xfer_t g_x[NW * 2];
static cycle_t g_w[64];
static uint32_t g_tr[4096];

static void fill_reads(xfer_t *x, size_t n, uint32_t base, uint8_t size) {
    for (size_t i = 0; i < n; i++) x[i] = (xfer_t){ base + (uint32_t)i * size, 0, size };
}

int main(void) {
    /* ---------- Part A: 파형과 총 사이클 ---------- */
    /* SRAM 읽기 1개, 파이프라인: 주소 1 + 데이터 1 = 2 사이클 (지연) */
    fill_reads(g_x, 1, SRAM_BASE, 4);
    CHECK(bus_run(BUS_PIPE, g_x, 1, g_w, 64) == 2);
    CHECK(g_w[0].addr_owner == 0 && g_w[0].data_owner == -1 && g_w[0].hready == 1);
    CHECK(g_w[1].addr_owner == -1 && g_w[1].data_owner == 0 && g_w[1].hready == 1);

    /* Flash 읽기 1개 (3 WS): 주소 1 + 데이터 1+3 = 5, HREADY = 1 0 0 0 1 */
    fill_reads(g_x, 1, FLASH_BASE, 4);
    CHECK(bus_run(BUS_PIPE, g_x, 1, g_w, 64) == 5);
    CHECK(g_w[1].hready == 0 && g_w[2].hready == 0 && g_w[3].hready == 0 && g_w[4].hready == 1);
    CHECK(g_w[1].data_owner == 0 && g_w[4].data_owner == 0);

    /* Flash 다음 SRAM (파이프라인): SRAM 주소는 flash wait 동안 붙잡혀 있다 → 1 + 4 + 1 = 6 */
    g_x[0] = (xfer_t){ FLASH_BASE, 0, 4 };
    g_x[1] = (xfer_t){ SRAM_BASE, 0, 4 };
    CHECK(bus_run(BUS_PIPE, g_x, 2, g_w, 64) == 6);
    CHECK(g_w[1].addr_owner == 1 && g_w[4].addr_owner == 1 && g_w[5].addr_owner == -1);
    CHECK(g_w[5].data_owner == 1 && g_w[5].hready == 1);
    /* 같은 두 전송, 비파이프라인: (2+3) + (2+0) = 7, 주소는 데이터가 끝난 뒤에야 */
    CHECK(bus_run(BUS_NONPIPE, g_x, 2, g_w, 64) == 7);
    CHECK(g_w[1].addr_owner == -1 && g_w[5].addr_owner == 1 && g_w[6].data_owner == 1);

    /* 256 워드 읽기 — 공식과 사이클 시뮬레이션이 같아야 한다 */
    fill_reads(g_x, NW, SRAM_BASE, 4);
    long sram_p = bus_run(BUS_PIPE, g_x, NW, NULL, 0), sram_n = bus_run(BUS_NONPIPE, g_x, NW, NULL, 0);
    fill_reads(g_x, NW, FLASH_BASE, 4);
    long fl_p = bus_run(BUS_PIPE, g_x, NW, NULL, 0), fl_n = bus_run(BUS_NONPIPE, g_x, NW, NULL, 0);
    CHECK(sram_p == 1 + NW * 1 && sram_n == NW * 2);
    CHECK(fl_p == 1 + NW * 4 && fl_n == NW * 5);
    /* 대역폭: 1024 B */
    CHECK(total_bytes(g_x, NW) == 1024);
    CHECK(NEAR(bw_bytes_per_cycle(1024, sram_p), 1024.0 / 257.0));
    CHECK(NEAR(bw_bytes_per_cycle(1024, fl_p), 1024.0 / 1025.0));
    CHECK(NEAR(bw_mb_per_s(1024, sram_n, 120e6), 240.0));
    CHECK(NEAR(bw_mb_per_s(1024, fl_n, 120e6), 96.0));
    CHECK(bw_mb_per_s(1024, sram_p, 120e6) > 478.0 && bw_mb_per_s(1024, fl_p, 120e6) < 120.0);
    CHECK(bus_run(BUS_PIPE, (xfer_t[]){ { 0x10000000u, 0, 4 } }, 1, NULL, 0) == -1);   /* 미매핑 */

    /* ---------- Part B: 클럭 → WS, 경계, 톱니 ---------- */
    CHECK(ws_for_clock(30000, 100000000) == 2);     /* 30 ns × 100 MHz = 정확히 3 사이클 → WS 2 */
    CHECK(ws_for_clock(30001, 100000000) == 3);     /* 1 ps 만 넘어도 4 사이클 */
    CHECK(ws_for_clock(30000, 1000000) == 0);       /* 느린 클럭: 0 WS (음수 아님) */
    /* t_acc ≈ 33.3 ns 로 STM32F4 (2.7–3.6 V) 표가 재현된다: 30 MHz 마다 1 WS, 168 MHz → 5 WS */
    {
        static const struct { uint64_t mhz; unsigned ws; } f4[] = {
            {24,0},{30,0},{31,1},{60,1},{61,2},{90,2},{91,3},{120,3},{121,4},{150,4},{151,5},{168,5} };
        int ok = 1;
        for (size_t i = 0; i < sizeof f4 / sizeof f4[0]; i++)
            if (ws_for_clock(33333, f4[i].mhz * 1000000u) != f4[i].ws) ok = 0;
        CHECK(ok);
    }
    /* 역함수: max_clock_for_ws 는 그 WS 가 버티는 마지막 Hz, +1 Hz 는 WS+1 */
    {
        int ok = 1;
        static const uint64_t taccs[] = { 20000, 25000, 33333, 35000, 41667, 50000 };
        for (size_t i = 0; i < sizeof taccs / sizeof taccs[0]; i++)
            for (unsigned w = 0; w <= 8; w++) {
                uint64_t f = max_clock_for_ws(taccs[i], w);
                if (ws_for_clock(taccs[i], f) != w || ws_for_clock(taccs[i], f + 1) != w + 1) ok = 0;
            }
        CHECK(ok);
    }
    /* 톱니: 30 MHz → 31 MHz 로 올렸더니 캐시 없는 플래시 워드 읽기 대역폭이 거의 절반 */
    CHECK(NEAR(flash_word_mbps(33333, 30000000), 120.0));
    CHECK(NEAR(flash_word_mbps(33333, 31000000), 62.0));
    CHECK(flash_word_mbps(33333, 168000000) < flash_word_mbps(33333, 120000000));   /* 112 < 120 */

    /* 바이트 전송: 같은 64 B 를 바이트로 읽으면 사이클은 같은 비율로 늘고 B/cycle 은 1 미만 */
    fill_reads(g_x, 64, SRAM_BASE, 1);
    long b_cyc = bus_run(BUS_PIPE, g_x, 64, NULL, 0);
    fill_reads(g_x, 16, SRAM_BASE, 4);
    long w_cyc = bus_run(BUS_PIPE, g_x, 16, NULL, 0);
    CHECK(b_cyc == 65 && w_cyc == 17);
    CHECK(bw_bytes_per_cycle(64, b_cyc) < 1.0 && bw_bytes_per_cycle(64, w_cyc) > 3.7);

    /* 파이프라인 이득 = 비파이프/파이프 → WS 가 클수록 작아진다 (0 WS ≈ 2배, 3 WS ≈ 1.25배) */
    fill_reads(g_x, NW, SRAM_BASE, 4);
    double gain0 = (double)bus_run(BUS_NONPIPE, g_x, NW, NULL, 0) / (double)bus_run(BUS_PIPE, g_x, NW, NULL, 0);
    fill_reads(g_x, NW, FLASH_BASE, 4);
    double gain3 = (double)bus_run(BUS_NONPIPE, g_x, NW, NULL, 0) / (double)bus_run(BUS_PIPE, g_x, NW, NULL, 0);
    CHECK(gain0 > 1.99 && gain3 > 1.24 && gain3 < 1.25);

    /* ---------- Part C: 플래시 페치 ---------- */
    const size_t N = 1024;
    gen_seq(g_tr, N, FLASH_BASE);
    for (unsigned ws = 0; ws <= 7; ws++) {
        uint64_t L = ws + 1u, period = L > 4 ? L : 4;
        fetch_stats_t a = fetch_run(FETCH_NOBUF, ws, g_tr, N);
        fetch_stats_t b = fetch_run(FETCH_LINEBUF, ws, g_tr, N);
        fetch_stats_t c = fetch_run(FETCH_PREFETCH, ws, g_tr, N);
        if (ws == 3) {   /* 대표값을 따로 확인 */
            CHECK(a.cycles == 4096 && b.cycles == 1792 && c.cycles == 1027);
            CHECK(b.hits == 768 && b.misses == 256 && c.misses == 1 && c.pf_hits == 255);
        }
        CHECK(a.cycles == N * L && b.cycles == (N / 4) * (L + 3) &&
              c.cycles == L + (N / 4 - 1) * period + 3);
    }
    /* 168 MHz / 5 WS: 128비트 라인(4 페치)으로는 6 사이클을 못 가린다 → 1.5 cycle/fetch */
    {
        fetch_stats_t c = fetch_run(FETCH_PREFETCH, 5, g_tr, N);
        CHECK(c.cycles == 6 * (N / 4) + 3);
    }
    /* 8 워드 루프 (라인 2개), 3 WS: 되돌아가는 분기마다 미스 + 쓸모없는 프리페치가 플래시를 1 사이클 붙잡음 */
    gen_loop(g_tr, 800, FLASH_BASE, 8);
    {
        fetch_stats_t a = fetch_run(FETCH_NOBUF, 3, g_tr, 800);
        fetch_stats_t b = fetch_run(FETCH_LINEBUF, 3, g_tr, 800);
        fetch_stats_t c = fetch_run(FETCH_PREFETCH, 3, g_tr, 800);
        CHECK(a.cycles == 32 * 100 && b.cycles == 14 * 100 && c.cycles == 12 * 100 - 1);
        CHECK(c.misses == 100 && c.pf_hits == 100);
    }
    /* 매번 먼 라인으로 점프: 프리페치가 오히려 2배 손해 (진행 중 읽기를 못 끊는 모델) */
    gen_far(g_tr, 200, FLASH_BASE, 10);
    {
        fetch_stats_t b = fetch_run(FETCH_LINEBUF, 3, g_tr, 200);
        fetch_stats_t c = fetch_run(FETCH_PREFETCH, 3, g_tr, 200);
        CHECK(b.cycles == 200 * 4 && c.cycles == 2 * 4 * 200 - 4);
    }
    /* 분기 많은 트레이스: 불변식 */
    gen_branchy(g_tr, 4096, FLASH_BASE, 0xB12u);
    {
        int ok = 1;
        for (unsigned ws = 0; ws <= 5; ws++) {
            uint64_t L = ws + 1u;
            fetch_stats_t a = fetch_run(FETCH_NOBUF, ws, g_tr, 4096);
            fetch_stats_t b = fetch_run(FETCH_LINEBUF, ws, g_tr, 4096);
            fetch_stats_t c = fetch_run(FETCH_PREFETCH, ws, g_tr, 4096);
            if (a.cycles != 4096 * L) ok = 0;
            if (b.cycles != b.hits + b.misses * L || b.cycles > a.cycles) ok = 0;
            if (c.hits + c.pf_hits + c.misses != 4096 || c.cycles < 4096) ok = 0;
            if (c.stall != c.cycles - c.fetches) ok = 0;
        }
        CHECK(ok);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
