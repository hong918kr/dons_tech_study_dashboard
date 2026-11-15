/* RTOS-M12 EDF 스케줄링 시뮬레이터와 RM 비교
 *
 * 호스트 모델: OS 스레드 1개로 도는 "tick 단위 결정적 스케줄 시뮬레이터" (커널 함수 호출 없음).
 *  - 태스크 i = 주기 태스크 (C 실행 시간, T 주기, D 상대 데드라인(0 이면 D = T), O 첫 릴리즈).
 *    job k 는 O + k*T 에 릴리즈되고 절대 데드라인 = 릴리즈 + D. 1 tick 에 1 단위 실행 (단일 코어).
 *  - tick t 의 순서: (1) 데드라인 검사 (절대 데드라인 == t 인데 남은 일이 있으면 미스)
 *                    (2) 릴리즈  (3) 실행할 job 고르기  (4) 선점·컨텍스트 스위치 집계  (5) 1 tick 실행.
 *  - 정책 RM : 주기가 짧을수록 높은 우선순위 (고정). 주기가 같으면 태스크 번호가 작은 쪽.
 *         EDF: 절대 데드라인이 가장 이른 job. 같으면 태스크 번호가 작은 쪽, 그래도 같으면 먼저 릴리즈된 job.
 *    같은 태스크의 밀린 job 들은 항상 릴리즈 순서(FIFO)로 돈다 (RM: seq, EDF: 데드라인이 더 이르다).
 *  - 미스 정책 MISS_CONTINUE: 데드라인을 넘긴 job 도 끝까지 실행 (절대 데드라인은 그대로 → EDF 에서 최우선).
 *              MISS_DROP    : 데드라인 순간에 남은 일을 버린다 (abort).
 *  - 선점(preemption): 직전 tick 에 돌던 job 이 아직 남은 일이 있는데 이번 tick 에 다른 job 이 돈다.
 *    컨텍스트 스위치: 이번 tick 에 도는 "태스크"(idle 포함, idle = -1)가 직전 tick 과 다르다.
 *    (같은 태스크의 다음 job 으로 넘어가는 것은 스위치가 아니다 — 실제 RTOS 에서 태스크는 같은 스레드의 루프.)
 *  - 타임라인: R=실행, r=끝나지 않은 job 이 있는데 못 돎, .=할 일 없음. miss 줄 = 그 tick 에 미스가 확인된 태스크.
 *
 * 이 레슨에서 채우는 것 (TODO 1~4):
 *  1. hyperperiod()  : lcm(T_i), 64비트로 계산하고 HYPER_MAX 를 넘으면 0 (오버플로 방지)
 *  2. pick_job()     : RM / EDF 선택 규칙과 동점 규칙
 *  3. on_deadlines() : 데드라인 미스 판정 + MISS_CONTINUE / MISS_DROP
 *  4. account()      : 선점 · 컨텍스트 스위치 집계
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXT      8
#define MAXACT    256          /* 동시에 살아 있는 job 최대 수 (밀린 job 포함) */
#define MAXTL     240          /* 타임라인으로 기록하는 tick 수 */
#define HYPER_MAX 100000u      /* 이보다 긴 하이퍼피리어드는 "시뮬레이션 거부" */
#define NONE      UINT32_MAX

typedef struct {
    const char *name;
    uint32_t C, T, D, O;       /* D = 0 → D = T */
    uint32_t ovr_job, ovr_extra;   /* job 번호 ovr_job 이 C + ovr_extra 만큼 실행 (overrun 주입, 0 = 없음) */
} ptask_t;

typedef enum { POL_RM, POL_EDF } policy_t;
typedef enum { MISS_CONTINUE, MISS_DROP } miss_policy_t;

typedef struct { int task; uint32_t rel, dl, rem, seq; int missed; } job_t;

typedef struct {
    uint32_t released, completed, missed, dropped, preempted;
    uint32_t max_resp, max_backlog, first_miss;
} tstat_t;

typedef struct {
    const ptask_t *ts; int n; policy_t pol; miss_policy_t mp;
    int rank[MAXT];            /* RM 우선순위 순위: 0 = 가장 높음 */
    job_t act[MAXACT]; int nact; uint32_t seq;
    uint32_t now, horizon, jobs_of[MAXT];
    int prev_task; uint32_t prev_seq;     /* 직전 tick 에 돈 태스크(-1 = idle) / job seq (NONE = 없음) */
    tstat_t st[MAXT];
    uint32_t preemptions, ctx_switches, idle_ticks, busy_ticks, misses, overflow;
    char row[MAXT][MAXTL + 1];
    char missrow[MAXTL + 1];
} sim_t;

static uint32_t dl_of(const ptask_t *t) { return t->D ? t->D : t->T; }

/* ---------------- TODO 1: 하이퍼피리어드 ---------------- */
static uint64_t gcd_u64(uint64_t a, uint64_t b) { while (b) { uint64_t r = a % b; a = b; b = r; } return a; }
static uint32_t hyperperiod(const ptask_t *ts, int n) {
    /* TODO 1: h = lcm(h, T_i) 를 64비트로 누적. T_i == 0 이거나 h > HYPER_MAX 가 되면 0 */
    (void)ts; (void)n; (void)gcd_u64;
    return 0;
}

/* ---------------- 주어진 코드: 초기화, 릴리즈, job 제거 ---------------- */
static void sim_init(sim_t *s, const ptask_t *ts, int n, policy_t pol, miss_policy_t mp) {
    memset(s, 0, sizeof *s);
    s->ts = ts; s->n = n; s->pol = pol; s->mp = mp;
    s->prev_task = -1; s->prev_seq = NONE;
    for (int i = 0; i < n; i++) {          /* RM 순위: 주기 오름차순, 같으면 번호 작은 쪽 */
        int r = 0;
        for (int j = 0; j < n; j++)
            if (ts[j].T < ts[i].T || (ts[j].T == ts[i].T && j < i)) r++;
        s->rank[i] = r;
        s->st[i].first_miss = NONE;
        memset(s->row[i], '.', MAXTL); s->row[i][MAXTL] = 0;
    }
    memset(s->missrow, ' ', MAXTL); s->missrow[MAXTL] = 0;
}
static void remove_job(sim_t *s, int j) { s->act[j] = s->act[--s->nact]; }
static void release_jobs(sim_t *s) {
    for (int i = 0; i < s->n; i++) {
        const ptask_t *t = &s->ts[i];
        if (s->now < t->O || (s->now - t->O) % t->T != 0) continue;
        uint32_t k = s->jobs_of[i]++;
        s->st[i].released++;
        if (s->nact == MAXACT) { s->overflow++; continue; }
        job_t *j = &s->act[s->nact++];
        j->task = i; j->rel = s->now; j->dl = s->now + dl_of(t);
        j->rem = t->C + ((t->ovr_extra && k == t->ovr_job) ? t->ovr_extra : 0);
        j->seq = s->seq++; j->missed = 0;
    }
}
static void note_miss(sim_t *s, int task) {
    if (s->now < MAXTL) s->missrow[s->now] = (s->missrow[s->now] == ' ') ? s->ts[task].name[0] : '*';
}

/* ---------------- TODO 2: 다음에 돌 job 고르기 ---------------- */
/* a 가 b 보다 먼저 돌아야 하면 1 */
static int before(const sim_t *s, const job_t *a, const job_t *b) {
    /* TODO 2: RM = (rank, seq) 사전식, EDF = (dl, task, seq) 사전식. 지금은 릴리즈 순서(FIFO) */
    (void)s;
    return a->seq < b->seq;
}
static int pick_job(const sim_t *s) {
    int best = -1;
    for (int j = 0; j < s->nact; j++)
        if (best < 0 || before(s, &s->act[j], &s->act[best])) best = j;
    return best;
}

/* ---------------- TODO 3: 데드라인 미스 판정 + 정책 ---------------- */
static void on_deadlines(sim_t *s) {
    /* TODO 3: act[] 중 dl == now 인 job → missed 표시, 통계(missed, misses, first_miss, note_miss),
     *         MISS_DROP 이면 dropped++ 후 remove_job (지운 자리에 마지막 job 이 오므로 j 를 올리지 말 것) */
    (void)s; (void)note_miss;
}

/* ---------------- TODO 4: 선점 · 컨텍스트 스위치 집계 ---------------- */
static void account(sim_t *s, int j) {
    /* TODO 4: j = 이번 tick 에 돌 job (-1 = idle). 직전 job(prev_seq)이 act[] 에 살아 있는데 다른 job 이
     *         고르면 선점, 태스크(idle = -1)가 바뀌면 컨텍스트 스위치. 끝에 prev_task / prev_seq 갱신 */
    (void)s; (void)j;
}

/* ---------------- 주어진 코드: 1 tick 실행과 전체 루프 ---------------- */
static void sim_tick(sim_t *s) {
    on_deadlines(s);
    release_jobs(s);
    for (int i = 0; i < s->n; i++) {
        uint32_t b = 0;
        for (int j = 0; j < s->nact; j++) b += (s->act[j].task == i);
        if (b > s->st[i].max_backlog) s->st[i].max_backlog = b;
        if (b && s->now < MAXTL) s->row[i][s->now] = 'r';
    }
    int j = pick_job(s);
    account(s, j);
    if (j < 0) { s->idle_ticks++; s->now++; return; }
    job_t *jb = &s->act[j];
    if (s->now < MAXTL) s->row[jb->task][s->now] = 'R';
    s->busy_ticks++;
    if (--jb->rem == 0) {
        tstat_t *st = &s->st[jb->task];
        uint32_t resp = s->now + 1 - jb->rel;
        st->completed++;
        if (resp > st->max_resp) st->max_resp = resp;
        if (s->prev_seq == jb->seq) s->prev_seq = NONE;   /* 끝난 job 은 다음 tick 에 "선점" 될 수 없다 */
        remove_job(s, j);
    }
    s->now++;
}
/* horizon = 0 → 하이퍼피리어드. 끝 시각의 데드라인까지 검사한다. 실패하면 -1. */
static int sim_run(sim_t *s, const ptask_t *ts, int n, policy_t pol, miss_policy_t mp, uint32_t horizon) {
    sim_init(s, ts, n, pol, mp);
    if (horizon == 0) horizon = hyperperiod(ts, n);
    if (horizon == 0) return -1;
    s->horizon = horizon;
    while (s->now < horizon) sim_tick(s);
    on_deadlines(s);                                   /* t = horizon 의 데드라인 */
    return 0;
}

static void print_sim(const sim_t *s, const char *title, uint32_t upto) {
    if (upto > MAXTL) upto = MAXTL;
    if (upto > s->horizon) upto = s->horizon;
    printf("%s\n tick : ", title);
    for (uint32_t t = 0; t < upto; t++) putchar((int)('0' + t % 10));
    putchar('\n');
    for (int i = 0; i < s->n; i++) printf(" %-5.5s: %.*s\n", s->ts[i].name, (int)upto, s->row[i]);
    printf(" miss : %.*s\n", (int)upto, s->missrow);
}
static void print_stats(const sim_t *s) {
    printf("  task  rel done miss drop preempted max_resp backlog\n");
    for (int i = 0; i < s->n; i++) {
        const tstat_t *st = &s->st[i];
        printf("  %-5.5s %3u %4u %4u %4u %9u %8u %7u\n", s->ts[i].name, st->released, st->completed,
               st->missed, st->dropped, st->preempted, st->max_resp, st->max_backlog);
    }
    printf("  total misses=%u preemptions=%u ctx_switches=%u busy=%u idle=%u\n",
           s->misses, s->preemptions, s->ctx_switches, s->busy_ticks, s->idle_ticks);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define ROW_IS(s, i, str) (strncmp((s)->row[i], (str), strlen(str)) == 0)

/* 오라클 (테스트 쪽): RM 응답 시간 분석 — RTOS-M11 의 R = C + Σ⌈R/Tj⌉Cj 를 그대로 쓴다 (D = T, 동기 릴리즈). */
static int rta_all_ok(const sim_t *s) {
    for (int i = 0; i < s->n; i++) {
        uint64_t R = s->ts[i].C, prev = 0;
        while (R != prev && R <= dl_of(&s->ts[i])) {
            prev = R; R = s->ts[i].C;
            for (int j = 0; j < s->n; j++)
                if (s->rank[j] < s->rank[i]) R += (prev + s->ts[j].T - 1) / s->ts[j].T * s->ts[j].C;
        }
        if (R > dl_of(&s->ts[i])) return 0;
    }
    return 1;
}
static uint32_t g_rng = 0x2026111Bu;
static uint32_t xorshift32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static uint32_t rnd(uint32_t lo, uint32_t hi) { return lo + xorshift32() % (hi - lo + 1); }
static const char *NAMES[MAXT] = { "a", "b", "c", "d", "e", "f", "g", "h" };

int main(int argc, char **argv) {
    int verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    static sim_t s, r;      /* static: sim_t 가 커서 스택 대신 */

    /* ---- Part A-1: 하이퍼피리어드 ---- */
    {
        const ptask_t a[] = { { "x", 1, 5, 0, 0, 0, 0 }, { "y", 1, 7, 0, 0, 0, 0 } };
        const ptask_t b[] = { { "x", 1, 4, 0, 0, 0, 0 }, { "y", 1, 6, 0, 0, 0, 0 }, { "z", 1, 10, 0, 0, 0, 0 } };
        const ptask_t c[] = { { "x", 1, 10, 0, 0, 0, 0 }, { "y", 1, 20, 0, 0, 0, 0 }, { "z", 1, 40, 0, 0, 0, 0 }, { "w", 1, 100, 0, 0, 0, 0 } };
        const ptask_t big[] = { { "x", 1, 9973, 0, 0, 0, 0 }, { "y", 1, 9967, 0, 0, 0, 0 } };          /* 99,400,891 */
        const ptask_t wrap[] = { { "x", 1, 65536, 0, 0, 0, 0 }, { "y", 1, 65537, 0, 0, 0, 0 } };      /* 2^32 + 65536 */
        const ptask_t zero[] = { { "x", 1, 0, 0, 0, 0, 0 } };
        CHECK(hyperperiod(a, 2) == 35);
        CHECK(hyperperiod(b, 3) == 60);
        CHECK(hyperperiod(c, 4) == 200);
        CHECK(hyperperiod(big, 2) == 0);
        CHECK(hyperperiod(wrap, 2) == 0);
        CHECK(65536u * 65537u == 65536u);      /* 32비트 곱은 조용히 감싼다 → 그럴듯한 "가짜" 하이퍼피리어드 */
        CHECK(hyperperiod(zero, 1) == 0);
        CHECK(sim_run(&s, big, 2, POL_EDF, MISS_CONTINUE, 0) == -1);
    }

    /* ---- Part A-2: U = 34/35 ≈ 0.971 두 태스크 (Buttazzo 의 고전 예) ---- */
    const ptask_t two[] = { { "t1", 2, 5, 0, 0, 0, 0 }, { "t2", 4, 7, 0, 0, 0, 0 } };
    CHECK(sim_run(&s, two, 2, POL_RM, MISS_CONTINUE, 0) == 0);
    CHECK(sim_run(&r, two, 2, POL_EDF, MISS_CONTINUE, 0) == 0);
    printf("Part A  U=34/35   misses  preempt  ctx  max_resp(t1,t2)  busy idle\n");
    printf("        RM     %8u %8u %4u %8u,%-8u %4u %4u\n", s.misses, s.preemptions, s.ctx_switches,
           s.st[0].max_resp, s.st[1].max_resp, s.busy_ticks, s.idle_ticks);
    printf("        EDF    %8u %8u %4u %8u,%-8u %4u %4u\n", r.misses, r.preemptions, r.ctx_switches,
           r.st[0].max_resp, r.st[1].max_resp, r.busy_ticks, r.idle_ticks);
    if (verbose) { print_sim(&s, "two-task RM (continue)", 35); print_stats(&s);
                   print_sim(&r, "two-task EDF (continue)", 35); print_stats(&r); }
    CHECK(s.st[1].first_miss == 7 && s.st[0].missed == 0);          /* RM: t2 의 첫 job 이 t=7 에 미스 */
    CHECK(s.misses == 1 && s.st[1].max_resp == 8);
    CHECK(r.misses == 0 && r.st[0].max_resp == 4 && r.st[1].max_resp == 6);
    CHECK(r.busy_ticks == 34 && r.idle_ticks == 1 && s.busy_ticks == 34);
    CHECK(ROW_IS(&s, 0, "RR...RR...RR...RR...RR...RR...RR..."));
    CHECK(ROW_IS(&s, 1, "rrRRRrrRRRrrRRRrrRRR.rRRRrrRRRrrRR."));
    CHECK(ROW_IS(&r, 0, "RR...rRR..rrRR.RR...RR...rRR..RR..."));
    CHECK(ROW_IS(&r, 1, "rrRRRR.rRRRR..RrrRRR.rRRRR..RRrrRR."));
    CHECK(s.preemptions == 5 && r.preemptions == 2);
    CHECK(s.ctx_switches == 15 && r.ctx_switches == 15);   /* 스위치 수는 같다 */
    sim_run(&s, two, 2, POL_RM, MISS_DROP, 0);
    CHECK(s.misses == 1 && s.st[1].dropped == 1 && s.st[1].completed == 4);

    /* ---- Part A-3: 무작위 태스크 세트 — 정리(theorem)를 오라클로 ---- */
    {
        static const uint32_t PER[] = { 4, 5, 6, 8, 10, 12, 15, 20, 24, 30, 40, 60 };
        uint32_t ovf = 0;
        int sets = 0, edf_iff = 0, rm_iff = 0, rm_ok_edf_ok = 1, work = 0, nwork = 0, nfeas = 0, nrmfail_feas = 0;
        uint64_t pre_rm = 0, pre_edf = 0, cs_rm = 0, cs_edf = 0;
        ptask_t ts[MAXT];
        for (int it = 0; it < 400; it++) {
            int n = (int)rnd(2, 5);
            for (int i = 0; i < n; i++) {
                uint32_t T = PER[rnd(0, 11)];
                ts[i] = (ptask_t){ NAMES[i], rnd(1, T / 2 > 1 ? T / 2 : 1), T, 0, 0, 0, 0 };
            }
            uint32_t H = hyperperiod(ts, n);
            uint64_t demand = 0;
            for (int i = 0; i < n; i++) demand += (uint64_t)ts[i].C * (H / ts[i].T);
            int feasible = demand <= H;                        /* U ≤ 1, 정수로: Σ C_i·(H/T_i) ≤ H */
            sim_run(&s, ts, n, POL_RM, MISS_CONTINUE, 0);
            sim_run(&r, ts, n, POL_EDF, MISS_CONTINUE, 0);
            sets++; ovf += s.overflow + r.overflow;
            edf_iff += ((r.misses == 0) == feasible);          /* Liu & Layland 1973: EDF ⇔ U ≤ 1 */
            rm_iff  += ((s.misses == 0) == rta_all_ok(&s));    /* 임계 순간 정리 + RTA (RTOS-M11) */
            if (s.misses == 0 && r.misses != 0) rm_ok_edf_ok = 0;
            if (feasible) {
                nfeas++; nwork++;
                work += (s.busy_ticks == demand && r.busy_ticks == demand);
                if (s.misses) nrmfail_feas++;
                pre_rm += s.preemptions; pre_edf += r.preemptions;
                cs_rm += s.ctx_switches; cs_edf += r.ctx_switches;
            }
        }
        printf("random: %d sets, %d with U<=1 (RM misses in %d of them), EDF<->U<=1 agree %d/%d, RM<->RTA agree %d/%d\n",
               sets, nfeas, nrmfail_feas, edf_iff, sets, rm_iff, sets);
        printf("        U<=1 sets: preemptions RM=%llu EDF=%llu, ctx switches RM=%llu EDF=%llu\n",
               (unsigned long long)pre_rm, (unsigned long long)pre_edf, (unsigned long long)cs_rm, (unsigned long long)cs_edf);
        CHECK(ovf == 0);
        CHECK(edf_iff == sets);
        CHECK(rm_iff == sets);
        CHECK(rm_ok_edf_ok);
        CHECK(work == nwork && nwork > 100);
        CHECK(nrmfail_feas > 10);                   /* U ≤ 1 인데 RM 이 놓친 세트가 실제로 있다 */
        CHECK(pre_edf < pre_rm);
    }

    /* ---- Part B: 과부하 U > 1 — EDF 도미노 vs RM 격리 ---- */
    const ptask_t ov[] = { { "A", 2, 8, 0, 0, 0, 0 }, { "B", 3, 10, 0, 0, 0, 0 },
                           { "C", 3, 12, 0, 0, 0, 0 }, { "D", 6, 15, 0, 0, 0, 0 } };   /* U = 1.2 */
    {
        static sim_t e_c, e_d, m_c, m_d;
        sim_run(&e_c, ov, 4, POL_EDF, MISS_CONTINUE, 0);
        sim_run(&e_d, ov, 4, POL_EDF, MISS_DROP, 0);
        sim_run(&m_c, ov, 4, POL_RM, MISS_CONTINUE, 0);
        sim_run(&m_d, ov, 4, POL_RM, MISS_DROP, 0);
        printf("Part B  U=1.2   H=%u   misses A/B/C/D            total  preempt\n", e_c.horizon);
        const sim_t *all[4] = { &m_c, &m_d, &e_c, &e_d };
        const char *lab[4] = { "RM  continue", "RM  drop    ", "EDF continue", "EDF drop    " };
        for (int k = 0; k < 4; k++)
            printf("        %s  %4u %4u %4u %4u  %12u %8u\n", lab[k], all[k]->st[0].missed, all[k]->st[1].missed,
                   all[k]->st[2].missed, all[k]->st[3].missed, all[k]->misses, all[k]->preemptions);
        if (verbose) { print_sim(&e_c, "overload EDF continue", 60); print_stats(&e_c);
                       print_sim(&m_c, "overload RM continue", 60); print_stats(&m_c);
                       print_sim(&e_d, "overload EDF drop", 60); print_stats(&e_d);
                       print_sim(&m_d, "overload RM drop", 60); print_stats(&m_d); }
        CHECK(e_c.horizon == 120);
        /* RM: 위 세 태스크(U = 0.833)는 하나도 안 놓친다 — 미스는 맨 아래 D 에만 */
        CHECK(m_c.st[0].missed == 0 && m_c.st[1].missed == 0 && m_c.st[2].missed == 0 && m_c.st[3].missed == 8);
        CHECK(m_d.st[0].missed == 0 && m_d.st[1].missed == 0 && m_d.st[2].missed == 0 && m_d.st[3].missed > 0);
        /* EDF continue: 모든 태스크가 미스 (도미노) */
        CHECK(e_c.st[0].missed > 0 && e_c.st[1].missed > 0 && e_c.st[2].missed > 0 && e_c.st[3].missed > 0);
        CHECK(e_c.misses == 35 && e_c.st[3].first_miss == 30 && e_c.st[0].first_miss == 32);
        CHECK(m_c.st[3].max_backlog > e_c.st[3].max_backlog);    /* RM continue: D 의 밀린 job 이 쌓인다 */
        CHECK(e_d.misses < e_c.misses);                          /* drop 은 도미노를 끊는다 */
        CHECK(m_c.st[3].max_backlog == 5 && m_d.st[3].max_backlog == 1);   /* drop 은 밀림을 1 job 으로 묶는다 */
        /* 같은 tick 에 두 job 이 동시에 미스 + drop (U = 2.25) — 지우면서 도는 루프의 함정 */
        const ptask_t tri[] = { { "p", 3, 4, 0, 0, 0, 0 }, { "q", 3, 4, 0, 0, 0, 0 }, { "s", 3, 4, 0, 0, 0, 0 } };
        sim_run(&e_d, tri, 3, POL_EDF, MISS_DROP, 0);
        CHECK(e_d.misses == 2 && e_d.st[1].dropped == 1 && e_d.st[2].dropped == 1 && e_d.missrow[4] == '*');
        sim_run(&e_d, tri, 3, POL_EDF, MISS_DROP, 12);
        CHECK(e_d.misses == 6 && e_d.busy_ticks == 12 && e_d.st[0].completed == 3);
    }

    /* ---- Part C: 모터 제어 펌웨어 (1 tick = 100 µs) ---- */
    ptask_t fw[] = { { "motor", 2, 10, 0, 0, 0, 0 },     /* FOC 전류 루프 1 kHz, 200 µs */
                     { "imu",   4, 20, 0, 0, 0, 0 },     /* IMU 500 Hz, SPI 읽기 + 필터 400 µs */
                     { "can",  12, 40, 0, 0, 0, 0 },     /* CAN/통신 250 Hz, 1.2 ms */
                     { "log",  26, 100, 0, 0, 0, 0 } };  /* 로깅 100 Hz, 2.6 ms — U = 0.96 */
    {
        static sim_t a, b, c, d, e;
        sim_run(&a, fw, 4, POL_RM, MISS_CONTINUE, 0);
        sim_run(&b, fw, 4, POL_EDF, MISS_CONTINUE, 0);
        printf("Part C  U=0.96 H=%u  RM: misses=%u (log max_resp=%u)  EDF: misses=%u (log max_resp=%u)  preempt RM=%u EDF=%u\n",
               a.horizon, a.misses, a.st[3].max_resp, b.misses, b.st[3].max_resp, a.preemptions, b.preemptions);
        CHECK(a.horizon == 200);
        CHECK(a.misses == 1 && a.st[3].first_miss == 100 && a.st[3].max_resp == 108);   /* RTA 고정점 108 과 같다 */
        CHECK(b.misses == 0);
        /* can 의 job 1 이 3 ms 더 걸린다 (재전송 폭주 등) — 일시 과부하 */
        fw[2].ovr_job = 1; fw[2].ovr_extra = 30;
        sim_run(&c, fw, 4, POL_RM, MISS_CONTINUE, 0);
        sim_run(&d, fw, 4, POL_EDF, MISS_CONTINUE, 0);
        sim_run(&e, fw, 4, POL_EDF, MISS_DROP, 0);
        printf("        can overrun +30: misses motor/imu/can/log  RM-cont %u/%u/%u/%u  EDF-cont %u/%u/%u/%u  EDF-drop %u/%u/%u/%u\n",
               c.st[0].missed, c.st[1].missed, c.st[2].missed, c.st[3].missed,
               d.st[0].missed, d.st[1].missed, d.st[2].missed, d.st[3].missed,
               e.st[0].missed, e.st[1].missed, e.st[2].missed, e.st[3].missed);
        if (verbose) { print_sim(&c, "fw overrun RM continue", 120); print_stats(&c);
                       print_sim(&d, "fw overrun EDF continue", 120); print_stats(&d);
                       print_sim(&e, "fw overrun EDF drop", 120); print_stats(&e); }
        CHECK(c.st[0].missed == 0 && c.st[1].missed == 0);       /* RM: 모터·IMU 는 보호된다 */
        CHECK(d.st[0].missed > 0);                               /* EDF continue: 모터가 놓친다 */
        CHECK(e.st[0].missed == 0 && e.st[2].dropped > 0);       /* EDF drop: 늦은 can job 을 버리면 모터는 산다 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
