/* ALGO-M04 demo_cm4_row — 비트보드 층 BFS(demo_bench V4)의 한 행 계산을 Cortex-M4 와 arm64 에서 비교
 * 64열 행 = uint64_t. 32비트 코어에서는 시프트가 두 워드로 쪼개진다 (lsl/lsr #31, lsrs + rrx).
 * <stdint.h> 만 쓰므로 베어메탈 타깃으로 바로 컴파일된다. 실행 파일이 아니라 asm 을 보는 파일.
 * build: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_cm4_row.c -o -
 *        clang -O2 -S demo_cm4_row.c -o -          (이 맥 arm64)
 */
#include <stdint.h>

uint64_t row_step(const uint64_t *fr, const uint64_t *freem, const uint64_t *vis, int r) {
    uint64_t m = (fr[r] << 1) | (fr[r] >> 1) | fr[r - 1] | fr[r + 1];
    return m & freem[r] & ~vis[r];
}
