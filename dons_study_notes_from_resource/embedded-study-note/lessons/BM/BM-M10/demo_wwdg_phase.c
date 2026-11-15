/* BM-M10 demo: WWDG refresh 시각 vs 결과 — 프리스케일러 위상 4096 가지 전부
 * cc -std=c11 -Wall -Wextra -Werror -O2 demo_wwdg_phase.c -o demo_wwdg_phase && ./demo_wwdg_phase
 *
 * 모델 = solution.c 의 mock 과 같다: 1 µs 마다 누산기에 PCLK1 을 더하고, 4096·2^WDGTB·1e6 을 넘을 때마다 카운터 -1.
 * 마지막 refresh 순간의 누산기 값(= 위상 φ, PCLK1 사이클 단위 0..4095)은 모른다 → 전부 세어 본다.
 * refresh 를 t µs 뒤에 하면:  카운터 > W → EARLY 리셋,  이미 0x3F 까지 내려갔으면 RESET(이미 리셋됨),  아니면 OK.
 */
#include <stdint.h>
#include <stdio.h>

#define PCLK 42000000u
#define TB   0u
#define T    0x7Fu
#define W    0x5Fu

static void row(uint32_t t_us) {
    unsigned early = 0, ok = 0, reset = 0;
    uint64_t per = (uint64_t)(4096u << TB) * 1000000u;
    for (uint32_t phi = 0; phi < (4096u << TB); phi++) {
        uint64_t dec = ((uint64_t)phi * 1000000u + (uint64_t)t_us * PCLK) / per;   /* t µs 동안 일어난 감소 횟수 */
        if (dec >= T - 0x3Fu) reset++;
        else if (T - dec > W) early++;
        else ok++;
    }
    double n = (double)(4096u << TB);
    printf("%6u   %6.1f%%  %6.1f%%  %6.1f%%\n", t_us, 100.0 * early / n, 100.0 * ok / n, 100.0 * reset / n);
}

int main(void) {
    printf("WDGTB=%u T=0x%02X W=0x%02X PCLK1=%u Hz, tick=%.3f us\n", TB, T, W, PCLK, (4096.0 * (1u << TB)) / 42.0);
    printf("t(us)    EARLY     OK       RESET\n");
    static const uint32_t ts[] = { 3000, 3023, 3024, 3050, 3100, 3120, 3121, 3200,
                                   6000, 6046, 6047, 6100, 6144, 6145, 6200, 6241, 6242, 6300 };
    for (unsigned i = 0; i < sizeof ts / sizeof ts[0]; i++) row(ts[i]);
    return 0;
}
