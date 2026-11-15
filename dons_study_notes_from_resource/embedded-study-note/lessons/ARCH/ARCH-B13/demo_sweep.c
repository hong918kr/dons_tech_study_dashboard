/* ARCH-B13 demo — (1) skew 스윕 표, (2) STA 리포트 모양으로 경로별 arrival / required / slack 출력
 * cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o demo_sweep && ./demo_sweep
 * 체크 테스트가 아니라 "눈으로 보는" 출력용. 식은 solution.c 와 같다 (자기 완결을 위해 다시 적음).
 */
#include <stdint.h>
#include <stdio.h>

typedef struct { int32_t tpcq, tccq, tpd, tcd, tsu, th, skew; } ff_path;
typedef struct { int32_t tpcq, tccq, tsu, th; } ff_lib;
typedef struct { const char *name; int32_t clk_arrival_ps; } reg_t;
typedef struct { const char *name; int launch, capture; int32_t tpd, tcd; } net_path;

static int32_t setup_slack_ps(const ff_path *p, int32_t tclk) { return tclk + p->skew - (p->tpcq + p->tpd + p->tsu); }
static int32_t hold_slack_ps(const ff_path *p)                { return p->tccq + p->tcd - p->th - p->skew; }
static int32_t min_period_ps(const ff_path *p)                { return p->tpcq + p->tpd + p->tsu - p->skew; }

int main(void) {
    const ff_path P = { 100, 60, 1500, 200, 80, 50, 0 };
    const int32_t TCLK = 2000;
    printf("(1) skew sweep, Tclk = %d ps (500 MHz), path P: tpcq 100 tccq 60 tpd 1500 tcd 200 tsu 80 th 50\n", TCLK);
    printf("  skew | setup slack | hold slack |  Tmin |  Fmax MHz | verdict\n");
    for (int32_t s = -400; s <= 300; s += 50) {
        ff_path q = P; q.skew = s;
        int32_t su = setup_slack_ps(&q, TCLK), ho = hold_slack_ps(&q), t = min_period_ps(&q);
        printf("  %4d | %11d | %10d | %5d | %9.3f | %s\n", s, su, ho, t, (1000000000 / t) / 1000.0, /* kHz 내림 */
               su < 0 ? "SETUP VIOLATED" : ho < 0 ? "HOLD VIOLATED" : "met");
    }

    const ff_lib lib = { 90, 40, 60, 30 };
    const reg_t regs[] = {
        { "pc", 400 }, { "ir", 420 }, { "opa", 450 }, { "acc", 600 },
        { "out", 430 }, { "sh0", 500 }, { "sh1", 580 },
    };
    const net_path paths[] = {
        { "pc->ir   imem decode",  0, 1, 1100, 200 },
        { "ir->opa  regfile read", 1, 2, 1300, 150 },
        { "opa->acc 32b adder",    2, 3, 1900, 160 },
        { "acc->opa forward mux",  3, 2, 1500, 120 },
        { "acc->out output reg",   3, 4,  300,  60 },
        { "sh0->sh1 shift wire",   5, 6,   20,  10 },
    };
    printf("\n(2) path report @ Tclk = %d ps  (lib: tpcq 90 tccq 40 tsu 60 th 30; times are absolute ps from clock source edge)\n", TCLK);
    printf("  %-22s | launch clk | arrival(max) | required(setup) | setup slack | arrival(min) | required(hold) | hold slack\n", "path");
    int32_t worst = INT32_MAX, tmin_max = 0; int wi = -1;
    for (int i = 0; i < (int)(sizeof paths / sizeof paths[0]); i++) {
        const net_path *n = &paths[i];
        int32_t la = regs[n->launch].clk_arrival_ps, ca = regs[n->capture].clk_arrival_ps;
        int32_t arr_max = la + lib.tpcq + n->tpd;         /* 가장 늦은 데이터 도착 */
        int32_t req_su  = TCLK + ca - lib.tsu;            /* 다음 capture 에지 − setup */
        int32_t arr_min = la + lib.tccq + n->tcd;         /* 가장 이른 새 데이터 도착 */
        int32_t req_h   = ca + lib.th;                    /* 같은 capture 에지 + hold */
        ff_path p = { lib.tpcq, lib.tccq, n->tpd, n->tcd, lib.tsu, lib.th, ca - la };
        printf("  %-22s | %10d | %12d | %15d | %6d %-4s | %12d | %14d | %5d %s\n", n->name, la, arr_max, req_su,
               req_su - arr_max, req_su - arr_max < 0 ? "VIOL" : "met", arr_min, req_h, arr_min - req_h,
               arr_min - req_h < 0 ? "VIOL" : "met");
        if (req_su - arr_max != setup_slack_ps(&p, TCLK) || arr_min - req_h != hold_slack_ps(&p)) printf("  MISMATCH!\n");
        if (req_su - arr_max < worst) { worst = req_su - arr_max; wi = i; }
        if (min_period_ps(&p) > tmin_max) tmin_max = min_period_ps(&p);
    }
    printf("  critical (setup) path: %s, WNS %d ps, design Tmin %d ps -> Fmax %.3f MHz (floor)\n",
           paths[wi].name, worst, tmin_max, (1000000000 / tmin_max) / 1000.0);
    return 0;
}
