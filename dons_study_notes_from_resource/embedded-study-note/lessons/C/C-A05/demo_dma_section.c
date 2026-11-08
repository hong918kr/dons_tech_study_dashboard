/* C-A05 demo: 펌웨어의 "정적" DMA 버퍼 — 정렬과 섹션 지정이 어셈블리에 어떻게 나오나
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m7 -mthumb -O2 -S demo_dma_section.c -o -
 * freestanding: <stdint.h>/<stddef.h> 만 쓴다 (libc 없음).
 */
#include <stddef.h>
#include <stdint.h>

#define CACHE_LINE 32u   /* Cortex-M7 L1 D-cache line */

/* ① 캐시 라인 정렬 + 전용 섹션 (링커 스크립트가 MPU non-cacheable 영역에 배치) */
__attribute__((section(".dma_buf"))) _Alignas(CACHE_LINE) uint8_t eth_rx[4][1536];

/* ② 4 KB 정렬된 DMA 아레나: 이 안에서 arena_alloc 이 4KB 경계를 안 넘게 잘라 쓴다.
 *    섹션 이름을 .bss.* 로 지으면 %nobits (0 초기화, 이미지 크기 0) 가 된다 — ① 과 비교 */
__attribute__((section(".bss.dma_arena"))) _Alignas(4096) uint8_t dma_arena[16384];

/* ③ 정렬 지정 없는 일반 버퍼 — 비교용 */
uint8_t plain_buf[100];

/* 버퍼 주소를 DMA 주소 레지스터에 쓰는 흉내 (eth_rx[i] = base + i*1536, 1536 = 3*512) */
void dma_set_addr(volatile uint32_t *reg, unsigned i) {
    *reg = (uint32_t)(uintptr_t)eth_rx[i];
}
