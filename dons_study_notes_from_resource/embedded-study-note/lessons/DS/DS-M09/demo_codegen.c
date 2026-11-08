/* DS-M09 demo_codegen — hb_highest 가 Cortex-M 에서 몇 명령이 되나 (실행 파일 아님, 어셈블리만 본다)
 * cc --target=thumbv7em-none-eabi -mcpu=cortex-m4    -ffreestanding -O2 -S -o - demo_codegen.c
 * cc --target=thumbv6m-none-eabi  -mcpu=cortex-m0plus -ffreestanding -O2 -S -o - demo_codegen.c
 */
#include <stdint.h>
typedef struct { uint32_t top; uint32_t leaf[32]; } hbm_t;
int hb_highest(const hbm_t *h) {
    if (h->top == 0) return -1;
    uint32_t g = 31u - (uint32_t)__builtin_clz(h->top);
    return (int)(g * 32u + 31u - (uint32_t)__builtin_clz(h->leaf[g]));
}
