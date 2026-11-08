/* BM-M11 demo_duty_table.c — solution.c 의 전력 모델(같은 수치)로 표를 찍는다.
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_duty_table.c -o demo_duty_table && ./demo_duty_table
 * 표 1: 주기 T = 10 s, duty cycle(활성 비율) 별 평균 전류와 CR2032(225 mAh) 수명
 * 표 2: 활성 5 ms 고정, 깨는 주기 별 평균 전류 (Stop vs Standby 손익분기)
 */
#include <stdint.h>
#include <stdio.h>

typedef struct { const char *name; uint64_t i_nA, over_us; } m_t;   /* over = wake_us + boot_us */
static const m_t M[4] = { { "Run", 1500000u, 0u }, { "Sleep", 400000u, 1u },
                          { "Stop", 1800u, 5u }, { "Standby", 400u, 2050u } };
static const uint64_t IRUN = 1500000u;

static uint64_t avg_nA(uint64_t T, uint64_t ta, int m) {
    uint64_t q;
    if (m == 0 || ta + M[m].over_us >= T) q = IRUN * T;
    else q = IRUN * (ta + M[m].over_us) + M[m].i_nA * (T - ta - M[m].over_us);
    return (q + T / 2u) / T;
}
static void fmt_i(char *b, uint64_t nA) {            /* 사람이 읽기 좋게 */
    if (nA >= 1000000u) sprintf(b, "%.3f mA", (double)nA / 1e6);
    else if (nA >= 1000u) sprintf(b, "%.2f uA", (double)nA / 1e3);
    else sprintf(b, "%llu nA", (unsigned long long)nA);
}
static void fmt_life(char *b, uint64_t nA) {
    double h = 225e6 / (double)nA;
    if (h < 48) sprintf(b, "%.0f h", h);
    else if (h < 24 * 730) sprintf(b, "%.0f d", h / 24);
    else sprintf(b, "%.1f y", h / 24 / 365);
}

int main(void) {
    const uint64_t T = 10000000u;
    const uint64_t ta[] = { 10000000u, 1000000u, 100000u, 10000u, 1000u, 100u };
    const char *dn[] = { "100%", "10%", "1%", "0.1%", "0.01%", "0.001%" };
    printf("표 1: T = 10 s, CR2032 225 mAh (이상적 용량)\n");
    printf("%-7s %-9s | %-22s | %-22s | %-22s | %-22s\n", "duty", "t_act", "Run", "Sleep", "Stop", "Standby");
    for (int i = 0; i < 6; i++) {
        printf("%-7s %-9llu |", dn[i], (unsigned long long)ta[i]);
        for (int m = 0; m < 4; m++) {
            char a[32], l[32];
            uint64_t v = avg_nA(T, ta[i], m);
            fmt_i(a, v); fmt_life(l, v);
            printf(" %-11s %-9s |", a, l);
        }
        printf("\n");
    }
    printf("\nCSV duty_pct,run,sleep,stop,standby (nA)\n");
    for (int i = 0; i < 6; i++)
        printf("%s,%llu,%llu,%llu,%llu\n", dn[i], (unsigned long long)avg_nA(T, ta[i], 0),
               (unsigned long long)avg_nA(T, ta[i], 1), (unsigned long long)avg_nA(T, ta[i], 2),
               (unsigned long long)avg_nA(T, ta[i], 3));

    printf("\n표 2: 활성 5 ms 고정, 깨는 주기 T 별 평균 전류 [nA]\n");
    printf("%-8s %10s %10s %10s  더 나은 쪽\n", "T", "Sleep", "Stop", "Standby");
    const uint64_t Ts[] = { 10000u, 100000u, 1000000u, 2000000u, 2500000u, 10000000u, 60000000u, 3600000000u };
    const char *tn[] = { "10 ms", "100 ms", "1 s", "2 s", "2.5 s", "10 s", "1 min", "1 h" };
    for (int i = 0; i < 8; i++) {
        uint64_t a = avg_nA(Ts[i], 5000u, 1), b = avg_nA(Ts[i], 5000u, 2), c = avg_nA(Ts[i], 5000u, 3);
        printf("%-8s %10llu %10llu %10llu  %s\n", tn[i], (unsigned long long)a, (unsigned long long)b,
               (unsigned long long)c, c < b ? "Standby" : "Stop");
    }
    return 0;
}
