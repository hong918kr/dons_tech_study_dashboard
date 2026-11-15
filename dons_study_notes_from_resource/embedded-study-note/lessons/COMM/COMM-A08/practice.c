/* COMM-A08 추가 연습 — 한 번에 검증
 *
 * solution.c 의 모델·드라이버를 그대로 쓴다 (같은 디렉터리에서 빌드):
 *   cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 *
 *   연습 1: 16 B 디스크립터 두 개가 한 캐시 라인 — 어떤 유지보수 순서로도 못 막는 상태 손실, 해법은 패딩/NC
 *   연습 2: 링 인덱스 수학 — 자유 증가 카운터 vs "한 칸 비우기", 2^32 랩에서 N 이 2 의 거듭제곱이어야 하는 이유
 *   연습 3: rx_copybreak — 작은 프레임은 복사하고 DMA 버퍼를 그 자리에 다시 건다 (풀을 안 건드림)
 *   연습 4: NC 디스크립터 + 쓰기 버퍼 재배열 — OWN 앞에 dmb (Linux dma_wmb) 가 필요한 이유
 */
#define COMM_A08_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"
#include "solution.c"

/* ---------------- 연습 1: 디스크립터 두 개가 한 라인 ----------------
 * 디스크립터 0 = PK, 디스크립터 1 = PK + 16 (같은 32 B 라인). 둘 다 DMA 소유로 시작.
 * DMA 가 0 을 끝냄 → CPU 가 0 을 읽고 재장전하는 동안 DMA 가 1 의 상태(OWN=0, 길이 70)를 RAM 에 씀.
 * 재장전 후 RAM 의 디스크립터 1 상태가 살아남았나?
 *   strategy 0: 그냥 고치고 clean            strategy 1: 고치기 직전에 invalidate (그래도 창이 남음)
 *   strategy 2: 디스크립터를 32 B 로 패딩     strategy 3: 디스크립터를 NC 영역에 (CPU 는 자기 워드만 씀) */
#define PK 0x0400u
static bool packed_survives(int strategy) {
    sys_reset(false);
    uint32_t d0 = strategy == 3 ? 0x0280u : PK;
    uint32_t d1 = strategy == 2 ? d0 + 32u : d0 + 16u;
    ram_wr32(d0, D_OWN); ram_wr32(d0 + 4, 0x1000); ram_wr32(d0 + 8, 128);
    ram_wr32(d1, D_OWN); ram_wr32(d1 + 4, 0x1080); ram_wr32(d1 + 8, 128);
    ram_wr32(d0, D_FS | D_LS | 60u);                  /* DMA: 디스크립터 0 완료 */
    cache_inval(d0, 16); uint32_t st = cpu_rd32(d0); /* CPU: 상태 읽기 (라인 채움 — 이 순간 d1 은 OWN=1) */
    if (!(st & D_LS)) return false;
    if (strategy == 1) cache_inval(d0, 16);          /* 고치기 직전에 다시 버려도 */
    cpu_wr32(d0 + 4, 0x1100); cpu_wr32(d0 + 8, 128); /* 재장전: 라인을 다시 채우고 고침 */
    ram_wr32(d1, D_FS | D_LS | 70u);                  /* 바로 그 사이 DMA: 디스크립터 1 완료 */
    cpu_wr32(d0, D_OWN);
    cache_clean(d0, 16);                              /* 라인 전체 write-back */
    return ram_rd32(d1) == (D_FS | D_LS | 70u);
}

/* ---------------- 연습 2: 링 인덱스 ---------------- */
/* (A) 자유 증가 uint32 카운터: used = head - tail (부호 없는 뺄셈은 랩에도 맞음), slot = head % N */
static uint32_t used_free(uint32_t head, uint32_t tail) { return head - tail; }
/* (B) 0..N-1 인덱스 + 한 칸 비우기: full = (h+1)%N == t, used = (h - t + N) % N, 용량 N-1 */
static uint32_t used_mod(uint32_t h, uint32_t t, uint32_t n) { return (h + n - t) % n; }
static bool full_mod(uint32_t h, uint32_t t, uint32_t n) { return (h + 1u) % n == t; }
/* 카운터가 2^32 에서 0 으로 돌 때 slot = c % N 이 1 씩 이어지나? (N 이 2^32 를 나눠야 함) */
static bool slot_continuous_at_wrap(uint32_t n) {
    uint32_t c = UINT32_MAX;
    return (c % n + 1u) % n == (uint32_t)(c + 1u) % n;
}

/* ---------------- 연습 3: rx_copybreak ---------------- */
#define COPYBREAK 96u
static unsigned g_cb_copied, g_cb_zc;
/* 2 = 작은 프레임을 dst 로 복사 (버퍼는 그 슬롯에 다시 걸림), 1 = 제로카피 pkt, 0 = 없음 */
static int rx_poll_copybreak(uint8_t *dst, uint32_t cap, uint32_t *dlen, pkt_t *out) {
    uint32_t s = g_drv.rx_next % RX_RING, d = rxd(s);
    desc_inval(d, M_RX_DESC_INVAL);                   /* DMA 가 쓴 상태를 읽기 위해 */
    uint32_t w0 = cpu_rd32(d), len = w0 & D_FL;
    if (w0 & D_OWN) return 0;
    if ((w0 & (D_FS | D_LS | D_ES)) == (D_FS | D_LS) && len > 0 && len <= COPYBREAK && len <= cap) {
        uint32_t a = buf_addr(g_drv.rx_buf[s]);
        cache_inval(a, g_bufsz);                      /* 복사도 CPU 읽기 → 완료 후 invalidate 는 그대로 필요 */
        for (uint32_t i = 0; i < len; i++) dst[i] = cpu_rd8(a + i);
        *dlen = len;
        rx_recycle(1);                                /* 같은 버퍼를 같은 슬롯에: 풀 get/put 0 번 */
        g_drv.rx_frames++; g_cb_copied++;
        return 2;
    }
    int r = drv_rx_poll(out);
    if (r == 1) g_cb_zc++;
    return r;
}

/* ---------------- 연습 4: NC 쓰기 버퍼 재배열 ----------------
 * Normal non-cacheable 쓰기는 쓰기 버퍼에서 합쳐지거나 순서가 바뀌어 장치에 보일 수 있다 (배리어 없으면).
 * 모델: 버퍼 4 칸, 하드웨어는 "아무 순서로나" 비운다 — 여기선 최악인 역순. dmb() 는 지금까지의 쓰기를 순서대로 비움. */
typedef struct { uint32_t a, v; } wce_t;
static wce_t g_wc[4];
static unsigned g_wcn;
static uint8_t g_ncm[64];
static void wc_flush_inorder(void) { for (unsigned i = 0; i < g_wcn; i++) st32(&g_ncm[g_wc[i].a], g_wc[i].v); g_wcn = 0; }
static void nc_store(uint32_t a, uint32_t v) { if (g_wcn == 4u) wc_flush_inorder(); g_wc[g_wcn].a = a; g_wc[g_wcn].v = v; g_wcn++; }
static void dmb(void) { wc_flush_inorder(); }
/* 디스크립터를 게시하고, 하드웨어가 버퍼를 역순으로 하나씩 비울 때마다 DMA 시점에서 본다. 찢어진 순간 수 반환. */
static unsigned nc_publish(bool use_dmb) {
    memset(g_ncm, 0, sizeof g_ncm); g_wcn = 0;
    st32(&g_ncm[4], 0xDEAD); st32(&g_ncm[8], 1);     /* 이전 바퀴의 낡은 buf/len */
    nc_store(4, 0x1180); nc_store(8, 64);
    if (use_dmb) dmb();                               /* Linux: dma_wmb() — 내용이 OWN 보다 먼저 보이게 */
    nc_store(0, D_OWN | D_FS | D_LS);
    unsigned torn = 0;
    while (g_wcn) {                                   /* 하드웨어: 마지막 것부터 하나씩 */
        wce_t e = g_wc[--g_wcn];
        st32(&g_ncm[e.a], e.v);
        torn += (ld32(&g_ncm[0]) & D_OWN) && (ld32(&g_ncm[4]) != 0x1180u || ld32(&g_ncm[8]) != 64u);
    }
    return torn;
}

int main(int argc, char **argv) {
    g_verbose = argc > 1 && strcmp(argv[1], "-v") == 0;

    /* 연습 1 */
    CHECK(packed_survives(0) == false);
    CHECK(packed_survives(1) == false);
    CHECK(packed_survives(2) == true);
    CHECK(packed_survives(3) == true);

    /* 연습 2 */
    {
        unsigned bad = 0;
        for (uint32_t base = UINT32_MAX - 20u; base != 20u; base++)        /* 2^32 랩 근처 */
            for (uint32_t k = 0; k <= 8; k++) bad += used_free(base + k, base) != k;
        CHECK(bad == 0u);
        bad = 0;
        for (uint32_t n = 2; n <= 9; n++)
            for (uint32_t t = 0; t < n; t++)
                for (uint32_t k = 0; k < n; k++) {
                    uint32_t h = (t + k) % n;
                    bad += used_mod(h, t, n) != k;
                    bad += full_mod(h, t, n) != (k == n - 1u);
                }
        CHECK(bad == 0u);
        CHECK(slot_continuous_at_wrap(8) && slot_continuous_at_wrap(16) && slot_continuous_at_wrap(256));
        CHECK(!slot_continuous_at_wrap(6) && !slot_continuous_at_wrap(10));
        printf("연습 2: UINT32_MAX %% 6 = %u, 0 %% 6 = 0 → 랩에서 슬롯이 %u → 0 으로 건너뜀\n", (unsigned)(UINT32_MAX % 6u), (unsigned)(UINT32_MAX % 6u));
    }

    /* 연습 3 */
    {
        sys_reset(false);
        drv_rx_init();
        uint8_t f[MAX_FRAME], got[MAX_FRAME];
        uint32_t rs = 4242;
        unsigned small = 0, good = 0, n = 0;
        g_cb_copied = g_cb_zc = 0;
        for (uint32_t seq = 1; seq <= 600; seq++) {
            uint32_t len = (xs32(&rs) % 2u) ? 16u + xs32(&rs) % 81u : 97u + xs32(&rs) % 288u;
            small += len <= COPYBREAK;
            gen_frame(seq, len, f); wire_rx(f, len, false); dma_run(16);
            pkt_t p; memset(&p, 0, sizeof p);
            uint32_t dl = 0;
            int r = rx_poll_copybreak(got, sizeof got, &dl, &p);
            if (r == 2) { n++; good += dl == len && memcmp(got, f, len) == 0; }
            else if (r == 1) {
                n++;
                good += pkt_read(&p, got) == len && memcmp(got, f, len) == 0;
                for (unsigned i = 0; i < p.nseg; i++) pool_put(p.seg[i].buf);
            }
        }
        CHECK(n == 600u && good == 600u && g_cb_copied == small && g_cb_zc == 600u - small);
        CHECK(drv_audit(NULL, 0) == 0u && g_hw.barrier_viol == 0u);
        printf("연습 3: 600 프레임 중 %u 개 복사(<= %u B, 풀 연산 0), %u 개 제로카피\n", g_cb_copied, COPYBREAK, g_cb_zc);
    }

    /* 연습 4 */
    CHECK(nc_publish(false) > 0u);
    CHECK(nc_publish(true) == 0u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
