/* ARM-B10 demo_idiom: what the compiler does with a plain C byte-copy loop
 *   clang -O2 -S -fno-asynchronous-unwind-tables demo_idiom.c -o -              # Mac
 *   clang -O2 -S -fno-asynchronous-unwind-tables -fno-builtin demo_idiom.c -o -
 *   gcc -O2 -S -ffreestanding -DMY_MEMCPY demo_idiom.c -o -                       # Lima VM (aarch64 Linux)
 *   gcc -O2 -S -ffreestanding -fno-tree-loop-distribute-patterns -DMY_MEMCPY demo_idiom.c -o -
 */
#include <stddef.h>

/* no restrict: d and s may overlap -> compiler may NOT call memcpy (needs a runtime overlap check) */
void *copy_loop(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

/* restrict: "no overlap" promised -> the loop IS memcpy */
void *copy_loop_restrict(void *restrict dst, const void *restrict src, size_t n) {
    unsigned char *restrict d = dst;
    const unsigned char *restrict s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

#ifdef MY_MEMCPY
/* a firmware mini-libc providing its own memcpy, written as the same loop */
void *memcpy(void *restrict dst, const void *restrict src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}
#endif
