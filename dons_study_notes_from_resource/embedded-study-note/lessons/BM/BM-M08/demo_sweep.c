/* BM-M08 demo — solution.c 의 모델을 그대로 써서 표 세 개를 찍는다 (테스트 아님, 관찰용)
 *   1) RX circular (N=8, 3 tick/바이트, 512 B): HT 콜백 비용 × ISR 클리어 방식 → 잃은 콜백 수
 *   2) M→M 1024 바이트 복사: 항목 크기별 tick 수
 *   3) RX 4096 바이트: 버퍼 N 별 DMA IRQ 진입 수
 * 빌드 (레슨 폴더에서): cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o demo_sweep && ./demo_sweep
 */
#define BM_M08_NO_MAIN 1
#pragma GCC diagnostic ignored "-Wunused-function"   /* main 이 쓰던 테스트 도우미 일부를 여기선 안 쓴다 */
#include "solution.c"

/* 변형: 읽자마자 그룹 전체를 지우고 콜백 (창이 읽기~쓰기 사이 1 tick 으로 좁다) */
static void bad_handler_clear_all_early_s2(void) {
    uint32_t f = (REG_READ(DMA2_BASE + DMA_LISR) >> 16) & DMA_GROUP_MASK;
    REG_WRITE(DMA2_BASE + DMA_LIFCR, DMA_GROUP_MASK << 16);
    if (f & (1u << DMA_HTIF)) cb_half(NULL);
    if (f & (1u << DMA_TCIF)) cb_full(NULL);
}

int main(void) {
    printf("1) RX circular N=8, 1 byte / 3 ticks, 512 B -> 128 callbacks expected\n");
    printf("   HT-cost | correct (seen-only) | clear-all-early | clear-all-at-end | ORE(correct)\n");
    static const unsigned COSTS[] = { 0, 8, 10, 11, 12, 18, 19, 20, 21, 22, 24, 30 };
    for (unsigned ci = 0; ci < sizeof COSTS / sizeof COSTS[0]; ci++) {
        unsigned cost = COSTS[ci];
        unsigned lost[3], ore0 = 0;
        isr_t hs[3] = { NULL, bad_handler_clear_all_early_s2, bad_handler_clear_all_at_end_s2 };
        for (unsigned v = 0; v < 3u; v++) {
            rx_scenario(8u, 512u, 3u, cost, hs[v]);
            lost[v] = 128u - (app.half + app.full);
            if (v == 0u) ore0 = U.ore;
        }
        printf("   %7u | %19u | %15u | %16u | %u\n", cost, lost[0], lost[1], lost[2], ore0);
    }

    printf("\n2) M2M copy 1024 bytes (stream 0, model: 1 item per tick)\n");
    static const char *nm[3] = { "byte", "halfword", "word" };
    for (unsigned sz = 0; sz < 3u; sz++) {
        sim_reset();
        uint16_t n = (uint16_t)(1024u >> sz);
        dma_cfg_t c = m2m_cfg(0u, sz, SA(0x100), SA(0x1000), n);
        dma_stream_config(DMA2_BASE, &c);
        dma_stream_start(DMA2_BASE, 0u);
        uint64_t t0 = MK.now;
        RUN_UNTIL(!(D.s[0].cr & DMA_CR_EN), 5000u);
        printf("   %-8s NDTR=%4u  ticks=%llu\n", nm[sz], (unsigned)n, (unsigned long long)(MK.now - t0));
    }

    printf("\n3) RX 4096 bytes: DMA2_Stream2 IRQ entries vs per-byte RXNE IRQ (would be 4096)\n");
    for (uint16_t n = 8; n <= 512u; n = (uint16_t)(n * 4u)) {
        rx_scenario(n, 4096u, 5u, 0u, NULL);
        printf("   N=%3u  IRQ entries=%4u  callbacks=%4u  data ok=%s\n", (unsigned)n, NV.entries[58],
               app.half + app.full, (app.nout == 4096u && memcmp(app.out, rx_data, 4096u) == 0) ? "yes" : "NO");
    }
    return 0;
}
