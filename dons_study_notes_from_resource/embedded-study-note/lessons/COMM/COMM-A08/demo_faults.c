/* COMM-A08 실험 — 유지보수 한 줄을 뺀 드라이버를 랜덤 soak 에 20 시드씩 돌리고 증상을 센다 + 비용 표
 *
 * solution.c 의 모델·드라이버·soak 를 그대로 쓴다 (같은 디렉터리에서 빌드):
 *   cc -std=c11 -O2 -Wall -Wextra demo_faults.c -o demo_faults && ./demo_faults
 */
#define COMM_A08_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"
#include "solution.c"

typedef struct { const char *name; unsigned flag; } om_t;
static const om_t OM[] = {
    { "(없음 = 정답)",          0 },
    { "(a)  TX 버퍼 clean",      M_TX_BUF_CLEAN },
    { "(b)  TX 디스크립터 clean", M_TX_DESC_CLEAN },
    { "(b') TX 디스크립터 inval", M_TX_DESC_INVAL },
    { "(c') RX 버퍼 넘기기 전",   M_RX_BUF_PREP },
    { "(c)  RX 버퍼 완료 후",     M_RX_BUF_INVAL },
    { "(bR) RX 디스크립터 clean", M_RX_DESC_CLEAN },
    { "(c\") RX 디스크립터 inval", M_RX_DESC_INVAL },
    { "(e1) OWN 먼저 store",      F_OWN_FIRST },
    { "(e2) 앞에서부터 넘김",     F_FWD_ORDER },
    { "     도어벨 앞 DSB",       F_NO_DSB },
};

int main(void) {
    printf("soak 4000 틱 x 시드 20 개 (시드 1..20). 숫자 = 20 시드 합계, 마지막 열 = 증상이 하나라도 난 시드 수\n");
    printf("%-26s %7s %7s %7s %7s %7s %7s %7s %6s %6s\n", "빠진 것", "rx손상", "tx손상", "미계산", "링이상", "누수",
           "underf", "배리어", "RBU", "시드");
    for (unsigned k = 0; k < sizeof OM / sizeof OM[0]; k++) {
        unsigned rx = 0, tx = 0, un = 0, ring = 0, leak = 0, uf = 0, bar = 0, rbu = 0, seeds = 0, deliv = 0, inj = 0;
        for (uint32_t seed = 1; seed <= 20; seed++) {
            soak_t s = soak(seed, OM[k].flag, false, 4000);
            rx += s.rx_corrupt; tx += s.tx_corrupt; un += s.unaccounted; ring += s.ring_bad; leak += s.leaks;
            uf += g_hw.tx_underflow + g_hw.tx_badseq; bar += g_hw.barrier_viol; rbu += g_hw.rx_rbu;
            deliv += s.delivered; inj += s.injected;
            seeds += soak_symptoms(&s) > 0u;
        }
        printf("%-26s %7u %7u %7u %7u %7u %7u %7u %6u %4u/20   (주입 %u, 전달 %u)\n", OM[k].name, rx, tx, un, ring, leak, uf, bar, rbu,
               seeds, inj, deliv);
    }

    /* 비용: 같은 에코 작업(교체·프리페치 훅 없음)을 디스크립터 캐시 가능 vs NC 로 */
    printf("\n비용 (에코 1000 프레임, 길이 60 B 와 300 B 번갈아, 랜덤 하드웨어 훅 없음) — 프레임당 평균\n");
    printf("%-22s %9s %9s %9s %9s %9s %9s\n", "디스크립터 위치", "CMO(desc)", "CMO(buf)", "line fill", "writeback", "NC 접근", "DSB");
    for (int nc = 0; nc < 2; nc++) {
        sys_reset(nc != 0);
        drv_rx_init();
        memset(&g_cs, 0, sizeof g_cs);
        uint8_t f[MAX_FRAME];
        unsigned frames = 0;
        for (uint32_t seq = 1; seq <= 1000; seq++) {
            uint32_t len = (seq & 1u) ? 60u : 300u;
            gen_frame(seq, len, f); wire_rx(f, len, false);
            dma_run(16);
            pkt_t p; memset(&p, 0, sizeof p);
            if (drv_rx_poll(&p) == 1) { frames++; if (app_echo(&p) != DRV_OK) printf("busy?\n"); }
            dma_run(16);
            drv_tx_reclaim();
        }
        printf("%-22s %9.2f %9.2f %9.2f %9.2f %9.2f %9.2f   (프레임 %u, 증상: audit %u)\n", nc ? "MPU non-cacheable" : "캐시 가능 + CMO",
               (double)g_cs.cmo_desc / frames, (double)g_cs.cmo_buf / frames, (double)g_cs.fills / frames,
               (double)g_cs.wbacks / frames, (double)g_cs.nc_acc / frames, (double)g_cs.dsb / frames, frames, drv_audit(NULL, 0));
    }
    return 0;
}
