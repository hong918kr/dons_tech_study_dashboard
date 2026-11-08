/* C-A02 추가 연습 — 한 번에 검증
 * 연습 1: 묶음 전송(burst) — 배리어 1번 + doorbell 1번으로 프레임 여러 개
 * 연습 2: scatter-gather 프레임 — 첫 세그먼트의 OWN 은 "맨 마지막에"
 * 연습 3: RX 링 — OWN 확인 → RMB → 캐시 invalidate → 복사 → 다시 DMA 에 넘기기
 * 연습 4: 상황별 배리어 고르기 (면접 단골 표)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define OWN 0x80000000u
#define LS  0x20000000u
#define FS  0x10000000u
#define RING 8u
#define BUF_PA(i) (0x30040000u + 64u * (i))

/* ---------------- 공용 mock: 디스크립터 링 + 버퍼(캐시/RAM) + trace ---------------- */
typedef struct { volatile uint32_t ctrl, len, addr, stat; } hwdesc_t;
static hwdesc_t d_[RING];
static uint8_t cache_[RING][64], ram_[RING][64];
static volatile uint32_t TX_TAIL;
static uint32_t head_, dev_idx_, underflow_, nlog_;
static uint8_t log_[1024];                       /* DMA 가 내보낸 바이트를 이어 붙인 것 */
static uint32_t loglen_, frames_;
static bool eager_;                              /* 연습 2: 쓰기마다 DMA 가 링을 훑는다 (실행 중인 DMA) */
static char tr_[512];

static void tr(const char *s) {
    size_t n = strlen(tr_);
    if (n && n + 1 < sizeof tr_) tr_[n++] = ' ';
    snprintf(tr_ + n, sizeof tr_ - n, "%s", s);
}
static void dev_poll(void);
static void buf_fill(uint32_t i, const uint8_t *s, uint32_t n) { memcpy(cache_[i], s, n); tr("CPY"); }
static void cache_clean(uint32_t i, uint32_t n) { memcpy(ram_[i], cache_[i], n); tr("CLEAN"); }
static void cache_inval(uint32_t i, uint32_t n) { memcpy(cache_[i], ram_[i], n); tr("INV"); }
static void d_set_addr(hwdesc_t *d, uint32_t v) { d->addr = v; tr("addr"); if (eager_) dev_poll(); }
static void d_set_len(hwdesc_t *d, uint32_t v)  { d->len = v;  tr("len");  if (eager_) dev_poll(); }
static void d_set_ctrl(hwdesc_t *d, uint32_t v) { d->ctrl = v; tr((v & OWN) ? "OWN" : "ctrl"); if (eager_) dev_poll(); }
static uint32_t d_get_ctrl(const hwdesc_t *d)   { tr("rC"); return d->ctrl; }
static uint32_t d_get_len(const hwdesc_t *d)    { tr("rL"); return d->len; }
static uint32_t d_get_stat(const hwdesc_t *d)   { tr("rS"); return d->stat; }
static void dma_wmb(void) { atomic_thread_fence(memory_order_release); tr("WMB"); }
static void dma_rmb(void) { atomic_thread_fence(memory_order_acquire); tr("RMB"); }
static void writel(volatile uint32_t *r, uint32_t v) {
    char t[24];
    dma_wmb(); *r = v;
    snprintf(t, sizeof t, "DB:%u", (unsigned)v); tr(t);
}
/* TX DMA: dev_idx_ 부터 OWN 인 프레임(FS..LS)을 통째로 전송. 도중에 OWN 아닌 세그먼트를 만나면 underflow. */
static void dev_tx_frames(bool need_doorbell) {
    for (;;) {
        if (need_doorbell && dev_idx_ == TX_TAIL) return;
        hwdesc_t *d = &d_[dev_idx_ % RING];
        if (!(d->ctrl & OWN)) return;
        uint32_t k = dev_idx_;
        for (;;) {                                       /* 프레임 끝(LS)까지 */
            hwdesc_t *s = &d_[k % RING];
            uint32_t c = s->ctrl;
            if (!(c & OWN)) { underflow_++; break; }     /* 아직 안 넘어온 세그먼트 → 전송 에러 */
            uint32_t i = (s->addr - BUF_PA(0)) / 64u, n = s->len;
            if (i < RING && n <= 64u && loglen_ + n <= sizeof log_) { memcpy(log_ + loglen_, ram_[i], n); loglen_ += n; }
            s->ctrl = c & ~OWN;                          /* write-back: 소유권 반환 */
            k++;
            if (c & LS) { frames_++; break; }
        }
        dev_idx_ = k;
    }
}
static void dev_poll(void) { dev_tx_frames(false); }
static void reset(void) {
    memset((void *)d_, 0, sizeof d_); memset(cache_, 0, sizeof cache_); memset(ram_, 0, sizeof ram_);
    TX_TAIL = 0; head_ = dev_idx_ = underflow_ = nlog_ = loglen_ = frames_ = 0;
    eager_ = false; tr_[0] = '\0';
}

/* ================= 연습 1: 묶음 전송 ================= */
/* 프레임 n 개를 링에 넣는다 (링에 빈칸이 cap 개뿐이면 그만큼만). 배리어는 필드 쓰기 뒤 1번,
 * doorbell 은 맨 끝에 1번. 넣은 개수를 돌려준다. 하나도 못 넣으면 doorbell 도 안 친다. */
static uint32_t xmit_burst(const uint8_t *const *f, const uint32_t *len, uint32_t n, uint32_t cap) {
    uint32_t k, m = n < cap ? n : cap;
    if (m == 0u) return 0u;
    for (k = 0; k < m; k++) {                      /* ① 버퍼·필드를 전부 쓴다 */
        uint32_t i = (head_ + k) % RING;
        buf_fill(i, f[k], len[k]);
        cache_clean(i, len[k]);
        d_set_addr(&d_[i], BUF_PA(i));
        d_set_len(&d_[i], len[k]);
    }
    dma_wmb();                                     /* ② 배리어 한 번으로 "모든 필드 → 모든 OWN" */
    for (k = 0; k < m; k++) d_set_ctrl(&d_[(head_ + k) % RING], OWN | FS | LS);
    head_ += m;
    writel(&TX_TAIL, head_);                       /* ③ doorbell 한 번 */
    return m;
}

/* ================= 연습 2: scatter-gather ================= */
/* 주어진 버그 버전: 세그먼트를 순서대로 OWN → 실행 중인 DMA 가 첫 세그먼트만 보고 출발한다 */
static void sg_xmit_naive(const uint8_t *p, uint32_t n, uint32_t seg) {
    uint32_t nseg = (n + seg - 1u) / seg;
    for (uint32_t s = 0; s < nseg; s++) {
        uint32_t i = (head_ + s) % RING, len = (s + 1u < nseg) ? seg : n - s * seg;
        buf_fill(i, p + s * seg, len); cache_clean(i, len);
        d_set_addr(&d_[i], BUF_PA(i)); d_set_len(&d_[i], len);
        dma_wmb();
        d_set_ctrl(&d_[i], OWN | (s == 0 ? FS : 0u) | (s + 1u == nseg ? LS : 0u));
    }
    head_ += nseg;
}
static void sg_xmit(const uint8_t *p, uint32_t n, uint32_t seg) {
    uint32_t nseg = (n + seg - 1u) / seg;
    for (uint32_t s = 0; s < nseg; s++) {
        uint32_t i = (head_ + s) % RING, len = (s + 1u < nseg) ? seg : n - s * seg;
        buf_fill(i, p + s * seg, len); cache_clean(i, len);
        d_set_addr(&d_[i], BUF_PA(i)); d_set_len(&d_[i], len);
        if (s > 0u) d_set_ctrl(&d_[i], OWN | (s + 1u == nseg ? LS : 0u));   /* 뒤 세그먼트는 바로 OWN */
    }
    dma_wmb();                                     /* 모든 세그먼트의 필드·OWN → 첫 OWN */
    d_set_ctrl(&d_[head_ % RING], OWN | FS | (nseg == 1u ? LS : 0u));       /* 첫 세그먼트를 맨 마지막에 */
    head_ += nseg;
}

/* ================= 연습 3: RX 링 ================= */
/* DMA(장치)가 프레임을 RAM 에 쓰고 len/stat 을 쓴 뒤 OWN 을 내린다 (주어진 mock) */
static void dev_rx_frame(uint32_t i, const uint8_t *p, uint32_t n, uint32_t stat) {
    memcpy(ram_[i], p, n);
    d_[i].len = n; d_[i].stat = stat;
    d_[i].ctrl = d_[i].ctrl & ~OWN;
}
/* 받을 프레임이 있으면 out 에 복사하고 길이를, 없으면 -1, 에러 프레임이면 -5. 처리한 칸은 다시 DMA 에 넘긴다.
 * 기대 trace: "rC RMB rL rS INV CPY addr len WMB OWN" */
static uint32_t rx_tail_;
static int rx_receive(uint8_t *out) {
    uint32_t i = rx_tail_ % RING;
    hwdesc_t *d = &d_[i];
    if (d_get_ctrl(d) & OWN) return -1;            /* 아직 DMA 가 쓰는 중 */
    dma_rmb();                                     /* OWN==0 을 본 뒤에 len/stat/데이터 */
    uint32_t n = d_get_len(d), st = d_get_stat(d);
    int r = -5;
    if (st == 0u && n <= 64u) {
        cache_inval(i, n);                         /* 캐시의 낡은 줄을 버리고 RAM 에서 다시 */
        memcpy(out, cache_[i], n); tr("CPY");
        r = (int)n;
    }
    d_set_addr(d, BUF_PA(i));                      /* 다시 DMA 에 넘긴다 (refill) */
    d_set_len(d, 64u);
    dma_wmb();
    d_set_ctrl(d, OWN);
    rx_tail_++;
    return r;
}

/* ================= 연습 4: 배리어 고르기 ================= */
enum bar { B_NONE, B_COMPILER, B_ACQREL, B_DMB, B_DSB, B_DSB_ISB };
enum scen {
    S_LOCAL,          /* 한 스레드 안에서만 쓰는 지역 버퍼                                    */
    S_ISR_SAME_CORE,  /* 같은 코어의 ISR 이 읽는 플래그 + 버퍼 (단일 코어 MCU)                */
    S_SMP,            /* 다른 코어/스레드가 읽는 데이터 + 플래그                              */
    S_DMA_OWN,        /* DMA 디스크립터 필드 → OWN (non-cacheable 메모리)                     */
    S_DOORBELL,       /* OWN(일반 메모리) → doorbell 레지스터(Device 메모리) 쓰기             */
    S_CACHE_THEN_GO,  /* D-cache clean by address 가 "끝난 뒤" DMA 시작 레지스터를 써야 할 때 */
    S_VTOR_MPU        /* VTOR / MPU / CONTROL 을 바꾼 뒤 바로 그 설정으로 명령어를 실행할 때   */
};
static enum bar pick_barrier(enum scen s) {
    switch (s) {
    case S_LOCAL:         return B_NONE;
    case S_ISR_SAME_CORE: return B_COMPILER;   /* atomic_signal_fence / asm("":::"memory") */
    case S_SMP:           return B_ACQREL;     /* release store / acquire load (또는 thread fence) */
    case S_DMA_OWN:       return B_DMB;        /* dma_wmb = DMB */
    case S_DOORBELL:      return B_DMB;        /* writel 안의 배리어 = DMB (arm64 Linux: dmb oshst) */
    case S_CACHE_THEN_GO: return B_DSB;        /* 캐시 유지보수의 "완료" 는 DSB 만 기다린다 */
    case S_VTOR_MPU:      return B_DSB_ISB;    /* 설정 완료(DSB) + 파이프라인 재인출(ISB) */
    }
    return B_NONE;
}

int main(void) {
    uint8_t a[64], b[64], c[64];
    for (int k = 0; k < 64; k++) { a[k] = (uint8_t)(0xA0 + k); b[k] = (uint8_t)(0xB0 + k); c[k] = (uint8_t)(0xC0 + k); }

    /* 연습 1 */
    reset();
    const uint8_t *fr[3] = { a, b, c };
    uint32_t ln[3] = { 10u, 20u, 30u };
    CHECK(xmit_burst(fr, ln, 2u, RING) == 2u);
    CHECK(strcmp(tr_, "CPY CLEAN addr len CPY CLEAN addr len WMB OWN OWN WMB DB:2") == 0);
    dev_tx_frames(true);
    CHECK(frames_ == 2u && loglen_ == 30u && memcmp(log_, a, 10) == 0 && memcmp(log_ + 10, b, 20) == 0);
    tr_[0] = '\0';
    CHECK(xmit_burst(fr, ln, 3u, 1u) == 1u && TX_TAIL == 3u);      /* 빈칸 1개 → 1개만 */
    tr_[0] = '\0';
    CHECK(xmit_burst(fr, ln, 3u, 0u) == 0u && tr_[0] == '\0' && TX_TAIL == 3u);   /* 꽉 참 → 아무것도 */

    /* 연습 2: DMA 가 이미 돌고 있다 (쓰기마다 링을 훑음) */
    reset(); eager_ = true;
    sg_xmit_naive(a, 50u, 32u);                                     /* 32 + 18 바이트, 세그먼트 2개 */
    CHECK(underflow_ == 1u);                                        /* 재현: 둘째 세그먼트가 늦었다 */
    reset(); eager_ = true;
    sg_xmit(a, 50u, 32u);
    CHECK(underflow_ == 0u && frames_ == 1u && loglen_ == 50u && memcmp(log_, a, 50) == 0);
    CHECK(strcmp(tr_, "CPY CLEAN addr len CPY CLEAN addr len OWN WMB OWN") == 0);
    reset(); eager_ = true;
    sg_xmit(b, 64u, 16u);                                           /* 세그먼트 4개 */
    CHECK(underflow_ == 0u && frames_ == 1u && loglen_ == 64u && memcmp(log_, b, 64) == 0);
    reset(); eager_ = true;
    sg_xmit(c, 5u, 32u);                                            /* 세그먼트 1개 = FS|LS */
    CHECK(underflow_ == 0u && frames_ == 1u && loglen_ == 5u && memcmp(log_, c, 5) == 0);

    /* 연습 3 */
    reset(); rx_tail_ = 0;
    for (uint32_t i = 0; i < RING; i++) { d_[i].addr = BUF_PA(i); d_[i].len = 64u; d_[i].ctrl = OWN; }
    memset(cache_, 0xEE, sizeof cache_);                            /* CPU 캐시에 남은 낡은 줄 */
    uint8_t out[64];
    CHECK(rx_receive(out) == -1 && strcmp(tr_, "rC") == 0);        /* 아직 없음 */
    dev_rx_frame(0u, b, 40u, 0u);
    tr_[0] = '\0';
    CHECK(rx_receive(out) == 40 && memcmp(out, b, 40) == 0);       /* invalidate 안 하면 0xEE 가 나온다 */
    CHECK(strcmp(tr_, "rC RMB rL rS INV CPY addr len WMB OWN") == 0);
    CHECK(d_[0].ctrl == OWN && d_[0].len == 64u && rx_tail_ == 1u);
    dev_rx_frame(1u, c, 8u, 3u);                                    /* CRC 에러 프레임 */
    CHECK(rx_receive(out) == -5 && d_[1].ctrl == OWN && rx_tail_ == 2u);   /* 에러여도 칸은 돌려준다 */

    /* 연습 4 */
    CHECK(pick_barrier(S_LOCAL) == B_NONE);
    CHECK(pick_barrier(S_ISR_SAME_CORE) == B_COMPILER);
    CHECK(pick_barrier(S_SMP) == B_ACQREL);
    CHECK(pick_barrier(S_DMA_OWN) == B_DMB && pick_barrier(S_DOORBELL) == B_DMB);
    CHECK(pick_barrier(S_CACHE_THEN_GO) == B_DSB);
    CHECK(pick_barrier(S_VTOR_MPU) == B_DSB_ISB);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
