/* ARM-M07 demo_trace: one neon_memchr call to single-step in lldb (see demo_trace.lldb)
 *   cc -std=c11 -g -O0 demo_trace.c solution.S -o /tmp/ARM-M07_trace && lldb -b -s demo_trace.lldb /tmp/ARM-M07_trace
 */
#include <stdio.h>
#include <stddef.h>
extern const void *neon_memchr(const void *p, int c, size_t n);
int main(void) {
    static const char frame[20] = "hello~world~~xyz!?";       /* '~' = 0x7E at 5, 11, 12 */
    const char *r = neon_memchr(frame, '~', 18);
    printf("index = %td\n", r - frame);
    return 0;
}
