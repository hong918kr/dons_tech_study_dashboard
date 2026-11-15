/* TOOL-B04 demo_link.c — supplies what anatomy.o left undefined, so we can
 * link and watch the relocation "holes" get filled.
 *   Mac : cc -O2 anatomy.c demo_link.c -o /tmp/TOOL-B04_work/linked && /tmp/TOOL-B04_work/linked
 *   VM  : clang -O2 anatomy.c demo_link.c -o /tmp/TOOL-B04_work/linked   (aarch64 Linux, PIE)
 */
#include <stdio.h>

int ext_counter = 1000;                          /* resolves anatomy.o's U ext_counter */
void ext_log(const char *msg, int v) { printf("log: %s %d\n", msg, v); }

extern int api_entry(int v);
extern const char *g_msg;

int main(void)
{
    int r = api_entry(5);                        /* 42 + 100 + board_init() (weak default 0) */
    printf("api_entry(5)=%d ext_counter=%d g_msg=%s\n", r, ext_counter, g_msg);
    return r == 142 && ext_counter == 1022 ? 0 : 1;   /* 1000 + (5*3+7) + 0 + 0 + 0 */
}
