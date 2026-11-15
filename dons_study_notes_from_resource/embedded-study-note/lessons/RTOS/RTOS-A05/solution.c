/* RTOS-A05 비주기 작업 서버: polling server vs sporadic server
 *
 * 호스트 모델: OS 스레드 1개로 도는 "tick 단위 결정적 고정 우선순위 스케줄 시뮬레이터" (단일 코어, 선점형).
 *  - 주기 태스크 i = (C, T, O): job k 는 O + kT 에 릴리즈, 데드라인 = 릴리즈 + T (D = T).
 *    우선순위 = Rate Monotonic (주기가 짧을수록 높음). 서버도 주기 Ts 로 같은 줄에 선다 (동률이면 서버가 위).
 *  - 비주기 요청 = (도착 시각, 실행량). 하나의 FIFO 큐로 도착 순서대로 처리한다 (서버 1개).
 *  - 정책:
 *      BG : background — 주기 태스크가 하나도 준비되지 않은 tick 에만 비주기 요청을 처리 (idle 우선순위).
 *      PS : polling server (Cs, Ts) — k*Ts 활성화 순간 큐가 비어 있으면 그 주기 예산은 0 (잃는다).
 *           처리 도중 큐가 비면 남은 예산도 버린다.
 *      DS : deferrable server — k*Ts 마다 예산을 Cs 로 채우고 주기 안에서는 아껴 둔다 (요청 오면 바로 처리).
 *      SS : sporadic server (Sprunt, Sha, Lehoczky 1989) — 쓴 만큼만, "활성화 시각 + Ts" 에 되돌려 준다.
 *           활성화(chunk 시작) = 예산 > 0 이고 일이 있어 서버가 준비 상태가 되는 순간.
 *           chunk 끝 = 예산 소진 / 큐 비움 / 도중에 보충이 도착 (→ 닫고 그 시각에 새 chunk).
 *  - tick t 의 순서: (1) 데드라인 검사 (2) 주기 릴리즈 (3) 비주기 도착 (4) srv_tick_begin
 *                    (5) sched_pick (6) 1 tick 실행 (+ srv_consume) (7) srv_tick_end (8) t++
 *  - 타임라인: 주기 태스크 R=실행 r=준비인데 못 돎 .=없음 / 서버 줄 R=실행 r=준비(예산·일 있음)인데 못 돎
 *              w=일은 있는데 예산 없음 .=일 없음 / budget 줄 = 그 tick 의 예산 (BG 는 '-').
 *
 * 이 레슨에서 채우는 것 (TODO 1~5):
 *  1. sched_pick()         : 고정 우선순위 선택 + 서버 준비 조건 + background
 *  2. PS 규칙              : 활성화 시 예산, 큐가 비면 예산 폐기
 *  3. SS 규칙              : chunk 시작/끝, 보충 (활성화 시각 + Ts, 쓴 양) 예약과 도착
 *  4. DS 규칙              : 매 주기 Cs 로 채우기
 *  5. server_max_budget()  : RTA 로 주기 태스크를 깨지 않는 최대 Cs (DS 는 지터 J = Ts - Cs 로 분석)
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter [-v | sweep]   (solution.c 도 같다)
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXP   6        /* 주기 태스크 최대 수 */
#define MAXAQ  4096     /* 한 실행의 비주기 요청 최대 수 */
#define MAXJ   8        /* 태스크당 밀린 job 최대 수 */
#define MAXREPL 64      /* SS 보충 대기열 */
#define MAXTL  80       /* 타임라인으로 기록하는 tick 수 */
#define MAXLOG 32       /* SS 보충 기록 (테스트용) */

typedef struct { const char *name; uint32_t C, T, O; } ptask_t;
typedef struct { uint32_t arr, C; } areq_t;
typedef enum { SRV_BG, SRV_PS, SRV_DS, SRV_SS } srv_kind_t;
typedef struct { srv_kind_t kind; uint32_t Cs, Ts; } srv_cfg_t;
typedef struct { uint32_t due, amt; } repl_t;
typedef struct { uint32_t start, due, amt; } repl_log_t;

typedef struct {
    /* 설정 */
    const ptask_t *ts; int n; srv_cfg_t sc; const areq_t *aq; int na;
    int rank[MAXP + 1];                  /* 0 = 가장 높음. 인덱스 n = 서버 */
    uint32_t now;
    /* 주기 태스크 상태: 밀린 job 들의 릴리즈 시각 링 + 맨 앞 job 의 남은 양 */
    uint32_t jrel[MAXP][MAXJ]; int jh[MAXP], jn[MAXP]; uint32_t jrem[MAXP];
    /* 비주기 큐: [qhead, narr) 가 도착했지만 안 끝난 요청, arem = 맨 앞 요청의 남은 양 */
    int qhead, narr; uint32_t arem;
    /* 서버 상태 */
    uint32_t budget;
    int in_chunk; uint32_t chunk_start, chunk_used;      /* SS */
    repl_t repl[MAXREPL]; int rh, rn;                      /* SS 보충 대기열 (due 오름차순) */
    repl_log_t rlog[MAXLOG]; int nrlog;
    /* 통계 */
    uint32_t pmiss[MAXP], pmaxresp[MAXP], misses, jovf;
    uint32_t a_done, a_maxresp, srv_ticks, inv_bad;
    uint64_t a_sum;
    int last;                                              /* 직전 tick 에 돈 것 (-1 idle, n = 서버) */
    char row[MAXP + 1][MAXTL + 1], brow[MAXTL + 1];
} sim_t;

static int q_nonempty(const sim_t *s) { return s->qhead < s->narr; }
static int srv_ready(const sim_t *s) { return s->sc.kind != SRV_BG && s->budget > 0 && q_nonempty(s); }

/* RM 순위: (주기, 서버 먼저, 인덱스) 사전식. 주어진 코드. */
static void assign_rm(sim_t *s) {
    int n = s->n;
    for (int i = 0; i <= n; i++) {
        uint32_t Ti = (i == n) ? s->sc.Ts : s->ts[i].T;
        int r = 0;
        for (int j = 0; j <= n; j++) {
            if (j == i) continue;
            uint32_t Tj = (j == n) ? s->sc.Ts : s->ts[j].T;
            int jfirst = Tj < Ti || (Tj == Ti && (j == n || (i != n && j < i)));
            if (s->sc.kind == SRV_BG && j == n) jfirst = 0;    /* BG: 서버는 맨 아래 */
            if (s->sc.kind == SRV_BG && i == n) jfirst = 1;
            r += jfirst;
        }
        s->rank[i] = r;
    }
}

/* ---------------- TODO 1: 고정 우선순위 선택 ---------------- */
/* 반환: -1 = idle, 0..n-1 = 주기 태스크, n = 서버(또는 BG 처리).
 * 주기 태스크는 밀린 job 이 있으면 준비. 서버는 srv_ready() 일 때만 rank[n] 으로 경쟁.
 * BG 는 주기 태스크가 하나도 준비되지 않았고 큐에 일이 있을 때만. */
static int sched_pick(const sim_t *s) {
    int best = -1;
    for (int i = 0; i < s->n; i++)
        if (s->jn[i] > 0 && (best < 0 || s->rank[i] < s->rank[best])) best = i;
    if (s->sc.kind == SRV_BG) {
        if (best < 0 && q_nonempty(s)) best = s->n;
    } else if (srv_ready(s) && (best < 0 || s->rank[s->n] < s->rank[best])) {
        best = s->n;
    }
    return best;
}

/* ---------------- TODO 2~4: 서버 규칙 ---------------- */
static void ss_close_chunk(sim_t *s) {
    /* SS: 열린 chunk 를 닫는다. 쓴 양이 있으면 (chunk_start + Ts, chunk_used) 보충을 예약. */
    if (!s->in_chunk) return;
    if (s->chunk_used > 0 && s->rn < MAXREPL) {
        int k = (s->rh + s->rn) % MAXREPL;
        s->repl[k].due = s->chunk_start + s->sc.Ts;
        s->repl[k].amt = s->chunk_used;
        s->rn++;
        if (s->nrlog < MAXLOG) {
            s->rlog[s->nrlog].start = s->chunk_start; s->rlog[s->nrlog].due = s->chunk_start + s->sc.Ts;
            s->rlog[s->nrlog].amt = s->chunk_used; s->nrlog++;
        }
    }
    s->in_chunk = 0; s->chunk_used = 0;
}

/* (4) tick 시작: 활성화 / 보충 / chunk 열기 */
static void srv_tick_begin(sim_t *s) {
    uint32_t t = s->now;
    switch (s->sc.kind) {
    case SRV_PS:   /* TODO 2a: k*Ts 에 큐가 비어 있지 않으면 Cs, 비어 있으면 0 (그 주기 예산을 잃는다) */
        if (t % s->sc.Ts == 0) s->budget = q_nonempty(s) ? s->sc.Cs : 0;
        break;
    case SRV_DS:   /* TODO 4: k*Ts 에 Cs 로 채운다 (쌓이지 않음) */
        if (t % s->sc.Ts == 0) s->budget = s->sc.Cs;
        break;
    case SRV_SS: { /* TODO 3a: due == t 인 보충을 더하고, chunk 도중 보충이 오면 닫았다가 다시 연다 */
        int got = 0;
        while (s->rn > 0 && s->repl[s->rh].due == t) {
            s->budget += s->repl[s->rh].amt;
            s->rh = (s->rh + 1) % MAXREPL; s->rn--; got = 1;
        }
        if (got && s->in_chunk) ss_close_chunk(s);
        /* TODO 3b: 준비 상태가 되는 순간 = 활성화. 그 시각이 보충 기준 */
        if (!s->in_chunk && s->budget > 0 && q_nonempty(s)) {
            s->in_chunk = 1; s->chunk_start = t; s->chunk_used = 0;
        }
        break;
    }
    case SRV_BG: break;
    }
}

/* (6) 서버가 1 tick 실행했다 */
static void srv_consume(sim_t *s) {
    if (s->sc.kind == SRV_BG) return;
    s->budget--;                                  /* 주어진 코드: 예산 1 감소 */
    if (s->sc.kind == SRV_SS) s->chunk_used++;    /* TODO 3c: chunk 에서 쓴 양 */
}

/* (7) tick 끝 */
static void srv_tick_end(sim_t *s) {
    switch (s->sc.kind) {
    case SRV_PS:   /* TODO 2b: 큐가 비면 남은 예산은 버린다 (서버는 다음 주기까지 쉰다) */
        if (!q_nonempty(s)) s->budget = 0;
        break;
    case SRV_SS:   /* TODO 3d: 예산 소진 또는 큐 비움 → chunk 닫기 */
        if (s->in_chunk && (s->budget == 0 || !q_nonempty(s))) ss_close_chunk(s);
        break;
    default: break;
    }
}

/* ---------------- 시뮬레이터 본체 (주어진 코드) ---------------- */
static void sim_init(sim_t *s, const ptask_t *ts, int n, srv_cfg_t sc, const areq_t *aq, int na) {
    memset(s, 0, sizeof *s);
    s->ts = ts; s->n = n; s->sc = sc; s->aq = aq; s->na = na; s->last = -1;
    if (sc.kind == SRV_DS || sc.kind == SRV_SS) s->budget = sc.Cs;   /* 시작할 때 가득 */
    assign_rm(s);
    for (int i = 0; i <= n; i++) { memset(s->row[i], ' ', MAXTL); s->row[i][MAXTL] = 0; }
    memset(s->brow, ' ', MAXTL); s->brow[MAXTL] = 0;
}

static void sim_step(sim_t *s) {
    uint32_t t = s->now; int n = s->n;
    /* (1) 데드라인 검사: 데드라인 == t 인데 남아 있는 job (job 은 계속 실행한다) */
    for (int i = 0; i < n; i++)
        for (int k = 0; k < s->jn[i]; k++)
            if (s->jrel[i][(s->jh[i] + k) % MAXJ] + s->ts[i].T == t) { s->pmiss[i]++; s->misses++; }
    /* (2) 주기 릴리즈 */
    for (int i = 0; i < n; i++) {
        const ptask_t *p = &s->ts[i];
        if (t >= p->O && (t - p->O) % p->T == 0) {
            if (s->jn[i] == MAXJ) { s->jovf++; continue; }
            if (s->jn[i] == 0) s->jrem[i] = p->C;
            s->jrel[i][(s->jh[i] + s->jn[i]) % MAXJ] = t; s->jn[i]++;
        }
    }
    /* (3) 비주기 도착 */
    while (s->narr < s->na && s->aq[s->narr].arr == t) {
        if (s->qhead == s->narr) s->arem = s->aq[s->narr].C;
        s->narr++;
    }
    /* (4) 서버 활성화·보충 */
    srv_tick_begin(s);
    uint32_t b_show = s->budget;
    /* (5) 선택 */
    int who = sched_pick(s);
    /* 타임라인 */
    if (t < MAXTL) {
        for (int i = 0; i < n; i++) s->row[i][t] = (who == i) ? 'R' : (s->jn[i] > 0 ? 'r' : '.');
        char c = '.';
        if (who == n) c = 'R';
        else if (q_nonempty(s)) c = (s->sc.kind == SRV_BG || srv_ready(s)) ? (srv_ready(s) ? 'r' : 'w') : 'w';
        s->row[n][t] = c;
        s->brow[t] = s->sc.kind == SRV_BG ? '-' : (char)(b_show < 10 ? '0' + b_show : '+');
    }
    /* (6) 1 tick 실행 */
    if (who >= 0 && who < n) {
        if (--s->jrem[who] == 0) {
            uint32_t rel = s->jrel[who][s->jh[who]];
            uint32_t resp = t + 1 - rel;
            if (resp > s->pmaxresp[who]) s->pmaxresp[who] = resp;
            s->jh[who] = (s->jh[who] + 1) % MAXJ; s->jn[who]--;
            if (s->jn[who] > 0) s->jrem[who] = s->ts[who].C;
        }
    } else if (who == n) {
        s->srv_ticks++;
        srv_consume(s);
        if (--s->arem == 0) {
            uint32_t resp = t + 1 - s->aq[s->qhead].arr;
            s->a_sum += resp; s->a_done++;
            if (resp > s->a_maxresp) s->a_maxresp = resp;
            s->qhead++;
            if (s->qhead < s->narr) s->arem = s->aq[s->qhead].C;
        }
    }
    s->last = who;
    /* (7) */
    srv_tick_end(s);
    /* 불변식 (SS): 예산 + 대기 중 보충 + 열린 chunk 사용량 == Cs */
    if (s->sc.kind == SRV_SS) {
        uint32_t tot = s->budget + (s->in_chunk ? s->chunk_used : 0);
        for (int k = 0; k < s->rn; k++) tot += s->repl[(s->rh + k) % MAXREPL].amt;
        if (tot != s->sc.Cs) s->inv_bad++;
    }
    if (s->budget > s->sc.Cs && s->sc.kind != SRV_BG) s->inv_bad++;
    s->now++;
}

static void sim_run(sim_t *s, const ptask_t *ts, int n, srv_cfg_t sc, const areq_t *aq, int na, uint32_t H) {
    sim_init(s, ts, n, sc, aq, na);
    for (uint32_t k = 0; k < H; k++) sim_step(s);
}

/* ---------------- RTA (RTOS-M11 식 + 지터, 주어진 코드) ---------------- */
typedef struct { uint32_t C, T, J; } ent_t;
/* e[0..k-1] 는 우선순위 내림차순. e[i] 의 최악 응답 R = w + J_i, w = C_i + Σ_{j<i} ⌈(w + J_j)/T_j⌉ C_j.
 * R > T_i (D = T) 면 그 값을 그대로 돌려준다 (= 실패). */
static uint32_t rta_resp(const ent_t *e, int i) {
    uint64_t w = e[i].C, prev = 0;
    while (w != prev) {
        prev = w; w = e[i].C;
        for (int j = 0; j < i; j++) w += (prev + e[j].J + e[j].T - 1) / e[j].T * e[j].C;
        if (w + e[i].J > e[i].T) break;
    }
    return (uint32_t)(w + e[i].J);
}
/* 주기 태스크 + 서버(Cs, Ts, J) 를 RM 순서로 세운 뒤, 주기 태스크 i 의 R 을 out[i] 에. 모두 R ≤ T 면 1. */
static int rta_with_server(const ptask_t *ts, int n, uint32_t Cs, uint32_t Ts, uint32_t Js, uint32_t *out) {
    ent_t e[MAXP + 1]; int id[MAXP + 1], k = 0, ok = 1;
    sim_t tmp; tmp.ts = ts; tmp.n = n; tmp.sc.kind = SRV_PS; tmp.sc.Ts = Ts; tmp.sc.Cs = Cs;
    assign_rm(&tmp);
    for (int r = 0; r <= n; r++)
        for (int i = 0; i <= n; i++)
            if (tmp.rank[i] == r) {
                if (i == n) { if (Cs == 0) continue; e[k].C = Cs; e[k].T = Ts; e[k].J = Js; }
                else { e[k].C = ts[i].C; e[k].T = ts[i].T; e[k].J = 0; }
                id[k++] = i;
            }
    for (int x = 0; x < k; x++) {
        if (id[x] == n) continue;
        uint32_t R = rta_resp(e, x);
        if (out) out[id[x]] = R;
        if (R > e[x].T) ok = 0;
    }
    return ok;
}

/* ---------------- TODO 5: RM 을 깨지 않는 최대 서버 예산 ---------------- */
/* Cs = Ts, Ts-1, ..., 1 중 주기 태스크가 모두 RTA 를 통과하는 가장 큰 값. 없으면 0.
 * ds_jitter = 1 이면 서버를 지터 J = Ts - Cs 인 주기 태스크로 본다 (DS 의 back-to-back 실행). */
static uint32_t server_max_budget(const ptask_t *ts, int n, uint32_t Ts, int ds_jitter) {
    for (uint32_t Cs = Ts; Cs >= 1; Cs--)
        if (rta_with_server(ts, n, Cs, Ts, ds_jitter ? Ts - Cs : 0, NULL)) return Cs;
    return 0;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define ROW_IS(s, i, str) (strncmp((s)->row[i], (str), strlen(str)) == 0)

static uint32_t g_rng;
static uint32_t xorshift32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static uint32_t rnd(uint32_t lo, uint32_t hi) { return lo + xorshift32() % (hi - lo + 1); }

/* 비주기 요청 생성: 도착 간격 U[1, 2*gap-1], 실행량 U[cmin, cmax], [0, H) 안에서. */
static int gen_aperiodic(areq_t *aq, uint32_t seed, uint32_t gap, uint32_t cmin, uint32_t cmax, uint32_t H) {
    g_rng = seed ? seed : 1u; int k = 0; uint32_t t = rnd(0, gap);
    while (t < H && k < MAXAQ) { aq[k].arr = t; aq[k].C = rnd(cmin, cmax); k++; t += rnd(1, 2 * gap - 1); }
    return k;
}

static const char *KN[] = { "BG", "PS", "DS", "SS" };
static void print_sim(const sim_t *s, const char *title, uint32_t upto) {
    printf("-- %s  (Cs=%u Ts=%u)\n", title, s->sc.Cs, s->sc.Ts);
    printf("tick   : "); for (uint32_t t = 0; t < upto; t++) putchar('0' + (char)(t % 10)); putchar('\n');
    for (int i = 0; i < s->n; i++) printf("%-6s : %.*s\n", s->ts[i].name, (int)upto, s->row[i]);
    printf("%-6s : %.*s\n", KN[s->sc.kind], (int)upto, s->row[s->n]);
    printf("budget : %.*s\n", (int)upto, s->brow);
}
static void print_resp(const sim_t *s, const char *tag) {
    printf("%s: aperiodic done=%u avg=%.2f max=%u | periodic misses=%u maxresp", tag, s->a_done,
           s->a_done ? (double)s->a_sum / s->a_done : 0.0, s->a_maxresp, s->misses);
    for (int i = 0; i < s->n; i++) printf(" %s=%u", s->ts[i].name, s->pmaxresp[i]);
    printf("\n");
}

int main(int argc, char **argv) {
    int verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    int sweep = argc > 1 && strcmp(argv[1], "sweep") == 0;
    static sim_t s;
    static areq_t aq[MAXAQ];

    /* ===== Part A: 선택 규칙 + 작은 예제 — background vs polling server ===== */
    /* hi(1,5) lo(2,10) + 서버 (Cs=2, Ts=4) → 서버가 가장 높은 우선순위. U = 0.2 + 0.2 + 0.5 = 0.9 (RTA 통과).
     * 요청: t=1 에 2, t=9 에 1, t=13 에 3 */
    const ptask_t A[] = { { "hi", 1, 5, 0 }, { "lo", 2, 10, 0 } };
    const areq_t AQ[] = { { 1, 2 }, { 9, 1 }, { 13, 3 } };
    {
        srv_cfg_t ps = { SRV_PS, 2, 4 }, bg = { SRV_BG, 0, 4 };
        sim_init(&s, A, 2, ps, AQ, 3);
        CHECK(s.rank[2] == 0 && s.rank[0] == 1 && s.rank[1] == 2);   /* 주어진 assign_rm: 서버 Ts=4 가 맨 위 */
        CHECK(sched_pick(&s) == -1);                                   /* 아무도 준비 안 됨 */
        s.jn[1] = 1; s.jrem[1] = 2;
        CHECK(sched_pick(&s) == 1);                                    /* lo 만 준비 */
        s.jn[0] = 1; s.jrem[0] = 1;
        CHECK(sched_pick(&s) == 0);                                    /* hi > lo */
        s.narr = 1; s.arem = 2; s.budget = 0;
        CHECK(sched_pick(&s) == 0);                                    /* 일은 있는데 예산 0 → 서버 아님 */
        s.budget = 2;
        CHECK(sched_pick(&s) == 2);                                    /* 예산·일 있음 → 서버(rank 0) */
        sim_init(&s, A, 2, bg, AQ, 3);
        s.narr = 1; s.arem = 2; s.jn[1] = 1; s.jrem[1] = 2;
        CHECK(sched_pick(&s) == 1);                                    /* BG: 주기 태스크가 있으면 못 돈다 */
        s.jn[1] = 0;
        CHECK(sched_pick(&s) == 2);                                    /* BG: 아무도 없을 때만 */

        sim_run(&s, A, 2, bg, AQ, 3, 40);
        if (verbose) print_sim(&s, "A background", 24);
        print_resp(&s, "A  BG");
        CHECK(s.a_done == 3 && s.misses == 0);
        CHECK(s.a_sum == 4 + 1 + 4 && s.a_maxresp == 4);
        CHECK(ROW_IS(&s, 2, ".wwRR....R...RRwR...."));
        sim_run(&s, A, 2, ps, AQ, 3, 40);
        if (verbose) print_sim(&s, "A polling server", 24);
        print_resp(&s, "A  PS");
        CHECK(s.a_done == 3 && s.misses == 0);
        CHECK(s.a_sum == 5 + 4 + 8 && s.a_maxresp == 8);              /* t=1 요청은 t=4 활성화까지 기다린다 */
        CHECK(ROW_IS(&s, 2, ".wwwRR...wwwRwwwRRwwR...."));
        CHECK(strncmp(s.brow, "000021000000200021002000", 24) == 0);  /* t=0 큐 비어 0, t=4 에 2, t=12 끝에 폐기 */
    }

    /* ===== Part B: sporadic server 보충 규칙 + deferrable server 반례 ===== */
    {
        srv_cfg_t ss = { SRV_SS, 2, 4 }, ds = { SRV_DS, 2, 4 };
        sim_run(&s, A, 2, ss, AQ, 3, 40);
        if (verbose) print_sim(&s, "B sporadic server", 24);
        print_resp(&s, "B  SS");
        CHECK(s.a_done == 3 && s.misses == 0 && s.inv_bad == 0);
        CHECK(s.a_sum == 2 + 1 + 5);
        CHECK(ROW_IS(&s, 2, ".RR......R...RRwwR......"));
        CHECK(strncmp(s.brow, "221002222211121002111222", 24) == 0);
        CHECK(s.nrlog == 4);
        CHECK(s.nrlog >= 4 && s.rlog[0].start == 1 && s.rlog[0].due == 5 && s.rlog[0].amt == 2);
        CHECK(s.nrlog >= 4 && s.rlog[1].start == 9 && s.rlog[1].due == 13 && s.rlog[1].amt == 1);
        CHECK(s.nrlog >= 4 && s.rlog[2].start == 13 && s.rlog[2].due == 17 && s.rlog[2].amt == 2);
        CHECK(s.nrlog >= 4 && s.rlog[3].start == 17 && s.rlog[3].due == 21 && s.rlog[3].amt == 1);
        sim_run(&s, A, 2, ds, AQ, 3, 40);
        print_resp(&s, "B  DS");
        CHECK(s.a_sum == 2 + 1 + 4 && s.misses == 0);

        /* 보충이 chunk 도중에 온다: mid(3,6,O=1) 가 서버 (2,8) 보다 높다 */
        const ptask_t M[] = { { "mid", 3, 6, 1 }, { "low", 1, 24, 0 } };
        const areq_t MQ[] = { { 0, 1 }, { 7, 3 } };
        srv_cfg_t ss2 = { SRV_SS, 2, 8 };
        sim_run(&s, M, 2, ss2, MQ, 2, 30);
        if (verbose) print_sim(&s, "B SS replenish mid-chunk", 24);
        CHECK(s.inv_bad == 0 && s.misses == 0 && s.a_done == 2);
        CHECK(s.nrlog >= 3 && s.rlog[0].start == 0 && s.rlog[0].due == 8 && s.rlog[0].amt == 1);
        CHECK(s.nrlog >= 3 && s.rlog[1].start == 8 && s.rlog[1].due == 16 && s.rlog[1].amt == 2);  /* 7 이 아니라 8 */
        CHECK(s.nrlog >= 3 && s.rlog[2].start == 16 && s.rlog[2].due == 24 && s.rlog[2].amt == 1);

        /* 함정 워크로드: 보충 시각을 chunk 처음 시각으로 잡으면 lo 가 놓친다 (practice.c 연습 1) */
        const ptask_t TR[] = { { "hi", 2, 7, 5 }, { "lo", 2, 20, 6 } };
        const areq_t TQ[] = { { 2, 4 }, { 5, 10 }, { 25, 4 }, { 27, 13 } };
        srv_cfg_t ss3 = { SRV_SS, 6, 10 };
        uint32_t R[MAXP];
        CHECK(rta_with_server(TR, 2, 6, 10, 0, R) == 1 && R[1] == 20);
        sim_run(&s, TR, 2, ss3, TQ, 4, 80);
        if (verbose) print_sim(&s, "B SS trap workload", 60);
        print_resp(&s, "B  SS(6,10) trap");
        CHECK(s.misses == 0 && s.pmaxresp[1] <= 20 && s.inv_bad == 0);

        /* DS 반례: 서버를 주기 (2,5) 로 보면 RTA 통과, 실제로는 back-to-back 4 tick → lo 미스 */
        const ptask_t D[] = { { "lo", 4, 8, 3 } };
        const areq_t DQ[] = { { 3, 6 } };
        CHECK(rta_with_server(D, 1, 2, 5, 0, R) == 1 && R[0] == 8);          /* 순진한 분석: OK */
        CHECK(rta_with_server(D, 1, 2, 5, 3, R) == 0 && R[0] == 10);         /* 지터 3: 실패, R = 10 */
        srv_cfg_t dsx = { SRV_DS, 2, 5 }, psx = { SRV_PS, 2, 5 }, ssx = { SRV_SS, 2, 5 };
        sim_run(&s, D, 1, dsx, DQ, 1, 40);
        if (verbose) print_sim(&s, "B DS back-to-back", 24);
        print_resp(&s, "B  DS(2,5) adversarial");
        CHECK(s.misses == 1 && s.pmaxresp[0] == 10);                       /* 관측 = 지터 RTA 값 */
        sim_run(&s, D, 1, psx, DQ, 1, 40);
        if (verbose) print_sim(&s, "B PS same workload", 24);
        print_resp(&s, "B  PS(2,5) adversarial");
        CHECK(s.misses == 0 && s.pmaxresp[0] <= 8);
        sim_run(&s, D, 1, ssx, DQ, 1, 40);
        if (verbose) print_sim(&s, "B SS same workload", 24);
        print_resp(&s, "B  SS(2,5) adversarial");
        CHECK(s.misses == 0 && s.pmaxresp[0] <= 8 && s.inv_bad == 0);
    }

    /* ===== Part C: 모터 컨트롤러 (tick = 100 us) + UART/CAN 명령 ===== */
    /* speed 1 kHz 200 us, imu 500 Hz 400 us, log 100 Hz 3 ms (부팅 후 0.9 ms 에 시작 = O 9) */
    const ptask_t FW[] = { { "speed", 2, 10, 0 }, { "imu", 4, 20, 0 }, { "log", 30, 100, 9 } };
    const uint32_t TS = 10, H = 20000;
    uint32_t cs = server_max_budget(FW, 3, TS, 0);
    uint32_t cs_ds = server_max_budget(FW, 3, TS, 1);
    {
        uint32_t R[MAXP];
        printf("C  server Ts=%u: max RM-feasible Cs = %u (PS/SS), DS with jitter = %u\n", TS, cs, cs_ds);
        CHECK(cs == 3);
        CHECK(cs_ds == 2);
        CHECK(rta_with_server(FW, 3, cs, TS, 0, R) == 1 && R[0] == 5 && R[1] == 9 && R[2] == 100);
        CHECK(rta_with_server(FW, 3, cs + 1, TS, 0, NULL) == 0);
        CHECK(rta_with_server(FW, 3, cs, TS, TS - cs, NULL) == 0);       /* DS 를 Cs=3 으로 두면 지터 분석 실패 */
        CHECK(server_max_budget(A, 2, 4, 0) == 2);
        const ptask_t FULL[] = { { "x", 5, 10, 0 }, { "y", 10, 20, 0 } };
        CHECK(server_max_budget(FULL, 2, 4, 0) == 0);                    /* U = 1 이면 서버 자리가 없다 */
    }
    {
        uint32_t Rb[MAXP], Rj[MAXP];
        rta_with_server(FW, 3, cs, TS, 0, Rb);
        rta_with_server(FW, 3, cs_ds, TS, TS - cs_ds, Rj);
        srv_cfg_t cfg[5] = { { SRV_BG, 0, TS }, { SRV_PS, cs, TS }, { SRV_DS, cs, TS },
                             { SRV_SS, cs, TS }, { SRV_DS, cs_ds, TS } };
        const char *lab[5] = { "BG", "PS", "DS", "SS", "DS-safe" };
        const uint32_t gaps[2] = { 20, 10 };
        const char *wl[2] = { "normal (gap~20, U_ap~0.125)", "burst  (gap~10, U_ap~0.25)" };
        for (int w = 0; w < 2; w++) {
            uint64_t sum[5] = { 0 }; uint32_t cnt[5] = { 0 }, mx[5] = { 0 }, miss[5] = { 0 }, over[5] = { 0 }, bad = 0;
            uint32_t total = 0;
            const int SEEDS = 40;
            for (int sd = 1; sd <= SEEDS; sd++) {
                int na = gen_aperiodic(aq, 0x2026A05u + (uint32_t)sd * 7919u, gaps[w], 1, 4, H - 400);
                total += (uint32_t)na;
                for (int p = 0; p < 5; p++) {
                    sim_run(&s, FW, 3, cfg[p], aq, na, H);
                    sum[p] += s.a_sum; cnt[p] += s.a_done; miss[p] += s.misses; bad += s.inv_bad;
                    if (s.a_maxresp > mx[p]) mx[p] = s.a_maxresp;
                    const uint32_t *bound = (p == 4) ? Rj : Rb;
                    for (int i = 0; i < 3; i++) if (s.pmaxresp[i] > bound[i]) over[p]++;
                }
            }
            printf("C  %s: %d seeds x %u ticks, %u requests\n", wl[w], SEEDS, H, total);
            printf("   policy   Cs  ap_done  ap_avg  ap_max  periodic_miss  over_RTA\n");
            for (int p = 0; p < 5; p++)
                printf("   %-7s  %2u  %7u  %6.2f  %6u  %13u  %8u\n", lab[p], cfg[p].Cs, cnt[p],
                       cnt[p] ? (double)sum[p] / cnt[p] : 0.0, mx[p], miss[p], over[p]);
            CHECK(miss[0] == 0 && miss[1] == 0 && miss[3] == 0 && miss[4] == 0);
            CHECK(over[0] == 0 && over[1] == 0 && over[3] == 0 && over[4] == 0);   /* 관측 ≤ RTA */
            CHECK(bad == 0);
            /* 평균 응답 비교 (정수 교차 곱: sum_a/cnt_a < sum_b/cnt_b) */
            CHECK(sum[3] * cnt[1] < sum[1] * cnt[3]);      /* SS < PS */
            CHECK(sum[1] * cnt[0] < sum[0] * cnt[1]);      /* PS < BG */
            CHECK(sum[2] * cnt[3] < sum[3] * cnt[2]);      /* DS < SS (대신 보장이 없다) */
            if (w == 0) CHECK(miss[2] == 0 && cnt[4] == cnt[3]);
            if (w == 1) CHECK(miss[2] > 0);                /* 폭주 때 DS 는 log 를 놓친다 */
            if (w == 1) CHECK(cnt[4] < cnt[3]);            /* DS-safe 대역폭 0.2 < 부하 0.25 → 못 끝낸 요청 */
        }
    }

    if (sweep) {
        printf("sweep: aperiodic load vs avg response (seeds 1..10, Ts=%u, Cs=%u)\n", TS, cs);
        printf("   gap  U_ap     BG      PS      DS      SS   | periodic misses BG/PS/DS/SS\n");
        const uint32_t gaps[] = { 80, 40, 20, 15, 12, 10 };
        for (int g = 0; g < 6; g++) {
            double avg[4]; uint32_t ms[4];
            for (int p = 0; p < 4; p++) {
                srv_cfg_t c = { (srv_kind_t)p, p == 0 ? 0 : cs, TS };
                uint64_t sm = 0; uint32_t ct = 0; ms[p] = 0;
                for (int sd = 1; sd <= 10; sd++) {
                    int na = gen_aperiodic(aq, 0x51u + (uint32_t)sd * 104729u, gaps[g], 1, 4, H - 400);
                    sim_run(&s, FW, 3, c, aq, na, H);
                    sm += s.a_sum; ct += s.a_done; ms[p] += s.misses;
                }
                avg[p] = ct ? (double)sm / ct : 0;
            }
            printf("   %3u  %.3f %7.1f %7.1f %7.1f %7.1f  | %u/%u/%u/%u\n", gaps[g], 2.5 / gaps[g],
                   avg[0], avg[1], avg[2], avg[3], ms[0], ms[1], ms[2], ms[3]);
        }
        /* log 의 시작 오프셋 O 를 바꾸면 DS(3,10) 가 언제 log 를 놓치나 (burst, seeds 1..10) */
        printf("sweep: log offset O vs DS(%u,%u) periodic misses (burst gap~10)\n   O:", cs, TS);
        for (uint32_t O = 0; O < TS; O++) {
            ptask_t fw2[3] = { FW[0], FW[1], FW[2] }; fw2[2].O = O;
            srv_cfg_t c = { SRV_DS, cs, TS }; uint32_t m = 0;
            for (int sd = 1; sd <= 10; sd++) {
                int na = gen_aperiodic(aq, 0x51u + (uint32_t)sd * 104729u, 10, 1, 4, H - 400);
                sim_run(&s, fw2, 3, c, aq, na, H); m += s.misses;
            }
            printf(" %u:%u", O, m);
        }
        printf("\n");
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
