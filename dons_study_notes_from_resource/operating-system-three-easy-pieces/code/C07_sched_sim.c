/* C07_sched_sim.c — FIFO / SJF / STCF / RR 를 1틱 단위로 시뮬레이션하고
 * Gantt 차트 + 반환시간(turnaround) + 응답시간(response)을 출력한다.
 * 도착 시간(arrival)을 지원하므로 원문 Figure 7.4/7.5 (늦게 온 B, C)도 재현된다. */
#include <stdio.h>
#include <string.h>

#define MAXJ 8
#define MAXT 512

typedef struct {
    char name;
    int arrival, length;   /* 입력 */
    int left, first_run, done;  /* 상태 */
} job_t;

enum policy { FIFO, SJF, STCF, RR };
static const char *pname[] = { "FIFO", "SJF", "STCF", "RR" };

static void simulate(enum policy p, int quantum, const job_t *in, int n) {
    job_t j[MAXJ];
    char gantt[MAXT + 1];
    int rrq[MAXJ * MAXT], qh = 0, qt = 0;     /* RR 용 FIFO 큐 (넉넉하게) */
    int cur = -1, slice = 0, finished = 0, t = 0;

    memcpy(j, in, sizeof(job_t) * (size_t)n);
    for (int i = 0; i < n; i++) { j[i].left = j[i].length; j[i].first_run = -1; j[i].done = -1; }

    while (finished < n && t < MAXT) {
        /* 1) 이번 틱에 도착한 job 을 RR 큐 뒤에 넣는다 */
        for (int i = 0; i < n; i++)
            if (j[i].arrival == t) rrq[qt++] = i;

        /* 2) 다음에 돌릴 job 고르기 */
        int pick = -1;
        if (p == FIFO || p == SJF) {            /* 비선점: 돌던 게 있으면 계속 */
            if (cur >= 0 && j[cur].left > 0) pick = cur;
            else for (int i = 0; i < n; i++) {
                if (j[i].arrival > t || j[i].left == 0) continue;
                if (pick < 0) { pick = i; continue; }
                if (p == FIFO && j[i].arrival < j[pick].arrival) pick = i;
                if (p == SJF  && j[i].length  < j[pick].length)  pick = i;
            }
        } else if (p == STCF) {                 /* 선점: 매 틱 남은 시간 최소 */
            for (int i = 0; i < n; i++) {
                if (j[i].arrival > t || j[i].left == 0) continue;
                if (pick < 0 || j[i].left < j[pick].left) pick = i;
            }
        } else {                                /* RR: 퀀텀 다 쓰면 큐 뒤로 */
            if (cur >= 0 && j[cur].left > 0 && slice < quantum) pick = cur;
            else {
                if (cur >= 0 && j[cur].left > 0) rrq[qt++] = cur;
                pick = (qh < qt) ? rrq[qh++] : -1;
                slice = 0;
            }
        }

        /* 3) 한 틱 실행 */
        if (pick < 0) { gantt[t++] = '.'; cur = -1; continue; }
        if (j[pick].first_run < 0) j[pick].first_run = t;
        gantt[t] = j[pick].name;
        j[pick].left--; slice++; t++;
        if (j[pick].left == 0) { j[pick].done = t; finished++; }
        cur = pick;
    }
    gantt[t] = '\0';

    double sumT = 0, sumR = 0;
    printf("[%s%s] ", pname[p], p == RR ? (quantum == 1 ? " q=1" : " q=2") : "");
    printf("Gantt: %s\n", gantt);
    for (int i = 0; i < n; i++) {
        int tat = j[i].done - j[i].arrival, rsp = j[i].first_run - j[i].arrival;
        sumT += tat; sumR += rsp;
        printf("   %c arr=%2d len=%2d  first=%2d done=%2d  T_turnaround=%3d T_response=%3d\n",
               j[i].name, j[i].arrival, j[i].length, j[i].first_run, j[i].done, tat, rsp);
    }
    printf("   avg turnaround=%.2f  avg response=%.2f\n", sumT / n, sumR / n);
}

int main(void) {
    /* 원문 Fig 7.4/7.5 를 1/10 축소: A=10틱(t=0 도착), B,C=1틱(t=1 도착) */
    job_t late[] = { {'A', 0, 10, 0,0,0}, {'B', 1, 1, 0,0,0}, {'C', 1, 1, 0,0,0} };
    puts("== Workload 1: A(0,10) B(1,1) C(1,1)  — 원문 Fig 7.4/7.5 의 1/10 축소판 ==");
    simulate(FIFO, 0, late, 3);
    simulate(SJF,  0, late, 3);
    simulate(STCF, 0, late, 3);
    simulate(RR,   1, late, 3);

    /* 새 예제: 도착이 제각각인 4개 job */
    job_t mix[] = { {'A', 0, 8, 0,0,0}, {'B', 1, 4, 0,0,0}, {'C', 2, 9, 0,0,0}, {'D', 3, 5, 0,0,0} };
    puts("\n== Workload 2: A(0,8) B(1,4) C(2,9) D(3,5) — 새 예제 ==");
    simulate(FIFO, 0, mix, 4);
    simulate(SJF,  0, mix, 4);
    simulate(STCF, 0, mix, 4);
    simulate(RR,   2, mix, 4);
    return 0;
}
