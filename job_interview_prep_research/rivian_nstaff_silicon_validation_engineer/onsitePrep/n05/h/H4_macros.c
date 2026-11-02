/* H4 · macro traps — each "BAD_" macro next to its fix; all well-defined. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define BAD_SQUARE(x)  x * x
#define SQUARE(x)      ((x) * (x))
#define BAD_DOUBLE(x)  (x) + (x)
#define DOUBLE(x)      ((x) + (x))
#define BAD_BIT(n)     1u << n
#define BIT(n)         (1u << (n))
#define MAX(a, b)      ((a) > (b) ? (a) : (b))      /* still evaluates twice */

/* multi-statement macro: BAD without do{}while(0) */
#define BAD_RESET(s)   (s).len = 0; (s).err = 0
#define RESET(s)       do { (s).len = 0; (s).err = 0; } while (0)

struct chan { int len, err; };

static int fifo_reads;                       /* stub "RX FIFO": pops 5, 9, 2 ... */
static int read_fifo(void) { static const int v[] = {5, 9, 2}; return v[fifo_reads++ % 3]; }

static inline int max_int(int a, int b) { return a > b ? a : b; }   /* the fix */

int main(void) {
    int a = 3;
    printf("BAD_SQUARE(a+1)=%d  SQUARE(a+1)=%d\n", BAD_SQUARE(a + 1), SQUARE(a + 1));
    assert(BAD_SQUARE(a + 1) == 7 && SQUARE(a + 1) == 16);     /* a+1*a+1 */

    printf("10/BAD_DOUBLE(5)=%d  10/DOUBLE(5)=%d\n", 10 / BAD_DOUBLE(5), 10 / DOUBLE(5));
    assert(10 / BAD_DOUBLE(5) == 7 && 10 / DOUBLE(5) == 1);    /* 10/5+5 */

    uint32_t m_bad = ~BAD_BIT(3), m_ok = ~BIT(3);
    printf("~BAD_BIT(3)=0x%08X  ~BIT(3)=0x%08X\n", (unsigned)m_bad, (unsigned)m_ok);
    assert(m_bad == 0xFFFFFFF0u && m_ok == 0xFFFFFFF7u);       /* (~1u)<<3 */

    int i = 5, j = 3;
    int r = MAX(i++, j);              /* ?: has a sequence point -> defined */
    printf("MAX(i++, j) = %d, i is now %d\n", r, i);
    assert(r == 6 && i == 7);         /* i incremented TWICE */

    fifo_reads = 0;
    int m = MAX(read_fifo(), 0);      /* hardware read evaluated twice! */
    printf("MAX(read_fifo(), 0) = %d after %d FIFO pops\n", m, fifo_reads);
    assert(m == 9 && fifo_reads == 2); /* compared 5, returned 9: data lost */

    fifo_reads = 0;
    m = max_int(read_fifo(), 0);
    assert(m == 5 && fifo_reads == 1);

    struct chan c = {7, 1};
    int enabled = 0;
    if (enabled) BAD_RESET(c);        /* expands to: if(enabled) c.len=0; c.err=0; */
    printf("BAD_RESET under false if: len=%d err=%d\n", c.len, c.err);
    assert(c.len == 7 && c.err == 0); /* err cleared anyway */

    c.err = 1;
    if (enabled) RESET(c); else c.len = 8;   /* with BAD_RESET this "else" won't compile */
    assert(c.len == 8 && c.err == 1);

    puts("H4 PASS");
    return 0;
}
