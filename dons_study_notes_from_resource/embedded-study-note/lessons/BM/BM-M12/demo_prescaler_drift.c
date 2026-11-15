/* BM-M12 demo — 프리스케일러 선택 표, 전수(brute force) 대조, 클럭원별 하루 드리프트
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_prescaler_drift.c -o /tmp/BM-M12_demo && /tmp/BM-M12_demo
 * (rtc_prescaler 는 solution.c 와 같은 코드. 수치는 이 모델의 계산 결과이고, LSI 범위 등 칩 값은 데이터시트 확인 권장) */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int rtc_prescaler(uint32_t f, uint32_t *pa, uint32_t *ps, int32_t *ppm) {
    if (f == 0u) return -1;
    uint64_t bn = 0, be = 0; uint32_t ba = 0, bs = 0;
    for (uint32_t a = 128u; a >= 1u; a--) {
        uint64_t s0 = f / a;
        for (uint64_t k = 0; k < 2u; k++) {
            uint64_t s = s0 + k;
            if (s < 1u) s = 1u;
            if (s > 32768u) s = 32768u;
            uint64_t n = a * s, e = f > n ? f - n : n - f;
            if (bn == 0u || e * bn < be * n) { bn = n; be = e; ba = a; bs = (uint32_t)s; }
        }
    }
    *pa = ba - 1u; *ps = bs - 1u;
    int64_t num = ((int64_t)f - (int64_t)bn) * 1000000, n = (int64_t)bn;
    *ppm = (int32_t)((num >= 0 ? num + n / 2 : num - n / 2) / n);
    return be == 0u ? 0 : 1;
}
static void brute(uint32_t f, uint32_t *pa, uint32_t *ps) {
    uint64_t bn = 0, be = 0;
    for (uint32_t a = 128; a >= 1; a--)
        for (uint32_t s = 1; s <= 32768u; s++) {
            uint64_t n = (uint64_t)a * s, e = f > n ? f - n : n - f;
            if (bn == 0 || e * bn < be * n) { bn = n; be = e; *pa = a - 1; *ps = s - 1; }
        }
}

int main(void) {
    printf("1) 클럭원별 PREDIV 선택 (A 최대 우선)\n");
    printf("   %-26s %9s %5s %6s %8s %9s %9s\n", "RTCCLK", "Hz", "A", "S", "ppm", "ck_apre", "SS res");
    static const struct { const char *name; uint32_t f; } src[] = {
        { "LSE 32.768 kHz", 32768u }, { "LSI nominal 32 kHz (F4)", 32000u }, { "LSI 40 kHz (other parts)", 40000u },
        { "HSE 8 MHz / RTCPRE 8", 1000000u }, { "HSE 25 MHz / RTCPRE 25", 1000000u }, { "HSE 25 MHz / RTCPRE 31", 806452u },
        { "LSI measured 31 337 Hz", 31337u },
    };
    for (size_t i = 0; i < sizeof src / sizeof src[0]; i++) {
        uint32_t a, s; int32_t ppm;
        int rc = rtc_prescaler(src[i].f, &a, &s, &ppm);
        double apre = (double)src[i].f / (a + 1);
        printf("   %-26s %9u %5u %6u %8d %7.1f Hz %6.0f us%s\n", src[i].name, src[i].f, a, s, ppm, apre,
               1e6 / apre, rc ? "  (inexact)" : "");
    }

    printf("\n2) 32.768 kHz 에서 정확한 (A+1)(S+1)=32768 조합: 전력(동기 카운터 클럭) vs 서브초 분해능\n");
    printf("   %5s %6s %10s %12s\n", "A+1", "S+1", "ck_apre Hz", "SS res (us)");
    for (uint32_t a1 = 1; a1 <= 128u; a1 *= 2u)
        printf("   %5u %6u %10u %12.2f\n", a1, 32768u / a1, 32768u / a1, 1e6 * a1 / 32768.0);

    printf("\n3) rtc_prescaler vs brute force (모든 A,S = 4,194,304 조합)\n");
    uint32_t seed = 2463534242u; int n = 0, bad = 0;
    for (int i = 0; i < 160; i++) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        uint32_t f = 1u + seed % 4300000u;
        if (i < 40) f = 31000u + (uint32_t)i * 53u;               /* LSI/LSE 근처 */
        uint32_t a, s, ba = 0, bs = 0; int32_t ppm;
        rtc_prescaler(f, &a, &s, &ppm); brute(f, &ba, &bs);
        n++; if (a != ba || s != bs) { bad++; printf("   MISMATCH f=%u\n", f); }
    }
    printf("   %d frequencies, %d mismatches\n", n, bad);

    printf("\n4) 클럭 오차 → 하루/한 달 드리프트 (프리스케일러는 명목값에 맞춰 둠)\n");
    printf("   %-34s %12s %12s %14s\n", "case", "error ppm", "s/day", "min/30days");
    static const struct { const char *name; double nominal, actual; } d[] = {
        { "LSE +20 ppm crystal", 32768.0, 32768.0 * (1 + 20e-6) },
        { "LSE -100 ppm (cold, 2nd-order)", 32768.0, 32768.0 * (1 - 100e-6) },
        { "LSI set 32000, actual 17000", 32000.0, 17000.0 },
        { "LSI set 32000, actual 47000", 32000.0, 47000.0 },
        { "LSI set 32000, actual 32500", 32000.0, 32500.0 },
    };
    for (size_t i = 0; i < sizeof d / sizeof d[0]; i++) {
        double e = d[i].actual / d[i].nominal - 1.0;
        printf("   %-34s %12.1f %12.2f %14.2f\n", d[i].name, e * 1e6, e * 86400.0, e * 86400.0 * 30 / 60.0);
    }
    return bad ? 1 : 0;
}
