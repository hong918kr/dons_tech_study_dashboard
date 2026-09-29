#include <stdint.h>
#define CAP 1024u                 /* 2의 거듭제곱: & 로 나머지 계산 */
#define MASK (CAP - 1u)
#define WIN_US 1000000ull         /* 창 = 1 s */

typedef struct { uint64_t ts; int v; } item_t;

static item_t q[CAP];
static uint32_t f, b;             /* free-running: 개수 = b - f */

static void push_min(uint64_t ts, int v)
{
    while (f != b && q[(b - 1u) & MASK].v >= v)   /* (1) 뒤에서 걷어내기 */
        b--;
    q[b & MASK] = (item_t){ ts, v };              /* (2) 붙이기 */
    b++;
}

static void expire(uint64_t now)
{
    while (f != b && now - q[f & MASK].ts > WIN_US)  /* (3) 앞에서 만료 */
        f++;
}

static int get_min(uint64_t now)
{
    expire(now);
    return (f == b) ? -32768 : q[f & MASK].v;      /* (4) front 가 답 */
}
int main(void){ push_min(1000,-70); push_min(2000,-85); return get_min(2000)== -85 ? 0 : 1; }
