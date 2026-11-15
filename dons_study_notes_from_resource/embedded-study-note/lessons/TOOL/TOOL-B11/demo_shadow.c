/* TOOL-B11 demo — read ASan shadow memory by hand: Shadow = (Addr >> 3) + Offset
 *   cc -std=c11 -g -O1 -fsanitize=address demo_shadow.c -o ds && ./ds
 * Prints the runtime's (scale, offset), then the shadow bytes around a heap
 * block, a stack array and a global array, computed with the formula above.
 */
#include <sanitizer/asan_interface.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static size_t g_scale, g_off;
static char g_tbl[13];                       /* 13-byte global → 00 05 + f9 redzone */

__attribute__((no_sanitize("address")))  /* reads shadow memory directly */
static void dump(const char *what, const void *p, size_t before, size_t nshadow)
{
    uintptr_t a = (uintptr_t)p;
    uintptr_t base = (a & ~(uintptr_t)7) - before * 8;          /* 8-byte granule start */
    printf("%-14s addr=0x%012lx shadow=0x%012lx :", what, (unsigned long)a,
           (unsigned long)((a >> g_scale) + g_off));
    for (size_t i = 0; i < nshadow; i++) {
        const uint8_t *s = (const uint8_t *)((base + i * 8) >> g_scale) + g_off;
        printf(" %02x", *s);
    }
    printf("   poisoned(p[0])=%d\n", __asan_address_is_poisoned(p));
}

__attribute__((noinline)) static void on_stack(void)
{
    volatile char buf[10];                  /* 10-byte local → 00 02 + f1/f3 redzones */
    buf[0] = 1;
    dump("stack buf[10]", (const void *)buf, 4, 8);
}

int main(void)
{
    __asan_get_shadow_mapping(&g_scale, &g_off);
    printf("shadow scale=%zu (1 shadow byte = %d app bytes)  offset=0x%zx\n",
           g_scale, 1 << g_scale, g_off);

    char *h = malloc(10);                   /* 10-byte heap block → 00 02 + fa redzone */
    dump("heap malloc(10)", h, 2, 6);
    free(h);
    dump("heap after free", h, 2, 6);       /* freed → fd (in quarantine) */
    on_stack();
    dump("global[13]", g_tbl, 2, 8);
    return 0;
}
