/* ARM-A09 demo_elf_main.c — use_all(0) == 48951, g_ptr == &g_val 을 확인 */
#include <stdio.h>
long use_all(long x);
extern long g_val, *g_ptr;
int main(void) {
    long r = use_all(0);
    printf("use_all(0)=%ld g_ptr==&g_val:%d\n", r, g_ptr == &g_val);
    return !(r == 48951 && g_ptr == &g_val);
}
