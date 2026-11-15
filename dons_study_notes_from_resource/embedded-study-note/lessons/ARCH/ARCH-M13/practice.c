/* ARCH-M13 추가 연습 — 한 번에 검증
 *
 *   연습 1  posted-write 브리지: AHB 는 1 사이클에 "끝났다" 고 하는데 APB 쓰기는 한참 뒤 — ISR 플래그 클리어 함정
 *   연습 2  AHB/APB 프로토콜 체커: 본문 시뮬레이터 파형은 통과, 일부러 망가뜨린 파형은 규칙 번호로 잡는다
 *   연습 3  SEQ 를 아는 슬레이브와 버스트: NONSEQ 2 WS / SEQ 0 WS, 1 KB 경계에서 버스트 쪼개기
 *   연습 4  PCLK 위상: APB 접근 1회 지연의 최소/최대 닫힌 식 vs 시뮬레이터
 *
 * 본문 모델(solution.c)을 그대로 가져다 쓴다 (main 만 이름을 바꿔 포함).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#define main solution_main
#include "solution.c"
#undef main

static long align_up(long x, long d) { return (x + d - 1) / d * d; }

/* ================= 연습 1: posted-write 브리지 (트랜잭션 단위) =================
 * 브리지에 쓰기 1개짜리 버퍼가 있다. APB 쓰기는 브리지가 비어 있으면 데이터 단계 첫 사이클에 바로
 * HREADYOUT=1 (AHB 입장에서는 "끝"), 실제 APB SETUP/ACCESS 는 그 뒤에 진행된다.
 * 브리지가 바쁘면(앞 쓰기가 아직 APB 위) 다음 APB 전송은 브리지가 빌 때까지 HREADY=0.
 * 읽기는 posted 될 수 없다(데이터가 있어야 끝난다) → 통과형과 같다. SRAM 전송은 브리지와 무관. */
typedef struct { long ahb_end; long commit; } post_t;   /* commit = APB 쓰기가 PREADY=1 로 끝나는 사이클 */

static long posted_run(const bus_cfg_t *cf, const ahb_req_t *rq, int n, post_t *out) {
    long d = cf->pclk_div ? (long)cf->pclk_div : 1, end = 0, bfree = 0;
    for (int i = 0; i < n; i++) {
        long s = end + 1;
        out[i].commit = -1;
        int r = region_of(rq[i].haddr);
        if (rq[i].htrans == HT_IDLE || rq[i].htrans == HT_BUSY) end = s;
        else if (r == R_SRAM) end = s + (long)cf->sram_ws;
        else if (r == R_APB && rq[i].hwrite) {
            long take = s > bfree ? s : bfree;              /* 브리지가 받아 줄 수 있는 첫 사이클 */
            long b = align_up(take, d);                     /* APB SETUP 은 PCLK 경계에서 */
            end = take;
            out[i].commit = b + d * (2 + (long)cf->apb_ws) - 1;
            bfree = out[i].commit + 1;
        } else if (r == R_APB) {
            long b = align_up(s > bfree ? s : bfree, d);
            end = b + d * (2 + (long)cf->apb_ws) - 1;
            bfree = end + 1;
        } else return -1;
        out[i].ahb_end = end;
    }
    return n > 0 ? end + 1 : 0;
}

/* ISR 이 플래그 클리어 쓰기(AHB 완료 = ahb_end) 후 exit_cycles 만에 예외 복귀하고, 그 순간 IRQ 선이
 * 아직 1 이면 다시 pending → 재진입. IRQ 선은 commit 다음 사이클부터 0 이 된다고 가정(모델). */
static bool reentry_risk(long ahb_end, long commit, long exit_cycles) { return commit + 1 > ahb_end + exit_cycles; }

/* ================= 연습 2: 프로토콜 체커 ================= */
enum {
    V_AHB_HOLD   = 1 << 0,   /* HREADY=0 다음 사이클에 주소·제어가 바뀜 */
    V_HWDATA     = 1 << 1,   /* 쓰기 데이터 단계가 늘어나는 동안 HWDATA 가 바뀜 */
    V_IDLE_WAIT  = 1 << 2,   /* IDLE/BUSY 의 데이터 단계에 wait (슬레이브는 0 wait OKAY 여야 함) */
    V_PEN_NOSEL  = 1 << 3,   /* PENABLE=1 인데 PSEL=0 */
    V_NO_SETUP   = 1 << 4,   /* ACCESS 앞에 같은 주소의 SETUP(PSEL=1) 이 없음 */
    V_APB_HOLD   = 1 << 5,   /* PREADY=0 인 ACCESS 다음 사이클에 PADDR/PWRITE/PWDATA 가 바뀌거나 ACCESS 가 끝남 */
    V_PEN_STUCK  = 1 << 6,   /* PREADY=1 로 끝난 PCLK 엣지 다음에도 PENABLE=1 (SETUP 을 건너뛴 연속 ACCESS) */
};
static unsigned check_trace(const cyc_t *w, long nc, const bus_cfg_t *cf) {
    unsigned v = 0;
    for (long i = 0; i < nc; i++) {
        const cyc_t *y = &w[i], *z = i + 1 < nc ? &w[i + 1] : NULL;
        if (z && !y->hready && (z->htrans != y->htrans || z->haddr != y->haddr || z->hwrite != y->hwrite)) v |= V_AHB_HOLD;
        if (z && !y->hready && y->hwdata_v && (!z->hwdata_v || z->hwdata != y->hwdata)) v |= V_HWDATA;
        if (z && y->hready && (y->htrans == HT_IDLE || y->htrans == HT_BUSY) && y->a_idx >= 0 && !z->hready) v |= V_IDLE_WAIT;
        if (y->penable && !y->psel) v |= V_PEN_NOSEL;
        if (y->penable && (i == 0 || !w[i - 1].psel || w[i - 1].paddr != y->paddr || w[i - 1].pwrite != y->pwrite)) v |= V_NO_SETUP;
        if (z && y->psel && y->penable && !y->pready &&
            (!z->penable || z->paddr != y->paddr || z->pwrite != y->pwrite || z->pwdata != y->pwdata)) v |= V_APB_HOLD;
        if (z && y->psel && y->penable && y->pready && pclk_edge(cf, i) && z->penable) v |= V_PEN_STUCK;
    }
    return v;
}

/* ================= 연습 3: SEQ 를 아는 슬레이브와 버스트 ================= */
/* 워드 읽기 n 개를 burst_len 비트씩 INCR 버스트로 낸다 (1 = 전부 NONSEQ). AHB 버스트는 1 KB 경계를
 * 넘으면 안 되므로, 경계에 닿으면 거기서 새 NONSEQ 로 다시 시작한다. 반환 = 슬롯 수 */
static int make_incr(ahb_req_t *r, uint32_t base, int n, int burst_len) {
    int k = 0, in_burst = 0;
    for (int i = 0; i < n; i++) {
        uint32_t a = base + 4u * (uint32_t)i;
        bool start = in_burst == 0 || (a & 0x3FFu) == 0;
        r[k++] = (ahb_req_t){ (uint8_t)(start ? HT_NONSEQ : HT_SEQ), 0, a, 0 };
        in_burst = start ? 1 : in_burst + 1;
        if (in_burst == burst_len) in_burst = 0;
    }
    return k;
}
/* 슬레이브: NONSEQ 는 ws_n, SEQ 는 ws_s wait, IDLE/BUSY 는 0 wait. 파이프라인 = 1 + Σ(1 + wait) */
static long seq_slave_cycles(const ahb_req_t *r, int n, unsigned ws_n, unsigned ws_s) {
    long c = n > 0 ? 1 : 0;
    for (int i = 0; i < n; i++)
        c += 1 + (r[i].htrans == HT_NONSEQ ? (long)ws_n : r[i].htrans == HT_SEQ ? (long)ws_s : 0);
    return c;
}

/* ================= 연습 4: PCLK 위상과 1회 지연 ================= */
/* 주소 단계 사이클 t 에 낸 APB 접근 1개의 지연(주소 단계부터 완료 사이클까지, 양 끝 포함) 범위 */
static void apb_latency_range(long d, long w, long *mn, long *mx) {
    *mn = 1 + d * (2 + w);      /* t ≡ d−1 (mod d): 다음 사이클이 바로 PCLK 경계 */
    *mx = d * (3 + w);          /* t ≡ 0   (mod d): 경계까지 d 사이클 */
}

static cyc_t p_w[512];

int main(void) {
    /* ---------- 연습 1 ---------- */
    {
        post_t o[8];
        bus_cfg_t cf = { 0, 0, 1, 0 };
        ahb_req_t rq[8];
        rq[0] = W(APB_BASE, 1);
        CHECK(posted_run(&cf, rq, 1, o) == 2 && o[0].ahb_end == 1 && o[0].commit == 2);   /* 통과형은 3 */
        cf.apb_ws = 3;
        CHECK(posted_run(&cf, rq, 1, o) == 2 && o[0].commit == 5);
        /* APB 쓰기 뒤 SRAM 3개: 브리지가 APB 를 처리하는 동안 AHB 는 계속 간다 */
        rq[1] = W(SRAM_BASE, 1); rq[2] = W(SRAM_BASE + 4, 2); rq[3] = W(SRAM_BASE + 8, 3);
        CHECK(posted_run(&cf, rq, 4, o) == 5 && sim_run(&cf, rq, 4, NULL, 0, NULL) == 1 + 5 + 3);
        /* APB 쓰기 연속: 지연은 숨기지만 처리량(주기 2+w)은 못 바꾼다 */
        cf.apb_ws = 0;
        for (int i = 0; i < 8; i++) rq[i] = W(APB_BASE + 4u * (uint32_t)i, (uint32_t)i);
        posted_run(&cf, rq, 8, o);
        CHECK(o[0].ahb_end == 1 && o[1].ahb_end == 3 && o[7].ahb_end == 15 && o[7].commit == 16);
        CHECK(sim_run(&cf, rq, 8, NULL, 0, NULL) == 17);
        /* 쓰기 직후 같은 레지스터 되읽기: 읽기는 쓰기가 APB 에서 끝난 뒤 끝난다 → 순서 보장 */
        rq[0] = W(APB_BASE + 0x10, 0); rq[1] = R(APB_BASE + 0x10);
        cf.pclk_div = 4;
        posted_run(&cf, rq, 2, o);
        CHECK(o[0].ahb_end == 1 && o[0].commit == 11 && o[1].ahb_end == 19 && o[1].ahb_end > o[0].commit);
        /* ISR 탈출이 쓰기 AHB 완료 6 사이클 뒤라면: PCLK/4 posted 는 위험, PCLK/1 통과형은 안전 */
        CHECK(reentry_risk(o[0].ahb_end, o[0].commit, 6) == true);
        CHECK(reentry_risk(2, 2, 6) == false);
    }

    /* ---------- 연습 2 ---------- */
    {
        static ahb_req_t rq[8];
        bus_cfg_t cfs[3] = { { 1, 1, 1, 0 }, { 0, 1, 2, 1 }, { 2, 0, 4, 0 } };
        int clean = 1;
        for (int k = 0; k < 3; k++) {
            rq[0] = W(SRAM_BASE, 1); rq[1] = W(APB_BASE + 8, 0x22); rq[2] = IDLE(); rq[3] = R(APB_BASE + 8);
            rq[4] = (ahb_req_t){ HT_NONSEQ, 0, SRAM_BASE, 0 }; rq[5] = (ahb_req_t){ HT_BUSY, 0, SRAM_BASE + 4, 0 };
            rq[6] = (ahb_req_t){ HT_SEQ, 0, SRAM_BASE + 4, 0 }; rq[7] = W(APB_BASE + 4, 0x33);
            long nc = sim_run(&cfs[k], rq, 8, p_w, 512, NULL);
            if (check_trace(p_w, nc, &cfs[k]) != 0) clean = 0;
        }
        CHECK(clean);   /* 본문 시뮬레이터 파형은 규칙 위반 0 */

        bus_cfg_t cf = { 1, 1, 1, 0 };
        rq[0] = W(SRAM_BASE, 1); rq[1] = W(APB_BASE + 8, 0x22); rq[2] = IDLE(); rq[3] = R(APB_BASE + 8);
        long nc = sim_run(&cf, rq, 4, p_w, 512, NULL);
        static cyc_t m[512];
        long iw = -1, ia = -1, isu = -1, iidle = -1;          /* 망가뜨릴 위치 찾기 */
        for (long i = 0; i < nc; i++) {
            if (iw < 0 && !p_w[i].hready && p_w[i].a_idx >= 0) iw = i;
            if (ia < 0 && p_w[i].penable && !p_w[i].pready) ia = i;
            if (isu < 0 && p_w[i].psel && !p_w[i].penable) isu = i;
            if (iidle < 0 && p_w[i].htrans == HT_IDLE && p_w[i].a_idx >= 0 && p_w[i].hready) iidle = i;   /* IDLE 이 받아들여지는 사이클 */
        }
        memcpy(m, p_w, sizeof m); m[iw + 1].haddr += 4;                      CHECK(check_trace(m, nc, &cf) == V_AHB_HOLD);
        memcpy(m, p_w, sizeof m); m[1].hwdata ^= 1;  /* SRAM 1 WS 쓰기의 두 번째 사이클 */
        CHECK(p_w[0].hready == 1 && p_w[1].hready == 0 && check_trace(m, nc, &cf) == V_HWDATA);
        memcpy(m, p_w, sizeof m); m[iidle + 1].hready = 0;
        CHECK((check_trace(m, nc, &cf) & V_IDLE_WAIT) != 0);
        memcpy(m, p_w, sizeof m); m[ia].psel = 0;
        CHECK((check_trace(m, nc, &cf) & V_PEN_NOSEL) != 0);
        memcpy(m, p_w, sizeof m); m[isu].psel = 0; m[isu].paddr = 0;          /* SETUP 을 지움 */
        CHECK(check_trace(m, nc, &cf) == V_NO_SETUP);
        memcpy(m, p_w, sizeof m); m[ia + 1].pwdata ^= 0x100;
        CHECK((check_trace(m, nc, &cf) & V_APB_HOLD) != 0);
        memcpy(m, p_w, sizeof m);                                           /* ACCESS 끝난 다음 사이클도 PENABLE=1 */
        for (long i = 0; i + 1 < nc; i++) if (m[i].penable && m[i].pready) { m[i + 1].penable = 1; break; }
        CHECK((check_trace(m, nc, &cf) & V_PEN_STUCK) != 0);
    }

    /* ---------- 연습 3 ---------- */
    {
        static ahb_req_t r[64];
        int n1 = make_incr(r, SRAM_BASE, 16, 1);
        CHECK(n1 == 16 && seq_slave_cycles(r, n1, 2, 0) == 1 + 16 * 3);           /* 49 */
        int n4 = make_incr(r, SRAM_BASE, 16, 4);
        CHECK(n4 == 16 && r[4].htrans == HT_NONSEQ && r[5].htrans == HT_SEQ && seq_slave_cycles(r, n4, 2, 0) == 25);
        int n16 = make_incr(r, SRAM_BASE, 16, 16);
        CHECK(seq_slave_cycles(r, n16, 2, 0) == 19);
        /* SEQ 정보를 안 쓰는 슬레이브(항상 2 WS)에게는 버스트가 이득 0 */
        CHECK(seq_slave_cycles(r, n16, 2, 2) == seq_slave_cycles(r, n1, 2, 2));
        /* 1 KB 경계: 0x3F0 부터 8 워드 INCR8 → 0x400 에서 새 NONSEQ */
        int nb = make_incr(r, SRAM_BASE + 0x3F0, 8, 8);
        int nonseq = 0;
        for (int i = 0; i < nb; i++) nonseq += r[i].htrans == HT_NONSEQ;
        CHECK(nonseq == 2 && r[4].haddr == SRAM_BASE + 0x400 && r[4].htrans == HT_NONSEQ);
        /* 본문 시뮬레이터(SEQ 를 모르는 SRAM 1 WS)로는 버스트든 아니든 같다 */
        bus_cfg_t cf = { 1, 0, 1, 0 };
        long a = sim_run(&cf, r, make_incr(r, SRAM_BASE, 16, 4), NULL, 0, NULL);
        long b = sim_run(&cf, r, make_incr(r, SRAM_BASE, 16, 1), NULL, 0, NULL);
        CHECK(a == b && a == 33);
    }

    /* ---------- 연습 4 ---------- */
    {
        int ok = 1;
        static const long divs[4] = { 1, 2, 4, 8 };
        ahb_req_t rq[16];
        for (int di = 0; di < 4; di++)
            for (long w = 0; w <= 2; w++) {
                long d = divs[di], mn, mx, smin = 1L << 30, smax = 0;
                bus_cfg_t cf = { 0, (unsigned)w, (unsigned)d, 0 };
                for (int k = 0; k < 2 * (int)d && k < 15; k++) {        /* IDLE k 개 뒤에 APB 쓰기 → 주소 단계 cycle k */
                    for (int i = 0; i < k; i++) rq[i] = IDLE();
                    rq[k] = W(APB_BASE, 1);
                    long lat = sim_run(&cf, rq, k + 1, NULL, 0, NULL) - k;
                    if (lat < smin) smin = lat;
                    if (lat > smax) smax = lat;
                }
                apb_latency_range(d, w, &mn, &mx);
                if (mn != smin || mx != smax) ok = 0;
            }
        CHECK(ok);
        long mn, mx;
        apb_latency_range(4, 0, &mn, &mx);
        CHECK(mn == 9 && mx == 12);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
