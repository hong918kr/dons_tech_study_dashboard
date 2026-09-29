/* cluster_check.c — 덩어리(cluster)가 나중에 오는 키를 어떻게 괴롭히는가 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CAP 1024u
#define MASK (CAP - 1u)
static uint32_t h_mask(uint32_t id) { return id & MASK; }
static uint32_t h_mul_low(uint32_t id) { return (id * 2654435761u) & MASK; }
static uint32_t h_mul_high(uint32_t id) { return (uint32_t)((id * 2654435761u) >> 22); }
typedef uint32_t (*hfn)(uint32_t);
static bool used[CAP]; static uint32_t key[CAP];
static uint64_t rs = 12345;
static uint32_t rnd(void){ rs^=rs<<13; rs^=rs>>7; rs^=rs<<17; return (uint32_t)(rs>>11); }
static void go(const char *nm, hfn h)
{
    memset(used,0,sizeof used); memset(key,0,sizeof key);
    for (uint32_t i = 0; i < 700u; i++) {            /* 700 consecutive ids first */
        uint32_t id = 10000u + i, home = h(id);
        for (uint32_t p = 0; p < CAP; p++) { uint32_t s=(home+p)&MASK;
            if (!used[s]) { used[s]=true; key[s]=id; break; } }
    }
    rs = 12345;
    long probes = 0, worst = 0;
    for (int i = 0; i < 100; i++) {                  /* then 100 unrelated ids */
        uint32_t id = 0x50000000u | rnd(), home = h(id);
        uint32_t p = 0;
        while (used[(home+p)&MASK]) p++;
        probes += p + 1; if ((long)p+1 > worst) worst = (long)p+1;
        used[(home+p)&MASK] = true; key[(home+p)&MASK] = id;
    }
    printf("  %-14s avg probes for the 100 latecomers=%.2f  worst=%ld\n", nm, (double)probes/100, worst);
}
int main(void){
    printf("D) 700 consecutive ids fill the table to 68%%, then 100 unrelated ids arrive\n");
    go("id & MASK", h_mask); go("mul>>low bits", h_mul_low); go("mul>>high bits", h_mul_high);
    return 0;
}
