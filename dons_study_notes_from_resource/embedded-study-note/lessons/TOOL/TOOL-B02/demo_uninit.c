/* demo_uninit.c - TOOL-B02: which uninitialized reads does a compiler see?
 * Build: cc  -std=c11 -Wall -O0|-O2 -c demo_uninit.c
 *        gcc -std=c11 -Wall -O0|-O2 -c demo_uninit.c   (Lima VM)
 * -DONLY_MAYBE drops U1/U6 (the "always uninitialized" ones) so that only the
 * "maybe" cases remain: the -Werror debug-vs-release experiment.            */
#include <stdbool.h>
#include <stdint.h>

int u1_plain(void);
int u2_branch(int n);
int u3_outparam(void);
int u4_array(void);
int u5_loop(int n);
int u6_struct(void);
bool sensor_read(int *out);             /* defined in another file (opaque) */

#ifndef ONLY_MAYBE
int u1_plain(void)
{
    int x;
    return x + 1;                       /* U1: always uninitialized         */
}
#endif

int u2_branch(int n)
{
    int v;
    if (n > 0)                          /* U2: uninitialized when n <= 0    */
        v = n * 2;
    return v;
}

int u3_outparam(void)
{
    int v;
    (void)sensor_read(&v);              /* U3: callee may not write v       */
    return v;
}

int u4_array(void)
{
    int a[4];
    for (int i = 0; i < 3; i++)
        a[i] = i;
    return a[3];                        /* U4: a[3] never written           */
}

int u5_loop(int n)
{
    int last;
    for (int i = 0; i < n; i++)
        last = i;
    return last;                        /* U5: uninitialized when n <= 0    */
}

#ifndef ONLY_MAYBE
struct cfg { int baud; int parity; };
int u6_struct(void)
{
    struct cfg c;
    c.baud = 115200;
    return c.parity;                    /* U6: member never written         */
}
#endif
