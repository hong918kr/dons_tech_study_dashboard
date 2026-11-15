/* OS-B12 추가 연습 — 한 번에 검증
 *
 *   연습 1  RR + 컨텍스트 스위치 비용: quantum 이 작으면 CPU 가 스위치에 먹힌다
 *   연습 2  HRRN (Highest Response Ratio Next): SJF 의 기아를 "에이징" 으로 막기
 *   연습 3  stride 스케줄링 (OSTEP 9장): 티켓 비율대로 CPU 를 결정적으로 나누기
 *   연습 4  다음 버스트 예측 (지수 평균): 오라클 없는 SJF 의 재료
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------------- 연습 1: RR + 스위치 비용 ----------------
 * 모든 작업이 0 에 도착, I/O 없음. 다른 작업으로 바꿀 때마다 (첫 디스패치 포함) cs tick 을 쓴다.
 * 같은 작업이 연달아 slice 를 받으면 (혼자 남았을 때) 스위치 비용이 없다. */
typedef struct { int makespan, useful, switches; double avg_tat; } rrcs_t;

static rrcs_t rr_cs(const int *len, int n, int q, int cs) {
    int rem[16], queue[16], cnt = n, fin[16];
    rrcs_t r = {0, 0, 0, 0.0};
    for (int j = 0; j < n; j++) { rem[j] = len[j]; queue[j] = j; }
    int t = 0, last = -1;
    while (cnt > 0) {
        int j = queue[0];
        if (j != last) { t += cs; r.switches++; }                 /* 다른 작업으로 바꿀 때만 비용 */
        int run = rem[j] < q ? rem[j] : q;
        t += run; rem[j] -= run; r.useful += run; last = j;
        memmove(queue, queue + 1, (size_t)(cnt - 1) * sizeof(int)); /* 맨 앞 제거 */
        if (rem[j] == 0) { fin[j] = t; cnt--; }
        else queue[cnt - 1] = j;                                    /* 맨 뒤로 */
    }
    long sum = 0;
    for (int j = 0; j < n; j++) sum += fin[j];
    r.makespan = t; r.avg_tat = (double)sum / n;
    return r;
}

/* ---------------- 연습 2: HRRN vs SJF (비선점, I/O 없음) ----------------
 * 응답 비율 = (대기 + 버스트) / 버스트. 정수 비교: (w1+b1)*b2 > (w2+b2)*b1. 동률은 먼저 온 작업.
 * hrrn=false 면 SJF (버스트 최소). 반환: start[j] 를 채운다. */
typedef struct { int arrival, len; } pj_t;

static void np_sched(const pj_t *jb, int n, bool hrrn, int *start) {
    bool done[32] = {false};
    int t = 0;
    for (int k = 0; k < n; k++) {
        int best = -1;
        for (;;) {
            for (int j = 0; j < n; j++) {
                if (done[j] || jb[j].arrival > t) continue;
                if (best < 0) { best = j; continue; }
                long w1 = t - jb[j].arrival, b1 = jb[j].len, w2 = t - jb[best].arrival, b2 = jb[best].len;
                bool better = hrrn ? (w1 + b1) * b2 > (w2 + b2) * b1 : b1 < b2;
                if (better) best = j;
            }
            if (best >= 0) break;
            t++;                                              /* 아무도 없으면 시간만 간다 */
        }
        start[best] = t; t += jb[best].len; done[best] = true;
    }
}

/* ---------------- 연습 3: stride 스케줄링 ----------------
 * stride = BIG / tickets. 매번 pass 가 가장 작은 작업 (동률은 낮은 id) 을 1 quantum 돌리고 pass += stride. */
#define BIG 10000
static void stride_run(const int *tickets, int n, int picks, int *count, char *trace, int trace_len) {
    long pass[8] = {0}, stride[8];
    for (int j = 0; j < n; j++) { stride[j] = BIG / tickets[j]; count[j] = 0; }
    for (int k = 0; k < picks; k++) {
        int best = 0;
        for (int j = 1; j < n; j++) if (pass[j] < pass[best]) best = j;
        pass[best] += stride[best]; count[best]++;
        if (k < trace_len - 1) { trace[k] = (char)('A' + best); trace[k + 1] = '\0'; }
    }
}

/* ---------------- 연습 4: 지수 평균 버스트 예측 ----------------
 * tau_{n+1} = alpha * t_n + (1 - alpha) * tau_n,  alpha = a_num / a_den (정수 연산, 내림) */
static int predict_next(int tau, int burst, int a_num, int a_den) {
    return (a_num * burst + (a_den - a_num) * tau) / a_den;
}

int main(void) {
    /* 연습 1 */
    int l3[] = {5, 5, 5};
    rrcs_t r = rr_cs(l3, 3, 1, 1);
    CHECK(r.makespan == 30 && r.useful == 15 && r.switches == 15);       /* 효율 50% */
    r = rr_cs(l3, 3, 5, 1);
    CHECK(r.makespan == 18 && r.switches == 3);                          /* q 가 길면 효율 15/18 */
    int l2[] = {5, 1};
    r = rr_cs(l2, 2, 1, 1);
    CHECK(r.makespan == 9 && r.switches == 3);                           /* 혼자 남은 A 는 스위치 없이 연속 */
    {
        int big[] = {100, 100, 100, 100};
        double prev_eff = 0; bool mono = true;
        for (int q = 1; q <= 64; q *= 2) {
            rrcs_t x = rr_cs(big, 4, q, 1);
            double eff = (double)x.useful / x.makespan;
            mono = mono && eff > prev_eff; prev_eff = eff;
            if (q == 1) CHECK(x.makespan == 800);                        /* 매 tick 스위치: 효율 1/2 */
        }
        CHECK(mono);
    }

    /* 연습 2: 긴 작업 L 이 1 에 오고, 짧은 작업(길이 2)이 2 tick 마다 계속 온다 */
    pj_t jb[12];
    jb[0] = (pj_t){0, 3}; jb[1] = (pj_t){1, 10};
    for (int i = 2; i < 12; i++) jb[i] = (pj_t){2 * (i - 1), 2};
    int st_sjf[12], st_h[12];
    np_sched(jb, 12, false, st_sjf);
    np_sched(jb, 12, true, st_h);
    CHECK(st_sjf[1] == 23);              /* SJF: 짧은 작업 10개가 다 끝나야 L 이 돈다 */
    CHECK(st_h[1] == 7);                 /* HRRN: L 의 비율이 커져 짧은 작업 2개 뒤에 돈다 */
    {
        long wsum_s = 0, wsum_h = 0;
        for (int j = 0; j < 12; j++) { wsum_s += st_sjf[j] - jb[j].arrival; wsum_h += st_h[j] - jb[j].arrival; }
        CHECK(wsum_s < wsum_h);          /* 대가: 평균 대기는 SJF 가 더 작다 */
    }

    /* 연습 3: OSTEP 9.6 의 예 — 티켓 100/50/250 → stride 100/200/40 */
    int tk[] = {100, 50, 250}, cnt[3];
    char tr[16];
    stride_run(tk, 3, 80, cnt, tr, (int)sizeof tr);
    CHECK(cnt[0] == 20 && cnt[1] == 10 && cnt[2] == 50);                /* 정확히 2 : 1 : 5 */
    CHECK(strncmp(tr, "ABCCCACC", 8) == 0);                              /* OSTEP 그림 9.3 의 순서 그대로 */
    stride_run(tk, 3, 7, cnt, tr, (int)sizeof tr);
    CHECK(cnt[0] + cnt[1] + cnt[2] == 7 && cnt[2] >= 3);

    /* 연습 4: alpha = 1/2, tau0 = 10, 실제 버스트 6 4 6 4 13 13 13 */
    int bursts[] = {6, 4, 6, 4, 13, 13, 13}, expect[] = {8, 6, 6, 5, 9, 11, 12};
    int tau = 10; bool ok = true;
    for (int i = 0; i < 7; i++) { tau = predict_next(tau, bursts[i], 1, 2); ok = ok && tau == expect[i]; }
    CHECK(ok);
    CHECK(predict_next(10, 4, 0, 1) == 10 && predict_next(10, 4, 1, 1) == 4);   /* alpha 0: 과거만, 1: 직전만 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
