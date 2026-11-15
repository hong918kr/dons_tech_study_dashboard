/* DS-A02 demo_rx_chunk.c — UART RX DMA 한 번의 길이 상한(UART_RX_CHUNK)이 유실에 미치는 영향 (결정적, solution.c 를 그대로 포함)
 *
 *   for c in 0 8 16 32; do cc -std=c11 -O2 -DUART_RX_CHUNK=$c demo_rx_chunk.c -o rc && ./rc $c; done
 *   (0 = 상한 없음: 빈 곳을 전부 예약)
 */
#define main solution_main
#include "solution.c"
#undef main

int main(int argc, char **argv) {
    printf("UART_RX_CHUNK=%s  (64 B 버퍼, 6000 B 스트림, 시드 40개 합계 유실 바이트)\n", argc > 1 ? argv[1] : "?");
    printf("  ISR 확률/접근 |");
    for (uint32_t p = 6; p <= 30; p += 6) printf(" %3u%%", p);
    printf("\n  유실 바이트   |");
    for (uint32_t p = 6; p <= 30; p += 6) {
        uint32_t tot = 0;
        for (uint32_t seed = 1; seed <= 40u; seed++) {
            g_rng = seed * 2654435761u;
            uint32_t lost = 0;
            (void)rx_run(6000u, 1u, 5u, true, NEVER, 0, p, &lost);
            tot += lost;
        }
        printf(" %4u", tot);
    }
    printf("\n");
    return 0;
}
