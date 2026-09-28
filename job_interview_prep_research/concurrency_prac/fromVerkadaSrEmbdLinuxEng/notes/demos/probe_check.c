/* probe_check.c — 해시 함수 3종을 같은 키 집합으로 비교한다.
 *   cc -std=c11 -O2 -Wall -Wextra -o probe_check probe_check.c && ./probe_check
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CAP 1024u
#define MASK (CAP - 1u)
#define WINDOW 16u

static uint32_t h_mask(uint32_t id) { return id & MASK; }
static uint32_t h_mul_low(uint32_t id) { return (id * 2654435761u) & MASK; }   /* 07의 해답 */
static uint32_t h_mul_high(uint32_t id) { return (uint32_t)((id * 2654435761u) >> 22); } /* 32-10 */

typedef uint32_t (*hfn)(uint32_t);

static void run(const char *name, hfn h, const uint32_t *ids, int n)
{
    static bool used[CAP];
    static uint32_t key[CAP];
    memset(used, 0, sizeof used);
    memset(key, 0, sizeof key);
    long probes = 0, worst = 0, lost = 0, placed = 0;
    for (int i = 0; i < n; i++) {
        uint32_t home = h(ids[i]);
        long empty = -1;
        uint32_t p;
        for (p = 0; p < WINDOW; p++) {
            uint32_t s = (home + p) & MASK;
            if (!used[s]) { empty = (long)s; break; }
            if (key[s] == ids[i]) break;
        }
        probes += (long)p + 1;
        if ((long)p + 1 > worst) worst = (long)p + 1;
        if (empty < 0) { lost++; continue; }
        used[empty] = true; key[empty] = ids[i]; placed++;
    }
    printf("  %-12s  placed=%3ld  refused(window full)=%3ld  avg probes=%.2f  worst=%ld\n",
           name, placed, lost, (double)probes / n, worst);
}

int main(void)
{
    uint32_t ids[600];
    hfn fns[3] = { h_mask, h_mul_low, h_mul_high };
    const char *nm[3] = { "id & MASK", "mul>>low bits", "mul>>high bits" };

    printf("A) 600 consecutive badge ids (10000..10599)\n");
    for (int i = 0; i < 600; i++) ids[i] = 10000u + (uint32_t)i;
    for (int f = 0; f < 3; f++) run(nm[f], fns[f], ids, 600);

    printf("B) 600 ids with a 10-bit facility code in the LOW bits: (seq<<10)|0x2a\n");
    for (int i = 0; i < 600; i++) ids[i] = ((uint32_t)i << 10) | 0x2au;
    for (int f = 0; f < 3; f++) run(nm[f], fns[f], ids, 600);

    printf("C) 600 ids in 8 sites, stride 4096: site*4096 + seq (75 per site)\n");
    for (int i = 0; i < 600; i++) ids[i] = (uint32_t)(i / 75) * 4096u + (uint32_t)(i % 75) * 8u;
    for (int f = 0; f < 3; f++) run(nm[f], fns[f], ids, 600);
    return 0;
}
