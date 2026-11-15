/* OS-B12 CPU 스케줄링 시뮬레이터: FCFS/SJF/RR/MLFQ
 *
 *   Part A  FCFS · SJF · STCF · RR 선택 규칙 + 평균 반환/응답/대기 시간
 *   Part B  MLFQ (OSTEP 8장 규칙 1~5): 우선순위, 같은 레벨 RR, 새 작업은 맨 위,
 *           할당량(allotment) 다 쓰면 강등, 주기적 부스트
 *   Part C  MLFQ 공격과 방어: 기아(starvation) · 게이밍(gaming)을 숫자로 — CPU 몫, 최장 공백
 *
 * 시간은 정수 tick. 한 tick 의 순서 (OSTEP mlfq.py 와 같은 순서):
 *   ① (MLFQ) 부스트 시각이면 부스트  ② 이번 tick 도착 → 큐 뒤 (id 순)
 *   ③ 이번 tick I/O 완료 → 큐 뒤 (id 순)  ④ pick_next() 가 고른 작업이 1 tick 실행
 *   ⑤ 끝났으면 DONE / I/O 차례면 BLOCKED / 아니면 tick_end 훅 (RR 회전, MLFQ 강등)
 * "실행 중인 작업도 자기 레디 큐 안(보통 맨 앞)에 그대로 있다" — 빼는 건 끝나거나 막힐 때뿐.
 *
 * 체크는 손 계산(OSTEP 7·8장 그림), 브루트포스(순열 전수), 고정 시드 xorshift32 랜덤,
 * mlfq.py 출력에서 옮겨 적은 값만 본다. 시뮬레이터는 tick 상한(MAXT)이 있어서
 * 스텁이 아무것도 고르지 않아도 멈추지 않고 FAIL 로 끝난다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXJ 16          /* 작업 수 상한 */
#define MAXL 8           /* MLFQ 레벨 수 상한 */
#define MAXT 20000       /* tick 상한 — 넘으면 simulate() 가 -1 (무한 루프 방지) */

typedef enum { POL_FCFS, POL_SJF, POL_STCF, POL_RR, POL_MLFQ } policy_t;

typedef struct {
    int arrival;         /* 도착 tick */
    int length;          /* 필요한 CPU tick 총량 */
    int io_every;        /* 0 = I/O 없음. k>0 이면 CPU 를 k tick 쓸 때마다 I/O (마지막 tick 뒤에는 안 함) */
    int io_len;          /* I/O 한 번의 길이 (tick) */
} job_t;

typedef struct {
    policy_t pol;
    int quantum;                 /* RR time slice */
    int nlevels;                 /* MLFQ 레벨 수. 레벨 0 = 가장 높은 우선순위 */
    int qlen[MAXL];              /* MLFQ 레벨별 time slice */
    int allot[MAXL];             /* MLFQ 레벨별 할당량 (time slice 몇 개) */
    int boost;                   /* MLFQ 부스트 주기 S (0 = 안 함) */
    bool stay_on_io;             /* 옛 규칙 4b: I/O 하면 그 레벨의 slice·할당량을 새로 채움 (mlfq.py -S) */
} cfg_t;

enum { ST_NEW, ST_READY, ST_BLOCKED, ST_DONE };

typedef struct {
    const job_t *job; int n; cfg_t cfg;
    int t;                                   /* 현재 tick */
    int state[MAXJ], remaining[MAXJ], ran[MAXJ], io_until[MAXJ];
    int level[MAXJ], ticks_left[MAXJ], allot_left[MAXJ];   /* MLFQ 상태 */
    int rq[MAXL][MAXJ], rqn[MAXL];           /* 레디 큐 (FIFO 순서). FCFS/SJF/STCF/RR 은 rq[0] 만 */
    int cur;                                 /* 직전 tick 에 돈 작업 (아직 READY 일 때만, 아니면 -1) */
    int slice;                               /* RR: cur 가 이번 slice 에 쓴 tick */
    int first_run[MAXJ], finish[MAXJ], waiting[MAXJ], io_ticks[MAXJ];
    int turnaround[MAXJ], response[MAXJ];
    double avg_tat, avg_resp, avg_wait;
    int dispatches;                          /* 직전 tick 과 다른 작업을 고른 횟수 (≈ 컨텍스트 스위치) */
    char gantt[MAXT + 1];                    /* tick 별 'A'+id, 쉬면 '.' */
    signed char glevel[MAXT];                /* tick 별 실행 레벨 (MLFQ), 쉬면 -1 */
} sim_t;

/* ---------------- 하네스 제공: 레디 큐 도우미 ---------------- */
static void rq_push(sim_t *s, int lv, int j) { s->rq[lv][s->rqn[lv]++] = j; }
static int  rq_find(const sim_t *s, int lv, int j) {
    for (int i = 0; i < s->rqn[lv]; i++) if (s->rq[lv][i] == j) return i;
    return -1;
}
static void rq_remove(sim_t *s, int lv, int j) {
    int i = rq_find(s, lv, j);
    if (i < 0) return;
    memmove(&s->rq[lv][i], &s->rq[lv][i + 1], (size_t)(s->rqn[lv] - i - 1) * sizeof(int));
    s->rqn[lv]--;
}
static void rq_move_to_tail(sim_t *s, int lv, int j) { rq_remove(s, lv, j); rq_push(s, lv, j); }

/* 다음 CPU 버스트 길이 = 다음 I/O 나 종료까지 남은 CPU tick (SJF/STCF 의 "오라클") */
static int next_burst(const sim_t *s, int j) {
    const job_t *jb = &s->job[j];
    int left = s->remaining[j];
    if (jb->io_every > 0) {
        int to_io = jb->io_every - s->ran[j] % jb->io_every;
        if (to_io < left) return to_io;
    }
    return left;
}

/* ================= Part A — 선택 규칙과 지표 ================= */
static int mlfq_pick(const sim_t *s);

/* 이번 tick 에 돌 작업 id (없으면 -1). 큐 순서 = 도착/복귀 순서.
 *  FCFS: 큐 맨 앞.  SJF(비선점): cur 가 있으면 계속, 없으면 next_burst 최소 (동률은 큐 앞쪽).
 *  STCF(선점): next_burst 최소. cur 가 최소와 같으면 cur 유지, 아니면 큐 앞쪽.
 *  RR: 큐 맨 앞 (slice 가 끝난 작업은 rr_tick_end 가 뒤로 보낸다). */
static int pick_next(sim_t *s) {
    if (s->cfg.pol == POL_MLFQ) return mlfq_pick(s);
    (void)next_burst;
    return -1;   /* TODO: FCFS / SJF / STCF / RR — 큐 rq[0] 에서 고른다 */
}

/* RR: 막히지 않고 1 tick 돈 뒤 호출. slice 를 다 쓰면 큐 뒤로 + cur=-1 (다음 선택 때 slice 새로 시작) */
static void rr_tick_end(sim_t *s, int j, bool blocked) {
    (void)s; (void)j; (void)blocked; (void)rq_move_to_tail;
    /* TODO: slice 를 다 쓰면 큐 뒤로 보내고 cur = -1 */
}

/* 작업별 반환/응답 시간과 세 평균. finish·first_run·waiting 은 엔진이 채워 둔다. */
static void compute_metrics(sim_t *s) {
    (void)s;
    /* TODO: turnaround[j], response[j], avg_tat, avg_resp, avg_wait */
}

/* ================= Part B — MLFQ ================= */
/* 규칙 1·2: 비어 있지 않은 가장 높은 레벨의 맨 앞 (돌던 작업은 맨 앞에 그대로 있다 → 선점돼도 이어서 돈다) */
static int mlfq_pick(const sim_t *s) {
    (void)s;
    return -1;   /* TODO: 규칙 1·2 */
}

/* 1 tick 돈 뒤 (끝나지 않았을 때) 호출. 규칙 4: slice 를 다 쓰면 할당량 1 차감,
 * 할당량이 0 이면 한 레벨 강등(맨 아래면 그 자리에서 리필), 아니면 같은 레벨 뒤로 (규칙 2).
 * stay_on_io (옛 규칙 4b): I/O 하는 순간 그 레벨 slice·할당량 리필 — mlfq.py -S 와 같은 순서로
 * "리필 → (slice 가 방금 0 이 됐으면) 차감" 을 한다. 막힌 작업은 큐에 없으니 레벨만 바꾼다. */
static void mlfq_tick_end(sim_t *s, int j, bool blocked) {
    (void)s; (void)j; (void)blocked;
    /* TODO: 규칙 4 (+ stay_on_io 이면 옛 규칙 4b) */
}

/* 규칙 5: 낮은 레벨 큐들을 맨 위 큐 뒤에 붙이고 (mlfq.py 처럼 가장 낮은 레벨부터),
 * 끝나지 않은 모든 작업(막힌 작업·도착 전 작업 포함)의 레벨·slice·할당량을 맨 위 기준으로 리셋 */
static void mlfq_boost(sim_t *s) {
    (void)s;
    /* TODO: 규칙 5 */
}

/* ================= Part C — 공정성 지표 ================= */
/* [t0, t1) 동안 작업 j 가 CPU 를 쓴 tick 수 (시뮬레이션 끝 이후는 0) */
static int cpu_ticks_in(const sim_t *s, int j, int t0, int t1) {
    (void)s; (void)j; (void)t0; (void)t1;
    return 0;    /* TODO */
}

/* 처음 실행 ~ 종료 사이에 j 가 연속으로 CPU 를 못 쓴 최장 tick 수 (기아 지표). 한 번도 안 돌았으면 -1 */
static int longest_gap(const sim_t *s, int j) {
    (void)s; (void)j;
    return -1;   /* TODO */
}

/* ---------------- 하네스 제공: 시뮬레이션 엔진 ---------------- */
static int simulate(const job_t *jobs, int n, const cfg_t *cfg, sim_t *s) {
    memset(s, 0, sizeof *s);
    s->job = jobs; s->n = n; s->cfg = *cfg; s->cur = -1;
    if (n < 1 || n > MAXJ) return -1;
    bool mlfq = cfg->pol == POL_MLFQ;
    if (mlfq && (cfg->nlevels < 1 || cfg->nlevels > MAXL)) return -1;
    if (cfg->pol == POL_RR && cfg->quantum < 1) return -1;
    for (int j = 0; j < n; j++) {
        if (jobs[j].length < 1 || jobs[j].arrival < 0 || jobs[j].io_len < 0) return -1;
        s->state[j] = ST_NEW; s->remaining[j] = jobs[j].length;
        s->first_run[j] = -1; s->finish[j] = -1;
        if (mlfq) { s->ticks_left[j] = cfg->qlen[0]; s->allot_left[j] = cfg->allot[0]; }
    }
    int ndone = 0;
    while (ndone < n) {
        int t = s->t;
        if (t >= MAXT) return -1;                                    /* ① 상한: 스텁도 여기서 끝난다 */
        if (mlfq && cfg->boost > 0 && t > 0 && t % cfg->boost == 0) mlfq_boost(s);
        for (int j = 0; j < n; j++)                                  /* ② 도착 */
            if (s->state[j] == ST_NEW && jobs[j].arrival == t) { s->state[j] = ST_READY; rq_push(s, s->level[j], j); }
        for (int j = 0; j < n; j++)                                  /* ③ I/O 완료 */
            if (s->state[j] == ST_BLOCKED && s->io_until[j] == t) { s->state[j] = ST_READY; rq_push(s, s->level[j], j); }
        int j = pick_next(s);                                        /* ④ 선택 */
        if (j >= n || (j >= 0 && (s->state[j] != ST_READY || rq_find(s, s->level[j], j) < 0))) return -1;
        for (int k = 0; k < n; k++) {
            if (s->state[k] == ST_READY && k != j) s->waiting[k]++;
            if (s->state[k] == ST_BLOCKED) s->io_ticks[k]++;
        }
        if (j < 0) { s->gantt[t] = '.'; s->glevel[t] = -1; s->cur = -1; s->t = t + 1; continue; }
        if (j != s->cur) { s->dispatches++; s->slice = 0; }
        if (s->first_run[j] < 0) s->first_run[j] = t;
        s->gantt[t] = (char)('A' + j); s->glevel[t] = (signed char)s->level[j];
        s->remaining[j]--; s->ran[j]++; s->t = t + 1; s->cur = j;
        if (s->remaining[j] == 0) {                                  /* ⑤ 종료 */
            s->state[j] = ST_DONE; s->finish[j] = t + 1;
            rq_remove(s, s->level[j], j); s->cur = -1; ndone++;
            continue;
        }
        bool blocked = jobs[j].io_every > 0 && s->ran[j] % jobs[j].io_every == 0;
        if (blocked) {
            s->state[j] = ST_BLOCKED; s->io_until[j] = t + 1 + jobs[j].io_len;
            rq_remove(s, s->level[j], j); s->cur = -1;
        }
        if (cfg->pol == POL_RR) rr_tick_end(s, j, blocked);
        else if (mlfq) mlfq_tick_end(s, j, blocked);
    }
    s->gantt[s->t] = '\0';
    compute_metrics(s);
    return 0;
}

#ifndef SCHED_NO_MAIN
/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define NJ(a) ((int)(sizeof(a) / sizeof((a)[0])))

static sim_t S, S2;     /* 커서 정적으로 (스택 대신) */

/* gantt 를 "A10 B10 .5" 처럼 압축 */
static const char *rle(const sim_t *s) {
    static char buf[4096];
    size_t o = 0; buf[0] = '\0';
    for (int t = 0; t < s->t && o + 16 < sizeof buf;) {
        int u = t;
        while (u < s->t && s->gantt[u] == s->gantt[t]) u++;
        o += (size_t)snprintf(buf + o, sizeof buf - o, "%s%c%d", o ? " " : "", s->gantt[t], u - t);
        t = u;
    }
    return buf;
}
static bool near(double a, double b) { return a - b < 1e-9 && b - a < 1e-9; }
static cfg_t C(policy_t p, int q) { cfg_t c = {0}; c.pol = p; c.quantum = q; return c; }
static cfg_t MLFQ3(int q, int boost, bool stay) {            /* OSTEP 기본: 3 레벨, slice q, 할당량 1 */
    cfg_t c = {0}; c.pol = POL_MLFQ; c.nlevels = 3; c.boost = boost; c.stay_on_io = stay;
    for (int i = 0; i < 3; i++) { c.qlen[i] = q; c.allot[i] = 1; }
    return c;
}
static int run(const job_t *jobs, int n, cfg_t c) { return simulate(jobs, n, &c, &S); }
/* 모든 작업에 대해: 반환 = CPU + 대기 + I/O (시간 보존) */
static bool conserved(const sim_t *s) {
    for (int j = 0; j < s->n; j++)
        if (s->turnaround[j] != s->job[j].length + s->waiting[j] + s->io_ticks[j]) return false;
    return true;
}
static int makespan(const sim_t *s) { int m = 0; for (int j = 0; j < s->n; j++) if (s->finish[j] > m) m = s->finish[j]; return m; }

static uint32_t rng = 2463534242u;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* 모두 0 에 도착, I/O 없음: jobs 를 perm 순서로 FCFS 하면 나오는 반환 시간 합 */
static long fcfs_sum(const job_t *jobs, const int *perm, int n) {
    long t = 0, sum = 0;
    for (int i = 0; i < n; i++) { t += jobs[perm[i]].length; sum += t; }
    return sum;
}
static long best_perm(const job_t *jobs, int *perm, int k, int n) {      /* 순열 전수 최소 */
    if (k == n) return fcfs_sum(jobs, perm, n);
    long best = -1;
    for (int i = k; i < n; i++) {
        int tmp = perm[k]; perm[k] = perm[i]; perm[i] = tmp;
        long v = best_perm(jobs, perm, k + 1, n);
        if (best < 0 || v < best) best = v;
        tmp = perm[k]; perm[k] = perm[i]; perm[i] = tmp;
    }
    return best;
}

static void part_a(void) {
    /* OSTEP 그림 7.1 / 7.2 / 7.3 */
    job_t f1[] = {{0, 10, 0, 0}, {0, 10, 0, 0}, {0, 10, 0, 0}};
    CHECK(run(f1, 3, C(POL_FCFS, 0)) == 0 && near(S.avg_tat, 20.0));
    CHECK(strcmp(rle(&S), "A10 B10 C10") == 0);
    job_t f2[] = {{0, 100, 0, 0}, {0, 10, 0, 0}, {0, 10, 0, 0}};
    CHECK(run(f2, 3, C(POL_FCFS, 0)) == 0 && near(S.avg_tat, 110.0) && near(S.avg_resp, 70.0));
    CHECK(run(f2, 3, C(POL_SJF, 0)) == 0 && near(S.avg_tat, 50.0) && strcmp(rle(&S), "B10 C10 A100") == 0);
    /* 그림 7.4 (SJF, B·C 가 10 에 도착) / 7.5 (STCF) */
    job_t f4[] = {{0, 100, 0, 0}, {10, 10, 0, 0}, {10, 10, 0, 0}};
    CHECK(run(f4, 3, C(POL_SJF, 0)) == 0 && near(S.avg_tat, 310.0 / 3) && strcmp(rle(&S), "A100 B10 C10") == 0);
    CHECK(run(f4, 3, C(POL_STCF, 0)) == 0 && near(S.avg_tat, 50.0) && strcmp(rle(&S), "A10 B10 C10 A90") == 0);
    CHECK(S.response[0] == 0 && S.response[1] == 0 && S.response[2] == 10);
    /* 그림 7.6 / 7.7: 5초짜리 3개 — SJF 응답 5, RR(q=1) 응답 1 */
    job_t f6[] = {{0, 5, 0, 0}, {0, 5, 0, 0}, {0, 5, 0, 0}};
    CHECK(run(f6, 3, C(POL_SJF, 0)) == 0 && near(S.avg_resp, 5.0) && near(S.avg_tat, 10.0));
    CHECK(run(f6, 3, C(POL_RR, 1)) == 0 && near(S.avg_resp, 1.0) && near(S.avg_tat, 14.0));
    CHECK(strncmp(S.gantt, "ABCABCABC", 9) == 0 && S.dispatches == 15);
    /* RR 동률 규칙: slice 가 끝난 작업이 같은 tick 에 도착한 작업보다 먼저 큐에 선다 (mlfq.py 와 같음) */
    job_t tie[] = {{0, 4, 0, 0}, {2, 2, 0, 0}};
    CHECK(run(tie, 2, C(POL_RR, 2)) == 0 && strcmp(S.gantt, "AAAABB") == 0);
    /* 그림 7.8 / 7.9: A 는 10 돌고 10 I/O, B 는 CPU 50 */
    job_t f8[] = {{0, 50, 10, 10}, {0, 50, 0, 0}};
    CHECK(run(f8, 2, C(POL_FCFS, 0)) == 0 && makespan(&S) == 130);        /* B 50 뒤에 A 가 줄 서고, A 의 I/O 동안 CPU 가 논다 */
    CHECK(strcmp(rle(&S), "A10 B50 A10 .10 A10 .10 A10 .10 A10") == 0);
    CHECK(run(f8, 2, C(POL_STCF, 0)) == 0 && makespan(&S) == 100);        /* 겹치기(overlap): 놀지 않는다 */
    CHECK(strcmp(rle(&S), "A10 B10 A10 B10 A10 B10 A10 B20 A10") == 0 && conserved(&S));
    /* 고정 시드 랜덤: 시간 보존, 작업 보존(makespan 동일), SJF = 순열 최솟값, STCF 가 평균 반환 최소 */
    bool simok = true, cons = true, ms_same = true, sjf_opt = true, stcf_best = true, rr1_resp = true;
    for (int it = 0; it < 300; it++) {
        int n = 2 + (int)(xs32() % 5);                                     /* 2..6 */
        job_t jb[6];
        bool zero = it % 2 == 0;
        for (int j = 0; j < n; j++) {
            jb[j].arrival = zero ? 0 : (int)(xs32() % 20);
            jb[j].length = 1 + (int)(xs32() % 15);
            jb[j].io_every = 0; jb[j].io_len = 0;
        }
        double tat[4] = {0}; int ms[4] = {0};
        policy_t ps[4] = {POL_FCFS, POL_SJF, POL_STCF, POL_RR};
        for (int p = 0; p < 4; p++) {
            if (run(jb, n, C(ps[p], 1 + (int)(xs32() % 4))) != 0) { simok = false; continue; }
            tat[p] = S.avg_tat; ms[p] = makespan(&S);
            cons = cons && conserved(&S);
        }
        for (int p = 1; p < 4; p++) ms_same = ms_same && ms[p] == ms[0];
        for (int p = 0; p < 4; p++) stcf_best = stcf_best && tat[2] <= tat[p] + 1e-9;
        if (zero) {
            int perm[6]; for (int j = 0; j < n; j++) perm[j] = j;
            sjf_opt = sjf_opt && near(tat[1] * n, (double)best_perm(jb, perm, 0, n));
            if (run(jb, n, C(POL_RR, 1)) != 0) simok = false;                /* 0 에 다 도착: 응답 = 순번 */
            for (int j = 0; j < n; j++) rr1_resp = rr1_resp && S.response[j] == j;
        }
        /* I/O 섞인 랜덤도 시간 보존 */
        for (int j = 0; j < n; j++) { jb[j].io_every = (int)(xs32() % 4); jb[j].io_len = 1 + (int)(xs32() % 5); }
        for (int p = 0; p < 4; p++)
            cons = cons && run(jb, n, C(ps[p], 1 + (int)(xs32() % 4))) == 0 && conserved(&S);
    }
    CHECK(simok && cons);
    CHECK(simok && ms_same);
    CHECK(simok && sjf_opt);
    CHECK(simok && stcf_best);
    CHECK(simok && rr1_resp);
}

/* mlfq.py 결과 옮겨 적기: python3 -I mlfq.py -n 3 -q 10 -l 0,120,0:30,40,7:50,25,3 -i 5 -B 100 -c */
static const int OSTEP_RESP[3] = {0, 0, 0}, OSTEP_TAT[3] = {185, 107, 77};

static void part_b(void) {
    /* 그림 8.2: 긴 작업 하나 — 레벨 0 에서 10, 레벨 1 에서 10, 나머지는 레벨 2 */
    job_t one[] = {{0, 200, 0, 0}};
    CHECK(run(one, 1, MLFQ3(10, 0, false)) == 0 && S.glevel[0] == 0 && S.glevel[9] == 0
          && S.glevel[10] == 1 && S.glevel[19] == 1 && S.glevel[20] == 2 && S.glevel[199] == 2);
    /* 그림 8.3: 짧은 작업 B 가 100 에 도착 → 바로 선점, 20 만에 끝 */
    job_t f3[] = {{0, 200, 0, 0}, {100, 20, 0, 0}};
    CHECK(run(f3, 2, MLFQ3(10, 0, false)) == 0 && S.response[1] == 0 && S.turnaround[1] == 20);
    CHECK(strcmp(rle(&S), "A100 B20 A100") == 0 && S.glevel[100] == 0 && S.glevel[110] == 1 && S.glevel[120] == 2);
    /* 규칙 4 (새 규칙): 1 tick 돌고 I/O 하는 작업도 레벨 0 할당량(10 tick) 을 다 쓰면 강등 */
    job_t f4[] = {{0, 100, 0, 0}, {0, 30, 1, 1}};
    CHECK(run(f4, 2, MLFQ3(10, 0, false)) == 0);
    {
        int seen = 0, lv_at11 = -2;
        for (int t = 0; t < S.t; t++) if (S.gantt[t] == 'B' && ++seen == 11) lv_at11 = S.glevel[t];
        CHECK(lv_at11 == 1);
    }
    /* 옛 규칙 4b (stay_on_io): 같은 작업이 끝까지 레벨 0 */
    CHECK(run(f4, 2, MLFQ3(10, 0, true)) == 0);
    {
        int at0 = 0;
        for (int t = 0; t < S.t; t++) at0 += S.gantt[t] == 'B' && S.glevel[t] == 0;
        CHECK(at0 == 30);
    }
    /* 규칙 5: 부스트 50 이면 t=50 에 긴 작업이 다시 레벨 0 */
    CHECK(run(one, 1, MLFQ3(10, 50, false)) == 0 && S.glevel[49] == 2 && S.glevel[50] == 0 && S.glevel[60] == 1);
    /* mlfq.py 교차 검증 (옮겨 적은 값) */
    job_t ox[] = {{0, 120, 0, 0}, {30, 40, 7, 5}, {50, 25, 3, 5}};
    CHECK(run(ox, 3, MLFQ3(10, 100, false)) == 0);
    {
        bool same = true;
        for (int j = 0; j < 3; j++) same = same && S.response[j] == OSTEP_RESP[j] && S.turnaround[j] == OSTEP_TAT[j];
        CHECK(same);
    }
    /* 참조 구현 비교: 레벨 1개 MLFQ(할당량 1) = RR(같은 q), I/O 없을 때 tick 단위로 같다 */
    bool eq_rr = true, eq_fcfs = true;
    for (int it = 0; it < 200; it++) {
        int n = 1 + (int)(xs32() % 6), q = 1 + (int)(xs32() % 5);
        job_t jb[6];
        for (int j = 0; j < n; j++) { jb[j].arrival = (int)(xs32() % 25); jb[j].length = 1 + (int)(xs32() % 20); jb[j].io_every = 0; jb[j].io_len = 0; }
        cfg_t m = {0}; m.pol = POL_MLFQ; m.nlevels = 1; m.qlen[0] = q; m.allot[0] = 1;
        cfg_t r = C(POL_RR, q);
        eq_rr = eq_rr && simulate(jb, n, &m, &S) == 0 && simulate(jb, n, &r, &S2) == 0 && strcmp(S.gantt, S2.gantt) == 0;
        m.qlen[0] = 1000; r = C(POL_FCFS, 0);                              /* slice 가 무한하면 FCFS */
        eq_fcfs = eq_fcfs && simulate(jb, n, &m, &S) == 0 && simulate(jb, n, &r, &S2) == 0 && strcmp(S.gantt, S2.gantt) == 0;
    }
    CHECK(eq_rr);
    CHECK(eq_fcfs);
}

static void part_c(void) {
    /* 지표 단위 테스트: 손으로 만든 gantt */
    job_t two[] = {{0, 4, 0, 0}, {0, 4, 0, 0}};
    CHECK(run(two, 2, C(POL_RR, 1)) == 0 && strcmp(S.gantt, "ABABABAB") == 0);
    CHECK(cpu_ticks_in(&S, 0, 0, 8) == 4 && cpu_ticks_in(&S, 1, 2, 5) == 1 && cpu_ticks_in(&S, 0, 6, 100) == 1);
    CHECK(longest_gap(&S, 0) == 1 && longest_gap(&S, 1) == 1);
    CHECK(run(two, 2, C(POL_FCFS, 0)) == 0 && longest_gap(&S, 0) == 0 && cpu_ticks_in(&S, 1, 0, 4) == 0);

    /* 기아: 배치 작업 + 20 에 도착하는 대화형 작업 2개 (1 tick 돌고 1 tick I/O) */
    job_t st[] = {{0, 200, 0, 0}, {20, 100, 1, 1}, {20, 100, 1, 1}};
    int gap[3], got[3];                                   /* [0] 옛 규칙  [1] 옛 규칙 + 부스트 50  [2] 새 규칙 4 */
    CHECK(run(st, 3, MLFQ3(10, 0, true)) == 0);   gap[0] = longest_gap(&S, 0); got[0] = cpu_ticks_in(&S, 0, 20, 220);
    CHECK(run(st, 3, MLFQ3(10, 50, true)) == 0);  gap[1] = longest_gap(&S, 0); got[1] = cpu_ticks_in(&S, 0, 20, 220);
    CHECK(run(st, 3, MLFQ3(10, 0, false)) == 0);  gap[2] = longest_gap(&S, 0); got[2] = cpu_ticks_in(&S, 0, 20, 220);
    printf("starvation: batch ticks in [20,220) / longest gap  old=%d/%d  old+boost50=%d/%d  new=%d/%d\n",
           got[0], gap[0], got[1], gap[1], got[2], gap[2]);
    CHECK(got[0] == 0 && got[1] == 40 && got[2] == 134);  /* 값은 mlfq.py 와 tick 단위로 같다 (demo_ostep_check.sh) */
    CHECK(gap[0] == 200 && gap[1] == 40 && gap[2] == 40);

    /* 게이밍: 배치 A 와 게이머 G (9 tick 돌고 1 tick I/O, slice 는 10) */
    job_t gm[] = {{0, 300, 0, 0}, {0, 300, 9, 1}};
    int g_old, g_new, g_old_b;
    CHECK(run(gm, 2, MLFQ3(10, 0, true)) == 0);   g_old = cpu_ticks_in(&S, 1, 0, 300);
    CHECK(run(gm, 2, MLFQ3(10, 0, false)) == 0);  g_new = cpu_ticks_in(&S, 1, 0, 300);
    CHECK(run(gm, 2, MLFQ3(10, 100, true)) == 0); g_old_b = cpu_ticks_in(&S, 1, 0, 300);
    printf("gaming: gamer CPU ticks in [0,300)  old=%d  new=%d  old+boost100=%d\n", g_old, g_new, g_old_b);
    CHECK(g_old == 261 && g_new == 108 && g_old_b == 243);   /* 87% / 36% / 81% */
}

int main(void) {
    part_a();
    part_b();
    part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* SCHED_NO_MAIN */
