/* ALGO-A11 demo_drift — 10^9 틱 동안 매 틱 호출 + 쓸 수 있는 토큰 전부 소진 (포화).
 * 버스트 b = 10, 속도 r = num/den 토큰/틱. 정답 = b + floor(T·num/den), T = 10^9 − 1 (마지막 틱 번호).
 *   exact   : 이 레슨 (토큰 × den 스케일, 나머지 보관)
 *   Q16.16  : 고정소수점 토큰, 속도를 round(65536·num/den) 로 반올림
 *   float / double : 실수 토큰 += num/den
 *   int-div : 정수 토큰, add = dt·num/den, last = now  (나머지 버림) — 호출 주기 P 에 따라 속도가 달라진다
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_drift.c -o demo_drift && ./demo_drift
 */
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define T_END 1000000000u
#define B 10u

static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }

/* 모두 t=0 에 가득 찬 b 개를 소진한 상태에서 시작 → 반환값에 b 를 더한다 */
static uint64_t run_exact(uint32_t num, uint32_t den) {          /* level = tokens × den */
    uint32_t level = 0, cap = B * den; uint64_t acc = 0;
    for (uint32_t t = 1; t < T_END; t++) {
        level += num; if (level > cap) level = cap;
        uint32_t k = level / den; level -= k * den; acc += k;
    }
    return acc + B;
}
static uint64_t run_q16(uint32_t num, uint32_t den) {
    uint32_t level = 0, cap = B << 16, rate = (uint32_t)(((65536ull * num) + den / 2) / den); uint64_t acc = 0;
    for (uint32_t t = 1; t < T_END; t++) {
        level += rate; if (level > cap) level = cap;
        uint32_t k = level >> 16; level -= k << 16; acc += k;
    }
    return acc + B;
}
static uint64_t run_float(uint32_t num, uint32_t den) {
    float tok = 0; uint64_t acc = 0; const float r = (float)num / (float)den;
    for (uint32_t t = 1; t < T_END; t++) {
        tok += r; if (tok > (float)B) tok = (float)B;
        uint32_t k = (uint32_t)tok; tok -= (float)k; acc += k;
    }
    return acc + B;
}
static uint64_t run_double(uint32_t num, uint32_t den) {
    double tok = 0; uint64_t acc = 0; const double r = (double)num / (double)den;
    for (uint32_t t = 1; t < T_END; t++) {
        tok += r; if (tok > (double)B) tok = (double)B;
        uint32_t k = (uint32_t)tok; tok -= (double)k; acc += k;
    }
    return acc + B;
}
static uint64_t run_intdiv(uint32_t num, uint32_t den, uint32_t period) {
    uint32_t tokens = 0, last = 0; uint64_t acc = B;
    for (uint32_t t = period; t < T_END; t += period) {
        uint32_t add = (t - last) * num / den; last = t;         /* 나머지를 버리고 last 를 now 로 */
        tokens += add; if (tokens > B) tokens = B;
        acc += tokens; tokens = 0;
    }
    return acc;
}

int main(void) {
    static const uint32_t R[][2] = { {1, 3}, {1, 10}, {7, 10}, {5, 9}, {2, 3} };
    printf("burst %u, ticks 0..%u, called every tick, saturating drain; diff = accepted - (b + floor(T*num/den))\n", B, T_END - 1);
    printf("  %-6s %12s %8s %8s %8s %8s\n", "rate", "expected", "exact", "Q16.16", "float", "double");
    double tsum[4] = {0};
    for (int i = 0; i < 5; i++) {
        uint32_t num = R[i][0], den = R[i][1];
        const uint64_t want = B + (uint64_t)(T_END - 1) * num / den;
        uint64_t (*fn[4])(uint32_t, uint32_t) = { run_exact, run_q16, run_float, run_double };
        long long d[4];
        for (int k = 0; k < 4; k++) {
            double t0 = now_s(); uint64_t got = fn[k](num, den); tsum[k] += now_s() - t0;
            d[k] = (long long)got - (long long)want;
        }
        char rs[16]; snprintf(rs, sizeof rs, "%u/%u", num, den);
        printf("  %-6s %12llu %+8lld %+8lld %+8lld %+8lld\n", rs, (unsigned long long)want, d[0], d[1], d[2], d[3]);
    }
    printf("  total seconds (5 rates): exact %.1f  Q16 %.1f  float %.1f  double %.1f\n", tsum[0], tsum[1], tsum[2], tsum[3]);
    static const uint32_t P[] = { 1, 2, 7, 25 };
    const uint64_t want3 = B + (uint64_t)(T_END - 1) / 3;
    for (int i = 0; i < 4; i++) {
        uint64_t got = run_intdiv(1, 3, P[i]);
        printf("  int-div r=1/3 called every %2u ticks: accepted=%llu (%.4f of expected)\n", P[i], (unsigned long long)got, (double)got / want3);
    }
    return 0;
}
