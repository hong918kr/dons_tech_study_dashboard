/* ALGO-M03 demo_m4: 비트마스크 N-Queens 재귀를 Cortex-M4 / Cortex-M0 용으로 컴파일해 프레임 크기를 본다.
 * <stdint.h> 만 쓴다 (베어메탈 타깃에 libc 헤더 없음). 실행하지 않고 asm 만 본다.
 * build: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4.c -o -
 *        clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_m4.c -o -
 */
#include <stdint.h>

uint32_t nq_nodes;   /* 노드 수 (전역: 포인터 인자 하나를 줄인다) */

uint32_t nq_bits_rec(uint32_t full, uint32_t cols, uint32_t d1, uint32_t d2) {
    nq_nodes++;
    if (cols == full) return 1;
    uint32_t cnt = 0;                       /* n <= 15 면 해 개수 < 2^32 */
    uint32_t avail = full & ~(cols | d1 | d2);
    while (avail) {
        uint32_t bit = avail & (0u - avail);
        avail ^= bit;
        cnt += nq_bits_rec(full, cols | bit, (d1 | bit) << 1, (d2 | bit) >> 1);
    }
    return cnt;
}
