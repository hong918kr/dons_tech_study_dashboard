/* ALGO-A12 demo: RAM 안 핵심 루틴을 Cortex-M4 용으로 컴파일해 스택 사용량(-fstack-usage)과 코드 모양을 본다.
 * freestanding (<stdint.h> 만). solution.c 의 sort_run / rheap_down / mless+mheap_down 과 같은 본문.
 * build: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -fstack-usage -c demo_m4.c -o "$SCRATCH/demo_m4.o"
 *        && cat "$SCRATCH/demo_m4.su"      (.su 파일은 -o 와 같은 폴더에 생긴다)
 *        clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4.c -o -   (asm)
 */
#include <stdint.h>

typedef struct { uint32_t key, seq, pay0, pay1; } rec_t;
typedef struct { uint32_t next, left; uint16_t pos, cnt; } mrun_t;
#define RPB 16u

void sort_run(rec_t *a, uint32_t n) {
    for (uint32_t i = 1; i < n; i++) {
        rec_t x = a[i];
        uint32_t j = i;
        while (j > 0 && a[j - 1].key > x.key) { a[j] = a[j - 1]; j--; }
        a[j] = x;
    }
}

void rheap_down(rec_t *h, uint32_t n, uint32_t i) {
    rec_t x = h[i];
    for (;;) {
        uint32_t c = 2 * i + 1;
        if (c >= n) break;
        if (c + 1 < n && h[c + 1].key < h[c].key) c++;
        if (h[c].key >= x.key) break;
        h[i] = h[c];
        i = c;
    }
    h[i] = x;
}

static int mless(const rec_t *ib, const mrun_t *st, uint16_t a, uint16_t b) {
    uint32_t ka = ib[a * RPB + st[a].pos].key, kb = ib[b * RPB + st[b].pos].key;
    return ka < kb || (ka == kb && a < b);
}
void mheap_down(uint16_t *hp, uint32_t n, uint32_t i, const rec_t *ib, const mrun_t *st) {
    uint16_t x = hp[i];
    for (;;) {
        uint32_t c = 2 * i + 1;
        if (c >= n) break;
        if (c + 1 < n && mless(ib, st, hp[c + 1], hp[c])) c++;
        if (!mless(ib, st, hp[c], x)) break;
        hp[i] = hp[c];
        i = c;
    }
    hp[i] = x;
}
