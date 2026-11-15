/* ALGO-A02 demo_fw_m4 — 플로이드-워셜 안쪽 루프가 Cortex-M4 에서 몇 개의 명령이 되나 (asm 보기 전용, 실행 안 함)
 * freestanding: <stdint.h> 만. 가드(INF 건너뛰기 + FW_NEG_INF 포화) 와 next 갱신까지 본문 fw_run 과 같은 모양.
 * build: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_fw_m4.c -o -
 */
#include <stdint.h>

#define N 256
#define FW_INF INT32_MAX
#define FW_NEG_INF (-(1 << 30))

int32_t d[N][N];     /* 256 KiB — 대부분의 MCU SRAM 보다 크다 (5. 풀이 Part C 메모리 표) */
uint8_t nx[N][N];    /*  64 KiB */

void fw_run_m4(int n) {
    for (int k = 0; k < n; k++)
        for (int i = 0; i < n; i++) {
            int32_t dik = d[i][k];
            if (dik == FW_INF) continue;
            uint8_t hop = nx[i][k];
            for (int j = 0; j < n; j++) {
                int32_t dkj = d[k][j];
                if (dkj == FW_INF) continue;
                int32_t s = dik + dkj;
                if (s < FW_NEG_INF) s = FW_NEG_INF;
                if (s < d[i][j]) { d[i][j] = s; nx[i][j] = hop; }
            }
        }
}
