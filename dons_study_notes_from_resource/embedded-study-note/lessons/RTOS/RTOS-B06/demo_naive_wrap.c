/* RTOS-B06 데모 — tick wrap 근처에서 "now >= wake" 와 날값(raw) 정렬이 어떻게 틀리는지
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_naive_wrap.c -o demo_naive_wrap && ./demo_naive_wrap
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static bool naive(uint32_t now, uint32_t wake) { return now >= wake; }
static bool safe(uint32_t now, uint32_t wake) { return (uint32_t)(now - wake) < 0x80000000u; }

static void sort_wakes(uint32_t *w, int n, uint32_t now, bool raw) {   /* 삽입 정렬 */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0; j--) {
            bool swap = raw ? w[j] < w[j - 1] : (uint32_t)(w[j] - now) < (uint32_t)(w[j - 1] - now);
            if (!swap) break;
            uint32_t t = w[j]; w[j] = w[j - 1]; w[j - 1] = t;
        }
}

int main(void) {
    uint32_t now = 0xFFFFFFF0u, wake = now + 20;              /* vTaskDelay(20) 를 0xFFFFFFF0 에서 */
    printf("delay(20) at 0x%08x -> wake 0x%08x\n", (unsigned)now, (unsigned)wake);
    printf("  naive now>=wake at the same tick : %s\n", naive(now, wake) ? "TRUE  (wakes after 0 ticks!)" : "false");
    printf("  safe  (now-wake) < 2^31          : %s\n", safe(now, wake) ? "TRUE" : "false (keeps sleeping)");

    uint32_t base[4] = { now + 5, now + 20, now + 32, now + 6 };  /* f5, 04, 10, f6 */
    for (int raw = 1; raw >= 0; raw--) {
        uint32_t w[4] = { base[0], base[1], base[2], base[3] };
        sort_wakes(w, 4, now, raw);
        printf("%s sorted list :", raw ? "raw " : "safe");
        for (int i = 0; i < 4; i++) printf(" %08x", (unsigned)w[i]);
        uint32_t t = now + 1;                                     /* 다음 tick 의 expire 루프 */
        int popped = 0;
        while (popped < 4 && (raw ? naive(t, w[popped]) : safe(t, w[popped]))) popped++;
        printf("   -> expire at 0x%08x pops %d task(s)\n", (unsigned)t, popped);
    }
    uint32_t d = 0x80000000u;
    printf("half range: now - wake = 0x%08x -> safe(now, wake) = %d and safe(wake, now) = %d (both 'not reached')\n",
           (unsigned)d, safe(d, 0), safe(0, d));
    return 0;
}
