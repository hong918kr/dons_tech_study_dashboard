/* COMM-A03 demo — 짧은 링크 끊김(flap)을 폴링으로 얼마나 잡나: BMSR 읽기 전략 3가지
 *   S1 "늘 한 번 읽기" : BMSR 를 한 번 읽어 래치값을 그대로 보고
 *   S2 "두 번, 둘째만" : 두 번 읽고 둘째(현재값)만 본다 → 폴링 순간 끊겨 있을 때만 보인다
 *   L  "Linux 식"      : up 이라 믿을 땐 한 번(래치), down 이라 믿을 땐 두 번 (genphy_update_link 의 주석대로)
 *   S3 "두 번, 둘 다"  : 첫 값(래치)으로 끊김을 세고 둘째로 현재 상태를 보고 (solution.c 의 phy_read_link)
 * 모델: BMSR.2 latch-low 의미만 축약 (MDIO 비트 계층·프레임 시간 생략, 폴링 순간 두 읽기는 같은 시각).
 * 각 칸: 폴링 주기 P, 끊김 길이 d, 끊김 시작 위치를 폴링 격자에 대해 균일 랜덤으로 4000번.
 *   detect  = 끊김이 어떤 폴링에서든 "down" 또는 "dropped" 로 보고된 비율
 *   stale   = 링크가 실제로는 up 인 순간에 "down" 이라고 보고한 폴링이 있었던 비율
 * 이론: S2 의 detect = min(1, d/P), S1·L·S3 = 1.
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_flap_poll.c -o demo_flap_poll && ./demo_flap_poll
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t g_rng = 0xF1A9F1A9u;
static uint32_t rnd(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

typedef struct { uint64_t s, e; } Win;                     /* 끊김 [s, e), 단위 us */
static bool down_at(Win w, uint64_t t) { return t >= w.s && t < w.e; }
static bool down_between(Win w, uint64_t a, uint64_t b) { return w.s <= b && w.e > a; }   /* (a, b] 와 교차 */

int main(void) {
    const uint64_t P_ms[] = { 10, 100, 1000 };
    const uint64_t d_ms[] = { 1, 5, 20, 100, 300, 1000, 2000 };
    const int TRIALS = 4000;
    printf("%6s %7s | %7s %7s %7s %7s | %8s %8s %8s | %9s\n", "P(ms)", "d(ms)", "S1 det", "S2 det", "L det", "S3 det",
           "S1 stale", "L stale", "S3 stale", "S2 theory");
    for (size_t pi = 0; pi < sizeof P_ms / sizeof P_ms[0]; pi++) {
        uint64_t P = P_ms[pi] * 1000;
        for (size_t di = 0; di < sizeof d_ms / sizeof d_ms[0]; di++) {
            uint64_t d = d_ms[di] * 1000;
            int det1 = 0, det2 = 0, detl = 0, det3 = 0, st1 = 0, stl = 0, st3 = 0;
            for (int t = 0; t < TRIALS; t++) {
                Win w; w.s = 10 * P + (uint64_t)rnd() % P; w.e = w.s + d;   /* 폴링 격자에 대한 위상 균일 */
                bool f1 = false, f2 = false, fl = false, f3 = false, s1 = false, sl = false, s3 = false;
                bool lin_up = true;                                         /* Linux 식이 믿는 상태 */
                bool latch = true, believed_up = true;                      /* 직전 폴링에서 up, 래치 1 */
                uint64_t prev = 9 * P;
                for (uint64_t T = 10 * P; T <= w.e + 3 * P; T += P) {
                    bool now_up = !down_at(w, T);
                    if (down_between(w, prev, T)) latch = false;
                    bool first = latch;                                    /* 첫 읽기 = 래치 */
                    latch = now_up;                                        /* 읽으면 현재값으로 재장전 */
                    bool second = latch;                                   /* 같은 순간 둘째 읽기 */
                    latch = now_up;
                    if (!first) f1 = true;                                 /* S1 은 첫 값만 보고 */
                    if (!first && now_up) s1 = true;                       /* 실제는 up 인데 down 보고 */
                    if (!second) f2 = true;
                    bool lrep = lin_up ? first : second;                   /* L: up 이면 한 번, down 이면 두 번 */
                    if (!lrep) fl = true;
                    if (!lrep && now_up) sl = true;
                    lin_up = lrep;
                    if (believed_up && !first) f3 = true;                  /* S3: dropped */
                    if (!second && now_up) s3 = true;                      /* S3 의 현재 상태 보고는 늘 정확 */
                    believed_up = second;
                    prev = T;
                }
                det1 += f1; det2 += f2; detl += fl; det3 += f3; st1 += s1; stl += sl; st3 += s3;
            }
            double th = (double)d / (double)P; if (th > 1) th = 1;
            printf("%6llu %7llu | %6.1f%% %6.1f%% %6.1f%% %6.1f%% | %7.1f%% %7.1f%% %7.1f%% | %8.1f%%\n",
                   (unsigned long long)P_ms[pi], (unsigned long long)d_ms[di], 100.0 * det1 / TRIALS,
                   100.0 * det2 / TRIALS, 100.0 * detl / TRIALS, 100.0 * det3 / TRIALS, 100.0 * st1 / TRIALS,
                   100.0 * stl / TRIALS, 100.0 * st3 / TRIALS, 100 * th);
        }
    }
    return 0;
}
