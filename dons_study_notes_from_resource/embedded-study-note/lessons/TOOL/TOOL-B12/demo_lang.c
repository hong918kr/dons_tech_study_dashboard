/* TOOL-B12 demo: same source as C and as C++ -> which checks fire?
 *   cc  -std=c11   -g -O1 -fsanitize=undefined demo_lang.c -o dl_c && ./dl_c
 *   c++ -x c++ -std=c++17 -g -O1 -fsanitize=undefined demo_lang.c -o dl_17 && ./dl_17
 *   c++ -x c++ -std=c++20 ... -o dl_20 && ./dl_20 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum pwr_mode { PWR_RUN, PWR_SLEEP, PWR_STOP };

int main(int argc, char **argv)
{
    int wire = argc > 1 ? atoi(argv[1]) : 7;      /* byte from a packet */
    int n = argc > 2 ? atoi(argv[2]) : 31;        /* shift count */
    enum pwr_mode m;
    memcpy(&m, &wire, sizeof m);                  /* enum object holds 7 */
    int is_stop = (m == PWR_STOP);                /* (a) load of the enum */
    int bit = 1 << n;                             /* (b) 1 << 31 */
    printf("is_stop=%d bit=%d\n", is_stop, bit);
    return 0;
}
