/* ALGO-M08 Cortex-M 관찰용 — <stdint.h> 만 포함 (libc 없는 크로스 컴파일).
 * 이 맥에서 실제로 돌린 명령:
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -c -fstack-usage demo_target.c -o /tmp/ALGO-M08_t.o
 *   (같은 것을 thumbv6m-none-eabi -mcpu=cortex-m0 으로도)
 * 보는 것: 한 줄 편집 거리의 스택 사용량(row[65] 은 스택), 1D 배낭 안쪽 루프에 곱셈·나눗셈이 없는지.
 */
#include <stdint.h>

#define MAX_S 64

int ed_1row(const char *a, int n, const char *b, int m) {
    if (m > n) { const char *t = a; a = b; b = t; int u = n; n = m; m = u; }
    uint16_t row[MAX_S + 1];
    for (int j = 0; j <= m; j++) row[j] = (uint16_t)j;
    for (int i = 1; i <= n; i++) {
        uint16_t diag = row[0];
        row[0] = (uint16_t)i;
        for (int j = 1; j <= m; j++) {
            uint16_t up = row[j];
            int best = diag + (a[i - 1] != b[j - 1]);
            if (up + 1 < best) best = up + 1;
            if (row[j - 1] + 1 < best) best = row[j - 1] + 1;
            row[j] = (uint16_t)best;
            diag = up;
        }
    }
    return row[m];
}

/* dp 는 호출자가 준 W+1 칸 버퍼 (정적 RAM) */
uint32_t knap_1d(int n, const uint16_t *size, const uint16_t *value, uint32_t W, uint32_t *dp) {
    for (uint32_t w = 0; w <= W; w++) dp[w] = 0;
    for (int i = 0; i < n; i++) {
        uint32_t s = size[i], v = value[i];
        if (s > W) continue;
        for (uint32_t w = W + 1; w-- > s; ) {
            uint32_t t = dp[w - s] + v;
            if (t > dp[w]) dp[w] = t;
        }
    }
    return dp[W];
}
