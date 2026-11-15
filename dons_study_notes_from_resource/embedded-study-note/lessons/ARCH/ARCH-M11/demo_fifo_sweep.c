/* ARCH-M11 demo — 주변장치 FIFO 에 필요한 깊이 스윕
 *
 * solution.c 의 엔진을 그대로 가져와(main 만 이름을 바꿔 숨김) UART RX 채널 + 항상 준비된 대량 mem→mem 채널을
 * 경쟁시키고, FIFO 깊이를 넉넉히(64) 둔 채 "FIFO 가 가장 많이 찼을 때" (= overrun 없이 필요한 깊이)를 잰다.
 *   N = 대량 채널 버스트 (비선점 단위), P = UART 바이트 간격(사이클), 생산 위상 0..P-1 중 최악.
 *   (a) UART 우선순위가 더 높을 때, (b) 같은 레벨 라운드로빈일 때. 비교 기준: ceil(N / P).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_fifo_sweep.c -o demo_fifo_sweep && ./demo_fifo_sweep
 */
#define main solution_main_unused
#include "solution.c"
#undef main

static uint32_t worst_level(uint8_t bulk_burst, uint32_t period, uint8_t uart_prio, bool *lost) {
    uint32_t worst = 0;
    *lost = false;
    for (uint32_t ph = 0; ph < period; ph++) {
        setup();
        uart(period, ph, 64, 200);
        wdesc(D0, SRC, 0x20008000u, 0x4000u, 0, F_SRC_INC | F_DST_INC | F_ESIZE(2));   /* 4096 비트: 끝까지 경쟁 */
        seg_t one[1] = { { DST, 200 } };
        build_rx_chain(D0 + 0x40, one, 1, 0, true);
        chan_cfg(&E, 0, 1, bulk_burst, 0);
        chan_cfg(&E, 1, uart_prio, 8, 0);
        dma_start(&E, 0, D0); dma_start(&E, 1, D0 + 0x40);
        dma_run(&E, 100000);
        if (g_per.overrun || memcmp(P(DST), g_stream, 200) != 0) *lost = true;   /* 깊이 64 로도 넘침 */
        if (g_per.max_level > worst) worst = g_per.max_level;
    }
    return worst;
}

int main(void) {
    static const uint8_t bursts[] = { 1, 2, 4, 8, 16, 32 };
    static const uint32_t periods[] = { 2, 3, 4, 8 };
    printf("needed FIFO depth (worst max_level over all phases), UART burst 8\n");
    printf(" N\\P |   P=2        P=3        P=4        P=8      (cell = hi-prio / equal-RR / ceil(N/P), ! = overrun at depth 64)\n");
    for (unsigned i = 0; i < sizeof bursts; i++) {
        printf("%3u  |", bursts[i]);
        for (unsigned j = 0; j < sizeof periods / sizeof periods[0]; j++) {
            bool lh, le;
            uint32_t hi = worst_level(bursts[i], periods[j], 2, &lh);
            uint32_t eq = worst_level(bursts[i], periods[j], 1, &le);
            uint32_t b = (bursts[i] + periods[j] - 1) / periods[j];
            printf("  %2u%c/%2u%c/%2u", (unsigned)hi, lh ? '!' : ' ', (unsigned)eq, le ? '!' : ' ', (unsigned)b);
        }
        printf("\n");
    }
    return 0;
}
