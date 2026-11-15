/* ALGO-B11: Cortex-M4 에서 재귀 flood fill 한 프레임이 몇 바이트인지 asm 으로 본다 (실행 파일 아님).
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S cm4_ff_rec.c -o - | grep -A12 '^ff_rec:'
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O0 -S cm4_ff_rec.c -o - | grep -A12 '^ff_rec:'
 * libc 헤더 없이 <stdint.h> 만 쓴다. 계측(depth_t) 없는 순수 재귀판.
 */
#include <stdint.h>

typedef struct { uint8_t *cell; int w, h; } grid_t;

int ff_rec(grid_t *g, int r, int c, uint8_t from, uint8_t to) {
    if (from == to || r < 0 || r >= g->h || c < 0 || c >= g->w || g->cell[r * g->w + c] != from) return 0;
    g->cell[r * g->w + c] = to;
    int n = 1;
    n += ff_rec(g, r - 1, c, from, to);
    n += ff_rec(g, r + 1, c, from, to);
    n += ff_rec(g, r, c - 1, from, to);
    n += ff_rec(g, r, c + 1, from, to);
    return n;
}
