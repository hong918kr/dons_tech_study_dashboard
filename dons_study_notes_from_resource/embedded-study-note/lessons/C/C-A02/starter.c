/* C-A02 컴파일러 배리어와 메모리 순서: volatile만으론 부족한 이유
 *
 * DMA 디스크립터를 채우고 OWN 비트를 세우는 코드를 세 층으로 나눠 결정적으로 테스트한다.
 *   Part A — 언어(C11) 층: 필드를 채운 뒤 OWN 을 release 로 게시, 회수는 acquire.
 *            진짜 pthread "DMA 엔진" 이 링을 소비한다 (올바른 코드면 매 실행 항상 PASS).
 *   Part B — CPU 층: 약한 메모리 순서 시뮬레이터 (store buffer + 이른 load, 고정 시드 PRNG).
 *            배리어 자리가 틀리면 같은 시드에서 항상 같은 위반이 나온다.
 *   Part C — 드라이버 층: TX 링 + 캐시 clean + doorbell (mock 레지스터, 이벤트 trace).
 * 컴파일러가 실제로 어떤 명령어를 내는지는 demo_order.c (arm64 / Cortex-M4 asm) 에서 본다.
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define OWN 0x80000000u    /* bit31: 1 = DMA 소유 (STM32 ETH TDES0 / stmmac 과 같은 자리) */
#define LS  0x20000000u    /* last segment  */
#define FS  0x10000000u    /* first segment */

static uint32_t g_rng;
static uint32_t xs32(void) {                  /* xorshift32 — 고정 시드로 재현 가능 */
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}

/* ======================= Part A: C11 게시(publish) / 회수(reap) ======================= */
typedef struct {
    _Atomic uint32_t ctrl;      /* OWN|FS|LS — CPU 와 DMA 가 소유권을 주고받는 워드        */
    uint32_t len;               /* CPU 가 쓴다 (OWN 전에)                                   */
    const uint8_t *buf;         /* CPU 가 쓴다 (실제 칩에선 uint32_t 물리 주소)              */
    uint32_t status;            /* DMA 가 되써 준다 (OWN 을 내리기 전에). 0 = OK            */
} desc_t;
#define A_MAXLEN 64u

/* TODO(A1): d 가 아직 DMA 소유(OWN)이거나 len 이 0 또는 A_MAXLEN 초과면 아무것도 안 쓰고 -1.
 * 아니면 buf, len 을 쓰고 status 를 0 으로 지운 뒤, "마지막에" ctrl = OWN|FS|LS 를 게시하고 0.
 * 어떤 memory_order 를 어디에 써야 DMA 엔진(다른 스레드)이 OWN 을 본 순간 필드도 보이는가? */
static int desc_publish(desc_t *d, const uint8_t *buf, uint32_t len) {
    (void)d; (void)buf; (void)len;
    return -1; /* TODO */
}
/* TODO(A2): OWN 이 서 있으면 -1. 아니면 DMA 가 써 둔 status 를 돌려준다 (status 는 OWN 을 본 "뒤에" 읽어야 한다). */
static int32_t desc_reap(desc_t *d) {
    (void)d;
    return -1; /* TODO */
}

/* ---- 주어진 코드: 별도 스레드로 도는 DMA 엔진 (실제 칩의 DMA 마스터 역할) ---- */
#define RING_A 8
#define NPKT   100000u
static desc_t g_ring[RING_A];
static uint8_t g_txbuf[RING_A][A_MAXLEN];
static atomic_bool g_stop;
static uint32_t g_eng_done, g_eng_bad;        /* 엔진만 쓰고, main 은 join 뒤에 읽는다 */

static uint8_t pat(uint32_t seq, uint32_t k) { return (uint8_t)(seq * 31u + k * 7u); }
static uint32_t pkt_len(uint32_t seq) { return 8u + seq % (A_MAXLEN - 7u); }   /* 8..64 */
static void pkt_fill(uint8_t *b, uint32_t seq) {
    uint32_t n = pkt_len(seq);
    memcpy(b, &seq, 4);
    for (uint32_t k = 4; k < n; k++) b[k] = pat(seq, k);
}
static void *dma_engine(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < NPKT; i++) {
        desc_t *d = &g_ring[i % RING_A];
        uint32_t c;
        while (!((c = atomic_load_explicit(&d->ctrl, memory_order_acquire)) & OWN))
            if (atomic_load_explicit(&g_stop, memory_order_relaxed)) return NULL;
        uint32_t n = d->len, seq = 0;
        const uint8_t *b = d->buf;
        bool ok = (c & (FS | LS)) == (FS | LS) && n == pkt_len(i) && b == g_txbuf[i % RING_A];
        if (ok) {
            memcpy(&seq, b, 4);
            ok = seq == i;
            for (uint32_t k = 4; ok && k < n; k++) ok = b[k] == pat(i, k);
        }
        d->status = ok ? 0u : 0xBADu;
        g_eng_bad += !ok;
        g_eng_done++;
        atomic_store_explicit(&d->ctrl, 0u, memory_order_release);   /* 소유권 반환 */
    }
    return NULL;
}
/* main 쪽 생산자: 링을 돌며 회수 → 버퍼 채우기 → 게시. 0 = 성공 */
static int run_threaded(uint32_t *cpu_bad) {
    pthread_t th;
    memset(g_ring, 0, sizeof g_ring);
    g_eng_done = g_eng_bad = 0;
    atomic_store(&g_stop, false);
    if (pthread_create(&th, NULL, dma_engine, NULL) != 0) return -1;
    int timeout = 0;
    for (uint32_t i = 0; i < NPKT + RING_A && !timeout; i++) {
        desc_t *d = &g_ring[i % RING_A];
        if (i >= RING_A) {                               /* 같은 칸의 이전 패킷 회수 */
            int32_t st;
            unsigned long spins = 0;
            while ((st = desc_reap(d)) == -1)
                if (++spins > 400000000ul) { timeout = 1; break; }
            if (!timeout && st != 0) ++*cpu_bad;
        }
        if (i >= NPKT || timeout) continue;
        pkt_fill(g_txbuf[i % RING_A], i);
        if (desc_publish(d, g_txbuf[i % RING_A], pkt_len(i)) != 0) ++*cpu_bad;
    }
    atomic_store(&g_stop, true);
    pthread_join(th, NULL);
    return timeout ? -1 : 0;
}

/* ======================= Part B: 약한 메모리 순서 시뮬레이터 ======================= */
/* 장치(DMA)와 CPU 가 공유하는 워드 6개. wm_mem = 장치가 보는(=메모리에 도달한) 값.
 * CPU store 는 store buffer 에 들어갔다가 임의 순서로 메모리에 도달한다 (같은 주소끼리는 순서 유지).
 * wm_wmb() 이전의 store 는 이후의 store 보다 먼저 도달한다 (= dma_wmb / DMB ST).
 * CPU load 는 [하한, 지금] 사이 "아무 시점" 의 값을 읽을 수 있다 (= 이른 load / 투기 실행).
 * wm_rmb() 는 하한을 "앞선 load 들이 본 가장 늦은 시점" 으로 올린다 (= dma_rmb / DMB LD).
 * wm_cbar() 는 컴파일러 배리어: 명령어가 없으므로 이 모델에서 아무 일도 안 한다.
 * 교육용 동작 모델일 뿐 ARMv8 공리 모델 전체(의존성, multi-copy atomicity)는 아니다. */
enum { W_CTRL, W_LEN, W_ADDR, W_STAT, W_BUF0, W_BUF1, W_N };
typedef struct { int a; uint32_t v; uint32_t ep; } wm_sbe_t;
static wm_sbe_t wm_sb[32];
static int wm_sbn;
static uint32_t wm_ep, wm_mem[W_N], wm_init[W_N];
static bool wm_dev_saw;                         /* TX: 장치가 OWN 을 본 적 있나        */
static uint32_t wm_snap[W_N];                   /* TX: 그 순간 장치가 읽은 디스크립터  */
typedef struct { uint32_t t; int a; uint32_t v; } wm_w_t;
static wm_w_t wm_hist[8], wm_script[4];         /* RX: 장치가 쓴 기록 / 쓸 예정         */
static int wm_hn, wm_sn, wm_si;
static uint32_t wm_now, wm_floor, wm_maxobs, wm_last[W_N];

static void wm_reset(uint32_t seed) {
    g_rng = seed * 2654435761u + 1u;
    wm_sbn = 0; wm_ep = 0; wm_dev_saw = false;
    wm_hn = wm_sn = wm_si = 0; wm_now = wm_floor = wm_maxobs = 0;
    memset(wm_last, 0, sizeof wm_last);
    memcpy(wm_mem, wm_init, sizeof wm_mem);
}
static void wm_dev_tx_poll(void) {              /* 장치는 OWN 을 보면 디스크립터를 읽어 간다 */
    if (!wm_dev_saw && (wm_mem[W_CTRL] & OWN)) { wm_dev_saw = true; memcpy(wm_snap, wm_mem, sizeof wm_mem); }
}
static void wm_commit_one(void) {               /* store buffer → 메모리, 허용된 것 중 아무거나 */
    int cand[32], nc = 0;
    for (int k = 0; k < wm_sbn; k++) {
        bool ok = true;
        for (int j = 0; j < k; j++)
            if (wm_sb[j].ep != wm_sb[k].ep || wm_sb[j].a == wm_sb[k].a) ok = false;
        if (ok) cand[nc++] = k;
    }
    if (nc == 0) return;
    int k = cand[xs32() % (uint32_t)nc];
    wm_mem[wm_sb[k].a] = wm_sb[k].v;
    memmove(&wm_sb[k], &wm_sb[k + 1], (size_t)(wm_sbn - k - 1) * sizeof wm_sb[0]);
    wm_sbn--;
    wm_dev_tx_poll();
}
static void wm_dev_rx_step(void) {              /* 장치가 예정된 쓰기를 프로그램 순서대로 하나 수행 */
    if (wm_si >= wm_sn) return;
    wm_hist[wm_hn] = wm_script[wm_si++];
    wm_hist[wm_hn].t = ++wm_now;
    wm_mem[wm_hist[wm_hn].a] = wm_hist[wm_hn].v;
    wm_hn++;
}
static void wm_tick(void) {                     /* CPU 명령어 하나 사이에 0~2 단계의 "세상" 진행 */
    for (uint32_t k = xs32() % 3u; k; k--) {
        if (wm_sbn) wm_commit_one();
        wm_dev_rx_step();
    }
}
static uint32_t wm_value_at(int a, uint32_t t) {
    uint32_t v = wm_init[a];
    for (int h = 0; h < wm_hn; h++) if (wm_hist[h].a == a && wm_hist[h].t <= t) v = wm_hist[h].v;
    return v;
}
static void wm_st(int a, uint32_t v) {
    if (wm_sbn == 32) wm_commit_one();
    wm_sb[wm_sbn++] = (wm_sbe_t){ a, v, wm_ep };
    wm_tick();
}
static uint32_t wm_ld(int a) {
    wm_tick();
    uint32_t lo = wm_floor > wm_last[a] ? wm_floor : wm_last[a];   /* 같은 주소는 시간이 뒤로 안 간다 */
    uint32_t t = lo + xs32() % (wm_now - lo + 1u);
    wm_last[a] = t;
    if (t > wm_maxobs) wm_maxobs = t;
    return wm_value_at(a, t);
}
static void wm_wmb(void)  { wm_ep++; wm_tick(); }                 /* dma_wmb()  — DMB ST    */
static void wm_rmb(void)  { wm_floor = wm_maxobs; wm_tick(); }    /* dma_rmb()  — DMB LD    */
static void wm_cbar(void) { }                                     /* barrier()  — 명령어 없음 */

#define WM_ADDR(n) (0x20000000u + 64u * (n))
#define WM_B0(n)   (0xA5000000u | (n))
#define WM_B1(n)   (0x5A000000u | (n))
/* 주어진 버그 버전 (재현용) */
static void wm_tx_post_naive(uint32_t n) {
    wm_st(W_BUF0, WM_B0(n)); wm_st(W_BUF1, WM_B1(n));
    wm_st(W_ADDR, WM_ADDR(n)); wm_st(W_LEN, n);
    wm_st(W_CTRL, OWN | FS | LS);
}
static void wm_tx_post_cbar(uint32_t n) {       /* asm volatile("":::"memory") 만 넣은 버전 */
    wm_st(W_BUF0, WM_B0(n)); wm_st(W_BUF1, WM_B1(n));
    wm_st(W_ADDR, WM_ADDR(n)); wm_st(W_LEN, n);
    wm_cbar();
    wm_st(W_CTRL, OWN | FS | LS);
}
static int wm_rx_reap_naive(uint32_t *len, uint32_t *stat) {
    if (wm_ld(W_CTRL) & OWN) return -1;
    *len = wm_ld(W_LEN);
    *stat = wm_ld(W_STAT);
    return 0;
}
/* TODO(B1): 버퍼 두 워드, ADDR, LEN 을 쓰고 마지막에 CTRL = OWN|FS|LS. 장치가 OWN 을 본 순간
 * 나머지가 전부 보이도록 wm_st / wm_wmb 로 작성한다. (값: WM_B0(n), WM_B1(n), WM_ADDR(n), n) */
static void wm_tx_post(uint32_t n) {
    (void)n; (void)wm_wmb; /* TODO */
}
/* TODO(B2): CTRL 을 읽어 OWN 이면 -1. 아니면 LEN, STAT 을 읽어 *len, *stat 에 넣고 0.
 * 장치는 LEN → STAT → CTRL(OWN 해제) 순서로 쓴다. wm_ld / wm_rmb 로 작성한다. */
static int wm_rx_reap(uint32_t *len, uint32_t *stat) {
    (void)len; (void)stat; (void)wm_rmb;
    return -1; /* TODO */
}

#define WM_SEEDS 2000u
static unsigned wm_count_tx(void (*post)(uint32_t)) {
    unsigned bad = 0;
    memset(wm_init, 0, sizeof wm_init);
    for (uint32_t s = 1; s <= WM_SEEDS; s++) {
        uint32_t n = 1u + s % 60u;
        wm_reset(s);
        post(n);
        while (wm_sbn) wm_commit_one();         /* 남은 store 를 다 내보낸다 */
        bad += !wm_dev_saw || wm_snap[W_LEN] != n || wm_snap[W_ADDR] != WM_ADDR(n)
            || wm_snap[W_BUF0] != WM_B0(n) || wm_snap[W_BUF1] != WM_B1(n);
    }
    return bad;
}
static unsigned wm_count_rx(int (*reap)(uint32_t *, uint32_t *)) {
    unsigned bad = 0;
    memset(wm_init, 0, sizeof wm_init);
    wm_init[W_CTRL] = OWN | FS | LS; wm_init[W_LEN] = 0xDEADu; wm_init[W_STAT] = 0xDEADu;   /* 낡은 값 */
    for (uint32_t s = 1; s <= WM_SEEDS; s++) {
        uint32_t n = 1u + s % 60u, len = 0, stat = 0;
        wm_reset(s);
        wm_script[0] = (wm_w_t){ 0, W_LEN, n };
        wm_script[1] = (wm_w_t){ 0, W_STAT, 0u };
        wm_script[2] = (wm_w_t){ 0, W_CTRL, 0u };
        wm_sn = 3;
        int r = -1;
        for (int tries = 0; tries < 64 && r != 0; tries++) r = reap(&len, &stat);
        bad += r != 0 || len != n || stat != 0u;
    }
    return bad;
}

/* ======================= Part C: TX 링 드라이버 (mock 하드웨어) ======================= */
#define RING 4u
#define BUF_PA(i) (0x30040000u + 64u * (i))      /* DMA 가 보는 버퍼 물리 주소 (가짜) */
typedef struct { volatile uint32_t ctrl, len, addr, stat; } hwdesc_t;
static hwdesc_t c_desc[RING];                  /* 디스크립터: MPU 로 non-cacheable 지정한 SRAM 가정 */
static uint8_t  c_cache[RING][64];             /* CPU 가 쓰는 버퍼 = D-cache 에 있는 값              */
static uint8_t  c_ram[RING][64];               /* DMA 가 읽는 버퍼 = 실제 RAM 에 있는 값             */
static volatile uint32_t TX_TAIL;              /* doorbell 레지스터: 여기까지 보냈다 (free-running) */
static uint32_t c_head, c_tail, c_ok, c_err;   /* 드라이버 SW 상태 (free-running 인덱스)            */
static uint32_t c_dev_idx;                     /* DMA 가 다음에 볼 디스크립터 번호                   */
static uint8_t  c_log[64][64];                 /* DMA 가 실제로 전송한 프레임                         */
static uint32_t c_loglen[64], c_nlog;
static int c_fail_next;                        /* 테스트용: 다음 프레임을 에러로 완료 */
static char c_trace[512];

static void ctrace(const char *s) {
    size_t n = strlen(c_trace);
    if (n && n + 1 < sizeof c_trace) c_trace[n++] = ' ';
    snprintf(c_trace + n, sizeof c_trace - n, "%s", s);
}
/* ---- 주어진 하드웨어 접근 함수 (타깃에서는 주석의 코드) ---- */
static void buf_fill(uint32_t i, const uint8_t *src, uint32_t n) { memcpy(c_cache[i], src, n); ctrace("CPY"); }
static void cache_clean(uint32_t i, uint32_t n) {   /* SCB_CleanDCache_by_Addr() — 끝에 DSB 포함 */
    memcpy(c_ram[i], c_cache[i], n); ctrace("CLEAN");
}
static void d_set_addr(hwdesc_t *d, uint32_t v) { d->addr = v; ctrace("addr"); }
static void d_set_len(hwdesc_t *d, uint32_t v)  { d->len = v;  ctrace("len"); }
static void d_set_ctrl(hwdesc_t *d, uint32_t v) { d->ctrl = v; ctrace((v & OWN) ? "OWN" : "ctrl"); }
static uint32_t d_get_ctrl(const hwdesc_t *d)   { ctrace("rC"); return d->ctrl; }
static uint32_t d_get_stat(const hwdesc_t *d)   { ctrace("rS"); return d->stat; }
static void dma_wmb(void) { atomic_thread_fence(memory_order_release); ctrace("WMB"); }  /* __DMB() */
static void dma_rmb(void) { atomic_thread_fence(memory_order_acquire); ctrace("RMB"); }  /* __DMB() */
static void writel(volatile uint32_t *reg, uint32_t v) {     /* Linux writel = 배리어 + MMIO 쓰기 */
    char t[24];
    dma_wmb();
    *reg = v;
    snprintf(t, sizeof t, "DB:%u", (unsigned)v);
    ctrace(t);
}
/* ---- 주어진 mock DMA 엔진: doorbell 이 가리키는 곳까지, OWN 인 디스크립터를 순서대로 전송 ---- */
static void dev_run(int max) {
    while (max-- > 0 && c_dev_idx != TX_TAIL) {
        hwdesc_t *d = &c_desc[c_dev_idx % RING];
        if (!(d->ctrl & OWN)) break;                       /* "descriptor unavailable" → 정지 */
        uint32_t i = (d->addr - BUF_PA(0)) / 64u, n = d->len;
        bool ok = i < RING && d->addr == BUF_PA(i) && n >= 1 && n <= 64;
        if (ok) { memcpy(c_log[c_nlog % 64], c_ram[i], n); c_loglen[c_nlog % 64] = n; c_nlog++; }
        d->stat = (!ok || c_fail_next) ? 1u : 0u;
        c_fail_next = 0;
        d->ctrl = d->ctrl & ~OWN;                          /* write-back: 소유권 반환 */
        c_dev_idx++;
    }
}
static void c_reset(void) {
    memset((void *)c_desc, 0, sizeof c_desc);
    memset(c_cache, 0, sizeof c_cache); memset(c_ram, 0, sizeof c_ram);
    TX_TAIL = 0; c_head = c_tail = c_ok = c_err = c_dev_idx = c_nlog = 0;
    c_fail_next = 0; c_trace[0] = '\0';
}
/* TODO(C1): 프레임 p[0..n) 을 보낸다. n 이 0 이거나 64 초과면 -22, 링이 꽉 찼으면(head-tail == RING) -16
 * (둘 다 아무 접근도 하지 않는다). 순서: 버퍼 복사 → 캐시 clean → addr → len → (배리어) → OWN|FS|LS
 * → head++ → doorbell(TX_TAIL = head, writel 로). 기대 trace: "CPY CLEAN addr len WMB OWN WMB DB:1" */
static int eth_xmit(const uint8_t *p, uint32_t n) {
    (void)p; (void)n; (void)buf_fill; (void)cache_clean; (void)d_set_addr; (void)d_set_len;
    (void)d_set_ctrl; (void)writel;
    return -1; /* TODO */
}
/* TODO(C2): tail 부터 head 전까지, DMA 가 돌려준(OWN==0) 디스크립터를 회수한다. 첫 OWN 에서 멈춘다.
 * 회수마다 stat==0 이면 c_ok++, 아니면 c_err++. 회수한 개수를 돌려준다. 기대 trace(1개): "rC RMB rS" */
static uint32_t eth_reclaim(void) {
    (void)d_get_ctrl; (void)d_get_stat; (void)dma_rmb;
    return 0; /* TODO */
}

int main(void) {
    /* ---------------- Part A ---------------- */
    static uint8_t b60[A_MAXLEN];
    desc_t d;
    CHECK(atomic_is_lock_free(&d.ctrl));               /* 원자 워드가 락 없이 구현돼야 DMA 모델로 쓴다 */
    atomic_init(&d.ctrl, 0u); d.len = 0; d.buf = NULL; d.status = 0xDEADu;
    CHECK(desc_publish(&d, b60, 60u) == 0);
    CHECK(atomic_load(&d.ctrl) == (OWN | FS | LS) && d.len == 60u && d.buf == b60 && d.status == 0u);
    CHECK(desc_publish(&d, b60, 10u) == -1 && d.len == 60u);       /* 아직 DMA 소유 → 손대지 않는다 */
    CHECK(desc_reap(&d) == -1);
    d.status = 7u; atomic_store(&d.ctrl, 0u);                      /* DMA 가 에러 7 로 완료했다고 치고 */
    CHECK(desc_reap(&d) == 7);
    CHECK(desc_publish(&d, b60, 0u) == -1 && desc_publish(&d, b60, A_MAXLEN + 1u) == -1);
    bool a_basic = g_pass == g_total;
    uint32_t cpu_bad = 0;
    if (a_basic) {
        int r = run_threaded(&cpu_bad);
        CHECK(r == 0 && g_eng_done == NPKT && g_eng_bad == 0u && cpu_bad == 0u);
        printf("Part A: %u packets through %d-entry ring, engine bad=%u, cpu bad=%u\n",
               (unsigned)g_eng_done, RING_A, (unsigned)g_eng_bad, (unsigned)cpu_bad);
    } else {
        CHECK(!"Part A 기본 테스트 실패 → 스레드 테스트 건너뜀");
    }

    /* ---------------- Part B ---------------- */
    unsigned tx_naive = wm_count_tx(wm_tx_post_naive), tx_cbar = wm_count_tx(wm_tx_post_cbar);
    unsigned rx_naive = wm_count_rx(wm_rx_reap_naive);
    unsigned tx_mine = wm_count_tx(wm_tx_post), rx_mine = wm_count_rx(wm_rx_reap);
    printf("Part B (%u seeds): tx naive=%u cbar=%u mine=%u | rx naive=%u mine=%u\n",
           WM_SEEDS, tx_naive, tx_cbar, tx_mine, rx_naive, rx_mine);
    CHECK(tx_naive > 0u);                              /* 재현: 배리어 없으면 OWN 이 먼저 보인다     */
    CHECK(tx_cbar > 0u);                               /* 재현: 컴파일러 배리어는 CPU 를 못 막는다   */
    CHECK(rx_naive > 0u);                              /* 재현: OWN==0 을 봤는데 낡은 LEN 을 읽는다  */
    CHECK(tx_mine == 0u);
    CHECK(rx_mine == 0u);

    /* ---------------- Part C ---------------- */
    uint8_t f[64];
    for (int k = 0; k < 64; k++) f[k] = (uint8_t)(0xC0 + k);
    c_reset();
    CHECK(eth_xmit(f, 60u) == 0);
    CHECK(strcmp(c_trace, "CPY CLEAN addr len WMB OWN WMB DB:1") == 0);
    CHECK(c_desc[0].addr == BUF_PA(0) && c_desc[0].len == 60u && c_desc[0].ctrl == (OWN | FS | LS) && TX_TAIL == 1u);
    dev_run(8);
    CHECK(c_nlog == 1u && c_loglen[0] == 60u && memcmp(c_log[0], f, 60) == 0);   /* clean 안 하면 0 이 나간다 */
    c_trace[0] = '\0';
    CHECK(eth_reclaim() == 1u && strcmp(c_trace, "rC RMB rS") == 0 && c_ok == 1u && c_tail == 1u);
    c_trace[0] = '\0';
    CHECK(eth_reclaim() == 0u && c_trace[0] == '\0');                  /* 보낸 게 없으면 읽지도 않는다 */
    CHECK(eth_xmit(f, 0u) == -22 && eth_xmit(f, 65u) == -22 && c_trace[0] == '\0');
    for (int k = 0; k < 4; k++) CHECK(eth_xmit(f + k, 10u) == 0);    /* 링 4칸을 채운다 */
    c_trace[0] = '\0';
    CHECK(eth_xmit(f, 10u) == -16 && c_trace[0] == '\0' && TX_TAIL == 5u);   /* 꽉 참: 아무것도 안 건드림 */
    c_trace[0] = '\0';
    CHECK(eth_reclaim() == 0u && strcmp(c_trace, "rC") == 0);          /* DMA 가 아직 안 했다 */
    dev_run(2);
    CHECK(eth_reclaim() == 2u);
    c_fail_next = 1;
    dev_run(8);
    CHECK(eth_reclaim() == 2u && c_ok == 4u && c_err == 1u && c_tail == c_head);
    CHECK(c_nlog == 5u && memcmp(c_log[4], f + 3, 10) == 0);
    /* 랜덤 인터리빙: 40 프레임을 보내고/DMA 가 처리하고/회수하는 순서를 섞는다 */
    c_reset();
    g_rng = 12345u;
    uint8_t frames[40][64];
    uint32_t lens[40], sent = 0, guard = 0;
    while (sent < 40u && guard++ < 10000u) {
        uint32_t op = xs32() % 3u;
        if (op == 0u) {
            lens[sent] = 1u + xs32() % 64u;
            for (uint32_t k = 0; k < lens[sent]; k++) frames[sent][k] = (uint8_t)xs32();
            if (eth_xmit(frames[sent], lens[sent]) == 0) sent++;
        } else if (op == 1u) dev_run((int)(xs32() % 3u));
        else eth_reclaim();
    }
    dev_run(64); eth_reclaim();
    bool all = sent == 40u && c_nlog == 40u && c_ok == 40u && c_tail == 40u;
    for (uint32_t k = 0; all && k < 40u; k++)
        all = c_loglen[k] == lens[k] && memcmp(c_log[k], frames[k], lens[k]) == 0;
    CHECK(all);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
