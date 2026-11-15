/* BM-M08 demo — ISR 플래그 클리어 버그 두 가지를 tick 단위로 추적 (solution.c 모델 사용, 관찰용)
 *   1) "콜백 뒤 그룹 전체 클리어": HT 콜백(11 tick) 도중 선 TC 가 끝의 LIFCR 쓰기로 지워진다
 *      vs 정답 핸들러 (본 비트만 먼저 지움) — TC 가 pending 으로 남아 바로 다시 들어온다
 *   2) "읽자마자 그룹 전체 클리어": 읽기와 쓰기 사이 1 tick 에 선 플래그가 안 보인 채 지워진다 (HT 콜백 19 tick)
 * 빌드 (레슨 폴더에서): cc -std=c11 -Wall -Wextra -Werror -O2 demo_trace.c -o demo_trace && ./demo_trace
 */
#define BM_M08_NO_MAIN 1
#pragma GCC diagnostic ignored "-Wunused-function"   /* main 이 쓰던 테스트 도우미 일부를 여기선 안 쓴다 */
#include "solution.c"

static void show(const char *what) {
    printf("  t=%3llu %-16s LISR[21:16]=0x%02X\n", (unsigned long long)MK.now, what,
           (unsigned)((D.isr[0] >> 16) & 0x3Fu));
}
static void clear_all_at_end(void) {
    show("ISR enter");
    uint32_t f = (REG_READ(DMA2_BASE + DMA_LISR) >> 16) & DMA_GROUP_MASK;
    show("after R LISR");
    if (f & (1u << DMA_HTIF)) cb_half(NULL);
    if (f & (1u << DMA_TCIF)) cb_full(NULL);
    show("callbacks done");
    REG_WRITE(DMA2_BASE + DMA_LIFCR, DMA_GROUP_MASK << 16);
    show("after W LIFCR");
}
static void correct(void) {
    show("ISR enter");
    DMA2_Stream2_IRQHandler();
    show("ISR exit");
}
static void clear_all_early(void) {
    uint64_t t = MK.now;
    uint32_t f = (REG_READ(DMA2_BASE + DMA_LISR) >> 16) & DMA_GROUP_MASK;
    uint32_t at_write = (D.isr[0] >> 16) & DMA_GROUP_MASK;          /* 쓰기 직전 실제 플래그 (백도어) */
    REG_WRITE(DMA2_BASE + DMA_LIFCR, DMA_GROUP_MASK << 16);
    if (MK.now < 160u)
        printf("  ISR t=%3llu read=0x%02X  flags at W=0x%02X%s\n", (unsigned long long)t, (unsigned)f,
               (unsigned)at_write, (at_write & ~f) ? "  <-- set between R and W, cleared unseen" : "");
    if (f & (1u << DMA_HTIF)) cb_half(NULL);
    if (f & (1u << DMA_TCIF)) cb_full(NULL);
}

int main(void) {
    printf("1a) clear-all-at-end, N=8, 1 byte / 3 ticks, HT callback 11 ticks, 16 bytes\n");
    rx_scenario(8u, 16u, 3u, 11u, clear_all_at_end);
    printf("    -> half=%u full=%u\n", app.half, app.full);
    printf("1b) correct handler (clear seen bits first), same timing\n");
    rx_scenario(8u, 16u, 3u, 11u, correct);
    printf("    -> half=%u full=%u\n", app.half, app.full);
    printf("2) clear-all-early, HT callback 19 ticks, 512 bytes (first lines)\n");
    rx_scenario(8u, 512u, 3u, 19u, clear_all_early);
    printf("    -> half=%u full=%u (of 64 + 64)\n", app.half, app.full);
    return 0;
}
