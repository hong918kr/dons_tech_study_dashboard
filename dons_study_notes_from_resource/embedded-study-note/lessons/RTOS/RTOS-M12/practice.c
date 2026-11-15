/* RTOS-M12 추가 연습 — 한 번에 검증
 *
 * solution.c 의 시뮬레이터(완성본)를 그대로 복사하고 다음을 더했다:
 *  - POL_DM  : Deadline Monotonic (상대 데드라인이 짧을수록 높은 고정 우선순위)          — 연습 1
 *  - dbf_ok(): EDF 처리 요구량(processor demand) 검사 dbf(L) ≤ L, D ≤ T 세트           — 연습 2
 *  - POL_LLF : Least Laxity First (여유 = 절대 데드라인 - 현재 - 남은 일)              — 연습 3
 *  - first_idle + busy_period(): 동기 릴리즈의 첫 바쁜 구간 길이 L = Σ⌈L/Ti⌉Ci       — 연습 4
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

typedef enum { POL_RM, POL_EDF, POL_DM, POL_LLF } policy_t;   /* + 연습 1 DM, 연습 3 LLF */
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
    uint32_t first_empty;                 /* 연습 4: t > 0 에서 처음으로 밀린 job 이 0 개가 된 시각 (NONE = 없음) */
    char row[MAXT][MAXTL + 1];
    char missrow[MAXTL + 1];
} sim_t;

static uint32_t dl_of(const ptask_t *t) { return t->D ? t->D : t->T; }

/* ---------------- TODO 1: 하이퍼피리어드 ---------------- */
static uint64_t gcd_u64(uint64_t a, uint64_t b) { while (b) { uint64_t r = a % b; a = b; b = r; } return a; }
static uint32_t hyperperiod(const ptask_t *ts, int n) {
    uint64_t h = 1;
    for (int i = 0; i < n; i++) {
        if (ts[i].T == 0) return 0;
        h = h / gcd_u64(h, ts[i].T) * ts[i].T;      /* 먼저 나누고 곱한다: h, T ≤ 2^32 → 곱 < 2^64 */
        if (h > HYPER_MAX) return 0;
    }
    return (uint32_t)h;
}

/* ---------------- 주어진 코드: 초기화, 릴리즈, job 제거 ---------------- */
static void sim_init(sim_t *s, const ptask_t *ts, int n, policy_t pol, miss_policy_t mp) {
    memset(s, 0, sizeof *s);
    s->ts = ts; s->n = n; s->pol = pol; s->mp = mp;
    s->prev_task = -1; s->prev_seq = NONE;
    s->first_empty = NONE;
    for (int i = 0; i < n; i++) {          /* RM: 주기 오름차순 / DM: 상대 데드라인 오름차순, 같으면 번호 작은 쪽 */
        int r = 0;
        for (int j = 0; j < n; j++) {
            uint32_t kj = (pol == POL_DM) ? dl_of(&ts[j]) : ts[j].T, ki = (pol == POL_DM) ? dl_of(&ts[i]) : ts[i].T;
            if (kj < ki || (kj == ki && j < i)) r++;
        }
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
    if (s->pol == POL_LLF) {                           /* 연습 3: 여유 = dl - now - rem, 작은 쪽 */
        int64_t la = (int64_t)a->dl - s->now - a->rem, lb = (int64_t)b->dl - s->now - b->rem;
        if (la != lb) return la < lb;
        if (a->task != b->task) return a->task < b->task;
        return a->seq < b->seq;
    }
    if (s->pol == POL_RM || s->pol == POL_DM) {
        if (s->rank[a->task] != s->rank[b->task]) return s->rank[a->task] < s->rank[b->task];
        return a->seq < b->seq;
    }
    if (a->dl != b->dl) return a->dl < b->dl;         /* EDF: 이른 절대 데드라인 */
    if (a->task != b->task) return a->task < b->task; /* 동점 → 작은 태스크 번호 */
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
    for (int j = 0; j < s->nact; ) {
        job_t *jb = &s->act[j];
        if (jb->dl == s->now && !jb->missed) {         /* 남은 일이 있는 job 만 act 에 있다 */
            tstat_t *st = &s->st[jb->task];
            jb->missed = 1; st->missed++; s->misses++;
            if (st->first_miss == NONE) st->first_miss = s->now;
            note_miss(s, jb->task);
            if (s->mp == MISS_DROP) { st->dropped++; remove_job(s, j); continue; }
        }
        j++;
    }
}

/* ---------------- TODO 4: 선점 · 컨텍스트 스위치 집계 ---------------- */
static void account(sim_t *s, int j) {
    int task = (j >= 0) ? s->act[j].task : -1;
    uint32_t seq = (j >= 0) ? s->act[j].seq : NONE;
    if (s->prev_seq != NONE && seq != s->prev_seq) {   /* 직전 job 이 아직 살아 있나? */
        for (int k = 0; k < s->nact; k++)
            if (s->act[k].seq == s->prev_seq) { s->preemptions++; s->st[s->act[k].task].preempted++; break; }
    }
    if (task != s->prev_task) s->ctx_switches++;
    s->prev_task = task; s->prev_seq = seq;
}

/* ---------------- 주어진 코드: 1 tick 실행과 전체 루프 ---------------- */
static void sim_tick(sim_t *s) {
    on_deadlines(s);
    if (s->now > 0 && s->nact == 0 && s->first_empty == NONE) s->first_empty = s->now;   /* 릴리즈 전에 본다 */
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


/* ---------------- 연습 2: EDF 처리 요구량 검사 ---------------- */
/* dbf(L) = 길이 L 구간 [0, L] 안에 릴리즈되고 데드라인도 그 안에 있는 일의 합 (동기 릴리즈) */
static uint64_t dbf(const ptask_t *ts, int n, uint32_t L) {
    uint64_t d = 0;
    for (int i = 0; i < n; i++) {
        uint32_t D = dl_of(&ts[i]);
        if (L >= D) d += (uint64_t)((L - D) / ts[i].T + 1) * ts[i].C;
    }
    return d;
}
/* D ≤ T, 동기 릴리즈: 0 < L ≤ H 의 모든 L 에서 dbf(L) ≤ L 이면 EDF 로 스케줄 가능 (L = H 가 U ≤ 1 을 포함) */
static int dbf_ok(const ptask_t *ts, int n) {
    uint32_t H = hyperperiod(ts, n);
    for (uint32_t L = 1; L <= H; L++) if (dbf(ts, n, L) > L) return 0;
    return 1;
}
/* 밀도(density) 검사 Σ Ci/Di ≤ 1 — 충분조건일 뿐. 정수로: Σ Ci·(P/Di) ≤ P, P = Π Di */
static int density_ok(const ptask_t *ts, int n) {
    uint64_t P = 1, sum = 0;
    for (int i = 0; i < n; i++) P *= dl_of(&ts[i]);
    for (int i = 0; i < n; i++) sum += (uint64_t)ts[i].C * (P / dl_of(&ts[i]));
    return sum <= P;
}

/* ---------------- 연습 4: 동기 바쁜 구간 ---------------- */
static uint32_t busy_period(const ptask_t *ts, int n) {
    uint64_t L = 0, next = 0;
    for (int i = 0; i < n; i++) next += ts[i].C;
    while (next != L) {
        L = next; next = 0;
        for (int i = 0; i < n; i++) next += (L + ts[i].T - 1) / ts[i].T * ts[i].C;
        if (next > HYPER_MAX) return NONE;
    }
    return (uint32_t)L;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define ROW_IS(s, i, str) (strncmp((s)->row[i], (str), strlen(str)) == 0)
static uint32_t g_rng = 0x2026111Bu;
static uint32_t xorshift32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static uint32_t rnd(uint32_t lo, uint32_t hi) { return lo + xorshift32() % (hi - lo + 1); }
static const char *NAMES[MAXT] = { "a", "b", "c", "d", "e", "f", "g", "h" };
static const uint32_t PER[] = { 4, 5, 6, 8, 10, 12, 15, 20, 24, 30, 40, 60 };
/* 무작위 세트: constrained = 1 이면 C ≤ D ≤ T 로 D 도 뽑는다 */
static int random_set(ptask_t *ts, int constrained) {
    int n = (int)rnd(2, 5);
    for (int i = 0; i < n; i++) {
        uint32_t T = PER[rnd(0, 11)];
        uint32_t C = rnd(1, T / 2 > 1 ? T / 2 : 1);
        uint32_t D = constrained ? rnd(C, T) : 0;
        ts[i] = (ptask_t){ NAMES[i], C, T, D, 0, 0, 0 };
    }
    return n;
}

int main(int argc, char **argv) {
    int verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    static sim_t s, r;
    ptask_t ts[MAXT];

    /* ---- 연습 1: D < T 에서 RM vs DM ---- */
    {
        const ptask_t dm[] = { { "ctrl", 2, 4, 0, 0, 0, 0 },      /* 제어 루프 2.5 kHz (1 tick = 100 µs) */
                               { "adc",  1, 10, 2, 0, 0, 0 } };   /* ADC 결과를 200 µs 안에 읽어야 함 */
        sim_run(&s, dm, 2, POL_RM, MISS_CONTINUE, 0);
        sim_run(&r, dm, 2, POL_DM, MISS_CONTINUE, 0);
        printf("ex1 RM: misses=%u (adc first miss t=%u)   DM: misses=%u, adc max_resp=%u ctrl max_resp=%u\n",
               s.misses, s.st[1].first_miss, r.misses, r.st[1].max_resp, r.st[0].max_resp);
        if (verbose) { print_sim(&s, "ex1 RM", 20); print_stats(&s); print_sim(&r, "ex1 DM", 20); print_stats(&r); }
        CHECK(s.st[1].first_miss == 2 && s.st[1].missed == 1 && s.st[1].max_resp == 3);
        CHECK(r.misses == 0 && r.st[1].max_resp == 1 && r.st[0].max_resp == 3);
        int rm_ok_dm_bad = 0, rm_bad_dm_ok = 0;
        for (int it = 0; it < 300; it++) {
            int n = random_set(ts, 1);
            sim_run(&s, ts, n, POL_RM, MISS_CONTINUE, 0);
            sim_run(&r, ts, n, POL_DM, MISS_CONTINUE, 0);
            rm_ok_dm_bad += (s.misses == 0 && r.misses != 0);
            rm_bad_dm_ok += (s.misses != 0 && r.misses == 0);
        }
        printf("ex1 random D<=T: RM ok but DM misses=%d, RM misses but DM ok=%d\n", rm_ok_dm_bad, rm_bad_dm_ok);
        CHECK(rm_ok_dm_bad == 0);          /* Leung & Whitehead 1982: DM 는 고정 우선순위 중 최적 */
        CHECK(rm_bad_dm_ok > 0);
    }

    /* ---- 연습 2: EDF + D < T — 처리 요구량 검사 vs 시뮬레이션 ---- */
    {
        int agree = 0, total = 0, dens_pess = 0, dens_wrong = 0, infeasible = 0;
        for (int it = 0; it < 300; it++) {
            int n = random_set(ts, 1);
            sim_run(&r, ts, n, POL_EDF, MISS_CONTINUE, 0);
            int ok = dbf_ok(ts, n);
            total++; agree += (ok == (r.misses == 0));
            infeasible += !ok;
            if (ok && !density_ok(ts, n)) dens_pess++;
            if (!ok && density_ok(ts, n)) dens_wrong++;
        }
        const ptask_t ex[] = { { "a", 2, 6, 3, 0, 0, 0 }, { "b", 3, 8, 5, 0, 0, 0 } };   /* 밀도 2/3 + 3/5 = 1.27 */
        printf("ex2 dbf vs EDF sim agree %d/%d (infeasible %d), density test too pessimistic in %d, unsafe %d\n",
               agree, total, infeasible, dens_pess, dens_wrong);
        CHECK(agree == total && infeasible > 20);
        CHECK(dens_wrong == 0 && dens_pess > 0);
        sim_run(&r, ex, 2, POL_EDF, MISS_CONTINUE, 0);
        printf("ex2 a(2,6,3) b(3,8,5): dbf(3)=%llu dbf(5)=%llu dbf(9)=%llu dbf(13)=%llu  density_ok=%d dbf_ok=%d EDF misses=%u\n",
               (unsigned long long)dbf(ex, 2, 3), (unsigned long long)dbf(ex, 2, 5), (unsigned long long)dbf(ex, 2, 9),
               (unsigned long long)dbf(ex, 2, 13), density_ok(ex, 2), dbf_ok(ex, 2), r.misses);
        CHECK(dbf(ex, 2, 3) == 2 && dbf(ex, 2, 5) == 5 && dbf(ex, 2, 9) == 7 && dbf(ex, 2, 13) == 10);
        CHECK(!density_ok(ex, 2) && dbf_ok(ex, 2) && r.misses == 0);
    }

    /* ---- 연습 3: LLF — 같은 결과, 더 많은 스위치 ---- */
    {
        const ptask_t two[] = { { "t1", 2, 5, 0, 0, 0, 0 }, { "t2", 4, 7, 0, 0, 0, 0 } };
        sim_run(&s, two, 2, POL_EDF, MISS_CONTINUE, 0);
        sim_run(&r, two, 2, POL_LLF, MISS_CONTINUE, 0);
        printf("ex3 two-task  EDF: misses=%u ctx=%u preempt=%u   LLF: misses=%u ctx=%u preempt=%u\n",
               s.misses, s.ctx_switches, s.preemptions, r.misses, r.ctx_switches, r.preemptions);
        if (verbose) { print_sim(&s, "ex3 EDF", 35); print_sim(&r, "ex3 LLF", 35); }
        CHECK(s.misses == 0 && r.misses == 0 && s.ctx_switches == 15 && r.ctx_switches == 23 && r.preemptions == 10);
        uint64_t cs_e = 0, cs_l = 0; int feas = 0, llf_miss = 0;
        for (int it = 0; it < 300; it++) {
            int n = random_set(ts, 0);
            sim_run(&s, ts, n, POL_EDF, MISS_CONTINUE, 0);
            if (s.misses) continue;                       /* U ≤ 1 세트만 */
            sim_run(&r, ts, n, POL_LLF, MISS_CONTINUE, 0);
            feas++; cs_e += s.ctx_switches; cs_l += r.ctx_switches; llf_miss += (r.misses != 0);
        }
        printf("ex3 random U<=1 sets=%d  ctx switches EDF=%llu LLF=%llu  LLF sets with a miss=%d\n",
               feas, (unsigned long long)cs_e, (unsigned long long)cs_l, llf_miss);
        CHECK(cs_l > cs_e && llf_miss == 0);
    }

    /* ---- 연습 4: 동기 바쁜 구간 = 첫 idle 시각 ---- */
    {
        int agree = 0, total = 0;
        for (int it = 0; it < 300; it++) {
            int n = random_set(ts, 0);
            sim_run(&s, ts, n, POL_EDF, MISS_CONTINUE, 0);
            if (s.misses) continue;                            /* U ≤ 1 세트만 */
            sim_run(&r, ts, n, POL_RM, MISS_CONTINUE, 0);      /* 작업 보존: 정책과 무관하게 같은 L */
            uint32_t L = busy_period(ts, n);
            uint32_t e1 = (s.first_empty == NONE) ? s.horizon : s.first_empty;
            uint32_t e2 = (r.first_empty == NONE) ? r.horizon : r.first_empty;
            total++; agree += (L == e1 && L == e2);
        }
        const ptask_t two[] = { { "t1", 2, 5, 0, 0, 0, 0 }, { "t2", 4, 7, 0, 0, 0, 0 } };
        sim_run(&s, two, 2, POL_EDF, MISS_CONTINUE, 0);
        printf("ex4 busy period L == first empty instant (EDF and RM) in %d/%d sets; two-task L=%u first_empty=%u first idle tick=34\n",
               agree, total, busy_period(two, 2), s.first_empty);
        CHECK(agree == total && total > 100);
        CHECK(busy_period(two, 2) == 14 && s.first_empty == 14 && s.row[0][34] == '.' && s.row[1][34] == '.');
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
