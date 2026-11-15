/* BM-M04 관찰용: 같은 레슨의 solution.c (드라이버 + 모델) 를 그대로 포함해서 표 3개를 뽑는다.
 *   cd lessons/BM/BM-M04
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_uart_irq.c -o /tmp/BM-M04_demo && /tmp/BM-M04_demo
 * 1 tick = 1 비트 시간. 115200 baud 라면 1 tick = 8.68 µs, 1 프레임(10 tick) = 86.8 µs.
 */
#define main solution_main
#include "solution.c"
#undef main

static uint8_t big_rx[1024];

/* 폴링 송신 (BM-B12 방식): TXE 를 기다리며 CPU 가 그 자리에서 돈다. 돌아온 시점까지의 tick = main 이 묶인 시간 */
static uint32_t polled_send(const uint8_t *b, uint32_t n) {
    uint32_t t0 = U.tick;
    uint32_t cr1 = mock_peek(USART_CR1);
    REG_WRITE(USART_CR1, cr1 & ~CR1_RXNEIE);             /* 순수 폴링: 인터럽트 없음 */
    for (uint32_t i = 0; i < n; i++) {
        for (int k = 0; k < 100000 && !(REG_READ(USART_SR) & SR_TXE); k++) sim_step();
        REG_WRITE(USART_DR, b[i]);
    }
    return U.tick - t0;
}

int main(void) {
    uint8_t msg[256];
    for (int i = 0; i < 256; i++) msg[i] = (uint8_t)('a' + i % 26);

    printf("== 표 1. TX: 폴링 vs 인터럽트 (메시지 L 바이트, 그 뒤 1000 tick 쉼) ==\n");
    printf("   L | 폴링: main 묶임 tick | IRQ: main 묶임 tick | IRQ ISR 호출 | TXEIE 안 끄는 ISR 호출\n");
    const uint32_t Ls[] = { 1, 16, 64, 128 };
    for (unsigned li = 0; li < 4; li++) {
        uint32_t L = Ls[li];
        setup();
        uint32_t tp = polled_send(msg, L);
        setup();
        uint32_t t0 = U.tick;
        size_t w = uart_write(msg, L);
        uint32_t ti = U.tick - t0;
        sim_run(L * 10u + 1000u);
        uint32_t good = g_st.isr_calls;
        int ok = w == L && U.cap_n == L;
        setup();
        nv_vec[USART2_IRQn] = uart_isr_no_txeie_off;
        uart_write(msg, L);
        sim_run(L * 10u + 1000u);
        printf("%4u | %20u | %19u | %12u | %22u %s\n", (unsigned)L, (unsigned)tp, (unsigned)ti, (unsigned)good,
               (unsigned)g_st.isr_calls, ok ? "" : "(!)");
    }

    printf("\n== 표 2. ISR 가 K tick 늦을 때 (붙어서 오는 20바이트, 첫 바이트 도착부터 마스크) ==\n");
    printf("   K | 유실 바이트 (ORE) | 드라이버 ore 카운트 | 받은 바이트\n");
    const uint32_t Ks[] = { 0, 5, 9, 10, 11, 19, 20, 35, 50, 100, 150 };
    for (unsigned ki = 0; ki < sizeof Ks / sizeof Ks[0]; ki++) {
        uint32_t K = Ks[ki];
        setup();
        for (int i = 0; i < 20; i++) s_src[i] = (uint8_t)('A' + i);
        rx_feed(20, NULL);
        nv_mask_from = s_at[0]; nv_mask_to = s_at[0] + K;
        sim_run(20 * 10 + 200);
        uint8_t rb[64];
        size_t got = uart_read(rb, sizeof rb);
        printf("%4u | %17u | %19u | %.*s\n", (unsigned)K, (unsigned)U.ore_lost, (unsigned)g_st.ore, (int)got, (const char *)rb);
    }

    printf("\n== 표 3. RX 링 크기 × main 이 읽는 주기 P (붙어서 오는 500바이트) → drop 개수 ==\n");
    const uint32_t caps[] = { 16, 32, 64, 128, 256 };
    const uint32_t Ps[] = { 50, 100, 200, 400, 800, 1600, 3200 };
    printf(" cap \\ P(tick) |");
    for (unsigned pi = 0; pi < 7; pi++) printf(" %5u", (unsigned)Ps[pi]);
    printf("\n");
    for (unsigned ci = 0; ci < 5; ci++) {
        printf(" %12u |", (unsigned)caps[ci]);
        for (unsigned pi = 0; pi < 7; pi++) {
            setup();
            g_rx.buf = big_rx; g_rx.mask = caps[ci] - 1u;  /* 같은 드라이버, 링만 키운다 */
            for (int i = 0; i < 500; i++) s_src[i] = (uint8_t)i;
            rx_feed(500, NULL);
            uint32_t got = 0;
            for (uint32_t t = 1; t <= 500u * 10u + 4000u; t++) {
                sim_step();
                if (t % Ps[pi] == 0) { uint8_t rb[256]; size_t k; while ((k = uart_read(rb, sizeof rb)) > 0) got += (uint32_t)k; }
            }
            if (got + g_st.rx_dropped != 500u) printf("(!)");
            printf(" %5u", (unsigned)g_st.rx_dropped);
        }
        printf("\n");
    }
    printf(" (참고: 링이 P 동안 버티려면 cap ≥ P/10 바이트 — 1 바이트 = 10 tick)\n");
    return 0;
}
