/* ARCH-M13 AHB/APB 개념: 파이프라인 버스와 브리지
 *
 *   Part A  AHB-Lite (매니저 1개) 사이클 모델: 주소 단계가 앞 전송의 데이터 단계와 겹친다.
 *           HTRANS IDLE/BUSY/NONSEQ/SEQ, HREADY=0 이 데이터 단계를 늘리고 다음 주소 단계를 붙잡는다.
 *   Part B  APB (APB3/4, PREADY 있음) 상태기계 IDLE → SETUP → ACCESS 와 AHB→APB 브리지:
 *           브리지는 APB 접근이 끝날 때까지 HREADYOUT=0 으로 AHB 를 세운다.
 *   Part C  PCLK = HCLK/div 와 브리지 응답 레지스터가 만드는 지연: 사이클 시뮬레이터와 같은 답을 내는
 *           트랜잭션 단위 예측식(TLM), 연속 접근 주기, 드라이버 시나리오(초기화 · 플래그 클리어 + 되읽기).
 *
 * 텍스트 파형: wave_format() (주어짐). VCD 출력은 demo_bus.c vcd.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================= 신호 · 요청 · 파형 기록 ================= */
enum { HT_IDLE = 0, HT_BUSY = 1, HT_NONSEQ = 2, HT_SEQ = 3 };       /* HTRANS[1:0] 인코딩 */

/* 매니저가 내는 "주소 단계 슬롯" 하나. 목록 순서대로 내고, HREADY=0 이면 같은 슬롯을 계속 낸다.
 * HSIZE 는 항상 워드(32비트)로 고정. wdata 는 이 전송의 데이터 단계에서 HWDATA 로 나간다. */
typedef struct { uint8_t htrans, hwrite; uint32_t haddr, wdata; } ahb_req_t;

typedef struct {                       /* 사이클 하나의 버스 상태 (파형 한 열) */
    /* AHB */
    uint8_t  htrans, hwrite, hready;
    uint32_t haddr;
    uint8_t  hwdata_v, hrdata_v;       /* 1 = 이 사이클 값이 의미 있음 */
    uint32_t hwdata, hrdata;
    /* APB */
    uint8_t  psel, penable, pwrite, pready, prdata_v;
    uint32_t paddr, pwdata, prdata;
    /* 추적용: 주소 단계 / 데이터 단계에 있는 요청 번호 (-1 = 없음) */
    int a_idx, d_idx;
} cyc_t;

/* ================= 메모리 맵 · 슬레이브 ================= */
#define SRAM_BASE 0x20000000u          /* AHB 직결 SRAM (wait state 는 설정값) */
#define APB_BASE  0x40000000u          /* AHB→APB 브리지 뒤의 주변장치 레지스터 */
#define REGION_SIZE 0x1000u
static uint32_t g_sram[REGION_SIZE / 4];
static uint32_t g_apb[REGION_SIZE / 4];
enum { R_NONE, R_SRAM, R_APB };
static int region_of(uint32_t a) {
    if (a - SRAM_BASE < REGION_SIZE) return R_SRAM;
    if (a - APB_BASE < REGION_SIZE) return R_APB;
    return R_NONE;                     /* 미매핑 (default slave · HRESP ERROR 는 Q&A) */
}
static uint32_t *word_at(uint32_t a) {
    return region_of(a) == R_SRAM ? &g_sram[(a - SRAM_BASE) / 4] : &g_apb[(a - APB_BASE) / 4];
}
static void mem_reset(void) { memset(g_sram, 0, sizeof g_sram); memset(g_apb, 0, sizeof g_apb); }

typedef struct {
    unsigned sram_ws;    /* SRAM 슬레이브가 HREADYOUT=0 으로 미는 사이클 수 */
    unsigned apb_ws;     /* APB 주변장치가 ACCESS 에서 PREADY=0 으로 미는 PCLK 사이클 수 */
    unsigned pclk_div;   /* PCLK = HCLK / div (1 = 같은 클럭). PCLK 엣지 = (c+1) % div == 0 인 사이클의 끝 */
    int      reg_resp;   /* 0 = PREADY/PRDATA 를 HREADYOUT/HRDATA 로 바로 넘김, 1 = 레지스터에 담고 다음 사이클 응답 */
} bus_cfg_t;

static bool pclk_edge(const bus_cfg_t *cfg, long c) {
    unsigned d = cfg->pclk_div ? cfg->pclk_div : 1u;
    return (unsigned long)(c + 1) % d == 0;
}

/* ================= Part B: APB 상태기계와 브리지 ================= */
typedef enum { APB_IDLE, APB_SETUP, APB_ACCESS } apb_state_t;

/* APB 스펙의 상태도 (PCLK 엣지마다 한 번):
 *   IDLE   --요청 있음--> SETUP, 없으면 IDLE
 *   SETUP  --항상--> ACCESS                (SETUP 은 정확히 1 PCLK)
 *   ACCESS --PREADY=0--> ACCESS (wait),  PREADY=1 이면 요청 있으면 SETUP, 없으면 IDLE */
static apb_state_t apb_next(apb_state_t s, bool req, bool pready) {
    (void)s; (void)req; (void)pready;
    return APB_IDLE; /* TODO: IDLE→SETUP(요청 시), SETUP→ACCESS, ACCESS→(PREADY? 요청? SETUP : IDLE : ACCESS) */
}

typedef struct { uint32_t addr, wdata; uint8_t write; } apb_xfer_t;
typedef struct {
    apb_state_t st;      /* APB 쪽 상태 (PCLK 엣지에서만 바뀐다) */
    bool pend;           /* AHB 에서 받아 둔, 아직 SETUP 을 시작 못 한 요청이 있나 */
    bool resp;           /* reg_resp=1: 이번 사이클이 "담아 둔 응답을 HREADYOUT=1 로 내는" 사이클 */
    apb_xfer_t cur, nxt; /* cur = 지금 APB 위의 전송, nxt = 대기 중 */
    unsigned wait_left;  /* 이번 ACCESS 에서 남은 PREADY=0 PCLK 사이클 */
    uint32_t rdata;      /* reg_resp 용으로 담아 둔 PRDATA */
} bridge_t;

/* 사이클 c 동안(엣지 전): 브리지 상태로 APB 신호를 cy 에 채우고 HREADYOUT 을 돌려준다.
 * (AHB 데이터 단계가 브리지 것일 때만 그 HREADYOUT 이 HREADY 가 된다) */
static int bridge_comb(const bridge_t *b, const bus_cfg_t *cfg, long c, cyc_t *cy) {
    (void)b; (void)cfg; (void)c; (void)cy;
    /* TODO: PSEL/PENABLE/PADDR/PWRITE/PWDATA, ACCESS 면 PREADY(wait_left==0)·PRDATA,
     *       HREADYOUT = resp 사이클이면 1, 통과형이면 ACCESS && PREADY && PCLK 엣지일 때 1 (HRDATA 도 채움) */
    return 0;
}

/* 사이클 c 의 끝(상승 엣지): acc != NULL 이면 이번 엣지에서 AHB 가 APB 영역 전송을 받아들였다.
 * PCLK 엣지이면 APB 상태를 apb_next 로 진행하고, ACCESS 가 PREADY=1 로 끝나면 레지스터 읽기/쓰기를 한다. */
static void bridge_edge(bridge_t *b, const bus_cfg_t *cfg, long c, const ahb_req_t *acc) {
    (void)b; (void)cfg; (void)c; (void)acc;
    /* TODO: resp 내리기 → acc 를 nxt 에 받아 두기(pend) → PCLK 엣지가 아니면 끝 →
     *       ACCESS 면 wait_left-- 또는 완료(레지스터 읽기/쓰기, reg_resp 면 resp=1) → apb_next → SETUP 진입 시 cur=nxt */
}

/* ================= Part A: AHB-Lite 사이클 루프 ================= */
enum { D_NONE, D_EMPTY, D_SRAM, D_BRIDGE };
#define SIM_MAX 100000L

/* 요청 n 개를 한 사이클씩 진행한다. 첫 주소 단계 = cycle 0. 반환 = 총 사이클 수
 * (마지막 데이터 단계가 끝나는 사이클 + 1), 미매핑 NONSEQ/SEQ = -1, 무한 루프 방지 = -2.
 * w 가 NULL 이 아니면 앞 cap 사이클을 기록. rd 가 NULL 이 아니면 읽기 요청 i 의 HRDATA 를 rd[i] 에.
 *   - 매 사이클: 데이터 단계(있으면)가 HREADY 를 정한다 (IDLE/BUSY 의 데이터 단계 = 0 wait OKAY)
 *   - 주소 버스에는 아직 받아들여지지 않은 첫 요청이 떠 있다 (없으면 HTRANS=IDLE)
 *   - HREADY=1 인 사이클 끝에: 데이터 단계 완료, 주소 단계의 요청이 받아들여져 다음 데이터 단계가 된다 */
static long sim_run(const bus_cfg_t *cfg, const ahb_req_t *rq, int n, cyc_t *w, long cap, uint32_t *rd) {
    (void)cfg; (void)rq; (void)n; (void)w; (void)cap; (void)rd; (void)word_at; (void)bridge_comb; (void)bridge_edge;
    /* TODO: 사이클 루프 — 데이터 단계(IDLE/BUSY=빈 단계, SRAM wait, 브리지 HREADYOUT)가 HREADY 를 정하고,
     *       주소 버스엔 아직 안 받아들여진 첫 요청, HREADY=1 인 엣지에서 주소 단계 → 데이터 단계 */
    return 0;
}

/* ================= Part C: 트랜잭션 단위 예측 (TLM) ================= */
/* 사이클 시뮬레이션 없이 "데이터 단계 길이" 만 더해서 총 사이클을 낸다. sim_run 과 항상 같아야 한다.
 * 요청 i 의 데이터 단계는 앞 데이터 단계가 끝난 다음 사이클 s 에 시작한다.
 *   IDLE/BUSY: 1 사이클 · SRAM: 1 + sram_ws
 *   APB: s 이후 첫 PCLK 경계 b (b % div == 0) 에서 SETUP, 끝 = b + div·(2 + apb_ws) − 1 (+1 reg_resp) */
static long tlm_cycles(const bus_cfg_t *cfg, const ahb_req_t *rq, int n) {
    (void)cfg; (void)rq; (void)n;
    return 0; /* TODO: end 를 따라가며 데이터 단계 길이 합산 (APB 는 PCLK 경계 정렬) */
}

/* APB 레지스터 연속 접근(back-to-back)의 정상 상태 주기 (HCLK 사이클 / 접근) */
static long apb_period(const bus_cfg_t *cfg) {
    (void)cfg;
    return 0; /* TODO: 연속 APB 접근 한 번의 HCLK 사이클 */
}

/* ================= 텍스트 파형 (주어짐) ================= */
static const char *ht_name(uint8_t t) {
    static const char *nm[4] = { "IDLE", "BUSY", "NSEQ", "SEQ" };
    return nm[t & 3];
}
static void fmt_addr(char *s, size_t n, uint32_t a) {   /* S = SRAM, P = APB, 하위 12비트 hex */
    int r = region_of(a);
    snprintf(s, n, "%c%03X", r == R_SRAM ? 'S' : r == R_APB ? 'P' : '?', (unsigned)(a & 0xFFFu));
}
/* 사이클 열, 신호 행. 값이 없는(don't care / 구동 안 함) 칸은 "-". 데이터는 하위 16비트 hex. */
static size_t wave_format(const cyc_t *w, long nc, int show_apb, char *out, size_t cap) {
    size_t len = 0;
    char cell[16];
#define EMIT(...) do { int k_ = snprintf(out + len, len < cap ? cap - len : 0, __VA_ARGS__); \
                       if (k_ > 0) len += (size_t)k_; } while (0)
    EMIT("%-7s|", "cycle");
    for (long i = 0; i < nc; i++) EMIT(" %4ld|", i);
    EMIT("\n");
    for (int row = 0; row < (show_apb ? 15 : 8); row++) {
        static const char *lab[15] = { "A-ph", "D-ph", "HTRANS", "HADDR", "HWRITE", "HWDATA", "HRDATA", "HREADY",
                                       "PSEL", "PENABLE", "PADDR", "PWRITE", "PWDATA", "PRDATA", "PREADY" };
        EMIT("%-7s|", lab[row]);
        for (long i = 0; i < nc; i++) {
            const cyc_t *y = &w[i];
            bool act = y->htrans == HT_NONSEQ || y->htrans == HT_SEQ;
            strcpy(cell, "-");
            switch (row) {
            case 0: if (y->a_idx >= 0) snprintf(cell, sizeof cell, "A%d", y->a_idx); break;
            case 1: if (y->d_idx >= 0) snprintf(cell, sizeof cell, "D%d", y->d_idx); break;
            case 2: snprintf(cell, sizeof cell, "%s", ht_name(y->htrans)); break;
            case 3: if (act || y->htrans == HT_BUSY) fmt_addr(cell, sizeof cell, y->haddr); break;
            case 4: if (act) snprintf(cell, sizeof cell, "%s", y->hwrite ? "W" : "R"); break;
            case 5: if (y->hwdata_v) snprintf(cell, sizeof cell, "%04X", (unsigned)(y->hwdata & 0xFFFFu)); break;
            case 6: if (y->hrdata_v) snprintf(cell, sizeof cell, "%04X", (unsigned)(y->hrdata & 0xFFFFu)); break;
            case 7: snprintf(cell, sizeof cell, "%u", y->hready); break;
            case 8: snprintf(cell, sizeof cell, "%u", y->psel); break;
            case 9: snprintf(cell, sizeof cell, "%u", y->penable); break;
            case 10: if (y->psel) fmt_addr(cell, sizeof cell, y->paddr); break;
            case 11: if (y->psel) snprintf(cell, sizeof cell, "%s", y->pwrite ? "W" : "R"); break;
            case 12: if (y->psel && y->pwrite) snprintf(cell, sizeof cell, "%04X", (unsigned)(y->pwdata & 0xFFFFu)); break;
            case 13: if (y->prdata_v) snprintf(cell, sizeof cell, "%04X", (unsigned)(y->prdata & 0xFFFFu)); break;
            case 14: if (y->psel && y->penable) snprintf(cell, sizeof cell, "%u", y->pready); break;
            }
            EMIT(" %4s|", cell);
        }
        EMIT("\n");
    }
#undef EMIT
    return len;
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static ahb_req_t W(uint32_t a, uint32_t d) { return (ahb_req_t){ HT_NONSEQ, 1, a, d }; }
static ahb_req_t R(uint32_t a)             { return (ahb_req_t){ HT_NONSEQ, 0, a, 0 }; }
static ahb_req_t IDLE(void)                { return (ahb_req_t){ HT_IDLE, 0, 0, 0 }; }

#define MAXR 256
static ahb_req_t g_rq[MAXR];
static cyc_t g_w[1024];
static uint32_t g_rd[MAXR];
static char g_txt[8192];

static const bus_cfg_t CFG0 = { 0, 0, 1, 0 };   /* 0 WS SRAM, 0 WS APB, PCLK = HCLK, 통과형 브리지 */

/* 여러 요청 (같은 종류) 만들기 */
static void fill_writes(ahb_req_t *r, int n, uint32_t base) {
    for (int i = 0; i < n; i++) r[i] = W(base + 4u * (uint32_t)(i % 64), 0x100u + (uint32_t)i);
}

int main(void) {
    /* ================= Part A: AHB-Lite 파이프라인 ================= */
    /* A1. SRAM 쓰기 4개 back-to-back, 0 WS → N + 1 = 5 사이클. 파형을 글자 그대로 비교 */
    mem_reset();
    for (int i = 0; i < 4; i++) g_rq[i] = W(SRAM_BASE + 4u * (uint32_t)i, 0xA0u + (uint32_t)i);
    long c = sim_run(&CFG0, g_rq, 4, g_w, 1024, NULL);
    CHECK(c == 5);
    wave_format(g_w, c, 0, g_txt, sizeof g_txt);
    CHECK(strcmp(g_txt,
        "cycle  |    0|    1|    2|    3|    4|\n"
        "A-ph   |   A0|   A1|   A2|   A3|    -|\n"
        "D-ph   |    -|   D0|   D1|   D2|   D3|\n"
        "HTRANS | NSEQ| NSEQ| NSEQ| NSEQ| IDLE|\n"
        "HADDR  | S000| S004| S008| S00C|    -|\n"
        "HWRITE |    W|    W|    W|    W|    -|\n"
        "HWDATA |    -| 00A0| 00A1| 00A2| 00A3|\n"
        "HRDATA |    -|    -|    -|    -|    -|\n"
        "HREADY |    1|    1|    1|    1|    1|\n") == 0);
    CHECK(g_sram[0] == 0xA0u && g_sram[3] == 0xA3u);

    /* A2. N 개 연속 쓰기: 0 WS → N+1, k WS → 1 + N(1+k)  (N = 1..64, k = 0..3) */
    {
        int ok = 1;
        for (unsigned k = 0; k <= 3; k++)
            for (int n = 1; n <= 64; n++) {
                bus_cfg_t cf = CFG0; cf.sram_ws = k;
                fill_writes(g_rq, n, SRAM_BASE);
                if (sim_run(&cf, g_rq, n, NULL, 0, NULL) != 1 + n * (1 + (long)k)) ok = 0;
            }
        CHECK(ok);
    }

    /* A3. 1 WS SRAM: 쓰기 W0, 읽기 R1(방금 쓴 주소), 쓰기 W2 — HREADY=0 동안 다음 주소·HWDATA 유지 */
    mem_reset();
    {
        bus_cfg_t cf = CFG0; cf.sram_ws = 1;
        g_rq[0] = W(SRAM_BASE + 0x10, 0x1234); g_rq[1] = R(SRAM_BASE + 0x10); g_rq[2] = W(SRAM_BASE + 0x14, 0xBEEF);
        c = sim_run(&cf, g_rq, 3, g_w, 1024, g_rd);
        CHECK(c == 7);
        wave_format(g_w, c, 0, g_txt, sizeof g_txt);
        CHECK(strcmp(g_txt,
            "cycle  |    0|    1|    2|    3|    4|    5|    6|\n"
            "A-ph   |   A0|   A1|   A1|   A2|   A2|    -|    -|\n"
            "D-ph   |    -|   D0|   D0|   D1|   D1|   D2|   D2|\n"
            "HTRANS | NSEQ| NSEQ| NSEQ| NSEQ| NSEQ| IDLE| IDLE|\n"
            "HADDR  | S010| S010| S010| S014| S014|    -|    -|\n"
            "HWRITE |    W|    R|    R|    W|    W|    -|    -|\n"
            "HWDATA |    -| 1234| 1234|    -|    -| BEEF| BEEF|\n"
            "HRDATA |    -|    -|    -|    -| 1234|    -|    -|\n"
            "HREADY |    1|    0|    1|    0|    1|    0|    1|\n") == 0);
        CHECK(g_rd[1] == 0x1234u && g_sram[5] == 0xBEEFu);
        /* 규칙 검사: HREADY=0 인 사이클 다음 사이클의 주소·제어는 그대로 */
        int held = 1;
        for (long i = 0; i + 1 < c; i++)
            if (!g_w[i].hready && (g_w[i + 1].haddr != g_w[i].haddr || g_w[i + 1].htrans != g_w[i].htrans ||
                                   g_w[i + 1].hwrite != g_w[i].hwrite || g_w[i + 1].hwdata != g_w[i].hwdata)) held = 0;
        CHECK(held);
    }

    /* A4. IDLE 슬롯: 데이터 단계는 1 사이클, 0 wait, HWDATA 없음.  W, IDLE, W → 4 사이클 */
    mem_reset();
    g_rq[0] = W(SRAM_BASE, 1); g_rq[1] = IDLE(); g_rq[2] = W(SRAM_BASE + 4, 2);
    c = sim_run(&CFG0, g_rq, 3, g_w, 1024, NULL);
    CHECK(c == 4 && g_w[2].d_idx == 1 && g_w[2].hready == 1 && g_w[2].hwdata_v == 0 && g_w[1].htrans == HT_IDLE);

    /* A5. INCR4 버스트 + BUSY: NSEQ a, SEQ a+4, BUSY a+8, SEQ a+8, SEQ a+C → 슬롯 5개 = 6 사이클.
     *     BUSY 는 "다음 전송 주소" 를 내고 데이터 단계는 비어 있다. 쓰기 4개만 메모리에 들어간다 */
    mem_reset();
    {
        uint32_t a = SRAM_BASE + 0x40;
        g_rq[0] = (ahb_req_t){ HT_NONSEQ, 1, a, 11 };      g_rq[1] = (ahb_req_t){ HT_SEQ, 1, a + 4, 12 };
        g_rq[2] = (ahb_req_t){ HT_BUSY, 1, a + 8, 0 };     g_rq[3] = (ahb_req_t){ HT_SEQ, 1, a + 8, 13 };
        g_rq[4] = (ahb_req_t){ HT_SEQ, 1, a + 12, 14 };
        c = sim_run(&CFG0, g_rq, 5, g_w, 1024, NULL);
        CHECK(c == 6 && g_w[3].d_idx == 2 && g_w[3].hwdata_v == 0 && g_w[2].htrans == HT_BUSY);
        CHECK(g_sram[0x10] == 11 && g_sram[0x11] == 12 && g_sram[0x12] == 13 && g_sram[0x13] == 14);
        /* 같은 버스트를 2 WS SRAM 에: BUSY 는 0 wait 이니 1 + 4·3 + 1 = 14 */
        bus_cfg_t cf = CFG0; cf.sram_ws = 2;
        CHECK(sim_run(&cf, g_rq, 5, NULL, 0, NULL) == 14);
    }

    /* A6. 미매핑 NONSEQ → -1, 하지만 미매핑 주소의 IDLE 은 문제 없음 */
    g_rq[0] = W(0x10000000u, 1);
    CHECK(sim_run(&CFG0, g_rq, 1, NULL, 0, NULL) == -1);
    g_rq[0] = (ahb_req_t){ HT_IDLE, 0, 0x10000000u, 0 };
    CHECK(sim_run(&CFG0, g_rq, 1, NULL, 0, NULL) == 2);

    /* ================= Part B: APB 상태기계와 브리지 ================= */
    /* B1. apb_next 전수 (3 상태 × req × pready) */
    {
        static const apb_state_t want[3][2][2] = {   /* [state][req][pready] */
            { { APB_IDLE, APB_IDLE }, { APB_SETUP, APB_SETUP } },
            { { APB_ACCESS, APB_ACCESS }, { APB_ACCESS, APB_ACCESS } },
            { { APB_ACCESS, APB_IDLE }, { APB_ACCESS, APB_SETUP } } };
        int ok = 1;
        for (int s = 0; s < 3; s++) for (int q = 0; q < 2; q++) for (int p = 0; p < 2; p++)
            if (apb_next((apb_state_t)s, q, p) != want[s][q][p]) ok = 0;
        CHECK(ok);
    }

    /* B2. APB 쓰기 1개: 주소 단계 1 + SETUP 1 + ACCESS 1 = 3 사이클 (SRAM 은 2) */
    mem_reset();
    g_rq[0] = W(APB_BASE + 0x8, 0x55);
    c = sim_run(&CFG0, g_rq, 1, g_w, 1024, NULL);
    CHECK(c == 3);
    CHECK(g_w[1].psel == 1 && g_w[1].penable == 0 && g_w[1].hready == 0);   /* SETUP */
    CHECK(g_w[2].psel == 1 && g_w[2].penable == 1 && g_w[2].pready == 1 && g_w[2].hready == 1);  /* ACCESS */
    CHECK(g_apb[2] == 0x55u);

    /* B3. APB 쓰기 2개 + 읽기 1개, PREADY 1 wait — 파형 글자 그대로 */
    mem_reset();
    {
        bus_cfg_t cf = CFG0; cf.apb_ws = 1;
        g_apb[1] = 0xC0DE;
        g_rq[0] = W(APB_BASE + 0x0, 0x11); g_rq[1] = W(APB_BASE + 0x8, 0x22); g_rq[2] = R(APB_BASE + 0x4);
        c = sim_run(&cf, g_rq, 3, g_w, 1024, g_rd);
        CHECK(c == 10);
        wave_format(g_w, c, 1, g_txt, sizeof g_txt);
        CHECK(strcmp(g_txt,
            "cycle  |    0|    1|    2|    3|    4|    5|    6|    7|    8|    9|\n"
            "A-ph   |   A0|   A1|   A1|   A1|   A2|   A2|   A2|    -|    -|    -|\n"
            "D-ph   |    -|   D0|   D0|   D0|   D1|   D1|   D1|   D2|   D2|   D2|\n"
            "HTRANS | NSEQ| NSEQ| NSEQ| NSEQ| NSEQ| NSEQ| NSEQ| IDLE| IDLE| IDLE|\n"
            "HADDR  | P000| P008| P008| P008| P004| P004| P004|    -|    -|    -|\n"
            "HWRITE |    W|    W|    W|    W|    R|    R|    R|    -|    -|    -|\n"
            "HWDATA |    -| 0011| 0011| 0011| 0022| 0022| 0022|    -|    -|    -|\n"
            "HRDATA |    -|    -|    -|    -|    -|    -|    -|    -|    -| C0DE|\n"
            "HREADY |    1|    0|    0|    1|    0|    0|    1|    0|    0|    1|\n"
            "PSEL   |    0|    1|    1|    1|    1|    1|    1|    1|    1|    1|\n"
            "PENABLE|    0|    0|    1|    1|    0|    1|    1|    0|    1|    1|\n"
            "PADDR  |    -| P000| P000| P000| P008| P008| P008| P004| P004| P004|\n"
            "PWRITE |    -|    W|    W|    W|    W|    W|    W|    R|    R|    R|\n"
            "PWDATA |    -| 0011| 0011| 0011| 0022| 0022| 0022|    -|    -|    -|\n"
            "PRDATA |    -|    -|    -|    -|    -|    -|    -|    -|    -| C0DE|\n"
            "PREADY |    -|    -|    0|    1|    -|    0|    1|    -|    0|    1|\n") == 0);
        CHECK(g_apb[0] == 0x11u && g_apb[2] == 0x22u && g_rd[2] == 0xC0DEu);
    }

    /* B4. APB 규칙: PENABLE=1 이면 PSEL=1, ACCESS 첫 사이클 직전은 같은 주소의 SETUP,
     *     PSEL 동안 PADDR/PWRITE/PWDATA 는 전송이 끝날 때까지 고정 */
    {
        int ok = 1;
        for (long i = 0; i < c; i++) {
            const cyc_t *y = &g_w[i];
            if (y->penable && !y->psel) ok = 0;
            if (y->penable && (i == 0 || !g_w[i - 1].psel || g_w[i - 1].paddr != y->paddr)) ok = 0;
            if (i > 0 && g_w[i - 1].penable && !g_w[i - 1].pready &&
                (y->paddr != g_w[i - 1].paddr || y->pwrite != g_w[i - 1].pwrite || y->pwdata != g_w[i - 1].pwdata ||
                 !y->penable)) ok = 0;
        }
        CHECK(ok);
    }

    /* B5. N 개 연속 APB 쓰기: 통과형 1 + N(2+w), 응답 레지스터형 1 + N(3+w)  (N 1..16, w 0..3) */
    {
        int ok = 1;
        for (int reg = 0; reg <= 1; reg++)
            for (unsigned wv = 0; wv <= 3; wv++)
                for (int n = 1; n <= 16; n++) {
                    bus_cfg_t cf = { 0, wv, 1, reg };
                    fill_writes(g_rq, n, APB_BASE);
                    if (sim_run(&cf, g_rq, n, NULL, 0, NULL) != 1 + n * (2 + (long)wv + reg)) ok = 0;
                }
        CHECK(ok);
    }

    /* B6. 응답 레지스터형 브리지: ACCESS 가 끝난 다음 사이클에 HREADY=1, 그 사이클 PSEL=0 */
    mem_reset();
    {
        bus_cfg_t cf = { 0, 0, 1, 1 };
        g_apb[3] = 0x77;
        g_rq[0] = R(APB_BASE + 0xC); g_rq[1] = R(APB_BASE + 0xC);
        c = sim_run(&cf, g_rq, 2, g_w, 1024, g_rd);
        CHECK(c == 7 && g_rd[0] == 0x77u && g_rd[1] == 0x77u);
        CHECK(g_w[2].penable == 1 && g_w[2].hready == 0 && g_w[3].psel == 0 && g_w[3].hready == 1);
    }

    /* B7. 섞기: SRAM W, APB W, SRAM W → 1 + 1 + 2 + 1 = 5. APB 레지스터 쓰기 뒤 되읽기 */
    mem_reset();
    g_rq[0] = W(SRAM_BASE, 1); g_rq[1] = W(APB_BASE + 0x20, 0xAB); g_rq[2] = W(SRAM_BASE + 4, 3);
    CHECK(sim_run(&CFG0, g_rq, 3, NULL, 0, NULL) == 5);
    g_rq[0] = W(APB_BASE + 0x20, 0xCD); g_rq[1] = R(APB_BASE + 0x20);
    CHECK(sim_run(&CFG0, g_rq, 2, NULL, 0, g_rd) == 5 && g_rd[1] == 0xCDu);

    /* ================= Part C: PCLK 분주 · 예측식 · 드라이버 시나리오 ================= */
    /* C1. PCLK = HCLK/2: 쓰기 1개 = 6 사이클 (경계 맞추기 1 + SETUP 2 + ACCESS 2). 앞에 IDLE 하나 두면 같은 6 */
    {
        bus_cfg_t cf = { 0, 0, 2, 0 };
        g_rq[0] = W(APB_BASE, 1);
        c = sim_run(&cf, g_rq, 1, g_w, 1024, NULL);
        CHECK(c == 6 && g_w[1].psel == 0 && g_w[2].psel == 1 && g_w[3].penable == 0 && g_w[4].penable == 1);
        g_rq[0] = IDLE(); g_rq[1] = W(APB_BASE, 1);
        CHECK(sim_run(&cf, g_rq, 2, NULL, 0, NULL) == 6);       /* IDLE 사이클이 경계 대기에 흡수됨 */
        cf.pclk_div = 4;
        g_rq[0] = W(APB_BASE, 1);
        CHECK(sim_run(&cf, g_rq, 1, NULL, 0, NULL) == 12);      /* 4 + 4 + 4 */
    }

    /* C2. 연속 접근 주기 = apb_period, sim 의 기울기와 같다 (div 1/2/4, w 0..2, reg 0/1) */
    {
        int ok = 1;
        static const unsigned divs[3] = { 1, 2, 4 };
        for (int di = 0; di < 3; di++) for (unsigned wv = 0; wv <= 2; wv++) for (int reg = 0; reg <= 1; reg++) {
            bus_cfg_t cf = { 0, wv, divs[di], reg };
            fill_writes(g_rq, 9, APB_BASE);
            long c8 = sim_run(&cf, g_rq, 8, NULL, 0, NULL), c9 = sim_run(&cf, g_rq, 9, NULL, 0, NULL);
            if (c9 - c8 != apb_period(&cf)) ok = 0;
        }
        CHECK(ok);
        bus_cfg_t a = { 0, 0, 1, 0 }, b = { 0, 1, 4, 1 };
        CHECK(apb_period(&a) == 2 && apb_period(&b) == 16);
    }

    /* C3. TLM 예측식 == 사이클 시뮬레이터: 결정적 무작위 600 시퀀스 (xorshift 고정 시드) */
    {
        uint32_t seed = 0xA13u;
        int ok = 1, nonzero = 1;
        static const unsigned divs[3] = { 1, 2, 4 };
        for (int t = 0; t < 600; t++) {
            bus_cfg_t cf = { xs32(&seed) % 3u, xs32(&seed) % 4u, divs[xs32(&seed) % 3u], (int)(xs32(&seed) & 1u) };
            int n = 1 + (int)(xs32(&seed) % 24u);
            for (int i = 0; i < n; i++) {
                uint32_t k = xs32(&seed) % 6u, off = 4u * (xs32(&seed) % 16u);
                if (k == 0) g_rq[i] = IDLE();
                else if (k == 1) g_rq[i] = W(SRAM_BASE + off, i);
                else if (k == 2) g_rq[i] = R(SRAM_BASE + off);
                else if (k == 3) g_rq[i] = W(APB_BASE + off, i);
                else if (k == 4) g_rq[i] = R(APB_BASE + off);
                else g_rq[i] = (ahb_req_t){ HT_BUSY, 0, SRAM_BASE, 0 };
            }
            long s = sim_run(&cf, g_rq, n, NULL, 0, NULL), m = tlm_cycles(&cf, g_rq, n);
            if (s != m) ok = 0;
            if (s <= n) nonzero = 0;
        }
        CHECK(ok && nonzero);
        CHECK(tlm_cycles(&CFG0, g_rq, 0) == 0);
        g_rq[0] = W(0x10000000u, 1);
        CHECK(tlm_cycles(&CFG0, g_rq, 1) == -1);
    }

    /* C4. 드라이버 초기화: 주변장치 레지스터 8개 쓰기. AHB(SRAM 자리) 9, APB /1 17, /2 34, /4 68 */
    {
        long cyc[4];
        fill_writes(g_rq, 8, SRAM_BASE);
        cyc[0] = sim_run(&CFG0, g_rq, 8, NULL, 0, NULL);
        fill_writes(g_rq, 8, APB_BASE);
        static const unsigned divs[3] = { 1, 2, 4 };
        for (int i = 0; i < 3; i++) { bus_cfg_t cf = { 0, 0, divs[i], 0 }; cyc[i + 1] = sim_run(&cf, g_rq, 8, NULL, 0, NULL); }
        CHECK(cyc[0] == 9 && cyc[1] == 17 && cyc[2] == 34 && cyc[3] == 68);
    }

    /* C5. ISR 끝: 플래그 클리어 쓰기 + 같은 레지스터 되읽기 (PCLK/2, 응답 레지스터형).
     *     되읽기는 쓰기가 APB 에서 끝난 뒤에야 시작 → 클리어가 주변장치에 "도착했음" 을 보장 */
    mem_reset();
    {
        bus_cfg_t cf = { 0, 0, 2, 1 };
        g_apb[4] = 0x1;                            /* SR: 플래그 1 */
        g_rq[0] = W(APB_BASE + 0x10, 0x0); g_rq[1] = R(APB_BASE + 0x10);
        c = sim_run(&cf, g_rq, 2, g_w, 1024, g_rd);
        CHECK(c == tlm_cycles(&cf, g_rq, 2) && c == 13 && g_rd[1] == 0x0u);
        long write_commit = -1, read_setup = -1;
        for (long i = 0; i < c; i++) {
            if (g_w[i].psel && g_w[i].penable && g_w[i].pwrite && g_w[i].pready && pclk_edge(&cf, i)) write_commit = i;
            if (read_setup < 0 && g_w[i].psel && !g_w[i].pwrite) read_setup = i;
        }
        CHECK(write_commit == 5 && read_setup == 8 && read_setup > write_commit);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
