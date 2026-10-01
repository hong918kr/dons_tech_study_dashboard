/* C08_mlfq.c — 규칙 1~5 를 그대로 구현한 작은 MLFQ 시뮬레이터.
 *  - 큐 3개 (Q2=최상위). 레벨별 퀀텀 10/20/40 틱, allotment = 퀀텀 1개분.
 *  - old 모드: Rule 4a/4b (I/O 하면 사용량 리셋 → 같은 레벨 유지)
 *  - new 모드: Rule 4 (I/O 와 상관없이 레벨별 사용량 누적 → 다 쓰면 강등)
 *  - boost>0 이면 Rule 5 (S 틱마다 모든 job 을 최상위로)
 * job 3개: A=CPU만 쓰는 긴 job, G=9틱 돌고 1틱 I/O 하는 "게이머", I=1틱 돌고 3틱 I/O 하는 대화형 job */
#include <stdio.h>

#define NQ 3
#define NJ 3
#define HORIZON 400

enum { READY, BLOCKED, DONE, NOTYET };
typedef struct {
    char name; int arrival, left, burst, io_time;   /* burst=0 이면 I/O 없음 */
    int state, prio, used, run_in_burst, io_left;
    unsigned long seq;                               /* 같은 레벨 안 RR 순서 */
    int ran, finish;
} job_t;

static const int quantum[NQ] = { 40, 20, 10 };       /* index = priority (2 가 최상위) */

static void run(const char *title, int old_rules, int boost) {
    job_t j[NJ] = {
        { 'A',  0, 300, 0, 0, NOTYET,0,0,0,0,0,0,-1 },
        { 'G',  0, 300, 9, 1, NOTYET,0,0,0,0,0,0,-1 },
        { 'I', 50,  30, 1, 3, NOTYET,0,0,0,0,0,0,-1 },
    };
    unsigned long seq = 0;
    int cur = -1, slice = 0;
    char line[HORIZON + 1];

    for (int t = 0; t < HORIZON; t++) {
        /* 도착 (Rule 3: 최상위 큐로) + I/O 완료 처리 */
        for (int i = 0; i < NJ; i++) {
            if (j[i].state == NOTYET && j[i].arrival == t) {
                j[i].state = READY; j[i].prio = NQ - 1; j[i].used = 0; j[i].seq = ++seq;
            } else if (j[i].state == BLOCKED && j[i].io_left-- == 0) {   /* io_time 틱 동안 BLOCKED */
                j[i].state = READY; j[i].seq = ++seq;
            }
        }
        /* Rule 5: priority boost */
        if (boost > 0 && t > 0 && t % boost == 0)
            for (int i = 0; i < NJ; i++)
                if (j[i].state != DONE) { j[i].prio = NQ - 1; j[i].used = 0; }

        /* Rule 1/2: 가장 높은 레벨, 그 안에서는 먼저 줄 선 job (RR) */
        int best = -1;
        for (int i = 0; i < NJ; i++) {
            if (j[i].state != READY) continue;
            if (best < 0 || j[i].prio > j[best].prio ||
                (j[i].prio == j[best].prio && j[i].seq < j[best].seq)) best = i;
        }
        /* 현재 job 이 퀀텀이 남았고, 더 높은 레벨이 없으면 계속 돈다 */
        if (cur >= 0 && j[cur].state == READY && slice < quantum[j[cur].prio] &&
            (best < 0 || j[best].prio <= j[cur].prio)) best = cur;
        if (best != cur) slice = 0;
        cur = best;
        if (cur < 0) { line[t] = '.'; continue; }

        /* 1틱 실행 */
        job_t *p = &j[cur];
        line[t] = p->name;
        p->left--; p->ran++; p->used++; p->run_in_burst++; slice++;

        if (p->left == 0) { p->state = DONE; p->finish = t + 1; cur = -1; continue; }

        /* Rule 4 (new): 레벨에서 쓴 총량이 allotment 를 넘으면 강등 */
        if (p->used >= quantum[p->prio]) {
            if (p->prio > 0) p->prio--;
            p->used = 0; slice = 0; p->seq = ++seq; cur = -1;   /* 큐 맨 뒤로 */
        } else if (slice >= quantum[p->prio]) {
            slice = 0; p->seq = ++seq; cur = -1;                /* 같은 레벨 맨 뒤로 */
        }
        /* I/O 발행: 4b(old)면 사용량을 잊어 준다 → 게이밍 가능 */
        if (p->burst > 0 && p->run_in_burst == p->burst) {
            p->run_in_burst = 0; p->state = BLOCKED; p->io_left = p->io_time;
            if (old_rules) p->used = 0;
            if (cur == (int)(p - j)) cur = -1;
            slice = 0;
        }
    }
    line[HORIZON] = '\0';

    printf("== %s ==\n", title);
    printf("  t=0..79   : %.80s\n", line);
    printf("  t=200..279: %.80s\n", line + 200);
    for (int i = 0; i < NJ; i++)
        if (j[i].state == DONE)
            printf("  %c: CPU %3d/%d ticks (%4.1f%%)  finish=%d\n", j[i].name, j[i].ran, HORIZON,
                   100.0 * j[i].ran / HORIZON, j[i].finish);
        else
            printf("  %c: CPU %3d/%d ticks (%4.1f%%)  (unfinished, %d left)\n", j[i].name, j[i].ran,
                   HORIZON, 100.0 * j[i].ran / HORIZON, j[i].left);
}

int main(void) {
    run("old Rule 4a/4b, no boost  (게이밍 가능)", 1, 0);
    run("new Rule 4 (accounting), no boost", 0, 0);
    run("new Rule 4 + Rule 5 boost every 100", 0, 100);
    return 0;
}
